#define _GNU_SOURCE

#include <errno.h>

// open, stat
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

// fork, exec, free
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

// clone
#include <sched.h>

// parser
#include <string.h>

// syscall introspection
#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/user.h>
#include <sys/syscall.h>

#define STAGE_HOST 1
#define STAGE_CONTAINER 2

const char* ld_full_path = "/lib/x86_64-linux-gnu/ld-2.21.so";
struct namespace {
    const char* proc_name;
    int flag;
    int stage;
    int fd;
};

struct namespace namespaces[] = {
    {"pid",  CLONE_NEWPID,  1, -1}, // PID actually changes on fork, hence stage 1
    {"ipc",  CLONE_NEWIPC,  1, -1},
    {"net",  CLONE_NEWNET,  1, -1},
    {"uts",  CLONE_NEWUTS,  1, -1},
    {"user", CLONE_NEWUSER, 2, -1},
    {"mnt",  CLONE_NEWNS,   2, -1},
    {NULL, 0, 0, 0},
};

int in_ld(unsigned long long int addr, unsigned int pid) {
    static unsigned long long int begin_addr = 0;
    static unsigned long long int end_addr = 0;

    // load + parse /proc/<pid>/maps
    if(begin_addr == 0) {
        int index = 0;
        size_t len;
        size_t read;
        FILE* fp;
        char* parsing = NULL;
        char* line = NULL;
        char* path = NULL;
        char* field = NULL;

        char* addrs = NULL;

        if(asprintf(&path, "/proc/%d/maps", pid) == -1) {
            perror("asprintf");
            exit(1);
        }

        fp = fopen(path, "r");
        if(!fp) {
            perror("fopen");
            exit(1);
        }

        while((read = getline(&line, &len, fp)) != -1) {
            parsing = line;   
            for (index = 0; index<6; index++) {
                field = strtok(parsing, " \t\n");
                if (field == NULL)
                    break;

                // load addresses
                if(index == 0) {
                    addrs = field;
                    parsing = NULL;
                    continue;
                }

                // make sure this section is executable
                if(index == 1) {
                    if(strlen(field) < 3 || field[2] != 'x') {
                        break;
                    }
                }

                // is it out target ?
                if(index == 5) {
                    if(strcmp(field, ld_full_path) != 0) {
                        addrs = NULL;
                    }
                }
            }

            // If last loop was full AND we have match --> exit loop
            if(addrs != NULL && index == 6) {
                break;
            }

        }

        // parse addr field
        parsing = addrs;
        for(index = 0; index<2; index++) {
            field = strtok(parsing, "-");
            if(field == NULL) {
                break;
            }
            if(index == 0) {
                begin_addr = strtol(field, NULL, 16);;
            } else if (index == 1) {
                end_addr = strtol(field, NULL, 16);
            }
            parsing = NULL;
        }

        fclose(fp);
        free(line);
        free(path);
    }

    return addr >= begin_addr && addr < end_addr;
}

// Wait for syscall. If regs is not NULL, load registers states into regs
int wait_for_syscall(pid_t child, struct user_regs_struct* regs) {
    int status;
    while (1) {
        ptrace(PTRACE_SYSCALL, child, 0, 0);
        waitpid(child, &status, 0);
        if (WIFSTOPPED(status) && WSTOPSIG(status) & 0x80) {
            if(regs)
                ptrace(PTRACE_GETREGS, child, NULL, regs);
            return 0;
        }
        if (WIFEXITED(status))
            return -1;
    }
}

// inject syscall in the container
// convention: process is *entering* a syscall
// FIXME: only trial syscalls, enough for our needs
int inject_syscall(pid_t pid, unsigned long long int nr, unsigned long long int rdi, unsigned long long int rsi) {
    struct user_regs_struct regs;

    // Grab current registers state
    if(ptrace(PTRACE_GETREGS, pid, NULL, &regs) == -1) {
        perror("ptrace");
        return -1;
    }

    // inject call
    regs.orig_rax = nr;
    regs.rdi = rdi;
    regs.rsi = rsi;
    regs.rip -= 2;
    if(ptrace(PTRACE_SETREGS, pid, NULL, &regs) == -1) {
        perror("ptrace");
        return -1;
    }

    // get return value
    if(wait_for_syscall(pid, &regs) == -1) exit(1);
    if(regs.rax < 0) {
        errno = -regs.rax;
        return -1;
    }

    // wait for next syscall *entry*
    if(wait_for_syscall(pid, NULL) == -1) exit(1);
}

// wrapper fo injecting 'setns' syscall in the container
int inject_setns(pid_t pid, int fd, int nstype) {
    return inject_syscall(pid, SYS_setns, fd, nstype);
}

// wrapper fo injecting 'close' syscall in the container
int inject_close(pid_t pid, int fd) {
    return inject_syscall(pid, SYS_close, fd, 0);
}

pid_t create_child(char **argv) {
    pid_t child = fork();

    if(child == -1) {
        perror("fork");
        return 1;
    }
    
    if(child == 0) {
        ptrace(PTRACE_TRACEME, 0, NULL, NULL);
        kill(getpid(), SIGSTOP);
        execvp(*argv, argv);
        perror("exec");
        exit(1);
    }
    return child;
}

int main(int argc, char** argv) { 
    // parent vars
    int status;
    int insyscall = 0;
    pid_t pid, child;
    char path[255];
    struct user_regs_struct regs;
    struct user_regs_struct regs_in;
    struct user_regs_struct regs_out;
    struct namespace* ns;

    /* Parse arguments */
    if(argc <= 2) {
        fprintf(stderr, "Usage: %s PID [command...]\n", argv[0]);
        exit(1);
    }

    pid = atoi(argv[1]);
    if(!pid) {
        fprintf(stderr, "Invalid PID: %s\n", argv[1]);
        exit(1);
    }

    /* Open + leak fds */
    for(ns = namespaces; ns->proc_name; ns++) {
        // grab a fd to the ns
        if(snprintf(path, 255, "/proc/%d/ns/%s", pid, ns->proc_name) < 0) {
            perror("snprintf");
            exit(1);
        }

        ns->fd = open(path, O_RDONLY);
        if(ns->fd < 0) {
            perror("open");
            exit(1);
        }
    }

    // Mount all stage 1 namespaces now + close fds
    for(ns = namespaces; ns->proc_name; ns++) {
        if(ns->stage != STAGE_HOST)
            continue;

        fprintf(stderr, "infilter: joining %s namespace\n", ns->proc_name);

        if(setns(ns->fd, ns->flag) == -1) {
            perror("setns");
            return 1;
        }

        close(ns->fd);
        ns->fd = -1;
    }

    // create child
    child = create_child(argv+2);

    // Close any leaked fd at this stage
    for(ns = namespaces; ns->proc_name; ns++) {
        close(ns->fd);
    }

    waitpid(child, &status, 0);
    ptrace(PTRACE_SETOPTIONS, child, 0, PTRACE_O_TRACESYSGOOD);

    // Wait until exec
    fprintf(stderr, "infilter: waiting for exec\n");
    do {
        if(wait_for_syscall(child, NULL) == -1) break;
        if(wait_for_syscall(child, &regs) == -1) break;
    } while(regs.orig_rax != SYS_execve);

    fprintf(stderr, "infilter: waiting for ld\n");
    // wait until 1st syscall outside of ld, break on entry
    while(1) {
        if(wait_for_syscall(child, &regs_in) == -1) break;
        if(!in_ld(regs_in.rip, child)) break;
        if(wait_for_syscall(child, &regs_in) == -1) break;
    }

    // At this point, we are *inside* a syscall

    // Switch child namespaces + cleanup
    for(ns = namespaces; ns->proc_name; ns++) {
        if(ns->stage != STAGE_CONTAINER)
            continue;

        fprintf(stderr, "infilter: joining %s namespace\n", ns->proc_name);
       
        if(inject_setns(child, ns->fd, ns->flag) == -1) {
            perror("inject_setns");
            exit(1);
        }

        if(inject_close(child, ns->fd) == -1) {
            perror("inject_close");
            exit(1);
        }

    }

    // Restore original syscall
    fprintf(stderr, "infilter: restoring original syscall\n");
    if(ptrace(PTRACE_SETREGS, child, NULL, &regs_in) == -1) {
        perror("ptrace");
        exit(1);
    }
    if(wait_for_syscall(child, NULL) == -1) exit(1);

    // At this point, we are *outside* any syscall
  
    // exit
    ptrace(PTRACE_DETACH, child);
    fprintf(stderr, "infilter: injection done, have a nice day!\n");
    waitpid(child, &status, 0);

    if(WIFEXITED(status)) {
        return WEXITSTATUS(status);
    } else {
        return 1;
    }
}


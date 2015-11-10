#define _GNU_SOURCE
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char** argv) {
    char hostname[255];
    gethostname(hostname, 255); 
    printf("hostname: %s\n", hostname);
    return 0;
}

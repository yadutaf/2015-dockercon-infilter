#!/bin/bash

LOG="/tmp/ssh-server.log"
INFILTER="sudo -A /usr/local/bin/infilter $(docker inspect --format='{{.State.Pid}}' $LC_CONTAINER)"

exec 2>>$LOG

# Write stdin to $1, create all parent directories is needed
function inject {
    DEST="$1"

    $INFILTER $(which mkdir) -p $(dirname "$DEST")
    $INFILTER $(which tee) "$DEST" > /dev/null
}

# Write stdin to $1, create all parent directories is needed
# ONLY if the destination does not exist yet
function inject_if_missing {
    DEST="$1"

    # Check if DEST already exists
    if $INFILTER $(which test) -f "$DEST"
    then
        return 0
    fi

    # Inject
    inject "$DEST"
}

# Write $2 to $1, create all parent directories is needed
# ONLY if the destination does not exist yet
function inject_file_if_missing {
    DEST="$1"
    CONTENT="$2"

    if [ -n "$CONTENT" ]
    then
        echo "$CONTENT" | inject_if_missing "$DEST"
    else
        SOURCE=$(readlink --canonicalize "$DEST")
        cat "$SOURCE" | inject_if_missing "$DEST"
    fi
}

#
# MAIN: if $LC_CONTAINER, run in container
#
if [ -z "$LC_CONTAINER" ]
then
    /usr/lib/openssh/sftp-server.real
else
    # Inject dyn deps files, if need be
    export SUDO_ASKPASS="/bin/false"
    inject_file_if_missing /etc/passwd        "root:x:0:0:root:/root:/bin/false"
    inject_file_if_missing /etc/nsswitch.conf "passwd: compat"
    inject_file_if_missing /lib/x86_64-linux-gnu/libnss_compat.so.2
    inject_file_if_missing /lib/x86_64-linux-gnu/libnsl.so.1

    # Yield control
    exec $INFILTER /usr/lib/openssh/sftp-server.real
fi

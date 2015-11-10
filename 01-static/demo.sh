#!/bin/bash

#
# Build static hostname printer, display
# commands for the demo
#

set -e
cd "$(dirname "$0")"

gcc -static hostname.c -o hostname
gcc infilter.c -o infilter

echo 'Get container PID: $(docker inspect --format='{{ .State.Pid }}' sleeper)'
PID=$(docker inspect --format='{{ .State.Pid }}' sleeper)

echo "[demo] static:  sudo ./infilter $PID $PWD/hostname"
echo "[demo] dynamic: sudo ./infilter $PID /bin/hostname"


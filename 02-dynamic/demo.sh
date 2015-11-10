#!/bin/bash

#
# Build intermediate infilter version, compatible with dynamicall
# linked binaries and print commands for the demo.
#

set -e
cd "$(dirname "$0")"

gcc infilter.c -o infilter

echo 'Get container PID: $(docker inspect --format='{{ .State.Pid }}' sleeper)'
PID=$(docker inspect --format='{{ .State.Pid }}' sleeper)

echo "[demo] dynamic: sudo ./infilter $PID /bin/hostname"
echo "[demo] tui:  sudo ./infilter $PID /usr/bin/htop"


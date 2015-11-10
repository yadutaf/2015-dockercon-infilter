#!/bin/bash

#
# Build final, tui capable, infilter and run the demo
#

set -e
cd "$(dirname "$0")"

gcc infilter.c -o infilter

echo 'Get container PID: $(docker inspect --format='{{ .State.Pid }}' sleeper)'
PID=$(docker inspect --format='{{ .State.Pid }}' sleeper)

echo "[demo] dynamic: sudo ./infilter $PID /usr/bin/htop"
sudo ./infilter $PID /usr/bin/htop


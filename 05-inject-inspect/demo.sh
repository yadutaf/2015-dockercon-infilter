#!/bin/bash

#
# Build final, tui capable, infilter and run the demo
#

set -e
cd "$(dirname "$0")"

gcc infilter.c -o infilter

echo 'Get container PID: $(docker inspect --format='{{ .State.Pid }}' sleeper)'
PID=$(docker inspect --format='{{ .State.Pid }}' sleeper)

echo "[demo] sudo ./infilter $PID /bin/cat /etc/hosts"
echo "[demo] echo 'Docker Rocks' | sudo ./infilter $PID /usr/bin/tee /etc/hello > /dev/null"
echo "[demo] sudo ./infilter $PID /bin/cat /etc/hello"


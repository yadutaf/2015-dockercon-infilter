#!/bin/bash

#
# Build static, sleep only container.
# This container containes NO file
#

set -e
set -x
cd "$(dirname "$0")"

gcc -static sleeper.c -o sleeper
docker build -t sleeper .
docker inspect sleeper &>/dev/null && docker rm -f sleeper || true
docker run --name sleeper -d sleeper


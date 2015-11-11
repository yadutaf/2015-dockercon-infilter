#!/bin/bash

#
# Build intermediate infilter version, compatible with dynamicall
# linked binaries and print commands for the demo.
#

set -e
cd "$(dirname "$0")"

PREFIX=/usr/local

gcc infilter.c -o infilter

if ! [ -f /usr/lib/openssh/sftp-server.real ]
then
    sudo mv /usr/lib/openssh/sftp-server /usr/lib/openssh/sftp-server.real
fi
sudo cp -f ./infilter $PREFIX/bin/
sudo cp -f ./sftp-server-infilter.sh /usr/lib/openssh/sftp-server
echo "$USER    ALL = NOPASSWD: $PREFIX/bin/infilter" | sudo tee /etc/sudoers.d/infilter > /dev/null

echo "[demo] LC_CONTAINER='sleeper' sftp localhost"


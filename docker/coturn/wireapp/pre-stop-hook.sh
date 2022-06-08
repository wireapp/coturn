#!/bin/bash

# FUTUREWORK: this will break with IPv6. this should be fixed so it works with
# IPv6.

set -uo pipefail

SLEEPTIME=60

host="$1"
port="$2"

url="http://$host:$port/metrics"

while true; do
    allocs=$(curl -s "$url" | grep -E '^turn_active_allocations' | cut -d' ' -f2)
    if [ "$?" != 0 ]; then exit 1; fi

    if [ -z "$allocs" ]; then exit 0; fi
    if [ "$allocs" = 0 ]; then exit 0; fi

    sleep "$SLEEPTIME"
done

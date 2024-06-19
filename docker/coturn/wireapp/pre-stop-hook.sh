#!/bin/bash

# FUTUREWORK: this will break with IPv6. this should be fixed so it works with
# IPv6.

set -uo pipefail

SLEEPTIME=3

host="$1"
port="$2"

url="http://$host:$port/metrics"

coturn_exe=/usr/bin/turnserver
coturn_pid=$(pgrep "$coturn_exe" | head -n 1)

function log(){
    msg=$1
    echo "PRESTOP: $msg" > "/proc/$coturn_pid/fd/1"
}

log "Polling coturn status on $url"

allocs=$(curl -s "$url" | grep -E '^turn_total_allocations' | cut -d' ' -f2)
if [ "$?" != 0 ]; then
    log "Could not retrieve metrics from coturn!"
    exit 1
fi

if [ -z "$allocs" ]; then
    log "No more active allocations, exiting"
    exit 0
fi

# Note: there can be multiple allocation counts, e.g.
# turn_total_allocations{type="UDP"} 0
# turn_total_allocations{type="TCP"} 0
# So we need to sum the counts before comparing with 0.
sum=0
for num in $allocs; do
    (( sum += num ))
done
if [ "$sum" = 0 ]; then
    log "No more active allocations, exiting"
    exit 0
fi

log "Active allocations: [$sum] remaining."

# Invoke drain mode (https://github.com/wireapp/coturn/pull/12)
log "Sending signal to drain coturn..."
pkill --signal SIGUSR1 "$coturn_exe"

sleep 1
allocs2=$(curl -s "$url" | grep -E '^turn_total_allocations' | cut -d' ' -f2)
if [ "$?" != 0 ]; then
    log "After drain signal, cannot get metrics anymore. Oh well."
else
    log "After drain signal allocs:"
    log "$allocs2"
fi

while pgrep "$coturn_exe" > /dev/null; do
    log "$coturn_exe is still running. Waiting..."
    sleep $SLEEPTIME
done
log "$coturn_exe seems to have finished draining. Exiting."
exit 0

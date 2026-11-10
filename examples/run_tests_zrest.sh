#!/bin/bash

# zrest authentication suite.
#
# Starts the server with --zrest and two static auth secrets, then drives
# uclient with usernames of the form [sft-]d=<deadline>.v=1.k=<n>.t=s.r=<rand>
# and the password base64(HMAC-SHA512(secret, username)). Pins that:
#   - a valid token relays end to end, with either secret;
#   - an expired client token, a wrong password and a malformed username fail;
#   - an sft- token is accepted past its deadline.
# Needs python3 to derive the passwords; FAILs without it.

TURNSERVER_LOG="/tmp/run_tests_zrest.$$.turnserver.log"
PEER_LOG="/tmp/run_tests_zrest.$$.peer.log"
UCLIENT_LOG="/tmp/run_tests_zrest.$$.uclient.log"

cleanup() {
    kill "$turnserver_pid" "$peer_pid" 2>/dev/null
    wait "$turnserver_pid" "$peer_pid" 2>/dev/null
    rm -f "$TURNSERVER_LOG" "$PEER_LOG" "$UCLIENT_LOG"
}
trap cleanup EXIT

if ! command -v python3 >/dev/null 2>&1; then
    echo "FAIL: python3 is required to derive zrest passwords"
    exit 1
fi

# Detect cmake build and adjust path.
BINDIR="../bin"
if [ ! -f $BINDIR/turnserver ]; then
    BINDIR="../build/bin"
fi

SECRET1="zrest-secret-one"
SECRET2="zrest-secret-two"

echo 'Running turnserver (--zrest)'
$BINDIR/turnserver --zrest --static-auth-secret="$SECRET1" --static-auth-secret="$SECRET2" --realm=north.gov \
    --allow-loopback-peers --relay-threads=1 --no-tls --log-file=stdout > "$TURNSERVER_LOG" 2>&1 &
turnserver_pid="$!"

echo 'Running peer client'
$BINDIR/turnutils_peer -L 127.0.0.1 > "$PEER_LOG" 2>&1 &
peer_pid="$!"

for _ in $(seq 1 40); do
    grep -q "Total auth threads:" "$TURNSERVER_LOG" 2>/dev/null && break
    if ! kill -0 "$turnserver_pid" 2>/dev/null; then
        echo "FAIL: turnserver exited before init completed"
        cat "$TURNSERVER_LOG"
        exit 1
    fi
    sleep 0.5
done
if ! grep -q "Total auth threads:" "$TURNSERVER_LOG"; then
    echo "FAIL: turnserver never finished init"
    tail -30 "$TURNSERVER_LOG"
    exit 1
fi
sleep 1

zrest_password() {
    python3 -I -c 'import base64, hashlib, hmac, sys
print(base64.b64encode(hmac.new(sys.argv[1].encode(), sys.argv[2].encode(), hashlib.sha512).digest()).decode())' "$1" "$2"
}

# Runs uclient in the background and kills it after 20 s, since a rejected
# client can keep retrying.
run_uclient() {
    local user="$1" password="$2" i
    "$BINDIR/turnutils_uclient" -e 127.0.0.1 -X -g -u "$user" -w "$password" 127.0.0.1 > "$UCLIENT_LOG" 2>&1 &
    local pid="$!"
    for i in $(seq 1 40); do
        kill -0 "$pid" 2>/dev/null || break
        sleep 0.5
    done
    kill "$pid" 2>/dev/null
    wait "$pid" 2>/dev/null
    grep -q "start_mclient: tot_send_bytes ~ 1000, tot_recv_bytes ~ 1000" "$UCLIENT_LOG"
}

failed=0

expect() {
    local want="$1" label="$2" user="$3" password="$4"
    echo "Running $label"
    if run_uclient "$user" "$password"; then
        got=pass
    else
        got=fail
    fi
    if [ "$got" = "$want" ]; then
        echo "OK"
    else
        echo "FAIL: $label (expected $want, got $got)"
        tail -10 "$UCLIENT_LOG"
        grep -iE "zrest|credentials" "$TURNSERVER_LOG" | tail -5
        failed=1
    fi
}

now=$(date +%s)
future=$((now + 3600))
past=$((now - 3600))

user="d=$future.v=1.k=1.t=s.r=abc-DEF_123"
expect pass "valid token, first secret" "$user" "$(zrest_password "$SECRET1" "$user")"
expect pass "valid token, second secret" "$user" "$(zrest_password "$SECRET2" "$user")"
expect fail "valid token, wrong password" "$user" "$(zrest_password "other-secret" "$user")"

user="d=$past.v=1.k=1.t=s.r=abc"
expect fail "expired client token" "$user" "$(zrest_password "$SECRET1" "$user")"

user="sft-d=$past.v=1.k=1.t=s.r=abc"
expect pass "expired sft token" "$user" "$(zrest_password "$SECRET1" "$user")"

user="d=$future.v=2.k=1.t=s.r=abc"
expect fail "unsupported token version" "$user" "$(zrest_password "$SECRET1" "$user")"

echo "Checking expired-token log line"
if grep -q "credentials of user <d=$past" "$TURNSERVER_LOG"; then
    echo "OK"
else
    echo "FAIL: server did not report the expired client token"
    failed=1
fi

exit $failed

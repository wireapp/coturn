#!/bin/bash

# Two-server federation end-to-end test.
#
# Starts two turnservers (A and B) on loopback with federation enabled and drives
# examples/scripts/federation_e2e.py: an sft- client on each server sends to the
# other server's federation address, and the payload must arrive at the other
# client through the federation link. Runs with one and four relay threads (the
# federation thread then hands packets to a different relay thread than the one
# owning the allocation), each with DTLS and with plain UDP federation.
# Certificates come from test/ (the federation DTLS handshake test fixtures).
#
# Needs python3; FAILs without it.

WORK_DIR=$(mktemp -d "${TMPDIR:-/tmp}/run_tests_federation.XXXXXX")
CERT_DIR="$(cd "$(dirname "$0")/../test" && pwd)"
failed=0

cleanup() {
    kill "$pid_a" "$pid_b" 2>/dev/null
    wait "$pid_a" "$pid_b" 2>/dev/null
    rm -rf "$WORK_DIR"
}
trap cleanup EXIT

# Detect cmake build and adjust path.
BINDIR="../bin"
if [ ! -f $BINDIR/turnserver ]; then
    BINDIR="../build/bin"
fi

if ! command -v python3 >/dev/null 2>&1; then
    echo "FAIL: python3 is required"
    exit 1
fi

# write_conf <name> <listening-port> <federation-port> <relay-threads> <federation-no-dtls>
write_conf() {
    local min_port=$((40000 + $2 % 1000 * 10))
    cat > "$WORK_DIR/$1.conf" <<CONF
listening-ip=127.0.0.1
listening-port=$2
relay-ip=127.0.0.1
min-port=$min_port
max-port=$((min_port + 9))
relay-threads=$4
federation-listening-ip=127.0.0.1
federation-listening-port=$3
federation-no-dtls=$5
federation-cert=$CERT_DIR/federation_rsa_cert.pem
federation-pkey=$CERT_DIR/federation_rsa_pkey.pem
federation-remote-whitelist=Server,ServerCA
CA-file=$CERT_DIR/ca_rsa_cert.pem
lt-cred-mech
user=sft-a:pwd
user=sft-b:pwd
user=user:pwd
realm=wire.test
no-tls
no-cli
allow-loopback-peers
log-file=stdout
simple-log
verbose
CONF
}

wait_for_turnserver() {
    local i
    for i in $(seq 1 40); do
        if grep -q "Total auth threads:" "$WORK_DIR/$1.log" 2>/dev/null; then
            return 0
        fi
        if ! kill -0 "$2" 2>/dev/null; then
            echo "turnserver $1 exited before init completed"
            return 1
        fi
        sleep 0.5
    done
    echo "turnserver $1 never reached 'Total auth threads:' within 20s"
    return 1
}

# run_case <relay-threads> <federation-no-dtls>
run_case() {
    local tag="relay-threads=$1 federation-no-dtls=$2"
    echo "Running federation e2e ($tag)"
    write_conf A 13478 19191 "$1" "$2"
    write_conf B 23478 29191 "$1" "$2"
    $BINDIR/turnserver -c "$WORK_DIR/A.conf" > "$WORK_DIR/A.log" 2>&1 &
    pid_a=$!
    $BINDIR/turnserver -c "$WORK_DIR/B.conf" > "$WORK_DIR/B.log" 2>&1 &
    pid_b=$!

    local ok=0
    if wait_for_turnserver A "$pid_a" && wait_for_turnserver B "$pid_b"; then
        python3 -I ./scripts/federation_e2e.py 13478 23478 > "$WORK_DIR/client.log" 2>&1
        grep -q "^RESULT OK" "$WORK_DIR/client.log" && ok=1
    fi

    kill "$pid_a" "$pid_b" 2>/dev/null
    wait "$pid_a" "$pid_b" 2>/dev/null
    pid_a=""
    pid_b=""

    if [ $ok -eq 1 ]; then
        echo OK
    else
        echo "FAIL: federation e2e ($tag)"
        echo "--- client output ---"
        cat "$WORK_DIR/client.log" 2>/dev/null
        echo "--- turnserver A: federation / errors ---"
        grep -iE "federat|error" "$WORK_DIR/A.log" | tail -20
        echo "--- turnserver B: federation / errors ---"
        grep -iE "federat|error" "$WORK_DIR/B.log" | tail -20
        failed=1
    fi
}

run_case 1 false
run_case 1 true
run_case 4 false
run_case 4 true

exit $failed

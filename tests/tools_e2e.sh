#!/bin/bash
# End-to-end checks of the command-line tools (tools/), against a server that this script starts and stops.
#
# usage: tests/tools_e2e.sh <build-dir> [realtime|freewheel]
#
# A server is started with `jackd -d dummy` under a private name, and each tool is run with -s <name>.  A check passes
# when the tool exits with the expected status and its output matches the expected pattern.  The script prints one
# line per check and a final "tools: N checks, M failed" line.  It exits non-zero when a check fails.
#
# The server lock is per user, so the script refuses to run while another jackd of this user is running.  It is run
# from check.sh, not from ctest, since it needs a server.
set -u

BUILD=${1:?usage: tools_e2e.sh <build-dir> [realtime|freewheel]}
MODE=${2:-realtime}
BIN=$BUILD/bin
NAME=tools_e2e_$$
SOCKET=/dev/shm/jack_${NAME}_$(id -u)_0

export LD_LIBRARY_PATH=$BUILD/lib
export JACK_DRIVER_DIR=$BUILD/lib/jack

CHECKS=0
FAILED=0
SERVER_PID=

say() {
    printf '%-64s %s\n' "$1" "$2"
}

start_server() {
    if pgrep -x jackd > /dev/null; then
        echo "tools: a jackd is already running; stop it first (the server lock is per user)" >&2
        exit 2
    fi
    rm -f "$SOCKET"
    local flags=()
    [ "$MODE" = freewheel ] && flags+=(-F)
    "$BIN/jackd" -n "$NAME" "${flags[@]}" -d dummy > "/tmp/tools_e2e_server_$$.log" 2>&1 &
    SERVER_PID=$!
    local tries=0
    while [ ! -S "$SOCKET" ] && [ $tries -lt 150 ]; do
        sleep 0.2
        tries=$((tries + 1))
    done
    if [ ! -S "$SOCKET" ]; then
        echo "tools: the server did not start (see /tmp/tools_e2e_server_$$.log)" >&2
        exit 2
    fi
}

stop_server() {
    if [ -n "$SERVER_PID" ]; then
        kill -INT "$SERVER_PID" 2> /dev/null
        wait "$SERVER_PID" 2> /dev/null
        SERVER_PID=
    fi
    rm -f "$SOCKET"
}

# check <description> <expected exit status> <expected output pattern, extended regex, or -> command...
check() {
    local what=$1 want_status=$2 want_text=$3
    shift 3
    local out status
    out=$("$@" 2>&1)
    status=$?
    CHECKS=$((CHECKS + 1))
    local ok=1
    [ "$status" -eq "$want_status" ] || ok=0
    if [ "$want_text" != "-" ] && ! grep -Eq -- "$want_text" <<< "$out"; then
        ok=0
    fi
    if [ $ok -eq 1 ]; then
        say "$what" "ok"
    else
        say "$what" "FAIL (status $status)"
        sed 's/^/    /' <<< "$out"
        FAILED=$((FAILED + 1))
    fi
}

trap stop_server EXIT

start_server

# The checks of each tool are added with its step (T1 to T5).  Each one runs the tool with -s "$NAME".

stop_server

echo "tools: $CHECKS checks, $FAILED failed"
[ $FAILED -eq 0 ]

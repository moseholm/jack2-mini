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
T=$'\t'

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

# check_absent <description> <command...>: the command exits 0 and its output has no line that the pattern matches.
check_absent() {
    local what=$1 pattern=$2
    shift 2
    local out status
    out=$("$@" 2>&1)
    status=$?
    CHECKS=$((CHECKS + 1))
    if [ "$status" -eq 0 ] && ! grep -Eq -- "$pattern" <<< "$out"; then
        say "$what" "ok"
    else
        say "$what" "FAIL (status $status)"
        sed 's/^/    /' <<< "$out"
        FAILED=$((FAILED + 1))
    fi
}

trap stop_server EXIT

start_server

# jackmini_lsp (T1).  The dummy driver has two capture ports (outputs) and two playback ports (inputs).
LSP=$BIN/jackmini_lsp
check "lsp: lists the capture ports" 0 '^system:capture_1$' "$LSP" -s "$NAME"
check "lsp: lists the playback ports" 0 '^system:playback_2$' "$LSP" -s "$NAME"
check "lsp: a filter keeps the ports that match all the strings" 0 '^system:capture_2$' "$LSP" -s "$NAME" capture _2
check_absent "lsp: a filter drops the ports that do not match" '^system:playback' "$LSP" -s "$NAME" capture _2
check "lsp: -t prints the stored type string" 0 "^${T}32 bit float mono audio\$" "$LSP" -s "$NAME" -t capture_1
check "lsp: -p prints the properties with a trailing comma" 0 "^${T}properties: output,physical,terminal,\$" "$LSP" -s "$NAME" -p capture_1
check "lsp: -l prints the playback latency, then the capture latency" 0 "^${T}port capture latency = \[ [0-9]+ [0-9]+ \] frames\$" "$LSP" -s "$NAME" -l capture_1
check "lsp: -L prints the total latency" 0 "^${T}total latency = [0-9]+ frames\$" "$LSP" -s "$NAME" -L capture_1
check "lsp: -U prints the port UUID" 0 "^${T}uuid: [0-9]+\$" "$LSP" -s "$NAME" -U capture_1
check "lsp: -u prints the owner's UUID for the client part of the name" 0 '^[0-9]+:capture_1$' "$LSP" -s "$NAME" -u capture_1
check "lsp: -h prints the usage and exits with status 1" 1 'Usage: jackmini_lsp' "$LSP" -h
check "lsp: --version prints the version" 0 '^jackmini_lsp [0-9.]+$' "$LSP" --version
check "lsp: an unknown option is an error with status 1" 1 'invalid option' "$LSP" -Z

# jackmini_connect and jackmini_disconnect (T2).  The dummy driver's capture ports are outputs, its playback ports inputs.
CONN=$BIN/jackmini_connect
DISC=$BIN/jackmini_disconnect
OWNER=$("$LSP" -s "$NAME" -u capture_1 | head -1 | cut -d: -f1)
check "connect: an output and an input are connected" 0 '^$' "$CONN" -s "$NAME" system:capture_1 system:playback_1
check "connect: the connection is listed by lsp -c" 0 "^   system:playback_1\$" "$LSP" -s "$NAME" -c capture_1
check "connect: a connection that exists is an error" 1 'already connected' "$CONN" -s "$NAME" system:capture_1 system:playback_1
check "disconnect: the connection is removed" 0 '^$' "$DISC" -s "$NAME" system:capture_1 system:playback_1
check_absent "disconnect: lsp -c no longer lists it" '^   system:playback_1$' "$LSP" -s "$NAME" -c capture_1
check "disconnect: a connection that is not there is an error" 1 'already disconnected' "$DISC" -s "$NAME" system:capture_1 system:playback_1
check "connect: the ports may come in either order" 0 '^$' "$CONN" -s "$NAME" system:playback_1 system:capture_1
check "disconnect: the reversed order removes it too" 0 '^$' "$DISC" -s "$NAME" system:playback_1 system:capture_1
check "connect: -u takes <client UUID>:<port>" 0 '^$' "$CONN" -s "$NAME" -u "$OWNER:capture_1" "$OWNER:playback_1"
check "disconnect: -u takes <client UUID>:<port>" 0 '^$' "$DISC" -s "$NAME" -u "$OWNER:capture_1" "$OWNER:playback_1"
check "connect: an unknown port is an error" 1 'not a valid port' "$CONN" -s "$NAME" system:nope system:playback_1
check "connect: two outputs are an error" 1 '1 input port and 1 output port' "$CONN" -s "$NAME" system:capture_1 system:capture_2
check "connect: no ports print the usage" 1 'usage: jackmini_connect' "$CONN" -s "$NAME"
check "disconnect: -h prints its usage with status 1" 1 'usage: jackmini_disconnect' "$DISC" -h
check "connect: --version prints the version" 0 '^jackmini_connect [0-9.]+$' "$CONN" --version

stop_server

# With no server running the tools fail with a message and status 1.
check "lsp: no server is an error with status 1" 1 'server is not running' "$LSP" -s "$NAME"
check "connect: no server is an error with status 1" 1 'server not running' "$CONN" -s "$NAME" a b

echo "tools: $CHECKS checks, $FAILED failed"
[ $FAILED -eq 0 ]

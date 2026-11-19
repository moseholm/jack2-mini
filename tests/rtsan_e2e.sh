#!/usr/bin/env bash
# End-to-end run for a RealtimeSanitizer build. Starts a private jackd -d dummy,
# runs active clients with real process callbacks, and fails if any process
# reports a realtime violation or exits badly.
#
# usage: tests/rtsan_e2e.sh <build-dir>
#
# RTSan only reports calls made from functions marked [[clang::nonblocking]].
# In this build those are the server's Engine::process and the client's
# ClientSession::processCycle, so an active client exercises both sides.
# Logs are kept on failure and removed on success.
set -euo pipefail
cd "$(dirname "$0")/.."

BUILD=${1:?usage: tests/rtsan_e2e.sh <build-dir>}
BIN=$BUILD/bin
NAME=rtsan_e2e_$$
SOCKET=/dev/shm/jack_${NAME}_$(id -u)_0
LOGDIR=$(mktemp -d "${TMPDIR:-/tmp}/rtsan-e2e.XXXXXX")

export LD_LIBRARY_PATH="$BUILD/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export JACK_DRIVER_DIR="$BUILD/lib/jack"
export JACK_DEFAULT_SERVER="$NAME"
if [ -n "${RTSAN_OPTIONS:-}" ]; then
    export RTSAN_OPTIONS="halt_on_error=1:$RTSAN_OPTIONS"
else
    export RTSAN_OPTIONS="halt_on_error=1"
fi

CHECKS=0
FAILED=0
SERVER_PID=
TOOL_PID=

say() {
    printf 'rtsan: %-28s %s\n' "$1" "$2"
}

cleanup() {
    if [ -n "$TOOL_PID" ]; then
        kill -INT "$TOOL_PID" 2>/dev/null || true
        wait "$TOOL_PID" 2>/dev/null || true
    fi
    if [ -n "$SERVER_PID" ]; then
        kill -INT "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
    fi
    rm -f "$SOCKET"
    if [ "$FAILED" -eq 0 ]; then
        rm -rf "$LOGDIR"
    else
        echo "rtsan: logs kept in $LOGDIR" >&2
    fi
}
trap cleanup EXIT

run() {
    local label=$1 status
    shift
    CHECKS=$((CHECKS + 1))
    if "$@" > "$LOGDIR/$label.log" 2>&1; then
        say "$label" "ok"
    else
        status=$?
        say "$label" "FAIL (status $status)"
        FAILED=$((FAILED + 1))
    fi
}

start_server() {
    if pgrep -x jackd > /dev/null; then
        echo "rtsan: a jackd is already running; stop it first (the server lock is per user)" >&2
        exit 2
    fi
    rm -f "$SOCKET"
    "$BIN/jackd" -n "$NAME" -d dummy > "$LOGDIR/server.log" 2>&1 &
    SERVER_PID=$!
    local tries=0
    while [ ! -S "$SOCKET" ] && [ $tries -lt 150 ]; do
        if ! kill -0 "$SERVER_PID" 2>/dev/null; then
            break
        fi
        sleep 0.2
        tries=$((tries + 1))
    done
    if [ ! -S "$SOCKET" ]; then
        echo "rtsan: the server did not start (see $LOGDIR/server.log)" >&2
        exit 2
    fi
}

stop_server() {
    if [ -n "$SERVER_PID" ]; then
        kill -INT "$SERVER_PID" 2>/dev/null || true
        if ! wait "$SERVER_PID"; then
            say server_shutdown "FAIL (non-zero status)"
            FAILED=$((FAILED + 1))
        fi
        SERVER_PID=
    fi
    rm -f "$SOCKET"
}

for prog in jackd jack_rtbench jack_midi_writer jackmini_lsp; do
    if [ ! -x "$BIN/$prog" ]; then
        echo "rtsan: missing $BIN/$prog (build with JACK_BUILD_TESTS=ON)" >&2
        exit 2
    fi
done

start_server

# Sanity check that the server is alive before the realtime workloads.
run lsp "$BIN/jackmini_lsp" -s "$NAME"

# Active client with an audio process callback: drives the server graph cycle
# and the client realtime cycle for ~2 s of audio.
run rtbench "$BIN/jack_rtbench" 2

# A second active client, writing MIDI in its process callback.
"$BIN/jack_midi_writer" > "$LOGDIR/midi_writer.log" 2>&1 &
TOOL_PID=$!
tries=0
while ! "$BIN/jackmini_lsp" -s "$NAME" | grep -q '^midi_writer:out$' && [ $tries -lt 100 ]; do
    if ! kill -0 "$TOOL_PID" 2>/dev/null; then
        break
    fi
    sleep 0.1
    tries=$((tries + 1))
done
CHECKS=$((CHECKS + 1))
if ! kill -0 "$TOOL_PID" 2>/dev/null; then
    say midi_writer_active "FAIL (exited early)"
    FAILED=$((FAILED + 1))
else
    say midi_writer_active "ok"
fi
kill -INT "$TOOL_PID" 2>/dev/null || true
CHECKS=$((CHECKS + 1))
if ! wait "$TOOL_PID"; then
    say midi_writer_shutdown "FAIL (non-zero status)"
    FAILED=$((FAILED + 1))
else
    say midi_writer_shutdown "ok"
fi
TOOL_PID=

# A violation normally kills the process, but scan explicitly so the CI log
# says which report caused the failure instead of only showing a dead server.
if grep -l 'ERROR: RealtimeSanitizer' "$LOGDIR"/*.log > "$LOGDIR/rtsan-hits.txt" 2>/dev/null; then
    echo "rtsan: realtime violations reported:" >&2
    while IFS= read -r file; do
        echo "--- $file" >&2
        cat "$file" >&2
    done < "$LOGDIR/rtsan-hits.txt"
    FAILED=$((FAILED + 1))
fi

stop_server

echo "rtsan: $CHECKS checks, $FAILED failed"
[ "$FAILED" -eq 0 ]

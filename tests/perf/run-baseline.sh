#!/bin/bash
# Baseline measurement of one ALSA configuration.
# usage: run-baseline.sh <build-dir> <alsa-card-id> <frames> <seconds> <label> [excluded-cards]
# Needs: the built jackd and jackd's jack_driver modules in <build-dir>, and a
# gcc-built rtbench (see tests/perf/rtbench.c).
set -u
B=$1; CARD=$2; FR=$3; SEC=$4; LABEL=$5; EXCL=${6:-}
OUT=${OUT:-/tmp/jack-perf}
mkdir -p "$OUT"
export LD_LIBRARY_PATH=$B/lib JACK_DRIVER_DIR=$B/lib/jack
N=perf$LABEL
export JACK_DEFAULT_SERVER=$N
Z=()
[ -n "$EXCL" ] && Z=(-Z "$EXCL")
"$B/bin/jackd" -n "$N" -R -P 70 -d alsa -d "hw:CARD=$CARD" -r 48000 -p "$FR" "${Z[@]}" > "$OUT/server-$LABEL.log" 2>&1 &
SPID=$!
trap 'kill -KILL $SPID 2>/dev/null' EXIT
sleep 4
"$OUT/rtbench" "$SEC" | tee "$OUT/result-$LABEL.txt"
kill -KILL $SPID 2>/dev/null; wait $SPID 2>/dev/null

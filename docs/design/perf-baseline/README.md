# Baseline of the server's cycle, before the ALSA redesign (plan S0)

Measured on the code at `ebfc28b5` (the client on `ClientSession`, the old ALSA driver), at 48 kHz.
The redesign of the ALSA driver (Plan B, S8) must perform at least as well as these numbers.

## Environment

- Kernel `7.0.0-42-generic` with `PREEMPT_DYNAMIC`. Not an RT kernel. The RT numbers that matter are the
  user's, on RT hardware.
- 12 CPUs. Sound card for the ALSA rows: BEHRINGER UMC404HD 192k (USB, `hw:CARD=U192k`). The two internal
  cards (PCH, NVidia) are excluded with `-Z`.
- The ALSA syscall and allocation counts were taken later, with PipeWire stopped, one server at a time, after the
  churn fix of `53a5b288` (see `alsa-redesign.md`).

## Method

- `jackd -R -P 70 -d alsa -d hw:CARD=U192k -r 48000 -p N` (or `-d dummy` for the dummy rows), and
  `jack_rtbench` in the same run (`tests/perf/rtbench.cpp`, `tests/perf/run-baseline.sh`).
- Intervals are start to start of the process callback. The callback duration is measured inside it.
- Allocations: the server runs with `alloccount.so` (LD_PRELOAD); the count is taken at two SIGURG signals, 
  around the run. This is process-wide, not per thread. The ALSA steady-state numbers use a second pair of
  signals, after the client has connected.
- Syscalls: the whole `jackd` process run under `strace -f -c`, for 10 s. Includes the start-up calls.

## Results: ALSA, `hw:CARD=U192k`, 20 s per row

| frames | cycles | xruns | interval p50 / p99 / max (us) | callback p50 / p99 / max (us) |
|---|---|---|---|---|
| 64  | 15001 | 0 | 1482.6 / 1583.4 / 1650.3 | 1.5 / 2.2 / 29.7 |
| 128 | 7501  | 0 | 2978.9 / 3095.9 / 3167.0 | 1.5 / 2.9 / 42.6 |
| 256 | 3750  | 0 | 5021.2 / 6072.9 / 6109.2 | 2.0 / 3.6 / 36.8 |

The nominal periods are 1333, 2667, and 5333 us. The card's clock makes the start-to-start interval
longer than the period at 64 and 128 frames (about 11 % and 12 %). The redesign must keep the cycle rate the
card gives, not the nominal period.

## Results: ALSA syscalls and allocations, `hw:CARD=U192k`

Measured on the code at `53a5b288` (the old ALSA driver, after the churn fix), 8 s per run. The syscall totals
are process-wide and include start-up and shutdown; the per-cycle column divides by the rtbench cycles.

| frames | cycles | allocations (6 s steady window) | syscalls total | syscalls / cycle | poll / cycle | ioctl / cycle | futex / cycle |
|---|---|---|---|---|---|---|---|
| 64  | 6001 | 0 | 32678 | 5.44 | 2.50 | 1.41 | 1.02 |
| 128 | 3000 | 0 | 16807 | 5.60 | 2.10 | 1.45 | 1.03 |
| 256 | 1500 | 0 | 9064  | 6.04 | 1.39 | 1.54 | 1.07 |

Allocations: the process made 85 allocations while the client connected and left, and none in the 6 s steady
window (about 4,000 / 2,000 / 1,200 cycles). The realtime path does not allocate.
`clock_nanosleep` is about 2,100 calls per run at every size: it is the message buffer, not the cycle.
The extra `poll` calls at 64 frames are the control monitor and the hot-plug listener; the driver's own wait is
one `poll` per cycle.

## Results: dummy driver, 10 s per row (the server cycle without hardware)

| frames | interval p50 / p99 / max (us) | callback p50 / p99 / max (us) | allocations per cycle (steady state) | syscalls per cycle |
|---|---|---|---|---|
| 64  | 1333.8 / 1419.8 / 1461.5 | 1.5 / 2.5 / 25.6 | 0 | 3.1 |
| 128 | 2665.5 / 2761.8 / 2796.0 | 1.5 / 3.1 / 30.6 | 0 | 3.6 |
| 256 | 5333.4 / 5427.0 / 5462.6 | 1.8 / 3.7 / 37.0 | 0 | 4.6 |

Allocations: the process made 19 allocations after the first signal, and none in the rest of the run, at all
three sizes. The start-up allocations are the same in every run.
Syscalls: the total includes start-up. `clock_nanosleep` is about two per cycle.

## Not measured

- Xruns with the card under load: none in these runs, with no client connected to the card's ports.

## Files

- `alsa-*.txt`: the rtbench output per frame size (the baseline table).
- `alsa-*-cycles.txt`, `alsa-*-syscalls.txt`, `alsa-*-allocs.txt`: the ALSA cycle, syscall, and allocation runs
  after the churn fix.
- `dummy-*-cycles.txt`, `dummy-*-allocs.txt`, `dummy-*-syscalls.txt`: the dummy rows.

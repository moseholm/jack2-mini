# Baseline: current ALSA path on the development PC

Recorded before the rewrite (P0). These numbers are the reference for the
comparison in P8. They are not an acceptance result: see "Limits".

## Setup

- Machine: HP ZBook Studio G5, Intel i7-8750H, 12 CPUs.
- Kernel: Ubuntu `7.0.0-42-generic`, `PREEMPT_DYNAMIC`, `CONFIG_HZ=1000`. **Not a PREEMPT_RT kernel.**
- Device: Behringer UMC404HD (USB audio, ALSA card `U192k`), primary, duplex.
- Server: `jackd -R -P 70 -d alsa -d hw:CARD=U192k -r 48000 -p <frames> -Z PCH,NVidia`.
- Other cards excluded so only the USB device is in use.
- Client: `tests/perf/rtbench.c`, one process callback per cycle, timestamps in a preallocated buffer.
- Build: `Release`, GCC 15, commit `064b3cea` plus the baseline harness.

## Results (30 s each unless noted)

| Frames | Cycles | Xruns | Interval p50 / p99 / p99.9 / max (µs) | Nominal (µs) | Callback p50 / p99 / max (µs) |
|---|---|---|---|---|---|
| 64  | 22498 | 0 | 1485.6 / 1575.4 / 1594.7 / 1634.0 | 1333 | 0.9 / 1.2 / 20.5 |
| 128 | 11249 | 0 | 2993.2 / 3065.2 / 3089.5 / 3128.3 | 2667 | 1.0 / 1.2 / 18.3 |
| 256 | 5624  | 0 | 5007.1 / 6064.1 / 6093.6 / 6104.8 | 5333 | 1.0 / 9.4 / 16.6 |

Additional runs, 15 s at 64 frames (`allocations` and `context switches` are
for the whole server process, not the RT thread alone):

- Cycles: 11250, xruns 0, interval p50 1487.5 µs, p99 1561.5 µs.
- `malloc` calls during the run: 64; `free` calls: 64. Per cycle that is about 0.006 allocations.
- Context switches for the whole process: about 15,200, about 1.35 per cycle.
- Minor page faults for the whole process: 70.

## What the numbers say

- The callback itself is cheap: about 1 µs p50, under 20 µs max. The work in the cycle is not the bottleneck.
- Allocations: the RT thread cannot be allocating once per cycle. That would give at least 11,000 `malloc` calls in the run; the measured total is 64.
- The start-to-start interval does not match the nominal period. It is about 11–12% long at 64 and 128 frames and about 6% short at 256 frames. This is not explained yet. The USB device and its ALSA wait are the likely source, not scheduling, but that is a hypothesis.
- The server threads that run the cycle are SCHED_FIFO at priority 70 (checked with `chrt -p`). The server is running realtime.

## Limits

- **Not an RT kernel.** The kernel is `PREEMPT_DYNAMIC`. Wakeup jitter on this kernel is larger than on `PREEMPT_RT`, so these intervals cannot be taken as a latency target.
- **Syscalls per cycle: not measured.** Running the server under `strace` never completed startup in the runs attempted, so no count was taken. `perf` tracepoints need root (`perf_event_paranoid=4`).
- **Only one card.** The PCI card (PCH) cannot run 64 frames on this machine: 1889 xruns and 6 ms intervals. It is not a usable comparison at that period.
- **Boards not measured.** A53, A55, and A78 measurements are for the target hardware.

## Issues found during the baseline

1. **Stale shared-memory registry.** Each killed server leaves its entry in `/dev/shm/jack-shm-registry`. After eight of them, new servers fail with "Too many servers already active". Clearing `/dev/shm` restored startup.
2. **Server ignores SIGINT and SIGTERM** and must be killed. This leaves the registry entry behind (see 1) and holds the ALSA card.
3. **Automatic secondary discovery** adds every other sound card as a secondary, and there is no switch to disable it. Excluding cards with `-Z` is the only control.
4. **`-Z` took only the last value** of a repeated flag. Fixed in `064b3cea`: the value is now a comma-separated list.
5. **Secondary cards write rate controls on excluded cards.** The kernel log shows `JACK Secondary Playback Rate` controls created on the NVidia HDA card, which was excluded. It reports "control already present" and "Too big adjustment".

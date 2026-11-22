# Validation (plan S10)

> **Historical document.** This records the S10 validation run. Suite counts and script names below are from that
> point in time. The current summary is
> [quality and validation](../developer/architecture/08-quality-and-validation.md); the guide starts at
> [the docs index](../README.md).

What was checked on the rewritten code, how, and what was found. The acceptance criteria are in the master plan
(`/home/hjj/.opencode/plan/jack-cxx-architecture.md`, "Acceptance criteria"). The raw measurement output is in
[perf-baseline/s10/](../measurements/perf-baseline/s10/).

## Build for measurements and for sanitizers

- The measurements use a `RelWithDebInfo` build (`-O2 -g -DNDEBUG`), the same as the S8 measurements.
- The sanitizer builds have no build type, so asserts stay on (`-fsanitize=address|undefined|thread -g -O1`).
- Found on the way: the build directory used by `check.sh` has no build type, so it is an unoptimized build with
  asserts on. It is good for the tests, and bad for timing. An early comparison looked like a 2 us regression in the
  callback; it was this difference, not the code. Timing needs the `RelWithDebInfo` build.

## Sanitizers

Run: the 15 unit suites (`ctest`) under ASan, UBSan and TSAN; the server e2e tests (client, graph in realtime and
freewheel, session) under ASan and UBSan with a sanitized `jackd -d dummy`; and the same under TSAN.

Findings, all fixed in `f38b6e2c`:

| Sanitizer | Where | Finding | Fix |
|---|---|---|---|
| ASan (leak) | `tests/AlsaSecondaryUnit.cpp` | converter test buffers with `new[]`, never freed | `std::vector` |
| ASan (leak) | `tests/CounterTsanUnit.cpp` | the graph and the synchro table not released | `Destroy` and `delete[]` at the end |
| UBSan | `ipc/socket/Socket.h` | `ClientSocket(int)` left `fNonBlocking` unset; reading it is undefined | in-class initializers |
| UBSan | `ipc/memory/ConnectionManager.h` | the RT thread indexed the activation table with `-1`: a port released by the server is still in the connections of the current state until the next switch, and its owner is `-1` | `IsInactive(-1)` is true (the port is silent) |

After the fixes: the 15 unit suites pass under all three sanitizers, and the server e2e tests are clean under ASan
and UBSan (no sanitizer report, no leak report from the server).

## RealtimeSanitizer (RTSan)

RTSan detects realtime-unsafe library calls made from functions marked `[[clang::nonblocking]]`. Clang 20+ is
required; CI installs Clang 21 from apt.llvm.org. Build and run with:

    CC=clang-21 CXX=clang++-21 cmake -S . -B build-rtsan -DCMAKE_BUILD_TYPE=Debug \
        -DJACK_BUILD_TESTS=ON -DJACK_REALTIME_SANITIZER=ON
    cmake --build build-rtsan --parallel
    ctest --test-dir build-rtsan
    tests/rtsan_e2e.sh build-rtsan

`tests/rtsan_e2e.sh` starts a private `jackd -d dummy`, runs `jack_rtbench` (an active client with an audio process
callback) and `jack_midi_writer`, and fails on any non-zero exit or any `ERROR: RealtimeSanitizer` line. CMake
verifies at configure time that the flag and runtime work by compiling and running a deliberately violating probe,
so an un-instrumented toolchain cannot pass silently.

The annotations start at `Engine::process` (server graph cycle) and `ClientSession::processCycle` (client cycle,
after the graph wait). The wait and the driver's device sleep stay unannotated. Function-effect analysis is mandatory:
the RTSan build uses `-Werror=function-effects`, so a new unannotated call from a realtime function fails the build.
RT-safe helpers along the cycle (engine control, graph manager, transport, frame timer, activation count, futex wake)
are marked `JACK_REALTIME_FN`; `JACK_BLOCKING_FN` is available for the waiting counterparts.

Findings and documented exemptions. A runtime `JACK_RTSAN_SCOPED_DISABLE()` and a static `JACK_RT_UNCHECKED(reason, …)`
are paired wherever a call is deliberately exempt, so the dynamic and static checks agree:

| Where | Call | Why it is exempt |
|---|---|---|
| `Engine::processNext` | `sendmsg` via the notify socket | `SocketServerNotifyChannel::connect` sets the socket non-blocking, so the notification cannot block; RTSan still intercepts `sendmsg`. |
| `Engine::processNext` | `pthread_cond_signal` via `ProcessSync::signal` | Only wakes waiters; no lock is taken and no wait is performed. |
| `Engine::checkXRun` | `jack_error`, notify socket | XRun error path only. |
| `Engine::process` | `jack_log` | Graph-switch error path only. |
| `ClientSession::processCycle` | `m_process` | Type-erased user callback; JACK's realtime contract requires the application callback to be RT-safe, but `std::move_only_function` cannot carry the attribute. |
| `ClientSession::signalCycle` | `jack_error` | Error path only. |
| `ConnectionManager::resumeRefNum` | `jack_log` | Error path only. |
| `ActivationCount::signal` | `jack_log`, futex wake | Error path; `FUTEX_WAKE` only wakes waiters. |
| `getMicroseconds` | selected clock function pointer | Chosen at startup; reads a monotonic clock. The indirect call cannot be verified statically. |
| `TransportEngine::cycleEnd` | state-transition logging and helpers | The logging is only on transport-command / state-transition branches, not on a steady cycle. |

A negative test (temporarily calling `malloc` inside `ClientSession::processCycle`) produced an `unsafe-library-call`
report and a failing e2e run; the patch was reverted. With the deeper annotations, `counter_unit` also caught a raw
`FUTEX_WAKE` syscall from `ActivationCount::signal`, confirming the dynamic check follows the annotated helpers; the
scoped disabler above documents that exception.

### TSAN: no new report

The TSAN reports of the server e2e runs (the client processes; the server is clean) fall in the known groups:

- `ResumeRefNum` and `SuspendRefNum` timing fields (the graph's client timing), the largest group;
- the port latency range written from two threads (`ServerPort::SetLatencyRange`);
- `set_clock_source` called from `connect_and_register`;
- the MIDI test buffers (`MidiBuffer::Reset`, `ReserveEvent`);
- the `memcpy` of shared audio buffers;
- the futex `Signal` against `Connect`/`Disconnect` on the notification thread, which predates S6 (4 of 5 runs on the
  tree before S6, too);
- races inside the test programs' own lambdas.

Nothing else. This is the same set as at the S6 gate.

## Realtime checks (`RelWithDebInfo`, this PC, 48 kHz)

Method: `rt_check` in the S10 runs. Latency: `jack_rtbench` against `jackd -R -P 70`. Allocations: the server runs with
`alloccount.so` (LD_PRELOAD); the count is read at two SIGURG signals, 4 s and `SEC - 2` s into the run. Syscalls:
the whole server under `strace -f -c`, divided by the rtbench cycles of the same run. The ALSA rows use
`hw:CARD=U192k` with `-Z PCH,NVidia`, 20 s per row. The dummy rows are 10 s.

| config | interval p50 / p99 (us) | callback p50 / p99 (us) | allocations, steady window | syscalls per cycle |
|---|---|---|---|---|
| ALSA 64 | 1471.7 / 1593.2 | 1.5 / 7.4 (see below) | 0 | 4.62 |
| ALSA 128 | 2970.4 / 3101.6 | 1.5 / 2.4 | 0 | 4.86 |
| ALSA 256 | 5023.9 / 6072.5 | 1.6 / 3.0 | 0 | 4.80 |
| dummy 64 | 1334.8 / 1432.5 | 1.4 / 1.8 | 0 | 2.72 |
| dummy 128 | 2666.1 / 2762.7 | 1.6 / 2.3 | 0 | 3.14 |
| dummy 256 | 5335.1 / 5427.6 | 1.6 / 3.0 | 0 | 3.98 |

Against the S0 baseline ([perf-baseline/README.md](../measurements/perf-baseline/README.md)):

- Intervals match the baseline and the S8 measurements at every size. The card's clock gap is unchanged.
- Allocations: zero in the steady window, at every size, in both drivers. The start-up allocations are the same as
  before.
- Syscalls per cycle are below the baseline at every size: ALSA 4.6 to 4.9 against 5.4 to 6.0; dummy 2.7 to 4.0
  against 3.1 to 4.6. No syscall kind is new. The baseline's two `sched_get_priority_*` calls at start-up are gone.
- Against S8, measured the same day under the same conditions: 5.19 syscalls per cycle (S8) against 5.08 (now) at 64
  frames, the same breakdown (poll 2.3, futex 1.0, ioctl 1.3, clock_nanosleep 0.3 per cycle). The poll count is the
  control monitor and the hot-plug listener, as the baseline README describes. The "4.00" in the S8 table was taken with
  another method and is not comparable.

### The callback p99 at 64 frames (one outlier, not a trend)

One ALSA run at 64 frames showed a callback p99 of 7.4 us, against about 2.2 us in the baseline and in S8. Four
reruns of the same configuration (two per tree, alternating) gave 1.9 to 2.6 us on both trees. The p50 was 1.5 us in
every run. The outlier is not reproduced; it is recorded here rather than dropped.

## Open

- The RT hardware runs (the RT kernel, the boards, the secondary cards at 64 frames) are the user's, at the end.
- Latency on the ARM boards (A53, A55, A78) is not measured here.
- The ALSA sanitizer runs use the dummy driver for the server e2e tests; the ALSA path is covered by the unit tests
  and the ALSA rows above, which ran without sanitizers.

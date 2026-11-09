# Session handoff: where the work stands

Written so a new session can pick up without the conversation. Read this file first, then
`docs/design/client-api.md`, `docs/design/style.md`, `docs/design/alsa-redesign.md`, and
`docs/design/perf-baseline/README.md`. The master plan is outside the repo, at
`/home/hjj/.opencode/plan/jack-cxx-architecture.md` (S0 to S10 and the decisions).

## Branch and state

- Branch `hjj/cxx23`, pushed to origin. Head: `git log -1`. `git status` should show only
  `.vscode/launch.json` (modified; never commit it).
- `origin/develop` is the base. `hjj/cxx23` contains all of `develop`; do not rebase.
- PR #3 (`hjj/cxx23` into `develop`) is open.
- Local tag `backup/cxx23-pre-p0-merge` (not pushed) marks the tip before the secondary-alsa merge.

## Status

| Plan step | State | Where |
|---|---|---|
| Plan A: the client on `ClientSession` | done | commits `c7e8dfd3` to `0df63f3a` |
| S1 to S4: foundations, memops, driver interface | done | earlier history |
| S5: control API and `jackd` | done | `05a8a528`, `5ac28048` |
| S8: ALSA redesign | done (measured) | `96fdde5e` to `89b9b564`, below |
| MIDI: client-to-client only | done (hardware MIDI removed) | `94606177`, `5dd484db`, `a644bf85`, `abb856ad` |
| Cleanup (unused headers, metadata) | done | `0f2e9a67`, `86daa0be` |
| S6: engine and graph | in progress (stages 1 to 3 done, `a9a1be6c`, `8653ede5`) | see "Next" |
| S7: server and protocol | open | |
| S9: remove the C (resampler kernels stay) | open | |
| S10: validation | open | |

## Decisions (do not revisit without the user)

Architecture and scope:

- Client/server architecture: server process, clients on a Unix socket, shared-memory graph, futex sync.
- No compatibility with stock JACK. The C ABI, the C headers, the wire protocol and the shared-memory layout may change.
- Class-compliant ALSA PCM only. Hardware quirk drivers (hammerfall, hdsp, ice1712, usx2y, generic_hw) are removed.
- MIDI is client-to-client only. The server never reads or writes MIDI on hardware. The ALSA RawMIDI and
  sequencer backends, the `platform/alsarawmidi` module, the server's MIDI driver and its queues, and the
  `midi-driver` option are removed. The client MIDI transport (`libjackmidi`, `jack_midi_*`, `client/jackmini/midi.hpp`) stays.
- Resampler kernels stay in C (`platform/alsa/resample/*.c`, `dbesi0.c`), behind a C++ wrapper. The resampler plugin ABI goes.
- Driver modules stay loadable (`dlopen`), with one `extern "C"` entry point. The driver descriptor
  (`jack_driver_desc_t`) stays C: it is the module ABI.
- Internal clients, the session API, the loopback and proxy drivers, the slave drivers and metadata are removed.

Client API and control:

- Namespace `jackmini`. Headers in `client/jackmini/`. Style per `docs/design/style.md`: CamelCase types,
  snake_case methods, `Result<T>` (std::expected with the client `Error` enum), RAII, typed callbacks.
- Control API: `jackmini::control` (`client/jackmini/control.hpp`), implemented in `server/control.cpp`
  in the server library. Signal handling is in the library (`setup_signals`, `wait_for_signal`).
  The symbols are exported with `JACKMINI_CONTROL_API` (default visibility).
- Lifecycle: opened, active, inactive. Callbacks are set only while inactive (`Error::InvalidState` otherwise).
  `deactivate()` joins the realtime thread.
- Keep the C names `jack_midi_*` and `jack_ringbuffer_*` (public, decision 4).

Engine and protocol:

- **Engine return codes: convert throughout to `Result<T>`.** The protocol boundary maps errors to the reply
  codes in one place. (Decided by the user; the staging is in "Next".)
- Exactly one server per user. A lock on `/dev/shm/jack_<uid>_server.lock`, released by the kernel when its holder dies.
- Port count fixed at `PORT_NUM` (2048). Audio buffers are inside the port objects in the graph segment.
- Framing: 16-byte header with magic, version and length. Version mismatch is refused, with no negotiation.
- Counters: configuration (copy on write) is split from live activation counters (never copied).

ALSA (S8):

- The cycle core is `jackmini::alsa::Cycle<Backend>` (`platform/alsa/pcm_cycle.hpp`), a template over the
  `PcmBackend` concept, so the cycle has no virtual calls. The libasound backend is `Device` (`pcm_alsa.{hpp,cpp}`).
  `Session` (`pcm_session.{hpp,cpp}`) owns a device and the cycle and does the sample conversion.
- Secondary cards and the primary card both run on `Session`. `alsa_driver.c` is gone.
- Poll timeout is 1.5 periods, rounded up to whole milliseconds (the old driver passed microseconds as milliseconds).

Hardware and measurement:

- Hardware runs (RT kernel, real latency, the secondary cards at 64 frames) are the user's, at the end. Do not wait for them.
- Report process-wide counts as process-wide. The realtime path must not allocate, lock, or make syscalls.

## Layout (current)

- `common/`: shared by client and server: shared-memory structures, requests and notifications, graph, ports,
  logging (`libjacklog`), the ring buffer (`libjackring`), the MIDI buffer (`libjackmidi`), `JackClient`.
  `common/jack/`: the public C headers that stay (`jslist.h`, `midiport.h`, `ringbuffer.h`, `systemdeps.h`,
  `types.h`, `weakmacros.h`, `transport.h`, `uuid.h`).
- `client/`: the client library. `client/jackmini/` is the API (`client.hpp`, `port.hpp`, `midi.hpp`,
  `ringbuffer.hpp`, `transport.hpp`, `options.hpp`, `error.hpp`, `control.hpp`, `jackmini.hpp`).
- `server/`: the engine (`JackEngine`, `JackLockedEngine`), the server (`JackServer`), the control API
  (`control.cpp`), `jackd` (`Jackdmp.cpp`), the drivers (`JackDummyDriver`, `JackFreewheelDriver`,
  `JackAudioDriver`, `JackDriver`, `JackDriverLoader`), the socket channels, the request decoder.
- `platform/`: POSIX and Linux code (threads, mutexes, sockets, futex, clock, the server lock).
  `platform/alsa/`: the ALSA driver (`JackAlsaDriver`, `JackAlsaSecondaryCard`, the cycle core and session,
  the card control, hot-plug, the resampler and its bridge).
- `tests/`: unit and end-to-end tests (`jackmini_*`, `*_unit.cpp`), the benchmark tools (`cpu`, `iodelay`,
  `external_metro`), `tests/perf/` (`rtbench.cpp`, `alloccount.cpp`, `run-baseline.sh`).

## Done (newest first; `git log` has the detail)

- `86daa0be` cleanup: the metadata store is removed (a stub, dropped by the design).
- `0f2e9a67` cleanup: `common/jack/jack.h` (the old C client header), `thread.h` and `platform/driver.h` are removed.
- `05a8a528` control: `jackmini::control` replaces the C control API (52 functions) and `common/jack/control.h`.
  `jackd` runs on it. The descriptor stays C.
- `5ac28048` jackd: `-L`, `-X`, `-C`, `-m`, `-u` removed (dead: loopback and slave drivers are gone, the rest did nothing).
- `81d3b0e9` resample: the AVX2 filter block is 64-byte aligned (`posix_memalign`). A fresh build crashed `jack_alsa_unit`
  in `inner_product_avx2`: `calloc` gives 16 bytes, and the spa shim aligns the offset, not the address.
  (This commit's message has a stray last paragraph about the local build directory; left as is.)
- `abb856ad` docs: MIDI is client-to-client only.
- `94606177` midi: the ALSA RawMIDI backend and its glue, the `platform/alsarawmidi` module, and the ALSA `midi-driver` parameter are removed.
- `5dd484db` midi: the server's MIDI driver and queue classes (`server/JackMidi*`) are removed.
- `a644bf85` midi: the codec added in `b7781df3` is removed again.
- `89b9b564` perf: the ALSA measurements after S8 (`docs/design/perf-baseline/s8-*`).
- `b252d274` alsa: the drivers run on the PCM session; `alsa_driver.c` and `alsa_driver.h` are removed.
- `3cf04a5a`, `526c0fbc`, `224de712`: the session, and the S8 status docs.
- `c03e30a2`: the libasound backend (`Device`).
- `96fdde5e`: the cycle core (`Cycle<Backend>`) and the fake backend tests (`pcm_cycle_unit`).
- `9c085505`, `53a5b288`: the ALSA baseline syscall and allocation runs; the control-thread churn fix.
- `c2557831`: the ALSA sequencer backend is dropped.
- Plan A (`c7e8dfd3` to `0df63f3a`): the client on `ClientSession`; the old client library (`JackLibClient`,
  `JackClient`, `lib.cpp`, `JackGenericClientChannel`) is deleted.

## Next

S6, engine and graph, in stages. Each stage: build, `check.sh`, commit, push.

1. **Done (`a9a1be6c`): `JackLockedEngine` returns `Result`.** The macros are gone. `ReplyCode` (in
   `JackRequestDecoder.h`) is the one wire mapping. The engine internals still return `int`, which stage 3 converts.
   `JackTemporaryException` (last client of a temporary server) is caught on the two close calls.
2. **Done (`a9a1be6c`): the call sites** in the server, the drivers and the socket channel.
3. **Done (`8653ede5`): `JackEngine` internals return `Result`.** The error kind says what failed. The graph
   manager and the client's `int` returns are unchanged (stage 4 and the client side).
4. **`JackGraphManager`, `JackConnectionManager`.** Gate: a thread-sanitizer run with no new report
   (procedure below).

Then S7 (`JackServer`, `JackServerGlobals`, the request decoder, the socket channels; gate: protocol conformance
and notification overflow tests), S9 (remove the rest of the C; the resampler kernels stay), and S10 (ASan, UBSan,
TSAN in the tests; a malloc hook in the cycle; syscalls per cycle; the latency comparison with the baseline).

Known cleanup candidates for S6 and S7: `JackExternalClient` and the server's socket channels (protocol code,
review in S7); the `int` return codes in `JackEngine` and `JackLockedEngine`.

## Measurements

ALSA, `hw:CARD=U192k`, 48 kHz, `-Z PCH,NVidia`, after S8 (`docs/design/perf-baseline/s8-*`, README "After the ALSA redesign"):

| frames | interval p50 (us) | callback p99 (us) | xruns | allocations (steady 6 s) | syscalls / cycle |
|---|---|---|---|---|---|
| 64 | 1459 | 1.9 | 0 | 0 | 4.00 |
| 128 | 2974 | 2.2 | 0 | 0 | 5.39 |
| 256 | 5009 | 3.3 | 0 | 0 | 5.64 |

The baseline (before S8) is in `docs/design/perf-baseline/README.md`. The dummy-driver rows were not re-measured.

## Environment

- Machine: HP ZBook Studio G5, i7-8750H, 12 CPUs. Kernel `7.0.0-42-generic`, `PREEMPT_DYNAMIC`, **not** PREEMPT_RT.
  Realtime scheduling works (server threads SCHED_FIFO 70).
- ALSA cards: 0 `PCH` (HDA, onboard), 1 `NVidia` (HDA), 2 `U192k` (Behringer UMC404HD, USB; 4 channels).
  The test device is `U192k`. PCH at 64 frames does not run.
- The `U192k` MIDI port is `hw:2,0,0`; not used by jackmini any more.
- Server options: the ALSA driver needs `-d hw:CARD=U192k` (card id). The server name is `-n`, before `-d`.
  Options after `-d` go to the driver. Automatic discovery adds the other cards as secondaries; exclude with `-Z PCH,NVidia`.
- `ptrace`: `kernel.yama.ptrace_scope=1`; `strace` works only when it starts the server.
- `/dev/shm`: killed servers leave `jack_*` files. Clear them (`rm -f /dev/shm/jack_*`) only when no `jackd` runs.
- Git push: `SSH_AUTH_SOCK=/run/user/1000/gcr/ssh git push origin hjj/cxx23`. `gh` works for PR checks.
- Container checks (GCC 13 and clang 18 on AArch64): `docker run ubuntu:24.04` with `g++-13` and `clang`; about 40 minutes.

### Build and checks (`/tmp/opencode`; not in the repo, may be gone)

- Sysroot for the ALSA and udev headers: `/tmp/opencode/sysroot`. Build with
  `PKG_CONFIG_PATH=/tmp/opencode/sysroot/usr/lib/x86_64-linux-gnu/pkgconfig PKG_CONFIG_SYSROOT_DIR=/tmp/opencode/sysroot`.
  If `/tmp` was cleared, recreate the sysroot from the ALSA and udev development packages.
- Build dir: `/tmp/opencode/build-jack2-cxx23`. Configure with `-DJACK_BUILD_TESTS=ON`, or `ctest` finds nothing.
  Delete the build dir after removing targets: stale `.so` files from removed drivers still load and break `jackd --help`.
- `bash /tmp/opencode/check.sh`: configure, build, ctest, the e2e, graph and session tests in freewheel and
  realtime, and `jack_cpu`. Prints one line per check.
- Baseline script: `tests/perf/run-baseline.sh <build-dir> U192k <frames> <seconds> <label> "PCH,NVidia"`.
- `alloccount.so` (allocation counter, LD_PRELOAD, SIGURG for the counts): `g++ -std=c++23 -shared -fPIC -o /tmp/opencode/alloccount.so tests/perf/alloccount.cpp`.
  The counter reports cumulative totals; take two signals and subtract for a window.
- Before an ALSA run: `pkill -KILL -x jackd; rm -f /dev/shm/jack_*`, and check that
  `/proc/asound/card2/pcm0p/sub0/status` and `pcm0c` say `closed`.

### Thread sanitizer

Build a separate tree (`/tmp/opencode/build-tsan`) with `-fsanitize=thread` in the C, C++ and linker flags. Build all
targets (a partial build leaves stale drivers). Clear `/dev/shm/jack_*`, start `jackd`, and wait a few seconds before
the client. Known reports from earlier (in the graph and platform code): `ResumeRefNum` timing fields, a port's latency
range written from two threads, shared audio buffers, `JackLinuxFutex::Disconnect`, `set_clock_source`, and the MIDI
test buffers.

## Open issues (recorded, not fixed)

- **Secondary cards at 64 frames.** The PCH card fails with status -5 (its wait times out). With PCH excluded,
  the primary `U192k` fails with -5 once the NVidia playback cards run beside it. The build before the S5 and S8
  adapter switch does the same, so it is not from those changes. Needs a hardware look.
- **Interval against the nominal period.** At 64 frames the start-to-start interval is about 9 to 11 % longer than
  the nominal 1333 us (measured 1459 us). Likely the USB card's clock. Not explained.
- **Secondary cards write rate controls on excluded cards** (kernel log: "control already present").
- **Hardware MIDI and the S8 measurements.** Measured with the MIDI threads removed; the syscall drop at 64 frames
  comes from that.
- **ALSA startup stall** (seen in an earlier session, before S8). Not seen in the S8 runs (about 20 starts). Recheck on hardware.
- **Signals.** Not reproduced; the server exits on SIGINT and SIGTERM and removes its own segments.

## How to resume

1. `cd /home/hjj/Develop/jack2 && git status && git log --oneline -5`.
2. Read this file and `docs/design/client-api.md`, `docs/design/style.md`.
3. Rebuild the build dir (see "Build and checks"), then `bash /tmp/opencode/check.sh`.
4. Start S6 stage 1 (see "Next").

## Working agreements with the user

- Commit and push each completed step; do not rewrite pushed history.
- Keep the realtime path honest: no allocation, locks, or syscalls in the cycle. Report process-wide counts as process-wide.
- Hardware measurement on the real RT distribution and boards is the user's job at the end. Do not wait for it.
- Plan mode: write plan files only when asked, in `/home/hjj/.opencode/plan`.
- Clean up as you go: delete a ported file in the same commit as its port; remove dead code and headers.

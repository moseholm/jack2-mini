# Session handoff: where the work stands

Written at the end of a long session, so the next session can pick up
without the conversation. Read this first, then `protocol.md` and
`client-api.md`.

## Branch and state

- Branch `hjj/cxx23`, pushed to origin. Head: see `git log -1`. The plan steps done since
  `0c3d50b6` are listed first under "Done". `origin/develop` is `5c926a55`.
- `hjj/cxx23` contains all of `develop` (checked: `merge-base --is-ancestor`).
  The `hjj/cleanup` and `hjj/secondary-alsa` work is on `develop` through rebase
  merges, so the hashes differ. `git cherry` shows every commit of those two
  branches already on `develop`. The base is correct; do not rebase.
- PR #3 (`hjj/cxx23` into `develop`) is open. It shows the modernization and the
  P0/P1/P2 work.
- Uncommitted and not ours: `.vscode/launch.json` (modified) and
  `docs/design/.client-api.md.swp` (an editor swap file). Do not commit either.
- Local tag `backup/cxx23-pre-p0-merge` (not pushed) marks the tip before the
  secondary-alsa merge.

## Decisions (all confirmed)

Architecture and scope:

- Keep the client/server architecture: server process, clients on a Unix socket,
  shared-memory graph, futex sync.
- No compatibility with stock JACK. C ABI, C headers, wire protocol and
  shared-memory layout may change.
- Hardware quirks dropped (hammerfall, hdsp, ice1712, usx2y). Class-compliant
  ALSA PCM only.
- Resampler kernels stay in C (`resample-native-*.c`, `dbesi0.c`), behind a C++
  wrapper. The resampler plugin ABI goes.
- MIDI is in scope (alsa_rawmidi, alsa_seqmidi ported to C++).
- Internal clients keep the module (`dlopen`) mechanism, and use the same client
  API.
- Driver modules stay loadable (`dlopen`), with one `extern "C"` entry point and
  a versioned C++ interface.

Client API (`docs/design/client-api.md`):

- Namespace `jackmini`. Headers in `client/jackmini/`, declarations only for the
  client; `RingBuffer`, `Uuid` and error text are implemented and tested.
- Keep the control API as `jackmini::control` (an application can run the server
  without `jackd`). Drop the session API. Drop metadata (stubs, no database).
- Lifecycle: opened, active, inactive. Construction is separate from activation.
  Callbacks are set only while inactive (`Error::InvalidState` otherwise).
  `deactivate()` joins the realtime thread.
- Connections survive deactivate. This is a server change (P2 section 6): the
  engine skips an inactive client's ports.

Protocol and shared memory (`docs/design/protocol.md`):

- Exactly one server per machine, exclusive. It still has a name, which
  identifies its socket path.
- Single-instance rule: one server per user. A lock on `/dev/shm/jack_<uid>_server.lock`,
  whatever the server's name (step 3, changed to this path). A lock released by
  the kernel when its holder dies, so a killed server cannot block a new start.
- Registry removed (step 2b/4). It is a shared segment that lists the other
  segments and the server table. Replaced by passing file descriptors with
  `SCM_RIGHTS` in the `ClientOpen` reply.
- Port count fixed at `PORT_NUM` (2048). Audio buffers are inside the port
  objects in the graph segment, so audio stays in shared memory.
- Framing: 16-byte header with magic, version and length. Version mismatch is
  refused, with no negotiation.
- Counters: split configuration (copied on write) from live activation counters
  (never copied). Fixes the TSAN race in the connection-state copy.

## Layout (current)

- `common/`: shared by client and server (shared memory structures, requests and notifications, graph, ports,
  logging, the MIDI buffer, the client base class `JackClient`, the platform selector, the public headers in `common/jack/`).
- `client/`: the client library (`JackLibClient`, `JackGenericClientChannel`, `JackLibGlobals.h`, `varargs.h`,
  `JackServerLaunch.h`) and the `jackmini` API (`client/jackmini/`).
- `server/`: the engine, the server and its control API, `jackd` (`Jackdmp.cpp`), the drivers (dummy, freewheel,
  audio, MIDI), the internal clients and the session file loader, the request decoder, the MIDI queues.
- `platform/`: the POSIX and Linux code (threads, mutexes, sockets, futex, clock, the ALSA drivers in
  `platform/alsa` and `platform/alsarawmidi`).
- The rule for a file used by both sides: `common/`. Where the placement was unclear (for example `JackClient`, which
  is the base of the server's internal clients), it stays in `common/`.

## Done (commits in order, newest first)

- `a89cae4c` Plan B S3: `memops.c` becomes `memops.cpp`, kernels unchanged. Byte-identical to the C build
  on random inputs (scratch harness, not committed), with default flags and `-msse4.1`. Runtime dispatch is NOT
  done (compile-time selection kept); the SSE2 and SSE4.1 outputs match, so it can follow.
- `46d826f9` Plan B S2: the ring buffer is C++ with `std::atomic` (`common/ringbuffer.cpp`); the `jack_ringbuffer_*`
  names stay; `jack_ringbuffer_t` is opaque now. `tests/ringbuffer_unit.cpp`, clean under TSAN.
- `d727b06a` Plan B S8 (first part): hdsp, hammerfall, ice1712, and the generic_hw stub are removed. `alsa_driver.c`
  keeps one no-op hardware struct; the `generic_hw` argument is gone. Ran `jackd -d alsa -d hw:U192k` for 12 s, no errors.
- `e00c74b2` Plan B S1: usx2y deleted; `promiscuous.c` becomes `platform/promiscuous.cpp`; `JackLinuxTime.c` becomes
  `platform/JackLinuxTime.cpp` (the clock functions move to `jackmini` and get snake_case names); `alloccount.c` becomes
  `tests/perf/alloccount.cpp`.
- `9b29f41c` Dead public headers removed: `intclient.h`, `statistics.h`, `weakjack.h`; `jack.h` loses the internal
  client declarations.
- `ea685c8e` Internal clients removed. The protocol has 26 requests and is at version 12.
- `8fd1e5a2` Style guide (`docs/design/style.md`) and the namespace rename `Jack` to `jackmini`.

What is left in C: `platform/alsa/alsa_driver.c`, `alsa_rawmidi.c`, `alsa_seqmidi.c` (the S8 port), and
`tests/alsa_test_resampler_plugin.c`. The ALSA C files use `server/driver_interface.h` and the old driver structs,
so the driver module interface (S4) comes before the ALSA port (S8).

- `0c3d50b6` Latency on `jackmini::Client` (port ranges, recompute, the latency callback),
  and `jackctl_server_set_freewheel` (freewheel as a server control). Found: a client opened
  while the server is in freewheel mode does not start its cycle (the base does the same).
- Reference tag `reference/c-client-api` (pushed) marks the last commit with the C client
  library, for reference.
- `c3ab712e` Client API step 3, third area: audio and MIDI buffers and transport on
  `jackmini::Client`; the port table is lock-free for the realtime thread; a header fix
  (the MIDI buffer's friend). Checked by `tests/jackmini_client_e2e.cpp` (with a server).
- `f92385ed` Client API step 3, second area: ports and connections.
- `e082dfe7` Client API step 3, first area: `jackmini::Client` lifecycle, callbacks and
  identity on the C library (`client/jackmini/client.cpp`). Not yet: ports, connections,
  buffers, transport (declared, not implemented: a program that calls them does not
  link). Also a C library fix: a failed open kept only `JackFailure|JackServerError`,
  dropping the reason (`JackNameNotUnique`). Checked by `tests/jackmini_client_e2e.cpp`
  with a server, and the existing checks.
- `6f7a2791` Protocol item 7: `tests/protocol_conformance_unit.cpp` (fake server on a
  socket pair; requests and replies; malformed, version-mismatch, bad-header and
  mismatched-reply refusals; a synchronous notification). TSAN clean. Layout checks for
  the new fields in `tests/shm_layout_unit.cpp`. The control-API test under TSAN (the
  in-process server) reports nothing.
- `66d3abc4` Protocol item 6: deactivate keeps the connections. The graph counts a
  deactivated client's connections only while it is active (`SetInactive`), its
  inputs are silent, and its outputs are not mixed in. The engine no longer disconnects
  or sends port-off notices on deactivate. Tests: `deactivate_graph_unit` (graph), and
  the client API with a running server (connection kept, the other client keeps
  cycling, silence, audio back on reactivation). Closing a deactivated client is fine.
- `98fe0131` Protocol item 5: the live activation counters are outside the copied
  connection state (`JackGraphManager::fActivation`). `JackActivationCount` keeps only
  the configuration count. `tests/counter_tsan_unit.cpp`: TSAN reports four races on
  the old code (the copy and two log reads), none on the new. Two debug-log reads of the
  16-bit index now use the atomic load.
- `04934528` Protocol item 4c: bounded per-client notification queue (256, to be measured),
  one writer thread per client, the oldest dropped and counted, `kDropped` sent to the
  client. The engine no longer waits for a slow client: 2000 registrations with a
  sleeping client take 0.35 s. Checks as before.
- `a20f118d` Protocol item 4a: notifications framed with the header (type = notification
  kind, request 0, or an id for a synchronous notification that the client's reply
  echoes). Messages are built on the stack (the RT thread sends notifications; no
  allocation). `SendWithFds` finishes partial writes; a full non-blocking socket drops
  the message as before. Checks: `message_frame_unit` (notification round trip, sync
  reply); end-to-end client; control API; old-version refusal; ALSA starts.
- `575b58e9` Protocol order of work, item 3: the 16-byte header on every request
  and reply (`common/JackMessage.h`), the request id echoed, the exact payload
  length, and the version check on every message (`JACK_PROTOCOL_VERSION` is 10).
  A client of another version gets status 7 (Protocol) and is disconnected.
  Notifications keep their old format (`JackClientNotification` keeps its size
  prefix) until item 4. Checks: `tests/message_frame_unit.cpp` (8/8 suites); the
  end-to-end client; the control API; an old-version raw client refused; five ALSA
  starts. Found on the way: removing every `CheckSize()` also removed the size
  check of `JackClientNotification` (not a request), which broke the add-client
  notification. Restored.
- `07a72929` Per-user lock (`/dev/shm/jack_<uid>_server.lock`), name-independent.
- `70ac646b` P2 step 4: the shared-memory registry (`shm.c`, `shm.h`) is removed.
  `JackShmMemAble` holds its size, address and descriptor. The
  `--replace-registry` option is removed (jackd, control API, server globals).
  Verification: unit suites 7/7; dummy-driver e2e; control API; second server with the
  same name refused; the server no longer creates `/dev/shm/jack-shm-registry`.
- Step 5 (ALSA startup on the USB card, `hw:CARD=U192k`, 10 starts, each ended with
  SIGKILL): **10 of 10** runs passed the end-to-end client (cycles counted), logs
  clean (no driver timeout, no xrun). Baseline was 2 of 4 failing. Ten runs is a
  small sample: repeat before calling the stall gone.
- `528aab29` P2 step 3: single-instance lock. `platform/JackServerLock` takes an
  `flock` (on the per-user file, see Decisions); both start paths use it (`jackd` and the
  control API). The server table is gone from `shm.c`. Also fixes a crash on
  a registry with the old layout (the bad-registry path used an index before the
  mapping).
- `7fa627fe` P2 step 2b: segments are memfd, and the `ClientOpen` reply carries
  the three descriptors (`SCM_RIGHTS`, one message, order engine, graph, client
  control) and the sizes. The client maps them and closes them. The index-based
  templates are gone (`JackShmReadPtr`, `JackShmReadWritePtr1`).
- `6bf4566b` P2 step 2a: `platform/JackFdSegment` (descriptor passing, memfd
  segments, `UniqueFd`), `tests/fd_segment_unit.cpp` (18 checks).
- `a03b05ed` P2 step 1: static layout checks, `tests/shm_layout_unit.cpp`.
- `cd3f88bd` P2 step 1: packed attributes removed from the shared-memory
  classes. Probe: all engine-control fields and the per-client timing array now
  at natural offsets (were misaligned before).
- `9335307f`, `8b01af98`, `337895aa`, `007e4c46`, `bf686dc0`: protocol spec and
  its corrections.
- `8cf05534`, `4accac6e`, `296fa8ab`: client API decisions and headers.
- `2d103706`: `RingBuffer`, `Uuid`, error text, with tests (TSAN clean).
- `f07d417e`: P0 baseline and measurement tools (`docs/perf/baseline-pc.md`,
  `tests/perf/`).
- `064b3cea`: `-Z` takes a comma-separated list; the exclude-card description
  fixed (it was too long for the descriptor and the option vanished).
- `606325b2`: merge of `origin/hjj/secondary-alsa`.
- Earlier: the C++23 modernization, atomics and the TSAN audit (see PR #3).

Test suites (all in CTest, all passing at `6bf4566b`): `cstring_unit`,
`alsa_secondary_unit`, `message_buffer_unit`, `jackmini_api_unit`,
`jackmini_unit`, `shm_layout_unit`, `fd_segment_unit`.

## Next

- **Notification overflow test (done).** `tests/notify_overflow_unit.cpp` (ctest `notify_overflow_unit`) attaches a
  socket pair to the notification channel (`JackSocketNotifyChannel::Attach`, the seam beside `Open`) and sends 1000
  notifications while the reader does not read: delivered + dropped = 1000, the newest is delivered, the order is kept.
  With 100 nothing is dropped. The counts do not depend on the kernel's socket buffers, only the invariants.
- **Protocol item 4b done.** The session requests and the property change request are removed, with their
  notifications (`kSessionCallback`, `kPropertyChangeCallback`) and the server and client code. The requests are
  renumbered 1 to 30. `JACK_PROTOCOL_VERSION` is 11. The metadata store stays inside the server (the ALSA drivers set
  pretty port names through it; no client can read them now). Checked: unit suite; end-to-end and graph tests in
  freewheel and real time; cpu; rtbench.
- **Benchmarks ported to jackmini** (`tests/cpu.cpp`, `tests/iodelay.cpp`, `tests/external_metro.cpp`,
  `tests/perf/rtbench.cpp` as `jack_rtbench`). Durations are counted in cycles (`WaitUntil` / `WaitCycles`
  in `tests/jackmini_wait.hpp`), no sleeps. `run-baseline.sh` uses `jack_rtbench` and no fixed delay.
  `alloccount.cpp` is an LD_PRELOAD tool, ported to C++ (build by hand: `g++ -std=c++23 -shared -fPIC -o alloccount.so tests/perf/alloccount.cpp`; send SIGURG for the counts).
- **Fixed: closing a client in freewheel stalled the graph.** The freewheel driver waited (10 s) for a
  removed client's signal. `JackEngine::ClientDeactivate` now wakes the driver in freewheel while the graph is
  unfinished; `Process` then switches to the graph without the client. Repeated `jack_cpu` runs and the e2e test
  followed by `jack_cpu` show no `SuspendRefNum` or close-wait errors.
- Freewheel and `jack_cpu`: the load is still wall-clock (a share of the buffer period per cycle), so a run in
  freewheel takes longer than the cycle count suggests. Not a bug.
- **Waits on a graph switch, not on time.** `JackEngine::WaitForSwitches(count)` waits until the engine's
  switch counter (`fSwitchCount`, incremented in `ProcessNext`) reaches `count`. Client open waits for one
  switch (the RT thread runs); activate, deactivate and close wait for two switches after their graph write (the
  first switch may have begun before the write). The only time limit is `GRAPH_SWITCH_GUARD` (15 s), a failure
  guard for a dead driver, which is longer than a freewheel cycle wait.
- Real time on this non-RT kernel (`PREEMPT_DYNAMIC`, not RT): `jack_cpu -t 1` takes 1 to 3 s. Benchmark numbers are
  for the RT hardware run.
- Real-time: `jack_cpu`, `jack_iodelay`, `jack_multiple_metro`, `jack_rtbench`, and the e2e test all pass.
- **`tests/test.cpp` replaced** by `tests/jackmini_graph_e2e.cpp` (ctest: no; run with a server, in
  freewheel or real time). Kept and checked: chain data flow (exact sample), 64 ports registered, listed and
  removed, connection rules, unique names and UUIDs, eight clients running with a transport seen by all.
  Dropped, because jackmini has no such feature: internal clients, `jack_client_new`, port tie/untie, port
  rename and its callback, registration, connect, rename, graph-order, sample-rate, buffer-size, xrun, freewheel
  and thread-init callbacks, `jack_set_buffer_size`, the sync callback (slow-sync transport), `jack_frame_time`,
  cycle times, alternate thread models, the error function. Freewheel is the `-F` start option, checked by the
  graph and end-to-end runs.
- **Foreign port handles (fixed).** A `Port` carries the token of the client that issued it. A client refuses a
  handle with another token (`Error::NotFound`). Before, the id of another client's handle could match one of this
  client's own ports, so a connect could hit the wrong port. The graph test checks the refusal.
- **Cycle struct (done).** The process callback is `bool(const Cycle&)`: `frames` (buffer size),
  `current_frames`, `current_usecs`, `next_usecs`, `period_usecs`, `freewheel`. Filled from
  `jack_get_cycle_times` and the engine control's `fFreewheel` (now `std::atomic<bool>`). Transport position
  dropped by decision. Checked by the graph test (`cycle:` checks; `jackd -F` run with the argument
  `freewheel`): frames advance by exactly the buffer size in both modes; in real time the next cycle is one
  period after the current one (within 2 us). In freewheel the period is measured, not nominal (it varies
  per cycle), so the period check is skipped there.
- **C client library removed (step 2 done).** The client API exports are gone: `common/JackAPI.cpp`,
  `common/JackLibAPI.cpp` and their wrapper bodies. The jackmini client calls `jackmini::lib`
  (`client/jackmini/lib.hpp`, `lib.cpp`), which holds the former bodies, not exported. The UUID functions
  the server uses moved to `common/JackUuid.cpp`. `jackd` assigns `jack_error_callback` directly.
  Exports left in libjack (24 `jack_` symbols): the `jack_midi_*` functions (the ALSA drivers use them as
  their interface), the `jack_ringbuffer_*` functions, and the `jack_error_callback`/`jack_info_callback`
  variables. Checked: unit suite; end-to-end and graph tests in freewheel and real time; cpu; rtbench.
- **Install (step 5 done).** Installed: the jackmini headers (`include/jackmini/`), the public headers of
  the MIDI, ring buffer and server control (`include/jack/`: midiport.h, ringbuffer.h, control.h, types.h, ...),
  and the libraries `libjack`, `libjackserver`, `libjackring`, `libjacklog`, `libjackmidi` (all versioned).
  The other C client headers (jack.h, intclient.h, ...) are no longer installed. There is no pkg-config
  file: the build is CMake only. Checked: a program built against the staged install (`cmake --install --prefix`)
  opens a client on a running server.
- **The C client library and its split are done.** `jack.pc` removed (the build is CMake only). The
  `jack_midi_*` and `jack_ringbuffer_*` names are kept as they are.
- **jackmini uses the MIDI library directly (step 4 done).** The client calls `jack_midi_*` from `libjackmidi`
  (prototypes in `common/JackMidiPort.h`). The copies in `lib.cpp` are gone. `libjack` no longer defines any
  `jack_midi_*` symbol.
- **MIDI buffer in its own library (step 3 done).** `libjackmidi` (`common/JackMidiPort.cpp`, `JackMidiPort.h`, and
  the `jack_midi_*` functions in `common/JackMidiAPI.cpp`). Its buffer symbols are `LIB_EXPORT` now, so the client
  library (which is not built with the server definitions) can link them. Linked by the client library, the server
  library, the drivers and the unit tests.
- **Basic logging split (step 2 done).** `libjacklog` (`common/JackLog.cpp`, `common/JackLog.h`): the
  callbacks, `jack_error`, `jack_info`, `jack_log_direct`, and the routing hook `jack_log_router`. It has no
  dependencies. The host (`common/JackError.cpp`) keeps `jack_log` (verbose only) and the per-thread routing, and
  installs its routing into the hook at load time. Every module that logs links `libjacklog`.
- **Libraries split (step 1 done).** The ring buffer is `libjackring` (`common/ringbuffer.c`), linked by
  `jack`, `jackserver`, the ALSA drivers, and the unit tests that build the server objects; installed with them.
  Next: `libjacklog` (basic logging), then `libjackmidi` (MIDI buffer), then the jackmini client on `JackMidiBuffer`,
  then the install rules and pkg-config.
- Then: protocol item 4b (drop session and metadata messages).
- **TSAN triage (current build, dummy driver, end-to-end, graph and cpu tests).** Reports, by location:
  - Real, in one process: `JackEngine::ProcessNext` (`fLastSwitchUsecs`, JackEngine.cpp:152), fixed (atomic);
    `JackConnectionManager::ResumeRefNum` timing fields (268/269); `JackClient::IsActive`, `Activate`, `Deactivate`
    (127, 438, 460); `JackClient::CallSyncCallbackAux` (913); `JackLibClient::Open` (143); `jack_log` reading
    `JackGlobals::fVerbose` (JackError.cpp:54); `SetClockSource` (platform/JackLinuxTime.cpp, the clock function pointer).
  - Probably not races under the protocol (ordering by futex syscalls across processes, which TSAN does not model):
    audio buffer reads and writes in the chain test (jackmini_graph_e2e.cpp:92, :100) and the shared-memory accessor
    `JackShmReadWritePtr::operator T*` (JackShmMem.h:185). Needs a decision on how to annotate them.
  - Not yet checked against `origin/develop`; the earlier list (above) said the client-side statements were the same there.
- **Found while running TSAN:** `JackPosixThread::Kill` stops the realtime thread with `pthread_cancel`. Under TSAN the
  forced unwinding reaches a `noexcept` frame, and the client aborts in `std::terminate` at deactivate. The normal build
  is not affected. The fix is a cooperative stop (a flag and a wake-up, then join), which changes the thread shutdown.
  Until it is done, TSAN runs of the client stop at deactivate.
- Open TSAN findings, outside the counters (verified to be the same statements as in
  `origin/develop`): `JackEngine::ProcessNext` (`fLastSwitchUsecs`); the client timing
  fields in `ResumeRefNum`; the client library's `JackClient::Activate`, `Deactivate`,
  `CallSyncCallbackAux`, `JackLibClient::Open`.

## Environment facts

- Machine: HP ZBook Studio G5, i7-8750H, 12 CPUs. Kernel `7.0.0-42-generic`,
  `PREEMPT_DYNAMIC`, **not** PREEMPT_RT. Realtime scheduling works: server
  threads are SCHED_FIFO 70 (`chrt -p`). `ulimit -r` is 95.
- ALSA cards: 0 `PCH` (HDA, onboard), 1 `NVidia` (HDA), 2 `U192k` (Behringer
  UMC404HD, USB). The PCI card cannot run 64 frames here (1889 xruns); the USB
  card is the test device.
- Server options: the ALSA driver needs `-d hw:CARD=U192k` (card id, not
  `hw:2`). The server name is `-n`, given before `-d`. Inside the driver `-n` is
  nperiods.
- Automatic discovery adds every other card as a secondary. Exclude with
  `-Z PCH,NVidia` (comma list since `064b3cea`).
- `ptrace`: `kernel.yama.ptrace_scope=1`, so `gdb -p` and `strace -p` on a
  running server fail. `strace` works only if it starts the server.
- `perf_event_paranoid=4`: no tracepoints without root.
- `/dev/shm`: killed servers leave `jack-1000-0`, `jack-1000-1`,
  `jack-shm-registry`, `jack_*_1000_0`, `jack_alsa_hw_*.lock`, `jack_sem.1000_*`.
  Clear them only when no `jackd` runs.
- Git push: the key is in the agent at `/run/user/1000/gcr/ssh`. Use
  `SSH_AUTH_SOCK=/run/user/1000/gcr/ssh git push origin hjj/cxx23`.
- `gh` works for PR checks (`gh pr checks 3`).
- Build dir used for everything: `/tmp/opencode/build-jack2-cxx23`:
  `PKG_CONFIG_PATH=/tmp/opencode/sysroot/usr/lib/x86_64-linux-gnu/pkgconfig`,
  `PKG_CONFIG_SYSROOT_DIR=/tmp/opencode/sysroot`, then `cmake -S . -B ...` and
  `cmake --build ... --parallel 8`. `/tmp` is not durable; recreate the sysroot if
  it is gone (the ALSA and udev headers were unpacked there).
- Container checks (GCC 13 and clang 18 on AArch64): `docker run ubuntu:24.04`
  with `g++-13` and `clang`; run in the background, it takes about 40 minutes.

## Open issues (recorded, not fixed)

- **ALSA startup stall.** (Step 5 run, 10 of 10 passing, see Done.) Intermittent: the server prints its banner, the driver
  never runs its first cycle, and the engine's 5-second driver timeout expires.
  Counts: `HEAD` 2 of 4 starts, the packed change 3 of 4. Not caused by the
  layout change.
- **Signals (not reproduced, closed).** The server exits on SIGINT and SIGTERM, with and without clients connected, and
  removes its own segments. A killed client's semaphore is removed by the server while the server runs. The registry is
  gone (step 4). A `jack_db-1000` directory from older builds stays in `/dev/shm`; the current code does not create it.
- **Interval does not match the nominal period.** At 64 frames the start-to-start
  interval is about 11% long; at 256 frames about 6% short. Hypotheses: the USB
  period and the non-RT kernel. Not explained.
- **Secondary cards write rate controls on excluded cards** (kernel log: "control
  already present", "Too big adjustment").
- **Syscalls per cycle not measured.** Under `strace` the server does not start
  (see the stall). Needs the startup fixed, or a different counting method.
- **Allocations per cycle:** effectively zero for the whole process (64 `malloc`
  in a 15 s run). Per-thread counts for the RT thread still to be added.

## How to resume

1. `cd /home/hjj/Develop/jack2 && git status && git log --oneline -3`.
2. Read this file, `docs/design/protocol.md` (section 4.1 and the order of work),
   and `docs/design/client-api.md`.
3. Rebuild the test dir and run `ctest --test-dir /tmp/opencode/build-jack2-cxx23`.
4. Continue client-API step 3 with ports and connections (see "Next").

## Working agreements with the user

- Commit and push each completed step; do not rewrite pushed history.
- Keep the realtime path honest: no allocation, locks, or syscalls in the cycle.
  Report process-wide counts as process-wide, not as the RT thread's.
- Hardware measurement on the real RT distribution and boards is the user's job
  at the end. Do not wait for it.
- Plan mode: write plan files only when asked, in `/home/hjj/.opencode/plan`.

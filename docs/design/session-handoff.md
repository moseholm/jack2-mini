# Session handoff: where the work stands

Written at the end of a long session, so the next session can pick up
without the conversation. Read this first, then `protocol.md` and
`client-api.md`.

## Branch and state

- Branch `hjj/cxx23`, pushed to origin. Head at the time of writing:
  `0c3d50b6` (latency; freewheel control). `origin/develop` is `5c926a55`.
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

- Namespace `jackmini`. Headers in `common/jackmini/`, declarations only for the
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

## Done (commits in order, newest first)

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
  identity on the C library (`common/jackmini/client.cpp`). Not yet: ports, connections,
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
- `528aab29` P2 step 3: single-instance lock. `posix/JackServerLock` takes an
  `flock` (on the per-user file, see Decisions); both start paths use it (`jackd` and the
  control API). The server table is gone from `shm.c`. Also fixes a crash on
  a registry with the old layout (the bad-registry path used an index before the
  mapping).
- `7fa627fe` P2 step 2b: segments are memfd, and the `ClientOpen` reply carries
  the three descriptors (`SCM_RIGHTS`, one message, order engine, graph, client
  control) and the sizes. The client maps them and closes them. The index-based
  templates are gone (`JackShmReadPtr`, `JackShmReadWritePtr1`).
- `6bf4566b` P2 step 2a: `posix/JackFdSegment` (descriptor passing, memfd
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

- **Freewheel** is a construction-only setting: `jackd -F` (parameter `freewheel`). The runtime
  switch `jackctl_server_set_freewheel` is removed. The late-join bug is fixed: a client opened
  during freewheel used the flushed driver synchro (its signals to the freewheel driver were
  dropped, so the driver waited 10 s each cycle). The shared `JackEngineControl::fFreewheel`
  flag now sets that mode at open.
- **Sleep-based tests:** the deactivate and reactivate checks in `tests/jackmini_client_e2e.cpp`
  count cycles of a clock client, not time. The e2e runs in freewheel (`jackd -F -d dummy`, 1 s)
  and in real time (6 s), both pass. The transport and latency checks still use sleeps.
- **Still open:** the overflow test (notification queue) is not in the repo; add it with the
  freewheel start if wanted.
- **Then:** port the test programs only when needed; remove the C client library (reference tag
  `reference/c-client-api`); protocol item 4b (drop session and metadata messages).
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
- **Server ignores SIGINT and SIGTERM.** Must be killed (`SIGKILL`). Leaves
  registry and segments behind.
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

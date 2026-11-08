# Session handoff: where the work stands

Written at the end of a long session, so the next session can pick up
without the conversation. Read this first, then `protocol.md` and
`client-api.md`.

## Branch and state

- Branch `hjj/cxx23`, pushed to origin. Head at the time of writing:
  `528aab29` (P2 step 3). `origin/develop` is `5c926a55`.
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
- Single-instance rule: a lock on the server's socket path (step 3). A lock
  released by the kernel when its holder dies, so a killed server cannot block a
  new start.
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

- `528aab29` P2 step 3: single-instance lock. `posix/JackServerLock` takes an
  `flock` on `<socket path>.lock`; both start paths use it (`jackd` and the
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

## Next: P2 step 4 and step 5

Steps 2b and 3 are done (see Done). The plan, in order:

4. **Remove the registry** from `common/shm.c` and `shm.h`: the registry header,
   `jack_access_registry`, `jack_create_registry`, `jack_shm_lock_registry`, the
   index-based attach/release, and `jack_cleanup_shm`'s registry part. The index
   field of `jack_shm_info_t` goes too. `jack_initialize_shm_server()` (server
   start, after the lock) and the client `jack_initialize_shm()` are no longer
   needed. Callers: `JackServerGlobals.cpp` and `JackControlAPI.cpp` (start,
   close, and the failure paths).
   **Needs a decision first:** the `replace-registry` option
   (`JackControlAPI.cpp`, `Jackdmp.cpp`, `JackServerGlobals.cpp`) has no meaning
   without a registry. Remove it, or keep it accepted and ignored.
   Also decide whether the server should unlink the old `/dev/shm/jack-shm-registry`
   once at start.
5. **Verify.** Smoke test (`jackd -d dummy`), in-process server test, the unit
   suites, then ALSA startup reliability: start the server 10 times on the USB
   card and count successes. Success means the intermittent stall is gone. That
   stall is parked until this step, as agreed.

Scripts used for the step 2b and 3 checks were in `/tmp/opencode` (not durable):
`e2e_2b.c` (two clients, audio through the shared graph) and `ctl_2b.cpp` (control
API open, second open refused, reopen after close). Run the server with
`JACK_DRIVER_DIR=<build>/lib/jack` and `LD_LIBRARY_PATH=<build>/lib`.

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

- **ALSA startup stall.** Intermittent: the server prints its banner, the driver
  never runs its first cycle, and the engine's 5-second driver timeout expires.
  Counts: `HEAD` 2 of 4 starts, the packed change 3 of 4. Not caused by the
  layout change. Parked until step 5 above.
- **The lock is per socket path, so per server name.** Two servers with
  different names run together (checked). The protocol doc says one server per
  machine. Decide: lock a name-independent path, or keep the approved design and
  change the doc.
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
4. Start P2 step 4 (see "Next"). Ask about the `replace-registry` option first.

## Working agreements with the user

- Commit and push each completed step; do not rewrite pushed history.
- Keep the realtime path honest: no allocation, locks, or syscalls in the cycle.
  Report process-wide counts as process-wide, not as the RT thread's.
- Hardware measurement on the real RT distribution and boards is the user's job
  at the end. Do not wait for it.
- Plan mode: write plan files only when asked, in `/home/hjj/.opencode/plan`.

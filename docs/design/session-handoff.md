# Session handoff: where the work stands

Written so a new session can pick up without the conversation. Read this file first, then
`docs/design/client-api.md`, `docs/design/style.md`, `docs/design/alsa-redesign.md`, and
`docs/design/perf-baseline/README.md`. The master plan is outside the repo, at
`/home/hjj/.opencode/plan/jack-cxx-architecture.md` (S0 to S10 and the decisions).

## Branch and state

- `origin/develop` is the base and contains all of the WebKit style work (PR #4, merged). Its tree is identical to
  the PR head `079e43a0`; the history was rewritten by the merge. Start new work from `develop`, on a new branch.
- The local branch `format/webkit` is the old PR branch; it is safe to delete once you are sure nothing is needed from it.
- `git status` should be clean. `.vscode/launch.json` must never be committed.
- PR #3 (`hjj/cxx23`) merged into `develop` earlier. `backup/cxx23-pre-p0-merge` is a local tag, not pushed.

## Current work: licensing and the README (not started)

The user wants the README to describe the new project honestly, and the licence layout made explicit. Agreed plan:

1. **Licence audit** in a separate file (not in this handoff). Classify every file as JACK2-derived or new, from the
   git history (the original tree is `e102e682`). Derived files keep their original licence.
2. **Licence layout:** `COPYING` stays GPLv2 for the server. The client stays LGPL-2.1-or-later, with its text in
   `COPYING.LGPL`. A short `LICENSING.md` says which files fall under which licence. The LGPL text lets integrators link
   without disclosing their own code. No MIT licence for the client: the original JACK2 client code is LGPL, not MIT.
3. **Contributor blocks:** keep every existing name and append the user's name and year (format still to be confirmed
   with the user). Never remove a name or a copyright line.
4. **README rewrite:** state plainly that jack2-mini is not compatible with the old JACK2 server or client API, and say
   what changed (C API names and types, wire protocol, shared-memory layout, ALSA-only backend). Credit JACK2 as the
   basis of the code. The current README says existing clients "work unchanged"; that is probably wrong, so verify the
   actual C API and protocol before writing the compatibility statement.
5. Build with `scripts/ci.sh`, then open a PR against `develop`.

## Status

| Step | State | Where |
|---|---|---|
| Plan A: the client on `ClientSession` | done | commits `c7e8dfd3` to `0df63f3a` |
| S1 to S4: foundations, memops, driver interface | done | earlier history |
| S5: control API and `jackd` | done | `05a8a528`, `5ac28048` |
| S8: ALSA redesign | done (measured) | `96fdde5e` to `89b9b564`, below |
| MIDI: client-to-client only | done (hardware MIDI removed) | `94606177`, `5dd484db`, `a644bf85`, `abb856ad` |
| Cleanup (unused headers, metadata) | done | `0f2e9a67`, `86daa0be` |
| S6: engine and graph | done (`a9a1be6c` to `e2124e88`) | see "Next" |
| S7: server and protocol | done (`a0f275e1` to `ab182c4e`) | see "Next" |
| S9: remove the C (resampler kernels stay) | done (`9ab39c81`, `38bcbfe5`) | see "Next" |
| S10: validation | done (`f38b6e2c`; `docs/design/validation.md`) | see "Next" |
| Tools: the command-line tools, rebuilt on the jackmini API | done (T0 to T5) | see "Tools" |
| WebKit style (PR #4, merged into `develop`) | done | identifier names, CamelCase files, `std::array`, `constexpr`, `//` comments; see "Decisions" |
| CI: `scripts/ci.sh` shared by the workflow and local runs | done | SSE4.1/AVX2 matrix entry added; latest run green |
| Licensing and README | not started | see "Current work" |

## Status

| Plan step | State | Where |
|---|---|---|
| Plan A: the client on `ClientSession` | done | commits `c7e8dfd3` to `0df63f3a` |
| S1 to S4: foundations, memops, driver interface | done | earlier history |
| S5: control API and `jackd` | done | `05a8a528`, `5ac28048` |
| S8: ALSA redesign | done (measured) | `96fdde5e` to `89b9b564`, below |
| MIDI: client-to-client only | done (hardware MIDI removed) | `94606177`, `5dd484db`, `a644bf85`, `abb856ad` |
| Cleanup (unused headers, metadata) | done | `0f2e9a67`, `86daa0be` |
| S6: engine and graph | done (`a9a1be6c` to `e2124e88`) | see "Next" |
| S7: server and protocol | done (`a0f275e1` to `ab182c4e`) | see "Next" |
| S9: remove the C (resampler kernels stay) | done (`9ab39c81`, `38bcbfe5`) | see "Next" |
| S10: validation | done (`f38b6e2c`; `docs/design/validation.md`) | see "Next" |
| Tools: the command-line tools, rebuilt on the jackmini API | done (T0 to T5) | see "Tools" |

## Decisions (do not revisit without the user)

Architecture and scope:

- Client/server architecture: server process, clients on a Unix socket, shared-memory graph, futex sync.
- No compatibility with stock JACK. The C ABI, the C headers, the wire protocol and the shared-memory layout may change.
- Class-compliant ALSA PCM only. Hardware quirk drivers (hammerfall, hdsp, ice1712, usx2y, generic_hw) are removed.
- MIDI is client-to-client only. The server never reads or writes MIDI on hardware. The ALSA RawMIDI and
  sequencer backends, the `platform/alsarawmidi` module, the server's MIDI driver and its queues, and the
  `midi-driver` option are removed. The client MIDI transport (`libjackmidi`, `jack_midi_*`, `client/jackmini/Midi.hpp`) stays.
- Resampler kernels stay in C (`platform/alsa/resample/*.c`, `dbesi0.c`, and the PipeWire bridge), behind a C++ wrapper.
  The resampler is PipeWire only: the plugin ABI is gone.
- Driver modules stay loadable (`dlopen`), with one `extern "C"` entry point. The driver descriptor
  (`jack_driver_desc_t`) stays C: it is the module ABI.
- Internal clients, the session API, the loopback and proxy drivers, the slave drivers and metadata are removed.

Client API and control:

- Namespace `jackmini`. Headers in `client/jackmini/`. Style per `docs/design/style.md`: CamelCase types,
  snake_case methods, `Result<T>` (std::expected with the client `Error` enum), RAII, typed callbacks.
- Control API: `jackmini::control` (`client/jackmini/Control.hpp`), implemented in `server/Control.cpp`
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

- The cycle core is `jackmini::alsa::Cycle<Backend>` (`platform/alsa/PcmCycle.hpp`), a template over the
  `PcmBackend` concept, so the cycle has no virtual calls. The libasound backend is `Device` (`pcm_alsa.{hpp,cpp}`).
  `Session` (`pcm_session.{hpp,cpp}`) owns a device and the cycle and does the sample conversion.
- Secondary cards and the primary card both run on `Session`. `alsa_driver.c` is gone.
- Poll timeout is 1.5 periods, rounded up to whole milliseconds (the old driver passed microseconds as milliseconds).

Hardware and measurement:

- Hardware runs (RT kernel, real latency, the secondary cards at 64 frames) are the user's, at the end. Do not wait for them.
- Report process-wide counts as process-wide. The realtime path must not allocate, lock, or make syscalls.

Style and naming (PR #4, the WebKit style; `docs/design/style.md` is the reference):

- Types CamelCase without a `Jack` prefix. Functions and methods camelCase. Data members `m_` (classes and structs),
  static members `s_`, globals `g`, constants and enumerators `kCamelCase`. Files CamelCase, named after their main type.
- Functions declared in `common/jack/`, `jack_error`/`jack_info`/`jack_log`, and `jack_slist_*` keep their names. C API
  types in `common/jack/` carry a `C` suffix (e.g. `PortIdC`). Internal classes that clashed were renamed:
  `JackPort` is `ServerPort`, `JackServer` is `ServerCore`, `JackResult` is `Reply`.
- Integers from `<cstdint>`; `char` for text only, `std::byte` for raw bytes; `std::array` for fixed arrays (the struct-hack
  trailing arrays stay); `nullptr`; `constexpr` constants; `//` comments only.
- The C files under `platform/alsa/resample/` and their headers are untouched by the style work.

Licensing (agreed with the user; see "Current work"):

- The server is GPLv2 (`COPYING`). The client is LGPL-2.1-or-later, because the original JACK2 client code is LGPL.
- Do not relicense JACK2-derived code. Do not remove copyright or contributor lines; append only.

## Layout (current)

- `common/`: shared by client and server: shared-memory structures, requests and notifications, graph, ports,
  logging (`libjacklog`), the ring buffer (`libjackring`), the MIDI buffer (`libjackmidi`), `JackClient`.
  `common/jack/`: the public C headers that stay (`jslist.h`, `midiport.h`, `ringbuffer.h`, `systemdeps.h`,
  `types.h`, `weakmacros.h`, `transport.h`, `uuid.h`).
- `client/`: the client library. `client/jackmini/` is the API (`client.hpp`, `port.hpp`, `midi.hpp`,
  `ringbuffer.hpp`, `transport.hpp`, `options.hpp`, `error.hpp`, `control.hpp`, `jackmini.hpp`).
- `server/`: the engine (`Engine`, `LockedEngine`), the server (`ServerCore`), the control API
  (`control.cpp`), `jackd` (`Jackdmp.cpp`), the drivers (`DummyDriver`, `FreewheelDriver`,
  `AudioDriver`, `Driver`, `JackDriverLoader`), the socket channels, the request decoder.
- `platform/`: POSIX and Linux code (threads, mutexes, sockets, futex, clock, the server lock).
  `platform/alsa/`: the ALSA driver (`AlsaDriver`, `AlsaSecondaryCard`, the cycle core and session,
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

## Tools

The command-line tools are rebuilt in `tools/` on the jackmini client API, as `jackmini_<tool>` programs. Status:
done, with each step committed and pushed. The plan outside the repo is `/home/hjj/.opencode/plan/tools-port.md`; its
T4 description is superseded (see the monitor note below). The user-facing list is `tools/README.md`.

Steps (commit, what it did):

- T0: `fc5a3a80` the port and client UUIDs in the shared graph, and the protocol version 13; `e03a492e` `Client::ports()`,
  `Client::total_latency`, and `tests/JackminiPortsE2e.cpp`; `65ccb54d` the skeleton (`JACK_TOOLS`, `tools/`,
  `tool_common.hpp`, `tests/tools_e2e.sh`); `3972eca5` `jackd -n` sets the server name (it was ignored, see below).
- T1 `jackmini_lsp`: `eb5d8f80`. The options and output of `jack_lsp`, minus nothing: `-A` prints nothing yet.
- T2 `jackmini_connect`, `jackmini_disconnect`: `ea87ce84`. One source (`tools/Connect.cpp`), two programs.
- T3 `jackmini_samplerate`, `jackmini_wait`: `50db4334`, with `44080e43` (the client library returns a failed connection
  to the server instead of logging it, so the probes of `jackmini_wait` are quiet).
- T4 `jackmini_monitor_client`: `cc6da20a`. See the monitor note.
- T5 `jackmini_midi_dump`, with the test source `tests/MidiWriter.cpp`: `2a84c745`.

`tests/tools_e2e.sh` has 51 checks against a `jackd -d dummy` server it starts and stops; `check.sh` runs it in both
modes. `tests/JackminiPortsE2e.cpp` covers the listing API.

Decisions (answered by the user): all tools use the `jackmini_` names; `jackmini_lsp` keeps `-A` and adds `-U`;
the listing and the total latency are API additions; the protocol goes to 13; the tools are installed with the other
binaries; the disconnect tool is built from the connect source.

Monitor note: `jackmini_monitor_client` follows the handoff's table (copies a client's audio inputs to monitor
outputs). It does not do what `jack_monitor_client` did. The old tool asked the server to monitor a port for 30
seconds, and the server has no audio path that reads that request, so the old tool had no audible effect. Whether
the server should do monitoring is a separate decision. The copy itself is not checked by a test.

Open issues found while porting (not fixed):

- `Uuid::to_string()` prints 16 hex digits, but the server and `jack_uuid_unparse` use decimal, and the client parses
  decimal. The tools print decimal. The two should agree; the hex form is used only by `jackmini_unit`'s round trip.
- `-A` (aliases) prints nothing: no code sets aliases.
- `jackmini_lsp -u` prints a client's name, not its UUID, for a client that owns no ports (the listing is built from
  the ports).
- The intermittent failure of a freewheel end-to-end run: it failed in 2 of about 20 `check.sh` runs (once the client
  lifecycle check, once `jack_cpu` ran past its 60-second limit) and has not been reproduced since. `check.sh` now keeps
  each program's output in `/tmp/opencode/check_<mode>_e2e.log` and `check_<mode>_cpu.log`.
- `jackd -n` was ignored (the socket was always named `default`) until `3972eca5`. A server started with `-n minijack`
  (the launch configurations do) now has that name, so clients that connect by the default name no longer find it.

## Next

S6 is done. Its stages (each: build, `check.sh`, commit, push) are listed for the record.

1. **Done (`a9a1be6c`): `LockedEngine` returns `Result`.** The macros are gone. `ReplyCode` (in
   `RequestDecoder.h`) is the one wire mapping. The engine internals still return `int`, which stage 3 converts.
   `TemporaryException` (last client of a temporary server) is caught on the two close calls.
2. **Done (`a9a1be6c`): the call sites** in the server, the drivers and the socket channel.
3. **Done (`8653ede5`): `Engine` internals return `Result`.** The error kind says what failed. The graph
   manager and the client's `int` returns are unchanged (stage 4 and the client side).
4. **Done (`e2124e88`): `GraphManager`, `ConnectionManager`.** Gate: a thread-sanitizer run with no new
   report, checked against the tree before the change (procedure below). A duplicate connect is now an error
   (`InvalidState`); it was a silent success.

S9 is done. The only C left is the resampler kernels and their bridge (`platform/alsa/resample/`). C linkage (`extern "C"`)
is kept for the driver module entry points and descriptor, the `jack_midi_*` and `jack_ringbuffer_*` names, the bridge, and the
allocation interposer in `tests/perf/`.

S10 is done (`docs/design/validation.md`): ASan, UBSan and TSAN over the unit suites and the server e2e tests; zero
allocations in the steady window; syscalls per cycle below the S0 baseline with no new kind; the latency matches the
baseline. The RT hardware runs are the user's.

The plan's steps are all done. Remaining work is the user's: the RT hardware runs, and the ARM boards. The next
the tools are done (see "Tools"). Open decisions: the server's monitor request, and the UUID text format.

S7 is done (gate: the protocol conformance and notify overflow suites pass). Left on purpose: `RequestDecoder::HandleRequest`
and the framing returns (the protocol loop's `int`); `ExternalClient::ClientNotify` and `Close` (the client interface's `int`);
`ServerGlobals` (the singletons the free functions and the ALSA hooks use).
Still `int` on purpose: the graph's sentinel lookups (`GetInputRefNum`, `GetOutputRefNum`), the driver's
`Open`, `Close` and `Process` (module ABI and RT), `Engine::Process` (RT, `bool`), and `ServerPort::RequestMonitor`.

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
- Git push: SSH to GitHub is not reliable from this machine. Push over HTTPS with the `gh` token, and keep the token
  out of the repo config: `TOKEN=$(gh auth token); GIT_CONFIG_GLOBAL=/tmp/opencode/empty.gitconfig git push
  "https://x-access-token:${TOKEN}@github.com/moseholm/jack2-mini.git" <local>:refs/heads/<branch>`. The global git
  config rewrites HTTPS GitHub URLs to SSH, which is why the empty config is needed. Do not use `-u` with the token URL,
  because git then stores the token in `.git/config`.
- A workflow file (`.github/workflows/*`) needs the `workflow` scope on the token. Without it GitHub rejects the push.
  Ask the user to refresh the token (`gh auth refresh -h github.com -s workflow`) or to push that commit themselves.
- `gh` works for PR checks and PR creation (`GIT_CONFIG_GLOBAL` as above).
- CI does not use Docker. It runs on GitHub's hosted runners: `ubuntu-latest` (gcc and clang matrix, plus a gcc entry with
  `-msse4.1 -mavx2`) and `ubuntu-24.04-arm` (AArch64, Ninja). Each job installs its packages with `apt-get`.
- Local machine: the apt packages from the workflow are installed (`build-essential clang cmake ninja-build pkg-config
  libasound2-dev libudev-dev`), so no sysroot is needed. ziglang (pip, in `/tmp/opencode/cf`) is used only for the
  AArch64 syntax check, because its libc++ lacks `std::move_only_function`, so it cannot check the client code.

### Build and checks

- **Use `scripts/ci.sh`** for the build, test and install steps. The workflow calls the same script, so a local run
  and a CI run take the same steps. Run it per configuration, each in its own build directory:

      CC=gcc CXX=g++ BUILD_DIR=/tmp/build-gcc scripts/ci.sh configure
      BUILD_DIR=/tmp/build-gcc scripts/ci.sh build
      BUILD_DIR=/tmp/build-gcc scripts/ci.sh test
      BUILD_DIR=/tmp/build-gcc scripts/ci.sh install

  The configurations CI runs: gcc; clang (`CC=clang CXX=clang++`); gcc with `EXTRA_FLAGS="-msse4.1 -mavx2"`;
  AArch64 with `GENERATOR=Ninja` on an arm64 machine.
- The SIMD and AArch64 branches are not compiled by a default x86 build. Check them with a flag variant
  (`-msse4.1 -mavx2`) and, for NEON, a syntax check with an AArch64 target. The PR #4 CI failures came from exactly these branches.
- Before an ALSA run: `pkill -KILL -x jackd; rm -f /dev/shm/jack_*`, and check that
  `/proc/asound/card2/pcm0p/sub0/status` and `pcm0c` say `closed`.
- Build for timing with `RelWithDebInfo` or `Release`, not a debug or asserts-on tree.
- `/tmp/opencode/check.sh`, the baseline script and the allocation counter from earlier sessions are described in the
  repository's `tests/perf/` and `docs/design/validation.md`. `/tmp/opencode` may have been cleared.

### Thread sanitizer

Build a separate tree (`/tmp/opencode/build-tsan`) with `-fsanitize=thread` in the C, C++ and linker flags. Build all
targets (a partial build leaves stale drivers). Clear `/dev/shm/jack_*`, start `jackd`, and wait a few seconds before
the client. Known reports from earlier (in the graph and platform code): `ResumeRefNum` timing fields, a port's latency
range written from two threads, shared audio buffers, `set_clock_source`, and the MIDI test buffers. The futex race
(`LinuxFutex::Connect` and `Disconnect` written from the notification thread, read unlocked in `Signal` on the RT
thread) is in 4 of 5 client runs on both the tree before and after `e2124e88`; it predates S6.

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

1. `cd /home/hjj/Develop/jack2 && git status && git log --oneline -5 origin/develop`.
2. Read this file, `docs/design/style.md`, and `docs/design/client-api.md`.
3. Start the licensing work on a new branch from `origin/develop`. Build and test it with `scripts/ci.sh` (see "Build and checks").
4. Read `docs/design/validation.md` for the current state of the checks.

## Working agreements with the user

- Commit and push each completed step; do not rewrite pushed history. The one exception so far was the author rewrite
  of PR #4, which the user asked for while the branch was still local only.
- Keep the realtime path honest: no allocation, locks, or syscalls in the cycle. Report process-wide counts as process-wide.
- Hardware measurement on the real RT distribution and boards is the user's job at the end. Do not wait for it.
- Plan mode: write plan files only when asked, in `/home/hjj/.opencode/plan`.
- Commit author: the repository's configured identity (`moseholm <mose.holm@gmail.com>`). Never use a placeholder.
- Clean up as you go: delete a ported file in the same commit as its port; remove dead code and headers.

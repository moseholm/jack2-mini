# Licence audit

Classification of the tree at `origin/develop`, `1c9a725a`. Input for `LICENSING.md`. The tables record that
baseline; the later legacy C API cleanup is noted below.

## Baseline (corrected)

The history has three stages that matter here:

1. **`17959465`**, 2026-01-07, upstream JACK2 (falkTX). This is the original JACK2 tree.
2. **`65c9d792`**, 2026-11-04, "Linux/ALSA-only refactor" (Henrik Sølvkjær Jespersen, the same person as the repository owner under another GitHub account). It deletes most of upstream (Windows, macOS, waf) and keeps the rest.
3. **`e102e682`**, the merge of PR #3 (`hjj/cxx23`). Everything after it is this project's work.

The earlier version of this audit used `e102e682` as the original tree. That is wrong: it skips stages 1 and 2. The table below still classifies each file by its path and renames from `e102e682`, which is the state this project started from. The check against upstream is in "Upstream check".

Limits of the method:
- `git log --follow` links files through copies as well as renames, so a path-based origin can be wrong. Two such links were found and discarded (`ipc/socket/Promiscuous.hpp`, `server/drivers/DriverInfo.h`).
- Content matching finds little verbatim text shared with upstream: 7 current files are 30 % or more identical in lines to an upstream file. Most of the code was rewritten, so a file's licence follows its header and its lineage, not its text.

## Legacy C API cleanup (after the audit)

The audit below was made before the old C API was removed. In the current tree, `common/jack/` is gone; its vocabulary
types moved to `common/Types.h`, its UUID helpers to `common/Uuid.hpp`, and its byte ring buffer was deleted in favour of
the C++ `RingBuffer<T>`. The `jackring` library and `tests/RingbufferUnit.cpp` were removed too. Rows in the table that
say "removed in the C API cleanup" describe the current state; the licences in those rows are still their original
licences, so the removals do not change the licensing conclusion.

## Upstream check

For each current file with an upstream file of the same name (ignoring the `Jack` prefix, case and underscores), the licence was compared with the upstream one. 129 files have such a match. The matches that differ are false (the name points to another file), except one, which is explained:

- `server/control/Control.hpp` and `server/control/Control.cpp` replace the C control API. Upstream's control API is in `common/jack/control.h` and `common/JackControlAPI.h`. Both carry `SPDX-License-Identifier: ( GPL-2.0-or-later OR LGPL-2.1-or-later OR MIT OR AFL-2.1 )`: the work may be used under any one of those four licences. LGPL-2.1-or-later is one of them, so the LGPL licence of the replacement is allowed. The header of `Control.hpp` names only the LGPL. Keep this note with the licence.

These two are the only upstream files with an SPDX header. All other upstream files state their licence in the text, which the classifier reads.

## Method

- **Lineage** comes from git: a file is *renamed* or *kept* when git pairs it with an original file (similarity of 10 % or more), and *kept* when its path is unchanged.
- **Rewritten (name match)**: git does not pair the file, but a deleted original has the same name apart from the `Jack` prefix or a case change, and the licence matches. Most of these are headers and files rewritten during the WebKit style work (PR #4), where the identifiers changed. A rewrite of a derived file is still derived, so it keeps the original licence.
- **New**: no original counterpart. The licence is ours to choose.
- **Licence now / original** is read from the header of each file (the first 40 lines). A file with no licence text is marked `no notice`.

## Summary

Files in the tree: 302. Relative to `e102e682` (not upstream, see "Baseline"): derived (renamed, kept, or rewritten) 301, new 1.

| Original licence | Files | Licence now |
|---|---|---|
| GPL-2.0 | 80 | GPL-2.0 (same) |
| LGPL-2.1-or-later | 128 | LGPL-2.1-or-later (same) |
| MIT | 7 | MIT (same) |
| no notice | 86 | no notice (same) |

Every derived file keeps the licence it had in the original tree. No licence changed.

## Findings to settle before the layout

0. **The client library contains GPL code.** `ipc/memory/FdSegment.cpp` is GPL-2.0 (in `e102e682` too, and in `origin/develop`). It is in `JACK_COMMON_SOURCES` in `CMakeLists.txt`, and `JACK_COMMON_SOURCES` is part of `JACK_CLIENT_SOURCES`, so the `jack` library (`libjack`) that clients link against is built with it. `client/session/ClientSession.cpp` and `ipc/memory/ShmMem.h` use `FdSegment`. The client is therefore not LGPL as built, whatever its headers say. Choices: relicense `FdSegment` (the owner's decision), move the client's use of it out of the library, or make the client library GPL. Neither file has an upstream origin: `FdSegment` was added in `6bf4566b` (2026-11-08) and `Message.h` in `575b58e9` (2026-11-08), both by this project. So no third-party copyright is involved in relicensing them.
   **Resolved:** `FdSegment.{h,cpp}` and `ipc/protocol/Message.h` (which `client/session/ClientSession.cpp` includes) are relicensed to LGPL-2.1-or-later, by the owner's decision. The include closure of `libjack` now has no GPL file (checked by following `#include "..."` from its sources). The "Licence now" column above is the state before that change; `LICENSING.md` has the current lists.
1. **Resampler kernels are MIT.** `server/drivers/alsa/resample/` (`resample.h`, `resample-native*.c`, `resample-native-impl.h`) is copied from PipeWire under MIT (SPDX headers). `dbesi0.c` is Takuya Ooura's Bessel function under its own permissive notice. The plan in the handoff does not mention MIT; `LICENSING.md` must name it.
2. **`server/drivers/alsa/resample/spa-shim/` has no licence notice.** The five headers are written as minimal subsets of PipeWire's SPA headers (MIT, per `resample/README.md`). They need an MIT notice, or a statement that they are MIT by provenance.
3. **`tests/perf/alloccount.cpp`, `tests/perf/rtbench.cpp`, `tests/perf/run-baseline.sh`, `tests/tools_e2e.sh`, `scripts/ci.sh`, `configure.sh`, and the build files have no licence notice.** Under the plan, the server code is GPL-2.0 (`COPYING`), so these fall under `COPYING` by default. `LICENSING.md` should say so, or each file should get a notice.
4. **Mixed licences in one directory.** The licence follows the file, not the directory. Exceptions to the directory's majority:
   - `common/`: all LGPL-2.1-or-later. `Memops.{h,cpp}` (GPL-2.0) moved to `server/drivers/`.
   - `common/os/`, `ipc/memory/`, `ipc/socket/`, `ipc/sync/` and `server/ipc/`: all LGPL-2.1-or-later.
   - `server/drivers/alsa/`: all GPL-2.0 (`Jslist.h` was removed).
   - `server/control/` and `server/ipc/` are LGPL-2.1-or-later, as are `DriverInterface.h`, `DriverLoader.h` and `DriverModule.h` in `server/drivers/`; `server/core/ServerLock.{h,cpp}` and the rest of `server/` are GPL-2.0.
   - `tests/`: split about evenly. `tools/` is all LGPL.

   `LICENSING.md` must list these per file, not per directory.
6. **Public headers** `common/jack/*.h` were LGPL and were the ABI that integrators compiled against. They were removed
   in the later C API cleanup; `LICENSING.md` lists the C++ headers that replaced them.

## Per-file table (source and build files)

| File | Status | Origin | Original licence | Licence now |
|---|---|---|---|---|
| `CMakeLists.txt` | kept, edited | `CMakeLists.txt` | no notice | no notice |
| `client/session/ClientSession.cpp` | renamed | `client/client_session.cpp` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `client/session/ClientSession.hpp` | renamed | `client/client_session.hpp` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `client/session/ProcessContext.cpp` | renamed | `client/process_context.cpp` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `client/session/ProcessContext.hpp` | renamed | `client/process_context.hpp` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `client/ServerLaunch.cpp` | removed | `client/JackServerLaunch.cpp` | LGPL-2.1-or-later | — |
| `client/ServerLaunch.h` | removed | `client/JackServerLaunch.h` | LGPL-2.1-or-later | — |
| `client/api/Client.cpp` | renamed | `client/api/client.cpp` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `client/api/Client.hpp` | renamed | `client/api/client.hpp` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `server/control/Control.hpp` | renamed | `client/api/control.hpp` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `client/api/Error.cpp` | renamed | `client/api/error.cpp` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `client/api/Error.hpp` | renamed | `client/api/error.hpp` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `client/api/Jackmini.hpp` | rewritten (name match) | `client/api/jackmini.hpp` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `client/api/Midi.hpp` | renamed | `client/api/midi.hpp` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `client/api/Options.hpp` | renamed | `client/api/options.hpp` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `client/api/Port.hpp` | renamed | `client/api/port.hpp` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `client/api/Ringbuffer.hpp` | renamed | `client/api/ringbuffer.hpp` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `client/api/Transport.hpp` | renamed | `client/api/transport.hpp` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `client/api/Uuid.cpp` | removed in the UUID consolidation | `client/api/uuid.cpp` | LGPL-2.1-or-later | — |
| `client/api/Uuid.hpp` | kept as a forwarding header | `client/api/uuid.hpp` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `ipc/memory/ActivationCount.cpp` | renamed | `common/JackActivationCount.cpp` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `ipc/memory/ActivationCount.h` | renamed | `common/JackActivationCount.h` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `ipc/memory/Atomic.h` | renamed | `common/JackAtomic.h` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `ipc/memory/AtomicArrayState.h` | renamed | `common/JackAtomicArrayState.h` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `ipc/memory/AtomicState.h` | renamed | `common/JackAtomicState.h` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `ipc/memory/AudioPort.cpp` | renamed | `common/JackAudioPort.cpp` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `common/CString.h` | renamed | `common/JackCString.h` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `ipc/protocol/Channel.h` | renamed | `common/JackChannel.h` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `ipc/memory/ClientControl.h` | rewritten (name match) | `common/JackClientControl.h` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `ipc/memory/ClientInterface.h` | rewritten (name match) | `common/JackClientInterface.h` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `common/CompilerDeps.h` | renamed | `common/JackCompilerDeps.h` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `ipc/memory/ConnectionManager.cpp` | renamed | `common/JackConnectionManager.cpp` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `ipc/memory/ConnectionManager.h` | renamed | `common/JackConnectionManager.h` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `ipc/protocol/Constants.h` | rewritten (name match) | `common/JackConstants.h` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `ipc/memory/EngineControl.cpp` | renamed | `common/JackEngineControl.cpp` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `ipc/memory/EngineControl.h` | rewritten (name match) | `common/JackEngineControl.h` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `common/Error.cpp` | renamed | `common/JackError.cpp` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `common/Error.h` | renamed | `common/JackError.h` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `common/Exception.cpp` | renamed | `common/JackException.cpp` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `common/Exception.h` | renamed | `common/JackException.h` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `ipc/memory/FrameTimer.cpp` | rewritten (name match) | `common/JackFrameTimer.cpp` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `ipc/memory/FrameTimer.h` | rewritten (name match) | `common/JackFrameTimer.h` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `common/Globals.cpp` | rewritten (name match) | `common/JackGlobals.cpp` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `common/Globals.h` | renamed | `common/JackGlobals.h` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `ipc/memory/GraphManager.cpp` | renamed | `common/JackGraphManager.cpp` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `ipc/memory/GraphManager.h` | rewritten (name match) | `common/JackGraphManager.h` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `common/Log.cpp` | renamed | `common/JackLog.cpp` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `common/Log.h` | renamed | `common/JackLog.h` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `server/drivers/Memops.cpp` | renamed | `common/memops.cpp` | GPL-2.0 | GPL-2.0 |
| `server/drivers/Memops.h` | rewritten (name match) | `common/memops.h` | GPL-2.0 | GPL-2.0 |
| `ipc/protocol/Message.h` | renamed | `common/JackMessage.h` | GPL-2.0 | GPL-2.0 |
| `common/MessageBuffer.cpp` | renamed | `common/JackMessageBuffer.cpp` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `common/MessageBuffer.h` | renamed | `common/JackMessageBuffer.h` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `common/MidiAPI.cpp` | renamed | `common/JackMidiAPI.cpp` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `common/MidiPort.cpp` | renamed | `common/JackMidiPort.cpp` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `common/MidiPort.h` | renamed | `common/JackMidiPort.h` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `common/Mutex.h` | renamed | `common/JackMutex.h` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `ipc/protocol/Notification.h` | renamed | `common/JackNotification.h` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `common/os/Platform.h` | renamed | `common/JackPlatformPlug.h` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `ipc/memory/PortType.cpp` | renamed | `common/JackPortType.cpp` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `ipc/memory/PortType.h` | rewritten (name match) | `common/JackPortType.h` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `ipc/protocol/Request.h` | renamed | `common/JackRequest.h` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `common/Ringbuffer.cpp` | removed in the C API cleanup | `common/ringbuffer.cpp` | LGPL-2.1-or-later | — |
| `ipc/memory/ServerPort.cpp` | renamed | `common/JackPort.cpp` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `ipc/memory/ServerPort.h` | rewritten (name match) | `common/JackPort.h` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `ipc/memory/ShmMem.cpp` | renamed | `common/JackShmMem.cpp` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `ipc/memory/ShmMem.h` | renamed | `common/JackShmMem.h` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `ipc/sync/Synchro.h` | renamed | `common/JackSynchro.h` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `common/SystemDeps.h` | renamed | `common/JackSystemDeps.h` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `common/Thread.h` | renamed | `common/JackThread.h` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `common/Time.h` | rewritten (name match) | `common/JackTime.h` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `common/Tools.cpp` | renamed | `common/JackTools.cpp` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `common/Tools.h` | renamed | `common/JackTools.h` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `ipc/memory/TransportEngine.cpp` | renamed | `common/JackTransportEngine.cpp` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `ipc/memory/TransportEngine.h` | rewritten (name match) | `common/JackTransportEngine.h` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `common/Types.h` | rewritten (name match) | `common/JackTypes.h` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `common/Uuid.cpp` | kept, edited as the one UUID implementation | `common/JackUuid.cpp` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `common/Uuid.hpp` | promoted from the public type and the internal helpers | `client/api/uuid.hpp`, `common/JackUuid.cpp` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `common/jack/jslist.h` | removed in the C API cleanup | `common/jack/jslist.h` | LGPL-2.1-or-later | — |
| `common/jack/midiport.h` | removed | `common/jack/midiport.h` | LGPL-2.1-or-later | — |
| `common/jack/ringbuffer.h` | removed in the C API cleanup | `common/jack/ringbuffer.h` | LGPL-2.1-or-later | — |
| `common/jack/systemdeps.h` | removed in the C API cleanup | `common/jack/systemdeps.h` | LGPL-2.1-or-later | — |
| `common/jack/transport.h` | removed | `common/jack/transport.h` | LGPL-2.1-or-later | — |
| `common/jack/types.h` | removed in the C API cleanup | `common/jack/types.h` | LGPL-2.1-or-later | — |
| `common/jack/uuid.h` | removed in the C API cleanup | `common/jack/uuid.h` | LGPL-2.1-or-later | — |
| `common/jack/weakmacros.h` | removed | `common/jack/weakmacros.h` | LGPL-2.1-or-later | — |
| `config.h.in` | kept, edited | `config.h.in` | no notice | no notice |
| `configure.sh` | kept | `configure.sh` | no notice | no notice |
| `ipc/memory/FdSegment.cpp` | renamed | `platform/JackFdSegment.cpp` | GPL-2.0 | GPL-2.0 |
| `ipc/memory/FdSegment.h` | renamed | `platform/JackFdSegment.h` | GPL-2.0 | GPL-2.0 |
| `ipc/sync/LinuxFutex.cpp` | renamed | `platform/JackLinuxFutex.cpp` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `ipc/sync/LinuxFutex.h` | rewritten (name match) | `platform/JackLinuxFutex.h` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `common/os/LinuxTime.cpp` | renamed | `platform/JackLinuxTime.cpp` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `common/os/PosixMutex.cpp` | renamed | `platform/JackPosixMutex.cpp` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `common/os/PosixMutex.h` | renamed | `platform/JackPosixMutex.h` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `common/os/PosixProcessSync.cpp` | renamed | `platform/JackPosixProcessSync.cpp` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `common/os/PosixProcessSync.h` | rewritten (name match) | `platform/JackPosixProcessSync.h` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `common/os/PosixThread.cpp` | renamed | `platform/JackPosixThread.cpp` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `common/os/PosixThread.h` | rewritten (name match) | `platform/JackPosixThread.h` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `ipc/socket/Promiscuous.cpp` | renamed | `platform/promiscuous.cpp` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `ipc/socket/Promiscuous.hpp` | renamed | `platform/promiscuous.hpp` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `server/core/ServerLock.cpp` | renamed | `platform/JackServerLock.cpp` | GPL-2.0 | GPL-2.0 |
| `server/core/ServerLock.h` | renamed | `platform/JackServerLock.h` | GPL-2.0 | GPL-2.0 |
| `ipc/socket/Socket.cpp` | renamed | `platform/JackSocket.cpp` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `ipc/socket/Socket.h` | renamed | `platform/JackSocket.h` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `server/ipc/ServerChannels.h` | split from `common/PlatformPlug.h` | — | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `server/ipc/SocketNotifyChannel.cpp` | renamed | `platform/JackSocketNotifyChannel.cpp` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `server/ipc/SocketNotifyChannel.h` | renamed | `platform/JackSocketNotifyChannel.h` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `server/ipc/SocketServerChannel.cpp` | renamed | `platform/JackSocketServerChannel.cpp` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `server/ipc/SocketServerChannel.h` | rewritten (name match) | `platform/JackSocketServerChannel.h` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `server/ipc/SocketServerNotifyChannel.cpp` | renamed | `platform/JackSocketServerNotifyChannel.cpp` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `server/ipc/SocketServerNotifyChannel.h` | rewritten (name match) | `platform/JackSocketServerNotifyChannel.h` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `server/drivers/alsa/AlsaCardDiff.h` | renamed | `platform/alsa/JackAlsaCardDiff.h` | GPL-2.0 | GPL-2.0 |
| `server/drivers/alsa/AlsaCardFailureSink.h` | rewritten (name match) | `platform/alsa/JackAlsaCardFailureSink.h` | GPL-2.0 | GPL-2.0 |
| `server/drivers/alsa/AlsaControl.cpp` | renamed | `platform/alsa/JackAlsaControl.cpp` | GPL-2.0 | GPL-2.0 |
| `server/drivers/alsa/AlsaControl.h` | renamed | `platform/alsa/JackAlsaControl.h` | GPL-2.0 | GPL-2.0 |
| `server/drivers/alsa/AlsaControlMonitor.cpp` | renamed | `platform/alsa/JackAlsaControlMonitor.cpp` | GPL-2.0 | GPL-2.0 |
| `server/drivers/alsa/AlsaControlMonitor.h` | renamed | `platform/alsa/JackAlsaControlMonitor.h` | GPL-2.0 | GPL-2.0 |
| `server/drivers/alsa/AlsaDriftController.cpp` | renamed | `platform/alsa/JackAlsaDriftController.cpp` | GPL-2.0 | GPL-2.0 |
| `server/drivers/alsa/AlsaDriftController.h` | renamed | `platform/alsa/JackAlsaDriftController.h` | GPL-2.0 | GPL-2.0 |
| `server/drivers/alsa/AlsaDriver.cpp` | renamed | `platform/alsa/JackAlsaDriver.cpp` | GPL-2.0 | GPL-2.0 |
| `server/drivers/alsa/AlsaDriver.h` | renamed | `platform/alsa/JackAlsaDriver.h` | GPL-2.0 | GPL-2.0 |
| `server/drivers/alsa/AlsaEnumerate.cpp` | renamed | `platform/alsa/JackAlsaEnumerate.cpp` | GPL-2.0 | GPL-2.0 |
| `server/drivers/alsa/AlsaEnumerate.h` | rewritten (name match) | `platform/alsa/JackAlsaEnumerate.h` | GPL-2.0 | GPL-2.0 |
| `server/drivers/alsa/AlsaHotplug.cpp` | renamed | `platform/alsa/JackAlsaHotplug.cpp` | GPL-2.0 | GPL-2.0 |
| `server/drivers/alsa/AlsaHotplug.h` | rewritten (name match) | `platform/alsa/JackAlsaHotplug.h` | GPL-2.0 | GPL-2.0 |
| `server/drivers/alsa/AlsaRateConverter.cpp` | renamed | `platform/alsa/JackAlsaRateConverter.cpp` | GPL-2.0 | GPL-2.0 |
| `server/drivers/alsa/AlsaRateConverter.h` | renamed | `platform/alsa/JackAlsaRateConverter.h` | GPL-2.0 | GPL-2.0 |
| `server/drivers/alsa/AlsaReconfigWorker.cpp` | renamed | `platform/alsa/JackAlsaReconfigWorker.cpp` | GPL-2.0 | GPL-2.0 |
| `server/drivers/alsa/AlsaReconfigWorker.h` | renamed | `platform/alsa/JackAlsaReconfigWorker.h` | GPL-2.0 | GPL-2.0 |
| `server/drivers/alsa/AlsaRingBuffer.cpp` | renamed | `platform/alsa/JackAlsaRingBuffer.cpp` | GPL-2.0 | GPL-2.0 |
| `server/drivers/alsa/AlsaRingBuffer.h` | renamed | `platform/alsa/JackAlsaRingBuffer.h` | GPL-2.0 | GPL-2.0 |
| `server/drivers/alsa/AlsaSecondaryCard.cpp` | renamed | `platform/alsa/JackAlsaSecondaryCard.cpp` | GPL-2.0 | GPL-2.0 |
| `server/drivers/alsa/AlsaSecondaryCard.h` | renamed | `platform/alsa/JackAlsaSecondaryCard.h` | GPL-2.0 | GPL-2.0 |
| `server/drivers/alsa/AlsaSecondaryConfig.cpp` | renamed | `platform/alsa/JackAlsaSecondaryConfig.cpp` | GPL-2.0 | GPL-2.0 |
| `server/drivers/alsa/AlsaSecondaryConfig.h` | renamed | `platform/alsa/JackAlsaSecondaryConfig.h` | GPL-2.0 | GPL-2.0 |
| `server/drivers/alsa/AlsaSlotTable.h` | renamed | `platform/alsa/JackAlsaSlotTable.h` | GPL-2.0 | GPL-2.0 |
| `platform/alsa/Bitset.h` | removed | `platform/alsa/bitset.h` | GPL-2.0 | — |
| `platform/alsa/Hardware.h` | removed | `platform/alsa/hardware.h` | GPL-2.0 | — |
| `platform/alsa/Jslist.h` | removed | `platform/alsa/jslist.h` | LGPL-2.1-or-later | — |
| `server/drivers/alsa/PcmAlsa.cpp` | renamed | `platform/alsa/pcm_alsa.cpp` | GPL-2.0 | GPL-2.0 |
| `server/drivers/alsa/PcmAlsa.hpp` | renamed | `platform/alsa/pcm_alsa.hpp` | GPL-2.0 | GPL-2.0 |
| `server/drivers/alsa/PcmCycle.hpp` | renamed | `platform/alsa/pcm_cycle.hpp` | GPL-2.0 | GPL-2.0 |
| `server/drivers/alsa/PcmSession.cpp` | renamed | `platform/alsa/pcm_session.cpp` | GPL-2.0 | GPL-2.0 |
| `server/drivers/alsa/PcmSession.hpp` | renamed | `platform/alsa/pcm_session.hpp` | GPL-2.0 | GPL-2.0 |
| `server/drivers/alsa/resample/Resampler.cpp` | renamed | `platform/alsa/resample/JackResampler.cpp` | GPL-2.0 | GPL-2.0 |
| `server/drivers/alsa/resample/Resampler.h` | renamed | `platform/alsa/resample/JackResampler.h` | GPL-2.0 | GPL-2.0 |
| `server/drivers/alsa/resample/dbesi0.c` | kept | `platform/alsa/resample/dbesi0.c` | no notice | no notice |
| `server/drivers/alsa/resample/pipewire_resample_bridge.c` | kept | `platform/alsa/resample/pipewire_resample_bridge.c` | GPL-2.0 | GPL-2.0 |
| `server/drivers/alsa/resample/pipewire_resample_bridge.h` | kept | `platform/alsa/resample/pipewire_resample_bridge.h` | GPL-2.0 | GPL-2.0 |
| `server/drivers/alsa/resample/resample-native-avx2.c` | kept | `platform/alsa/resample/resample-native-avx2.c` | MIT | MIT |
| `server/drivers/alsa/resample/resample-native-c.c` | kept | `platform/alsa/resample/resample-native-c.c` | MIT | MIT |
| `server/drivers/alsa/resample/resample-native-impl.h` | kept | `platform/alsa/resample/resample-native-impl.h` | MIT | MIT |
| `server/drivers/alsa/resample/resample-native-neon.c` | kept | `platform/alsa/resample/resample-native-neon.c` | MIT | MIT |
| `server/drivers/alsa/resample/resample-native-sse.c` | kept | `platform/alsa/resample/resample-native-sse.c` | MIT | MIT |
| `server/drivers/alsa/resample/resample-native.c` | kept | `platform/alsa/resample/resample-native.c` | MIT | MIT |
| `server/drivers/alsa/resample/resample.h` | kept | `platform/alsa/resample/resample.h` | MIT | MIT |
| `server/drivers/alsa/resample/spa-shim/spa/param/audio/format.h` | kept | `platform/alsa/resample/spa-shim/spa/param/audio/format.h` | no notice | no notice |
| `server/drivers/alsa/resample/spa-shim/spa/support/cpu.h` | kept | `platform/alsa/resample/spa-shim/spa/support/cpu.h` | no notice | no notice |
| `server/drivers/alsa/resample/spa-shim/spa/support/log.h` | kept | `platform/alsa/resample/spa-shim/spa/support/log.h` | no notice | no notice |
| `server/drivers/alsa/resample/spa-shim/spa/utils/defs.h` | kept | `platform/alsa/resample/spa-shim/spa/utils/defs.h` | no notice | no notice |
| `server/drivers/alsa/resample/spa-shim/spa/utils/overflow.h` | kept | `platform/alsa/resample/spa-shim/spa/utils/overflow.h` | no notice | no notice |
| `scripts/ci.sh` | new | - | - | no notice |
| `server/drivers/AudioDriver.cpp` | renamed | `server/JackAudioDriver.cpp` | GPL-2.0 | GPL-2.0 |
| `server/drivers/AudioDriver.h` | rewritten (name match) | `server/JackAudioDriver.h` | GPL-2.0 | GPL-2.0 |
| `server/control/Control.cpp` | renamed | `server/control.cpp` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `server/drivers/Driver.cpp` | renamed | `server/JackDriver.cpp` | GPL-2.0 | GPL-2.0 |
| `server/drivers/Driver.h` | renamed | `server/JackDriver.h` | GPL-2.0 | GPL-2.0 |
| `server/drivers/DriverInfo.h` | rewritten (name match) | `server/JackDriverInfo.h` | GPL-2.0 | GPL-2.0 |
| `server/drivers/DriverInterface.h` | renamed | `server/driver_interface.h` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `server/drivers/DriverLoader.cpp` | renamed | `server/JackDriverLoader.cpp` | GPL-2.0 | GPL-2.0 |
| `server/drivers/DriverLoader.h` | rewritten (name match) | `server/JackDriverLoader.h` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `server/drivers/DriverModule.h` | renamed | `server/driver_module.h` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `server/drivers/DummyDriver.cpp` | renamed | `server/JackDummyDriver.cpp` | GPL-2.0 | GPL-2.0 |
| `server/drivers/DummyDriver.h` | renamed | `server/JackDummyDriver.h` | GPL-2.0 | GPL-2.0 |
| `server/core/Engine.cpp` | renamed | `server/JackEngine.cpp` | GPL-2.0 | GPL-2.0 |
| `server/core/Engine.h` | renamed | `server/JackEngine.h` | GPL-2.0 | GPL-2.0 |
| `server/core/ExternalClient.cpp` | renamed | `server/JackExternalClient.cpp` | GPL-2.0 | GPL-2.0 |
| `server/core/ExternalClient.h` | renamed | `server/JackExternalClient.h` | GPL-2.0 | GPL-2.0 |
| `server/drivers/FreewheelDriver.cpp` | renamed | `server/JackFreewheelDriver.cpp` | GPL-2.0 | GPL-2.0 |
| `server/drivers/FreewheelDriver.h` | renamed | `server/JackFreewheelDriver.h` | GPL-2.0 | GPL-2.0 |
| `server/Jackdmp.cpp` | kept, edited | `server/Jackdmp.cpp` | GPL-2.0 | GPL-2.0 |
| `server/core/LockedEngine.h` | renamed | `server/JackLockedEngine.h` | GPL-2.0 | GPL-2.0 |
| `server/ipc/RequestDecoder.cpp` | renamed | `server/JackRequestDecoder.cpp` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `server/ipc/RequestDecoder.h` | renamed | `server/JackRequestDecoder.h` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `server/core/ServerCore.cpp` | renamed | `server/JackServer.cpp` | GPL-2.0 | GPL-2.0 |
| `server/core/ServerCore.h` | rewritten (name match) | `server/JackServer.h` | GPL-2.0 | GPL-2.0 |
| `server/core/ServerGlobals.cpp` | rewritten (name match) | `server/JackServerGlobals.cpp` | GPL-2.0 | GPL-2.0 |
| `server/core/ServerGlobals.h` | rewritten (name match) | `server/JackServerGlobals.h` | GPL-2.0 | GPL-2.0 |
| `server/drivers/ThreadedDriver.cpp` | renamed | `server/JackThreadedDriver.cpp` | GPL-2.0 | GPL-2.0 |
| `server/drivers/ThreadedDriver.h` | rewritten (name match) | `server/JackThreadedDriver.h` | GPL-2.0 | GPL-2.0 |
| `server/drivers/TimedDriver.cpp` | renamed | `server/JackTimedDriver.cpp` | GPL-2.0 | GPL-2.0 |
| `server/drivers/TimedDriver.h` | renamed | `server/JackTimedDriver.h` | GPL-2.0 | GPL-2.0 |
| `tests/AlsaFakeBackend.hpp` | renamed | `tests/alsa_fake_backend.hpp` | GPL-2.0 | GPL-2.0 |
| `tests/AlsaSecondaryUnit.cpp` | renamed | `tests/alsa_secondary_unit.cpp` | GPL-2.0 | GPL-2.0 |
| `tests/ClientSessionE2e.cpp` | renamed | `tests/client_session_e2e.cpp` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `tests/CounterTsanUnit.cpp` | renamed | `tests/counter_tsan_unit.cpp` | GPL-2.0 | GPL-2.0 |
| `tests/Cpu.cpp` | renamed | `tests/cpu.cpp` | GPL-2.0 | GPL-2.0 |
| `tests/CstringUnit.cpp` | renamed | `tests/cstring_unit.cpp` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `tests/DeactivateGraphUnit.cpp` | renamed | `tests/deactivate_graph_unit.cpp` | GPL-2.0 | GPL-2.0 |
| `tests/ExternalMetro.cpp` | renamed | `tests/external_metro.cpp` | GPL-2.0 | GPL-2.0 |
| `tests/ExternalMetro.h` | rewritten (name match) | `tests/external_metro.h` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `tests/FdSegmentUnit.cpp` | renamed | `tests/fd_segment_unit.cpp` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `tests/Iodelay.cpp` | renamed | `tests/iodelay.cpp` | GPL-2.0 | GPL-2.0 |
| `tests/JackminiApiUnit.cpp` | renamed | `tests/jackmini_api_unit.cpp` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `tests/JackminiClientE2e.cpp` | renamed | `tests/jackmini_client_e2e.cpp` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `tests/JackminiGraphE2e.cpp` | renamed | `tests/jackmini_graph_e2e.cpp` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `tests/JackminiPortsE2e.cpp` | renamed | `tests/jackmini_ports_e2e.cpp` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `tests/JackminiUnit.cpp` | renamed | `tests/jackmini_unit.cpp` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `tests/JackminiWait.hpp` | renamed | `tests/jackmini_wait.hpp` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `tests/MemopsUnit.cpp` | renamed | `tests/memops_unit.cpp` | GPL-2.0 | GPL-2.0 |
| `tests/MessageBufferUnit.cpp` | renamed | `tests/message_buffer_unit.cpp` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `tests/MessageFrameUnit.cpp` | renamed | `tests/message_frame_unit.cpp` | GPL-2.0 | GPL-2.0 |
| `tests/MidiWriter.cpp` | renamed | `tests/midi_writer.cpp` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `tests/NotifyOverflowUnit.cpp` | renamed | `tests/notify_overflow_unit.cpp` | GPL-2.0 | GPL-2.0 |
| `tests/PcmCycleUnit.cpp` | renamed | `tests/pcm_cycle_unit.cpp` | GPL-2.0 | GPL-2.0 |
| `tests/ProtocolConformanceUnit.cpp` | renamed | `tests/protocol_conformance_unit.cpp` | GPL-2.0 | GPL-2.0 |
| `tests/RingbufferUnit.cpp` | removed in the C API cleanup | `tests/ringbuffer_unit.cpp` | GPL-2.0 | — |
| `tests/ShmLayoutUnit.cpp` | renamed | `tests/shm_layout_unit.cpp` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `tests/perf/Alloccount.cpp` | renamed | `tests/perf/alloccount.cpp` | no notice | no notice |
| `tests/perf/Rtbench.cpp` | renamed | `tests/perf/rtbench.cpp` | no notice | no notice |
| `tests/perf/run-baseline.sh` | kept | `tests/perf/run-baseline.sh` | no notice | no notice |
| `tests/tools_e2e.sh` | kept | `tests/tools_e2e.sh` | no notice | no notice |
| `tools/CMakeLists.txt` | kept, edited | `tools/CMakeLists.txt` | no notice | no notice |
| `tools/Connect.cpp` | renamed | `tools/connect.cpp` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `tools/Lsp.cpp` | renamed | `tools/lsp.cpp` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `tools/MidiDump.cpp` | renamed | `tools/midi_dump.cpp` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `tools/MonitorClient.cpp` | renamed | `tools/monitor_client.cpp` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `tools/Samplerate.cpp` | renamed | `tools/samplerate.cpp` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `tools/ToolCommon.hpp` | renamed | `tools/tool_common.hpp` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `tools/Wait.cpp` | renamed | `tools/wait.cpp` | LGPL-2.1-or-later | LGPL-2.1-or-later |

## Other files

73 files that are not source or build files (documentation, data, workflow, and the `tools/README.md`). None of them has a licence header. `COPYING` holds the GPL-2.0 text, and `server/drivers/alsa/resample/README.md` names the MIT licence of the vendored resampler. Derived ones: 73. New ones: 0.

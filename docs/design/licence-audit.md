# Licence audit

Classification of every file in the tree (`origin/develop`, `1c9a725a`) against the original JACK2 tree (`e102e682`). Input for `LICENSING.md`.

## Method

- **Lineage** comes from git: a file is *renamed* or *kept* when git pairs it with an original file (similarity of 10 % or more), and *kept* when its path is unchanged.
- **Rewritten (name match)**: git does not pair the file, but a deleted original has the same name apart from the `Jack` prefix or a case change, and the licence matches. Most of these are headers and files rewritten during the WebKit style work (PR #4), where the identifiers changed. A rewrite of a derived file is still derived, so it keeps the original licence.
- **New**: no original counterpart. The licence is ours to choose.
- **Licence now / original** is read from the header of each file (the first 40 lines). A file with no licence text is marked `no notice`.

## Summary

Files in the tree: 302. Derived from JACK2 (renamed, kept, or rewritten): 301. New: 1.

| Original licence | Files | Licence now |
|---|---|---|
| GPL-2.0 | 80 | GPL-2.0 (same) |
| LGPL-2.1-or-later | 128 | LGPL-2.1-or-later (same) |
| MIT | 7 | MIT (same) |
| no notice | 86 | no notice (same) |

Every derived file keeps the licence it had in the original tree. No licence changed.

## Findings to settle before the layout

0. **The client library contains GPL code.** `platform/FdSegment.cpp` is GPL-2.0 (in `e102e682` too, and in `origin/develop`). It is in `JACK_COMMON_SOURCES` in `CMakeLists.txt`, and `JACK_COMMON_SOURCES` is part of `JACK_CLIENT_SOURCES`, so the `jack` library (`libjack`) that clients link against is built with it. `client/ClientSession.cpp` and `common/ShmMem.h` use `FdSegment`. The client is therefore not LGPL as built, whatever its headers say. Choices: relicense `FdSegment` (the copyright holder decides, and it is derived from a GPL file), move the client's use of it out of the library, or make the client library GPL.
1. **Resampler kernels are MIT.** `platform/alsa/resample/` (`resample.h`, `resample-native*.c`, `resample-native-impl.h`) is copied from PipeWire under MIT (SPDX headers). `dbesi0.c` is Takuya Ooura's Bessel function under its own permissive notice. The plan in the handoff does not mention MIT; `LICENSING.md` must name it.
2. **`platform/alsa/resample/spa-shim/` has no licence notice.** The five headers are written as minimal subsets of PipeWire's SPA headers (MIT, per `resample/README.md`). They need an MIT notice, or a statement that they are MIT by provenance.
3. **`tests/perf/alloccount.cpp`, `tests/perf/rtbench.cpp`, `tests/perf/run-baseline.sh`, `tests/tools_e2e.sh`, `scripts/ci.sh`, `configure.sh`, and the build files have no licence notice.** Under the plan, the server code is GPL-2.0 (`COPYING`), so these fall under `COPYING` by default. `LICENSING.md` should say so, or each file should get a notice.
4. **Mixed licences in one directory.** The licence follows the file, not the directory. Exceptions to the directory's majority:
   - `common/`: GPL-2.0 are `Memops.{h,cpp}` and `Message.h`; the rest is LGPL.
   - `platform/`: GPL-2.0 are `FdSegment.{h,cpp}` and `ServerLock.{h,cpp}`; the rest is LGPL, including `Mutex.h` and `PosixMutex.{h,cpp}`.
   - `platform/alsa/`: LGPL-2.1-or-later is `Jslist.h`; the rest is GPL.
   - `server/`: LGPL-2.1-or-later are `Control.cpp`, `DriverInterface.h`, `DriverModule.h`, `DriverLoader.h`, `RequestDecoder.{h,cpp}`; the rest is GPL.
   - `tests/`: split about evenly. `tools/` is all LGPL.

   `LICENSING.md` must list these per file, not per directory.
6. **Public headers** `common/jack/*.h` are LGPL and are the ABI that integrators compile against. They stay LGPL.

## Per-file table (source and build files)

| File | Status | Origin | Original licence | Licence now |
|---|---|---|---|---|
| `CMakeLists.txt` | kept, edited | `CMakeLists.txt` | no notice | no notice |
| `client/ClientSession.cpp` | renamed | `client/client_session.cpp` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `client/ClientSession.hpp` | renamed | `client/client_session.hpp` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `client/ProcessContext.cpp` | renamed | `client/process_context.cpp` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `client/ProcessContext.hpp` | renamed | `client/process_context.hpp` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `client/ServerLaunch.cpp` | renamed | `client/JackServerLaunch.cpp` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `client/ServerLaunch.h` | renamed | `client/JackServerLaunch.h` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `client/jackmini/Client.cpp` | renamed | `client/jackmini/client.cpp` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `client/jackmini/Client.hpp` | renamed | `client/jackmini/client.hpp` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `client/jackmini/Control.hpp` | renamed | `client/jackmini/control.hpp` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `client/jackmini/Error.cpp` | renamed | `client/jackmini/error.cpp` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `client/jackmini/Error.hpp` | renamed | `client/jackmini/error.hpp` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `client/jackmini/Jackmini.hpp` | rewritten (name match) | `client/jackmini/jackmini.hpp` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `client/jackmini/Midi.hpp` | renamed | `client/jackmini/midi.hpp` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `client/jackmini/Options.hpp` | renamed | `client/jackmini/options.hpp` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `client/jackmini/Port.hpp` | renamed | `client/jackmini/port.hpp` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `client/jackmini/Ringbuffer.hpp` | renamed | `client/jackmini/ringbuffer.hpp` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `client/jackmini/Transport.hpp` | renamed | `client/jackmini/transport.hpp` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `client/jackmini/Uuid.cpp` | renamed | `client/jackmini/uuid.cpp` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `client/jackmini/Uuid.hpp` | renamed | `client/jackmini/uuid.hpp` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `common/ActivationCount.cpp` | renamed | `common/JackActivationCount.cpp` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `common/ActivationCount.h` | renamed | `common/JackActivationCount.h` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `common/Atomic.h` | renamed | `common/JackAtomic.h` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `common/AtomicArrayState.h` | renamed | `common/JackAtomicArrayState.h` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `common/AtomicState.h` | renamed | `common/JackAtomicState.h` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `common/AudioPort.cpp` | renamed | `common/JackAudioPort.cpp` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `common/CString.h` | renamed | `common/JackCString.h` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `common/Channel.h` | renamed | `common/JackChannel.h` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `common/ClientControl.h` | rewritten (name match) | `common/JackClientControl.h` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `common/ClientInterface.h` | rewritten (name match) | `common/JackClientInterface.h` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `common/CompilerDeps.h` | renamed | `common/JackCompilerDeps.h` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `common/ConnectionManager.cpp` | renamed | `common/JackConnectionManager.cpp` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `common/ConnectionManager.h` | renamed | `common/JackConnectionManager.h` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `common/Constants.h` | rewritten (name match) | `common/JackConstants.h` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `common/EngineControl.cpp` | renamed | `common/JackEngineControl.cpp` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `common/EngineControl.h` | rewritten (name match) | `common/JackEngineControl.h` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `common/Error.cpp` | renamed | `common/JackError.cpp` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `common/Error.h` | renamed | `common/JackError.h` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `common/Exception.cpp` | renamed | `common/JackException.cpp` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `common/Exception.h` | renamed | `common/JackException.h` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `common/FrameTimer.cpp` | rewritten (name match) | `common/JackFrameTimer.cpp` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `common/FrameTimer.h` | rewritten (name match) | `common/JackFrameTimer.h` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `common/Globals.cpp` | rewritten (name match) | `common/JackGlobals.cpp` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `common/Globals.h` | renamed | `common/JackGlobals.h` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `common/GraphManager.cpp` | renamed | `common/JackGraphManager.cpp` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `common/GraphManager.h` | rewritten (name match) | `common/JackGraphManager.h` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `common/Log.cpp` | renamed | `common/JackLog.cpp` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `common/Log.h` | renamed | `common/JackLog.h` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `common/Memops.cpp` | renamed | `common/memops.cpp` | GPL-2.0 | GPL-2.0 |
| `common/Memops.h` | rewritten (name match) | `common/memops.h` | GPL-2.0 | GPL-2.0 |
| `common/Message.h` | renamed | `common/JackMessage.h` | GPL-2.0 | GPL-2.0 |
| `common/MessageBuffer.cpp` | renamed | `common/JackMessageBuffer.cpp` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `common/MessageBuffer.h` | renamed | `common/JackMessageBuffer.h` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `common/MidiAPI.cpp` | renamed | `common/JackMidiAPI.cpp` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `common/MidiPort.cpp` | renamed | `common/JackMidiPort.cpp` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `common/MidiPort.h` | renamed | `common/JackMidiPort.h` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `common/Mutex.h` | renamed | `common/JackMutex.h` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `common/Notification.h` | renamed | `common/JackNotification.h` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `common/PlatformPlug.h` | renamed | `common/JackPlatformPlug.h` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `common/PortType.cpp` | renamed | `common/JackPortType.cpp` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `common/PortType.h` | rewritten (name match) | `common/JackPortType.h` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `common/Request.h` | renamed | `common/JackRequest.h` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `common/Ringbuffer.cpp` | renamed | `common/ringbuffer.cpp` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `common/ServerPort.cpp` | renamed | `common/JackPort.cpp` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `common/ServerPort.h` | rewritten (name match) | `common/JackPort.h` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `common/ShmMem.cpp` | renamed | `common/JackShmMem.cpp` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `common/ShmMem.h` | renamed | `common/JackShmMem.h` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `common/Synchro.h` | renamed | `common/JackSynchro.h` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `common/SystemDeps.h` | renamed | `common/JackSystemDeps.h` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `common/Thread.h` | renamed | `common/JackThread.h` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `common/Time.h` | rewritten (name match) | `common/JackTime.h` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `common/Tools.cpp` | renamed | `common/JackTools.cpp` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `common/Tools.h` | renamed | `common/JackTools.h` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `common/TransportEngine.cpp` | renamed | `common/JackTransportEngine.cpp` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `common/TransportEngine.h` | rewritten (name match) | `common/JackTransportEngine.h` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `common/Types.h` | rewritten (name match) | `common/JackTypes.h` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `common/Uuid.cpp` | renamed | `common/JackUuid.cpp` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `common/jack/jslist.h` | kept, edited | `common/jack/jslist.h` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `common/jack/midiport.h` | kept, edited | `common/jack/midiport.h` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `common/jack/ringbuffer.h` | kept, edited | `common/jack/ringbuffer.h` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `common/jack/systemdeps.h` | kept, edited | `common/jack/systemdeps.h` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `common/jack/transport.h` | kept, edited | `common/jack/transport.h` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `common/jack/types.h` | kept, edited | `common/jack/types.h` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `common/jack/uuid.h` | kept, edited | `common/jack/uuid.h` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `common/jack/weakmacros.h` | kept, edited | `common/jack/weakmacros.h` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `config.h.in` | kept, edited | `config.h.in` | no notice | no notice |
| `configure.sh` | kept | `configure.sh` | no notice | no notice |
| `platform/FdSegment.cpp` | renamed | `platform/JackFdSegment.cpp` | GPL-2.0 | GPL-2.0 |
| `platform/FdSegment.h` | renamed | `platform/JackFdSegment.h` | GPL-2.0 | GPL-2.0 |
| `platform/LinuxFutex.cpp` | renamed | `platform/JackLinuxFutex.cpp` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `platform/LinuxFutex.h` | rewritten (name match) | `platform/JackLinuxFutex.h` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `platform/LinuxTime.cpp` | renamed | `platform/JackLinuxTime.cpp` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `platform/PosixMutex.cpp` | renamed | `platform/JackPosixMutex.cpp` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `platform/PosixMutex.h` | renamed | `platform/JackPosixMutex.h` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `platform/PosixProcessSync.cpp` | renamed | `platform/JackPosixProcessSync.cpp` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `platform/PosixProcessSync.h` | rewritten (name match) | `platform/JackPosixProcessSync.h` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `platform/PosixThread.cpp` | renamed | `platform/JackPosixThread.cpp` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `platform/PosixThread.h` | rewritten (name match) | `platform/JackPosixThread.h` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `platform/Promiscuous.cpp` | renamed | `platform/promiscuous.cpp` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `platform/Promiscuous.hpp` | renamed | `platform/promiscuous.hpp` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `platform/ServerLock.cpp` | renamed | `platform/JackServerLock.cpp` | GPL-2.0 | GPL-2.0 |
| `platform/ServerLock.h` | renamed | `platform/JackServerLock.h` | GPL-2.0 | GPL-2.0 |
| `platform/Socket.cpp` | renamed | `platform/JackSocket.cpp` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `platform/Socket.h` | renamed | `platform/JackSocket.h` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `platform/SocketNotifyChannel.cpp` | renamed | `platform/JackSocketNotifyChannel.cpp` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `platform/SocketNotifyChannel.h` | renamed | `platform/JackSocketNotifyChannel.h` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `platform/SocketServerChannel.cpp` | renamed | `platform/JackSocketServerChannel.cpp` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `platform/SocketServerChannel.h` | rewritten (name match) | `platform/JackSocketServerChannel.h` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `platform/SocketServerNotifyChannel.cpp` | renamed | `platform/JackSocketServerNotifyChannel.cpp` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `platform/SocketServerNotifyChannel.h` | rewritten (name match) | `platform/JackSocketServerNotifyChannel.h` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `platform/alsa/AlsaCardDiff.h` | renamed | `platform/alsa/JackAlsaCardDiff.h` | GPL-2.0 | GPL-2.0 |
| `platform/alsa/AlsaCardFailureSink.h` | rewritten (name match) | `platform/alsa/JackAlsaCardFailureSink.h` | GPL-2.0 | GPL-2.0 |
| `platform/alsa/AlsaControl.cpp` | renamed | `platform/alsa/JackAlsaControl.cpp` | GPL-2.0 | GPL-2.0 |
| `platform/alsa/AlsaControl.h` | renamed | `platform/alsa/JackAlsaControl.h` | GPL-2.0 | GPL-2.0 |
| `platform/alsa/AlsaControlMonitor.cpp` | renamed | `platform/alsa/JackAlsaControlMonitor.cpp` | GPL-2.0 | GPL-2.0 |
| `platform/alsa/AlsaControlMonitor.h` | renamed | `platform/alsa/JackAlsaControlMonitor.h` | GPL-2.0 | GPL-2.0 |
| `platform/alsa/AlsaDriftController.cpp` | renamed | `platform/alsa/JackAlsaDriftController.cpp` | GPL-2.0 | GPL-2.0 |
| `platform/alsa/AlsaDriftController.h` | renamed | `platform/alsa/JackAlsaDriftController.h` | GPL-2.0 | GPL-2.0 |
| `platform/alsa/AlsaDriver.cpp` | renamed | `platform/alsa/JackAlsaDriver.cpp` | GPL-2.0 | GPL-2.0 |
| `platform/alsa/AlsaDriver.h` | renamed | `platform/alsa/JackAlsaDriver.h` | GPL-2.0 | GPL-2.0 |
| `platform/alsa/AlsaEnumerate.cpp` | renamed | `platform/alsa/JackAlsaEnumerate.cpp` | GPL-2.0 | GPL-2.0 |
| `platform/alsa/AlsaEnumerate.h` | rewritten (name match) | `platform/alsa/JackAlsaEnumerate.h` | GPL-2.0 | GPL-2.0 |
| `platform/alsa/AlsaHotplug.cpp` | renamed | `platform/alsa/JackAlsaHotplug.cpp` | GPL-2.0 | GPL-2.0 |
| `platform/alsa/AlsaHotplug.h` | rewritten (name match) | `platform/alsa/JackAlsaHotplug.h` | GPL-2.0 | GPL-2.0 |
| `platform/alsa/AlsaRateConverter.cpp` | renamed | `platform/alsa/JackAlsaRateConverter.cpp` | GPL-2.0 | GPL-2.0 |
| `platform/alsa/AlsaRateConverter.h` | renamed | `platform/alsa/JackAlsaRateConverter.h` | GPL-2.0 | GPL-2.0 |
| `platform/alsa/AlsaReconfigWorker.cpp` | renamed | `platform/alsa/JackAlsaReconfigWorker.cpp` | GPL-2.0 | GPL-2.0 |
| `platform/alsa/AlsaReconfigWorker.h` | renamed | `platform/alsa/JackAlsaReconfigWorker.h` | GPL-2.0 | GPL-2.0 |
| `platform/alsa/AlsaRingBuffer.cpp` | renamed | `platform/alsa/JackAlsaRingBuffer.cpp` | GPL-2.0 | GPL-2.0 |
| `platform/alsa/AlsaRingBuffer.h` | renamed | `platform/alsa/JackAlsaRingBuffer.h` | GPL-2.0 | GPL-2.0 |
| `platform/alsa/AlsaSecondaryCard.cpp` | renamed | `platform/alsa/JackAlsaSecondaryCard.cpp` | GPL-2.0 | GPL-2.0 |
| `platform/alsa/AlsaSecondaryCard.h` | renamed | `platform/alsa/JackAlsaSecondaryCard.h` | GPL-2.0 | GPL-2.0 |
| `platform/alsa/AlsaSecondaryConfig.cpp` | renamed | `platform/alsa/JackAlsaSecondaryConfig.cpp` | GPL-2.0 | GPL-2.0 |
| `platform/alsa/AlsaSecondaryConfig.h` | renamed | `platform/alsa/JackAlsaSecondaryConfig.h` | GPL-2.0 | GPL-2.0 |
| `platform/alsa/AlsaSlotTable.h` | renamed | `platform/alsa/JackAlsaSlotTable.h` | GPL-2.0 | GPL-2.0 |
| `platform/alsa/Bitset.h` | renamed | `platform/alsa/bitset.h` | GPL-2.0 | GPL-2.0 |
| `platform/alsa/Hardware.h` | renamed | `platform/alsa/hardware.h` | GPL-2.0 | GPL-2.0 |
| `platform/alsa/Jslist.h` | renamed | `platform/alsa/jslist.h` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `platform/alsa/PcmAlsa.cpp` | renamed | `platform/alsa/pcm_alsa.cpp` | GPL-2.0 | GPL-2.0 |
| `platform/alsa/PcmAlsa.hpp` | renamed | `platform/alsa/pcm_alsa.hpp` | GPL-2.0 | GPL-2.0 |
| `platform/alsa/PcmCycle.hpp` | renamed | `platform/alsa/pcm_cycle.hpp` | GPL-2.0 | GPL-2.0 |
| `platform/alsa/PcmSession.cpp` | renamed | `platform/alsa/pcm_session.cpp` | GPL-2.0 | GPL-2.0 |
| `platform/alsa/PcmSession.hpp` | renamed | `platform/alsa/pcm_session.hpp` | GPL-2.0 | GPL-2.0 |
| `platform/alsa/resample/Resampler.cpp` | renamed | `platform/alsa/resample/JackResampler.cpp` | GPL-2.0 | GPL-2.0 |
| `platform/alsa/resample/Resampler.h` | renamed | `platform/alsa/resample/JackResampler.h` | GPL-2.0 | GPL-2.0 |
| `platform/alsa/resample/dbesi0.c` | kept | `platform/alsa/resample/dbesi0.c` | no notice | no notice |
| `platform/alsa/resample/pipewire_resample_bridge.c` | kept | `platform/alsa/resample/pipewire_resample_bridge.c` | GPL-2.0 | GPL-2.0 |
| `platform/alsa/resample/pipewire_resample_bridge.h` | kept | `platform/alsa/resample/pipewire_resample_bridge.h` | GPL-2.0 | GPL-2.0 |
| `platform/alsa/resample/resample-native-avx2.c` | kept | `platform/alsa/resample/resample-native-avx2.c` | MIT | MIT |
| `platform/alsa/resample/resample-native-c.c` | kept | `platform/alsa/resample/resample-native-c.c` | MIT | MIT |
| `platform/alsa/resample/resample-native-impl.h` | kept | `platform/alsa/resample/resample-native-impl.h` | MIT | MIT |
| `platform/alsa/resample/resample-native-neon.c` | kept | `platform/alsa/resample/resample-native-neon.c` | MIT | MIT |
| `platform/alsa/resample/resample-native-sse.c` | kept | `platform/alsa/resample/resample-native-sse.c` | MIT | MIT |
| `platform/alsa/resample/resample-native.c` | kept | `platform/alsa/resample/resample-native.c` | MIT | MIT |
| `platform/alsa/resample/resample.h` | kept | `platform/alsa/resample/resample.h` | MIT | MIT |
| `platform/alsa/resample/spa-shim/spa/param/audio/format.h` | kept | `platform/alsa/resample/spa-shim/spa/param/audio/format.h` | no notice | no notice |
| `platform/alsa/resample/spa-shim/spa/support/cpu.h` | kept | `platform/alsa/resample/spa-shim/spa/support/cpu.h` | no notice | no notice |
| `platform/alsa/resample/spa-shim/spa/support/log.h` | kept | `platform/alsa/resample/spa-shim/spa/support/log.h` | no notice | no notice |
| `platform/alsa/resample/spa-shim/spa/utils/defs.h` | kept | `platform/alsa/resample/spa-shim/spa/utils/defs.h` | no notice | no notice |
| `platform/alsa/resample/spa-shim/spa/utils/overflow.h` | kept | `platform/alsa/resample/spa-shim/spa/utils/overflow.h` | no notice | no notice |
| `scripts/ci.sh` | new | - | - | no notice |
| `server/AudioDriver.cpp` | renamed | `server/JackAudioDriver.cpp` | GPL-2.0 | GPL-2.0 |
| `server/AudioDriver.h` | rewritten (name match) | `server/JackAudioDriver.h` | GPL-2.0 | GPL-2.0 |
| `server/Control.cpp` | renamed | `server/control.cpp` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `server/Driver.cpp` | renamed | `server/JackDriver.cpp` | GPL-2.0 | GPL-2.0 |
| `server/Driver.h` | renamed | `server/JackDriver.h` | GPL-2.0 | GPL-2.0 |
| `server/DriverInfo.h` | rewritten (name match) | `server/JackDriverInfo.h` | GPL-2.0 | GPL-2.0 |
| `server/DriverInterface.h` | renamed | `server/driver_interface.h` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `server/DriverLoader.cpp` | renamed | `server/JackDriverLoader.cpp` | GPL-2.0 | GPL-2.0 |
| `server/DriverLoader.h` | rewritten (name match) | `server/JackDriverLoader.h` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `server/DriverModule.h` | renamed | `server/driver_module.h` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `server/DummyDriver.cpp` | renamed | `server/JackDummyDriver.cpp` | GPL-2.0 | GPL-2.0 |
| `server/DummyDriver.h` | renamed | `server/JackDummyDriver.h` | GPL-2.0 | GPL-2.0 |
| `server/Engine.cpp` | renamed | `server/JackEngine.cpp` | GPL-2.0 | GPL-2.0 |
| `server/Engine.h` | renamed | `server/JackEngine.h` | GPL-2.0 | GPL-2.0 |
| `server/ExternalClient.cpp` | renamed | `server/JackExternalClient.cpp` | GPL-2.0 | GPL-2.0 |
| `server/ExternalClient.h` | renamed | `server/JackExternalClient.h` | GPL-2.0 | GPL-2.0 |
| `server/FreewheelDriver.cpp` | renamed | `server/JackFreewheelDriver.cpp` | GPL-2.0 | GPL-2.0 |
| `server/FreewheelDriver.h` | renamed | `server/JackFreewheelDriver.h` | GPL-2.0 | GPL-2.0 |
| `server/Jackdmp.cpp` | kept, edited | `server/Jackdmp.cpp` | GPL-2.0 | GPL-2.0 |
| `server/LockedEngine.h` | renamed | `server/JackLockedEngine.h` | GPL-2.0 | GPL-2.0 |
| `server/RequestDecoder.cpp` | renamed | `server/JackRequestDecoder.cpp` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `server/RequestDecoder.h` | renamed | `server/JackRequestDecoder.h` | LGPL-2.1-or-later | LGPL-2.1-or-later |
| `server/ServerCore.cpp` | renamed | `server/JackServer.cpp` | GPL-2.0 | GPL-2.0 |
| `server/ServerCore.h` | rewritten (name match) | `server/JackServer.h` | GPL-2.0 | GPL-2.0 |
| `server/ServerGlobals.cpp` | rewritten (name match) | `server/JackServerGlobals.cpp` | GPL-2.0 | GPL-2.0 |
| `server/ServerGlobals.h` | rewritten (name match) | `server/JackServerGlobals.h` | GPL-2.0 | GPL-2.0 |
| `server/ThreadedDriver.cpp` | renamed | `server/JackThreadedDriver.cpp` | GPL-2.0 | GPL-2.0 |
| `server/ThreadedDriver.h` | rewritten (name match) | `server/JackThreadedDriver.h` | GPL-2.0 | GPL-2.0 |
| `server/TimedDriver.cpp` | renamed | `server/JackTimedDriver.cpp` | GPL-2.0 | GPL-2.0 |
| `server/TimedDriver.h` | renamed | `server/JackTimedDriver.h` | GPL-2.0 | GPL-2.0 |
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
| `tests/RingbufferUnit.cpp` | renamed | `tests/ringbuffer_unit.cpp` | GPL-2.0 | GPL-2.0 |
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

73 files that are not source or build files (documentation, data, workflow, and the `tools/README.md`). None of them has a licence header. `COPYING` holds the GPL-2.0 text, and `platform/alsa/resample/README.md` names the MIT licence of the vendored resampler. Derived ones: 73. New ones: 0.

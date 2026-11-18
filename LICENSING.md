# Licensing

jack2-mini is three things with three licences. Which licence applies to a file is the one in its header;
the lists below say the same thing for every source file, because one directory can mix licences.

| Part | Licence | Text |
|---|---|---|
| Server: `jackd`, `server/`, the drivers, the ALSA driver, and the other GPL files (list below) | GPL-2.0 | `COPYING` |
| Client library `libjack` and its public API: `client/`, `common/jack/`, the shared `common/os/` and `ipc/` code, and the other LGPL files (list below) | LGPL-2.1-or-later | `COPYING.LGPL` |
| Vendored PipeWire resampler (`server/drivers/alsa/resample/`) | MIT | `server/drivers/alsa/resample/README.md` |
| Bessel function `server/drivers/alsa/resample/dbesi0.c` (Takuya Ooura) | Permissive notice in the file | `server/drivers/alsa/resample/dbesi0.c` |
| Build files, scripts, and perf tools with no licence header | GPL-2.0 (by default) | `COPYING` |

jack2-mini is derived from JACK2. The original licences of the derived files are kept. Files that are
relicensed in this tree are listed below. The copyright and contributor lines are kept as they were: see
`AUTHORS.rst`, and the header of each file.

## Why the client library is LGPL

The client library must not force integrators to release their own code under the GPL. It is therefore LGPL-2.1-or-later,
and its build must not link any GPL-2.0 file. The original JACK2 client code is LGPL too. Every file that `libjack` is
built from is LGPL-2.1-or-later, and so is every header that a client includes.

`ipc/memory/FdSegment.{h,cpp}` and `ipc/protocol/Message.h` were GPL-2.0. They are now LGPL-2.1-or-later, so that `libjack`
is LGPL throughout. The change is by Mose Holm (2026); the existing copyright lines in the headers are kept.

The GPL headers say "version 2 ... or (at your option) any later version". `COPYING` is the version 2 text.
The licence is therefore GPL-2.0 as stated in the headers.

## LGPL-2.1-or-later

The client library, its public headers, and the shared code that it is built from.

- `client/api/`: `Client.cpp`, `Client.hpp`, `Error.cpp`, `Error.hpp`, `Jackmini.hpp`, `Midi.hpp`, `Options.hpp`, `Port.hpp`, `Ringbuffer.hpp`, `Transport.hpp`, `Uuid.cpp`, `Uuid.hpp`
- `client/session/`: `ClientSession.cpp`, `ClientSession.hpp`, `ProcessContext.cpp`, `ProcessContext.hpp`
- `common/`: `CompilerDeps.h`, `CString.h`, `Error.cpp`, `Error.h`, `Exception.cpp`, `Exception.h`, `Globals.cpp`, `Globals.h`, `Log.cpp`, `Log.h`, `MessageBuffer.cpp`, `MessageBuffer.h`, `MidiAPI.cpp`, `MidiPort.cpp`, `MidiPort.h`, `Mutex.h`, `Ringbuffer.cpp`, `SystemDeps.h`, `Thread.h`, `Time.h`, `Tools.cpp`, `Tools.h`, `Types.h`, `Uuid.cpp`
- `common/jack/`: `jslist.h`, `ringbuffer.h`, `systemdeps.h`, `types.h`, `uuid.h`
- `common/os/`: `LinuxTime.cpp`, `Platform.h`, `PosixMutex.cpp`, `PosixMutex.h`, `PosixProcessSync.cpp`, `PosixProcessSync.h`, `PosixThread.cpp`, `PosixThread.h`
- `ipc/memory/`: `ActivationCount.cpp`, `ActivationCount.h`, `Atomic.h`, `AtomicArrayState.h`, `AtomicState.h`, `AudioPort.cpp`, `ClientControl.h`, `ClientInterface.h`, `ConnectionManager.cpp`, `ConnectionManager.h`, `EngineControl.cpp`, `EngineControl.h`, `FdSegment.cpp`, `FdSegment.h`, `FrameTimer.cpp`, `FrameTimer.h`, `GraphManager.cpp`, `GraphManager.h`, `PortType.cpp`, `PortType.h`, `ServerPort.cpp`, `ServerPort.h`, `ShmMem.cpp`, `ShmMem.h`, `TransportEngine.cpp`, `TransportEngine.h`
- `ipc/protocol/`: `Channel.h`, `Constants.h`, `Message.h`, `Notification.h`, `Request.h`
- `ipc/socket/`: `Promiscuous.cpp`, `Promiscuous.hpp`, `Socket.cpp`, `Socket.h`
- `ipc/sync/`: `LinuxFutex.cpp`, `LinuxFutex.h`, `Synchro.h`
- `server/control/`: `Control.cpp`, `Control.hpp`
- `server/drivers/`: `DriverInterface.h`, `DriverLoader.h`, `DriverModule.h`
- `server/ipc/`: `RequestDecoder.cpp`, `RequestDecoder.h`, `ServerChannels.h`, `SocketNotifyChannel.cpp`, `SocketNotifyChannel.h`, `SocketServerChannel.cpp`, `SocketServerChannel.h`, `SocketServerNotifyChannel.cpp`, `SocketServerNotifyChannel.h`
- `tests/`: `ClientSessionE2e.cpp`, `CstringUnit.cpp`, `ExternalMetro.h`, `FdSegmentUnit.cpp`, `JackminiApiUnit.cpp`, `JackminiClientE2e.cpp`, `JackminiGraphE2e.cpp`, `JackminiPortsE2e.cpp`, `JackminiUnit.cpp`, `JackminiWait.hpp`, `MessageBufferUnit.cpp`, `MidiWriter.cpp`, `ShmLayoutUnit.cpp`
- `tools/`: `Connect.cpp`, `Lsp.cpp`, `MidiDump.cpp`, `MonitorClient.cpp`, `Samplerate.cpp`, `ToolCommon.hpp`, `Wait.cpp`

## GPL-2.0

The server, the drivers, and the ALSA driver. See `COPYING`.

- `server/`: `Jackdmp.cpp`
- `server/core/`: `Engine.cpp`, `Engine.h`, `ExternalClient.cpp`, `ExternalClient.h`, `LockedEngine.h`, `ServerCore.cpp`, `ServerCore.h`, `ServerGlobals.cpp`, `ServerGlobals.h`, `ServerLock.cpp`, `ServerLock.h`
- `server/drivers/`: `AudioDriver.cpp`, `AudioDriver.h`, `Driver.cpp`, `Driver.h`, `DriverInfo.h`, `DriverLoader.cpp`, `DummyDriver.cpp`, `DummyDriver.h`, `FreewheelDriver.cpp`, `FreewheelDriver.h`, `Memops.cpp`, `Memops.h`, `ThreadedDriver.cpp`, `ThreadedDriver.h`, `TimedDriver.cpp`, `TimedDriver.h`
- `server/drivers/alsa/`: `AlsaCardDiff.h`, `AlsaCardFailureSink.h`, `AlsaControl.cpp`, `AlsaControl.h`, `AlsaControlMonitor.cpp`, `AlsaControlMonitor.h`, `AlsaDriftController.cpp`, `AlsaDriftController.h`, `AlsaDriver.cpp`, `AlsaDriver.h`, `AlsaEnumerate.cpp`, `AlsaEnumerate.h`, `AlsaHotplug.cpp`, `AlsaHotplug.h`, `AlsaRateConverter.cpp`, `AlsaRateConverter.h`, `AlsaReconfigWorker.cpp`, `AlsaReconfigWorker.h`, `AlsaRingBuffer.cpp`, `AlsaRingBuffer.h`, `AlsaSecondaryCard.cpp`, `AlsaSecondaryCard.h`, `AlsaSecondaryConfig.cpp`, `AlsaSecondaryConfig.h`, `AlsaSlotTable.h`, `PcmAlsa.cpp`, `PcmAlsa.hpp`, `PcmCycle.hpp`, `PcmSession.cpp`, `PcmSession.hpp`
- `server/drivers/alsa/resample/`: `Resampler.cpp`, `Resampler.h`, `pipewire_resample_bridge.c`, `pipewire_resample_bridge.h`
- `tests/`: `AlsaFakeBackend.hpp`, `AlsaSecondaryUnit.cpp`, `CounterTsanUnit.cpp`, `Cpu.cpp`, `DeactivateGraphUnit.cpp`, `ExternalMetro.cpp`, `Iodelay.cpp`, `MemopsUnit.cpp`, `MessageFrameUnit.cpp`, `NotifyOverflowUnit.cpp`, `PcmCycleUnit.cpp`, `ProtocolConformanceUnit.cpp`, `RingbufferUnit.cpp`

## MIT

The vendored PipeWire resampler, under the MIT licence (`server/drivers/alsa/resample/README.md`). Each file has an
SPDX header: `SPDX-License-Identifier: MIT`.

- `server/drivers/alsa/resample/`: `resample-native-avx2.c`, `resample-native-c.c`, `resample-native-impl.h`, `resample-native-neon.c`, `resample-native-sse.c`, `resample-native.c`, `resample.h`

`server/drivers/alsa/resample/dbesi0.c` is Takuya Ooura's Bessel function, under the notice in the file.
`server/drivers/alsa/resample/spa-shim/` holds minimal subsets of PipeWire's SPA headers. They are not licence-tagged
themselves; they are MIT by provenance (see the resampler README).

## No licence header

These files have no licence notice of their own. They are GPL-2.0 under `COPYING`, except the
PipeWire-derived shim headers, which are MIT by provenance.

- `./`: `CMakeLists.txt`, `config.h.in`, `configure.sh`
- `.github/ISSUE_TEMPLATE/`: `bug_report.md`, `feature_request.md`
- `server/drivers/alsa/resample/`: `dbesi0.c`, `README.md`
- `server/drivers/alsa/resample/spa-shim/spa/param/audio/`: `format.h`
- `server/drivers/alsa/resample/spa-shim/spa/support/`: `cpu.h`, `log.h`
- `server/drivers/alsa/resample/spa-shim/spa/utils/`: `defs.h`, `overflow.h`
- `scripts/`: `ci.sh`
- `tests/perf/`: `Alloccount.cpp`, `Rtbench.cpp`, `run-baseline.sh`
- `tests/`: `tools_e2e.sh`
- `tools/`: `CMakeLists.txt`, `README.md`

## Changing this document

When a file is added, put its licence in its header, and add it to the list above. A new file that is
derived from a file of another licence keeps that licence.

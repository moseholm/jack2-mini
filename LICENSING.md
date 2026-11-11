# Licensing

jack2-mini is three things with three licences. Which licence applies to a file is the one in its header;
the lists below say the same thing for every source file, because one directory can mix licences.

| Part | Licence | Text |
|---|---|---|
| Server: `jackd`, `server/`, the drivers, the ALSA driver, and the other GPL files (list below) | GPL-2.0 | `COPYING` |
| Client library `libjack` and its public API: `client/`, `common/jack/`, and the other LGPL files (list below) | LGPL-2.1-or-later | `COPYING.LGPL` |
| Vendored PipeWire resampler (`platform/alsa/resample/`) | MIT | `platform/alsa/resample/README.md` |
| Bessel function `platform/alsa/resample/dbesi0.c` (Takuya Ooura) | Permissive notice in the file | `platform/alsa/resample/dbesi0.c` |
| Build files, scripts, and perf tools with no licence header | GPL-2.0 (by default) | `COPYING` |

jack2-mini is derived from JACK2. The original licences of the derived files are kept. Files that are
relicensed in this tree are listed below. The copyright and contributor lines are kept as they were: see
`AUTHORS.rst`, and the header of each file.

## Why the client library is LGPL

The client library must not force integrators to release their own code under the GPL. It is therefore LGPL-2.1-or-later,
and its build must not link any GPL-2.0 file. The original JACK2 client code is LGPL too. Every file that `libjack` is
built from is LGPL-2.1-or-later, and so is every header that a client includes.

`platform/FdSegment.{h,cpp}` and `common/Message.h` were GPL-2.0. They are now LGPL-2.1-or-later, so that `libjack`
is LGPL throughout. The change is by Mose Holm (2026); the existing copyright lines in the headers are kept.

The GPL headers say "version 2 ... or (at your option) any later version". `COPYING` is the version 2 text.
The licence is therefore GPL-2.0 as stated in the headers.

## LGPL-2.1-or-later

The client library, its public headers, and the shared code that it is built from.

- `client/`: `ClientSession.cpp`, `ClientSession.hpp`, `ProcessContext.cpp`, `ProcessContext.hpp`, `ServerLaunch.cpp`, `ServerLaunch.h`
- `client/jackmini/`: `Client.cpp`, `Client.hpp`, `Control.hpp`, `Error.cpp`, `Error.hpp`, `Jackmini.hpp`, `Midi.hpp`, `Options.hpp`, `Port.hpp`, `Ringbuffer.hpp`, `Transport.hpp`, `Uuid.cpp`, `Uuid.hpp`
- `common/`: `ActivationCount.cpp`, `ActivationCount.h`, `Atomic.h`, `AtomicArrayState.h`, `AtomicState.h`, `AudioPort.cpp`, `CString.h`, `Channel.h`, `ClientControl.h`, `ClientInterface.h`, `CompilerDeps.h`, `ConnectionManager.cpp`, `ConnectionManager.h`, `Constants.h`, `EngineControl.cpp`, `EngineControl.h`, `Error.cpp`, `Error.h`, `Exception.cpp`, `Exception.h`, `FrameTimer.cpp`, `FrameTimer.h`, `Globals.cpp`, `Globals.h`, `GraphManager.cpp`, `GraphManager.h`, `Log.cpp`, `Log.h`, `Message.h`, `MessageBuffer.cpp`, `MessageBuffer.h`, `MidiAPI.cpp`, `MidiPort.cpp`, `MidiPort.h`, `Mutex.h`, `Notification.h`, `PlatformPlug.h`, `PortType.cpp`, `PortType.h`, `Request.h`, `Ringbuffer.cpp`, `ServerPort.cpp`, `ServerPort.h`, `ShmMem.cpp`, `ShmMem.h`, `Synchro.h`, `SystemDeps.h`, `Thread.h`, `Time.h`, `Tools.cpp`, `Tools.h`, `TransportEngine.cpp`, `TransportEngine.h`, `Types.h`, `Uuid.cpp`
- `common/jack/`: `jslist.h`, `midiport.h`, `ringbuffer.h`, `systemdeps.h`, `transport.h`, `types.h`, `uuid.h`, `weakmacros.h`
- `platform/`: `FdSegment.cpp`, `FdSegment.h`, `LinuxFutex.cpp`, `LinuxFutex.h`, `LinuxTime.cpp`, `PosixMutex.cpp`, `PosixMutex.h`, `PosixProcessSync.cpp`, `PosixProcessSync.h`, `PosixThread.cpp`, `PosixThread.h`, `Promiscuous.cpp`, `Promiscuous.hpp`, `Socket.cpp`, `Socket.h`, `SocketNotifyChannel.cpp`, `SocketNotifyChannel.h`, `SocketServerChannel.cpp`, `SocketServerChannel.h`, `SocketServerNotifyChannel.cpp`, `SocketServerNotifyChannel.h`
- `platform/alsa/`: `Jslist.h`
- `server/`: `Control.cpp`, `DriverInterface.h`, `DriverLoader.h`, `DriverModule.h`, `RequestDecoder.cpp`, `RequestDecoder.h`
- `tests/`: `ClientSessionE2e.cpp`, `CstringUnit.cpp`, `ExternalMetro.h`, `FdSegmentUnit.cpp`, `JackminiApiUnit.cpp`, `JackminiClientE2e.cpp`, `JackminiGraphE2e.cpp`, `JackminiPortsE2e.cpp`, `JackminiUnit.cpp`, `JackminiWait.hpp`, `MessageBufferUnit.cpp`, `MidiWriter.cpp`, `ShmLayoutUnit.cpp`
- `tools/`: `Connect.cpp`, `Lsp.cpp`, `MidiDump.cpp`, `MonitorClient.cpp`, `Samplerate.cpp`, `ToolCommon.hpp`, `Wait.cpp`

## GPL-2.0

The server, the drivers, and the ALSA driver. See `COPYING`.

- `common/`: `Memops.cpp`, `Memops.h`
- `platform/`: `ServerLock.cpp`, `ServerLock.h`
- `platform/alsa/`: `AlsaCardDiff.h`, `AlsaCardFailureSink.h`, `AlsaControl.cpp`, `AlsaControl.h`, `AlsaControlMonitor.cpp`, `AlsaControlMonitor.h`, `AlsaDriftController.cpp`, `AlsaDriftController.h`, `AlsaDriver.cpp`, `AlsaDriver.h`, `AlsaEnumerate.cpp`, `AlsaEnumerate.h`, `AlsaHotplug.cpp`, `AlsaHotplug.h`, `AlsaRateConverter.cpp`, `AlsaRateConverter.h`, `AlsaReconfigWorker.cpp`, `AlsaReconfigWorker.h`, `AlsaRingBuffer.cpp`, `AlsaRingBuffer.h`, `AlsaSecondaryCard.cpp`, `AlsaSecondaryCard.h`, `AlsaSecondaryConfig.cpp`, `AlsaSecondaryConfig.h`, `AlsaSlotTable.h`, `Bitset.h`, `Hardware.h`, `PcmAlsa.cpp`, `PcmAlsa.hpp`, `PcmCycle.hpp`, `PcmSession.cpp`, `PcmSession.hpp`
- `platform/alsa/resample/`: `Resampler.cpp`, `Resampler.h`, `pipewire_resample_bridge.c`, `pipewire_resample_bridge.h`
- `server/`: `AudioDriver.cpp`, `AudioDriver.h`, `Driver.cpp`, `Driver.h`, `DriverInfo.h`, `DriverLoader.cpp`, `DummyDriver.cpp`, `DummyDriver.h`, `Engine.cpp`, `Engine.h`, `ExternalClient.cpp`, `ExternalClient.h`, `FreewheelDriver.cpp`, `FreewheelDriver.h`, `Jackdmp.cpp`, `LockedEngine.h`, `ServerCore.cpp`, `ServerCore.h`, `ServerGlobals.cpp`, `ServerGlobals.h`, `ThreadedDriver.cpp`, `ThreadedDriver.h`, `TimedDriver.cpp`, `TimedDriver.h`
- `tests/`: `AlsaFakeBackend.hpp`, `AlsaSecondaryUnit.cpp`, `CounterTsanUnit.cpp`, `Cpu.cpp`, `DeactivateGraphUnit.cpp`, `ExternalMetro.cpp`, `Iodelay.cpp`, `MemopsUnit.cpp`, `MessageFrameUnit.cpp`, `NotifyOverflowUnit.cpp`, `PcmCycleUnit.cpp`, `ProtocolConformanceUnit.cpp`, `RingbufferUnit.cpp`

## MIT

The vendored PipeWire resampler, under the MIT licence (`platform/alsa/resample/README.md`). Each file has an
SPDX header: `SPDX-License-Identifier: MIT`.

- `platform/alsa/resample/`: `resample-native-avx2.c`, `resample-native-c.c`, `resample-native-impl.h`, `resample-native-neon.c`, `resample-native-sse.c`, `resample-native.c`, `resample.h`

`platform/alsa/resample/dbesi0.c` is Takuya Ooura's Bessel function, under the notice in the file.
`platform/alsa/resample/spa-shim/` holds minimal subsets of PipeWire's SPA headers. They are not licence-tagged
themselves; they are MIT by provenance (see the resampler README).

## No licence header

These files have no licence notice of their own. They are GPL-2.0 under `COPYING`, except the
PipeWire-derived shim headers, which are MIT by provenance.

- `./`: `CMakeLists.txt`, `config.h.in`, `configure.sh`
- `platform/alsa/resample/`: `dbesi0.c`
- `platform/alsa/resample/spa-shim/spa/param/audio/`: `format.h`
- `platform/alsa/resample/spa-shim/spa/support/`: `cpu.h`, `log.h`
- `platform/alsa/resample/spa-shim/spa/utils/`: `defs.h`, `overflow.h`
- `scripts/`: `ci.sh`
- `tests/perf/`: `Alloccount.cpp`, `Rtbench.cpp`, `run-baseline.sh`
- `tests/`: `tools_e2e.sh`
- `tools/`: `CMakeLists.txt`

## Changing this document

When a file is added, put its licence in its header, and add it to the list above. A new file that is
derived from a file of another licence keeps that licence.

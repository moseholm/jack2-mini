# ALSA redesign (Plan B, S8)

Status: started. Steps 1 and 2 are done, step 3 is partly done (the backend, not the adapter). Plan A (the client on `ClientSession`) is done. The
baseline the redesign must meet is in `perf-baseline/README.md`.

## What is there now

- `platform/alsa/alsa_driver.c` (2342 lines): the driver core in C. It configures the streams, runs the
  start, stop, wait, read, and write of each cycle, does the xrun recovery and the clock sync, and it
  deletes the driver. It keeps the I/O function pointers (`driver->read`, `driver->write`, ...) that the
  JACK driver base calls.
- `platform/alsa/alsa_rawmidi.c` (1261): the MIDI backend, in C. The ALSA sequencer backend (`alsa_seqmidi.c`)
  is dropped: the in-band MIDI between clients does not need the sequencer, and raw hardware devices use the
  RawMIDI backend.
- `platform/alsa/alsa_midi_impl.h`, `alsa_midi.h`, `midi_pack.h`, `midi_unpack.h`: the C glue for them.
- The C++ parts are already in place: `JackAlsaDriver` (the JACK driver), `JackAlsaSecondaryCard` and
  `JackAlsaSecondaryConfig` (more cards), `JackAlsaRingBuffer`, `JackAlsaRateConverter`,
  `JackAlsaDriftController`, `JackAlsaControl*`, `JackAlsaHotplug`, `JackAlsaReconfigWorker`,
  `JackAlsaEnumerate`. Those are the adapter around the C core.

## The target

- `jackmini::alsa` in `platform/alsa/`: the RT core is a template over a backend, so the cycle has no
  virtual calls. The backend concept is the set of libasound calls the core needs (open, hw and sw
  params, start, the avail and the delay, read and write of the interleaved or the non-interleaved
  buffers, the xrun recovery, the close).
- `libasound` backend: the only one that calls `snd_pcm_*`.
- Fake backend: for the unit tests. It replays the sequence of avail, the delays, and the xruns, and it
  records the calls, so the cycle logic can be tested without a card.
- `jack_alsa.so` (`JackAlsaDriver`): a thin adapter. It owns the JACK driver base and the threads, and it
  calls the core.
- MIDI: `alsa_rawmidi` in C++ on the same backend pattern (the raw hardware devices; there is no sequencer
  backend).
- The public C names that stay: `jack_midi_*` and `jack_ringbuffer_*` (decision 4).

## Requirements

- The realtime path must perform at least as well as the baseline: the same cycle rate with the card's clock,
  no xruns in the same conditions, no allocation in steady state, and the same or fewer syscalls per cycle.
- The core must not call through a virtual function in the cycle.
- The unit tests with the fake backend run in ctest. The hardware runs are the user's.

## Found in this session, to look at first

- **Thread churn when the device cannot run.** A server whose ALSA device kept returning no frames looped in
  `JackAlsaDriver::Read()`: each xrun recovery called `Stop()` and `Start()`, and those stopped and re-created the
  control monitor, the hot-plug listener, and the reconfig worker. The RT thread never completed a cycle, so
  `WaitForSwitches` refused every client, and the server churned about 4,400 threads per second (44,182 `clone3`
  in 10 s under `strace`). An idle but healthy server under `strace` showed 7,604 `clone3` in 10 s, all from the
  same restart path.
  Fixed in this step:
  - The control threads now live for the whole driver: an xrun recovery restarts the streams only.
  - `Read()` bounds the consecutive recoveries (5) and calls `PrimaryFailed` instead of retrying forever. The
    server stops when the primary device cannot deliver frames.
  - The server opens the client's notification channel only after `WaitForSwitches` succeeds, so a refused open
    does not create a writer thread.
  After the fix an idle server under `strace` showed 6 `clone3` in 10 s (only the start-up threads).
- **The card's clock.** At 64 and 128 frames the start-to-start interval is about 11 % to 12 % longer than the
  nominal period. The drift controller and the wait must keep working with that, not against the nominal rate.

## Steps (each one a commit with its tests)

1. ~~Find and fix the thread churn when the device cannot run.~~ Done: the control threads are not restarted by an
   xrun recovery, the recovery loop is bounded, and the notification channel opens after the driver runs.
2. Done (`96fdde5e`). The backend concept, the fake backend, and the cycle core `platform/alsa/pcm_cycle.hpp`, with
   `tests/pcm_cycle_unit.cpp` (807 checks in ctest): the avail, the xrun recovery, the poll retries, the sync, the
   read and write through the mapped areas, and the silencing of untouched channels.
3. Partly done. The libasound backend is `platform/alsa/pcm_alsa.{hpp,cpp}` (`jackmini::alsa::Device`, the
   `PcmBackend` for the core, in `jack_alsa`). It configures the streams with the old negotiation, links them, and
   reports `capture_and_playback_not_synced()`. Checked on the UMC404HD (U192k) at 48 kHz: 64, 128 and 256 frames,
   0 xruns, cycle time equal to the period (`c03e30a2`).
   Not done: the adapter. `JackAlsaDriver` and `JackAlsaSecondaryCard` still call the `alsa_driver_*` C API (about
   40 call sites and fields: `alsa_driver_new`, `reset_parameters`, `start`, `stop`, `wait`, `read`, `write`, the
   channel helpers, `set_callbacks`, the MIDI attach and start, and the clock-sync listeners). The adapter switch
   and the deletion of `alsa_driver.c` belong in one commit (step 5). The measurement that counts for step 6 is the
   server's, with the adapter; the harness's syscalls (2.2 per cycle) have no engine or client IPC and are not
   comparable with the baseline's 5.4 to 6.0.
   Found while porting: the core passed the whole descriptor array to the playback `poll_revents`, and the old
   `snd_pcm_link` step was missing from the backend. Both are fixed; the unit test checks the slice.
4. The MIDI core (raw) on the same backend pattern, with tests on a fake backend.
5. Delete `alsa_driver.c`, `alsa_rawmidi.c`, and the C glue, in the same commit as each port.
6. Final measurement against the baseline, committed as data.

## Decisions for the user

- The redesign is large (about 4,600 lines of C, plus the adapter). It is done in several commits, as above. Each
  commit must keep the driver working with the card.
- Whether the sequencer MIDI backend stays. Decided: it goes. The in-band MIDI between clients does not need
  the ALSA sequencer, and raw hardware devices use the RawMIDI backend.

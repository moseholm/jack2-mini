# ALSA redesign (Plan B, S8)

Status: planned, not started in code. Plan A (the client on `ClientSession`) is done. The baseline the
redesign must meet is in `perf-baseline/README.md`.

## What is there now

- `platform/alsa/alsa_driver.c` (2342 lines): the driver core in C. It configures the streams, runs the
  start, stop, wait, read, and write of each cycle, does the xrun recovery and the clock sync, and it
  deletes the driver. It keeps the I/O function pointers (`driver->read`, `driver->write`, ...) that the
  JACK driver base calls.
- `platform/alsa/alsa_rawmidi.c` (1261) and `alsa_seqmidi.c` (970): the MIDI backends, in C.
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
- MIDI: `alsa_rawmidi` and `alsa_seqmidi` in C++ on the same backend pattern (the sequencer and the raw
  backends are separate backends of one MIDI core).
- The public C names that stay: `jack_midi_*` and `jack_ringbuffer_*` (decision 4).

## Requirements

- The realtime path must perform at least as well as the baseline: the same cycle rate with the card's clock,
  no xruns in the same conditions, no allocation in steady state, and the same or fewer syscalls per cycle.
- The core must not call through a virtual function in the cycle.
- The unit tests with the fake backend run in ctest. The hardware runs are the user's.

## Found in this session, to look at first

- **Thread churn when the device is busy.** With the USB card held by PipeWire, the server created a thread
  about 43,000 times in 10 s (`clone3` and `madvise` in the strace), and the log said "not available now
  (Device or resource busy)" and then "driver is not running". The loop has to be found and fixed before the
  redesign is measured. It is not in the baseline, which is a free card.
- **The card's clock.** At 64 and 128 frames the start-to-start interval is about 11 % to 12 % longer than the
  nominal period. The drift controller and the wait must keep working with that, not against the nominal rate.

## Steps (each one a commit with its tests)

1. Find and fix the thread churn when the device is busy (a test with the fake backend that reports busy).
2. The backend concept, the fake backend, and the cycle core for the playback and the capture, with unit tests
   on the fake backend: the avail, the xrun recovery, the sync, and the cycle counts.
3. The libasound backend, and the adapter: `JackAlsaDriver` runs on the core. Check with the baseline script
   on this machine, with PipeWire stopped by the user (or with the card free).
4. The MIDI cores (raw and sequencer) on the same backend pattern, with tests on a fake sequencer.
5. Delete `alsa_driver.c`, `alsa_rawmidi.c`, `alsa_seqmidi.c`, and the C glue, in the same commit as each port.
6. Final measurement against the baseline, committed as data.

## Decisions for the user

- The redesign is large (about 4,600 lines of C, plus the adapter). It is done in several commits, as above. Each
  commit must keep the driver working with the card.
- Whether the sequencer MIDI backend stays. It is used by the drivers that some users run; the plan keeps it.

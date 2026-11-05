# Secondary ALSA cards

JACK can drive its graph from one ALSA card (the **primary**) and bring in
audio from, or send audio to, additional **secondary** cards. Each secondary
card is capture-only, playback-only or duplex, and is resampled to the primary
rate. The primary card's buffer size and rate stay fixed while secondary cards
are in use.

This document covers configuration, the ports and controls a secondary card
exposes, how rate changes behave, the client notification API, the current
limits, and how the code is tested.

## Quick start

```sh
# Primary card hw:0 (duplex); secondary USB card hw:1, capture only
jackd -d alsa -d hw:0 --secondary "name=usb1;device=hw:1;mode=capture"

# Several secondary cards, one per line in a file
jackd -d alsa -d hw:0 --secondary-file cards.conf
```

`--secondary` may be repeated. `--secondary-file` reads the same key/value
syntax, one card per line. Blank lines and lines starting with `#` are ignored.

```text
# cards.conf
name=usb1;device=hw:1;mode=capture
name=hdmi;device=hw:2,3;mode=playback;clock=sync
```

Malformed values stop the server before any device is opened, and the message
names the offending line.

## Configuration keys

Each card is a `;`-separated list of `key=value` pairs. A bare device name
(`hw:1,0`) is shorthand for `device=hw:1,0`.

| Key | Default | Meaning |
|-----|---------|---------|
| `name` | derived from the device, e.g. `hw1` | Name used in port and control names. Sanitised to letters, digits and `_`. |
| `device` | none | ALSA device for both directions. |
| `capture_device`, `playback_device` | `device` | Separate devices. Both must belong to the same ALSA card. |
| `mode` | `duplex` | `duplex`, `capture` (or `in`) or `playback` (or `out`). |
| `channels` | 2 | Channel count for both directions. |
| `capture_channels`, `playback_channels` | `channels` | Separate channel counts. |
| `rate` | the primary rate | Stream rate. The device may give a different rate; see [Rate changes](#rate-changes). |
| `nperiods` | the primary's `-n` | Periods in the ALSA buffer. At least 2. |
| `clock` | `async` | `async` runs a drift controller; `sync` keeps a fixed ratio. See [Clocks](#clocks). |
| `resampler` | `pipewire` | Resampler by name: `pipewire` (built in) or a plugin found in `JACK_RESAMPLER_PATH`. See [Resampler plugins](#resampler-plugins). |
| `format` | `auto` | Sample format: `auto` (the ALSA core prefers 32-bit float), `float`, `s32` or `s16`. Set `s32` or `s16` when a device mishandles float. |
| `optional` | `false` | If the card cannot be opened, skip it instead of failing the server. |
| `follow_hw_rate` | `false` | Allow a capture rate change to reopen the card. See [Rate changes](#rate-changes). |
| `capture_rate_ctl`, `playback_rate_ctl` | auto | Name of a native ALSA control to use as the rate control. See [Rate controls](#rate-controls). |
| `rate_map` | none | Reserved for enumerated native controls. Not yet used. |

## Ports

The secondary ports are added to the single `system` client:

- `system:capture_<name>_<n>` for capture channels, starting at 1;
- `system:playback_<name>_<n>` for playback channels, starting at 1.

For example, `name=usb1` with two capture channels gives `system:capture_usb1_1`
and `system:capture_usb1_2`.

## Rate controls

Each secondary card has one rate control per enabled direction, on the card's
ALSA control device (for example `hw:1`):

- `JACK Secondary Capture Rate`
- `JACK Secondary Playback Rate`

They are integer controls in Hz. Read them with `amixer -c 1 cget` to see the
rate the card runs at. Write them to ask for a rate change. Applications can
read and write them directly, or use the notification API below.

The driver chooses the control in this order:

1. The native control named by `capture_rate_ctl` or `playback_rate_ctl`, if
   given. A playback control must be writable.
2. A probed native control with a common name: `Capture Rate` (capture) or
   `Playback Rate` (playback), then `Sample Rate`, `Rate`, `Clock Rate`,
   `Sample Clock Rate` and `IEC958 Sample Rate`. A playback control must be
   writable.
3. Otherwise, a JACK user element with the names above. It is created when the
   card opens and removed when the server closes.

The chosen control is logged when the card opens.

## Clocks

`clock` decides how the secondary card's clock is handled relative to the
primary's.

- **`async`** (the default) keeps a ring of audio between the card and the
  graph and runs a drift controller. The controller adjusts the resampler ratio
  by up to ±1000 ppm, slew-limited, to hold the ring at its target fill. A card
  on its own crystal can run indefinitely.
- **`sync`** uses a fixed ratio. Use it only when the card's clock is locked to
  the primary's (for example word clock). With a free-running card the ring
  drifts until it under- or overruns.

With `async` the card always has a resampler, even at matching rates, so the
controller has something to steer.

## Rate changes

A rate change is requested by writing the rate control (or by a native
control's own change). The server then either reopens the card at the new
rate, or rejects the request and restores the control to the card's current
rate.

- **Playback** changes reopen the card's stream at the new rate. Accepted if
  the device accepts it.
- **Capture** changes are rejected unless `follow_hw_rate=1`. With the default,
  the change is logged and the card keeps its rate.
- Rates outside 8 kHz to 768 kHz are rejected.
- A change that the device refuses leaves the card at its previous rate.

A reopen stops the card, closes its stream, opens it again at the new rate,
rebuilds its buffers and converters, and restarts it. The card's audio is
silent during the reopen. The primary card and the JACK graph are not
restarted.

### The period limit

The card's ALSA period is set from the primary's buffer size:

```text
secondary period = primary buffer size * new rate / primary rate
```

If the device will not accept that period, the rate change is refused. For
example, with a 1024-frame primary buffer, a 44.1 kHz request needs a 941-frame
period. Some devices accept 44.1 kHz only with other periods and so refuse the
change. This is deliberate for now; see [Limits](#limits).

### Capture and the hardware rate

A device may run at a rate other than the one requested. When it does, the
card reopens at the rate the device gives, and its period is computed from
that rate. The log shows both rates:

```text
secondary card 'usb1': device runs at 48000 Hz, not the requested 44100 Hz; using 48000 Hz
```

## Notifications

Applications register for rate changes with
`jack_set_secondary_sample_rate_callback()`:

```c
#include <jack/jack.h>
#include <jack/types.h>

static int on_secondary_rate(jack_nframes_t rate, const char *card,
                             int direction, void *arg)
{
    int capture  = direction & JackSecondaryRateCapture;
    int playback = direction & JackSecondaryRatePlayback;
    /* react to the new rate */
    return 0;
}

jack_set_secondary_sample_rate_callback(client, on_secondary_rate, NULL);
```

- The callback runs on a non-real-time thread, after the new rate is active.
- `direction` is a bitmask. A card whose capture and playback share one stream
  reports both bits in one call.
- Failed or refused changes are not reported.
- Call it before `jack_activate()`.

## Limits

- **One JACK server per ALSA card.** Each card a server uses is locked for the
  server's lifetime (`flock` on `/dev/shm/jack_alsa_<card>.lock`). A second
  server is refused, and so is a secondary card that is the primary's card or
  another secondary's card. The primary's capture and playback may share one
  card. The lock is released when the process exits, including after a crash.
- **The period is fixed by the primary buffer size.** See
  [The period limit](#the-period-limit). Rate changes that need a different
  period fail.
- **Capture rate changes** need `follow_hw_rate=1` to be applied. That path is
  implemented but has not been tested on real hardware (see [Testing](#testing)).
- **Latency is an estimate.** Secondary ports report the cycle, the card's ALSA
  buffer, the ring prefill and the resampler's filter delay, in primary frames. It
  is recomputed after a rate change. The ring's fill is held at about three periods,
  so the value is a design estimate; it has not been measured against hardware.
- **No runtime add or remove** of secondary cards. Cards are fixed at startup.
- **Native control types.** Only integer controls are used. Enumerated controls
  are not yet supported, so a card whose rate control is an enum falls back to a
  JACK element.
- **No linear or other fallback resampler.** Only `pipewire` is built in. If a
  requested resampler is unavailable, the server does not start.

## Resampler plugins

A secondary card can use a resampler supplied as a shared object. The built-in
PipeWire resampler is always used when `resampler=pipewire` (the default), and a
plugin cannot replace it.

To use a plugin:

1. Put `<name>.so` in a directory and point `JACK_RESAMPLER_PATH` at it. Several
   directories may be given, separated by `:`.
2. Select it with `resampler=<name>` on the card.

The server loads a plugin when the card is configured. A plugin that is missing,
has the wrong ABI version or struct size, or does not report the name it was
loaded by, is refused with a message, and the server does not start.

The ABI is internal to this source tree and is versioned. The header is
`linux/alsa/resample/JackResamplerPlugin.h`; it is not installed. A plugin exports
`jack_resampler_plugin_v1()`, which returns a `jack_resampler_plugin_t` with the
create, destroy, reset, set_rate, in_len, out_len, process and delay functions.
The unit tests include a pass-through plugin (`tests/alsa_test_resampler_plugin.c`)
as a working example.

## Troubleshooting

| Message | Meaning and what to do |
|---------|------------------------|
| `invalid --secondary "...": ...` | The value does not parse. The message names the bad key or value. |
| `ALSA card hw:N is already in use ...` | Another JACK server, or another device of this server, uses that card. Stop the other server or choose a different card. |
| `secondary card 'x': device runs at R Hz, not the requested Q Hz` | The hardware chose a different rate. The card runs at R Hz; no action needed. |
| `secondary card 'x': cannot run at R Hz; restoring Q Hz` | The device refused the requested rate, usually because of the period limit. The card keeps its previous rate. |
| `secondary card 'x': capture rate changed to R Hz; keeping Q Hz (follow_hw_rate=0)` | A capture change was refused by design. Set `follow_hw_rate=1` to allow it. |
| `secondary card 'x': cannot create a ... rate control; keeping R Hz` | No JACK element could be created (for example, the name is taken). The card runs at its startup rate; drift control still works. |
| `secondary card 'x': real-time thread did not reach a safe point` | The RT thread was not running, so the reopen was abandoned. Check that the server is started. |
| `secondary card 'x': resampler 'n' is not available` | The named resampler is neither built in nor found in `JACK_RESAMPLER_PATH`. |
| `secondary card 'x': offline, no usable rate` | Neither the requested nor the previous rate could be opened. The card is silent until a later change succeeds. |

## Testing

### Unit tests

The unit tests cover the parts that need no hardware: the configuration parser,
the ring buffer, the converter with a test-only resampler, the PipeWire
resampler through its factory, the converter's frame counts at 44.1 and 48 kHz,
and the drift controller's convergence and limits.

```sh
cmake -S . -B build -DJACK_BUILD_TESTS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

They also run as `build/bin/jack_alsa_unit`.

### Live checks

These need real hardware or a loopback card, so they are not part of the
automated tests.

Tested on this development machine:

- creating, reading, writing, and removing a rate control, with a write from a
  second client producing a value event;
- a server with a secondary capture card, and its element removed on shutdown;
- refusal of a second server on the same card;
- a playback card on HDMI (`hw:1,3`) reopened at 96 kHz and rolled back from
  44.1 kHz, with the callback delivered only for the accepted change.

Not yet tested on hardware:

- capture rate changes with `follow_hw_rate=1` (the only capture card here runs
  at 48 kHz);
- drift control over a long run with a real free-running card;
- `clock=sync` with a locked card.

To repeat the live checks with a loopback card, load `snd-aloop` and use its
card as the secondary device:

```sh
sudo modprobe snd-aloop
jackd -d alsa -d hw:0 --secondary "name=loop;device=hw:Loopback,0,0;mode=duplex"
# In another terminal, change the rate:
amixer -c Loopback cset name='JACK Secondary Playback Rate' 44100
```

Loopback devices may refuse some rates or periods; the log says why.

What was found on `snd-aloop` (kernel 7.0):

- Playback through a secondary card reaches the loopback's capture side:
  a 1 kHz tone played through the card was read back with a peak of 0.76 by
  `arecord`.
- Reading the loopback's capture side through the ALSA core's mmap path returns
  silence, whether or not the tone is present. Plain read/write capture of the
  same device returns the tone. This also happens for the primary card, so it is
  a property of the loopback's mmap capture, not of the secondary-card code. A
  duplex or capture-only loopback therefore cannot validate the capture path.
- 32-bit float does not carry data through the loopback at all, which is why the
  `format` key exists. Use `format=s32` on the loopback.
- A playback-only secondary card on the loopback (`format=s32`) reopened at
  96 kHz after a write to `JACK Secondary Playback Rate`, and the tone was read
  back at 96 kHz with the same peak. This is the end-to-end check of a playback
  rate change with audio.

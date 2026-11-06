jack2-mini
##########

.. image:: https://github.com/moseholm/jack2-mini/actions/workflows/build.yml/badge.svg
   :target: https://github.com/moseholm/jack2-mini/actions/workflows/build.yml

jack2-mini is a Linux-only JACK server built on the JACK2 engine. It uses ALSA
as its only hardware backend and gives the whole graph one clock: a single
**primary** ALSA card. Every other sound device can join as a **secondary**
card, resampled to the primary rate and added or removed while the server runs.

It is meant for a machine where one JACK server should own all of the sound
hardware: no PulseAudio, no second jackd, and no application taking a card
behind the server's back. Existing JACK clients work unchanged.

.. code-block:: text

    JACK clients
         |
    +----+--------------------------------------------------+
    |                  JACK graph (jack2)                  |
    +----+---------------------------+---------------------+
         |                           |
    primary card                secondary cards
    hw:CARD=PCH                 hw:CARD=USB, hw:CARD=NVidia, ...
    clock and buffer size       resampled to the primary rate
                                added/removed by udev

What it does
============

- **One primary card, chosen by ALSA id.** Start the server with
  ``-d hw:CARD=PCH``. Numeric names such as ``hw:0`` are refused, so the
  choice survives card renumbering. The primary sets the graph's rate and
  buffer size.
- **Every other PCM device is a secondary card.** They are discovered at
  startup, one ALSA PCM device per secondary card. A device with both
  directions is used duplex; otherwise it uses the direction it supports.
  Automatic cards use at most 12 channels per direction.
- **Everything is resampled to the primary rate.** A secondary runs at its own
  rate and the built-in PipeWire resampler converts between it and the graph.
  There is no linear fallback.
- **Hot-plug is part of the design.** The server listens to udev. A device that
  appears is added, and a device that goes away has its ports removed, without
  restarting the primary or the graph. Failed opens are retried at the next
  device event, not by polling.
- **Per-device ports and controls.** A secondary device gets ports named
  ``system:capture_<card>_d<dev>_<ch>`` and
  ``system:playback_<card>_d<dev>_<ch>``, plus
  ``JACK Secondary Capture Rate d<dev>`` and
  ``JACK Secondary Playback Rate d<dev>`` ALSA controls.
- **Rate changes through ALSA.** Writing a secondary's rate control reopens
  that card at the new rate while the primary keeps running. Playback changes
  are applied when the device accepts them; capture changes need
  ``follow_hw_rate=1``. A refused change restores the control to the rate the
  card is actually using.
- **One server per card.** Each card is locked for the server's lifetime. A
  second server is refused for that card, while several devices of one card
  can be used by the same server.
- **Eight secondary slots.** Automatic and explicitly configured cards share
  the same eight slots.
- **Resampler plugins.** Besides the built-in PipeWire resampler, a secondary
  can load a versioned resampler plugin selected with ``resampler=``.

Requirements and ownership
==========================

- Linux with ALSA. This tree does not build for other platforms or backends.
- jack2-mini must be the only thing that opens the ALSA cards. Stop
  PulseAudio and any other jackd instance. PipeWire may keep running, but its
  ALSA devices must be disabled so it does not claim the cards.
- The primary card must be present when the server starts. If it disappears,
  the server stops with an error naming the device; no secondary takes over.
- The graph rate and buffer size are fixed by the primary. A secondary's
  period is derived from the primary buffer size and its own rate.

Running
=======

Build first (see `Building`_), then::

    jackd -d alsa -d hw:CARD=PCH -r 48000 -p 1024

Leave a card out of automatic discovery with ``--exclude-card`` (repeatable,
case-insensitive)::

    jackd -d alsa -d hw:CARD=PCH --exclude-card=Loopback

Override an automatic card, or configure one explicitly, with ``--secondary``
(repeatable) or ``--secondary-file``::

    jackd -d alsa -d hw:CARD=PCH \
        --secondary "name=usb;device=hw:CARD=USB,DEV=0;mode=duplex"

Explicit cards are fatal if they cannot be opened, unless ``optional=1`` is
set. Automatic cards never stop the server.

Secondary cards
===============

Configuration keys, ports, clocks, rate controls, the client notification API,
hot-plug behaviour and the current limits are described in
`docs/secondary-alsa-cards.md <docs/secondary-alsa-cards.md>`_.

Building
========

The build requires CMake, a C/C++ compiler, the ALSA development headers and
libraries, and libudev for sound-device hot-plug. On Debian-based systems the
dependencies are::

    sudo apt-get install build-essential cmake libasound2-dev libudev-dev

Configure and build with::

    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build --parallel

Install with::

    cmake --install build

The install prefix and library directory can be set with the standard CMake
options ``-DCMAKE_INSTALL_PREFIX=/usr/local`` and
``-DCMAKE_INSTALL_LIBDIR=lib``.

The maximum number of clients and ports per application can be configured with
``-DJACK_MAX_CLIENTS=256`` and ``-DJACK_PORTS_PER_APPLICATION=2048``.

Tests
=====

The tests are off by default. Configure with ``-DJACK_BUILD_TESTS=ON`` to build
the secondary-card unit tests and the JACK test programs::

    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DJACK_BUILD_TESTS=ON
    cmake --build build --parallel
    ctest --test-dir build

Heritage
========

jack2-mini is a focused fork of `JACK2 <https://github.com/jackaudio/jack2>`_.
The JACK engine, graph and client API come from JACK2 and work as before; what
this tree changes is how Linux hardware is discovered and owned. It builds the
ALSA and ALSA raw MIDI drivers together with the ``dummy``, ``loopback`` and
``proxy`` drivers. See ``COPYING`` for the license.

For general JACK information, see the `JACK homepage <https://jackaudio.org/>`_
and `wiki <https://github.com/jackaudio/jackaudio.github.com/wiki>`_. There are
also the #jack and #lad chat channels on
`Libera Chat IRC <https://web.libera.chat/#jack>`_.

Acknowledgements
================

This project came together in `opencode <https://opencode.ai/>`_, with a great
deal of help from **Big Pickle** and **Claude Haiku**. Without those three this
would not have happened. Thank you for the long hours on ALSA, udev, resampling
and the JACK graph.

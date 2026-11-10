// Copyright (C) 2026 JACK contributors
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

#include "JackAlsaSecondaryCard.h"
#include "JackAlsaSecondaryConfig.h"
#include "JackError.h"
#include "JackGraphManager.h"
#include "pcm_session.hpp"

#include <errno.h>
#include <math.h>
#include <string.h>
#include <sys/mman.h>
#include <algorithm>
#include <chrono>
#include <thread>

namespace jackmini {

static float** AllocChannelBuffers(int channels, unsigned int frames)
{
    float** buffers = new float*[channels];
    for (int i = 0; i < channels; i++) {
        buffers[i] = new float[frames];
        memset(buffers[i], 0, sizeof(float) * frames);
    }
    return buffers;
}

static void FreeChannelBuffers(float** buffers, int channels)
{
    if (buffers == NULL) {
        return;
    }
    for (int i = 0; i < channels; i++) {
        delete[] buffers[i];
    }
    delete[] buffers;
}

// Best-effort residency for buffers the realtime threads touch.  A failure is
// not fatal; it only means the kernel may still fault pages in later.
static bool LockChannelBuffers(float** buffers, int channels, unsigned int frames)
{
    if (buffers == NULL || channels <= 0) {
        return true;
    }

    bool locked = mlock(buffers, sizeof(float*) * channels) == 0;
    for (int i = 0; i < channels; i++) {
        if (buffers[i] != NULL && frames > 0) {
            locked = (mlock(buffers[i], sizeof(float) * frames) == 0) && locked;
        }
    }
    return locked;
}

JackAlsaSecondaryCard::JackAlsaSecondaryCard(const JackAlsaSecondaryConfig& config, JackResamplerFactory* factory)
    : fConfig(config)
    , fThread(this)
    , fRunning(false)
    , fOpened(false)
    , fRealTime(false)
    , fPriority(0)
    , fCaptureEnabled(false)
    , fPlaybackEnabled(false)
    , fPeriod(0)
    , fNperiods(0)
    , fRate(0)
    , fCaptureChannels(0)
    , fPlaybackChannels(0)
    , fCaptureRing(NULL)
    , fPlaybackRing(NULL)
    , fFactory(factory)
    , fCaptureConverter(NULL)
    , fPlaybackConverter(NULL)
    , fCaptureConvIn(NULL)
    , fPlaybackConvOut(NULL)
    , fConvCap(0)
    , fXrunGeneration(0)
    , fSeenXrunGeneration(0)
    , fCaptureDropped(0)
    , fPlaybackDropped(0)
    , fIoError(IoError::None)
    , fIoErrorStatus(0)
    , fCtl(NULL)
    , fCaptureTemp(NULL)
    , fPlaybackTemp(NULL)
    , fPortScratch(NULL)
    , fSilence(NULL)
{
    fRateValid[RateCapture] = false;
    fRateValid[RatePlayback] = false;
    fSilent.store(false);
    fRTEpoch = NULL;
    fRTRunning = NULL;
    fId = -1;
    fDevice = -1;
    fFailureSink = NULL;
    fPrimaryBufferSize = 0;
    fPrimaryRate = 0;
}

JackAlsaSecondaryCard::~JackAlsaSecondaryCard()
{
    Close();
}

void JackAlsaSecondaryCard::FreeStreamObjects()
{
    fSession.close();
    FreeBuffers();
}

void JackAlsaSecondaryCard::FreeBuffers()
{
    if (fCaptureTemp) {
        for (int i = 0; i < fCaptureChannels; i++) {
            delete[] fCaptureTemp[i];
        }
        delete[] fCaptureTemp;
        fCaptureTemp = NULL;
    }

    if (fPlaybackTemp) {
        for (int i = 0; i < fPlaybackChannels; i++) {
            delete[] fPlaybackTemp[i];
        }
        delete[] fPlaybackTemp;
        fPlaybackTemp = NULL;
    }

    delete[] fPortScratch;
    fPortScratch = NULL;

    delete[] fSilence;
    fSilence = NULL;

    delete fCaptureRing;
    fCaptureRing = NULL;

    delete fPlaybackRing;
    fPlaybackRing = NULL;

    delete fCaptureConverter;
    fCaptureConverter = NULL;

    delete fPlaybackConverter;
    fPlaybackConverter = NULL;

    FreeChannelBuffers(fCaptureConvIn, fCaptureChannels);
    fCaptureConvIn = NULL;

    FreeChannelBuffers(fPlaybackConvOut, fPlaybackChannels);
    fPlaybackConvOut = NULL;
}

static jackmini::alsa::SampleFormat ToAlsaFormat(JackAlsaSampleFormat format)
{
    switch (format) {
    case SampleFormatFloat:
        return jackmini::alsa::SampleFormat::Float;
    case SampleFormatS32:
        return jackmini::alsa::SampleFormat::S32;
    case SampleFormatS16:
        return jackmini::alsa::SampleFormat::S16;
    default:
        return jackmini::alsa::SampleFormat::Any;
    }
}

int JackAlsaSecondaryCard::OpenStream(jack_nframes_t requested_rate,
    jack_nframes_t primary_buffer_size,
    jack_nframes_t primary_rate)
{
    const std::string& playback_device = fConfig.playback_device.empty()
        ? fConfig.capture_device
        : fConfig.playback_device;
    const std::string& capture_device = fConfig.capture_device.empty()
        ? fConfig.playback_device
        : fConfig.capture_device;

    // The hardware may not run at the requested rate.  If it doesn't, reopen at
    // the rate it actually gives, so the period matches the card's real clock.
    jack_nframes_t rate = requested_rate;
    bool opened = false;
    for (int attempt = 0; attempt < 2 && !opened; attempt++) {
        double period = ((double)primary_buffer_size * (double)rate) / (double)primary_rate;
        fPeriod = (jack_nframes_t)llround(period);
        if (fPeriod < 1) {
            fPeriod = 1;
        }

        jack_info("secondary card '%s': device %s, mode %s, rate %u, period %u, %u periods",
            fConfig.name.c_str(),
            (playback_device + "|" + capture_device).c_str(),
            (fCaptureEnabled && fPlaybackEnabled) ? "duplex" : (fCaptureEnabled ? "capture" : "playback"),
            rate, fPeriod, fNperiods);

        jackmini::alsa::Request request;
        request.rate = rate;
        request.frames_per_cycle = fPeriod;
        request.periods = fNperiods;
        request.capture_channels = fCaptureEnabled ? fConfig.capture_channels : 0;
        request.playback_channels = fPlaybackEnabled ? fConfig.playback_channels : 0;
        request.format = ToAlsaFormat(fConfig.sample_format);
        if (fSession.open(fCaptureEnabled ? capture_device.c_str() : NULL,
                fPlaybackEnabled ? playback_device.c_str() : NULL,
                request, None)
            < 0) {
            jack_error("secondary card '%s': cannot open ALSA device %s",
                fConfig.name.c_str(), (playback_device + "|" + capture_device).c_str());
            return -1;
        }

        if (fSession.rate() == rate) {
            opened = true;
            break;
        }

        if (attempt == 0) {
            jack_info("secondary card '%s': device runs at %u Hz, not the requested %u Hz; using %u Hz",
                fConfig.name.c_str(), (unsigned int)fSession.rate(), (unsigned int)rate,
                (unsigned int)fSession.rate());
            rate = fSession.rate();
        } else {
            jack_error("secondary card '%s': device rate %u Hz does not settle",
                fConfig.name.c_str(), (unsigned int)fSession.rate());
        }
        fSession.close();
    }

    if (!opened) {
        return -1;
    }

    fCaptureChannels = fCaptureEnabled ? fSession.info(jackmini::alsa::Stream::Capture).channels : 0;
    fPlaybackChannels = fPlaybackEnabled ? fSession.info(jackmini::alsa::Stream::Playback).channels : 0;
    fPeriod = fSession.frames_per_cycle();
    fRate = fSession.rate();
    return 0;
}

int JackAlsaSecondaryCard::BuildBuffers(jack_nframes_t primary_buffer_size,
    jack_nframes_t primary_rate)
{
    fCaptureTemp = new float*[fCaptureChannels];
    for (int i = 0; i < fCaptureChannels; i++) {
        fCaptureTemp[i] = new float[fPeriod];
        memset(fCaptureTemp[i], 0, sizeof(float) * fPeriod);
    }

    fPlaybackTemp = new float*[fPlaybackChannels];
    for (int i = 0; i < fPlaybackChannels; i++) {
        fPlaybackTemp[i] = new float[fPeriod];
        memset(fPlaybackTemp[i], 0, sizeof(float) * fPeriod);
    }

    int scratch_channels = std::max(fCaptureChannels, fPlaybackChannels);
    fPortScratch = new float*[std::max(scratch_channels, 1)];

    jack_nframes_t silence_size = std::max(primary_buffer_size, fPeriod);
    fSilence = new float[silence_size];
    memset(fSilence, 0, sizeof(float) * silence_size);

    fConvCap = std::max(primary_buffer_size, fPeriod) * 4 + 1024;
    // Asynchronous clocks always go through a converter, even at equal rates,
    // so the drift controller has something to steer.
    if (fRate != primary_rate || fConfig.clock == SecondaryClockAsync) {
        if (fFactory == NULL) {
            jack_error("secondary card '%s': no resampler factory", fConfig.name.c_str());
            FreeBuffers();
            fSession.close();
            return -1;
        }

        // Capture: card rate -> primary rate.  Playback: primary rate -> card rate.
        if (fCaptureEnabled) {
            JackResamplerParams params = { (unsigned int)fCaptureChannels, fRate, primary_rate, -1 };
            JackResampler* resampler = fFactory->Create(params);
            fCaptureConverter = new JackAlsaRateConverter(resampler, fCaptureChannels, fConvCap);
            if (!fCaptureConverter->IsValid()) {
                jack_error("secondary card '%s': cannot create capture resampler %u->%u",
                    fConfig.name.c_str(), fRate, primary_rate);
                FreeBuffers();
                fSession.close();
                return -1;
            }
            fCaptureConvIn = AllocChannelBuffers(fCaptureChannels, fConvCap);
        }

        if (fPlaybackEnabled) {
            JackResamplerParams params = { (unsigned int)fPlaybackChannels, primary_rate, fRate, -1 };
            JackResampler* resampler = fFactory->Create(params);
            fPlaybackConverter = new JackAlsaRateConverter(resampler, fPlaybackChannels, fConvCap);
            if (!fPlaybackConverter->IsValid()) {
                jack_error("secondary card '%s': cannot create playback resampler %u->%u",
                    fConfig.name.c_str(), primary_rate, fRate);
                FreeBuffers();
                fSession.close();
                return -1;
            }
            fPlaybackConvOut = AllocChannelBuffers(fPlaybackChannels, fConvCap);
        }
    }

    jack_nframes_t ring_capacity = fPeriod * 8;
    if (ring_capacity < fPeriod + 1) {
        ring_capacity = fPeriod + 1;
    }

    if (fCaptureEnabled) {
        fCaptureRing = new JackAlsaRingBuffer(fCaptureChannels, ring_capacity);
        fCapturePorts.resize(fCaptureChannels, 0);
    }
    if (fPlaybackEnabled) {
        fPlaybackRing = new JackAlsaRingBuffer(fPlaybackChannels, ring_capacity);
        fPlaybackPorts.resize(fPlaybackChannels, 0);
    }

    // The card thread and the primary RT thread use these buffers every cycle.
    // Keep them resident now, on the non-realtime setup path.
    bool locked = true;
    if (fCaptureRing) {
        locked = fCaptureRing->LockMemory() && locked;
    }
    if (fPlaybackRing) {
        locked = fPlaybackRing->LockMemory() && locked;
    }
    locked = LockChannelBuffers(fCaptureTemp, fCaptureChannels, fPeriod) && locked;
    locked = LockChannelBuffers(fPlaybackTemp, fPlaybackChannels, fPeriod) && locked;
    locked = LockChannelBuffers(fCaptureConvIn, fCaptureChannels, fConvCap) && locked;
    locked = LockChannelBuffers(fPlaybackConvOut, fPlaybackChannels, fConvCap) && locked;
    if (fPortScratch) {
        locked = (mlock(fPortScratch, sizeof(float*) * std::max(scratch_channels, 1)) == 0) && locked;
    }
    if (fSilence) {
        locked = (mlock(fSilence, sizeof(float) * silence_size) == 0) && locked;
    }
    if (!locked) {
        jack_log("secondary card '%s': could not lock all real-time buffers",
            fConfig.name.c_str());
    }

    jackmini::alsa::PcmCycle::Callbacks callbacks;
    callbacks.arg = this;
    callbacks.read_input = ReadInputThunk;
    callbacks.clear_output = ClearOutputThunk;
    callbacks.write_output = WriteOutputThunk;
    callbacks.set_time = SetTimeThunk;
    callbacks.restart = RestartThunk;
    fSession.set_callbacks(callbacks);
    return 0;
}

int JackAlsaSecondaryCard::Open(jack_nframes_t primary_buffer_size,
    jack_nframes_t primary_rate,
    jack_nframes_t primary_nperiods,
    bool realtime,
    int priority)
{
    if (fOpened) {
        return 0;
    }

    fRealTime = realtime;
    fPriority = priority;

    fCaptureEnabled = (fConfig.mode != SecondaryModePlayback);
    fPlaybackEnabled = (fConfig.mode != SecondaryModeCapture);

    fDevice = JackAlsaPcmDeviceNumber(fConfig.capture_device.empty() ? fConfig.playback_device : fConfig.capture_device);

    jack_nframes_t requested_rate = (fConfig.rate > 0) ? fConfig.rate : primary_rate;
    fNperiods = (fConfig.nperiods > 0) ? fConfig.nperiods : primary_nperiods;

    if (primary_buffer_size == 0 || primary_rate == 0) {
        jack_error("secondary card '%s': invalid primary timing", fConfig.name.c_str());
        return -1;
    }

    fPrimaryBufferSize = primary_buffer_size;
    fPrimaryRate = primary_rate;

    if (OpenStream(requested_rate, primary_buffer_size, primary_rate) < 0) {
        return -1;
    }
    if (BuildBuffers(primary_buffer_size, primary_rate) < 0) {
        return -1;
    }

    if (OpenControls() < 0) {
        Close();
        return -1;
    }

    fOpened = true;
    return 0;
}

jack_nframes_t JackAlsaSecondaryCard::CaptureLatency() const
{
    if (fRate == 0 || fPrimaryRate == 0) {
        return 0;
    }
    // Card frames -> primary frames.
    double ratio = (double)fPrimaryRate / (double)fRate;
    double frames = fPrimaryBufferSize;
    if (fCaptureConverter) {
        // The RT side keeps about three periods in the ring, and the converter adds its filter delay.
        frames += (3.0 * fPeriod + fCaptureConverter->Delay()) * ratio;
    }
    return (jack_nframes_t)ceil(frames);
}

jack_nframes_t JackAlsaSecondaryCard::PlaybackLatency() const
{
    if (fRate == 0 || fPrimaryRate == 0) {
        return 0;
    }
    double ratio = (double)fPrimaryRate / (double)fRate;
    // The card's ALSA buffer, the playback ring's prefill, and the converter delay.
    double card_frames = (double)(fNperiods > 0 ? fNperiods - 1 : 0) * fPeriod + 3.0 * fPeriod;
    if (fPlaybackConverter) {
        card_frames += fPlaybackConverter->Delay();
    }
    return (jack_nframes_t)ceil(fPrimaryBufferSize + card_frames * ratio);
}

int JackAlsaSecondaryCard::OpenControls()
{
    const std::string& capture_pcm = fConfig.capture_device.empty() ? fConfig.playback_device : fConfig.capture_device;
    const std::string& playback_pcm = fConfig.playback_device.empty() ? fConfig.capture_device : fConfig.playback_device;

    std::string capture_ctl = JackAlsaControlDeviceName(capture_pcm);
    std::string playback_ctl = JackAlsaControlDeviceName(playback_pcm);
    if (capture_ctl.empty() || playback_ctl.empty() || capture_ctl != playback_ctl) {
        jack_error("secondary card '%s': capture and playback devices must belong to the same ALSA card",
            fConfig.name.c_str());
        return -1;
    }
    fCtlName = capture_ctl;

    int err = snd_ctl_open(&fCtl, fCtlName.c_str(), 0);
    if (err < 0) {
        fCtl = NULL;
        jack_error("secondary card '%s': cannot open control device %s (%s)",
            fConfig.name.c_str(), fCtlName.c_str(), snd_strerror(err));
        return -1;
    }

    for (int d = 0; d < 2; d++) {
        JackAlsaRateDirection dir = (JackAlsaRateDirection)d;
        bool enabled = (dir == RateCapture) ? fCaptureEnabled : fPlaybackEnabled;
        if (!enabled) {
            continue;
        }

        const std::string& configured = (dir == RateCapture) ? fConfig.capture_rate_ctl : fConfig.playback_rate_ctl;
        JackAlsaRateElement elem;
        std::string error;
        int res = JackAlsaControlResolveRate(fCtl, dir, configured, &elem, &error);
        if (res < 0) {
            jack_error("secondary card '%s': %s", fConfig.name.c_str(), error.c_str());
            return -1;
        }

        if (res == 1) {
            if (JackAlsaControlCreateRate(fCtl, dir, fDevice, fRate, &elem) < 0) {
                // Without a control the card keeps its startup rate; ASRC still
                // corrects drift.
                jack_error("secondary card '%s': cannot create a %s rate control; keeping %u Hz",
                    fConfig.name.c_str(), (dir == RateCapture) ? "capture" : "playback", fRate);
                continue;
            }
            jack_info("secondary card '%s': %s rate control is JACK element '%s' (numid %u)",
                fConfig.name.c_str(), (dir == RateCapture) ? "capture" : "playback",
                elem.name.c_str(), elem.numid);
        } else {
            jack_info("secondary card '%s': %s rate control is native element '%s' (numid %u, %s)",
                fConfig.name.c_str(), (dir == RateCapture) ? "capture" : "playback",
                elem.name.c_str(), elem.numid, elem.writable ? "writable" : "read-only");
        }

        fRateElem[d] = elem;
        fRateValid[d] = true;
    }

    return 0;
}

void JackAlsaSecondaryCard::CloseControls()
{
    if (!fCtl) {
        return;
    }
    for (int d = 0; d < 2; d++) {
        if (fRateValid[d] && fRateElem[d].user) {
            int err = JackAlsaControlRemoveRate(fCtl, fRateElem[d]);
            // ENODEV: the card has been unplugged, so its control is already gone.
            if (err < 0 && err != -ENODEV) {
                jack_error("secondary card '%s': cannot remove control '%s' (%s)",
                    fConfig.name.c_str(), fRateElem[d].name.c_str(), strerror(-err));
            }
        }
        fRateValid[d] = false;
    }
    snd_ctl_close(fCtl);
    fCtl = NULL;
}

int JackAlsaSecondaryCard::ReadRate(JackAlsaRateDirection dir, long* value)
{
    if (!fCtl || !fRateValid[dir]) {
        return -1;
    }
    return JackAlsaControlReadRate(fCtl, fRateElem[dir], value);
}

int JackAlsaSecondaryCard::WriteRate(JackAlsaRateDirection dir, long value)
{
    if (!fCtl || !fRateValid[dir]) {
        return -1;
    }
    return JackAlsaControlWriteRate(fCtl, fRateElem[dir], value);
}

int JackAlsaSecondaryCard::Close()
{
    if (!fOpened && !fSession.is_open()) {
        return 0;
    }

    Stop();
    CloseControls();
    FreeStreamObjects();
    fCapturePorts.clear();
    fPlaybackPorts.clear();
    fOpened = false;
    fCaptureChannels = 0;
    fPlaybackChannels = 0;
    fRate = 0;
    fPeriod = 0;
    return 0;
}

int JackAlsaSecondaryCard::Start()
{
    if (!fOpened) {
        return -1;
    }
    if (fRunning.load()) {
        return 0;
    }

    if (fCaptureRing) {
        fCaptureRing->Reset();
    }
    ResetResampling();
    fSeenXrunGeneration = fXrunGeneration.load();

    if (fCaptureRing && fCaptureConverter) {
        // Give the converter a cushion of silence, matching the playback ring.
        for (int i = 0; i < fCaptureChannels; i++) {
            fPortScratch[i] = fSilence;
        }
        for (int period = 0; period < 3; period++) {
            fCaptureRing->Write((const float* const*)fPortScratch, fPeriod);
        }
    }
    if (fPlaybackRing) {
        // Pre-fill a few periods of silence so the first writes do not
        // immediately underrun while the card thread spins up.
        fPlaybackRing->Reset();
        for (int i = 0; i < fPlaybackChannels; i++) {
            fPortScratch[i] = fSilence;
        }
        for (int period = 0; period < 3; period++) {
            fPlaybackRing->Write((const float* const*)fPortScratch, fPeriod);
        }
    }

    if (fSession.start() < 0) {
        jack_error("secondary card '%s': cannot start ALSA stream", fConfig.name.c_str());
        return -1;
    }

    fRunning.store(true);
    if (fThread.StartSync() < 0) {
        jack_error("secondary card '%s': cannot start I/O thread", fConfig.name.c_str());
        fRunning.store(false);
        fSession.stop();
        return -1;
    }

    return 0;
}

std::string JackAlsaSecondaryCard::PortBase() const
{
    if (fDevice < 0) {
        return fConfig.name;
    }
    return fConfig.name + "_d" + std::to_string(fDevice);
}

void JackAlsaSecondaryCard::MarkFailed()
{
    fRunning.store(false);
    if (fFailureSink != NULL) {
        fFailureSink->OnCardFailed(fId);
    }
}

void JackAlsaSecondaryCard::ReportDrops()
{
    uint64_t capture = fCaptureDropped.exchange(0, std::memory_order_relaxed);
    uint64_t playback = fPlaybackDropped.exchange(0, std::memory_order_relaxed);

    if (capture) {
        jack_log("secondary card '%s': dropped %llu capture frames",
            fConfig.name.c_str(), (unsigned long long)capture);
    }
    if (playback) {
        jack_log("secondary card '%s': dropped %llu playback frames",
            fConfig.name.c_str(), (unsigned long long)playback);
    }
}

void JackAlsaSecondaryCard::ReportPendingIoError()
{
    IoError error = fIoError.exchange(IoError::None, std::memory_order_acquire);
    int status = fIoErrorStatus.load(std::memory_order_relaxed);

    switch (error) {
    case IoError::Wait:
        jack_error("secondary card '%s': ALSA wait failed (status %d)",
            fConfig.name.c_str(), status);
        break;
    case IoError::Read:
        jack_error("secondary card '%s': ALSA read failed", fConfig.name.c_str());
        break;
    case IoError::Write:
        jack_error("secondary card '%s': ALSA write failed", fConfig.name.c_str());
        break;
    case IoError::None:
        break;
    }
}

bool JackAlsaSecondaryCard::WaitForRTCycles(unsigned int cycles)
{
    // A cycle that was already running when fSilent was set may finish one
    // more time, so wait for two completed cycles to be sure the RT thread
    // has seen the flag.
    uint64_t start = fRTEpoch->load(std::memory_order_acquire);
    for (int i = 0; i < 2000; i++) {
        if (fRTEpoch->load(std::memory_order_acquire) >= start + cycles) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

bool JackAlsaSecondaryCard::ReopenAt(jack_nframes_t rate, int capture_channels, int playback_channels)
{
    if (OpenStream(rate, fPrimaryBufferSize, fPrimaryRate) < 0) {
        return false;
    }
    if (fCaptureChannels != capture_channels || fPlaybackChannels != playback_channels) {
        // The port lists are fixed, so the channel layout must not change.
        jack_error("secondary card '%s': channel count changed at %u Hz", fConfig.name.c_str(), rate);
        FreeStreamObjects();
        return false;
    }
    if (BuildBuffers(fPrimaryBufferSize, fPrimaryRate) < 0) {
        FreeStreamObjects();
        return false;
    }
    return true;
}

int JackAlsaSecondaryCard::Reconfigure(jack_nframes_t rate)
{
    if (!fOpened) {
        return -1;
    }
    if (rate == fRate) {
        return 0;
    }

    jack_nframes_t old_rate = fRate;
    int capture_channels = fCaptureChannels;
    int playback_channels = fPlaybackChannels;
    bool was_running = fRunning.load();

    // Publish silence first, then wait until the RT thread has run past this
    // point, so it never touches a buffer while it is being freed.
    fSilent.store(true, std::memory_order_release);
    // The RT side may use this card's buffers while the driver runs, even when
    // the card's own thread has stopped (for example after an I/O error).
    bool rt_live = (fRTRunning != NULL && fRTRunning->load());
    if (rt_live && fRTEpoch != NULL && !WaitForRTCycles(2)) {
        jack_error("secondary card '%s': real-time thread did not reach a safe point; keeping %u Hz",
            fConfig.name.c_str(), old_rate);
        fSilent.store(false, std::memory_order_release);
        return -1;
    }

    Stop();
    FreeStreamObjects();

    bool ok = ReopenAt(rate, capture_channels, playback_channels);
    if (!ok) {
        jack_error("secondary card '%s': cannot run at %u Hz; restoring %u Hz",
            fConfig.name.c_str(), rate, old_rate);
        ok = ReopenAt(old_rate, capture_channels, playback_channels);
        if (!ok) {
            // The card stays silent: it is offline until the next successful change.
            jack_error("secondary card '%s': offline, no usable rate", fConfig.name.c_str());
            return -1;
        }
    }

    if (was_running && Start() < 0) {
        jack_error("secondary card '%s': cannot restart after the rate change", fConfig.name.c_str());
        return -1;
    }

    fSilent.store(false, std::memory_order_release);
    if (ok && fRate == rate) {
        jack_info("secondary card '%s': now running at %u Hz", fConfig.name.c_str(), fRate);
        return 0;
    }
    return -1;
}

int JackAlsaSecondaryCard::Stop()
{
    if (!fSession.is_open()) {
        return 0;
    }

    fRunning.store(false);

    // The thread wakes up at the next poll timeout.  It only touches the
    // card's own ALSA stream and rings, so joining here is safe.
    fThread.Stop();

    // The RT threads have stopped: it is safe to emit their diagnostics here.
    ReportDrops();
    ReportPendingIoError();

    fSession.stop();
    return 0;
}

bool JackAlsaSecondaryCard::Init()
{
    if (fRealTime) {
        if (fThread.AcquireSelfRealTime(fPriority) < 0) {
            jack_error("secondary card '%s': cannot acquire real-time scheduling",
                fConfig.name.c_str());
        }
    }
    return true;
}

bool JackAlsaSecondaryCard::Execute()
{
    int wait_status = 0;
    float delayed_usecs = 0.f;

    if (!fRunning.load()) {
        return false;
    }

    const jackmini::alsa::WaitResult wait = fSession.wait();
    wait_status = wait.status;
    delayed_usecs = wait.delayed_usecs;
    const jack_nframes_t nframes = static_cast<jack_nframes_t>(wait.frames);

    if (!fRunning.load()) {
        return false;
    }

    if (wait_status < 0) {
        fIoErrorStatus.store(wait_status, std::memory_order_relaxed);
        fIoError.store(IoError::Wait, std::memory_order_release);
        MarkFailed();
        return false;
    }

    if (nframes == 0) {
        // An xrun was detected and recovered.
        fXrunGeneration.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    if (fCaptureEnabled) {
        if (fSession.read(fPeriod) < 0) {
            fIoError.store(IoError::Read, std::memory_order_release);
            MarkFailed();
            return false;
        }
        jack_nframes_t written = fCaptureRing->Write((const float* const*)fCaptureTemp, fPeriod);
        if (written != fPeriod) {
            fCaptureDropped.fetch_add(fPeriod - written, std::memory_order_relaxed);
        }
    }

    if (fPlaybackEnabled) {
        fPlaybackRing->Read(fPlaybackTemp, fPeriod);
        if (fSession.write(fPeriod) < 0) {
            fIoError.store(IoError::Write, std::memory_order_release);
            MarkFailed();
            return false;
        }
    }

    return fRunning.load();
}

void JackAlsaSecondaryCard::ResetResampling()
{
    if (fCaptureConverter) {
        fCaptureConverter->Reset();
    }
    if (fPlaybackConverter) {
        fPlaybackConverter->Reset();
    }
    fDrift.Reset();
}

void JackAlsaSecondaryCard::UpdateDrift(bool capture_side)
{
    if (fConfig.clock != SecondaryClockAsync) {
        return;
    }

    unsigned int generation = fXrunGeneration.load(std::memory_order_relaxed);
    if (generation != fSeenXrunGeneration) {
        fSeenXrunGeneration = generation;
        fDrift.Reset();
    }

    // The ring is measured together with the converter's own staging, so
    // the target is the total buffered audio rather than just the ring.
    double fill;
    if (capture_side) {
        fill = fCaptureRing->AvailableRead() + fCaptureConverter->Backlog();
    } else {
        fill = fPlaybackRing->AvailableRead() + fPlaybackConverter->OutputBacklog();
    }

    double rate = fDrift.Update(fill, 3.0 * fPeriod, fPeriod);

    if (capture_side) {
        // Capture converts card -> primary.  A duplex card shares one clock, so
        // the playback converter (primary -> card) runs the reciprocal rate.
        if (fCaptureConverter) {
            fCaptureConverter->SetRate(rate);
        }
        if (fPlaybackConverter) {
            fPlaybackConverter->SetRate(1.0 / rate);
        }
    } else if (fPlaybackConverter) {
        fPlaybackConverter->SetRate(rate);
    }
}

void JackAlsaSecondaryCard::ReadToPorts(JackGraphManager* graph_manager, jack_nframes_t nframes)
{
    if (!fCaptureEnabled || !fCaptureRing || fCapturePorts.size() < (size_t)fCaptureChannels) {
        return;
    }

    if (fSilent.load(std::memory_order_acquire)) {
        // Reconfiguring: publish silence rather than data from a stream being rebuilt.
        for (int i = 0; i < fCaptureChannels; i++) {
            float* buf = (float*)graph_manager->GetBuffer(fCapturePorts[i], nframes);
            if (buf) {
                memset(buf, 0, sizeof(float) * nframes);
            }
        }
        return;
    }

    for (int i = 0; i < fCaptureChannels; i++) {
        fPortScratch[i] = (float*)graph_manager->GetBuffer(fCapturePorts[i], nframes);
        if (!fPortScratch[i]) {
            fPortScratch[i] = fSilence;
        }
    }

    if (!fCaptureConverter) {
        fCaptureRing->Read(fPortScratch, nframes);
        return;
    }

    UpdateDrift(true);

    unsigned int need = fCaptureConverter->NeedInput(nframes);
    if (need > fConvCap) {
        need = fConvCap;
    }
    fCaptureRing->Read(fCaptureConvIn, need);

    unsigned int got = fCaptureConverter->Convert(fCaptureConvIn, need, fPortScratch, nframes);
    for (int i = 0; i < fCaptureChannels; i++) {
        if (got < nframes) {
            memset(fPortScratch[i] + got, 0, sizeof(float) * (nframes - got));
        }
    }
}

void JackAlsaSecondaryCard::WriteFromPorts(JackGraphManager* graph_manager, jack_nframes_t nframes)
{
    if (!fPlaybackEnabled || !fPlaybackRing || fPlaybackPorts.size() < (size_t)fPlaybackChannels) {
        return;
    }

    if (fSilent.load(std::memory_order_acquire)) {
        return;
    }

    for (int i = 0; i < fPlaybackChannels; i++) {
        if (graph_manager->GetConnectionsNum(fPlaybackPorts[i]) > 0) {
            fPortScratch[i] = (float*)graph_manager->GetBuffer(fPlaybackPorts[i], nframes);
        } else {
            fPortScratch[i] = fSilence;
        }
        if (!fPortScratch[i]) {
            fPortScratch[i] = fSilence;
        }
    }

    if (fPlaybackConverter) {
        if (!fCaptureEnabled) {
            UpdateDrift(false);
        }
        unsigned int produced = fPlaybackConverter->Convert(fPortScratch, nframes,
            fPlaybackConvOut, fConvCap);
        jack_nframes_t written = fPlaybackRing->Write((const float* const*)fPlaybackConvOut, produced);
        if (written != produced) {
            fPlaybackDropped.fetch_add(produced - written, std::memory_order_relaxed);
        }
        return;
    }

    jack_nframes_t written = fPlaybackRing->Write((const float* const*)fPortScratch, nframes);
    if (written != nframes) {
        fPlaybackDropped.fetch_add(nframes - written, std::memory_order_relaxed);
    }
}

void JackAlsaSecondaryCard::ReadInput(const jackmini::alsa::MappedFrames& mapped, std::ptrdiff_t nread)
{
    for (int i = 0; i < fCaptureChannels; i++) {
        fSession.read_channel(mapped.channels[i], fCaptureTemp[i] + nread, mapped.frames);
    }
}

void JackAlsaSecondaryCard::ClearOutput()
{
    for (int i = 0; i < fPlaybackChannels; i++) {
        memset(fPlaybackTemp[i], 0, sizeof(float) * fPeriod);
    }
}

void JackAlsaSecondaryCard::WriteOutput(const jackmini::alsa::MappedFrames& mapped, std::ptrdiff_t nwritten)
{
    for (int i = 0; i < fPlaybackChannels; i++) {
        fSession.write_channel(i, mapped.channels[i], fPlaybackTemp[i] + nwritten, mapped.frames);
    }
}

void JackAlsaSecondaryCard::SetTime(jack_time_t time)
{
    // Timing comes from the primary card.
}

int JackAlsaSecondaryCard::Restart()
{
    int res = fSession.stop();
    if (res < 0) {
        return res;
    }
    return fSession.start();
}

void JackAlsaSecondaryCard::ReadInputThunk(void* arg, jackmini::alsa::PcmCycle&,
    const jackmini::alsa::MappedFrames& mapped, std::uint32_t,
    std::ptrdiff_t nread)
{
    static_cast<JackAlsaSecondaryCard*>(arg)->ReadInput(mapped, nread);
}

void JackAlsaSecondaryCard::ClearOutputThunk(void* arg)
{
    static_cast<JackAlsaSecondaryCard*>(arg)->ClearOutput();
}

void JackAlsaSecondaryCard::WriteOutputThunk(void* arg, jackmini::alsa::PcmCycle&,
    const jackmini::alsa::MappedFrames& mapped, std::uint32_t,
    std::ptrdiff_t nwritten)
{
    static_cast<JackAlsaSecondaryCard*>(arg)->WriteOutput(mapped, nwritten);
}

void JackAlsaSecondaryCard::SetTimeThunk(void* arg, jack_time_t time)
{
    static_cast<JackAlsaSecondaryCard*>(arg)->SetTime(time);
}

int JackAlsaSecondaryCard::RestartThunk(void* arg)
{
    return static_cast<JackAlsaSecondaryCard*>(arg)->Restart();
}

} // namespace jackmini

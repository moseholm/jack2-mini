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

#include "AlsaSecondaryCard.h"
#include "AlsaSecondaryConfig.h"
#include "Error.h"
#include "GraphManager.h"
#include "PcmSession.hpp"

#include <errno.h>
#include <math.h>
#include <string.h>
#include <sys/mman.h>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <thread>

namespace jackmini {

static float** allocChannelBuffers(std::int32_t channels, std::uint32_t frames)
{
    float** buffers = new float*[channels];
    for (std::int32_t i = 0; i < channels; i++) {
        buffers[i] = new float[frames];
        memset(buffers[i], 0, sizeof(float) * frames);
    }
    return buffers;
}

static void freeChannelBuffers(float** buffers, std::int32_t channels)
{
    if (buffers == nullptr) {
        return;
    }
    for (std::int32_t i = 0; i < channels; i++) {
        delete[] buffers[i];
    }
    delete[] buffers;
}

// Best-effort residency for buffers the realtime threads touch.  A failure is
// not fatal; it only means the kernel may still fault pages in later.
static bool lockChannelBuffers(float** buffers, std::int32_t channels, std::uint32_t frames)
{
    if (buffers == nullptr || channels <= 0) {
        return true;
    }

    bool locked = mlock(buffers, sizeof(float*) * channels) == 0;
    for (std::int32_t i = 0; i < channels; i++) {
        if (buffers[i] != nullptr && frames > 0) {
            locked = (mlock(buffers[i], sizeof(float) * frames) == 0) && locked;
        }
    }
    return locked;
}

AlsaSecondaryCard::AlsaSecondaryCard(const AlsaSecondaryConfig& config, ResamplerFactory* factory)
    : m_config(config)
    , m_thread(this)
    , m_running(false)
    , m_opened(false)
    , m_realTime(false)
    , m_priority(0)
    , m_captureEnabled(false)
    , m_playbackEnabled(false)
    , m_period(0)
    , m_nperiods(0)
    , m_rate(0)
    , m_captureChannels(0)
    , m_playbackChannels(0)
    , m_captureRing(nullptr)
    , m_playbackRing(nullptr)
    , m_factory(factory)
    , m_captureConverter(nullptr)
    , m_playbackConverter(nullptr)
    , m_captureConvIn(nullptr)
    , m_playbackConvOut(nullptr)
    , m_convCap(0)
    , m_xrunGeneration(0)
    , m_seenXrunGeneration(0)
    , m_captureDropped(0)
    , m_playbackDropped(0)
    , m_ioError(IoError::kNone)
    , m_ioErrorStatus(0)
    , m_ctl(nullptr)
    , m_captureTemp(nullptr)
    , m_playbackTemp(nullptr)
    , m_portScratch(nullptr)
    , m_silence(nullptr)
{
    m_rateValid[kRateCapture] = false;
    m_rateValid[kRatePlayback] = false;
    m_silent.store(false);
    m_rTEpoch = nullptr;
    m_rTRunning = nullptr;
    m_id = -1;
    m_device = -1;
    m_failureSink = nullptr;
    m_primaryBufferSize = 0;
    m_primaryRate = 0;
}

AlsaSecondaryCard::~AlsaSecondaryCard()
{
    close();
}

void AlsaSecondaryCard::freeStreamObjects()
{
    m_session.close();
    freeBuffers();
}

void AlsaSecondaryCard::freeBuffers()
{
    if (m_captureTemp) {
        for (std::int32_t i = 0; i < m_captureChannels; i++) {
            delete[] m_captureTemp[i];
        }
        delete[] m_captureTemp;
        m_captureTemp = nullptr;
    }

    if (m_playbackTemp) {
        for (std::int32_t i = 0; i < m_playbackChannels; i++) {
            delete[] m_playbackTemp[i];
        }
        delete[] m_playbackTemp;
        m_playbackTemp = nullptr;
    }

    delete[] m_portScratch;
    m_portScratch = nullptr;

    delete[] m_silence;
    m_silence = nullptr;

    delete m_captureRing;
    m_captureRing = nullptr;

    delete m_playbackRing;
    m_playbackRing = nullptr;

    delete m_captureConverter;
    m_captureConverter = nullptr;

    delete m_playbackConverter;
    m_playbackConverter = nullptr;

    freeChannelBuffers(m_captureConvIn, m_captureChannels);
    m_captureConvIn = nullptr;

    freeChannelBuffers(m_playbackConvOut, m_playbackChannels);
    m_playbackConvOut = nullptr;
}

static jackmini::alsa::SampleFormat toAlsaFormat(AlsaSampleFormat format)
{
    switch (format) {
    case kSampleFormatFloat:
        return jackmini::alsa::SampleFormat::kFloat;
    case kSampleFormatS32:
        return jackmini::alsa::SampleFormat::kS32;
    case kSampleFormatS16:
        return jackmini::alsa::SampleFormat::kS16;
    default:
        return jackmini::alsa::SampleFormat::kAny;
    }
}

std::int32_t AlsaSecondaryCard::openStream(NframesC requestedRate,
    NframesC primaryBufferSize,
    NframesC primaryRate)
{
    const std::string& playbackDevice = m_config.m_playbackDevice.empty()
        ? m_config.m_captureDevice
        : m_config.m_playbackDevice;
    const std::string& captureDevice = m_config.m_captureDevice.empty()
        ? m_config.m_playbackDevice
        : m_config.m_captureDevice;

    // The hardware may not run at the requested rate.  If it doesn't, reopen at
    // the rate it actually gives, so the period matches the card's real clock.
    NframesC rate = requestedRate;
    bool opened = false;
    for (std::int32_t attempt = 0; attempt < 2 && !opened; attempt++) {
        double period = ((double)primaryBufferSize * (double)rate) / (double)primaryRate;
        m_period = (NframesC)llround(period);
        if (m_period < 1) {
            m_period = 1;
        }

        jack_info("secondary card '%s': device %s, mode %s, rate %u, period %u, %u periods",
            m_config.m_name.c_str(),
            (playbackDevice + "|" + captureDevice).c_str(),
            (m_captureEnabled && m_playbackEnabled) ? "duplex" : (m_captureEnabled ? "capture" : "playback"),
            rate, m_period, m_nperiods);

        jackmini::alsa::Request request;
        request.m_rate = rate;
        request.m_framesPerCycle = m_period;
        request.m_periods = m_nperiods;
        request.m_captureChannels = m_captureEnabled ? m_config.m_captureChannels : 0;
        request.m_playbackChannels = m_playbackEnabled ? m_config.m_playbackChannels : 0;
        request.m_format = toAlsaFormat(m_config.m_sampleFormat);
        if (m_session.open(m_captureEnabled ? captureDevice.c_str() : nullptr,
                m_playbackEnabled ? playbackDevice.c_str() : nullptr,
                request, kNone)
            < 0) {
            jack_error("secondary card '%s': cannot open ALSA device %s",
                m_config.m_name.c_str(), (playbackDevice + "|" + captureDevice).c_str());
            return -1;
        }

        if (m_session.rate() == rate) {
            opened = true;
            break;
        }

        if (attempt == 0) {
            jack_info("secondary card '%s': device runs at %u Hz, not the requested %u Hz; using %u Hz",
                m_config.m_name.c_str(), (std::uint32_t)m_session.rate(), (std::uint32_t)rate,
                (std::uint32_t)m_session.rate());
            rate = m_session.rate();
        } else {
            jack_error("secondary card '%s': device rate %u Hz does not settle",
                m_config.m_name.c_str(), (std::uint32_t)m_session.rate());
        }
        m_session.close();
    }

    if (!opened) {
        return -1;
    }

    m_captureChannels = m_captureEnabled ? m_session.info(jackmini::alsa::Stream::kCapture).m_channels : 0;
    m_playbackChannels = m_playbackEnabled ? m_session.info(jackmini::alsa::Stream::kPlayback).m_channels : 0;
    m_period = m_session.framesPerCycle();
    m_rate = m_session.rate();
    return 0;
}

std::int32_t AlsaSecondaryCard::buildBuffers(NframesC primaryBufferSize,
    NframesC primaryRate)
{
    m_captureTemp = new float*[m_captureChannels];
    for (std::int32_t i = 0; i < m_captureChannels; i++) {
        m_captureTemp[i] = new float[m_period];
        memset(m_captureTemp[i], 0, sizeof(float) * m_period);
    }

    m_playbackTemp = new float*[m_playbackChannels];
    for (std::int32_t i = 0; i < m_playbackChannels; i++) {
        m_playbackTemp[i] = new float[m_period];
        memset(m_playbackTemp[i], 0, sizeof(float) * m_period);
    }

    std::int32_t scratchChannels = std::max(m_captureChannels, m_playbackChannels);
    m_portScratch = new float*[std::max(scratchChannels, 1)];

    NframesC silenceSize = std::max(primaryBufferSize, m_period);
    m_silence = new float[silenceSize];
    memset(m_silence, 0, sizeof(float) * silenceSize);

    m_convCap = std::max(primaryBufferSize, m_period) * 4 + 1024;
    // Asynchronous clocks always go through a converter, even at equal rates,
    // so the drift controller has something to steer.
    if (m_rate != primaryRate || m_config.m_clock == kSecondaryClockAsync) {
        if (m_factory == nullptr) {
            jack_error("secondary card '%s': no resampler factory", m_config.m_name.c_str());
            freeBuffers();
            m_session.close();
            return -1;
        }

        // Capture: card rate -> primary rate.  Playback: primary rate -> card rate.
        if (m_captureEnabled) {
            ResamplerParams params = { (std::uint32_t)m_captureChannels, m_rate, primaryRate, -1 };
            Resampler* resampler = m_factory->create(params);
            m_captureConverter = new AlsaRateConverter(resampler, m_captureChannels, m_convCap);
            if (!m_captureConverter->isValid()) {
                jack_error("secondary card '%s': cannot create capture resampler %u->%u",
                    m_config.m_name.c_str(), m_rate, primaryRate);
                freeBuffers();
                m_session.close();
                return -1;
            }
            m_captureConvIn = allocChannelBuffers(m_captureChannels, m_convCap);
        }

        if (m_playbackEnabled) {
            ResamplerParams params = { (std::uint32_t)m_playbackChannels, primaryRate, m_rate, -1 };
            Resampler* resampler = m_factory->create(params);
            m_playbackConverter = new AlsaRateConverter(resampler, m_playbackChannels, m_convCap);
            if (!m_playbackConverter->isValid()) {
                jack_error("secondary card '%s': cannot create playback resampler %u->%u",
                    m_config.m_name.c_str(), primaryRate, m_rate);
                freeBuffers();
                m_session.close();
                return -1;
            }
            m_playbackConvOut = allocChannelBuffers(m_playbackChannels, m_convCap);
        }
    }

    NframesC ringCapacity = m_period * 8;
    if (ringCapacity < m_period + 1) {
        ringCapacity = m_period + 1;
    }

    if (m_captureEnabled) {
        m_captureRing = new AlsaRingBuffer(m_captureChannels, ringCapacity);
        m_capturePorts.resize(m_captureChannels, 0);
    }
    if (m_playbackEnabled) {
        m_playbackRing = new AlsaRingBuffer(m_playbackChannels, ringCapacity);
        m_playbackPorts.resize(m_playbackChannels, 0);
    }

    // The card thread and the primary RT thread use these buffers every cycle.
    // Keep them resident now, on the non-realtime setup path.
    bool locked = true;
    if (m_captureRing) {
        locked = m_captureRing->lockMemory() && locked;
    }
    if (m_playbackRing) {
        locked = m_playbackRing->lockMemory() && locked;
    }
    locked = lockChannelBuffers(m_captureTemp, m_captureChannels, m_period) && locked;
    locked = lockChannelBuffers(m_playbackTemp, m_playbackChannels, m_period) && locked;
    locked = lockChannelBuffers(m_captureConvIn, m_captureChannels, m_convCap) && locked;
    locked = lockChannelBuffers(m_playbackConvOut, m_playbackChannels, m_convCap) && locked;
    if (m_portScratch) {
        locked = (mlock(m_portScratch, sizeof(float*) * std::max(scratchChannels, 1)) == 0) && locked;
    }
    if (m_silence) {
        locked = (mlock(m_silence, sizeof(float) * silenceSize) == 0) && locked;
    }
    if (!locked) {
        jack_log("secondary card '%s': could not lock all real-time buffers",
            m_config.m_name.c_str());
    }

    jackmini::alsa::PcmCycle::Callbacks callbacks;
    callbacks.m_arg = this;
    callbacks.m_readInput = readInputThunk;
    callbacks.m_clearOutput = clearOutputThunk;
    callbacks.m_writeOutput = writeOutputThunk;
    callbacks.m_setTime = setTimeThunk;
    callbacks.m_restart = restartThunk;
    m_session.setCallbacks(callbacks);
    return 0;
}

std::int32_t AlsaSecondaryCard::open(NframesC primaryBufferSize,
    NframesC primaryRate,
    NframesC primaryNperiods,
    bool realtime,
    std::int32_t priority)
{
    if (m_opened) {
        return 0;
    }

    m_realTime = realtime;
    m_priority = priority;

    m_captureEnabled = (m_config.m_mode != kSecondaryModePlayback);
    m_playbackEnabled = (m_config.m_mode != kSecondaryModeCapture);

    m_device = alsaPcmDeviceNumber(m_config.m_captureDevice.empty() ? m_config.m_playbackDevice : m_config.m_captureDevice);

    NframesC requestedRate = (m_config.m_rate > 0) ? m_config.m_rate : primaryRate;
    m_nperiods = (m_config.m_nperiods > 0) ? m_config.m_nperiods : primaryNperiods;

    if (primaryBufferSize == 0 || primaryRate == 0) {
        jack_error("secondary card '%s': invalid primary timing", m_config.m_name.c_str());
        return -1;
    }

    m_primaryBufferSize = primaryBufferSize;
    m_primaryRate = primaryRate;

    if (openStream(requestedRate, primaryBufferSize, primaryRate) < 0) {
        return -1;
    }
    if (buildBuffers(primaryBufferSize, primaryRate) < 0) {
        return -1;
    }

    if (openControls() < 0) {
        close();
        return -1;
    }

    m_opened = true;
    return 0;
}

NframesC AlsaSecondaryCard::captureLatency() const
{
    if (m_rate == 0 || m_primaryRate == 0) {
        return 0;
    }
    // Card frames -> primary frames.
    double ratio = (double)m_primaryRate / (double)m_rate;
    double frames = m_primaryBufferSize;
    if (m_captureConverter) {
        // The RT side keeps about three periods in the ring, and the converter adds its filter delay.
        frames += (3.0 * m_period + m_captureConverter->delay()) * ratio;
    }
    return (NframesC)ceil(frames);
}

NframesC AlsaSecondaryCard::playbackLatency() const
{
    if (m_rate == 0 || m_primaryRate == 0) {
        return 0;
    }
    double ratio = (double)m_primaryRate / (double)m_rate;
    // The card's ALSA buffer, the playback ring's prefill, and the converter delay.
    double cardFrames = (double)(m_nperiods > 0 ? m_nperiods - 1 : 0) * m_period + 3.0 * m_period;
    if (m_playbackConverter) {
        cardFrames += m_playbackConverter->delay();
    }
    return (NframesC)ceil(m_primaryBufferSize + cardFrames * ratio);
}

std::int32_t AlsaSecondaryCard::openControls()
{
    const std::string& capturePcm = m_config.m_captureDevice.empty() ? m_config.m_playbackDevice : m_config.m_captureDevice;
    const std::string& playbackPcm = m_config.m_playbackDevice.empty() ? m_config.m_captureDevice : m_config.m_playbackDevice;

    std::string captureCtl = alsaControlDeviceName(capturePcm);
    std::string playbackCtl = alsaControlDeviceName(playbackPcm);
    if (captureCtl.empty() || playbackCtl.empty() || captureCtl != playbackCtl) {
        jack_error("secondary card '%s': capture and playback devices must belong to the same ALSA card",
            m_config.m_name.c_str());
        return -1;
    }
    m_ctlName = captureCtl;

    std::int32_t err = snd_ctl_open(&m_ctl, m_ctlName.c_str(), 0);
    if (err < 0) {
        m_ctl = nullptr;
        jack_error("secondary card '%s': cannot open control device %s (%s)",
            m_config.m_name.c_str(), m_ctlName.c_str(), snd_strerror(err));
        return -1;
    }

    for (std::int32_t d = 0; d < 2; d++) {
        AlsaRateDirection dir = (AlsaRateDirection)d;
        bool enabled = (dir == kRateCapture) ? m_captureEnabled : m_playbackEnabled;
        if (!enabled) {
            continue;
        }

        const std::string& configured = (dir == kRateCapture) ? m_config.m_captureRateCtl : m_config.m_playbackRateCtl;
        AlsaRateElement elem;
        std::string error;
        std::int32_t res = alsaControlResolveRate(m_ctl, dir, configured, &elem, &error);
        if (res < 0) {
            jack_error("secondary card '%s': %s", m_config.m_name.c_str(), error.c_str());
            return -1;
        }

        if (res == 1) {
            if (alsaControlCreateRate(m_ctl, dir, m_device, m_rate, &elem) < 0) {
                // Without a control the card keeps its startup rate; ASRC still
                // corrects drift.
                jack_error("secondary card '%s': cannot create a %s rate control; keeping %u Hz",
                    m_config.m_name.c_str(), (dir == kRateCapture) ? "capture" : "playback", m_rate);
                continue;
            }
            jack_info("secondary card '%s': %s rate control is JACK element '%s' (numid %u)",
                m_config.m_name.c_str(), (dir == kRateCapture) ? "capture" : "playback",
                elem.m_name.c_str(), elem.m_numid);
        } else {
            jack_info("secondary card '%s': %s rate control is native element '%s' (numid %u, %s)",
                m_config.m_name.c_str(), (dir == kRateCapture) ? "capture" : "playback",
                elem.m_name.c_str(), elem.m_numid, elem.m_writable ? "writable" : "read-only");
        }

        m_rateElem[d] = elem;
        m_rateValid[d] = true;
    }

    return 0;
}

void AlsaSecondaryCard::closeControls()
{
    if (!m_ctl) {
        return;
    }
    for (std::int32_t d = 0; d < 2; d++) {
        if (m_rateValid[d] && m_rateElem[d].m_user) {
            std::int32_t err = alsaControlRemoveRate(m_ctl, m_rateElem[d]);
            // ENODEV: the card has been unplugged, so its control is already gone.
            if (err < 0 && err != -ENODEV) {
                jack_error("secondary card '%s': cannot remove control '%s' (%s)",
                    m_config.m_name.c_str(), m_rateElem[d].m_name.c_str(), strerror(-err));
            }
        }
        m_rateValid[d] = false;
    }
    snd_ctl_close(m_ctl);
    m_ctl = nullptr;
}

std::int32_t AlsaSecondaryCard::readRate(AlsaRateDirection dir, std::int64_t* value)
{
    if (!m_ctl || !m_rateValid[dir]) {
        return -1;
    }
    return alsaControlReadRate(m_ctl, m_rateElem[dir], value);
}

std::int32_t AlsaSecondaryCard::writeRate(AlsaRateDirection dir, std::int64_t value)
{
    if (!m_ctl || !m_rateValid[dir]) {
        return -1;
    }
    return alsaControlWriteRate(m_ctl, m_rateElem[dir], value);
}

std::int32_t AlsaSecondaryCard::close()
{
    if (!m_opened && !m_session.isOpen()) {
        return 0;
    }

    stop();
    closeControls();
    freeStreamObjects();
    m_capturePorts.clear();
    m_playbackPorts.clear();
    m_opened = false;
    m_captureChannels = 0;
    m_playbackChannels = 0;
    m_rate = 0;
    m_period = 0;
    return 0;
}

std::int32_t AlsaSecondaryCard::start()
{
    if (!m_opened) {
        return -1;
    }
    if (m_running.load()) {
        return 0;
    }

    if (m_captureRing) {
        m_captureRing->reset();
    }
    resetResampling();
    m_seenXrunGeneration = m_xrunGeneration.load();

    if (m_captureRing && m_captureConverter) {
        // Give the converter a cushion of silence, matching the playback ring.
        for (std::int32_t i = 0; i < m_captureChannels; i++) {
            m_portScratch[i] = m_silence;
        }
        for (std::int32_t period = 0; period < 3; period++) {
            m_captureRing->write((const float* const*)m_portScratch, m_period);
        }
    }
    if (m_playbackRing) {
        // Pre-fill a few periods of silence so the first writes do not
        // immediately underrun while the card thread spins up.
        m_playbackRing->reset();
        for (std::int32_t i = 0; i < m_playbackChannels; i++) {
            m_portScratch[i] = m_silence;
        }
        for (std::int32_t period = 0; period < 3; period++) {
            m_playbackRing->write((const float* const*)m_portScratch, m_period);
        }
    }

    if (m_session.start() < 0) {
        jack_error("secondary card '%s': cannot start ALSA stream", m_config.m_name.c_str());
        return -1;
    }

    m_running.store(true);
    if (m_thread.startSync() < 0) {
        jack_error("secondary card '%s': cannot start I/O thread", m_config.m_name.c_str());
        m_running.store(false);
        m_session.stop();
        return -1;
    }

    return 0;
}

std::string AlsaSecondaryCard::portBase() const
{
    if (m_device < 0) {
        return m_config.m_name;
    }
    return m_config.m_name + "_d" + std::to_string(m_device);
}

void AlsaSecondaryCard::markFailed()
{
    m_running.store(false);
    if (m_failureSink != nullptr) {
        m_failureSink->onCardFailed(m_id);
    }
}

void AlsaSecondaryCard::reportDrops()
{
    std::uint64_t capture = m_captureDropped.exchange(0, std::memory_order_relaxed);
    std::uint64_t playback = m_playbackDropped.exchange(0, std::memory_order_relaxed);

    if (capture) {
        jack_log("secondary card '%s': dropped %llu capture frames",
            m_config.m_name.c_str(), (std::uint64_t)capture);
    }
    if (playback) {
        jack_log("secondary card '%s': dropped %llu playback frames",
            m_config.m_name.c_str(), (std::uint64_t)playback);
    }
}

void AlsaSecondaryCard::reportPendingIoError()
{
    IoError error = m_ioError.exchange(IoError::kNone, std::memory_order_acquire);
    std::int32_t status = m_ioErrorStatus.load(std::memory_order_relaxed);

    switch (error) {
    case IoError::kWait:
        jack_error("secondary card '%s': ALSA wait failed (status %d)",
            m_config.m_name.c_str(), status);
        break;
    case IoError::kRead:
        jack_error("secondary card '%s': ALSA read failed", m_config.m_name.c_str());
        break;
    case IoError::kWrite:
        jack_error("secondary card '%s': ALSA write failed", m_config.m_name.c_str());
        break;
    case IoError::kNone:
        break;
    }
}

bool AlsaSecondaryCard::waitForRTCycles(std::uint32_t cycles)
{
    // A cycle that was already running when fSilent was set may finish one
    // more time, so wait for two completed cycles to be sure the RT thread
    // has seen the flag.
    std::uint64_t start = m_rTEpoch->load(std::memory_order_acquire);
    for (std::int32_t i = 0; i < 2000; i++) {
        if (m_rTEpoch->load(std::memory_order_acquire) >= start + cycles) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

bool AlsaSecondaryCard::reopenAt(NframesC rate, std::int32_t captureChannels, std::int32_t playbackChannels)
{
    if (openStream(rate, m_primaryBufferSize, m_primaryRate) < 0) {
        return false;
    }
    if (m_captureChannels != captureChannels || m_playbackChannels != playbackChannels) {
        // The port lists are fixed, so the channel layout must not change.
        jack_error("secondary card '%s': channel count changed at %u Hz", m_config.m_name.c_str(), rate);
        freeStreamObjects();
        return false;
    }
    if (buildBuffers(m_primaryBufferSize, m_primaryRate) < 0) {
        freeStreamObjects();
        return false;
    }
    return true;
}

std::int32_t AlsaSecondaryCard::reconfigure(NframesC rate)
{
    if (!m_opened) {
        return -1;
    }
    if (rate == m_rate) {
        return 0;
    }

    NframesC oldRate = m_rate;
    std::int32_t captureChannels = m_captureChannels;
    std::int32_t playbackChannels = m_playbackChannels;
    bool wasRunning = m_running.load();

    // Publish silence first, then wait until the RT thread has run past this
    // point, so it never touches a buffer while it is being freed.
    m_silent.store(true, std::memory_order_release);
    // The RT side may use this card's buffers while the driver runs, even when
    // the card's own thread has stopped (for example after an I/O error).
    bool rtLive = (m_rTRunning != nullptr && m_rTRunning->load());
    if (rtLive && m_rTEpoch != nullptr && !waitForRTCycles(2)) {
        jack_error("secondary card '%s': real-time thread did not reach a safe point; keeping %u Hz",
            m_config.m_name.c_str(), oldRate);
        m_silent.store(false, std::memory_order_release);
        return -1;
    }

    stop();
    freeStreamObjects();

    bool ok = reopenAt(rate, captureChannels, playbackChannels);
    if (!ok) {
        jack_error("secondary card '%s': cannot run at %u Hz; restoring %u Hz",
            m_config.m_name.c_str(), rate, oldRate);
        ok = reopenAt(oldRate, captureChannels, playbackChannels);
        if (!ok) {
            // The card stays silent: it is offline until the next successful change.
            jack_error("secondary card '%s': offline, no usable rate", m_config.m_name.c_str());
            return -1;
        }
    }

    if (wasRunning && start() < 0) {
        jack_error("secondary card '%s': cannot restart after the rate change", m_config.m_name.c_str());
        return -1;
    }

    m_silent.store(false, std::memory_order_release);
    if (ok && m_rate == rate) {
        jack_info("secondary card '%s': now running at %u Hz", m_config.m_name.c_str(), m_rate);
        return 0;
    }
    return -1;
}

std::int32_t AlsaSecondaryCard::stop()
{
    if (!m_session.isOpen()) {
        return 0;
    }

    m_running.store(false);

    // The thread wakes up at the next poll timeout.  It only touches the
    // card's own ALSA stream and rings, so joining here is safe.
    m_thread.stop();

    // The RT threads have stopped: it is safe to emit their diagnostics here.
    reportDrops();
    reportPendingIoError();

    m_session.stop();
    return 0;
}

bool AlsaSecondaryCard::init()
{
    if (m_realTime) {
        if (m_thread.acquireSelfRealTime(m_priority) < 0) {
            jack_error("secondary card '%s': cannot acquire real-time scheduling",
                m_config.m_name.c_str());
        }
    }
    return true;
}

bool AlsaSecondaryCard::execute()
{
    std::int32_t waitStatus = 0;
    float delayedUsecs = 0.f;

    if (!m_running.load()) {
        return false;
    }

    const jackmini::alsa::WaitResult wait = m_session.wait();
    waitStatus = wait.m_status;
    delayedUsecs = wait.m_delayedUsecs;
    const NframesC nframes = static_cast<NframesC>(wait.m_frames);

    if (!m_running.load()) {
        return false;
    }

    if (waitStatus < 0) {
        m_ioErrorStatus.store(waitStatus, std::memory_order_relaxed);
        m_ioError.store(IoError::kWait, std::memory_order_release);
        markFailed();
        return false;
    }

    if (nframes == 0) {
        // An xrun was detected and recovered.
        m_xrunGeneration.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    if (m_captureEnabled) {
        if (m_session.read(m_period) < 0) {
            m_ioError.store(IoError::kRead, std::memory_order_release);
            markFailed();
            return false;
        }
        NframesC written = m_captureRing->write((const float* const*)m_captureTemp, m_period);
        if (written != m_period) {
            m_captureDropped.fetch_add(m_period - written, std::memory_order_relaxed);
        }
    }

    if (m_playbackEnabled) {
        m_playbackRing->read(m_playbackTemp, m_period);
        if (m_session.write(m_period) < 0) {
            m_ioError.store(IoError::kWrite, std::memory_order_release);
            markFailed();
            return false;
        }
    }

    return m_running.load();
}

void AlsaSecondaryCard::resetResampling()
{
    if (m_captureConverter) {
        m_captureConverter->reset();
    }
    if (m_playbackConverter) {
        m_playbackConverter->reset();
    }
    m_drift.reset();
}

void AlsaSecondaryCard::updateDrift(bool captureSide)
{
    if (m_config.m_clock != kSecondaryClockAsync) {
        return;
    }

    std::uint32_t generation = m_xrunGeneration.load(std::memory_order_relaxed);
    if (generation != m_seenXrunGeneration) {
        m_seenXrunGeneration = generation;
        m_drift.reset();
    }

    // The ring is measured together with the converter's own staging, so
    // the target is the total buffered audio rather than just the ring.
    double fill;
    if (captureSide) {
        fill = m_captureRing->availableRead() + m_captureConverter->backlog();
    } else {
        fill = m_playbackRing->availableRead() + m_playbackConverter->outputBacklog();
    }

    double rate = m_drift.update(fill, 3.0 * m_period, m_period);

    if (captureSide) {
        // Capture converts card -> primary.  A duplex card shares one clock, so
        // the playback converter (primary -> card) runs the reciprocal rate.
        if (m_captureConverter) {
            m_captureConverter->setRate(rate);
        }
        if (m_playbackConverter) {
            m_playbackConverter->setRate(1.0 / rate);
        }
    } else if (m_playbackConverter) {
        m_playbackConverter->setRate(rate);
    }
}

void AlsaSecondaryCard::readToPorts(GraphManager* graphManager, NframesC nframes)
{
    if (!m_captureEnabled || !m_captureRing || m_capturePorts.size() < (size_t)m_captureChannels) {
        return;
    }

    if (m_silent.load(std::memory_order_acquire)) {
        // Reconfiguring: publish silence rather than data from a stream being rebuilt.
        for (std::int32_t i = 0; i < m_captureChannels; i++) {
            float* buf = (float*)graphManager->getBuffer(m_capturePorts[i], nframes);
            if (buf) {
                memset(buf, 0, sizeof(float) * nframes);
            }
        }
        return;
    }

    for (std::int32_t i = 0; i < m_captureChannels; i++) {
        m_portScratch[i] = (float*)graphManager->getBuffer(m_capturePorts[i], nframes);
        if (!m_portScratch[i]) {
            m_portScratch[i] = m_silence;
        }
    }

    if (!m_captureConverter) {
        m_captureRing->read(m_portScratch, nframes);
        return;
    }

    updateDrift(true);

    std::uint32_t need = m_captureConverter->needInput(nframes);
    if (need > m_convCap) {
        need = m_convCap;
    }
    m_captureRing->read(m_captureConvIn, need);

    std::uint32_t got = m_captureConverter->convert(m_captureConvIn, need, m_portScratch, nframes);
    for (std::int32_t i = 0; i < m_captureChannels; i++) {
        if (got < nframes) {
            memset(m_portScratch[i] + got, 0, sizeof(float) * (nframes - got));
        }
    }
}

void AlsaSecondaryCard::writeFromPorts(GraphManager* graphManager, NframesC nframes)
{
    if (!m_playbackEnabled || !m_playbackRing || m_playbackPorts.size() < (size_t)m_playbackChannels) {
        return;
    }

    if (m_silent.load(std::memory_order_acquire)) {
        return;
    }

    for (std::int32_t i = 0; i < m_playbackChannels; i++) {
        if (graphManager->getConnectionsNum(m_playbackPorts[i]) > 0) {
            m_portScratch[i] = (float*)graphManager->getBuffer(m_playbackPorts[i], nframes);
        } else {
            m_portScratch[i] = m_silence;
        }
        if (!m_portScratch[i]) {
            m_portScratch[i] = m_silence;
        }
    }

    if (m_playbackConverter) {
        if (!m_captureEnabled) {
            updateDrift(false);
        }
        std::uint32_t produced = m_playbackConverter->convert(m_portScratch, nframes,
            m_playbackConvOut, m_convCap);
        NframesC written = m_playbackRing->write((const float* const*)m_playbackConvOut, produced);
        if (written != produced) {
            m_playbackDropped.fetch_add(produced - written, std::memory_order_relaxed);
        }
        return;
    }

    NframesC written = m_playbackRing->write((const float* const*)m_portScratch, nframes);
    if (written != nframes) {
        m_playbackDropped.fetch_add(nframes - written, std::memory_order_relaxed);
    }
}

void AlsaSecondaryCard::readInput(const jackmini::alsa::MappedFrames& mapped, std::ptrdiff_t nread)
{
    for (std::int32_t i = 0; i < m_captureChannels; i++) {
        m_session.readChannel(mapped.m_channels[i], m_captureTemp[i] + nread, mapped.m_frames);
    }
}

void AlsaSecondaryCard::clearOutput()
{
    for (std::int32_t i = 0; i < m_playbackChannels; i++) {
        memset(m_playbackTemp[i], 0, sizeof(float) * m_period);
    }
}

void AlsaSecondaryCard::writeOutput(const jackmini::alsa::MappedFrames& mapped, std::ptrdiff_t nwritten)
{
    for (std::int32_t i = 0; i < m_playbackChannels; i++) {
        m_session.writeChannel(i, mapped.m_channels[i], m_playbackTemp[i] + nwritten, mapped.m_frames);
    }
}

void AlsaSecondaryCard::setTime(TimeC time)
{
    // Timing comes from the primary card.
}

std::int32_t AlsaSecondaryCard::restart()
{
    std::int32_t res = m_session.stop();
    if (res < 0) {
        return res;
    }
    return m_session.start();
}

void AlsaSecondaryCard::readInputThunk(void* arg, jackmini::alsa::PcmCycle&,
    const jackmini::alsa::MappedFrames& mapped, std::uint32_t,
    std::ptrdiff_t nread)
{
    static_cast<AlsaSecondaryCard*>(arg)->readInput(mapped, nread);
}

void AlsaSecondaryCard::clearOutputThunk(void* arg)
{
    static_cast<AlsaSecondaryCard*>(arg)->clearOutput();
}

void AlsaSecondaryCard::writeOutputThunk(void* arg, jackmini::alsa::PcmCycle&,
    const jackmini::alsa::MappedFrames& mapped, std::uint32_t,
    std::ptrdiff_t nwritten)
{
    static_cast<AlsaSecondaryCard*>(arg)->writeOutput(mapped, nwritten);
}

void AlsaSecondaryCard::setTimeThunk(void* arg, TimeC time)
{
    static_cast<AlsaSecondaryCard*>(arg)->setTime(time);
}

std::int32_t AlsaSecondaryCard::restartThunk(void* arg)
{
    return static_cast<AlsaSecondaryCard*>(arg)->restart();
}

} // namespace jackmini

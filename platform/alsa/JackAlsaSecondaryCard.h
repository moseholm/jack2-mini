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

#pragma once

#include "JackAlsaCardFailureSink.h"
#include "JackAlsaControl.h"
#include "JackAlsaDriftController.h"
#include "JackAlsaRateConverter.h"
#include "JackAlsaRingBuffer.h"
#include "JackAlsaSecondaryConfig.h"
#include "JackPlatformPlug.h"
#include "JackThread.h"
#include "resample/JackResampler.h"
#include "types.h"
#include "pcm_session.hpp"

#include <array>
#include <cstdint>
#include <atomic>
#include <string>
#include <vector>

namespace jackmini {

class GraphManager;

// \brief One secondary ALSA card.
//
// The card owns its ALSA stream, its I/O thread and the lock-free rings that
// connect it to the primary RT thread.  The primary thread only touches the
// rings and the JACK port buffers; all ALSA calls happen on the card's own
// thread.

class AlsaSecondaryCard : public RunnableInterface {

private:
    AlsaSecondaryConfig m_config;
    jackmini::alsa::Session m_session;
    Thread m_thread;
    std::atomic<bool> m_running;

    bool m_opened;
    bool m_realTime;
    std::int32_t m_priority;
    bool m_captureEnabled;
    bool m_playbackEnabled;

    NframesC m_period;
    NframesC m_nperiods;
    NframesC m_rate;
    std::int32_t m_captureChannels;
    std::int32_t m_playbackChannels;

    AlsaRingBuffer* m_captureRing;
    AlsaRingBuffer* m_playbackRing;

    ResamplerFactory* m_factory;
    AlsaRateConverter* m_captureConverter;
    AlsaRateConverter* m_playbackConverter;
    float** m_captureConvIn;
    float** m_playbackConvOut;
    std::uint32_t m_convCap;

    AlsaDriftController m_drift;
    std::atomic<std::uint32_t> m_xrunGeneration;
    std::uint32_t m_seenXrunGeneration;

    // Counters for the realtime threads.  The audio callbacks only bump
    // these; the messages are emitted later from a non-realtime thread.
    std::atomic<std::uint64_t> m_captureDropped;
    std::atomic<std::uint64_t> m_playbackDropped;

    enum class IoError : std::int32_t {
        kNone = 0,
        kWait,
        kRead,
        kWrite
    };
    std::atomic<IoError> m_ioError;
    std::atomic<std::int32_t> m_ioErrorStatus;

    snd_ctl_t* m_ctl;
    std::string m_ctlName;
    std::array<AlsaRateElement, 2> m_rateElem;
    std::array<bool, 2> m_rateValid;

    float** m_captureTemp;
    float** m_playbackTemp;
    float** m_portScratch;
    float* m_silence;

    std::vector<PortIdC> m_capturePorts;
    std::vector<PortIdC> m_playbackPorts;

    void freeBuffers();
    void freeStreamObjects();
    std::int32_t openStream(NframesC requestedRate, NframesC primaryBufferSize,
        NframesC primaryRate);
    std::int32_t buildBuffers(NframesC primaryBufferSize, NframesC primaryRate);
    bool reopenAt(NframesC rate, std::int32_t captureChannels, std::int32_t playbackChannels);
    bool waitForRTCycles(std::uint32_t cycles);

    NframesC m_primaryBufferSize;
    NframesC m_primaryRate;
    std::atomic<bool> m_silent;
    const std::atomic<std::uint64_t>* m_rTEpoch;
    const std::atomic<bool>* m_rTRunning;
    std::int32_t m_id;
    std::int32_t m_device;
    AlsaCardFailureSink* m_failureSink;

    // Stops the card after an I/O error and tells the driver.
    void markFailed();
    // Non-realtime reporting of conditions collected by the RT threads.
    void reportDrops();
    void reportPendingIoError();
    void updateDrift(bool captureSide);
    void closeControls();

    // ALSA core callbacks
    void readInput(const jackmini::alsa::MappedFrames& mapped, std::ptrdiff_t nread);
    void clearOutput();
    void writeOutput(const jackmini::alsa::MappedFrames& mapped, std::ptrdiff_t nwritten);
    void setTime(TimeC time);
    std::int32_t restart();

    static void readInputThunk(void* arg, jackmini::alsa::PcmCycle& cycle, const jackmini::alsa::MappedFrames& mapped,
        std::uint32_t origFrames, std::ptrdiff_t nread);
    static void clearOutputThunk(void* arg);
    static void writeOutputThunk(void* arg, jackmini::alsa::PcmCycle& cycle,
        const jackmini::alsa::MappedFrames& mapped, std::uint32_t origFrames,
        std::ptrdiff_t nwritten);
    static void setTimeThunk(void* arg, TimeC time);
    static std::int32_t restartThunk(void* arg);

    AlsaSecondaryCard(const AlsaSecondaryCard&);
    AlsaSecondaryCard& operator=(const AlsaSecondaryCard&);

public:
    AlsaSecondaryCard(const AlsaSecondaryConfig& config, ResamplerFactory* factory);
    virtual ~AlsaSecondaryCard();

    std::int32_t open(NframesC primaryBufferSize,
        NframesC primaryRate,
        NframesC primaryNperiods,
        bool realtime,
        std::int32_t priority);
    std::int32_t close();

    std::int32_t start();
    std::int32_t stop();

    // JackRunnableInterface
    virtual bool init();
    virtual bool execute();

    // RT side, called from the primary driver thread.
    void readToPorts(GraphManager* graphManager, NframesC nframes);
    void writeFromPorts(GraphManager* graphManager, NframesC nframes);

    const std::string& name() const { return m_config.m_name; }

    // The device number of this card's PCM, or -1 when it is not known.
    std::int32_t device() const { return m_device; }

    // The name used in port names: the card name with its device, e.g. "nvidia_d3".
    std::string portBase() const;

    // Stable identifier chosen by the driver; used to route events to the card.
    std::int32_t id() const { return m_id; }
    void setId(std::int32_t id) { m_id = id; }
    void setFailureSink(AlsaCardFailureSink* sink) { m_failureSink = sink; }
    const AlsaSecondaryConfig& config() const { return m_config; }

    bool captureEnabled() const { return m_captureEnabled; }
    bool playbackEnabled() const { return m_playbackEnabled; }
    std::int32_t captureChannels() const { return m_captureChannels; }
    std::int32_t playbackChannels() const { return m_playbackChannels; }
    NframesC period() const { return m_period; }
    NframesC rate() const { return m_rate; }
    bool isOpen() const { return m_opened; }
    bool isRunning() const { return m_running.load(); }
    // Clears resampler history and drift state.  Called by the driver after a primary xrun.
    void resetResampling();

    // Gives the card the driver's RT cycle counter, used to know when the RT
    // thread has stopped touching the card during a reconfiguration.
    // The driver shares its RT cycle counter and running state.  While the RT
    // side runs, a reconfiguration waits for it to pass a safe point.
    void setRTState(const std::atomic<std::uint64_t>* epoch, const std::atomic<bool>* running)
    {
        m_rTEpoch = epoch;
        m_rTRunning = running;
    }

    // Changes the stream rate: stops the card, reopens the ALSA stream and rebuilds
    // its buffers, converters and rings, then restarts it if it was running.  If the
    // new rate cannot be opened the previous rate is restored and -1 is returned.
    std::int32_t reconfigure(NframesC rate);

    // Latency of the card's ports, in primary frames.  Counts the cycle, the
    // card's ALSA buffer and the ring and converter buffering on this side.
    NframesC captureLatency() const;
    NframesC playbackLatency() const;

    // Resolves or creates the rate elements for the enabled directions.
    std::int32_t openControls();

    const std::string& controlName() const { return m_ctlName; }
    bool rateElementValid(AlsaRateDirection dir) const { return m_rateValid[dir]; }
    const AlsaRateElement& rateElement(AlsaRateDirection dir) const { return m_rateElem[dir]; }
    std::int32_t readRate(AlsaRateDirection dir, std::int64_t* value);
    std::int32_t writeRate(AlsaRateDirection dir, std::int64_t value);

    bool isConverting() const { return m_captureConverter != nullptr || m_playbackConverter != nullptr; }

    std::vector<PortIdC>& capturePorts() { return m_capturePorts; }
    std::vector<PortIdC>& playbackPorts() { return m_playbackPorts; }
};

} // namespace jackmini

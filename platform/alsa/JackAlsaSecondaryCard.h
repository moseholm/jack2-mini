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

class JackGraphManager;

// \brief One secondary ALSA card.
//
// The card owns its ALSA stream, its I/O thread and the lock-free rings that
// connect it to the primary RT thread.  The primary thread only touches the
// rings and the JACK port buffers; all ALSA calls happen on the card's own
// thread.

class JackAlsaSecondaryCard : public JackRunnableInterface {

private:
    JackAlsaSecondaryConfig fConfig;
    jackmini::alsa::Session fSession;
    JackThread fThread;
    std::atomic<bool> fRunning;

    bool fOpened;
    bool fRealTime;
    std::int32_t fPriority;
    bool fCaptureEnabled;
    bool fPlaybackEnabled;

    jack_nframes_t fPeriod;
    jack_nframes_t fNperiods;
    jack_nframes_t fRate;
    std::int32_t fCaptureChannels;
    std::int32_t fPlaybackChannels;

    JackAlsaRingBuffer* fCaptureRing;
    JackAlsaRingBuffer* fPlaybackRing;

    JackResamplerFactory* fFactory;
    JackAlsaRateConverter* fCaptureConverter;
    JackAlsaRateConverter* fPlaybackConverter;
    float** fCaptureConvIn;
    float** fPlaybackConvOut;
    std::uint32_t fConvCap;

    JackAlsaDriftController fDrift;
    std::atomic<std::uint32_t> fXrunGeneration;
    std::uint32_t fSeenXrunGeneration;

    // Counters for the realtime threads.  The audio callbacks only bump
    // these; the messages are emitted later from a non-realtime thread.
    std::atomic<std::uint64_t> fCaptureDropped;
    std::atomic<std::uint64_t> fPlaybackDropped;

    enum class IoError : std::int32_t {
        None = 0,
        Wait,
        Read,
        Write
    };
    std::atomic<IoError> fIoError;
    std::atomic<std::int32_t> fIoErrorStatus;

    snd_ctl_t* fCtl;
    std::string fCtlName;
    std::array<JackAlsaRateElement, 2> fRateElem;
    std::array<bool, 2> fRateValid;

    float** fCaptureTemp;
    float** fPlaybackTemp;
    float** fPortScratch;
    float* fSilence;

    std::vector<jack_port_id_t> fCapturePorts;
    std::vector<jack_port_id_t> fPlaybackPorts;

    void FreeBuffers();
    void FreeStreamObjects();
    std::int32_t OpenStream(jack_nframes_t requested_rate, jack_nframes_t primary_buffer_size,
        jack_nframes_t primary_rate);
    std::int32_t BuildBuffers(jack_nframes_t primary_buffer_size, jack_nframes_t primary_rate);
    bool ReopenAt(jack_nframes_t rate, std::int32_t capture_channels, std::int32_t playback_channels);
    bool WaitForRTCycles(std::uint32_t cycles);

    jack_nframes_t fPrimaryBufferSize;
    jack_nframes_t fPrimaryRate;
    std::atomic<bool> fSilent;
    const std::atomic<std::uint64_t>* fRTEpoch;
    const std::atomic<bool>* fRTRunning;
    std::int32_t fId;
    std::int32_t fDevice;
    JackAlsaCardFailureSink* fFailureSink;

    // Stops the card after an I/O error and tells the driver.
    void MarkFailed();
    // Non-realtime reporting of conditions collected by the RT threads.
    void ReportDrops();
    void ReportPendingIoError();
    void UpdateDrift(bool capture_side);
    void CloseControls();

    // ALSA core callbacks
    void ReadInput(const jackmini::alsa::MappedFrames& mapped, std::ptrdiff_t nread);
    void ClearOutput();
    void WriteOutput(const jackmini::alsa::MappedFrames& mapped, std::ptrdiff_t nwritten);
    void SetTime(jack_time_t time);
    std::int32_t Restart();

    static void ReadInputThunk(void* arg, jackmini::alsa::PcmCycle& cycle, const jackmini::alsa::MappedFrames& mapped,
        std::uint32_t orig_frames, std::ptrdiff_t nread);
    static void ClearOutputThunk(void* arg);
    static void WriteOutputThunk(void* arg, jackmini::alsa::PcmCycle& cycle,
        const jackmini::alsa::MappedFrames& mapped, std::uint32_t orig_frames,
        std::ptrdiff_t nwritten);
    static void SetTimeThunk(void* arg, jack_time_t time);
    static std::int32_t RestartThunk(void* arg);

    JackAlsaSecondaryCard(const JackAlsaSecondaryCard&);
    JackAlsaSecondaryCard& operator=(const JackAlsaSecondaryCard&);

public:
    JackAlsaSecondaryCard(const JackAlsaSecondaryConfig& config, JackResamplerFactory* factory);
    virtual ~JackAlsaSecondaryCard();

    std::int32_t Open(jack_nframes_t primary_buffer_size,
        jack_nframes_t primary_rate,
        jack_nframes_t primary_nperiods,
        bool realtime,
        std::int32_t priority);
    std::int32_t Close();

    std::int32_t Start();
    std::int32_t Stop();

    // JackRunnableInterface
    virtual bool Init();
    virtual bool Execute();

    // RT side, called from the primary driver thread.
    void ReadToPorts(JackGraphManager* graph_manager, jack_nframes_t nframes);
    void WriteFromPorts(JackGraphManager* graph_manager, jack_nframes_t nframes);

    const std::string& Name() const { return fConfig.name; }

    // The device number of this card's PCM, or -1 when it is not known.
    std::int32_t Device() const { return fDevice; }

    // The name used in port names: the card name with its device, e.g. "nvidia_d3".
    std::string PortBase() const;

    // Stable identifier chosen by the driver; used to route events to the card.
    std::int32_t Id() const { return fId; }
    void SetId(std::int32_t id) { fId = id; }
    void SetFailureSink(JackAlsaCardFailureSink* sink) { fFailureSink = sink; }
    const JackAlsaSecondaryConfig& Config() const { return fConfig; }

    bool CaptureEnabled() const { return fCaptureEnabled; }
    bool PlaybackEnabled() const { return fPlaybackEnabled; }
    std::int32_t CaptureChannels() const { return fCaptureChannels; }
    std::int32_t PlaybackChannels() const { return fPlaybackChannels; }
    jack_nframes_t Period() const { return fPeriod; }
    jack_nframes_t Rate() const { return fRate; }
    bool IsOpen() const { return fOpened; }
    bool IsRunning() const { return fRunning.load(); }
    // Clears resampler history and drift state.  Called by the driver after a primary xrun.
    void ResetResampling();

    // Gives the card the driver's RT cycle counter, used to know when the RT
    // thread has stopped touching the card during a reconfiguration.
    // The driver shares its RT cycle counter and running state.  While the RT
    // side runs, a reconfiguration waits for it to pass a safe point.
    void SetRTState(const std::atomic<std::uint64_t>* epoch, const std::atomic<bool>* running)
    {
        fRTEpoch = epoch;
        fRTRunning = running;
    }

    // Changes the stream rate: stops the card, reopens the ALSA stream and rebuilds
    // its buffers, converters and rings, then restarts it if it was running.  If the
    // new rate cannot be opened the previous rate is restored and -1 is returned.
    std::int32_t Reconfigure(jack_nframes_t rate);

    // Latency of the card's ports, in primary frames.  Counts the cycle, the
    // card's ALSA buffer and the ring and converter buffering on this side.
    jack_nframes_t CaptureLatency() const;
    jack_nframes_t PlaybackLatency() const;

    // Resolves or creates the rate elements for the enabled directions.
    std::int32_t OpenControls();

    const std::string& ControlName() const { return fCtlName; }
    bool RateElementValid(JackAlsaRateDirection dir) const { return fRateValid[dir]; }
    const JackAlsaRateElement& RateElement(JackAlsaRateDirection dir) const { return fRateElem[dir]; }
    std::int32_t ReadRate(JackAlsaRateDirection dir, std::int64_t* value);
    std::int32_t WriteRate(JackAlsaRateDirection dir, std::int64_t value);

    bool IsConverting() const { return fCaptureConverter != nullptr || fPlaybackConverter != nullptr; }

    std::vector<jack_port_id_t>& CapturePorts() { return fCapturePorts; }
    std::vector<jack_port_id_t>& PlaybackPorts() { return fPlaybackPorts; }
};

} // namespace jackmini

// Copyright (C) 2001 Paul Davis
// Copyright (C) 2004 Grame
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

#include "JackAlsaCardDiff.h"
#include "JackAlsaControlMonitor.h"
#include "JackAlsaEnumerate.h"
#include "JackAlsaHotplug.h"
#include "JackAlsaReconfigWorker.h"
#include "JackAlsaSecondaryCard.h"
#include "JackAlsaSecondaryConfig.h"
#include "JackAlsaSlotTable.h"
#include "JackAudioDriver.h"
#include "JackThreadedDriver.h"
#include "JackTime.h"
#include "pcm_session.hpp"

#include <cstdint>
#include <cstdint>
#include <atomic>
#include <mutex>
#include <vector>

namespace jackmini {

// \brief The ALSA driver.

// The cards the RT thread walks.  Eight is the limit for secondary cards.
typedef JackAlsaSlotTable<JackAlsaSecondaryCard, 8> JackAlsaCardSlots;

class JackAlsaDriver : public JackAudioDriver, public JackAlsaRateRequestHandler {

private:
    jackmini::alsa::Session fSession;
    jackmini::alsa::Request fRequest; // the open parameters; the rate is the one last set
    DitherAlgorithm fDither;
    std::string fCaptureDevice; // empty when the direction is not used
    std::string fPlaybackDevice;
    jack_nframes_t fCaptureFrameLatency;
    jack_nframes_t fPlaybackFrameLatency;
    jack_native_thread_t fReservationLoopThread;
    std::vector<JackAlsaSecondaryCard*> fSecondaryCards; // owned cards, guarded by fCardsMutex
    JackAlsaCardSlots fSlots; // the RT thread's view of promoted cards
    std::mutex fCardsMutex;
    std::int32_t fNextCardId;
    JackAlsaControlMonitor* fControlMonitor;
    JackAlsaHotplug* fHotplug;
    JackAlsaReconfigWorker* fReconfigWorker;
    std::vector<std::int32_t> fCardLocks;
    std::atomic<std::uint64_t> fRTEpoch;
    std::atomic<bool> fRTRunning;
    std::atomic<bool> fRestarting;
    std::vector<std::string> fCardLockNames;
    std::vector<JackAlsaSecondaryConfig> fConfiguredSecondaries; // as given to Open
    std::vector<std::string> fExcludedCards; // --exclude-card ids
    std::vector<JackAlsaPcmInfo> fKnownPcms; // the last device listing
    std::vector<std::string> fReportedFailures; // each logged once

    bool LockCard(const char* pcm_name, bool shared);
    void UnlockCards();

    void UpdateLatencies();
    void UpdateSecondaryLatencies();
    JackAlsaSecondaryCard* FindCard(std::int32_t id);
    bool PromoteCard(JackAlsaSecondaryCard* card);
    void UnregisterCardPorts(JackAlsaSecondaryCard* card);
    void RetireCard(JackAlsaSecondaryCard* card);
    bool WaitForRTSafePoint();
    void PrimaryFailed(std::int32_t status);
    std::atomic<bool> fPrimaryFailed;
    JackAlsaSecondaryCard* CreateCard(const JackAlsaSecondaryConfig& cfg, JackResamplerFactory* factory);
    bool AddCard(const JackAlsaSecondaryConfig& cfg);
    void ReportOnce(const std::string& message);

public:
    // Called on the RT thread when the ALSA core asks for a restart after an xrun.
    std::int32_t RestartFromRT();

private:
    void StopSecondaryCards();
    std::int32_t OpenPrimary(jack_nframes_t frames_per_cycle);
    void StartControlMonitoring();
    void StopControlMonitoring();

    // JackAlsaRateRequestHandler
    virtual void HandleRateRequest(std::int32_t card, JackAlsaRateDirection dir, std::int64_t value);
    virtual void HandleCardFailure(std::int32_t card);
    virtual void HandleSoundChanged();

public:
    JackAlsaDriver(const char* name, const char* alias, JackLockedEngine* engine, JackSynchro* table)
        : JackAudioDriver(name, alias, engine, table)
        , fDither(None)
        , fCaptureFrameLatency(0)
        , fPlaybackFrameLatency(0)
        , fControlMonitor(nullptr)
        , fReconfigWorker(nullptr)
        , fHotplug(nullptr)
    {
        fRTRunning.store(false);
        fRestarting.store(false);
        fPrimaryFailed.store(false);
        fNextCardId = 0;
    }
    virtual ~JackAlsaDriver()
    {
    }

    std::int32_t Open(jack_nframes_t buffer_size,
        jack_nframes_t user_nperiods,
        jack_nframes_t samplerate,
        bool hw_monitoring,
        bool hw_metering,
        bool capturing,
        bool playing,
        DitherAlgorithm dither,
        bool soft_mode,
        bool monitor,
        std::int32_t inchannels,
        std::int32_t outchannels,
        bool shorts_first,
        const char* capture_driver_name,
        const char* playback_driver_name,
        jack_nframes_t capture_latency,
        jack_nframes_t playback_latency,
        const std::vector<JackAlsaSecondaryConfig>& secondaries,
        const std::vector<std::string>& excluded_cards);

    std::int32_t Close();
    std::int32_t Attach();
    std::int32_t Detach();

    std::int32_t Start();
    std::int32_t Stop();

    std::int32_t Read();
    std::int32_t Write();

    // BufferSize can be changed
    bool IsFixedBufferSize()
    {
        return false;
    }

    std::int32_t SetBufferSize(jack_nframes_t buffer_size);

    void ReadInputAux(jack_nframes_t orig_nframes, const jackmini::alsa::MappedFrames& mapped, std::ptrdiff_t nread);
    void ClearOutputAux();
    void WriteOutputAux(jack_nframes_t orig_nframes, const jackmini::alsa::MappedFrames& mapped,
        std::ptrdiff_t nwritten);
    void SetTimetAux(jack_time_t time);
};

} // namespace jackmini

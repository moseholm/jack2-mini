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
typedef AlsaSlotTable<AlsaSecondaryCard, 8> AlsaCardSlots;

class AlsaDriver : public AudioDriver, public AlsaRateRequestHandler {

private:
    jackmini::alsa::Session m_session;
    jackmini::alsa::Request m_request; // the open parameters; the rate is the one last set
    DitherAlgorithm m_dither;
    std::string m_captureDevice; // empty when the direction is not used
    std::string m_playbackDevice;
    NframesC m_captureFrameLatency;
    NframesC m_playbackFrameLatency;
    NativeThreadC m_reservationLoopThread;
    std::vector<AlsaSecondaryCard*> m_secondaryCards; // owned cards, guarded by fCardsMutex
    AlsaCardSlots m_slots; // the RT thread's view of promoted cards
    std::mutex m_cardsMutex;
    std::int32_t m_nextCardId;
    AlsaControlMonitor* m_controlMonitor;
    AlsaHotplug* m_hotplug;
    AlsaReconfigWorker* m_reconfigWorker;
    std::vector<std::int32_t> m_cardLocks;
    std::atomic<std::uint64_t> m_rTEpoch;
    std::atomic<bool> m_rTRunning;
    std::atomic<bool> m_restarting;
    std::vector<std::string> m_cardLockNames;
    std::vector<AlsaSecondaryConfig> m_configuredSecondaries; // as given to Open
    std::vector<std::string> m_excludedCards; // --exclude-card ids
    std::vector<AlsaPcmInfo> m_knownPcms; // the last device listing
    std::vector<std::string> m_reportedFailures; // each logged once

    bool lockCard(const char* pcmName, bool shared);
    void unlockCards();

    void updateLatencies();
    void updateSecondaryLatencies();
    AlsaSecondaryCard* findCard(std::int32_t id);
    bool promoteCard(AlsaSecondaryCard* card);
    void unregisterCardPorts(AlsaSecondaryCard* card);
    void retireCard(AlsaSecondaryCard* card);
    bool waitForRTSafePoint();
    void primaryFailed(std::int32_t status);
    std::atomic<bool> m_primaryFailed;
    AlsaSecondaryCard* createCard(const AlsaSecondaryConfig& cfg, ResamplerFactory* factory);
    bool addCard(const AlsaSecondaryConfig& cfg);
    void reportOnce(const std::string& message);

public:
    // Called on the RT thread when the ALSA core asks for a restart after an xrun.
    std::int32_t restartFromRT();

private:
    void stopSecondaryCards();
    std::int32_t openPrimary(NframesC framesPerCycle);
    void startControlMonitoring();
    void stopControlMonitoring();

    // JackAlsaRateRequestHandler
    virtual void handleRateRequest(std::int32_t card, AlsaRateDirection dir, std::int64_t value);
    virtual void handleCardFailure(std::int32_t card);
    virtual void handleSoundChanged();

public:
    AlsaDriver(const char* name, const char* alias, LockedEngine* engine, Synchro* table)
        : AudioDriver(name, alias, engine, table)
        , m_dither(kNone)
        , m_captureFrameLatency(0)
        , m_playbackFrameLatency(0)
        , m_controlMonitor(nullptr)
        , m_reconfigWorker(nullptr)
        , m_hotplug(nullptr)
    {
        m_rTRunning.store(false);
        m_restarting.store(false);
        m_primaryFailed.store(false);
        m_nextCardId = 0;
    }
    virtual ~AlsaDriver()
    {
    }

    std::int32_t open(NframesC bufferSize,
        NframesC userNperiods,
        NframesC samplerate,
        bool hwMonitoring,
        bool hwMetering,
        bool capturing,
        bool playing,
        DitherAlgorithm dither,
        bool softMode,
        bool monitor,
        std::int32_t inchannels,
        std::int32_t outchannels,
        bool shortsFirst,
        const char* captureDriverName,
        const char* playbackDriverName,
        NframesC captureLatency,
        NframesC playbackLatency,
        const std::vector<AlsaSecondaryConfig>& secondaries,
        const std::vector<std::string>& excludedCards);

    std::int32_t close();
    std::int32_t attach();
    std::int32_t detach();

    std::int32_t start();
    std::int32_t stop();

    std::int32_t read();
    std::int32_t write();

    // BufferSize can be changed
    bool isFixedBufferSize()
    {
        return false;
    }

    std::int32_t setBufferSize(NframesC bufferSize);

    void readInputAux(NframesC origNframes, const jackmini::alsa::MappedFrames& mapped, std::ptrdiff_t nread);
    void clearOutputAux();
    void writeOutputAux(NframesC origNframes, const jackmini::alsa::MappedFrames& mapped,
        std::ptrdiff_t nwritten);
    void setTimetAux(TimeC time);
};

} // namespace jackmini

/*
Copyright (C) 2001 Paul Davis
Copyright (C) 2004 Grame

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

*/

#ifndef __JackAlsaDriver__
#define __JackAlsaDriver__

#include "JackAudioDriver.h"
#include "JackThreadedDriver.h"
#include "JackTime.h"
#include "JackAlsaReconfigWorker.h"
#include "JackAlsaControlMonitor.h"
#include "JackAlsaSecondaryCard.h"
#include "JackAlsaCardDiff.h"
#include "JackAlsaEnumerate.h"
#include "JackAlsaHotplug.h"
#include "JackAlsaSlotTable.h"
#include "JackAlsaSecondaryConfig.h"
#include "alsa_driver.h"

#include <atomic>
#include <mutex>
#include <stdint.h>
#include <vector>

namespace Jack
{

/*!
\brief The ALSA driver.
*/

// The cards the RT thread walks.  Eight is the limit for secondary cards.
typedef JackAlsaSlotTable<JackAlsaSecondaryCard, 8> JackAlsaCardSlots;

class JackAlsaDriver : public JackAudioDriver, public JackAlsaRateRequestHandler
{

    private:

        jack_driver_t* fDriver;
        jack_native_thread_t fReservationLoopThread;
        std::vector<JackAlsaSecondaryCard*> fSecondaryCards;   // owned cards, guarded by fCardsMutex
        JackAlsaCardSlots fSlots;                              // the RT thread's view of promoted cards
        std::mutex fCardsMutex;
        int fNextCardId;
        JackAlsaControlMonitor* fControlMonitor;
        JackAlsaHotplug* fHotplug;
        JackAlsaReconfigWorker* fReconfigWorker;
        std::vector<int> fCardLocks;
        std::atomic<uint64_t> fRTEpoch;
        std::atomic<bool> fRTRunning;
        std::atomic<bool> fRestarting;
        std::vector<std::string> fCardLockNames;
        std::vector<JackAlsaSecondaryConfig> fConfiguredSecondaries;   // as given to Open
        std::vector<std::string> fExcludedCards;                       // --exclude-card ids
        std::vector<JackAlsaPcmInfo> fKnownPcms;                       // the last device listing
        std::vector<std::string> fReportedFailures;                    // each logged once

        bool LockCard(const char* pcm_name, bool shared);
        void UnlockCards();

        void UpdateLatencies();
        void UpdateSecondaryLatencies();
        JackAlsaSecondaryCard* FindCard(int id);
        bool PromoteCard(JackAlsaSecondaryCard* card);
        void UnregisterCardPorts(JackAlsaSecondaryCard* card);
        void RetireCard(JackAlsaSecondaryCard* card);
        bool WaitForRTSafePoint();
        JackAlsaSecondaryCard* CreateCard(const JackAlsaSecondaryConfig& cfg, JackResamplerFactory* factory,
                                          alsa_driver_t* primary);
        bool AddCard(const JackAlsaSecondaryConfig& cfg, alsa_driver_t* primary);
        void ReportOnce(const std::string& message);

    public:
        // Called on the RT thread when the ALSA core asks for a restart after an xrun.
        int RestartFromRT();

    private:
        void StopSecondaryCards();
        void StartControlMonitoring();
        void StopControlMonitoring();

        // JackAlsaRateRequestHandler
        virtual void HandleRateRequest(int card, JackAlsaRateDirection dir, long value);
        virtual void HandleCardFailure(int card);
        virtual void HandleSoundChanged();

    public:

        JackAlsaDriver(const char* name, const char* alias, JackLockedEngine* engine, JackSynchro* table)
		: JackAudioDriver(name, alias, engine, table),fDriver(NULL),fControlMonitor(NULL),fReconfigWorker(NULL),fHotplug(NULL)
        {
            fRTRunning.store(false);
            fRestarting.store(false);
            fNextCardId = 0;
        }
        virtual ~JackAlsaDriver()
        {}

        int Open(jack_nframes_t buffer_size,
                 jack_nframes_t user_nperiods,
                 jack_nframes_t samplerate,
                 bool hw_monitoring,
                 bool hw_metering,
                 bool capturing,
                 bool playing,
                 DitherAlgorithm dither,
                 bool soft_mode,
                 bool monitor,
                 int inchannels,
                 int outchannels,
                 bool shorts_first,
                 const char* capture_driver_name,
                 const char* playback_driver_name,
                 jack_nframes_t capture_latency,
                 jack_nframes_t playback_latency,
                 const char* midi_driver_name,
                 const std::vector<JackAlsaSecondaryConfig>& secondaries,
                 const std::vector<std::string>& excluded_cards);

        int Close();
        int Attach();
        int Detach();

        int Start();
        int Stop();

        int Read();
        int Write();

        // BufferSize can be changed
        bool IsFixedBufferSize()
        {
            return false;
        }

        int SetBufferSize(jack_nframes_t buffer_size);

        void ReadInputAux(jack_nframes_t orig_nframes, snd_pcm_sframes_t contiguous, snd_pcm_sframes_t nread);
        void MonitorInputAux();
        void ClearOutputAux();
        void WriteOutputAux(jack_nframes_t orig_nframes, snd_pcm_sframes_t contiguous, snd_pcm_sframes_t nwritten);
        void SetTimetAux(jack_time_t time);

        int PortSetDefaultMetadata(jack_port_id_t port_id, const char* pretty_name);

        // JACK API emulation for the midi driver
        int is_realtime() const;
        int create_thread(pthread_t *thread, int prio, int rt, void *(*start_func)(void*), void *arg);

        jack_port_id_t port_register(const char *port_name, const char *port_type, unsigned long flags, unsigned long buffer_size);
        int port_unregister(jack_port_id_t port_index);
        void* port_get_buffer(int port, jack_nframes_t nframes);
        int port_set_alias(int port, const char* name);

        jack_nframes_t get_sample_rate() const;
        jack_nframes_t frame_time() const;
        jack_nframes_t last_frame_time() const;
};

} // end of namespace

#endif

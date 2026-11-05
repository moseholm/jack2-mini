/*
Copyright (C) 2026 JACK contributors

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

#ifndef __JackAlsaSecondaryCard__
#define __JackAlsaSecondaryCard__

#include "JackAlsaDriftController.h"
#include "JackAlsaRateConverter.h"
#include "JackAlsaRingBuffer.h"
#include "JackAlsaSecondaryConfig.h"
#include "resample/JackResampler.h"
#include "JackPlatformPlug.h"
#include "JackThread.h"
#include "types.h"

#include <alsa/asoundlib.h>
#include <atomic>
#include <string>
#include <vector>

struct _alsa_driver;
typedef struct _alsa_driver alsa_driver_t;

namespace Jack
{

class JackGraphManager;

/*!
\brief One secondary ALSA card.

The card owns its ALSA stream, its I/O thread and the lock-free rings that
connect it to the primary RT thread.  The primary thread only touches the
rings and the JACK port buffers; all ALSA calls happen on the card's own
thread.
*/

class JackAlsaSecondaryCard : public JackRunnableInterface
{

    private:

        JackAlsaSecondaryConfig fConfig;
        alsa_driver_t* fAlsaDriver;
        JackThread fThread;
        std::atomic<bool> fRunning;

        bool fOpened;
        bool fRealTime;
        int fPriority;
        bool fCaptureEnabled;
        bool fPlaybackEnabled;

        jack_nframes_t fPeriod;
        jack_nframes_t fNperiods;
        jack_nframes_t fRate;
        int fCaptureChannels;
        int fPlaybackChannels;

        JackAlsaRingBuffer* fCaptureRing;
        JackAlsaRingBuffer* fPlaybackRing;

        JackResamplerFactory* fFactory;
        JackAlsaRateConverter* fCaptureConverter;
        JackAlsaRateConverter* fPlaybackConverter;
        float** fCaptureConvIn;
        float** fPlaybackConvOut;
        unsigned int fConvCap;

        JackAlsaDriftController fDrift;
        std::atomic<unsigned int> fXrunGeneration;
        unsigned int fSeenXrunGeneration;

        float** fCaptureTemp;
        float** fPlaybackTemp;
        float** fPortScratch;
        float* fSilence;

        std::vector<jack_port_id_t> fCapturePorts;
        std::vector<jack_port_id_t> fPlaybackPorts;

        void FreeBuffers();
        void UpdateDrift(bool capture_side);

        // ALSA core callbacks
        void ReadInput(jack_nframes_t orig_nframes, snd_pcm_sframes_t contiguous, snd_pcm_sframes_t nread);
        void MonitorInput();
        void ClearOutput();
        void WriteOutput(jack_nframes_t orig_nframes, snd_pcm_sframes_t contiguous, snd_pcm_sframes_t nwritten);
        void SetTime(jack_time_t time);
        int Restart();

        static void ReadInputThunk(void* arg, jack_nframes_t orig_nframes,
                                   snd_pcm_sframes_t contiguous, snd_pcm_sframes_t nread);
        static void MonitorInputThunk(void* arg);
        static void ClearOutputThunk(void* arg);
        static void WriteOutputThunk(void* arg, jack_nframes_t orig_nframes,
                                    snd_pcm_sframes_t contiguous, snd_pcm_sframes_t nwritten);
        static void SetTimeThunk(void* arg, jack_time_t time);
        static int RestartThunk(void* arg);

        JackAlsaSecondaryCard(const JackAlsaSecondaryCard&);
        JackAlsaSecondaryCard& operator=(const JackAlsaSecondaryCard&);

    public:

        JackAlsaSecondaryCard(const JackAlsaSecondaryConfig& config, JackResamplerFactory* factory);
        virtual ~JackAlsaSecondaryCard();

        int Open(jack_nframes_t primary_buffer_size,
                 jack_nframes_t primary_rate,
                 jack_nframes_t primary_nperiods,
                 bool realtime,
                 int priority);
        int Close();

        int Start();
        int Stop();

        // JackRunnableInterface
        virtual bool Init();
        virtual bool Execute();

        // RT side, called from the primary driver thread.
        void ReadToPorts(JackGraphManager* graph_manager, jack_nframes_t nframes);
        void WriteFromPorts(JackGraphManager* graph_manager, jack_nframes_t nframes);

        const std::string& Name() const { return fConfig.name; }
        const JackAlsaSecondaryConfig& Config() const { return fConfig; }

        bool CaptureEnabled() const { return fCaptureEnabled; }
        bool PlaybackEnabled() const { return fPlaybackEnabled; }
        int CaptureChannels() const { return fCaptureChannels; }
        int PlaybackChannels() const { return fPlaybackChannels; }
        jack_nframes_t Period() const { return fPeriod; }
        jack_nframes_t Rate() const { return fRate; }
        bool IsOpen() const { return fOpened; }
        bool IsRunning() const { return fRunning.load(); }
        // Clears resampler history and drift state.  Called by the driver after a primary xrun.
        void ResetResampling();

        bool IsConverting() const { return fCaptureConverter != NULL || fPlaybackConverter != NULL; }

        std::vector<jack_port_id_t>& CapturePorts() { return fCapturePorts; }
        std::vector<jack_port_id_t>& PlaybackPorts() { return fPlaybackPorts; }
};

} // end of namespace

#endif

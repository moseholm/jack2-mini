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

#include "JackAlsaSecondaryCard.h"
#include "JackAlsaSecondaryConfig.h"
#include "JackError.h"
#include "JackGraphManager.h"
#include "alsa_driver.h"

#include <algorithm>
#include <math.h>
#include <string.h>

namespace Jack
{

JackAlsaSecondaryCard::JackAlsaSecondaryCard(const JackAlsaSecondaryConfig& config)
    : fConfig(config),
      fAlsaDriver(NULL),
      fThread(this),
      fRunning(false),
      fOpened(false),
      fRealTime(false),
      fPriority(0),
      fCaptureEnabled(false),
      fPlaybackEnabled(false),
      fPeriod(0),
      fNperiods(0),
      fRate(0),
      fCaptureChannels(0),
      fPlaybackChannels(0),
      fCaptureRing(NULL),
      fPlaybackRing(NULL),
      fCaptureTemp(NULL),
      fPlaybackTemp(NULL),
      fPortScratch(NULL),
      fSilence(NULL)
{}

JackAlsaSecondaryCard::~JackAlsaSecondaryCard()
{
    Close();
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

    fCapturePorts.clear();
    fPlaybackPorts.clear();
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

    jack_nframes_t requested_rate = (fConfig.rate > 0) ? fConfig.rate : primary_rate;
    fNperiods = (fConfig.nperiods > 0) ? fConfig.nperiods : primary_nperiods;

    if (primary_buffer_size == 0 || primary_rate == 0) {
        jack_error("secondary card '%s': invalid primary timing", fConfig.name.c_str());
        return -1;
    }

    double period = ((double)primary_buffer_size * (double)requested_rate) / (double)primary_rate;
    fPeriod = (jack_nframes_t)llround(period);
    if (fPeriod < 1) {
        fPeriod = 1;
    }

    const std::string& playback_device = fConfig.playback_device.empty()
                                       ? fConfig.capture_device
                                       : fConfig.playback_device;
    const std::string& capture_device = fConfig.capture_device.empty()
                                      ? fConfig.playback_device
                                      : fConfig.capture_device;

    jack_info("secondary card '%s': device %s, mode %s, rate %u, period %u, %u periods",
              fConfig.name.c_str(),
              (playback_device + "|" + capture_device).c_str(),
              (fCaptureEnabled && fPlaybackEnabled) ? "duplex" :
                  (fCaptureEnabled ? "capture" : "playback"),
              requested_rate, fPeriod, fNperiods);

    fAlsaDriver = (alsa_driver_t*)alsa_driver_new((char*)"alsa_secondary",
                                                  (char*)playback_device.c_str(),
                                                  (char*)capture_device.c_str(),
                                                  NULL,
                                                  fPeriod,
                                                  fNperiods,
                                                  requested_rate,
                                                  FALSE,
                                                  FALSE,
                                                  fCaptureEnabled,
                                                  fPlaybackEnabled,
                                                  None,
                                                  FALSE,
                                                  FALSE,
                                                  fCaptureEnabled ? fConfig.capture_channels : 0,
                                                  fPlaybackEnabled ? fConfig.playback_channels : 0,
                                                  FALSE,
                                                  0,
                                                  0,
                                                  NULL,
                                                  1);
    if (!fAlsaDriver) {
        jack_error("secondary card '%s': cannot open ALSA device %s",
                   fConfig.name.c_str(), (playback_device + "|" + capture_device).c_str());
        return -1;
    }

    fCaptureChannels = fCaptureEnabled ? fAlsaDriver->capture_nchannels : 0;
    fPlaybackChannels = fPlaybackEnabled ? fAlsaDriver->playback_nchannels : 0;
    fPeriod = fAlsaDriver->frames_per_cycle;
    fRate = fAlsaDriver->frame_rate;

    if (fRate != primary_rate) {
        jack_error("secondary card '%s': actual rate %u differs from primary rate %u; "
                   "resampling is not available in this build",
                   fConfig.name.c_str(), fRate, primary_rate);
        alsa_driver_delete(fAlsaDriver);
        fAlsaDriver = NULL;
        return -1;
    }

    if ((fCaptureEnabled && fCaptureChannels == 0) ||
        (fPlaybackEnabled && fPlaybackChannels == 0)) {
        jack_error("secondary card '%s': no usable channels", fConfig.name.c_str());
        alsa_driver_delete(fAlsaDriver);
        fAlsaDriver = NULL;
        return -1;
    }

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

    alsa_driver_callbacks_t callbacks;
    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.arg = this;
    callbacks.read_input = ReadInputThunk;
    callbacks.monitor_input = MonitorInputThunk;
    callbacks.clear_output = ClearOutputThunk;
    callbacks.write_output = WriteOutputThunk;
    callbacks.set_time = SetTimeThunk;
    callbacks.restart = RestartThunk;
    alsa_driver_set_callbacks(fAlsaDriver, &callbacks);

    fOpened = true;
    return 0;
}

int JackAlsaSecondaryCard::Close()
{
    if (!fOpened && !fAlsaDriver) {
        return 0;
    }

    Stop();

    if (fAlsaDriver) {
        alsa_driver_delete(fAlsaDriver);
        fAlsaDriver = NULL;
    }

    FreeBuffers();
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

    if (alsa_driver_start(fAlsaDriver) < 0) {
        jack_error("secondary card '%s': cannot start ALSA stream", fConfig.name.c_str());
        return -1;
    }

    fRunning.store(true);
    if (fThread.StartSync() < 0) {
        jack_error("secondary card '%s': cannot start I/O thread", fConfig.name.c_str());
        fRunning.store(false);
        alsa_driver_stop(fAlsaDriver);
        return -1;
    }

    return 0;
}

int JackAlsaSecondaryCard::Stop()
{
    if (!fAlsaDriver) {
        return 0;
    }

    fRunning.store(false);

    // The thread wakes up at the next poll timeout.  It only touches the
    // card's own ALSA stream and rings, so joining here is safe.
    fThread.Stop();

    alsa_driver_stop(fAlsaDriver);
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

    jack_nframes_t nframes = alsa_driver_wait(fAlsaDriver, -1, &wait_status, &delayed_usecs);

    if (!fRunning.load()) {
        return false;
    }

    if (wait_status < 0) {
        jack_error("secondary card '%s': ALSA wait failed (status %d)",
                   fConfig.name.c_str(), wait_status);
        fRunning.store(false);
        return false;
    }

    if (nframes == 0) {
        // An xrun was detected and recovered.
        return true;
    }

    if (fCaptureEnabled) {
        if (alsa_driver_read(fAlsaDriver, fPeriod) < 0) {
            jack_error("secondary card '%s': ALSA read failed", fConfig.name.c_str());
            fRunning.store(false);
            return false;
        }
        jack_nframes_t written = fCaptureRing->Write((const float* const*)fCaptureTemp, fPeriod);
        if (written != fPeriod) {
            jack_log("secondary card '%s': capture ring overflow, dropped %u frames",
                     fConfig.name.c_str(), fPeriod - written);
        }
    }

    if (fPlaybackEnabled) {
        fPlaybackRing->Read(fPlaybackTemp, fPeriod);
        if (alsa_driver_write(fAlsaDriver, fPeriod) < 0) {
            jack_error("secondary card '%s': ALSA write failed", fConfig.name.c_str());
            fRunning.store(false);
            return false;
        }
    }

    return fRunning.load();
}

void JackAlsaSecondaryCard::ReadToPorts(JackGraphManager* graph_manager, jack_nframes_t nframes)
{
    if (!fCaptureEnabled || !fCaptureRing ||
        fCapturePorts.size() < (size_t)fCaptureChannels) {
        return;
    }

    for (int i = 0; i < fCaptureChannels; i++) {
        fPortScratch[i] = (float*)graph_manager->GetBuffer(fCapturePorts[i], nframes);
        if (!fPortScratch[i]) {
            fPortScratch[i] = fSilence;
        }
    }

    fCaptureRing->Read(fPortScratch, nframes);
}

void JackAlsaSecondaryCard::WriteFromPorts(JackGraphManager* graph_manager, jack_nframes_t nframes)
{
    if (!fPlaybackEnabled || !fPlaybackRing ||
        fPlaybackPorts.size() < (size_t)fPlaybackChannels) {
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

    jack_nframes_t written = fPlaybackRing->Write((const float* const*)fPortScratch, nframes);
    if (written != nframes) {
        jack_log("secondary card '%s': playback ring overflow, dropped %u frames",
                 fConfig.name.c_str(), nframes - written);
    }
}

void JackAlsaSecondaryCard::ReadInput(jack_nframes_t orig_nframes,
                                      snd_pcm_sframes_t contiguous,
                                      snd_pcm_sframes_t nread)
{
    for (int i = 0; i < fCaptureChannels; i++) {
        alsa_driver_read_from_channel(fAlsaDriver, i, fCaptureTemp[i] + nread, contiguous);
    }
}

void JackAlsaSecondaryCard::MonitorInput()
{
    // Secondary cards have no monitor ports.
}

void JackAlsaSecondaryCard::ClearOutput()
{
    for (int i = 0; i < fPlaybackChannels; i++) {
        memset(fPlaybackTemp[i], 0, sizeof(float) * fPeriod);
    }
}

void JackAlsaSecondaryCard::WriteOutput(jack_nframes_t orig_nframes,
                                        snd_pcm_sframes_t contiguous,
                                        snd_pcm_sframes_t nwritten)
{
    for (int i = 0; i < fPlaybackChannels; i++) {
        alsa_driver_write_to_channel(fAlsaDriver, i, fPlaybackTemp[i] + nwritten, contiguous);
    }
}

void JackAlsaSecondaryCard::SetTime(jack_time_t time)
{
    // Timing comes from the primary card.
}

int JackAlsaSecondaryCard::Restart()
{
    int res = alsa_driver_stop(fAlsaDriver);
    if (res < 0) {
        return res;
    }
    return alsa_driver_start(fAlsaDriver);
}

void JackAlsaSecondaryCard::ReadInputThunk(void* arg, jack_nframes_t orig_nframes,
                                           snd_pcm_sframes_t contiguous, snd_pcm_sframes_t nread)
{
    static_cast<JackAlsaSecondaryCard*>(arg)->ReadInput(orig_nframes, contiguous, nread);
}

void JackAlsaSecondaryCard::MonitorInputThunk(void* arg)
{
    static_cast<JackAlsaSecondaryCard*>(arg)->MonitorInput();
}

void JackAlsaSecondaryCard::ClearOutputThunk(void* arg)
{
    static_cast<JackAlsaSecondaryCard*>(arg)->ClearOutput();
}

void JackAlsaSecondaryCard::WriteOutputThunk(void* arg, jack_nframes_t orig_nframes,
                                             snd_pcm_sframes_t contiguous, snd_pcm_sframes_t nwritten)
{
    static_cast<JackAlsaSecondaryCard*>(arg)->WriteOutput(orig_nframes, contiguous, nwritten);
}

void JackAlsaSecondaryCard::SetTimeThunk(void* arg, jack_time_t time)
{
    static_cast<JackAlsaSecondaryCard*>(arg)->SetTime(time);
}

int JackAlsaSecondaryCard::RestartThunk(void* arg)
{
    return static_cast<JackAlsaSecondaryCard*>(arg)->Restart();
}

} // end of namespace

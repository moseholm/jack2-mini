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

#define __STDC_FORMAT_MACROS   // For inttypes.h to work in C++

#include <iostream>
#include <math.h>
#include <stdio.h>
#include <memory.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <stdarg.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/time.h>
#include <string.h>
#include <algorithm>
#include <chrono>
#include <thread>
#include <utility>

#include "JackAlsaDriver.h"
#include "JackAlsaEnumerate.h"
#include "JackEngineControl.h"
#include "JackClientControl.h"
#include "JackPort.h"
#include "JackGraphManager.h"
#include "JackLockedEngine.h"
#include "JackPosixThread.h"
#include "JackCompilerDeps.h"
#include "JackServerGlobals.h"

static struct jack_constraint_enum_str_descriptor midi_constraint_descr_array[] =
{
    { "none", "no MIDI driver" },
    { "seq", "ALSA Sequencer driver" },
    { "raw", "ALSA RawMIDI driver" },
    { 0 }
};

static struct jack_constraint_enum_char_descriptor dither_constraint_descr_array[] =
{
    { 'n', "none" },
    { 'r', "rectangular" },
    { 's', "shaped" },
    { 't', "triangular" },
    { 0 }
};

namespace Jack
{

static volatile bool device_reservation_loop_running = false;

static void* on_device_reservation_loop(void*)
{
    while (device_reservation_loop_running && JackServerGlobals::on_device_reservation_loop != NULL) {
        JackServerGlobals::on_device_reservation_loop();
        usleep(50*1000);
    }

    return NULL;
}

/*
 * Per-instance thunks installed in the ALSA core.  The core used to call
 * global C functions that reached a single global JackAlsaDriver; routing the
 * calls through the callback table lets each ALSA core instance (primary and
 * secondary cards) address its own driver object.
 */

static void AlsaReadInputThunk(void* arg, jack_nframes_t orig_nframes,
                               snd_pcm_sframes_t contiguous, snd_pcm_sframes_t nread)
{
    static_cast<JackAlsaDriver*>(arg)->ReadInputAux(orig_nframes, contiguous, nread);
}

static void AlsaMonitorInputThunk(void* arg)
{
    static_cast<JackAlsaDriver*>(arg)->MonitorInputAux();
}

static void AlsaClearOutputThunk(void* arg)
{
    static_cast<JackAlsaDriver*>(arg)->ClearOutputAux();
}

static void AlsaWriteOutputThunk(void* arg, jack_nframes_t orig_nframes,
                                 snd_pcm_sframes_t contiguous, snd_pcm_sframes_t nwritten)
{
    static_cast<JackAlsaDriver*>(arg)->WriteOutputAux(orig_nframes, contiguous, nwritten);
}

static void AlsaSetTimeThunk(void* arg, jack_time_t time)
{
    static_cast<JackAlsaDriver*>(arg)->SetTimetAux(time);
}

static int AlsaRestartThunk(void* arg)
{
    return static_cast<JackAlsaDriver*>(arg)->RestartFromRT();
}

int JackAlsaDriver::RestartFromRT()
{
    // Stop and Start run inside one RT cycle here.  The RT side stays marked as
    // running throughout, so a reconfiguration waits for the cycle to finish
    // rather than freeing buffers mid-restart.
    fRestarting.store(true);
    int res = Stop();
    if (res == 0) {
        res = Start();
    }
    fRestarting.store(false);
    return res;
}

static void InstallAlsaCallbacks(JackAlsaDriver* driver, alsa_driver_t* alsa_driver)
{
    alsa_driver_callbacks_t callbacks;

    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.arg = driver;
    callbacks.read_input = AlsaReadInputThunk;
    callbacks.monitor_input = AlsaMonitorInputThunk;
    callbacks.clear_output = AlsaClearOutputThunk;
    callbacks.write_output = AlsaWriteOutputThunk;
    callbacks.set_time = AlsaSetTimeThunk;
    callbacks.restart = AlsaRestartThunk;

    alsa_driver_set_callbacks(alsa_driver, &callbacks);
}

int JackAlsaDriver::SetBufferSize(jack_nframes_t buffer_size)
{
    jack_log("JackAlsaDriver::SetBufferSize %ld", buffer_size);

    // Secondary card periods are derived from the primary buffer size, so it
    // cannot change while secondary cards exist.
    if (!fSecondaryCards.empty()) {
        jack_error("buffer size cannot change while secondary ALSA cards are in use");
        return -1;
    }
    int res = alsa_driver_reset_parameters((alsa_driver_t *)fDriver, buffer_size,
                                           ((alsa_driver_t *)fDriver)->user_nperiods,
                                           ((alsa_driver_t *)fDriver)->frame_rate);

    if (res == 0) { // update fEngineControl and fGraphManager
        JackAudioDriver::SetBufferSize(buffer_size);  // Generic change, never fails
        // ALSA specific
        UpdateLatencies();
    } else {
        // Restore old values
        alsa_driver_reset_parameters((alsa_driver_t *)fDriver, fEngineControl->fBufferSize,
                                     ((alsa_driver_t *)fDriver)->user_nperiods,
                                     ((alsa_driver_t *)fDriver)->frame_rate);
    }

    return res;
}

JackAlsaSecondaryCard* JackAlsaDriver::FindCard(int id)
{
    std::lock_guard<std::mutex> lock(fCardsMutex);
    for (size_t i = 0; i < fSecondaryCards.size(); i++) {
        if (fSecondaryCards[i]->Id() == id) {
            return fSecondaryCards[i];
        }
    }
    return NULL;
}

// Registers a card's ports and puts it in a slot.  Called on the server thread
// at Attach, before the RT thread starts.
bool JackAlsaDriver::PromoteCard(JackAlsaSecondaryCard* card)
{
    char name[REAL_JACK_PORT_NAME_SIZE+1];
    jack_port_id_t port_index;
    std::vector<jack_port_id_t>& capture_ports = card->CapturePorts();
    std::vector<jack_port_id_t>& playback_ports = card->PlaybackPorts();

    for (int i = 0; i < card->CaptureChannels(); i++) {
        snprintf(name, sizeof(name), "%s:capture_%s_%d", fClientControl.fName, card->PortBase().c_str(), i + 1);
        if (fEngine->PortRegister(fClientControl.fRefNum, name, JACK_DEFAULT_AUDIO_TYPE, (JackPortFlags)CaptureDriverFlags, fEngineControl->fBufferSize, &port_index) < 0) {
            jack_error("driver: cannot register port for %s", name);
            UnregisterCardPorts(card);
            return false;
        }
        capture_ports[i] = port_index;
    }

    for (int i = 0; i < card->PlaybackChannels(); i++) {
        snprintf(name, sizeof(name), "%s:playback_%s_%d", fClientControl.fName, card->PortBase().c_str(), i + 1);
        if (fEngine->PortRegister(fClientControl.fRefNum, name, JACK_DEFAULT_AUDIO_TYPE, (JackPortFlags)PlaybackDriverFlags, fEngineControl->fBufferSize, &port_index) < 0) {
            jack_error("driver: cannot register port for %s", name);
            UnregisterCardPorts(card);
            return false;
        }
        playback_ports[i] = port_index;
    }

    if (fSlots.Assign(card) < 0) {
        jack_error("secondary card '%s' cannot be used: all %zu secondary card slots are in use",
                   card->Name().c_str(), JackAlsaCardSlots::Capacity());
        UnregisterCardPorts(card);
        return false;
    }
    return true;
}

void JackAlsaDriver::UnregisterCardPorts(JackAlsaSecondaryCard* card)
{
    std::vector<jack_port_id_t>& capture_ports = card->CapturePorts();
    std::vector<jack_port_id_t>& playback_ports = card->PlaybackPorts();
    for (size_t i = 0; i < capture_ports.size(); i++) {
        if (capture_ports[i]) {
            fEngine->PortUnRegister(fClientControl.fRefNum, capture_ports[i]);
            capture_ports[i] = 0;
        }
    }
    for (size_t i = 0; i < playback_ports.size(); i++) {
        if (playback_ports[i]) {
            fEngine->PortUnRegister(fClientControl.fRefNum, playback_ports[i]);
            playback_ports[i] = 0;
        }
    }
}

// Waits until the RT thread has finished two cycles, so it can no longer hold
// a reference to a card taken out of its slot.  Returns false on timeout.
bool JackAlsaDriver::WaitForRTSafePoint()
{
    if (!fRTRunning.load()) {
        return true;
    }
    uint64_t start = fRTEpoch.load(std::memory_order_acquire);
    for (int i = 0; i < 2000; i++) {
        if (fRTEpoch.load(std::memory_order_acquire) >= start + 2) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

// Takes a card out of service and frees it.  Runs on the worker thread.  The
// card leaves the RT slots first; only then is the RT thread waited out, so
// no cycle still uses the card when its ports are removed and it is freed.
void JackAlsaDriver::RetireCard(JackAlsaSecondaryCard* card)
{
    {
        std::lock_guard<std::mutex> lock(fCardsMutex);
        for (size_t i = 0; i < fSecondaryCards.size(); i++) {
            if (fSecondaryCards[i] == card) {
                fSecondaryCards.erase(fSecondaryCards.begin() + i);
                break;
            }
        }
        fSlots.Remove(card);
    }

    if (!WaitForRTSafePoint()) {
        // The RT thread did not reach a safe point.  Leak the card rather than free
        // memory the RT thread may still use.
        jack_error("secondary card '%s' could not be removed safely; it is left unused",
                   card->Name().c_str());
        return;
    }

    if (fControlMonitor != NULL) {
        fControlMonitor->RemoveWatches(card->Id());
    }
    UnregisterCardPorts(card);
    delete card;   // stops the card's thread, closes its controls and stream
    UpdateSecondaryLatencies();
}

void JackAlsaDriver::HandleCardFailure(int card_id)
{
    JackAlsaSecondaryCard* card = FindCard(card_id);
    if (card == NULL) {
        return;
    }
    jack_error("secondary card '%s' failed; it has been removed and its ports are gone",
               card->Name().c_str());
    RetireCard(card);
}

void JackAlsaDriver::UpdateSecondaryLatencies()
{
    std::lock_guard<std::mutex> lock(fCardsMutex);
    for (size_t c = 0; c < fSecondaryCards.size(); c++) {
        JackAlsaSecondaryCard* card = fSecondaryCards[c];
        jack_latency_range_t range;

        range.min = range.max = card->CaptureLatency();
        for (int i = 0; i < card->CaptureChannels(); i++) {
            fGraphManager->GetPort(card->CapturePorts()[i])->SetLatencyRange(JackCaptureLatency, &range);
        }

        range.min = range.max = card->PlaybackLatency();
        for (int i = 0; i < card->PlaybackChannels(); i++) {
            fGraphManager->GetPort(card->PlaybackPorts()[i])->SetLatencyRange(JackPlaybackLatency, &range);
        }
    }
}

void JackAlsaDriver::UpdateLatencies()
{
    jack_latency_range_t range;
    alsa_driver_t* alsa_driver = (alsa_driver_t*)fDriver;

    for (int i = 0; i < fCaptureChannels; i++) {
        range.min = range.max = alsa_driver->frames_per_cycle + alsa_driver->capture_frame_latency;
        fGraphManager->GetPort(fCapturePortList[i])->SetLatencyRange(JackCaptureLatency, &range);
    }

    for (int i = 0; i < fPlaybackChannels; i++) {
        // Add one buffer more latency if "async" mode is used...
        range.min = range.max = (alsa_driver->frames_per_cycle * (alsa_driver->user_nperiods - 1)) +
                         ((fEngineControl->fSyncMode) ? 0 : fEngineControl->fBufferSize) + alsa_driver->playback_frame_latency;
        fGraphManager->GetPort(fPlaybackPortList[i])->SetLatencyRange(JackPlaybackLatency, &range);
        // Monitor port
        if (fWithMonitorPorts) {
            range.min = range.max = alsa_driver->frames_per_cycle;
            fGraphManager->GetPort(fMonitorPortList[i])->SetLatencyRange(JackCaptureLatency, &range);
        }
    }
}

int JackAlsaDriver::Attach()
{
    JackPort* port;
    jack_port_id_t port_index;
    unsigned long port_flags = (unsigned long)CaptureDriverFlags;
    char name[REAL_JACK_PORT_NAME_SIZE+1];
    char alias[REAL_JACK_PORT_NAME_SIZE+1];

    assert(fCaptureChannels < DRIVER_PORT_NUM);
    assert(fPlaybackChannels < DRIVER_PORT_NUM);

    alsa_driver_t* alsa_driver = (alsa_driver_t*)fDriver;

    if (alsa_driver->has_hw_monitoring)
        port_flags |= JackPortCanMonitor;

    // ALSA driver may have changed the values
    JackAudioDriver::SetBufferSize(alsa_driver->frames_per_cycle);
    JackAudioDriver::SetSampleRate(alsa_driver->frame_rate);

    jack_log("JackAlsaDriver::Attach fBufferSize %ld fSampleRate %ld", fEngineControl->fBufferSize, fEngineControl->fSampleRate);

    for (int i = 0; i < fCaptureChannels; i++) {
        snprintf(alias, sizeof(alias), "%s:%s:out%d", fAliasName, fCaptureDriverName, i + 1);
        snprintf(name, sizeof(name), "%s:capture_%d", fClientControl.fName, i + 1);
        if (fEngine->PortRegister(fClientControl.fRefNum, name, JACK_DEFAULT_AUDIO_TYPE, (JackPortFlags)port_flags, fEngineControl->fBufferSize, &port_index) < 0) {
            jack_error("driver: cannot register port for %s", name);
            return -1;
        }
        port = fGraphManager->GetPort(port_index);
        port->SetAlias(alias);
        fCapturePortList[i] = port_index;
        jack_log("JackAlsaDriver::Attach fCapturePortList[i] %ld ", port_index);
    }

    port_flags = (unsigned long)PlaybackDriverFlags;

    for (int i = 0; i < fPlaybackChannels; i++) {
        snprintf(alias, sizeof(alias), "%s:%s:in%d", fAliasName, fPlaybackDriverName, i + 1);
        snprintf(name, sizeof(name), "%s:playback_%d", fClientControl.fName, i + 1);
        if (fEngine->PortRegister(fClientControl.fRefNum, name, JACK_DEFAULT_AUDIO_TYPE, (JackPortFlags)port_flags, fEngineControl->fBufferSize, &port_index) < 0) {
            jack_error("driver: cannot register port for %s", name);
            return -1;
        }
        port = fGraphManager->GetPort(port_index);
        port->SetAlias(alias);
        fPlaybackPortList[i] = port_index;
        jack_log("JackAlsaDriver::Attach fPlaybackPortList[i] %ld ", port_index);

        // Monitor ports
        if (fWithMonitorPorts) {
            jack_log("Create monitor port");
            snprintf(name, sizeof(name), "%s:monitor_%d", fClientControl.fName, i + 1);
            if (fEngine->PortRegister(fClientControl.fRefNum, name, JACK_DEFAULT_AUDIO_TYPE, MonitorDriverFlags, fEngineControl->fBufferSize, &port_index) < 0) {
                jack_error("ALSA: cannot register monitor port for %s", name);
            } else {
                fMonitorPortList[i] = port_index;
            }
        }
    }

    {
        std::vector<JackAlsaSecondaryCard*> cards;
        {
            std::lock_guard<std::mutex> lock(fCardsMutex);
            cards = fSecondaryCards;
        }
        for (size_t c = 0; c < cards.size(); c++) {
            if (!PromoteCard(cards[c])) {
                return -1;
            }
        }
    }

    UpdateLatencies();
    UpdateSecondaryLatencies();

    if (alsa_driver->midi) {
        int err = (alsa_driver->midi->attach)(alsa_driver->midi);
        if (err)
            jack_error ("ALSA: cannot attach MIDI: %d", err);
    }

    return 0;
}

int JackAlsaDriver::Detach()
{
    alsa_driver_t* alsa_driver = (alsa_driver_t*)fDriver;

    // Stop the threads that can change the card list before taking the cards out.
    StopControlMonitoring();
    {
        std::lock_guard<std::mutex> lock(fCardsMutex);
        for (size_t c = 0; c < fSecondaryCards.size(); c++) {
            fSlots.Remove(fSecondaryCards[c]);
            UnregisterCardPorts(fSecondaryCards[c]);
        }
    }
    if (alsa_driver->midi)
        (alsa_driver->midi->detach)(alsa_driver->midi);

    return JackAudioDriver::Detach();
}

extern "C" char* get_control_device_name(const char * device_name)
{
    char * ctl_name;
    const char * comma;

    /* the user wants a hw or plughw device, the ctl name
     * should be hw:x where x is the card identification.
     * We skip the subdevice suffix that starts with comma */

    if (strncasecmp(device_name, "plughw:", 7) == 0) {
        /* skip the "plug" prefix" */
        device_name += 4;
    }

    comma = strchr(device_name, ',');
    if (comma == NULL) {
        ctl_name = strdup(device_name);
        if (ctl_name == NULL) {
            jack_error("strdup(\"%s\") failed.", device_name);
        }
    } else {
        ctl_name = strndup(device_name, comma - device_name);
        if (ctl_name == NULL) {
            jack_error("strndup(\"%s\", %u) failed.", device_name, (unsigned int)(comma - device_name));
        }
    }

    return ctl_name;
}

static int card_to_num(const char* device)
{
    int err;
    char* ctl_name;
    snd_ctl_card_info_t *card_info;
    snd_ctl_t* ctl_handle;
    int i = -1;

    snd_ctl_card_info_alloca (&card_info);

    ctl_name = get_control_device_name(device);
    if (ctl_name == NULL) {
        jack_error("get_control_device_name() failed.");
        goto fail;
    }

    if ((err = snd_ctl_open (&ctl_handle, ctl_name, 0)) < 0) {
        jack_error ("control open \"%s\" (%s)", ctl_name,
                    snd_strerror(err));
        goto free;
    }

    if ((err = snd_ctl_card_info(ctl_handle, card_info)) < 0) {
        jack_error ("control hardware info \"%s\" (%s)",
                    device, snd_strerror (err));
        goto close;
    }

    i = snd_ctl_card_info_get_card(card_info);

close:
    snd_ctl_close(ctl_handle);

free:
    free(ctl_name);

fail:
    return i;
}

// PortBase of a configuration: its name and device number.  Two devices of one
// card have different port bases even though their card name is the same.
static std::string PortBaseOf(const JackAlsaSecondaryConfig& cfg)
{
    const std::string& pcm = cfg.capture_device.empty() ? cfg.playback_device : cfg.capture_device;
    return cfg.name + "_d" + std::to_string(JackAlsaPcmDeviceNumber(pcm));
}

// Finds the card and device of a PCM name among the enumerated devices.  Accepts
// "hw:<index>[,n]", "hw:<id>[,n]" and "hw:CARD=<id>,DEV=<n>".
static bool ResolvePcm(const std::string& pcm, const std::vector<JackAlsaPcmInfo>& pcms,
                       int* card, int* device)
{
    std::string rest = pcm.find(':') == std::string::npos ? pcm : pcm.substr(pcm.find(':') + 1);
    std::string key;
    if (rest.compare(0, 5, "CARD=") == 0) {
        key = rest.substr(5, rest.find(',') == std::string::npos ? std::string::npos : rest.find(',') - 5);
    } else {
        key = rest.substr(0, rest.find(','));
    }
    bool numeric = !key.empty() && key.find_first_not_of("0123456789") == std::string::npos;
    *device = JackAlsaPcmDeviceNumber(pcm);
    for (size_t i = 0; i < pcms.size(); i++) {
        if ((numeric && pcms[i].card_num == atoi(key.c_str())) || (!numeric && pcms[i].card_id == key)) {
            *card = pcms[i].card_num;
            return true;
        }
    }
    return false;
}

// The configured cards, followed by an automatic card for every other PCM device
// that has a stream.  The primary's device and the devices configured explicitly
// are not added again.  Automatic cards stop at the slot limit.
static std::vector<JackAlsaSecondaryConfig> BuildSecondaryPlan(
        const std::vector<JackAlsaSecondaryConfig>& configured,
        const char* primary_capture, const char* primary_playback,
        const std::vector<JackAlsaPcmInfo>& known, bool report,
        const std::vector<std::string>& excluded,
        std::vector<JackAlsaPcmInfo>* listing)
{
    std::vector<JackAlsaSecondaryConfig> plan = configured;
    std::vector<JackAlsaPcmInfo> pcms = JackAlsaEnumeratePcms(known, report);
    if (listing != NULL) {
        *listing = pcms;
    }

    std::vector<std::pair<int, int> > claimed;
    const char* names[] = { primary_capture, primary_playback };
    std::vector<std::string> used;
    for (size_t i = 0; i < 2; i++) {
        int card, device;
        if (names[i] != NULL && ResolvePcm(names[i], pcms, &card, &device)) {
            claimed.push_back(std::make_pair(card, device));
        }
    }
    for (size_t i = 0; i < configured.size(); i++) {
        int card, device;
        const std::string pcms_for[] = { configured[i].capture_device, configured[i].playback_device };
        for (size_t d = 0; d < 2; d++) {
            if (!pcms_for[d].empty() && ResolvePcm(pcms_for[d], pcms, &card, &device)) {
                claimed.push_back(std::make_pair(card, device));
            }
        }
        used.push_back(PortBaseOf(configured[i]));
    }

    for (size_t i = 0; i < pcms.size(); i++) {
        const JackAlsaPcmInfo& pcm = pcms[i];
        if (std::find(claimed.begin(), claimed.end(), std::make_pair(pcm.card_num, pcm.device)) != claimed.end()) {
            continue;
        }
        if (JackAlsaCardExcluded(pcm.card_id, excluded)) {
            if (report) {
                jack_info("ALSA device %s is not added: its card %s is excluded", pcm.pcm.c_str(), pcm.card_id.c_str());
            }
            continue;
        }
        if (plan.size() >= JackAlsaCardSlots::Capacity()) {
            jack_info("ALSA device %s is not added: all %zu secondary card slots are in use",
                      pcm.pcm.c_str(), JackAlsaCardSlots::Capacity());
            continue;
        }

        JackAlsaSecondaryConfig cfg;
        cfg.name = JackAlsaSecondarySanitizeName(pcm.card_id);
        std::transform(cfg.name.begin(), cfg.name.end(), cfg.name.begin(), ::tolower);
        if (pcm.playback) {
            cfg.playback_device = pcm.pcm;
            cfg.playback_channels = pcm.playback_channels < 12 ? pcm.playback_channels : 12;
        }
        if (pcm.capture) {
            cfg.capture_device = pcm.pcm;
            cfg.capture_channels = pcm.capture_channels < 12 ? pcm.capture_channels : 12;
        }
        cfg.mode = (pcm.playback && pcm.capture) ? SecondaryModeDuplex
                 : (pcm.capture ? SecondaryModeCapture : SecondaryModePlayback);
        cfg.automatic = true;

        // Two devices of one card share a card name: their port bases still differ.
        if (std::find(used.begin(), used.end(), PortBaseOf(cfg)) != used.end()) {
            jack_error("ALSA device %s is not added: its name %s is already used",
                       pcm.pcm.c_str(), PortBaseOf(cfg).c_str());
            continue;
        }
        used.push_back(PortBaseOf(cfg));
        if (report) {
            jack_info("automatic secondary card for %s: %s, %d/%d channels",
                      pcm.pcm.c_str(), PortBaseOf(cfg).c_str(), cfg.capture_channels, cfg.playback_channels);
        }
        plan.push_back(cfg);
    }
    return plan;
}

int JackAlsaDriver::Open(jack_nframes_t nframes,
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
                         const std::vector<std::string>& excluded_cards)
{
    // Generic JackAudioDriver Open
    if (JackAudioDriver::Open(nframes, samplerate, capturing, playing,
                              inchannels, outchannels, monitor, capture_driver_name, playback_driver_name,
                              capture_latency, playback_latency) != 0) {
        return -1;
    }

    alsa_midi_t *midi = 0;
    if (strcmp(midi_driver_name, "seq") == 0)
        midi = alsa_seqmidi_new((jack_client_t*)this, 0);
    else if (strcmp(midi_driver_name, "raw") == 0)
        midi = alsa_rawmidi_new((jack_client_t*)this);

    if (JackServerGlobals::on_device_acquire != NULL) {
        int capture_card = card_to_num(capture_driver_name);
        int playback_card = card_to_num(playback_driver_name);
        char audio_name[32];

        if (capture_card >= 0) {
            snprintf(audio_name, sizeof(audio_name), "Audio%d", capture_card);
            if (!JackServerGlobals::on_device_acquire(audio_name)) {
                jack_error("Audio device %s cannot be acquired...", capture_driver_name);
                return -1;
            }
        }

        if (playback_card >= 0 && playback_card != capture_card) {
            snprintf(audio_name, sizeof(audio_name), "Audio%d", playback_card);
            if (!JackServerGlobals::on_device_acquire(audio_name)) {
                jack_error("Audio device %s cannot be acquired...", playback_driver_name);
                if (capture_card >= 0) {
                    snprintf(audio_name, sizeof(audio_name), "Audio%d", capture_card);
                    JackServerGlobals::on_device_release(audio_name);
                }
                return -1;
            }
        }
    }

    // One server per card: lock the primary devices before touching them.
    if (capturing && !LockCard(capture_driver_name, true)) {
        Close();
        return -1;
    }
    if (playing && !LockCard(playback_driver_name, true)) {
        Close();
        return -1;
    }

    fDriver = alsa_driver_new ((char*)"alsa_pcm", (char*)playback_driver_name, (char*)capture_driver_name,
                               NULL,
                               nframes,
                               user_nperiods,
                               samplerate,
                               hw_monitoring,
                               hw_metering,
                               capturing,
                               playing,
                               dither,
                               soft_mode,
                               monitor,
                               inchannels,
                               outchannels,
                               shorts_first,
                               capture_latency,
                               playback_latency,
                               midi,
                               0,
                               ALSA_SAMPLE_FORMAT_ANY);
    if (!fDriver) {
        Close();
        return -1;
    }

    InstallAlsaCallbacks(this, (alsa_driver_t*)fDriver);

    // ALSA driver may have changed the in/out values
    fCaptureChannels = ((alsa_driver_t *)fDriver)->capture_nchannels;
    fPlaybackChannels = ((alsa_driver_t *)fDriver)->playback_nchannels;

    alsa_driver_t* primary = (alsa_driver_t*)fDriver;
    fConfiguredSecondaries = secondaries;
    fExcludedCards = excluded_cards;
    fReportedFailures.clear();
    std::vector<JackAlsaPcmInfo> listing;
    std::vector<JackAlsaSecondaryConfig> plan = BuildSecondaryPlan(secondaries, capture_driver_name, playback_driver_name,
                                                                   fKnownPcms, true, fExcludedCards, &listing);
    fKnownPcms = listing;

    // Always created: a card can appear after startup, and the watchers must
    // exist to add it.
    fReconfigWorker = new JackAlsaReconfigWorker(this);
    fControlMonitor = new JackAlsaControlMonitor(fReconfigWorker);
    fHotplug = new JackAlsaHotplug(fReconfigWorker);

    for (size_t i = 0; i < plan.size(); i++) {
        const JackAlsaSecondaryConfig& cfg = plan[i];

        for (size_t j = 0; j < i; j++) {
            if (PortBaseOf(plan[j]) == PortBaseOf(cfg)) {
                jack_error("secondary ALSA card '%s' is configured more than once", PortBaseOf(cfg).c_str());
                Close();
                return -1;
            }
        }

        if (!cfg.automatic && (cfg.capture_device == capture_driver_name || cfg.capture_device == playback_driver_name ||
            cfg.playback_device == capture_driver_name || cfg.playback_device == playback_driver_name)) {
            jack_error("secondary ALSA card '%s' uses a device already used by the primary card", cfg.name.c_str());
            Close();
            return -1;
        }

        // One server may use several devices of one card: rate elements are named per
        // device.  Other servers are still excluded by the card lock.
        if (!LockCard(cfg.capture_device.empty() ? cfg.playback_device.c_str() : cfg.capture_device.c_str(), true)) {
            if (cfg.automatic) {
                jack_error("automatic secondary card '%s' is skipped: its card is held by another server",
                           PortBaseOf(cfg).c_str());
                continue;
            }
            Close();
            return -1;
        }

        JackResamplerFactory* factory = JackResamplerFactory::Find(cfg.resampler.c_str());
        if (factory == NULL) {
            jack_error("secondary card '%s': resampler '%s' is not available (set JACK_RESAMPLER_PATH for plugins)",
                       cfg.name.c_str(), cfg.resampler.c_str());
            Close();
            return -1;
        }

        JackAlsaSecondaryCard* card = CreateCard(cfg, factory, primary);
        if (card == NULL) {
            if (cfg.optional || cfg.automatic) {
                jack_error("skipping secondary ALSA card '%s'", PortBaseOf(cfg).c_str());
                continue;
            }
            Close();
            return -1;
        }
        fSecondaryCards.push_back(card);
    }

    for (size_t i = 0; i < fSecondaryCards.size(); i++) {
        JackAlsaSecondaryCard* card = fSecondaryCards[i];
        for (int d = 0; d < 2; d++) {
            JackAlsaRateDirection dir = (JackAlsaRateDirection)d;
            if (card->RateElementValid(dir) &&
                fControlMonitor->AddWatch(card->Id(), card->ControlName(), card->RateElement(dir), dir) < 0) {
                jack_error("secondary card '%s': rate control changes will not be seen",
                           card->Name().c_str());
            }
        }
    }

    if (JackServerGlobals::on_device_reservation_loop != NULL) {
        device_reservation_loop_running = true;
        if (JackPosixThread::StartImp(&fReservationLoopThread, 0, 0, on_device_reservation_loop, NULL) != 0) {
            device_reservation_loop_running = false;
        }
    }

    return 0;
}

// shared: the primary's capture and playback may use one card.  A secondary
// card must be a card this server does not already use, since its rate
// control element would clash with the other device's.
bool JackAlsaDriver::LockCard(const char* pcm_name, bool shared)
{
    std::string ctl = JackAlsaControlDeviceName(pcm_name);
    if (ctl.empty()) {
        return false;
    }
    for (size_t i = 0; i < fCardLockNames.size(); i++) {
        if (ctl == fCardLockNames[i]) {
            if (shared) {
                return true;
            }
            jack_error("secondary ALSA card %s is already used by this server", ctl.c_str());
            return false;
        }
    }
    int fd = JackAlsaControlLockCard(ctl);
    if (fd < 0) {
        return false;
    }
    fCardLocks.push_back(fd);
    fCardLockNames.push_back(ctl);
    return true;
}

void JackAlsaDriver::UnlockCards()
{
    for (size_t i = 0; i < fCardLocks.size(); i++) {
        JackAlsaControlUnlockCard(fCardLocks[i]);
    }
    fCardLocks.clear();
    fCardLockNames.clear();
}

int JackAlsaDriver::Close()
{
    // Generic audio driver close
    int res = JackAudioDriver::Close();

    // Stop every thread that can change the cards before they are freed.
    StopControlMonitoring();
    UnlockCards();

    // The hot-plug listener, monitor and worker refer to the cards, so they go first.
    delete fHotplug;
    fHotplug = NULL;
    delete fControlMonitor;
    fControlMonitor = NULL;
    delete fReconfigWorker;
    fReconfigWorker = NULL;

    for (size_t i = 0; i < fSecondaryCards.size(); i++) {
        delete fSecondaryCards[i];
    }
    fSecondaryCards.clear();

    if (fDriver) {
        alsa_driver_delete((alsa_driver_t*)fDriver);
    }

    if (device_reservation_loop_running) {
        device_reservation_loop_running = false;
        JackPosixThread::StopImp(fReservationLoopThread);
    }

    if (JackServerGlobals::on_device_release != NULL)
    {
        char audio_name[32];
        int capture_card = card_to_num(fCaptureDriverName);
        if (capture_card >= 0) {
            snprintf(audio_name, sizeof(audio_name), "Audio%d", capture_card);
            JackServerGlobals::on_device_release(audio_name);
        }

        int playback_card = card_to_num(fPlaybackDriverName);
        if (playback_card >= 0 && playback_card != capture_card) {
            snprintf(audio_name, sizeof(audio_name), "Audio%d", playback_card);
            JackServerGlobals::on_device_release(audio_name);
        }
    }

    return res;
}

int JackAlsaDriver::Start()
{
    int res = JackAudioDriver::Start();
    if (res >= 0) {
        res = alsa_driver_start((alsa_driver_t *)fDriver);
        if (res < 0) {
            JackAudioDriver::Stop();
        } else {
            std::lock_guard<std::mutex> lock(fCardsMutex);
            for (size_t i = 0; i < fSecondaryCards.size(); i++) {
                if (fSecondaryCards[i]->Start() < 0) {
                    res = -1;
                    break;
                }
            }
            if (res < 0) {
                StopSecondaryCards();
                alsa_driver_stop((alsa_driver_t *)fDriver);
                JackAudioDriver::Stop();
            } else {
                StartControlMonitoring();
                if (!fRestarting.load()) {
                    fRTRunning.store(true);
                }
            }
        }
    }
    return res;
}

void JackAlsaDriver::StopSecondaryCards()
{
    std::lock_guard<std::mutex> lock(fCardsMutex);
    for (size_t i = 0; i < fSecondaryCards.size(); i++) {
        fSecondaryCards[i]->Stop();
    }
}

void JackAlsaDriver::StartControlMonitoring()
{
    if (fControlMonitor == NULL) {
        return;
    }
    if (fReconfigWorker->Start() < 0 || fControlMonitor->Start() < 0) {
        // Losing rate-control events leaves the cards running at their current
        // rates; the primary card is unaffected.
        jack_error("secondary card rate controls are not being monitored");
    }
    if (fHotplug != NULL && fHotplug->Start() < 0) {
        jack_error("hot-plug is not active; cards will not be added or removed while running");
    }
}

void JackAlsaDriver::StopControlMonitoring()
{
    if (fControlMonitor == NULL) {
        return;
    }
    if (fHotplug != NULL) {
        fHotplug->Stop();
    }
    fControlMonitor->Stop();
    fReconfigWorker->Stop();
}

// The identity of a card for reconciliation: its port base and its mode.  A card
// whose device changed mode is a different card.
static std::string CardKeyOf(const JackAlsaSecondaryConfig& cfg)
{
    return PortBaseOf(cfg) + "|" + std::to_string((int)cfg.mode);
}

// Runs on the worker thread once a burst of sound events has settled.  Cards
// whose device is gone are retired; devices that are now present are added.
// A device that fails to open is not retried until the next event.
void JackAlsaDriver::HandleSoundChanged()
{
    if (fDriver == NULL) {
        return;
    }
    alsa_driver_t* primary = (alsa_driver_t*)fDriver;

    std::vector<JackAlsaPcmInfo> listing;
    std::vector<JackAlsaSecondaryConfig> plan = BuildSecondaryPlan(fConfiguredSecondaries, fCaptureDriverName,
                                                                   fPlaybackDriverName, fKnownPcms, false,
                                                                   fExcludedCards, &listing);
    fKnownPcms = listing;

    std::vector<JackAlsaSecondaryCard*> cards;
    {
        std::lock_guard<std::mutex> lock(fCardsMutex);
        cards = fSecondaryCards;
    }
    std::vector<std::string> current;
    std::vector<std::string> desired;
    for (size_t i = 0; i < cards.size(); i++) {
        current.push_back(CardKeyOf(cards[i]->Config()));
    }
    for (size_t j = 0; j < plan.size(); j++) {
        desired.push_back(CardKeyOf(plan[j]));
    }

    JackAlsaCardChanges changes = JackAlsaDiffCards(current, desired);
    for (size_t k = 0; k < changes.retire.size(); k++) {
        JackAlsaSecondaryCard* card = cards[changes.retire[k]];
        jack_info("secondary card '%s' removed: its device is no longer present or no longer wanted",
                  PortBaseOf(card->Config()).c_str());
        RetireCard(card);
    }
    for (size_t k = 0; k < changes.add.size(); k++) {
        AddCard(plan[changes.add[k]], primary);
    }
    UpdateSecondaryLatencies();
}

// Creates and opens a card for the primary's current settings.  Returns NULL if
// the card cannot be opened.
JackAlsaSecondaryCard* JackAlsaDriver::CreateCard(const JackAlsaSecondaryConfig& cfg, JackResamplerFactory* factory,
                                                  alsa_driver_t* primary)
{
    JackAlsaSecondaryCard* card = new JackAlsaSecondaryCard(cfg, factory);
    card->SetRTState(&fRTEpoch, &fRTRunning);
    card->SetId(fNextCardId++);
    card->SetFailureSink(fReconfigWorker);
    if (card->Open(primary->frames_per_cycle, primary->frame_rate, primary->user_nperiods,
                   fEngineControl->fRealTime, fEngineControl->fServerPriority) < 0) {
        delete card;
        return NULL;
    }
    return card;
}

// Adds a card for a device that appeared after startup.  Runs on the worker
// thread, with the driver running.  A failure is logged once and the card is
// left out until the next event.
bool JackAlsaDriver::AddCard(const JackAlsaSecondaryConfig& cfg, alsa_driver_t* primary)
{
    const std::string& pcm = cfg.capture_device.empty() ? cfg.playback_device : cfg.capture_device;
    if (!LockCard(pcm.c_str(), true)) {
        ReportOnce("secondary card '" + PortBaseOf(cfg) + "' is skipped: its card is held by another server");
        return false;
    }

    JackResamplerFactory* factory = JackResamplerFactory::Find(cfg.resampler.c_str());
    if (factory == NULL) {
        ReportOnce("secondary card '" + PortBaseOf(cfg) + "': resampler '" + cfg.resampler + "' is not available");
        return false;
    }

    JackAlsaSecondaryCard* card = CreateCard(cfg, factory, primary);
    if (card == NULL) {
        ReportOnce("secondary card '" + PortBaseOf(cfg) + "' cannot be opened; it will be tried at the next device change");
        return false;
    }
    if (card->Start() < 0) {
        delete card;
        ReportOnce("secondary card '" + PortBaseOf(cfg) + "' cannot be started; it will be tried at the next device change");
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(fCardsMutex);
        fSecondaryCards.push_back(card);
    }
    if (!PromoteCard(card)) {
        // Not promoted, so the RT thread never used it; retiring frees it.
        RetireCard(card);
        ReportOnce("secondary card '" + PortBaseOf(cfg) + "' cannot be added; it will be tried at the next device change");
        return false;
    }

    for (int d = 0; d < 2; d++) {
        JackAlsaRateDirection dir = (JackAlsaRateDirection)d;
        if (card->RateElementValid(dir)) {
            fControlMonitor->QueueWatch(card->Id(), card->ControlName(), card->RateElement(dir), dir);
        }
    }
    jack_info("secondary card '%s' added", PortBaseOf(cfg).c_str());
    return true;
}

void JackAlsaDriver::ReportOnce(const std::string& message)
{
    for (size_t i = 0; i < fReportedFailures.size(); i++) {
        if (fReportedFailures[i] == message) {
            return;
        }
    }
    fReportedFailures.push_back(message);
    jack_error("%s", message.c_str());
}

void JackAlsaDriver::HandleRateRequest(int card, JackAlsaRateDirection dir, long value)
{
    JackAlsaSecondaryCard* secondary = FindCard(card);
    if (secondary == NULL) {
        return;   // the card was removed; its watch may still have delivered this
    }
    long current = (long)secondary->Rate();
    if (value == current) {
        // Our own write echoing back, or a no-op.
        return;
    }

    const char* what = (dir == RateCapture) ? "capture" : "playback";
    if (value < 8000 || value > 768000) {
        jack_error("secondary card '%s': %s rate %ld Hz is out of range; keeping %ld Hz",
                   secondary->Name().c_str(), what, value, current);
    } else if (dir == RateCapture && !secondary->Config().follow_hw_rate) {
        // The default: a card keeps its rate unless it is asked to follow the hardware.
        jack_error("secondary card '%s': capture rate changed to %ld Hz; keeping %ld Hz "
                   "(follow_hw_rate=0)", secondary->Name().c_str(), value, current);
    } else {
        jack_info("secondary card '%s': %s rate change %ld Hz -> %ld Hz, reopening",
                  secondary->Name().c_str(), what, current, value);
        if (secondary->Reconfigure((jack_nframes_t)value) < 0) {
            jack_error("secondary card '%s': %s rate change to %ld Hz failed",
                       secondary->Name().c_str(), what, value);
        } else if ((long)secondary->Rate() != current) {
            UpdateSecondaryLatencies();
            // Only a change that took effect is reported.  A card whose capture and
            // playback share one stream reports both directions in one call.
            int directions = (secondary->CaptureEnabled() ? JackSecondaryRateCapture : 0) |
                             (secondary->PlaybackEnabled() ? JackSecondaryRatePlayback : 0);
            NotifySecondarySampleRate(secondary->Name().c_str(), secondary->Rate(), directions);
        }
    }

    // Report the rate the card actually runs at.  The write echoes back as an
    // event with the same value, which is ignored above.
    long actual = (long)secondary->Rate();
    for (int d = 0; d < 2; d++) {
        JackAlsaRateDirection each = (JackAlsaRateDirection)d;
        if (secondary->RateElementValid(each) && secondary->RateElement(each).writable) {
            if (secondary->WriteRate(each, actual) < 0) {
                jack_error("secondary card '%s': cannot update the rate control", secondary->Name().c_str());
            }
        }
    }
}

int JackAlsaDriver::Stop()
{
    if (!fRestarting.load()) {
        fRTRunning.store(false);
    }
    StopControlMonitoring();
    StopSecondaryCards();
    int res = alsa_driver_stop((alsa_driver_t *)fDriver);
    if (JackAudioDriver::Stop() < 0) {
        res = -1;
    }
    return res;
}

int JackAlsaDriver::Read()
{
    /* Taken from alsa_driver_run_cycle */
    int wait_status;
    jack_nframes_t nframes;
    fDelayedUsecs = 0.f;

retry:

    nframes = alsa_driver_wait((alsa_driver_t *)fDriver, -1, &wait_status, &fDelayedUsecs);

    if (wait_status < 0)
        return -1;		/* driver failed */

    if (nframes == 0) {
        /* we detected an xrun and restarted: notify
         * clients about the delay.
         */
        jack_log("ALSA XRun wait_status = %d", wait_status);
        NotifyXRun(fBeginDateUst, fDelayedUsecs);
        for (size_t i = 0; i < fSlots.Capacity(); i++) {
            JackAlsaSecondaryCard* card = fSlots.Get(i);
            if (card) {
                card->ResetResampling();
            }
        }
        goto retry; /* recoverable error*/
    }

    if (nframes != fEngineControl->fBufferSize)
        jack_log("JackAlsaDriver::Read warning fBufferSize = %ld nframes = %ld", fEngineControl->fBufferSize, nframes);

    // Has to be done before read
    JackDriver::CycleIncTime();

    int res = alsa_driver_read((alsa_driver_t *)fDriver, fEngineControl->fBufferSize);
    if (res < 0) {
        return res;
    }

    for (size_t i = 0; i < fSlots.Capacity(); i++) {
        JackAlsaSecondaryCard* card = fSlots.Get(i);
        if (card) {
            card->ReadToPorts(fGraphManager, fEngineControl->fBufferSize);
        }
    }
    return 0;
}

int JackAlsaDriver::Write()
{
    int res = alsa_driver_write((alsa_driver_t *)fDriver, fEngineControl->fBufferSize);
    if (res < 0) {
        return res;
    }

    for (size_t i = 0; i < fSlots.Capacity(); i++) {
        JackAlsaSecondaryCard* card = fSlots.Get(i);
        if (card) {
            card->WriteFromPorts(fGraphManager, fEngineControl->fBufferSize);
        }
    }

    // One RT cycle is complete.  Card reconfiguration waits on this counter.
    fRTEpoch.fetch_add(1, std::memory_order_release);
    return 0;
}

void JackAlsaDriver::ReadInputAux(jack_nframes_t orig_nframes, snd_pcm_sframes_t contiguous, snd_pcm_sframes_t nread)
{
    for (int chn = 0; chn < fCaptureChannels; chn++) {
        if (fGraphManager->GetConnectionsNum(fCapturePortList[chn]) > 0) {
            jack_default_audio_sample_t* buf = (jack_default_audio_sample_t*)fGraphManager->GetBuffer(fCapturePortList[chn], orig_nframes);
            alsa_driver_read_from_channel((alsa_driver_t *)fDriver, chn, buf + nread, contiguous);
        }
    }
}

void JackAlsaDriver::MonitorInputAux()
{
    for (int chn = 0; chn < fCaptureChannels; chn++) {
        JackPort* port = fGraphManager->GetPort(fCapturePortList[chn]);
        if (port->MonitoringInput()) {
            ((alsa_driver_t *)fDriver)->input_monitor_mask |= (1 << chn);
        }
    }
}

void JackAlsaDriver::ClearOutputAux()
{
    for (int chn = 0; chn < fPlaybackChannels; chn++) {
        jack_default_audio_sample_t* buf =
            (jack_default_audio_sample_t*)fGraphManager->GetBuffer(fPlaybackPortList[chn], fEngineControl->fBufferSize);
        memset(buf, 0, sizeof (jack_default_audio_sample_t) * fEngineControl->fBufferSize);
    }
}

void JackAlsaDriver::SetTimetAux(jack_time_t time)
{
    fBeginDateUst = time;
}

int JackAlsaDriver::PortSetDefaultMetadata(jack_port_id_t port_id, const char* pretty_name)
{
    return fEngine->PortSetDefaultMetadata(fClientControl.fRefNum, port_id, pretty_name);
}

void JackAlsaDriver::WriteOutputAux(jack_nframes_t orig_nframes, snd_pcm_sframes_t contiguous, snd_pcm_sframes_t nwritten)
{
    for (int chn = 0; chn < fPlaybackChannels; chn++) {
        // Output ports
        if (fGraphManager->GetConnectionsNum(fPlaybackPortList[chn]) > 0) {
            jack_default_audio_sample_t* buf = (jack_default_audio_sample_t*)fGraphManager->GetBuffer(fPlaybackPortList[chn], orig_nframes);
            alsa_driver_write_to_channel(((alsa_driver_t *)fDriver), chn, buf + nwritten, contiguous);
            // Monitor ports
            if (fWithMonitorPorts && fGraphManager->GetConnectionsNum(fMonitorPortList[chn]) > 0) {
                jack_default_audio_sample_t* monbuf = (jack_default_audio_sample_t*)fGraphManager->GetBuffer(fMonitorPortList[chn], orig_nframes);
                memcpy(monbuf + nwritten, buf + nwritten, contiguous * sizeof(jack_default_audio_sample_t));
            }
        }
    }
}

int JackAlsaDriver::is_realtime() const
{
    return fEngineControl->fRealTime;
}

int JackAlsaDriver::create_thread(pthread_t *thread, int priority, int realtime, void *(*start_routine)(void*), void *arg)
{
    return JackPosixThread::StartImp(thread, priority, realtime, start_routine, arg);
}

jack_port_id_t JackAlsaDriver::port_register(const char *port_name, const char *port_type, unsigned long flags, unsigned long buffer_size)
{
    jack_port_id_t port_index;
    int res = fEngine->PortRegister(fClientControl.fRefNum, port_name, port_type, flags, buffer_size, &port_index);
    return (res == 0) ? port_index : 0;
}

int JackAlsaDriver::port_unregister(jack_port_id_t port_index)
{
    return fEngine->PortUnRegister(fClientControl.fRefNum, port_index);
}

void* JackAlsaDriver::port_get_buffer(int port, jack_nframes_t nframes)
{
    return fGraphManager->GetBuffer(port, nframes);
}

int  JackAlsaDriver::port_set_alias(int port, const char* name)
{
    return fGraphManager->GetPort(port)->SetAlias(name);
}

jack_nframes_t JackAlsaDriver::get_sample_rate() const
{
    return fEngineControl->fSampleRate;
}

jack_nframes_t JackAlsaDriver::frame_time() const
{
    JackTimer timer;
    fEngineControl->ReadFrameTime(&timer);
    return timer.Time2Frames(GetMicroSeconds(), fEngineControl->fBufferSize);
}

jack_nframes_t JackAlsaDriver::last_frame_time() const
{
    JackTimer timer;
    fEngineControl->ReadFrameTime(&timer);
    return timer.CurFrame();
}

} // end of namespace


#ifdef __cplusplus
extern "C"
{
#endif

static
jack_driver_param_constraint_desc_t *
enum_alsa_devices()
{
    snd_ctl_t * handle;
    snd_ctl_card_info_t * info;
    snd_pcm_info_t * pcminfo_capture;
    snd_pcm_info_t * pcminfo_playback;
    int card_no = -1;
    jack_driver_param_value_t card_id;
    jack_driver_param_value_t device_id;
    char description[64];
    int device_no;
    bool has_capture;
    bool has_playback;
    jack_driver_param_constraint_desc_t * constraint_ptr;
    uint32_t array_size = 0;

    snd_ctl_card_info_alloca(&info);
    snd_pcm_info_alloca(&pcminfo_capture);
    snd_pcm_info_alloca(&pcminfo_playback);

    constraint_ptr = NULL;

    while(snd_card_next(&card_no) >= 0 && card_no >= 0)
    {
        snprintf(card_id.str, sizeof(card_id.str), "hw:%d", card_no);

        if (snd_ctl_open(&handle, card_id.str, 0) >= 0 &&
            snd_ctl_card_info(handle, info) >= 0)
        {
            snprintf(card_id.str, sizeof(card_id.str), "hw:%s", snd_ctl_card_info_get_id(info));
            if (!jack_constraint_add_enum(
                    &constraint_ptr,
                    &array_size,
                    &card_id,
                    snd_ctl_card_info_get_name(info)))
                goto fail;

            device_no = -1;

            while (snd_ctl_pcm_next_device(handle, &device_no) >= 0 && device_no != -1)
            {
                snprintf(device_id.str, sizeof(device_id.str), "%s,%d", card_id.str, device_no);

                snd_pcm_info_set_device(pcminfo_capture, device_no);
                snd_pcm_info_set_subdevice(pcminfo_capture, 0);
                snd_pcm_info_set_stream(pcminfo_capture, SND_PCM_STREAM_CAPTURE);
                has_capture = snd_ctl_pcm_info(handle, pcminfo_capture) >= 0;

                snd_pcm_info_set_device(pcminfo_playback, device_no);
                snd_pcm_info_set_subdevice(pcminfo_playback, 0);
                snd_pcm_info_set_stream(pcminfo_playback, SND_PCM_STREAM_PLAYBACK);
                has_playback = snd_ctl_pcm_info(handle, pcminfo_playback) >= 0;

                if (has_capture && has_playback)
                {
                    snprintf(description, sizeof(description),"%s (duplex)", snd_pcm_info_get_name(pcminfo_capture));
                }
                else if (has_capture)
                {
                    snprintf(description, sizeof(description),"%s (capture)", snd_pcm_info_get_name(pcminfo_capture));
                }
                else if (has_playback)
                {
                    snprintf(description, sizeof(description),"%s (playback)", snd_pcm_info_get_name(pcminfo_playback));
                }
                else
                {
                    continue;
                }

                if (!jack_constraint_add_enum(
                        &constraint_ptr,
                        &array_size,
                        &device_id,
                        description))
                    goto fail;
            }

            snd_ctl_close(handle);
        }
    }

    return constraint_ptr;
fail:
    jack_constraint_free(constraint_ptr);
    return NULL;
}

static int
dither_opt (char c, DitherAlgorithm* dither)
{
    switch (c) {
        case '-':
        case 'n':
            *dither = None;
            break;

        case 'r':
            *dither = Rectangular;
            break;

        case 's':
            *dither = Shaped;
            break;

        case 't':
            *dither = Triangular;
            break;

        default:
            fprintf (stderr, "ALSA driver: illegal dithering mode %c\n", c);
            return -1;
    }
    return 0;
}

SERVER_EXPORT const jack_driver_desc_t* driver_get_descriptor ()
{
    jack_driver_desc_t * desc;
    jack_driver_desc_filler_t filler;
    jack_driver_param_value_t value;

    desc = jack_driver_descriptor_construct("alsa", JackDriverMaster, "Linux ALSA API based audio backend", &filler);

    strcpy(value.str, "hw:0");
    jack_driver_descriptor_add_parameter(desc, &filler, "device", 'd', JackDriverParamString, &value, enum_alsa_devices(), "ALSA device name", NULL);

    strcpy(value.str, "none");
    jack_driver_descriptor_add_parameter(desc, &filler, "capture", 'C', JackDriverParamString, &value, NULL, "Provide capture ports.  Optionally set device", NULL);
    jack_driver_descriptor_add_parameter(desc, &filler, "playback", 'P', JackDriverParamString, &value, NULL, "Provide playback ports.  Optionally set device", NULL);

    value.ui = 48000U;
    jack_driver_descriptor_add_parameter(desc, &filler, "rate", 'r', JackDriverParamUInt, &value, NULL, "Sample rate", NULL);

    value.ui = 1024U;
    jack_driver_descriptor_add_parameter(desc, &filler, "period", 'p', JackDriverParamUInt, &value, NULL, "Frames per period", NULL);

    value.ui = 2U;
    jack_driver_descriptor_add_parameter(desc, &filler, "nperiods", 'n', JackDriverParamUInt, &value, NULL, "Number of periods of playback latency", NULL);

    value.i = 0;
    jack_driver_descriptor_add_parameter(desc, &filler, "hwmon", 'H', JackDriverParamBool, &value, NULL, "Hardware monitoring, if available", NULL);

    value.i = 0;
    jack_driver_descriptor_add_parameter(desc, &filler, "hwmeter", 'M', JackDriverParamBool, &value, NULL, "Hardware metering, if available", NULL);

    value.i = 1;
    jack_driver_descriptor_add_parameter(desc, &filler, "duplex", 'D', JackDriverParamBool, &value, NULL, "Provide both capture and playback ports", NULL);

    value.i = 0;
    jack_driver_descriptor_add_parameter(desc, &filler, "softmode", 's', JackDriverParamBool, &value, NULL, "Soft-mode, no xrun handling", NULL);

    value.i = 0;
    jack_driver_descriptor_add_parameter(desc, &filler, "monitor", 'm', JackDriverParamBool, &value, NULL, "Provide monitor ports for the output", NULL);

    value.c = 'n';
    jack_driver_descriptor_add_parameter(
        desc,
        &filler,
        "dither",
        'z',
        JackDriverParamChar,
        &value,
        jack_constraint_compose_enum_char(
            JACK_CONSTRAINT_FLAG_STRICT | JACK_CONSTRAINT_FLAG_FAKE_VALUE,
            dither_constraint_descr_array),
        "Dithering mode",
        NULL);

    value.ui = 0;
    jack_driver_descriptor_add_parameter(desc, &filler, "inchannels", 'i', JackDriverParamUInt, &value, NULL, "Number of capture channels (defaults to hardware max)", NULL);
    jack_driver_descriptor_add_parameter(desc, &filler, "outchannels", 'o', JackDriverParamUInt, &value, NULL, "Number of playback channels (defaults to hardware max)", NULL);

    value.i = FALSE;
    jack_driver_descriptor_add_parameter(desc, &filler, "shorts", 'S', JackDriverParamBool, &value, NULL, "Try 16-bit samples before 32-bit", NULL);

    value.ui = 0;
    jack_driver_descriptor_add_parameter(desc, &filler, "input-latency", 'I', JackDriverParamUInt, &value, NULL, "Extra input latency (frames)", NULL);
    jack_driver_descriptor_add_parameter(desc, &filler, "output-latency", 'O', JackDriverParamUInt, &value, NULL, "Extra output latency (frames)", NULL);

    strcpy(value.str, "none");
    jack_driver_descriptor_add_parameter(
        desc,
        &filler,
        "midi-driver",
        'X',
        JackDriverParamString,
        &value,
        jack_constraint_compose_enum_str(
            JACK_CONSTRAINT_FLAG_STRICT | JACK_CONSTRAINT_FLAG_FAKE_VALUE,
            midi_constraint_descr_array),
        "ALSA MIDI driver",
        NULL);

    strcpy(value.str, "none");
    jack_driver_descriptor_add_parameter(desc, &filler, "secondary", 'Y', JackDriverParamString, &value, NULL,
        "Secondary ALSA card (repeatable)",
        "Secondary ALSA card, repeatable. Keys: name, device, mode (duplex|capture|playback), channels, rate, nperiods, clock (async|sync), format (auto|float|s32|s16), resampler, optional, follow_hw_rate. Example: name=usb1;device=hw:1;mode=capture;clock=async");
    jack_driver_descriptor_add_parameter(desc, &filler, "exclude-card", 'Z', JackDriverParamString, &value, NULL,
        "ALSA card id to leave out of automatic discovery (repeatable)",
        "ALSA card id (for example Loopback) that automatic discovery must not add. Repeatable. Applies only to automatic cards; a card given with --secondary or --secondary-file is still used. Matching ignores case");

    jack_driver_descriptor_add_parameter(desc, &filler, "secondary-file", 'K', JackDriverParamString, &value, NULL,
        "File with one secondary ALSA card per line",
        "File with one secondary ALSA card per line, read at startup. Same syntax as --secondary. Blank lines and lines starting with # are ignored");

    return desc;
}

SERVER_EXPORT Jack::JackDriverClientInterface* driver_initialize(Jack::JackLockedEngine* engine, Jack::JackSynchro* table, const JSList* params)
{
    jack_nframes_t srate = 48000;
    jack_nframes_t frames_per_interrupt = 1024;
    unsigned long user_nperiods = 2;
    const char *playback_pcm_name = "hw:0";
    const char *capture_pcm_name = "hw:0";
    int hw_monitoring = FALSE;
    int hw_metering = FALSE;
    int capture = FALSE;
    int playback = FALSE;
    int soft_mode = FALSE;
    int monitor = FALSE;
    DitherAlgorithm dither = None;
    int user_capture_nchnls = 0;
    int user_playback_nchnls = 0;
    int shorts_first = FALSE;
    jack_nframes_t systemic_input_latency = 0;
    jack_nframes_t systemic_output_latency = 0;
    const JSList * node;
    const jack_driver_param_t * param;
    const char *midi_driver = "none";
    std::vector<Jack::JackAlsaSecondaryConfig> secondaries;
    std::vector<std::string> excluded_cards;

    for (node = params; node; node = jack_slist_next (node)) {
        param = (const jack_driver_param_t *) node->data;

        switch (param->character) {

            case 'C':
                capture = TRUE;
                if (strcmp (param->value.str, "none") != 0) {
                    capture_pcm_name = strdup (param->value.str);
                    jack_log("capture device %s", capture_pcm_name);
                }
                break;

            case 'P':
                playback = TRUE;
                if (strcmp (param->value.str, "none") != 0) {
                    playback_pcm_name = strdup (param->value.str);
                    jack_log("playback device %s", playback_pcm_name);
                }
                break;

            case 'D':
                playback = TRUE;
                capture = TRUE;
                break;

            case 'd':
                if (strcmp (param->value.str, "none") != 0) {
                    playback_pcm_name = strdup (param->value.str);
                    capture_pcm_name = strdup (param->value.str);
                    jack_log("playback device %s", playback_pcm_name);
                    jack_log("capture device %s", capture_pcm_name);
                }
                break;

            case 'H':
                hw_monitoring = param->value.i;
                break;

            case 'm':
                monitor = param->value.i;
                break;

            case 'M':
                hw_metering = param->value.i;
                break;

            case 'r':
                srate = param->value.ui;
                jack_log("apparent rate = %d", srate);
                break;

            case 'p':
                frames_per_interrupt = param->value.ui;
                jack_log("frames per period = %d", frames_per_interrupt);
                break;

            case 'n':
                user_nperiods = param->value.ui;
                if (user_nperiods < 2) {    /* enforce minimum value */
                    user_nperiods = 2;
                }
                break;

            case 's':
                soft_mode = param->value.i;
                break;

            case 'z':
                if (dither_opt (param->value.c, &dither)) {
                    return NULL;
                }
                break;

            case 'i':
                user_capture_nchnls = param->value.ui;
                break;

            case 'o':
                user_playback_nchnls = param->value.ui;
                break;

            case 'S':
                shorts_first = param->value.i;
                break;

            case 'I':
                systemic_input_latency = param->value.ui;
                break;

            case 'O':
                systemic_output_latency = param->value.ui;
                break;

            case 'X':
                midi_driver = strdup(param->value.str);
                break;

            case 'Y':
                if (strcmp (param->value.str, "none") != 0) {
                    Jack::JackAlsaSecondaryConfig config;
                    std::string error;
                    if (!Jack::JackAlsaSecondaryConfigParse(param->value.str, &config, &error)) {
                        jack_error("invalid --secondary \"%s\": %s", param->value.str, error.c_str());
                        return NULL;
                    }
                    secondaries.push_back(config);
                }
                break;

            case 'Z':
                if (strcmp (param->value.str, "none") != 0 && param->value.str[0] != '\0') {
                    excluded_cards.push_back(param->value.str);
                }
                break;

            case 'K':
                if (strcmp (param->value.str, "none") != 0) {
                    std::string error;
                    if (!Jack::JackAlsaSecondaryConfigLoadFile(param->value.str, &secondaries, &error)) {
                        jack_error("%s", error.c_str());
                        return NULL;
                    }
                }
                break;
        }
    }

    /* duplex is the default */
    if (!capture && !playback) {
        capture = TRUE;
        playback = TRUE;
    }

    Jack::JackAlsaDriver* alsa_driver = new Jack::JackAlsaDriver("system", "alsa_pcm", engine, table);
    Jack::JackDriverClientInterface* threaded_driver = new Jack::JackThreadedDriver(alsa_driver);
    // Special open for ALSA driver...
    if (alsa_driver->Open(frames_per_interrupt, user_nperiods, srate, hw_monitoring, hw_metering, capture, playback, dither, soft_mode, monitor,
                          user_capture_nchnls, user_playback_nchnls, shorts_first, capture_pcm_name, playback_pcm_name,
                          systemic_input_latency, systemic_output_latency, midi_driver,
                          secondaries, excluded_cards) == 0) {
        return threaded_driver;
    } else {
        delete threaded_driver; // Delete the decorated driver
        return NULL;
    }
}

#ifdef __cplusplus
}
#endif



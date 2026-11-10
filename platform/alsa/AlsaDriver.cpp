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

#define __STDC_FORMAT_MACROS // For inttypes.h to work in C++

#include <errno.h>
#include <math.h>
#include <memory.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <string_view>
#include <thread>
#include <utility>

#include "AlsaDriver.h"
#include "AlsaEnumerate.h"
#include "CString.h"
#include "ClientControl.h"
#include "CompilerDeps.h"
#include "DriverModule.h"
#include "EngineControl.h"
#include "GraphManager.h"
#include "LockedEngine.h"
#include "PosixThread.h"
#include "ServerGlobals.h"
#include "ServerPort.h"
#include "Tools.h"

static std::array<struct jack_constraint_enum_char_descriptor, 5> gDitherConstraintDescrArray = { { { 'n', "none" },
    { 'r', "rectangular" },
    { 's', "shaped" },
    { 't', "triangular" },
    { 0 } } };

namespace jackmini {

static std::atomic<bool> gDeviceReservationLoopRunning { false };

static void* onDeviceReservationLoop(void*)
{
    while (gDeviceReservationLoopRunning && ServerGlobals::s_onDeviceReservationLoop != nullptr) {
        ServerGlobals::s_onDeviceReservationLoop();
        usleep(50 * 1000);
    }

    return nullptr;
}

// Per-instance thunks installed in the PCM cycle.  Each callback gets the
// driver it belongs to, so the primary card and the secondary cards each reach
// their own object.

static void alsaReadInputThunk(void* arg, jackmini::alsa::PcmCycle&, const jackmini::alsa::MappedFrames& mapped,
    std::uint32_t origFrames, std::ptrdiff_t nread)
{
    static_cast<AlsaDriver*>(arg)->readInputAux(origFrames, mapped, nread);
}

static void alsaClearOutputThunk(void* arg)
{
    static_cast<AlsaDriver*>(arg)->clearOutputAux();
}

static void alsaWriteOutputThunk(void* arg, jackmini::alsa::PcmCycle&, const jackmini::alsa::MappedFrames& mapped,
    std::uint32_t origFrames, std::ptrdiff_t nwritten)
{
    static_cast<AlsaDriver*>(arg)->writeOutputAux(origFrames, mapped, nwritten);
}

static void alsaSetTimeThunk(void* arg, TimeC time)
{
    static_cast<AlsaDriver*>(arg)->setTimetAux(time);
}

static std::int32_t alsaRestartThunk(void* arg)
{
    return static_cast<AlsaDriver*>(arg)->restartFromRT();
}

std::int32_t AlsaDriver::restartFromRT()
{
    // Stop and Start run inside one RT cycle here.  The RT side stays marked as
    // running throughout, so a reconfiguration waits for the cycle to finish
    // rather than freeing buffers mid-restart.
    m_restarting.store(true);
    std::int32_t res = stop();
    if (res == 0) {
        res = start();
    }
    m_restarting.store(false);
    return res;
}

static jackmini::alsa::PcmCycle::Callbacks alsaCallbacks(AlsaDriver* driver)
{
    jackmini::alsa::PcmCycle::Callbacks callbacks;
    callbacks.m_arg = driver;
    callbacks.m_readInput = alsaReadInputThunk;
    callbacks.m_clearOutput = alsaClearOutputThunk;
    callbacks.m_writeOutput = alsaWriteOutputThunk;
    callbacks.m_setTime = alsaSetTimeThunk;
    callbacks.m_restart = alsaRestartThunk;
    return callbacks;
}

std::int32_t AlsaDriver::openPrimary(NframesC framesPerCycle)
{
    jackmini::alsa::Request request = m_request;
    request.m_framesPerCycle = framesPerCycle;
    if (m_session.open(m_captureDevice.empty() ? nullptr : m_captureDevice.c_str(),
            m_playbackDevice.empty() ? nullptr : m_playbackDevice.c_str(), request, m_dither)
        < 0) {
        return -1;
    }
    m_request.m_rate = request.m_rate;
    return 0;
}

std::int32_t AlsaDriver::setBufferSize(NframesC bufferSize)
{
    jack_log("JackAlsaDriver::SetBufferSize %ld", bufferSize);

    // Secondary card periods are derived from the primary buffer size, so it
    // cannot change while secondary cards exist.
    if (!m_secondaryCards.empty()) {
        jack_error("buffer size cannot change while secondary ALSA cards are in use");
        return -1;
    }
    m_session.close();
    std::int32_t res = openPrimary(bufferSize);

    if (res == 0) { // update fEngineControl and fGraphManager
        AudioDriver::setBufferSize(bufferSize); // Generic change, never fails
        // ALSA specific
        updateLatencies();
    } else {
        // Restore old values
        openPrimary(m_engineControl->m_bufferSize);
    }

    return res;
}

AlsaSecondaryCard* AlsaDriver::findCard(std::int32_t id)
{
    std::lock_guard<std::mutex> lock(m_cardsMutex);
    for (size_t i = 0; i < m_secondaryCards.size(); i++) {
        if (m_secondaryCards[i]->id() == id) {
            return m_secondaryCards[i];
        }
    }
    return nullptr;
}

// Registers a card's ports and puts it in a slot.  Called on the server thread
// at Attach, before the RT thread starts.
bool AlsaDriver::promoteCard(AlsaSecondaryCard* card)
{
    std::array<char, kRealJackPortNameSize + 1> name { };
    PortIdC portIndex;
    std::vector<PortIdC>& capturePorts = card->capturePorts();
    std::vector<PortIdC>& playbackPorts = card->playbackPorts();

    for (std::int32_t i = 0; i < card->captureChannels(); i++) {
        jackmini::formatCString(name, "{}:capture_{}_{}", m_clientControl.m_name.data(), card->portBase().c_str(), i + 1);
        if (!m_engine->portRegister(m_clientControl.m_refNum, name.data(), kDefaultAudioType, (PortFlagsC)kCaptureDriverFlags, m_engineControl->m_bufferSize, &portIndex)) {
            jack_error("driver: cannot register port for %s", name);
            unregisterCardPorts(card);
            return false;
        }
        capturePorts[i] = portIndex;
    }

    for (std::int32_t i = 0; i < card->playbackChannels(); i++) {
        jackmini::formatCString(name, "{}:playback_{}_{}", m_clientControl.m_name.data(), card->portBase().c_str(), i + 1);
        if (!m_engine->portRegister(m_clientControl.m_refNum, name.data(), kDefaultAudioType, (PortFlagsC)kPlaybackDriverFlags, m_engineControl->m_bufferSize, &portIndex)) {
            jack_error("driver: cannot register port for %s", name);
            unregisterCardPorts(card);
            return false;
        }
        playbackPorts[i] = portIndex;
    }

    if (m_slots.assign(card) < 0) {
        jack_error("secondary card '%s' cannot be used: all %zu secondary card slots are in use",
            card->name().c_str(), AlsaCardSlots::capacity());
        unregisterCardPorts(card);
        return false;
    }
    return true;
}

void AlsaDriver::unregisterCardPorts(AlsaSecondaryCard* card)
{
    std::vector<PortIdC>& capturePorts = card->capturePorts();
    std::vector<PortIdC>& playbackPorts = card->playbackPorts();
    for (size_t i = 0; i < capturePorts.size(); i++) {
        if (capturePorts[i]) {
            m_engine->portUnRegister(m_clientControl.m_refNum, capturePorts[i]);
            capturePorts[i] = 0;
        }
    }
    for (size_t i = 0; i < playbackPorts.size(); i++) {
        if (playbackPorts[i]) {
            m_engine->portUnRegister(m_clientControl.m_refNum, playbackPorts[i]);
            playbackPorts[i] = 0;
        }
    }
}

// Waits until the RT thread has finished two cycles, so it can no longer hold
// a reference to a card taken out of its slot.  Returns false on timeout.
bool AlsaDriver::waitForRTSafePoint()
{
    if (!m_rTRunning.load()) {
        return true;
    }
    std::uint64_t start = m_rTEpoch.load(std::memory_order_acquire);
    for (std::int32_t i = 0; i < 2000; i++) {
        if (m_rTEpoch.load(std::memory_order_acquire) >= start + 2) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

// Takes a card out of service and frees it.  Runs on the worker thread.  The
// card leaves the RT slots first; only then is the RT thread waited out, so
// no cycle still uses the card when its ports are removed and it is freed.
void AlsaDriver::retireCard(AlsaSecondaryCard* card)
{
    {
        std::lock_guard<std::mutex> lock(m_cardsMutex);
        for (size_t i = 0; i < m_secondaryCards.size(); i++) {
            if (m_secondaryCards[i] == card) {
                m_secondaryCards.erase(m_secondaryCards.begin() + i);
                break;
            }
        }
        m_slots.remove(card);
    }

    if (!waitForRTSafePoint()) {
        // The RT thread did not reach a safe point.  Leak the card rather than free
        // memory the RT thread may still use.
        jack_error("secondary card '%s' could not be removed safely; it is left unused",
            card->name().c_str());
        return;
    }

    if (m_controlMonitor != nullptr) {
        m_controlMonitor->removeWatches(card->id());
    }
    unregisterCardPorts(card);
    delete card; // stops the card's thread, closes its controls and stream
    updateSecondaryLatencies();
}

// The primary card's clock drives the server, so when it fails the server cannot
// go on.  Clients are told why, and the server is stopped as on Ctrl-C.  Only the
// first failure is reported.
void AlsaDriver::primaryFailed(std::int32_t status)
{
    if (m_primaryFailed.exchange(true)) {
        return;
    }
    const char* name = m_captureDriverName[0] != '\0' ? m_captureDriverName.data() : m_playbackDriverName.data();
    std::string message = std::string("the primary ALSA device ") + name + " failed (status " + std::to_string(status) + "); the server stops";
    jack_error("%s", message.c_str());
    notifyFailure(kBackendError, message.c_str());
    Tools::killServer();
}

void AlsaDriver::handleCardFailure(std::int32_t cardId)
{
    AlsaSecondaryCard* card = findCard(cardId);
    if (card == nullptr) {
        return;
    }
    jack_error("secondary card '%s' failed; it has been removed and its ports are gone",
        card->name().c_str());
    retireCard(card);
}

void AlsaDriver::updateSecondaryLatencies()
{
    std::lock_guard<std::mutex> lock(m_cardsMutex);
    for (size_t c = 0; c < m_secondaryCards.size(); c++) {
        AlsaSecondaryCard* card = m_secondaryCards[c];
        LatencyRangeC range;

        range.m_min = range.m_max = card->captureLatency();
        for (std::int32_t i = 0; i < card->captureChannels(); i++) {
            m_graphManager->getPort(card->capturePorts()[i])->setLatencyRange(kCaptureLatency, &range);
        }

        range.m_min = range.m_max = card->playbackLatency();
        for (std::int32_t i = 0; i < card->playbackChannels(); i++) {
            m_graphManager->getPort(card->playbackPorts()[i])->setLatencyRange(kPlaybackLatency, &range);
        }
    }
}

void AlsaDriver::updateLatencies()
{
    LatencyRangeC range;
    const NframesC frames = m_session.framesPerCycle();

    for (std::int32_t i = 0; i < m_captureChannels; i++) {
        range.m_min = range.m_max = frames + m_captureFrameLatency;
        m_graphManager->getPort(m_capturePortList[i])->setLatencyRange(kCaptureLatency, &range);
    }

    for (std::int32_t i = 0; i < m_playbackChannels; i++) {
        // Add one buffer more latency if "async" mode is used...
        range.m_min = range.m_max = (frames * (m_request.m_periods - 1)) + ((m_engineControl->m_syncMode) ? 0 : m_engineControl->m_bufferSize) + m_playbackFrameLatency;
        m_graphManager->getPort(m_playbackPortList[i])->setLatencyRange(kPlaybackLatency, &range);
        // Monitor port
        if (m_withMonitorPorts) {
            range.m_min = range.m_max = frames;
            m_graphManager->getPort(m_monitorPortList[i])->setLatencyRange(kCaptureLatency, &range);
        }
    }
}

std::int32_t AlsaDriver::attach()
{
    ServerPort* port;
    PortIdC portIndex;
    std::uint64_t portFlags = (std::uint64_t)kCaptureDriverFlags;
    std::array<char, kRealJackPortNameSize + 1> name { };
    std::array<char, kRealJackPortNameSize + 1> alias { };

    assert(fCaptureChannels < kDriverPortNum);
    assert(fPlaybackChannels < kDriverPortNum);

    // ALSA driver may have changed the values
    AudioDriver::setBufferSize(m_session.framesPerCycle());
    AudioDriver::setSampleRate(m_session.rate());

    jack_log("JackAlsaDriver::Attach fBufferSize %ld fSampleRate %ld", m_engineControl->m_bufferSize, m_engineControl->m_sampleRate);

    for (std::int32_t i = 0; i < m_captureChannels; i++) {
        jackmini::formatCString(alias, "{}:{}:out{}", m_aliasName.data(), m_captureDriverName.data(), i + 1);
        jackmini::formatCString(name, "{}:capture_{}", m_clientControl.m_name.data(), i + 1);
        if (!m_engine->portRegister(m_clientControl.m_refNum, name.data(), kDefaultAudioType, (PortFlagsC)portFlags, m_engineControl->m_bufferSize, &portIndex)) {
            jack_error("driver: cannot register port for %s", name);
            return -1;
        }
        port = m_graphManager->getPort(portIndex);
        port->setAlias(alias.data());
        m_capturePortList[i] = portIndex;
        jack_log("JackAlsaDriver::Attach fCapturePortList[i] %ld ", portIndex);
    }

    portFlags = (std::uint64_t)kPlaybackDriverFlags;

    for (std::int32_t i = 0; i < m_playbackChannels; i++) {
        jackmini::formatCString(alias, "{}:{}:in{}", m_aliasName.data(), m_playbackDriverName.data(), i + 1);
        jackmini::formatCString(name, "{}:playback_{}", m_clientControl.m_name.data(), i + 1);
        if (!m_engine->portRegister(m_clientControl.m_refNum, name.data(), kDefaultAudioType, (PortFlagsC)portFlags, m_engineControl->m_bufferSize, &portIndex)) {
            jack_error("driver: cannot register port for %s", name);
            return -1;
        }
        port = m_graphManager->getPort(portIndex);
        port->setAlias(alias.data());
        m_playbackPortList[i] = portIndex;
        jack_log("JackAlsaDriver::Attach fPlaybackPortList[i] %ld ", portIndex);

        // Monitor ports
        if (m_withMonitorPorts) {
            jack_log("Create monitor port");
            jackmini::formatCString(name, "{}:monitor_{}", m_clientControl.m_name.data(), i + 1);
            if (!m_engine->portRegister(m_clientControl.m_refNum, name.data(), kDefaultAudioType, kMonitorDriverFlags, m_engineControl->m_bufferSize, &portIndex)) {
                jack_error("ALSA: cannot register monitor port for %s", name);
            } else {
                m_monitorPortList[i] = portIndex;
            }
        }
    }

    {
        std::vector<AlsaSecondaryCard*> cards;
        {
            std::lock_guard<std::mutex> lock(m_cardsMutex);
            cards = m_secondaryCards;
        }
        for (size_t c = 0; c < cards.size(); c++) {
            if (!promoteCard(cards[c])) {
                return -1;
            }
        }
    }

    updateLatencies();
    updateSecondaryLatencies();

    return 0;
}

std::int32_t AlsaDriver::detach()
{
    // Stop the threads that can change the card list before taking the cards out.
    stopControlMonitoring();
    {
        std::lock_guard<std::mutex> lock(m_cardsMutex);
        for (size_t c = 0; c < m_secondaryCards.size(); c++) {
            m_slots.remove(m_secondaryCards[c]);
            unregisterCardPorts(m_secondaryCards[c]);
        }
    }

    return AudioDriver::detach();
}

char* getControlDeviceName(const char* deviceName)
{
    char* ctlName;
    const char* comma;

    // the user wants a hw or plughw device, the ctl name
    // should be hw:x where x is the card identification.
    // We skip the subdevice suffix that starts with comma

    if (strncasecmp(deviceName, "plughw:", 7) == 0) {
        // skip the "plug" prefix"
        deviceName += 4;
    }

    comma = strchr(deviceName, ',');
    if (comma == nullptr) {
        ctlName = strdup(deviceName);
        if (ctlName == nullptr) {
            jack_error("strdup(\"%s\") failed.", deviceName);
        }
    } else {
        ctlName = strndup(deviceName, comma - deviceName);
        if (ctlName == nullptr) {
            jack_error("strndup(\"%s\", %u) failed.", deviceName, (std::uint32_t)(comma - deviceName));
        }
    }

    return ctlName;
}

static std::int32_t cardToNum(const char* device)
{
    std::int32_t err;
    char* ctlName;
    snd_ctl_card_info_t* cardInfo;
    snd_ctl_t* ctlHandle;
    std::int32_t i = -1;

    snd_ctl_card_info_alloca(&cardInfo);

    ctlName = getControlDeviceName(device);
    if (ctlName == nullptr) {
        jack_error("get_control_device_name() failed.");
        goto fail;
    }

    if ((err = snd_ctl_open(&ctlHandle, ctlName, 0)) < 0) {
        jack_error("control open \"%s\" (%s)", ctlName,
            snd_strerror(err));
        goto free;
    }

    if ((err = snd_ctl_card_info(ctlHandle, cardInfo)) < 0) {
        jack_error("control hardware info \"%s\" (%s)",
            device, snd_strerror(err));
        goto close;
    }

    i = snd_ctl_card_info_get_card(cardInfo);

close:
    snd_ctl_close(ctlHandle);

free:
    free(ctlName);

fail:
    return i;
}

// PortBase of a configuration: its name and device number.  Two devices of one
// card have different port bases even though their card name is the same.
static std::string portBaseOf(const AlsaSecondaryConfig& cfg)
{
    const std::string& pcm = cfg.m_captureDevice.empty() ? cfg.m_playbackDevice : cfg.m_captureDevice;
    return cfg.m_name + "_d" + std::to_string(alsaPcmDeviceNumber(pcm));
}

// Finds the card and device of a PCM name among the enumerated devices.  Accepts
// "hw:<index>[,n]", "hw:<id>[,n]" and "hw:CARD=<id>,DEV=<n>".
static bool resolvePcm(const std::string& pcm, const std::vector<AlsaPcmInfo>& pcms,
    std::int32_t* card, std::int32_t* device)
{
    std::string rest = pcm.find(':') == std::string::npos ? pcm : pcm.substr(pcm.find(':') + 1);
    std::string key;
    if (rest.compare(0, 5, "CARD=") == 0) {
        key = rest.substr(5, rest.find(',') == std::string::npos ? std::string::npos : rest.find(',') - 5);
    } else {
        key = rest.substr(0, rest.find(','));
    }
    bool numeric = !key.empty() && key.find_first_not_of("0123456789") == std::string::npos;
    *device = alsaPcmDeviceNumber(pcm);
    for (size_t i = 0; i < pcms.size(); i++) {
        if ((numeric && pcms[i].m_cardNum == jackmini::parseLeadingInteger<std::int32_t>(key)) || (!numeric && pcms[i].m_cardId == key)) {
            *card = pcms[i].m_cardNum;
            return true;
        }
    }
    return false;
}

// The configured cards, followed by an automatic card for every other PCM device
// that has a stream.  The primary's device and the devices configured explicitly
// are not added again.  Automatic cards stop at the slot limit.
static std::vector<AlsaSecondaryConfig> buildSecondaryPlan(
    const std::vector<AlsaSecondaryConfig>& configured,
    const char* primaryCapture, const char* primaryPlayback,
    const std::vector<AlsaPcmInfo>& known, bool report,
    const std::vector<std::string>& excluded,
    std::vector<AlsaPcmInfo>* listing)
{
    std::vector<AlsaSecondaryConfig> plan = configured;
    std::vector<AlsaPcmInfo> pcms = alsaEnumeratePcms(known, report);
    if (listing != nullptr) {
        *listing = pcms;
    }

    std::vector<std::pair<std::int32_t, std::int32_t>> claimed;
    std::array<const char*, 2> names = { primaryCapture, primaryPlayback };
    std::vector<std::string> used;
    for (size_t i = 0; i < 2; i++) {
        std::int32_t card, device;
        if (names[i] != nullptr && resolvePcm(names[i], pcms, &card, &device)) {
            claimed.push_back(std::make_pair(card, device));
        }
    }
    for (size_t i = 0; i < configured.size(); i++) {
        std::int32_t card, device;
        std::array pcmsFor = { configured[i].m_captureDevice, configured[i].m_playbackDevice };
        for (size_t d = 0; d < 2; d++) {
            if (!pcmsFor[d].empty() && resolvePcm(pcmsFor[d], pcms, &card, &device)) {
                claimed.push_back(std::make_pair(card, device));
            }
        }
        used.push_back(portBaseOf(configured[i]));
    }

    for (size_t i = 0; i < pcms.size(); i++) {
        const AlsaPcmInfo& pcm = pcms[i];
        if (std::find(claimed.begin(), claimed.end(), std::make_pair(pcm.m_cardNum, pcm.m_device)) != claimed.end()) {
            continue;
        }
        if (alsaCardExcluded(pcm.m_cardId, excluded)) {
            if (report) {
                jack_info("ALSA device %s is not added: its card %s is excluded", pcm.m_pcm.c_str(), pcm.m_cardId.c_str());
            }
            continue;
        }
        if (plan.size() >= AlsaCardSlots::capacity()) {
            jack_info("ALSA device %s is not added: all %zu secondary card slots are in use",
                pcm.m_pcm.c_str(), AlsaCardSlots::capacity());
            continue;
        }

        AlsaSecondaryConfig cfg;
        cfg.m_name = alsaSecondarySanitizeName(pcm.m_cardId);
        std::transform(cfg.m_name.begin(), cfg.m_name.end(), cfg.m_name.begin(), ::tolower);
        if (pcm.m_playback) {
            cfg.m_playbackDevice = pcm.m_pcm;
            cfg.m_playbackChannels = pcm.m_playbackChannels < 12 ? pcm.m_playbackChannels : 12;
        }
        if (pcm.m_capture) {
            cfg.m_captureDevice = pcm.m_pcm;
            cfg.m_captureChannels = pcm.m_captureChannels < 12 ? pcm.m_captureChannels : 12;
        }
        cfg.m_mode = (pcm.m_playback && pcm.m_capture) ? kSecondaryModeDuplex
                                                       : (pcm.m_capture ? kSecondaryModeCapture : kSecondaryModePlayback);
        cfg.m_automatic = true;

        // Two devices of one card share a card name: their port bases still differ.
        if (std::find(used.begin(), used.end(), portBaseOf(cfg)) != used.end()) {
            jack_error("ALSA device %s is not added: its name %s is already used",
                pcm.m_pcm.c_str(), portBaseOf(cfg).c_str());
            continue;
        }
        used.push_back(portBaseOf(cfg));
        if (report) {
            jack_info("automatic secondary card for %s: %s, %d/%d channels",
                pcm.m_pcm.c_str(), portBaseOf(cfg).c_str(), cfg.m_captureChannels, cfg.m_playbackChannels);
        }
        plan.push_back(cfg);
    }
    return plan;
}

std::int32_t AlsaDriver::open(NframesC nframes,
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
    const std::vector<std::string>& excludedCards)
{
    // Generic JackAudioDriver Open
    if (AudioDriver::open(nframes, samplerate, capturing, playing,
            inchannels, outchannels, monitor, captureDriverName, playbackDriverName,
            captureLatency, playbackLatency)
        != 0) {
        return -1;
    }

    if (ServerGlobals::s_onDeviceAcquire != nullptr) {
        std::int32_t captureCard = cardToNum(captureDriverName);
        std::int32_t playbackCard = cardToNum(playbackDriverName);
        std::array<char, 32> audioName { };

        if (captureCard >= 0) {
            jackmini::formatCString(audioName, "Audio{}", captureCard);
            if (!ServerGlobals::s_onDeviceAcquire(audioName.data())) {
                jack_error("Audio device %s cannot be acquired...", captureDriverName);
                return -1;
            }
        }

        if (playbackCard >= 0 && playbackCard != captureCard) {
            jackmini::formatCString(audioName, "Audio{}", playbackCard);
            if (!ServerGlobals::s_onDeviceAcquire(audioName.data())) {
                jack_error("Audio device %s cannot be acquired...", playbackDriverName);
                if (captureCard >= 0) {
                    jackmini::formatCString(audioName, "Audio{}", captureCard);
                    ServerGlobals::s_onDeviceRelease(audioName.data());
                }
                return -1;
            }
        }
    }

    // One server per card: lock the primary devices before touching them.
    if (capturing && !lockCard(captureDriverName, true)) {
        close();
        return -1;
    }
    if (playing && !lockCard(playbackDriverName, true)) {
        close();
        return -1;
    }

    m_request = jackmini::alsa::Request();
    m_request.m_rate = samplerate;
    m_request.m_periods = userNperiods;
    m_request.m_captureChannels = inchannels;
    m_request.m_playbackChannels = outchannels;
    m_request.m_sampleBytes = shortsFirst ? 2 : 4;
    m_request.m_softMode = softMode;
    m_dither = dither;
    m_captureDevice = capturing ? captureDriverName : "";
    m_playbackDevice = playing ? playbackDriverName : "";
    m_captureFrameLatency = captureLatency;
    m_playbackFrameLatency = playbackLatency;
    m_session.setCallbacks(alsaCallbacks(this));
    if (openPrimary(nframes) < 0) {
        close();
        return -1;
    }

    // ALSA driver may have changed the in/out values
    m_captureChannels = m_session.hasCapture() ? m_session.info(jackmini::alsa::Stream::kCapture).m_channels : 0;
    m_playbackChannels = m_session.hasPlayback() ? m_session.info(jackmini::alsa::Stream::kPlayback).m_channels : 0;
    m_configuredSecondaries = secondaries;
    m_excludedCards = excludedCards;
    m_reportedFailures.clear();
    std::vector<AlsaPcmInfo> listing;
    std::vector<AlsaSecondaryConfig> plan = buildSecondaryPlan(secondaries, captureDriverName, playbackDriverName,
        m_knownPcms, true, m_excludedCards, &listing);
    m_knownPcms = listing;

    // Always created: a card can appear after startup, and the watchers must
    // exist to add it.
    m_reconfigWorker = new AlsaReconfigWorker(this);
    m_controlMonitor = new AlsaControlMonitor(m_reconfigWorker);
    m_hotplug = new AlsaHotplug(m_reconfigWorker);

    for (size_t i = 0; i < plan.size(); i++) {
        const AlsaSecondaryConfig& cfg = plan[i];

        for (size_t j = 0; j < i; j++) {
            if (portBaseOf(plan[j]) == portBaseOf(cfg)) {
                jack_error("secondary ALSA card '%s' is configured more than once", portBaseOf(cfg).c_str());
                close();
                return -1;
            }
        }

        if (!cfg.m_automatic && (cfg.m_captureDevice == captureDriverName || cfg.m_captureDevice == playbackDriverName || cfg.m_playbackDevice == captureDriverName || cfg.m_playbackDevice == playbackDriverName)) {
            jack_error("secondary ALSA card '%s' uses a device already used by the primary card", cfg.m_name.c_str());
            close();
            return -1;
        }

        // One server may use several devices of one card: rate elements are named per
        // device.  Other servers are still excluded by the card lock.
        if (!lockCard(cfg.m_captureDevice.empty() ? cfg.m_playbackDevice.c_str() : cfg.m_captureDevice.c_str(), true)) {
            if (cfg.m_automatic) {
                jack_error("automatic secondary card '%s' is skipped: its card is held by another server",
                    portBaseOf(cfg).c_str());
                continue;
            }
            close();
            return -1;
        }

        ResamplerFactory* factory = ResamplerFactory::find(cfg.m_resampler.c_str());
        if (factory == nullptr) {
            jack_error("secondary card '%s': resampler '%s' is not available",
                cfg.m_name.c_str(), cfg.m_resampler.c_str());
            close();
            return -1;
        }

        AlsaSecondaryCard* card = createCard(cfg, factory);
        if (card == nullptr) {
            if (cfg.m_optional || cfg.m_automatic) {
                jack_error("skipping secondary ALSA card '%s'", portBaseOf(cfg).c_str());
                continue;
            }
            close();
            return -1;
        }
        m_secondaryCards.push_back(card);
    }

    for (size_t i = 0; i < m_secondaryCards.size(); i++) {
        AlsaSecondaryCard* card = m_secondaryCards[i];
        for (std::int32_t d = 0; d < 2; d++) {
            AlsaRateDirection dir = (AlsaRateDirection)d;
            if (card->rateElementValid(dir) && m_controlMonitor->addWatch(card->id(), card->controlName(), card->rateElement(dir), dir) < 0) {
                jack_error("secondary card '%s': rate control changes will not be seen",
                    card->name().c_str());
            }
        }
    }

    if (ServerGlobals::s_onDeviceReservationLoop != nullptr) {
        gDeviceReservationLoopRunning = true;
        if (PosixThread::startImp(&m_reservationLoopThread, 0, 0, onDeviceReservationLoop, nullptr) != 0) {
            gDeviceReservationLoopRunning = false;
        }
    }

    return 0;
}

// shared: the primary's capture and playback may use one card.  A secondary
// card must be a card this server does not already use, since its rate
// control element would clash with the other device's.
bool AlsaDriver::lockCard(const char* pcmName, bool shared)
{
    std::string ctl = alsaControlDeviceName(pcmName);
    if (ctl.empty()) {
        return false;
    }
    for (size_t i = 0; i < m_cardLockNames.size(); i++) {
        if (ctl == m_cardLockNames[i]) {
            if (shared) {
                return true;
            }
            jack_error("secondary ALSA card %s is already used by this server", ctl.c_str());
            return false;
        }
    }
    std::int32_t fd = alsaControlLockCard(ctl);
    if (fd < 0) {
        return false;
    }
    m_cardLocks.push_back(fd);
    m_cardLockNames.push_back(ctl);
    return true;
}

void AlsaDriver::unlockCards()
{
    for (size_t i = 0; i < m_cardLocks.size(); i++) {
        alsaControlUnlockCard(m_cardLocks[i]);
    }
    m_cardLocks.clear();
    m_cardLockNames.clear();
}

std::int32_t AlsaDriver::close()
{
    // Generic audio driver close
    std::int32_t res = AudioDriver::close();

    // Stop every thread that can change the cards before they are freed.
    stopControlMonitoring();
    unlockCards();

    // The hot-plug listener, monitor and worker refer to the cards, so they go first.
    delete m_hotplug;
    m_hotplug = nullptr;
    delete m_controlMonitor;
    m_controlMonitor = nullptr;
    delete m_reconfigWorker;
    m_reconfigWorker = nullptr;

    for (size_t i = 0; i < m_secondaryCards.size(); i++) {
        delete m_secondaryCards[i];
    }
    m_secondaryCards.clear();

    m_session.close();

    if (gDeviceReservationLoopRunning) {
        gDeviceReservationLoopRunning = false;
        PosixThread::stopImp(m_reservationLoopThread);
    }

    if (ServerGlobals::s_onDeviceRelease != nullptr) {
        std::array<char, 32> audioName { };
        std::int32_t captureCard = cardToNum(m_captureDriverName.data());
        if (captureCard >= 0) {
            jackmini::formatCString(audioName, "Audio{}", captureCard);
            ServerGlobals::s_onDeviceRelease(audioName.data());
        }

        std::int32_t playbackCard = cardToNum(m_playbackDriverName.data());
        if (playbackCard >= 0 && playbackCard != captureCard) {
            jackmini::formatCString(audioName, "Audio{}", playbackCard);
            ServerGlobals::s_onDeviceRelease(audioName.data());
        }
    }

    return res;
}

std::int32_t AlsaDriver::start()
{
    std::int32_t res = AudioDriver::start();
    if (res >= 0) {
        res = m_session.start();
        if (res < 0) {
            AudioDriver::stop();
        } else {
            std::lock_guard<std::mutex> lock(m_cardsMutex);
            for (size_t i = 0; i < m_secondaryCards.size(); i++) {
                if (m_secondaryCards[i]->start() < 0) {
                    res = -1;
                    break;
                }
            }
            if (res < 0) {
                stopSecondaryCards();
                m_session.stop();
                AudioDriver::stop();
            } else {
                // An xrun recovery restarts the streams only.  The monitor, the hot-plug listener and the reconfig
                // worker live for the whole driver, so a restarting driver must not create them again.
                if (!m_restarting.load()) {
                    startControlMonitoring();
                    m_rTRunning.store(true);
                }
            }
        }
    }
    return res;
}

void AlsaDriver::stopSecondaryCards()
{
    std::lock_guard<std::mutex> lock(m_cardsMutex);
    for (size_t i = 0; i < m_secondaryCards.size(); i++) {
        m_secondaryCards[i]->stop();
    }
}

void AlsaDriver::startControlMonitoring()
{
    if (m_controlMonitor == nullptr) {
        return;
    }
    if (m_reconfigWorker->start() < 0 || m_controlMonitor->start() < 0) {
        // Losing rate-control events leaves the cards running at their current
        // rates; the primary card is unaffected.
        jack_error("secondary card rate controls are not being monitored");
    }
    if (m_hotplug != nullptr && m_hotplug->start() < 0) {
        jack_error("hot-plug is not active; cards will not be added or removed while running");
    }
}

void AlsaDriver::stopControlMonitoring()
{
    if (m_controlMonitor == nullptr) {
        return;
    }
    if (m_hotplug != nullptr) {
        m_hotplug->stop();
    }
    m_controlMonitor->stop();
    m_reconfigWorker->stop();
}

// The identity of a card for reconciliation: its port base and its mode.  A card
// whose device changed mode is a different card.
static std::string cardKeyOf(const AlsaSecondaryConfig& cfg)
{
    return portBaseOf(cfg) + "|" + std::to_string(std::to_underlying(cfg.m_mode));
}

// Runs on the worker thread once a burst of sound events has settled.  Cards
// whose device is gone are retired; devices that are now present are added.
// A device that fails to open is not retried until the next event.
void AlsaDriver::handleSoundChanged()
{
    if (!m_session.isOpen()) {
        return;
    }

    std::vector<AlsaPcmInfo> listing;
    std::vector<AlsaSecondaryConfig> plan = buildSecondaryPlan(m_configuredSecondaries, m_captureDriverName.data(),
        m_playbackDriverName.data(), m_knownPcms, false,
        m_excludedCards, &listing);
    m_knownPcms = listing;

    std::vector<AlsaSecondaryCard*> cards;
    {
        std::lock_guard<std::mutex> lock(m_cardsMutex);
        cards = m_secondaryCards;
    }
    std::vector<std::string> current;
    std::vector<std::string> desired;
    for (size_t i = 0; i < cards.size(); i++) {
        current.push_back(cardKeyOf(cards[i]->config()));
    }
    for (size_t j = 0; j < plan.size(); j++) {
        desired.push_back(cardKeyOf(plan[j]));
    }

    AlsaCardChanges changes = alsaDiffCards(current, desired);
    if (changes.m_retire.empty() && changes.m_add.empty()) {
        // Verbose only: a change event that changes nothing must not touch the
        // graph, but seeing the reconcile is useful when testing hot-plug.
        jack_log("sound device change: no secondary card added or removed");
    }
    for (size_t k = 0; k < changes.m_retire.size(); k++) {
        AlsaSecondaryCard* card = cards[changes.m_retire[k]];
        jack_info("secondary card '%s' removed: its device is no longer present or no longer wanted",
            portBaseOf(card->config()).c_str());
        retireCard(card);
    }
    for (size_t k = 0; k < changes.m_add.size(); k++) {
        addCard(plan[changes.m_add[k]]);
    }
    updateSecondaryLatencies();
}

// Creates and opens a card for the primary's current settings.  Returns nullptr if
// the card cannot be opened.
AlsaSecondaryCard* AlsaDriver::createCard(const AlsaSecondaryConfig& cfg, ResamplerFactory* factory)
{
    AlsaSecondaryCard* card = new AlsaSecondaryCard(cfg, factory);
    card->setRTState(&m_rTEpoch, &m_rTRunning);
    card->setId(m_nextCardId++);
    card->setFailureSink(m_reconfigWorker);
    if (card->open(m_session.framesPerCycle(), m_session.rate(), m_request.m_periods,
            m_engineControl->m_realTime, m_engineControl->m_serverPriority)
        < 0) {
        delete card;
        return nullptr;
    }
    return card;
}

// Adds a card for a device that appeared after startup.  Runs on the worker
// thread, with the driver running.  A failure is logged once and the card is
// left out until the next event.
bool AlsaDriver::addCard(const AlsaSecondaryConfig& cfg)
{
    const std::string& pcm = cfg.m_captureDevice.empty() ? cfg.m_playbackDevice : cfg.m_captureDevice;
    if (!lockCard(pcm.c_str(), true)) {
        reportOnce("secondary card '" + portBaseOf(cfg) + "' is skipped: its card is held by another server");
        return false;
    }

    ResamplerFactory* factory = ResamplerFactory::find(cfg.m_resampler.c_str());
    if (factory == nullptr) {
        reportOnce("secondary card '" + portBaseOf(cfg) + "': resampler '" + cfg.m_resampler + "' is not available");
        return false;
    }

    AlsaSecondaryCard* card = createCard(cfg, factory);
    if (card == nullptr) {
        reportOnce("secondary card '" + portBaseOf(cfg) + "' cannot be opened; it will be tried at the next device change");
        return false;
    }
    if (card->start() < 0) {
        delete card;
        reportOnce("secondary card '" + portBaseOf(cfg) + "' cannot be started; it will be tried at the next device change");
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(m_cardsMutex);
        m_secondaryCards.push_back(card);
    }
    if (!promoteCard(card)) {
        // Not promoted, so the RT thread never used it; retiring frees it.
        retireCard(card);
        reportOnce("secondary card '" + portBaseOf(cfg) + "' cannot be added; it will be tried at the next device change");
        return false;
    }

    for (std::int32_t d = 0; d < 2; d++) {
        AlsaRateDirection dir = (AlsaRateDirection)d;
        if (card->rateElementValid(dir)) {
            m_controlMonitor->queueWatch(card->id(), card->controlName(), card->rateElement(dir), dir);
        }
    }
    jack_info("secondary card '%s' added", portBaseOf(cfg).c_str());
    return true;
}

void AlsaDriver::reportOnce(const std::string& message)
{
    for (size_t i = 0; i < m_reportedFailures.size(); i++) {
        if (m_reportedFailures[i] == message) {
            return;
        }
    }
    m_reportedFailures.push_back(message);
    jack_error("%s", message.c_str());
}

void AlsaDriver::handleRateRequest(std::int32_t card, AlsaRateDirection dir, std::int64_t value)
{
    AlsaSecondaryCard* secondary = findCard(card);
    if (secondary == nullptr) {
        return; // the card was removed; its watch may still have delivered this
    }
    std::int64_t current = (std::int64_t)secondary->rate();
    if (value == current) {
        // Our own write echoing back, or a no-op.
        return;
    }

    const char* what = (dir == kRateCapture) ? "capture" : "playback";
    if (value < 8000 || value > 768000) {
        jack_error("secondary card '%s': %s rate %ld Hz is out of range; keeping %ld Hz",
            secondary->name().c_str(), what, value, current);
    } else if (dir == kRateCapture && !secondary->config().m_followHwRate) {
        // The default: a card keeps its rate unless it is asked to follow the hardware.
        jack_error("secondary card '%s': capture rate changed to %ld Hz; keeping %ld Hz "
                   "(follow_hw_rate=0)",
            secondary->name().c_str(), value, current);
    } else {
        jack_info("secondary card '%s': %s rate change %ld Hz -> %ld Hz, reopening",
            secondary->name().c_str(), what, current, value);
        if (secondary->reconfigure((NframesC)value) < 0) {
            jack_error("secondary card '%s': %s rate change to %ld Hz failed",
                secondary->name().c_str(), what, value);
        } else if ((std::int64_t)secondary->rate() != current) {
            updateSecondaryLatencies();
            // Only a change that took effect is reported.  A card whose capture and
            // playback share one stream reports both directions in one call.
            std::int32_t directions = (secondary->captureEnabled() ? kSecondaryRateCapture : 0) | (secondary->playbackEnabled() ? kSecondaryRatePlayback : 0);
            notifySecondarySampleRate(secondary->name().c_str(), secondary->rate(), directions);
        }
    }

    // Report the rate the card actually runs at.  The write echoes back as an
    // event with the same value, which is ignored above.
    std::int64_t actual = (std::int64_t)secondary->rate();
    for (std::int32_t d = 0; d < 2; d++) {
        AlsaRateDirection each = (AlsaRateDirection)d;
        if (secondary->rateElementValid(each) && secondary->rateElement(each).m_writable) {
            if (secondary->writeRate(each, actual) < 0) {
                jack_error("secondary card '%s': cannot update the rate control", secondary->name().c_str());
            }
        }
    }
}

std::int32_t AlsaDriver::stop()
{
    if (!m_restarting.load()) {
        m_rTRunning.store(false);
        stopControlMonitoring();
    }
    stopSecondaryCards();
    std::int32_t res = m_session.stop();
    if (AudioDriver::stop() < 0) {
        res = -1;
    }
    return res;
}

std::int32_t AlsaDriver::read()
{
    // Taken from alsa_driver_run_cycle
    std::int32_t waitStatus;
    NframesC nframes;
    m_delayedUsecs = 0.f;

    // A real xrun recovers in one or two tries.  A device that keeps returning no frames must fail the driver
    // instead of looping here forever and holding the server without a running cycle.
    constexpr std::int32_t kMaxRecoveries = 5;
    std::int32_t recoveries = 0;

retry:

    const jackmini::alsa::WaitResult wait = m_session.wait();
    waitStatus = wait.m_status;
    m_delayedUsecs = wait.m_delayedUsecs;
    nframes = static_cast<NframesC>(wait.m_frames);

    if (waitStatus < 0) {
        primaryFailed(waitStatus);
        return -1; // driver failed
    }

    if (nframes == 0) {
        // we detected an xrun and restarted: notify
        // clients about the delay.
        jack_log("ALSA XRun wait_status = %d", waitStatus);
        notifyXRun(m_beginDateUst, m_delayedUsecs);
        for (size_t i = 0; i < m_slots.capacity(); i++) {
            AlsaSecondaryCard* card = m_slots.get(i);
            if (card) {
                card->resetResampling();
            }
        }
        if (++recoveries > kMaxRecoveries) {
            primaryFailed(-5);
            return -1;
        }
        goto retry; // recoverable error
    }

    if (nframes != m_engineControl->m_bufferSize)
        jack_log("JackAlsaDriver::Read warning fBufferSize = %ld nframes = %ld", m_engineControl->m_bufferSize, nframes);

    // Has to be done before read
    Driver::cycleIncTime();

    std::int32_t res = m_session.read(m_engineControl->m_bufferSize);
    if (res < 0) {
        return res;
    }

    for (size_t i = 0; i < m_slots.capacity(); i++) {
        AlsaSecondaryCard* card = m_slots.get(i);
        if (card) {
            card->readToPorts(m_graphManager, m_engineControl->m_bufferSize);
        }
    }
    return 0;
}

std::int32_t AlsaDriver::write()
{
    std::int32_t res = m_session.write(m_engineControl->m_bufferSize);
    if (res < 0) {
        primaryFailed(res);
        return res;
    }

    for (size_t i = 0; i < m_slots.capacity(); i++) {
        AlsaSecondaryCard* card = m_slots.get(i);
        if (card) {
            card->writeFromPorts(m_graphManager, m_engineControl->m_bufferSize);
        }
    }

    // One RT cycle is complete.  Card reconfiguration waits on this counter.
    m_rTEpoch.fetch_add(1, std::memory_order_release);
    return 0;
}

void AlsaDriver::readInputAux(NframesC origNframes, const jackmini::alsa::MappedFrames& mapped,
    std::ptrdiff_t nread)
{
    for (std::int32_t chn = 0; chn < m_captureChannels; chn++) {
        if (m_graphManager->getConnectionsNum(m_capturePortList[chn]) > 0) {
            DefaultAudioSampleC* buf = (DefaultAudioSampleC*)m_graphManager->getBuffer(m_capturePortList[chn], origNframes);
            m_session.readChannel(mapped.m_channels[chn], buf + nread, mapped.m_frames);
        }
    }
}

void AlsaDriver::clearOutputAux()
{
    for (std::int32_t chn = 0; chn < m_playbackChannels; chn++) {
        DefaultAudioSampleC* buf = (DefaultAudioSampleC*)m_graphManager->getBuffer(m_playbackPortList[chn], m_engineControl->m_bufferSize);
        memset(buf, 0, sizeof(DefaultAudioSampleC) * m_engineControl->m_bufferSize);
    }
}

void AlsaDriver::setTimetAux(TimeC time)
{
    m_beginDateUst = time;
}

void AlsaDriver::writeOutputAux(NframesC origNframes, const jackmini::alsa::MappedFrames& mapped,
    std::ptrdiff_t nwritten)
{
    for (std::int32_t chn = 0; chn < m_playbackChannels; chn++) {
        // Output ports
        if (m_graphManager->getConnectionsNum(m_playbackPortList[chn]) > 0) {
            DefaultAudioSampleC* buf = (DefaultAudioSampleC*)m_graphManager->getBuffer(m_playbackPortList[chn], origNframes);
            m_session.writeChannel(chn, mapped.m_channels[chn], buf + nwritten, mapped.m_frames);
            // Monitor ports
            if (m_withMonitorPorts && m_graphManager->getConnectionsNum(m_monitorPortList[chn]) > 0) {
                DefaultAudioSampleC* monbuf = (DefaultAudioSampleC*)m_graphManager->getBuffer(m_monitorPortList[chn], origNframes);
                memcpy(monbuf + nwritten, buf + nwritten, mapped.m_frames * sizeof(DefaultAudioSampleC));
            }
        }
    }
}

} // namespace jackmini


static DriverParamConstraintDesc*
enumAlsaDevices()
{
    snd_ctl_t* handle;
    snd_ctl_card_info_t* info;
    snd_pcm_info_t* pcminfoCapture;
    snd_pcm_info_t* pcminfoPlayback;
    std::int32_t cardNo = -1;
    DriverParamValue cardId;
    DriverParamValue deviceId;
    std::array<char, 64> description { };
    std::int32_t deviceNo;
    bool hasCapture;
    bool hasPlayback;
    DriverParamConstraintDesc* constraintPtr;
    std::uint32_t arraySize = 0;

    snd_ctl_card_info_alloca(&info);
    snd_pcm_info_alloca(&pcminfoCapture);
    snd_pcm_info_alloca(&pcminfoPlayback);

    constraintPtr = nullptr;

    while (snd_card_next(&cardNo) >= 0 && cardNo >= 0) {
        jackmini::formatCString(std::span<char>(cardId.m_str), "hw:{}", cardNo);

        if (snd_ctl_open(&handle, cardId.m_str.data(), 0) >= 0 && snd_ctl_card_info(handle, info) >= 0) {
            jackmini::formatCString(std::span<char>(cardId.m_str), "hw:{}", snd_ctl_card_info_get_id(info));
            if (!jack_constraint_add_enum(
                    &constraintPtr,
                    &arraySize,
                    &cardId,
                    snd_ctl_card_info_get_name(info)))
                goto fail;

            deviceNo = -1;

            while (snd_ctl_pcm_next_device(handle, &deviceNo) >= 0 && deviceNo != -1) {
                jackmini::formatCString(std::span<char>(deviceId.m_str), "{},{}", cardId.m_str.data(), deviceNo);

                snd_pcm_info_set_device(pcminfoCapture, deviceNo);
                snd_pcm_info_set_subdevice(pcminfoCapture, 0);
                snd_pcm_info_set_stream(pcminfoCapture, SND_PCM_STREAM_CAPTURE);
                hasCapture = snd_ctl_pcm_info(handle, pcminfoCapture) >= 0;

                snd_pcm_info_set_device(pcminfoPlayback, deviceNo);
                snd_pcm_info_set_subdevice(pcminfoPlayback, 0);
                snd_pcm_info_set_stream(pcminfoPlayback, SND_PCM_STREAM_PLAYBACK);
                hasPlayback = snd_ctl_pcm_info(handle, pcminfoPlayback) >= 0;

                if (hasCapture && hasPlayback) {
                    jackmini::formatCString(description, "{} (duplex)", snd_pcm_info_get_name(pcminfoCapture));
                } else if (hasCapture) {
                    jackmini::formatCString(description, "{} (capture)", snd_pcm_info_get_name(pcminfoCapture));
                } else if (hasPlayback) {
                    jackmini::formatCString(description, "{} (playback)", snd_pcm_info_get_name(pcminfoPlayback));
                } else {
                    continue;
                }

                if (!jack_constraint_add_enum(
                        &constraintPtr,
                        &arraySize,
                        &deviceId,
                        description.data()))
                    goto fail;
            }

            snd_ctl_close(handle);
        }
    }

    return constraintPtr;
fail:
    jack_constraint_free(constraintPtr);
    return nullptr;
}

static std::int32_t
ditherOpt(char c, DitherAlgorithm* dither)
{
    switch (c) {
    case '-':
    case 'n':
        *dither = kNone;
        break;

    case 'r':
        *dither = kRectangular;
        break;

    case 's':
        *dither = kShaped;
        break;

    case 't':
        *dither = kTriangular;
        break;

    default:
        fprintf(stderr, "ALSA driver: illegal dithering mode %c\n", c);
        return -1;
    }
    return 0;
}

static DriverDesc* describeDriver()
{
    DriverDesc* desc;
    DriverDescFiller filler;
    DriverParamValue value;

    desc = jack_driver_descriptor_construct("alsa", kDriverMaster, "Linux ALSA API based audio backend", &filler);

    strcpy(value.m_str.data(), "");
    jack_driver_descriptor_add_parameter(desc, &filler, "device", 'd', kDriverParamString, &value, enumAlsaDevices(), "ALSA device name", nullptr);

    strcpy(value.m_str.data(), "none");
    jack_driver_descriptor_add_parameter(desc, &filler, "capture", 'C', kDriverParamString, &value, nullptr, "Provide capture ports.  Optionally set device", nullptr);
    jack_driver_descriptor_add_parameter(desc, &filler, "playback", 'P', kDriverParamString, &value, nullptr, "Provide playback ports.  Optionally set device", nullptr);

    value.m_ui = 48000U;
    jack_driver_descriptor_add_parameter(desc, &filler, "rate", 'r', kDriverParamUInt, &value, nullptr, "Sample rate", nullptr);

    value.m_ui = 1024U;
    jack_driver_descriptor_add_parameter(desc, &filler, "period", 'p', kDriverParamUInt, &value, nullptr, "Frames per period", nullptr);

    value.m_ui = 2U;
    jack_driver_descriptor_add_parameter(desc, &filler, "nperiods", 'n', kDriverParamUInt, &value, nullptr, "Number of periods of playback latency", nullptr);

    value.m_i = 0;
    jack_driver_descriptor_add_parameter(desc, &filler, "hwmon", 'H', kDriverParamBool, &value, nullptr, "Hardware monitoring, if available", nullptr);

    value.m_i = 0;
    jack_driver_descriptor_add_parameter(desc, &filler, "hwmeter", 'M', kDriverParamBool, &value, nullptr, "Hardware metering, if available", nullptr);

    value.m_i = 1;
    jack_driver_descriptor_add_parameter(desc, &filler, "duplex", 'D', kDriverParamBool, &value, nullptr, "Provide both capture and playback ports", nullptr);

    value.m_i = 0;
    jack_driver_descriptor_add_parameter(desc, &filler, "softmode", 's', kDriverParamBool, &value, nullptr, "Soft-mode, no xrun handling", nullptr);

    value.m_i = 0;
    jack_driver_descriptor_add_parameter(desc, &filler, "monitor", 'm', kDriverParamBool, &value, nullptr, "Provide monitor ports for the output", nullptr);

    value.m_c = 'n';
    jack_driver_descriptor_add_parameter(
        desc,
        &filler,
        "dither",
        'z',
        kDriverParamChar,
        &value,
        jack_constraint_compose_enum_char(
            kConstraintFlagStrict | kConstraintFlagFakeValue,
            gDitherConstraintDescrArray.data()),
        "Dithering mode",
        nullptr);

    value.m_ui = 0;
    jack_driver_descriptor_add_parameter(desc, &filler, "inchannels", 'i', kDriverParamUInt, &value, nullptr, "Number of capture channels (defaults to hardware max)", nullptr);
    jack_driver_descriptor_add_parameter(desc, &filler, "outchannels", 'o', kDriverParamUInt, &value, nullptr, "Number of playback channels (defaults to hardware max)", nullptr);

    value.m_i = false;
    jack_driver_descriptor_add_parameter(desc, &filler, "shorts", 'S', kDriverParamBool, &value, nullptr, "Try 16-bit samples before 32-bit", nullptr);

    value.m_ui = 0;
    jack_driver_descriptor_add_parameter(desc, &filler, "input-latency", 'I', kDriverParamUInt, &value, nullptr, "Extra input latency (frames)", nullptr);
    jack_driver_descriptor_add_parameter(desc, &filler, "output-latency", 'O', kDriverParamUInt, &value, nullptr, "Extra output latency (frames)", nullptr);

    strcpy(value.m_str.data(), "none");
    jack_driver_descriptor_add_parameter(desc, &filler, "secondary", 'Y', kDriverParamString, &value, nullptr,
        "Secondary ALSA card (repeatable)",
        "Secondary ALSA card, repeatable. Keys: name, device, mode (duplex|capture|playback), channels, rate, nperiods, clock (async|sync), format (auto|float|s32|s16), resampler, optional, follow_hw_rate. Example: name=usb1;device=hw:1;mode=capture;clock=async");
    jack_driver_descriptor_add_parameter(desc, &filler, "exclude-card", 'Z', kDriverParamString, &value, nullptr,
        "ALSA card ids to leave out of automatic discovery",
        "ALSA card id (for example Loopback) that automatic discovery must not add. Comma separated, for example PCH,NVidia. Applies only to automatic cards; a card given with --secondary or --secondary-file is still used. Matching ignores case");

    jack_driver_descriptor_add_parameter(desc, &filler, "secondary-file", 'K', kDriverParamString, &value, nullptr,
        "File with one secondary ALSA card per line",
        "File with one secondary ALSA card per line, read at startup. Same syntax as --secondary. Blank lines and lines starting with # are ignored");

    return desc;
}

static jackmini::DriverClientInterface* initializeDriver(jackmini::LockedEngine* engine, jackmini::Synchro* table, const JSListC* params)
{
    NframesC srate = 48000;
    NframesC framesPerInterrupt = 1024;
    std::uint64_t userNperiods = 2;
    const char* playbackPcmName = "";
    const char* capturePcmName = "";
    std::int32_t hwMonitoring = false;
    std::int32_t hwMetering = false;
    std::int32_t capture = false;
    std::int32_t playback = false;
    std::int32_t softMode = false;
    std::int32_t monitor = false;
    DitherAlgorithm dither = kNone;
    std::int32_t userCaptureNchnls = 0;
    std::int32_t userPlaybackNchnls = 0;
    std::int32_t shortsFirst = false;
    NframesC systemicInputLatency = 0;
    NframesC systemicOutputLatency = 0;
    const JSListC* node;
    const DriverParam* param;
    std::vector<jackmini::AlsaSecondaryConfig> secondaries;
    std::vector<std::string> excludedCards;

    for (node = params; node; node = jack_slist_next(node)) {
        param = (const DriverParam*)node->m_data;

        switch (param->m_character) {

        case 'C':
            capture = true;
            if (strcmp(param->m_value.m_str.data(), "none") != 0) {
                capturePcmName = strdup(param->m_value.m_str.data());
                jack_log("capture device %s", capturePcmName);
            }
            break;

        case 'P':
            playback = true;
            if (strcmp(param->m_value.m_str.data(), "none") != 0) {
                playbackPcmName = strdup(param->m_value.m_str.data());
                jack_log("playback device %s", playbackPcmName);
            }
            break;

        case 'D':
            playback = true;
            capture = true;
            break;

        case 'd':
            if (strcmp(param->m_value.m_str.data(), "none") != 0) {
                playbackPcmName = strdup(param->m_value.m_str.data());
                capturePcmName = strdup(param->m_value.m_str.data());
                jack_log("playback device %s", playbackPcmName);
                jack_log("capture device %s", capturePcmName);
            }
            break;

        case 'H':
            hwMonitoring = param->m_value.m_i;
            break;

        case 'm':
            monitor = param->m_value.m_i;
            break;

        case 'M':
            hwMetering = param->m_value.m_i;
            break;

        case 'r':
            srate = param->m_value.m_ui;
            jack_log("apparent rate = %d", srate);
            break;

        case 'p':
            framesPerInterrupt = param->m_value.m_ui;
            jack_log("frames per period = %d", framesPerInterrupt);
            break;

        case 'n':
            userNperiods = param->m_value.m_ui;
            if (userNperiods < 2) { // enforce minimum value
                userNperiods = 2;
            }
            break;

        case 's':
            softMode = param->m_value.m_i;
            break;

        case 'z':
            if (ditherOpt(param->m_value.m_c, &dither)) {
                return nullptr;
            }
            break;

        case 'i':
            userCaptureNchnls = param->m_value.m_ui;
            break;

        case 'o':
            userPlaybackNchnls = param->m_value.m_ui;
            break;

        case 'S':
            shortsFirst = param->m_value.m_i;
            break;

        case 'I':
            systemicInputLatency = param->m_value.m_ui;
            break;

        case 'O':
            systemicOutputLatency = param->m_value.m_ui;
            break;

        case 'Y':
            if (strcmp(param->m_value.m_str.data(), "none") != 0) {
                jackmini::AlsaSecondaryConfig config;
                std::string error;
                if (!jackmini::alsaSecondaryConfigParse(param->m_value.m_str.data(), &config, &error)) {
                    jack_error("invalid --secondary \"%s\": %s", param->m_value.m_str, error.c_str());
                    return nullptr;
                }
                secondaries.push_back(config);
            }
            break;

        case 'Z':
            // The parameter holds one value, so a list is comma separated: "-Z PCH,NVidia".
            if (strcmp(param->m_value.m_str.data(), "none") != 0) {
                std::string_view list(param->m_value.m_str);
                while (!list.empty()) {
                    const auto comma = list.find(',');
                    const std::string_view id = list.substr(0, comma);
                    if (!id.empty()) {
                        excludedCards.emplace_back(id);
                    }
                    if (comma == std::string_view::npos) {
                        break;
                    }
                    list.remove_prefix(comma + 1);
                }
            }
            break;

        case 'K':
            if (strcmp(param->m_value.m_str.data(), "none") != 0) {
                std::string error;
                if (!jackmini::alsaSecondaryConfigLoadFile(param->m_value.m_str.data(), &secondaries, &error)) {
                    jack_error("%s", error.c_str());
                    return nullptr;
                }
            }
            break;
        }
    }

    // duplex is the default
    if (!capture && !playback) {
        capture = true;
        playback = true;
    }

    // The primary card is named by its ALSA id, so that it stays the same card
    // when devices are added or removed.  "hw:0" would name whatever card has
    // that number at the time.
    if ((capture && !jackmini::alsaPcmNameIsById(capturePcmName)) || (playback && !jackmini::alsaPcmNameIsById(playbackPcmName))) {
        jack_error("the primary ALSA device must be given by card id, for example -d hw:CARD=PCH; "
                   "numeric forms such as hw:0 are not accepted");
        return nullptr;
    }

    jackmini::AlsaDriver* alsaDriver = new jackmini::AlsaDriver("system", "alsa_pcm", engine, table);
    jackmini::DriverClientInterface* threadedDriver = new jackmini::ThreadedDriver(alsaDriver);
    // Special open for ALSA driver...
    if (alsaDriver->open(framesPerInterrupt, userNperiods, srate, hwMonitoring, hwMetering, capture, playback, dither, softMode, monitor,
            userCaptureNchnls, userPlaybackNchnls, shortsFirst, capturePcmName, playbackPcmName,
            systemicInputLatency, systemicOutputLatency,
            secondaries, excludedCards)
        == 0) {
        return threadedDriver;
    } else {
        delete threadedDriver; // Delete the decorated driver
        return nullptr;
    }
}


extern "C" SERVER_EXPORT const jackmini::driver::Module* jackmini_driver_module()
{
    static const jackmini::driver::Module kDriverModule { jackmini::driver::kModuleVersion, describeDriver, initializeDriver };
    return &kDriverModule;
}

// Copyright (C) 2002 Anthony Van Groningen
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

#include "ExternalMetro.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace jackmini;

const double gPI = 3.14;

namespace {

// The keys are read as they are typed.  EOF ends the wait, so that a closed input does not spin.
bool waitForKey(std::int32_t key)
{
    std::int32_t c;
    while ((c = getchar()) != key) {
        if (c == EOF) {
            return false;
        }
    }
    return true;
}

Client openClient(const char* clientName)
{
    OpenOptions options;
    auto opened = Client::open(clientName, options);
    if (!opened) {
        std::fprintf(stderr, "jack server not running?\n");
        throw -1;
    }
    return std::move(*opened);
}

} // namespace

bool ExternalMetro::processAudio(FrameCount nframes)
{
    float* buffer = m_client.audioBuffer(m_outputPort).data();
    FrameCount framesLeft = nframes;

    while (m_waveLength - m_offset < framesLeft) {
        std::memcpy(buffer + (nframes - framesLeft), m_wave + m_offset, sizeof(float) * (m_waveLength - m_offset));
        framesLeft -= m_waveLength - m_offset;
        m_offset = 0;
    }
    if (framesLeft > 0) {
        std::memcpy(buffer + (nframes - framesLeft), m_wave + m_offset, sizeof(float) * framesLeft);
        m_offset += framesLeft;
    }

    return true;
}

ExternalMetro::ExternalMetro(std::int32_t freqArg, double maxAmp, std::int32_t durArg, std::int32_t bpmArg, const char* clientName)
    : m_client(openClient(clientName))
{
    float scale;
    std::int32_t i, attackLength, decayLength;
    std::int32_t attackPercent = 1, decayPercent = 10;
    m_offset = 0;
    m_wave = nullptr;
    m_amp = nullptr;
    m_freq = freqArg;
    m_bpm = bpmArg;

    m_client.onProcess([this](const Cycle& cycle) { return processAudio(cycle.m_frames); });
    m_client.onShutdown([] { std::printf("shutdown called..\n"); });
    auto out = m_client.registerPort("bpm", PortType::kAudio, PortFlags::kOutput);
    auto in = m_client.registerPort("metro_in", PortType::kAudio, PortFlags::kInput);
    if (out) {
        m_outputPort = *out;
    }
    if (in) {
        m_inputPort = *in;
    }

    m_sr = m_client.sampleRate();

    // setup wave table parameters
    m_waveLength = 60 * m_sr / m_bpm;
    m_toneLength = m_sr * durArg / 1000;
    attackLength = m_toneLength * attackPercent / 100;
    decayLength = m_toneLength * decayPercent / 100;
    scale = 2 * gPI * m_freq / m_sr;

    if (m_toneLength >= m_waveLength) {
        return;
    }
    if (attackLength + decayLength > (std::int32_t)m_toneLength) {
        std::fprintf(stderr, "invalid attack/decay\n");
        return;
    }

    // Build the wave table
    m_wave = (float*)std::malloc(m_waveLength * sizeof(float));
    m_amp = (double*)std::malloc(m_toneLength * sizeof(double));

    for (i = 0; i < attackLength; i++) {
        m_amp[i] = maxAmp * i / ((double)attackLength);
    }
    for (i = attackLength; i < (std::int32_t)m_toneLength - decayLength; i++) {
        m_amp[i] = maxAmp;
    }
    for (i = (std::int32_t)m_toneLength - decayLength; i < (std::int32_t)m_toneLength; i++) {
        m_amp[i] = -maxAmp * (i - (double)m_toneLength) / ((double)decayLength);
    }
    for (i = 0; i < (std::int32_t)m_toneLength; i++) {
        m_wave[i] = m_amp[i] * std::sin(scale * i);
    }
    for (i = m_toneLength; i < (std::int32_t)m_waveLength; i++) {
        m_wave[i] = 0;
    }

    if (!m_client.activate().has_value()) {
        std::fprintf(stderr, "cannot activate client\n");
    }
}

ExternalMetro::~ExternalMetro()
{
    // The process callback reads the wave table: the client stops before the table is freed.
    m_client.deactivate();
    m_client.close();
    std::free(m_amp);
    std::free(m_wave);
}

int main(std::int32_t argc, char* argv[])
{
    ExternalMetro* client1 = nullptr;
    ExternalMetro* client2 = nullptr;
    ExternalMetro* client3 = nullptr;
    ExternalMetro* client4 = nullptr;
    ExternalMetro* client5 = nullptr;

    std::printf("Testing multiple Jack clients in one process: open 5 metro clients...\n");
    client1 = new ExternalMetro(1200, 0.4, 20, 80, "t1");
    client2 = new ExternalMetro(600, 0.4, 20, 150, "t2");
    client3 = new ExternalMetro(1000, 0.4, 20, 110, "t3");
    client4 = new ExternalMetro(400, 0.4, 20, 200, "t4");
    client5 = new ExternalMetro(1500, 0.4, 20, 150, "t5");

    std::printf("Type 'c' to close all clients and go to next test...\n");
    waitForKey('c');

    delete client1;
    delete client2;
    delete client3;
    delete client4;
    delete client5;

    std::printf("Testing quitting the server while a client is running...\n");
    client1 = new ExternalMetro(1200, 0.4, 20, 80, "t1");

    std::printf("Now quit the server, shutdown callback should be called...\n");
    std::printf("Type 'c' to move on...\n");
    waitForKey('c');

    std::printf("Closing client...\n");
    delete client1;

    std::printf("Now start the server again...\n");
    std::printf("Type 'c' to move on...\n");
    waitForKey('c');

    std::printf("Opening a new client....\n");
    client1 = new ExternalMetro(1200, 0.4, 20, 80, "t1");

    std::printf("Now quit the server, shutdown callback should be called...\n");
    std::printf("Type 'c' to move on...\n");
    waitForKey('c');

    std::printf("Simulating client not correctly closed...\n");

    std::printf("Opening a new client....\n");
    try {
        client1 = new ExternalMetro(1200, 0.4, 20, 80, "t1");
    } catch (std::int32_t num) {
        std::printf("Cannot open a new client since old one was not closed correctly... OK\n");
    }

    std::printf("Type 'q' to quit...\n");
    waitForKey('q');
    delete client1;
    return 0;
}

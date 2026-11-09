/*
 Copyright (C) 2002 Anthony Van Groningen

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

#include "external_metro.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace jackmini;

const double PI = 3.14;

namespace {

// The keys are read as they are typed.  EOF ends the wait, so that a closed input does not spin.
bool WaitForKey(int key)
{
    int c;
    while ((c = getchar()) != key) {
        if (c == EOF) {
            return false;
        }
    }
    return true;
}

Client OpenClient(const char* client_name)
{
    OpenOptions options;
    auto opened = Client::open(client_name, options);
    if (!opened) {
        std::fprintf(stderr, "jack server not running?\n");
        throw -1;
    }
    return std::move(*opened);
}

} // namespace

bool ExternalMetro::process_audio(FrameCount nframes)
{
    float* buffer = client.audio_buffer(output_port).data();
    FrameCount frames_left = nframes;

    while (wave_length - offset < frames_left) {
        std::memcpy(buffer + (nframes - frames_left), wave + offset, sizeof(float) * (wave_length - offset));
        frames_left -= wave_length - offset;
        offset = 0;
    }
    if (frames_left > 0) {
        std::memcpy(buffer + (nframes - frames_left), wave + offset, sizeof(float) * frames_left);
        offset += frames_left;
    }

    return true;
}

ExternalMetro::ExternalMetro(int freq_arg, double max_amp, int dur_arg, int bpm_arg, const char* client_name)
    : client(OpenClient(client_name))
{
    float scale;
    int i, attack_length, decay_length;
    int attack_percent = 1, decay_percent = 10;
    offset = 0;
    wave = nullptr;
    amp = nullptr;
    freq = freq_arg;
    bpm = bpm_arg;

    client.on_process([this](const Cycle& cycle) { return process_audio(cycle.frames); });
    client.on_shutdown([] { std::printf("shutdown called..\n"); });
    auto out = client.register_port("bpm", PortType::Audio, PortFlags::Output);
    auto in = client.register_port("metro_in", PortType::Audio, PortFlags::Input);
    if (out) {
        output_port = *out;
    }
    if (in) {
        input_port = *in;
    }

    sr = client.sample_rate();

    /* setup wave table parameters */
    wave_length = 60 * sr / bpm;
    tone_length = sr * dur_arg / 1000;
    attack_length = tone_length * attack_percent / 100;
    decay_length = tone_length * decay_percent / 100;
    scale = 2 * PI * freq / sr;

    if (tone_length >= wave_length) {
        return;
    }
    if (attack_length + decay_length > (int)tone_length) {
        std::fprintf(stderr, "invalid attack/decay\n");
        return;
    }

    /* Build the wave table */
    wave = (float*)std::malloc(wave_length * sizeof(float));
    amp = (double*)std::malloc(tone_length * sizeof(double));

    for (i = 0; i < attack_length; i++) {
        amp[i] = max_amp * i / ((double)attack_length);
    }
    for (i = attack_length; i < (int)tone_length - decay_length; i++) {
        amp[i] = max_amp;
    }
    for (i = (int)tone_length - decay_length; i < (int)tone_length; i++) {
        amp[i] = -max_amp * (i - (double)tone_length) / ((double)decay_length);
    }
    for (i = 0; i < (int)tone_length; i++) {
        wave[i] = amp[i] * std::sin(scale * i);
    }
    for (i = tone_length; i < (int)wave_length; i++) {
        wave[i] = 0;
    }

    if (!client.activate().has_value()) {
        std::fprintf(stderr, "cannot activate client\n");
    }
}

ExternalMetro::~ExternalMetro()
{
    // The process callback reads the wave table: the client stops before the table is freed.
    client.deactivate();
    client.close();
    std::free(amp);
    std::free(wave);
}

int main(int argc, char* argv[])
{
    ExternalMetro* client1 = NULL;
    ExternalMetro* client2 = NULL;
    ExternalMetro* client3 = NULL;
    ExternalMetro* client4 = NULL;
    ExternalMetro* client5 = NULL;

    std::printf("Testing multiple Jack clients in one process: open 5 metro clients...\n");
    client1 = new ExternalMetro(1200, 0.4, 20, 80, "t1");
    client2 = new ExternalMetro(600, 0.4, 20, 150, "t2");
    client3 = new ExternalMetro(1000, 0.4, 20, 110, "t3");
    client4 = new ExternalMetro(400, 0.4, 20, 200, "t4");
    client5 = new ExternalMetro(1500, 0.4, 20, 150, "t5");

    std::printf("Type 'c' to close all clients and go to next test...\n");
    WaitForKey('c');

    delete client1;
    delete client2;
    delete client3;
    delete client4;
    delete client5;

    std::printf("Testing quitting the server while a client is running...\n");
    client1 = new ExternalMetro(1200, 0.4, 20, 80, "t1");

    std::printf("Now quit the server, shutdown callback should be called...\n");
    std::printf("Type 'c' to move on...\n");
    WaitForKey('c');

    std::printf("Closing client...\n");
    delete client1;

    std::printf("Now start the server again...\n");
    std::printf("Type 'c' to move on...\n");
    WaitForKey('c');

    std::printf("Opening a new client....\n");
    client1 = new ExternalMetro(1200, 0.4, 20, 80, "t1");

    std::printf("Now quit the server, shutdown callback should be called...\n");
    std::printf("Type 'c' to move on...\n");
    WaitForKey('c');

    std::printf("Simulating client not correctly closed...\n");

    std::printf("Opening a new client....\n");
    try {
        client1 = new ExternalMetro(1200, 0.4, 20, 80, "t1");
    } catch (int num) {
        std::printf("Cannot open a new client since old one was not closed correctly... OK\n");
    }

    std::printf("Type 'q' to quit...\n");
    WaitForKey('q');
    delete client1;
    return 0;
}

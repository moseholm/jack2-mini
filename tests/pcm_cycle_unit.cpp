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

// Unit tests for the ALSA cycle core.  The fake backend scripts the poll, the avail, and the xrun status, so the
// avail, the recovery, the start/stop sync, and the cycle counts are checked without a sound card.

#include "alsa_fake_backend.hpp"

#include <cstdint>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

using namespace jackmini::alsa;

static std::int32_t g_failures = 0;
static std::int32_t g_checks = 0;

#define CHECK(cond)                                                         \
    do {                                                                    \
        g_checks++;                                                         \
        if (!(cond)) {                                                      \
            g_failures++;                                                   \
            std::printf("  FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                   \
    } while (0)

namespace {

using FakeCycle = Cycle<FakePcmBackend>;

Config MakeConfig(std::uint32_t frames, std::uint32_t periods, bool not_synced = false)
{
    Config config;
    config.frames_per_cycle = frames;
    config.buffer_frames = frames * periods;
    config.period_usecs = 1000;
    config.capture_and_playback_not_synced = not_synced;
    return config;
}

struct RestartCounter {
    std::int32_t count = 0;
};

std::int32_t RestartThunk(void* arg)
{
    ++static_cast<RestartCounter*>(arg)->count;
    return 0;
}

struct IoContext {
    std::vector<std::vector<float>> capture_out;
    std::vector<std::vector<float>> playback_in;
    std::vector<std::ptrdiff_t> read_offsets;
    std::vector<std::ptrdiff_t> write_offsets;
    bool mark_done = true;
    std::size_t mark_channels = std::numeric_limits<std::size_t>::max();
};

void ReadThunk(void* arg, FakeCycle&, const MappedFrames& mapped, std::uint32_t orig_frames, std::ptrdiff_t nread)
{
    auto& context = *static_cast<IoContext*>(arg);
    context.read_offsets.push_back(nread);
    CHECK(static_cast<std::uint32_t>(mapped.frames) <= orig_frames);
    for (std::size_t channel = 0; channel < mapped.channel_count; ++channel) {
        const auto* source = reinterpret_cast<const float*>(mapped.channels[channel].data);
        const std::size_t stride = mapped.channels[channel].stride / sizeof(float);
        for (std::size_t frame = 0; frame < mapped.frames; ++frame) {
            context.capture_out[channel][static_cast<std::size_t>(nread) + frame] = source[frame * stride];
        }
    }
}

void WriteThunk(void* arg, FakeCycle& cycle, const MappedFrames& mapped, std::uint32_t orig_frames,
    std::ptrdiff_t nwritten)
{
    auto& context = *static_cast<IoContext*>(arg);
    context.write_offsets.push_back(nwritten);
    CHECK(static_cast<std::uint32_t>(mapped.frames) <= orig_frames);
    for (std::size_t channel = 0; channel < mapped.channel_count; ++channel) {
        if (context.mark_done && channel >= context.mark_channels) {
            break;
        }
        auto* destination = reinterpret_cast<float*>(mapped.channels[channel].data);
        const std::size_t stride = mapped.channels[channel].stride / sizeof(float);
        for (std::size_t frame = 0; frame < mapped.frames; ++frame) {
            destination[frame * stride] = context.playback_in[channel][static_cast<std::size_t>(nwritten) + frame];
        }
        if (context.mark_done) {
            cycle.mark_channel_done(channel);
        }
    }
}

void TestAvailRounding()
{
    std::printf("cycle: avail rounds down to a period\n");
    FakePcmBackend backend(true, 2, true, 2, 128);
    FakeCycle cycle(backend, MakeConfig(64, 2));

    backend.capture().avail = 100;
    backend.playback().avail = 100;
    WaitResult result = cycle.wait();
    CHECK(result.status == 0);
    CHECK(result.frames == 64);

    backend.capture().avail = 128;
    backend.playback().avail = 128;
    result = cycle.wait();
    CHECK(result.status == 0);
    CHECK(result.frames == 128);

    backend.capture().avail = 63;
    backend.playback().avail = 63;
    result = cycle.wait();
    CHECK(result.status == 0);
    CHECK(result.frames == 0);
}

void TestCycleCounts()
{
    std::printf("cycle: one wait and one read per period\n");
    FakePcmBackend backend(true, 2, true, 2, 128);
    FakeCycle cycle(backend, MakeConfig(64, 2));

    IoContext context;
    context.capture_out.assign(2, std::vector<float>(64, 0.0f));
    context.playback_in.assign(2, std::vector<float>(64, 0.0f));
    for (std::size_t channel = 0; channel < 2; ++channel) {
        for (std::size_t frame = 0; frame < 128; ++frame) {
            backend.capture().data[channel][frame] = static_cast<float>(channel * 1000 + frame);
        }
    }

    FakeCycle::Callbacks callbacks;
    callbacks.arg = &context;
    callbacks.read_input = ReadThunk;
    callbacks.write_output = WriteThunk;
    cycle.set_callbacks(callbacks);

    for (std::int32_t i = 0; i < 100; ++i) {
        backend.capture().avail = 64;
        backend.playback().avail = 64;
        const WaitResult result = cycle.wait();
        CHECK(result.status == 0);
        CHECK(result.frames == 64);
        CHECK(cycle.read(64) == 0);
        CHECK(cycle.write(64) == 0);
    }

    CHECK(context.read_offsets.size() == 100);
    CHECK(context.write_offsets.size() == 100);
    CHECK(cycle.process_count() == 100);
    CHECK(context.capture_out[0][0] == 0.0f);
    CHECK(context.capture_out[1][0] == 1000.0f);
}

void TestXrunOnAvail()
{
    std::printf("cycle: an avail EPIPE recovers through the restart callback\n");
    FakePcmBackend backend(true, 1, true, 1, 128);
    FakeCycle cycle(backend, MakeConfig(64, 2));

    RestartCounter restarts;
    FakeCycle::Callbacks callbacks;
    callbacks.arg = &restarts;
    callbacks.restart = RestartThunk;
    cycle.set_callbacks(callbacks);

    // The xrun is reported after the first process call, as in the old driver.
    CHECK(cycle.write(0) == 0);

    backend.set_status({ false, true, 1234.0f });
    backend.capture().avail_script.push_back(-EPIPE);

    const WaitResult result = cycle.wait();
    CHECK(result.status == 0);
    CHECK(result.frames == 0);
    CHECK(result.delayed_usecs == 1234.0f);
    CHECK(cycle.xrun_count() == 1);
    CHECK(restarts.count == 1);
    CHECK(backend.has_call("prepare:capture"));
    CHECK(backend.has_call("prepare:playback"));
    CHECK(backend.has_call("status"));
}

void TestXrunOnRevents()
{
    std::printf("cycle: a POLLERR recovers through the restart callback\n");
    FakePcmBackend backend(true, 1, true, 1, 128);
    FakeCycle cycle(backend, MakeConfig(64, 2));

    RestartCounter restarts;
    FakeCycle::Callbacks callbacks;
    callbacks.arg = &restarts;
    callbacks.restart = RestartThunk;
    cycle.set_callbacks(callbacks);

    CHECK(cycle.write(0) == 0);
    backend.set_status({ false, true, 999.0f });
    backend.capture().revents = POLLIN | POLLERR;

    const WaitResult result = cycle.wait();
    CHECK(result.status == 0);
    CHECK(result.frames == 0);
    CHECK(result.delayed_usecs == 999.0f);
    CHECK(cycle.xrun_count() == 1);
    CHECK(restarts.count == 1);
}

void TestSuspended()
{
    std::printf("cycle: a suspended device is prepared before the restart\n");
    FakePcmBackend backend(true, 1, true, 1, 128);
    FakeCycle cycle(backend, MakeConfig(64, 2));

    RestartCounter restarts;
    FakeCycle::Callbacks callbacks;
    callbacks.arg = &restarts;
    callbacks.restart = RestartThunk;
    cycle.set_callbacks(callbacks);

    backend.set_status({ true, false, 0.0f });
    backend.script_poll(0);
    backend.script_poll(1);
    backend.capture().avail = 64;
    backend.playback().avail = 64;

    const WaitResult result = cycle.wait();
    CHECK(result.status == 0);
    CHECK(result.frames == 64);
    CHECK(restarts.count == 1);
    CHECK(backend.count_calls("prepare:capture") == 1);
    CHECK(backend.count_calls("prepare:playback") == 1);
}

void TestPollTimeout()
{
    std::printf("cycle: a poll that never wakes fails after the retries\n");
    FakePcmBackend backend(true, 1, true, 1, 128);
    FakeCycle cycle(backend, MakeConfig(64, 2));

    RestartCounter restarts;
    FakeCycle::Callbacks callbacks;
    callbacks.arg = &restarts;
    callbacks.restart = RestartThunk;
    cycle.set_callbacks(callbacks);

    for (std::int32_t i = 0; i < 6; ++i) {
        backend.script_poll(0);
    }

    const WaitResult result = cycle.wait();
    CHECK(result.status == -5);
    CHECK(restarts.count == 5);
}

void TestReventsSlices()
{
    std::printf("cycle: each stream checks only its own descriptors\n");
    FakePcmBackend backend(true, 1, true, 1, 128);
    FakeCycle cycle(backend, MakeConfig(64, 2));

    backend.capture().avail = 64;
    backend.playback().avail = 64;
    CHECK(cycle.wait().status == 0);
    CHECK(backend.revents_nfds(Stream::Playback) == 1);
    CHECK(backend.revents_nfds(Stream::Capture) == 1);
}

void TestPollErrors()
{
    std::printf("cycle: poll errors and EINTR\n");
    FakePcmBackend backend(true, 1, true, 1, 128);
    FakeCycle cycle(backend, MakeConfig(64, 2));

    errno = EINTR;
    backend.script_poll(-1);
    CHECK(cycle.wait().status == -2);

    errno = EIO;
    backend.script_poll(-1);
    CHECK(cycle.wait().status == -3);
}

void TestStartStopSynced()
{
    std::printf("cycle: start and stop when the streams are not synced\n");
    FakePcmBackend backend(true, 2, true, 2, 128);
    FakeCycle cycle(backend, MakeConfig(64, 2, true));

    CHECK(cycle.start() == 0);
    const std::vector<std::string> expected = {
        "prepare:playback",
        "prepare:capture",
        "begin:playback:128",
        "silence:playback:0:128",
        "silence:playback:1:128",
        "commit:playback:0:128",
        "start:playback",
        "start:capture",
    };
    CHECK(backend.log() == expected);

    backend.clear_log();
    CHECK(cycle.stop() == 0);
    const std::vector<std::string> expected_stop = {
        "drop:playback",
        "drop:capture",
    };
    CHECK(backend.log() == expected_stop);
}

void TestStartStopLinked()
{
    std::printf("cycle: start and stop when the streams are synced\n");
    FakePcmBackend backend(true, 2, true, 2, 128);
    FakeCycle cycle(backend, MakeConfig(64, 2, false));

    CHECK(cycle.start() == 0);
    CHECK(backend.has_call("prepare:playback"));
    CHECK(!backend.has_call("prepare:capture"));
    CHECK(backend.has_call("start:playback"));
    CHECK(!backend.has_call("start:capture"));

    backend.clear_log();
    CHECK(cycle.stop() == 0);
    CHECK(backend.has_call("drop:playback"));
    CHECK(!backend.has_call("drop:capture"));
}

void TestStartNeedsFullBuffer()
{
    std::printf("cycle: start needs the whole device buffer\n");
    FakePcmBackend backend(true, 1, true, 1, 128);
    FakeCycle cycle(backend, MakeConfig(64, 2));

    backend.playback().avail = 64;
    CHECK(cycle.start() == -1);
}

void TestReadWrite()
{
    std::printf("cycle: read and write copy through the mapped areas\n");
    FakePcmBackend backend(true, 2, true, 2, 128);
    FakeCycle cycle(backend, MakeConfig(64, 2));

    for (std::size_t channel = 0; channel < 2; ++channel) {
        for (std::size_t frame = 0; frame < 128; ++frame) {
            backend.capture().data[channel][frame] = static_cast<float>(channel * 1000 + frame);
        }
    }

    IoContext context;
    context.capture_out.assign(2, std::vector<float>(64, -1.0f));
    context.playback_in.assign(2, std::vector<float>(64, 0.0f));
    for (std::size_t channel = 0; channel < 2; ++channel) {
        for (std::size_t frame = 0; frame < 64; ++frame) {
            context.playback_in[channel][frame] = static_cast<float>(channel * 100 + frame);
        }
    }

    FakeCycle::Callbacks callbacks;
    callbacks.arg = &context;
    callbacks.read_input = ReadThunk;
    callbacks.write_output = WriteThunk;
    cycle.set_callbacks(callbacks);

    CHECK(cycle.read(64) == 0);
    CHECK(context.capture_out[0][0] == 0.0f);
    CHECK(context.capture_out[0][63] == 63.0f);
    CHECK(context.capture_out[1][0] == 1000.0f);
    CHECK(context.capture_out[1][63] == 1063.0f);

    CHECK(cycle.write(64) == 0);
    for (std::size_t channel = 0; channel < 2; ++channel) {
        for (std::size_t frame = 0; frame < 64; ++frame) {
            CHECK(backend.playback().data[channel][frame] == static_cast<float>(channel * 100 + frame));
        }
    }
}

void TestPartialContiguous()
{
    std::printf("cycle: a short mapped area is looped over\n");
    FakePcmBackend backend(true, 1, true, 1, 128);
    FakeCycle cycle(backend, MakeConfig(64, 2));

    IoContext context;
    context.capture_out.assign(1, std::vector<float>(64, 0.0f));
    context.playback_in.assign(1, std::vector<float>(64, 5.0f));

    FakeCycle::Callbacks callbacks;
    callbacks.arg = &context;
    callbacks.read_input = ReadThunk;
    callbacks.write_output = WriteThunk;
    cycle.set_callbacks(callbacks);

    backend.capture().contiguous = 32;
    CHECK(cycle.read(64) == 0);
    CHECK(context.read_offsets.size() == 2);
    CHECK(context.read_offsets[0] == 0);
    CHECK(context.read_offsets[1] == 32);

    context.write_offsets.clear();
    backend.playback().contiguous = 32;
    CHECK(cycle.write(64) == 0);
    CHECK(context.write_offsets.size() == 2);
    CHECK(context.write_offsets[0] == 0);
    CHECK(context.write_offsets[1] == 32);
    CHECK(backend.count_calls("commit:playback:0:32") == 2);
}

void TestSilenceUntouched()
{
    std::printf("cycle: a channel the adapter does not write is silenced\n");
    FakePcmBackend backend(false, 0, true, 2, 128);
    FakeCycle cycle(backend, MakeConfig(64, 2));

    IoContext context;
    context.playback_in.assign(2, std::vector<float>(64, 7.0f));
    context.mark_channels = 1; // only channel 0 is written
    for (std::size_t channel = 0; channel < 2; ++channel) {
        std::fill(backend.playback().data[channel].begin(), backend.playback().data[channel].end(), 1.0f);
    }

    FakeCycle::Callbacks callbacks;
    callbacks.arg = &context;
    callbacks.write_output = WriteThunk;
    cycle.set_callbacks(callbacks);

    CHECK(cycle.write(64) == 0);
    CHECK(backend.playback().data[0][0] == 7.0f);
    CHECK(backend.playback().data[0][63] == 7.0f);
    CHECK(backend.playback().data[1][0] == 0.0f);
    CHECK(backend.playback().data[1][63] == 0.0f);
    CHECK(backend.has_call("silence:playback:1:64"));
}

} // namespace

int main()
{
    TestAvailRounding();
    TestCycleCounts();
    TestXrunOnAvail();
    TestXrunOnRevents();
    TestSuspended();
    TestPollTimeout();
    TestPollErrors();
    TestReventsSlices();
    TestStartStopSynced();
    TestStartStopLinked();
    TestStartNeedsFullBuffer();
    TestReadWrite();
    TestPartialContiguous();
    TestSilenceUntouched();

    std::printf("%d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}

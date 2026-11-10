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

static std::int32_t gFailures = 0;
static std::int32_t gChecks = 0;

#define CHECK(cond)                                                         \
    do {                                                                    \
        gChecks++;                                                         \
        if (!(cond)) {                                                      \
            gFailures++;                                                   \
            std::printf("  FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                   \
    } while (0)

namespace {

using FakeCycle = Cycle<FakePcmBackend>;

Config makeConfig(std::uint32_t frames, std::uint32_t periods, bool notSynced = false)
{
    Config config;
    config.m_framesPerCycle = frames;
    config.m_bufferFrames = frames * periods;
    config.m_periodUsecs = 1000;
    config.m_captureAndPlaybackNotSynced = notSynced;
    return config;
}

struct RestartCounter {
    std::int32_t m_count = 0;
};

std::int32_t restartThunk(void* arg)
{
    ++static_cast<RestartCounter*>(arg)->m_count;
    return 0;
}

struct IoContext {
    std::vector<std::vector<float>> m_captureOut;
    std::vector<std::vector<float>> m_playbackIn;
    std::vector<std::ptrdiff_t> m_readOffsets;
    std::vector<std::ptrdiff_t> m_writeOffsets;
    bool m_markDone = true;
    std::size_t m_markChannels = std::numeric_limits<std::size_t>::max();
};

void readThunk(void* arg, FakeCycle&, const MappedFrames& mapped, std::uint32_t origFrames, std::ptrdiff_t nread)
{
    auto& context = *static_cast<IoContext*>(arg);
    context.m_readOffsets.push_back(nread);
    CHECK(static_cast<std::uint32_t>(mapped.m_frames) <= origFrames);
    for (std::size_t channel = 0; channel < mapped.m_channelCount; ++channel) {
        const auto* source = reinterpret_cast<const float*>(mapped.m_channels[channel].m_data);
        const std::size_t stride = mapped.m_channels[channel].m_stride / sizeof(float);
        for (std::size_t frame = 0; frame < mapped.m_frames; ++frame) {
            context.m_captureOut[channel][static_cast<std::size_t>(nread) + frame] = source[frame * stride];
        }
    }
}

void writeThunk(void* arg, FakeCycle& cycle, const MappedFrames& mapped, std::uint32_t origFrames,
    std::ptrdiff_t nwritten)
{
    auto& context = *static_cast<IoContext*>(arg);
    context.m_writeOffsets.push_back(nwritten);
    CHECK(static_cast<std::uint32_t>(mapped.m_frames) <= origFrames);
    for (std::size_t channel = 0; channel < mapped.m_channelCount; ++channel) {
        if (context.m_markDone && channel >= context.m_markChannels) {
            break;
        }
        auto* destination = reinterpret_cast<float*>(mapped.m_channels[channel].m_data);
        const std::size_t stride = mapped.m_channels[channel].m_stride / sizeof(float);
        for (std::size_t frame = 0; frame < mapped.m_frames; ++frame) {
            destination[frame * stride] = context.m_playbackIn[channel][static_cast<std::size_t>(nwritten) + frame];
        }
        if (context.m_markDone) {
            cycle.markChannelDone(channel);
        }
    }
}

void testAvailRounding()
{
    std::printf("cycle: avail rounds down to a period\n");
    FakePcmBackend backend(true, 2, true, 2, 128);
    FakeCycle cycle(backend, makeConfig(64, 2));

    backend.capture().m_avail = 100;
    backend.playback().m_avail = 100;
    WaitResult result = cycle.wait();
    CHECK(result.m_status == 0);
    CHECK(result.m_frames == 64);

    backend.capture().m_avail = 128;
    backend.playback().m_avail = 128;
    result = cycle.wait();
    CHECK(result.m_status == 0);
    CHECK(result.m_frames == 128);

    backend.capture().m_avail = 63;
    backend.playback().m_avail = 63;
    result = cycle.wait();
    CHECK(result.m_status == 0);
    CHECK(result.m_frames == 0);
}

void testCycleCounts()
{
    std::printf("cycle: one wait and one read per period\n");
    FakePcmBackend backend(true, 2, true, 2, 128);
    FakeCycle cycle(backend, makeConfig(64, 2));

    IoContext context;
    context.m_captureOut.assign(2, std::vector<float>(64, 0.0f));
    context.m_playbackIn.assign(2, std::vector<float>(64, 0.0f));
    for (std::size_t channel = 0; channel < 2; ++channel) {
        for (std::size_t frame = 0; frame < 128; ++frame) {
            backend.capture().m_data[channel][frame] = static_cast<float>(channel * 1000 + frame);
        }
    }

    FakeCycle::Callbacks callbacks;
    callbacks.m_arg = &context;
    callbacks.m_readInput = readThunk;
    callbacks.m_writeOutput = writeThunk;
    cycle.setCallbacks(callbacks);

    for (std::int32_t i = 0; i < 100; ++i) {
        backend.capture().m_avail = 64;
        backend.playback().m_avail = 64;
        const WaitResult result = cycle.wait();
        CHECK(result.m_status == 0);
        CHECK(result.m_frames == 64);
        CHECK(cycle.read(64) == 0);
        CHECK(cycle.write(64) == 0);
    }

    CHECK(context.m_readOffsets.size() == 100);
    CHECK(context.m_writeOffsets.size() == 100);
    CHECK(cycle.processCount() == 100);
    CHECK(context.m_captureOut[0][0] == 0.0f);
    CHECK(context.m_captureOut[1][0] == 1000.0f);
}

void testXrunOnAvail()
{
    std::printf("cycle: an avail EPIPE recovers through the restart callback\n");
    FakePcmBackend backend(true, 1, true, 1, 128);
    FakeCycle cycle(backend, makeConfig(64, 2));

    RestartCounter restarts;
    FakeCycle::Callbacks callbacks;
    callbacks.m_arg = &restarts;
    callbacks.m_restart = restartThunk;
    cycle.setCallbacks(callbacks);

    // The xrun is reported after the first process call, as in the old driver.
    CHECK(cycle.write(0) == 0);

    backend.setStatus({ false, true, 1234.0f });
    backend.capture().m_availScript.push_back(-EPIPE);

    const WaitResult result = cycle.wait();
    CHECK(result.m_status == 0);
    CHECK(result.m_frames == 0);
    CHECK(result.m_delayedUsecs == 1234.0f);
    CHECK(cycle.xrunCount() == 1);
    CHECK(restarts.m_count == 1);
    CHECK(backend.hasCall("prepare:capture"));
    CHECK(backend.hasCall("prepare:playback"));
    CHECK(backend.hasCall("status"));
}

void testXrunOnRevents()
{
    std::printf("cycle: a POLLERR recovers through the restart callback\n");
    FakePcmBackend backend(true, 1, true, 1, 128);
    FakeCycle cycle(backend, makeConfig(64, 2));

    RestartCounter restarts;
    FakeCycle::Callbacks callbacks;
    callbacks.m_arg = &restarts;
    callbacks.m_restart = restartThunk;
    cycle.setCallbacks(callbacks);

    CHECK(cycle.write(0) == 0);
    backend.setStatus({ false, true, 999.0f });
    backend.capture().m_revents = POLLIN | POLLERR;

    const WaitResult result = cycle.wait();
    CHECK(result.m_status == 0);
    CHECK(result.m_frames == 0);
    CHECK(result.m_delayedUsecs == 999.0f);
    CHECK(cycle.xrunCount() == 1);
    CHECK(restarts.m_count == 1);
}

void testSuspended()
{
    std::printf("cycle: a suspended device is prepared before the restart\n");
    FakePcmBackend backend(true, 1, true, 1, 128);
    FakeCycle cycle(backend, makeConfig(64, 2));

    RestartCounter restarts;
    FakeCycle::Callbacks callbacks;
    callbacks.m_arg = &restarts;
    callbacks.m_restart = restartThunk;
    cycle.setCallbacks(callbacks);

    backend.setStatus({ true, false, 0.0f });
    backend.scriptPoll(0);
    backend.scriptPoll(1);
    backend.capture().m_avail = 64;
    backend.playback().m_avail = 64;

    const WaitResult result = cycle.wait();
    CHECK(result.m_status == 0);
    CHECK(result.m_frames == 64);
    CHECK(restarts.m_count == 1);
    CHECK(backend.countCalls("prepare:capture") == 1);
    CHECK(backend.countCalls("prepare:playback") == 1);
}

void testPollTimeout()
{
    std::printf("cycle: a poll that never wakes fails after the retries\n");
    FakePcmBackend backend(true, 1, true, 1, 128);
    FakeCycle cycle(backend, makeConfig(64, 2));

    RestartCounter restarts;
    FakeCycle::Callbacks callbacks;
    callbacks.m_arg = &restarts;
    callbacks.m_restart = restartThunk;
    cycle.setCallbacks(callbacks);

    for (std::int32_t i = 0; i < 6; ++i) {
        backend.scriptPoll(0);
    }

    const WaitResult result = cycle.wait();
    CHECK(result.m_status == -5);
    CHECK(restarts.m_count == 5);
}

void testReventsSlices()
{
    std::printf("cycle: each stream checks only its own descriptors\n");
    FakePcmBackend backend(true, 1, true, 1, 128);
    FakeCycle cycle(backend, makeConfig(64, 2));

    backend.capture().m_avail = 64;
    backend.playback().m_avail = 64;
    CHECK(cycle.wait().m_status == 0);
    CHECK(backend.reventsNfds(Stream::kPlayback) == 1);
    CHECK(backend.reventsNfds(Stream::kCapture) == 1);
}

void testPollErrors()
{
    std::printf("cycle: poll errors and EINTR\n");
    FakePcmBackend backend(true, 1, true, 1, 128);
    FakeCycle cycle(backend, makeConfig(64, 2));

    errno = EINTR;
    backend.scriptPoll(-1);
    CHECK(cycle.wait().m_status == -2);

    errno = EIO;
    backend.scriptPoll(-1);
    CHECK(cycle.wait().m_status == -3);
}

void testStartStopSynced()
{
    std::printf("cycle: start and stop when the streams are not synced\n");
    FakePcmBackend backend(true, 2, true, 2, 128);
    FakeCycle cycle(backend, makeConfig(64, 2, true));

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

    backend.clearLog();
    CHECK(cycle.stop() == 0);
    const std::vector<std::string> expectedStop = {
        "drop:playback",
        "drop:capture",
    };
    CHECK(backend.log() == expectedStop);
}

void testStartStopLinked()
{
    std::printf("cycle: start and stop when the streams are synced\n");
    FakePcmBackend backend(true, 2, true, 2, 128);
    FakeCycle cycle(backend, makeConfig(64, 2, false));

    CHECK(cycle.start() == 0);
    CHECK(backend.hasCall("prepare:playback"));
    CHECK(!backend.hasCall("prepare:capture"));
    CHECK(backend.hasCall("start:playback"));
    CHECK(!backend.hasCall("start:capture"));

    backend.clearLog();
    CHECK(cycle.stop() == 0);
    CHECK(backend.hasCall("drop:playback"));
    CHECK(!backend.hasCall("drop:capture"));
}

void testStartNeedsFullBuffer()
{
    std::printf("cycle: start needs the whole device buffer\n");
    FakePcmBackend backend(true, 1, true, 1, 128);
    FakeCycle cycle(backend, makeConfig(64, 2));

    backend.playback().m_avail = 64;
    CHECK(cycle.start() == -1);
}

void testReadWrite()
{
    std::printf("cycle: read and write copy through the mapped areas\n");
    FakePcmBackend backend(true, 2, true, 2, 128);
    FakeCycle cycle(backend, makeConfig(64, 2));

    for (std::size_t channel = 0; channel < 2; ++channel) {
        for (std::size_t frame = 0; frame < 128; ++frame) {
            backend.capture().m_data[channel][frame] = static_cast<float>(channel * 1000 + frame);
        }
    }

    IoContext context;
    context.m_captureOut.assign(2, std::vector<float>(64, -1.0f));
    context.m_playbackIn.assign(2, std::vector<float>(64, 0.0f));
    for (std::size_t channel = 0; channel < 2; ++channel) {
        for (std::size_t frame = 0; frame < 64; ++frame) {
            context.m_playbackIn[channel][frame] = static_cast<float>(channel * 100 + frame);
        }
    }

    FakeCycle::Callbacks callbacks;
    callbacks.m_arg = &context;
    callbacks.m_readInput = readThunk;
    callbacks.m_writeOutput = writeThunk;
    cycle.setCallbacks(callbacks);

    CHECK(cycle.read(64) == 0);
    CHECK(context.m_captureOut[0][0] == 0.0f);
    CHECK(context.m_captureOut[0][63] == 63.0f);
    CHECK(context.m_captureOut[1][0] == 1000.0f);
    CHECK(context.m_captureOut[1][63] == 1063.0f);

    CHECK(cycle.write(64) == 0);
    for (std::size_t channel = 0; channel < 2; ++channel) {
        for (std::size_t frame = 0; frame < 64; ++frame) {
            CHECK(backend.playback().m_data[channel][frame] == static_cast<float>(channel * 100 + frame));
        }
    }
}

void testPartialContiguous()
{
    std::printf("cycle: a short mapped area is looped over\n");
    FakePcmBackend backend(true, 1, true, 1, 128);
    FakeCycle cycle(backend, makeConfig(64, 2));

    IoContext context;
    context.m_captureOut.assign(1, std::vector<float>(64, 0.0f));
    context.m_playbackIn.assign(1, std::vector<float>(64, 5.0f));

    FakeCycle::Callbacks callbacks;
    callbacks.m_arg = &context;
    callbacks.m_readInput = readThunk;
    callbacks.m_writeOutput = writeThunk;
    cycle.setCallbacks(callbacks);

    backend.capture().m_contiguous = 32;
    CHECK(cycle.read(64) == 0);
    CHECK(context.m_readOffsets.size() == 2);
    CHECK(context.m_readOffsets[0] == 0);
    CHECK(context.m_readOffsets[1] == 32);

    context.m_writeOffsets.clear();
    backend.playback().m_contiguous = 32;
    CHECK(cycle.write(64) == 0);
    CHECK(context.m_writeOffsets.size() == 2);
    CHECK(context.m_writeOffsets[0] == 0);
    CHECK(context.m_writeOffsets[1] == 32);
    CHECK(backend.countCalls("commit:playback:0:32") == 2);
}

void testSilenceUntouched()
{
    std::printf("cycle: a channel the adapter does not write is silenced\n");
    FakePcmBackend backend(false, 0, true, 2, 128);
    FakeCycle cycle(backend, makeConfig(64, 2));

    IoContext context;
    context.m_playbackIn.assign(2, std::vector<float>(64, 7.0f));
    context.m_markChannels = 1; // only channel 0 is written
    for (std::size_t channel = 0; channel < 2; ++channel) {
        std::fill(backend.playback().m_data[channel].begin(), backend.playback().m_data[channel].end(), 1.0f);
    }

    FakeCycle::Callbacks callbacks;
    callbacks.m_arg = &context;
    callbacks.m_writeOutput = writeThunk;
    cycle.setCallbacks(callbacks);

    CHECK(cycle.write(64) == 0);
    CHECK(backend.playback().m_data[0][0] == 7.0f);
    CHECK(backend.playback().m_data[0][63] == 7.0f);
    CHECK(backend.playback().m_data[1][0] == 0.0f);
    CHECK(backend.playback().m_data[1][63] == 0.0f);
    CHECK(backend.hasCall("silence:playback:1:64"));
}

} // namespace

int main()
{
    testAvailRounding();
    testCycleCounts();
    testXrunOnAvail();
    testXrunOnRevents();
    testSuspended();
    testPollTimeout();
    testPollErrors();
    testReventsSlices();
    testStartStopSynced();
    testStartStopLinked();
    testStartNeedsFullBuffer();
    testReadWrite();
    testPartialContiguous();
    testSilenceUntouched();

    std::printf("%d checks, %d failed\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}

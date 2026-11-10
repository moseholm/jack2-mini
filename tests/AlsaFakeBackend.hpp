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

#pragma once

#include "PcmCycle.hpp"

#include <poll.h>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <string_view>
#include <vector>

namespace jackmini::alsa {

// A device that never touches a card.  It records every call, scripts the poll and avail results, and keeps one float
// buffer per channel, so the cycle logic and the adapter callbacks can be checked without ALSA.
class FakePcmBackend {
public:
    struct StreamState {
        bool m_present = false;
        std::int32_t m_channels = 0;
        std::ptrdiff_t m_avail = 0;
        std::deque<std::ptrdiff_t> m_availScript;
        std::uint16_t m_revents = 0;
        std::size_t m_contiguous = 0; // 0 means "as much as requested"
        std::vector<std::vector<float>> m_data;
        std::int32_t m_prepareResult = 0;
        std::int32_t m_startResult = 0;
        std::int32_t m_dropResult = 0;
        std::int32_t m_beginResult = 0;
        std::int32_t m_commitResult = 0;
    };

    FakePcmBackend(bool capture, std::int32_t captureChannels, bool playback, std::int32_t playbackChannels,
        std::size_t bufferFrames)
        : m_bufferFrames(bufferFrames)
    {
        m_capture.m_present = capture;
        m_capture.m_channels = captureChannels;
        m_capture.m_avail = static_cast<std::ptrdiff_t>(bufferFrames);
        m_capture.m_revents = POLLIN;
        m_capture.m_data.resize(static_cast<std::size_t>(captureChannels), std::vector<float>(bufferFrames, 0.0f));

        m_playback.m_present = playback;
        m_playback.m_channels = playbackChannels;
        m_playback.m_avail = static_cast<std::ptrdiff_t>(bufferFrames);
        m_playback.m_revents = POLLOUT;
        m_playback.m_data.resize(static_cast<std::size_t>(playbackChannels), std::vector<float>(bufferFrames, 0.0f));
    }

    // The backend concept.
    bool hasCapture() const { return m_capture.m_present; }
    bool hasPlayback() const { return m_playback.m_present; }
    std::size_t captureChannels() const { return static_cast<std::size_t>(m_capture.m_channels); }
    std::size_t playbackChannels() const { return static_cast<std::size_t>(m_playback.m_channels); }

    std::int32_t pollDescriptorCount(Stream) const { return 1; }

    std::int32_t pollDescriptors(Stream stream, struct pollfd* fds, nfds_t nfds)
    {
        if (nfds == 0) {
            return -1;
        }
        fds[0].fd = stream == Stream::kCapture ? 101 : 100;
        fds[0].events = stream == Stream::kCapture ? POLLIN : POLLOUT;
        fds[0].revents = 0;
        return 1;
    }

    std::int32_t poll(struct pollfd*, nfds_t, std::int32_t timeout)
    {
        m_log.push_back("poll:" + std::to_string(timeout));
        if (m_pollScript.empty()) {
            m_lastPoll = m_pollResult;
        } else {
            m_lastPoll = m_pollScript.front();
            m_pollScript.pop_front();
        }
        return m_lastPoll;
    }

    // A poll that timed out reports no events, as the kernel does.
    std::int32_t pollRevents(Stream stream, struct pollfd*, nfds_t nfds, std::uint16_t* revents)
    {
        StreamState& state = streamState(stream);
        m_reventsNfds[stream == Stream::kCapture ? 1 : 0] = nfds;
        *revents = m_lastPoll == 0 ? 0 : state.m_revents;
        return 0;
    }

    std::uint64_t nowUs()
    {
        const std::uint64_t now = m_now;
        m_now += 500;
        return now;
    }

    std::ptrdiff_t avail(Stream stream)
    {
        StreamState& state = streamState(stream);
        if (state.m_availScript.empty()) {
            return state.m_avail;
        }
        const std::ptrdiff_t value = state.m_availScript.front();
        state.m_availScript.pop_front();
        return value;
    }

    std::int32_t begin(Stream stream, MappedFrames& mapped)
    {
        StreamState& state = streamState(stream);
        m_log.push_back(std::string("begin:") + name(stream) + ":" + std::to_string(mapped.m_requested));
        if (state.m_beginResult < 0) {
            return state.m_beginResult;
        }
        mapped.m_offset = 0;
        mapped.m_frames = state.m_contiguous == 0 ? mapped.m_requested
                                                  : std::min(mapped.m_requested, state.m_contiguous);
        for (std::size_t channel = 0; channel < mapped.m_channelCount; ++channel) {
            mapped.m_channels[channel].m_data = reinterpret_cast<std::byte*>(state.m_data[channel].data() + mapped.m_offset);
            mapped.m_channels[channel].m_stride = sizeof(float);
        }
        return 0;
    }

    std::int32_t commit(Stream stream, std::size_t offset, std::size_t frames)
    {
        StreamState& state = streamState(stream);
        m_log.push_back(std::string("commit:") + name(stream) + ":" + std::to_string(offset) + ":" + std::to_string(frames));
        return state.m_commitResult;
    }

    void silence(Stream stream, std::size_t channel, std::size_t frames)
    {
        StreamState& state = streamState(stream);
        m_log.push_back(std::string("silence:") + name(stream) + ":" + std::to_string(channel) + ":" + std::to_string(frames));
        std::fill_n(state.m_data[channel].begin(), std::min(frames, state.m_data[channel].size()), 0.0f);
    }

    std::int32_t prepare(Stream stream)
    {
        StreamState& state = streamState(stream);
        m_log.push_back(std::string("prepare:") + name(stream));
        return state.m_prepareResult;
    }

    std::int32_t start(Stream stream)
    {
        StreamState& state = streamState(stream);
        m_log.push_back(std::string("start:") + name(stream));
        return state.m_startResult;
    }

    std::int32_t drop(Stream stream)
    {
        StreamState& state = streamState(stream);
        m_log.push_back(std::string("drop:") + name(stream));
        return state.m_dropResult;
    }

    std::int32_t status(XrunStatus& out)
    {
        m_log.push_back("status");
        out = m_status;
        return m_statusResult;
    }

    // Test controls.
    StreamState& capture() { return m_capture; }
    StreamState& playback() { return m_playback; }
    const std::vector<std::string>& log() const { return m_log; }
    nfds_t reventsNfds(Stream stream) const { return m_reventsNfds[stream == Stream::kCapture ? 1 : 0]; }
    void clearLog() { m_log.clear(); }
    void scriptPoll(std::int32_t result) { m_pollScript.push_back(result); }
    void setPollResult(std::int32_t result) { m_pollResult = result; }
    void setStatus(const XrunStatus& status) { m_status = status; }
    void setStatusResult(std::int32_t result) { m_statusResult = result; }
    std::size_t bufferFrames() const { return m_bufferFrames; }

    bool hasCall(std::string_view text) const
    {
        return std::find(m_log.begin(), m_log.end(), text) != m_log.end();
    }

    std::size_t countCalls(std::string_view text) const
    {
        return static_cast<std::size_t>(std::count(m_log.begin(), m_log.end(), text));
    }

private:
    static const char* name(Stream stream)
    {
        return stream == Stream::kCapture ? "capture" : "playback";
    }

    StreamState& streamState(Stream stream)
    {
        return stream == Stream::kCapture ? m_capture : m_playback;
    }

    std::size_t m_bufferFrames;
    StreamState m_capture;
    StreamState m_playback;
    std::vector<std::string> m_log;
    std::deque<std::int32_t> m_pollScript;
    std::int32_t m_pollResult = 1;
    std::int32_t m_lastPoll = 1;
    std::array<nfds_t, 2> m_reventsNfds = { 0, 0 };
    XrunStatus m_status;
    std::int32_t m_statusResult = 0;
    std::uint64_t m_now = 0;
};

} // namespace jackmini::alsa

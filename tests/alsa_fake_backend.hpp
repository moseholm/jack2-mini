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

#ifndef JACKMINI_TESTS_ALSA_FAKE_BACKEND_HPP
#define JACKMINI_TESTS_ALSA_FAKE_BACKEND_HPP

#include "pcm_cycle.hpp"

#include <poll.h>
#include <algorithm>
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
        bool present = false;
        int channels = 0;
        std::ptrdiff_t avail = 0;
        std::deque<std::ptrdiff_t> avail_script;
        unsigned short revents = 0;
        std::size_t contiguous = 0; // 0 means "as much as requested"
        std::vector<std::vector<float>> data;
        int prepare_result = 0;
        int start_result = 0;
        int drop_result = 0;
        int begin_result = 0;
        int commit_result = 0;
    };

    FakePcmBackend(bool capture, int capture_channels, bool playback, int playback_channels,
        std::size_t buffer_frames)
        : buffer_frames_(buffer_frames)
    {
        capture_.present = capture;
        capture_.channels = capture_channels;
        capture_.avail = static_cast<std::ptrdiff_t>(buffer_frames);
        capture_.revents = POLLIN;
        capture_.data.resize(static_cast<std::size_t>(capture_channels), std::vector<float>(buffer_frames, 0.0f));

        playback_.present = playback;
        playback_.channels = playback_channels;
        playback_.avail = static_cast<std::ptrdiff_t>(buffer_frames);
        playback_.revents = POLLOUT;
        playback_.data.resize(static_cast<std::size_t>(playback_channels), std::vector<float>(buffer_frames, 0.0f));
    }

    // The backend concept.
    bool has_capture() const { return capture_.present; }
    bool has_playback() const { return playback_.present; }
    std::size_t capture_channels() const { return static_cast<std::size_t>(capture_.channels); }
    std::size_t playback_channels() const { return static_cast<std::size_t>(playback_.channels); }

    int poll_descriptor_count(Stream) const { return 1; }

    int poll_descriptors(Stream stream, struct pollfd* fds, nfds_t nfds)
    {
        if (nfds == 0) {
            return -1;
        }
        fds[0].fd = stream == Stream::Capture ? 101 : 100;
        fds[0].events = stream == Stream::Capture ? POLLIN : POLLOUT;
        fds[0].revents = 0;
        return 1;
    }

    int poll(struct pollfd*, nfds_t, int timeout)
    {
        log_.push_back("poll:" + std::to_string(timeout));
        if (poll_script_.empty()) {
            last_poll_ = poll_result_;
        } else {
            last_poll_ = poll_script_.front();
            poll_script_.pop_front();
        }
        return last_poll_;
    }

    // A poll that timed out reports no events, as the kernel does.
    int poll_revents(Stream stream, struct pollfd*, nfds_t nfds, unsigned short* revents)
    {
        StreamState& state = stream_state(stream);
        revents_nfds_[stream == Stream::Capture ? 1 : 0] = nfds;
        *revents = last_poll_ == 0 ? 0 : state.revents;
        return 0;
    }

    std::uint64_t now_us()
    {
        const std::uint64_t now = now_;
        now_ += 500;
        return now;
    }

    std::ptrdiff_t avail(Stream stream)
    {
        StreamState& state = stream_state(stream);
        if (state.avail_script.empty()) {
            return state.avail;
        }
        const std::ptrdiff_t value = state.avail_script.front();
        state.avail_script.pop_front();
        return value;
    }

    int begin(Stream stream, MappedFrames& mapped)
    {
        StreamState& state = stream_state(stream);
        log_.push_back(std::string("begin:") + name(stream) + ":" + std::to_string(mapped.requested));
        if (state.begin_result < 0) {
            return state.begin_result;
        }
        mapped.offset = 0;
        mapped.frames = state.contiguous == 0 ? mapped.requested
                                              : std::min(mapped.requested, state.contiguous);
        for (std::size_t channel = 0; channel < mapped.channel_count; ++channel) {
            mapped.channels[channel].data = reinterpret_cast<std::byte*>(state.data[channel].data() + mapped.offset);
            mapped.channels[channel].stride = sizeof(float);
        }
        return 0;
    }

    int commit(Stream stream, std::size_t offset, std::size_t frames)
    {
        StreamState& state = stream_state(stream);
        log_.push_back(std::string("commit:") + name(stream) + ":" + std::to_string(offset) + ":" + std::to_string(frames));
        return state.commit_result;
    }

    void silence(Stream stream, std::size_t channel, std::size_t frames)
    {
        StreamState& state = stream_state(stream);
        log_.push_back(std::string("silence:") + name(stream) + ":" + std::to_string(channel) + ":" + std::to_string(frames));
        std::fill_n(state.data[channel].begin(), std::min(frames, state.data[channel].size()), 0.0f);
    }

    int prepare(Stream stream)
    {
        StreamState& state = stream_state(stream);
        log_.push_back(std::string("prepare:") + name(stream));
        return state.prepare_result;
    }

    int start(Stream stream)
    {
        StreamState& state = stream_state(stream);
        log_.push_back(std::string("start:") + name(stream));
        return state.start_result;
    }

    int drop(Stream stream)
    {
        StreamState& state = stream_state(stream);
        log_.push_back(std::string("drop:") + name(stream));
        return state.drop_result;
    }

    int status(XrunStatus& out)
    {
        log_.push_back("status");
        out = status_;
        return status_result_;
    }

    // Test controls.
    StreamState& capture() { return capture_; }
    StreamState& playback() { return playback_; }
    const std::vector<std::string>& log() const { return log_; }
    nfds_t revents_nfds(Stream stream) const { return revents_nfds_[stream == Stream::Capture ? 1 : 0]; }
    void clear_log() { log_.clear(); }
    void script_poll(int result) { poll_script_.push_back(result); }
    void set_poll_result(int result) { poll_result_ = result; }
    void set_status(const XrunStatus& status) { status_ = status; }
    void set_status_result(int result) { status_result_ = result; }
    std::size_t buffer_frames() const { return buffer_frames_; }

    bool has_call(std::string_view text) const
    {
        return std::find(log_.begin(), log_.end(), text) != log_.end();
    }

    std::size_t count_calls(std::string_view text) const
    {
        return static_cast<std::size_t>(std::count(log_.begin(), log_.end(), text));
    }

private:
    static const char* name(Stream stream)
    {
        return stream == Stream::Capture ? "capture" : "playback";
    }

    StreamState& stream_state(Stream stream)
    {
        return stream == Stream::Capture ? capture_ : playback_;
    }

    std::size_t buffer_frames_;
    StreamState capture_;
    StreamState playback_;
    std::vector<std::string> log_;
    std::deque<int> poll_script_;
    int poll_result_ = 1;
    int last_poll_ = 1;
    nfds_t revents_nfds_[2] = { 0, 0 };
    XrunStatus status_;
    int status_result_ = 0;
    std::uint64_t now_ = 0;
};

} // namespace jackmini::alsa

#endif

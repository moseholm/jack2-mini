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

#include <poll.h>
#include <algorithm>
#include <cerrno>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace jackmini::alsa {

enum class Stream {
    Capture,
    Playback,
};

// One channel of a mapped device buffer.  The core never looks at the sample format; the backend and the adapter do.
struct ChannelArea {
    std::byte* data = nullptr;
    std::size_t stride = 0; // bytes between two frames of this channel
};

// A contiguous piece of a device buffer.  The backend fills the channel areas; the core sets requested before begin().
struct MappedFrames {
    std::size_t offset = 0; // frame offset for commit
    std::size_t frames = 0; // contiguous frames the backend can give
    std::size_t requested = 0; // contiguous frames the caller wants
    ChannelArea* channels = nullptr;
    std::size_t channel_count = 0;
};

struct XrunStatus {
    bool suspended = false;
    bool xrun = false;
    float delayed_usecs = 0.0f;
};

struct WaitResult {
    std::ptrdiff_t frames = 0; // ready frames rounded down to a period; 0 after a recovered xrun
    int status = -1; // 0 is a normal cycle; the negatives follow the old ALSA driver
    float delayed_usecs = 0.0f;
};

struct Config {
    std::uint32_t frames_per_cycle = 0;
    std::uint32_t buffer_frames = 0; // frames in the device buffer, usually frames_per_cycle * periods
    std::uint64_t period_usecs = 0;
    bool capture_and_playback_not_synced = false;
};

// The set of device operations the cycle needs.  The cycle is a template over this concept, so the realtime path
// makes no virtual call.  A backend is created after the device is open, and its stream and descriptor counts do not
// change while the cycle runs.
template <class B>
concept PcmBackend = requires(B& backend,
    Stream stream,
    struct pollfd* fds,
    nfds_t nfds,
    unsigned short* revents,
    MappedFrames& mapped,
    std::size_t offset,
    std::size_t frames,
    std::size_t channel,
    XrunStatus& xrun_status) {
    { backend.has_capture() } -> std::convertible_to<bool>;
    { backend.has_playback() } -> std::convertible_to<bool>;
    { backend.capture_channels() } -> std::convertible_to<std::size_t>;
    { backend.playback_channels() } -> std::convertible_to<std::size_t>;
    { backend.poll_descriptor_count(stream) } -> std::convertible_to<int>;
    { backend.poll_descriptors(stream, fds, nfds) } -> std::convertible_to<int>;
    { backend.poll_revents(stream, fds, nfds, revents) } -> std::convertible_to<int>;
    { backend.poll(fds, nfds, 0) } -> std::convertible_to<int>;
    { backend.now_us() } -> std::convertible_to<std::uint64_t>;
    { backend.avail(stream) } -> std::convertible_to<std::ptrdiff_t>;
    { backend.begin(stream, mapped) } -> std::convertible_to<int>;
    { backend.commit(stream, offset, frames) } -> std::convertible_to<int>;
    { backend.silence(stream, channel, frames) } -> std::same_as<void>;
    { backend.prepare(stream) } -> std::convertible_to<int>;
    { backend.start(stream) } -> std::convertible_to<int>;
    { backend.drop(stream) } -> std::convertible_to<int>;
    { backend.status(xrun_status) } -> std::convertible_to<int>;
};

// The device cycle: start, wait, read, write, stop, and the xrun recovery.  The cycle itself allocates only in its
// constructor.  The callbacks are plain function pointers, and the backend is a template parameter.
template <PcmBackend Backend>
class Cycle {
public:
    struct Callbacks {
        void* arg = nullptr;
        // The mapped buffer belongs to the cycle and is valid only during the call.
        void (*read_input)(void* arg, Cycle& cycle, const MappedFrames& mapped,
            std::uint32_t orig_frames, std::ptrdiff_t nread) = nullptr;
        void (*write_output)(void* arg, Cycle& cycle, const MappedFrames& mapped,
            std::uint32_t orig_frames, std::ptrdiff_t nwritten) = nullptr;
        void (*clear_output)(void* arg) = nullptr;
        int (*restart)(void* arg) = nullptr;
        void (*set_time)(void* arg, std::uint64_t usecs) = nullptr;
    };

    Cycle(Backend& backend, const Config& config);
    Cycle(const Cycle&) = delete;
    Cycle& operator=(const Cycle&) = delete;

    void set_callbacks(const Callbacks& callbacks) { callbacks_ = callbacks; }

    int start();
    int stop();

    // Waits for the next period.  A returned frames == 0 with status == 0 means an xrun was recovered.
    WaitResult wait();

    int read(std::uint32_t frames);
    int write(std::uint32_t frames);

    // The adapter calls this for each channel it has filled in write_output().
    void mark_channel_done(std::size_t channel);

    int xrun_count() const { return xrun_count_; }
    int poll_late() const { return poll_late_; }
    int process_count() const { return process_count_; }
    std::uint64_t last_wait_usecs() const { return last_wait_usecs_; }

private:
    static constexpr int kMaxRetryCount = 5;
    static constexpr int kXrunReportDelay = 0;

    int xrun_recovery(float* delayed_usecs);
    void silence_untouched(std::size_t frames);
    void reset_channels_not_done();
    bool channel_not_done(std::size_t channel) const;

    Backend& backend_;
    Config config_;
    Callbacks callbacks_;

    int poll_timeout_ms_;
    std::vector<struct pollfd> poll_fds_;
    std::vector<ChannelArea> capture_areas_;
    std::vector<ChannelArea> playback_areas_;
    MappedFrames capture_map_;
    MappedFrames playback_map_;
    std::vector<std::uint64_t> channels_not_done_;
    std::vector<std::size_t> silent_;
    std::uint64_t channel_mask_last_ = 0;

    std::uint64_t poll_last_ = 0;
    std::uint64_t poll_next_ = 0;
    std::uint64_t last_wait_usecs_ = 0;
    int poll_late_ = 0;
    int xrun_count_ = 0;
    int process_count_ = 0;
};

template <PcmBackend Backend>
Cycle<Backend>::Cycle(Backend& backend, const Config& config)
    : backend_(backend)
    , config_(config)
{
    const int capture_nfds = backend_.has_capture() ? backend_.poll_descriptor_count(Stream::Capture) : 0;
    const int playback_nfds = backend_.has_playback() ? backend_.poll_descriptor_count(Stream::Playback) : 0;
    poll_fds_.resize(static_cast<std::size_t>(std::max(0, capture_nfds) + std::max(0, playback_nfds)));

    capture_areas_.resize(backend_.capture_channels());
    playback_areas_.resize(backend_.playback_channels());
    capture_map_.channels = capture_areas_.data();
    capture_map_.channel_count = capture_areas_.size();
    playback_map_.channels = playback_areas_.data();
    playback_map_.channel_count = playback_areas_.size();

    const std::size_t channels = std::max(backend_.capture_channels(), backend_.playback_channels());
    channels_not_done_.resize((channels + 63) / 64);
    if (channels % 64 != 0) {
        channel_mask_last_ = (std::uint64_t { 1 } << (channels % 64)) - 1;
    } else if (channels != 0) {
        channel_mask_last_ = ~std::uint64_t { 0 };
    }

    silent_.assign(backend_.playback_channels(), 0);
    reset_channels_not_done();

    // One and a half periods, rounded up to whole milliseconds.  The old driver passed microseconds as milliseconds,
    // so its timeout was a thousand times longer; the PCM descriptor wakes the poll long before either timeout.
    poll_timeout_ms_ = std::max(1, static_cast<int>((config_.period_usecs * 3 + 1999) / 2000));
}

template <PcmBackend Backend>
int Cycle<Backend>::start()
{
    poll_last_ = 0;
    poll_next_ = 0;

    if (backend_.has_playback()) {
        if (backend_.prepare(Stream::Playback) < 0) {
            return -1;
        }
    }

    if ((backend_.has_capture() && config_.capture_and_playback_not_synced) || !backend_.has_playback()) {
        if (backend_.prepare(Stream::Capture) < 0) {
            return -1;
        }
    }

    if (backend_.has_playback()) {
        const std::size_t full_buffer = config_.buffer_frames;
        if (backend_.avail(Stream::Playback) != static_cast<std::ptrdiff_t>(full_buffer)) {
            return -1;
        }

        playback_map_.requested = full_buffer;
        if (backend_.begin(Stream::Playback, playback_map_) < 0) {
            return -1;
        }
        if (playback_map_.frames < full_buffer) {
            return -1;
        }

        for (std::size_t channel = 0; channel < playback_areas_.size(); ++channel) {
            backend_.silence(Stream::Playback, channel, full_buffer);
            silent_[channel] = 0;
        }

        if (backend_.commit(Stream::Playback, playback_map_.offset, full_buffer) < 0) {
            return -1;
        }
        if (backend_.start(Stream::Playback) < 0) {
            return -1;
        }
    }

    if ((backend_.has_capture() && config_.capture_and_playback_not_synced) || !backend_.has_playback()) {
        if (backend_.start(Stream::Capture) < 0) {
            return -1;
        }
    }

    return 0;
}

template <PcmBackend Backend>
int Cycle<Backend>::stop()
{
    if (callbacks_.clear_output) {
        callbacks_.clear_output(callbacks_.arg);
    }

    if (backend_.has_playback()) {
        if (backend_.drop(Stream::Playback) < 0) {
            return -1;
        }
    }

    if ((!backend_.has_playback() || config_.capture_and_playback_not_synced) && backend_.has_capture()) {
        if (backend_.drop(Stream::Capture) < 0) {
            return -1;
        }
    }

    return 0;
}

template <PcmBackend Backend>
WaitResult Cycle<Backend>::wait()
{
    WaitResult result;
    result.status = -1;

    bool need_capture = backend_.has_capture();
    bool need_playback = backend_.has_playback();
    bool xrun_detected = false;
    int retry_count = 0;
    std::uint64_t poll_ret = 0;

    while ((need_playback || need_capture) && !xrun_detected) {
        std::size_t nfds = 0;
        std::size_t playback_nfds = 0;
        std::size_t capture_index = 0;

        if (need_playback) {
            const int count = backend_.poll_descriptors(
                Stream::Playback, poll_fds_.data() + nfds,
                static_cast<nfds_t>(poll_fds_.size() - nfds));
            if (count < 0) {
                result.status = -6;
                return result;
            }
            nfds += static_cast<std::size_t>(count);
            playback_nfds = static_cast<std::size_t>(count);
        }

        if (need_capture) {
            capture_index = nfds;
            const int count = backend_.poll_descriptors(
                Stream::Capture, poll_fds_.data() + nfds,
                static_cast<nfds_t>(poll_fds_.size() - nfds));
            if (count < 0) {
                result.status = -6;
                return result;
            }
            nfds += static_cast<std::size_t>(count);
        }

        // ALSA does not always set POLLERR by itself.
        for (std::size_t i = 0; i < nfds; ++i) {
            poll_fds_[i].events |= POLLERR;
        }

        const std::uint64_t poll_enter = backend_.now_us();
        if (poll_enter > poll_next_) {
            // This cycle was delayed past the next due interrupt: do not count it as a wakeup delay.
            poll_next_ = 0;
            ++poll_late_;
        }

        const int poll_result = backend_.poll(poll_fds_.data(), static_cast<nfds_t>(nfds), poll_timeout_ms_);
        if (poll_result < 0) {
            result.status = (errno == EINTR) ? -2 : -3;
            return result;
        }

        poll_ret = backend_.now_us();

        if (poll_result == 0) {
            ++retry_count;
            if (retry_count > kMaxRetryCount) {
                result.status = -5;
                return result;
            }
            const int status = xrun_recovery(&result.delayed_usecs);
            if (status != 0) {
                result.status = status;
                return result;
            }
        }

        if (callbacks_.set_time) {
            callbacks_.set_time(callbacks_.arg, poll_ret);
        }

        if (poll_next_ != 0 && poll_ret > poll_next_) {
            result.delayed_usecs = static_cast<float>(poll_ret - poll_next_);
        }
        poll_last_ = poll_ret;
        poll_next_ = poll_ret + config_.period_usecs;

        if (need_playback) {
            unsigned short revents = 0;
            if (backend_.poll_revents(Stream::Playback, poll_fds_.data(), static_cast<nfds_t>(playback_nfds),
                    &revents)
                < 0) {
                result.status = -6;
                return result;
            }
            if (revents & POLLNVAL) {
                result.status = -7;
                return result;
            }
            if (revents & POLLERR) {
                xrun_detected = true;
            }
            if (revents & POLLOUT) {
                need_playback = false;
            }
        }

        if (need_capture) {
            unsigned short revents = 0;
            if (backend_.poll_revents(Stream::Capture, poll_fds_.data() + capture_index,
                    static_cast<nfds_t>(nfds - capture_index), &revents)
                < 0) {
                result.status = -6;
                return result;
            }
            if (revents & POLLNVAL) {
                result.status = -7;
                return result;
            }
            if (revents & POLLERR) {
                xrun_detected = true;
            }
            if (revents & POLLIN) {
                need_capture = false;
            }
        }
    }

    constexpr std::ptrdiff_t kNoStream = std::numeric_limits<std::ptrdiff_t>::max();
    const std::ptrdiff_t capture_avail = backend_.has_capture() ? backend_.avail(Stream::Capture) : kNoStream;
    const std::ptrdiff_t playback_avail = backend_.has_playback() ? backend_.avail(Stream::Playback) : kNoStream;

    if (capture_avail == -EPIPE || playback_avail == -EPIPE) {
        xrun_detected = true;
    } else if (capture_avail < 0 || playback_avail < 0) {
        result.status = -8;
        return result;
    }

    if (xrun_detected) {
        result.status = xrun_recovery(&result.delayed_usecs);
        return result;
    }

    result.status = 0;
    last_wait_usecs_ = poll_ret;

    const std::ptrdiff_t avail = std::min(capture_avail, playback_avail);
    reset_channels_not_done();
    result.frames = avail - (avail % static_cast<std::ptrdiff_t>(config_.frames_per_cycle));
    return result;
}

template <PcmBackend Backend>
int Cycle<Backend>::xrun_recovery(float* delayed_usecs)
{
    XrunStatus status;
    // A failed status call is not fatal: the restart below still has a chance to recover the device.
    backend_.status(status);

    if (status.suspended) {
        if (backend_.has_capture() && backend_.prepare(Stream::Capture) < 0) {
            return -1;
        }
        if (backend_.has_playback() && backend_.prepare(Stream::Playback) < 0) {
            return -1;
        }
    }

    if (status.xrun && process_count_ > kXrunReportDelay) {
        ++xrun_count_;
        *delayed_usecs = status.delayed_usecs;
        if (backend_.has_capture() && backend_.prepare(Stream::Capture) < 0) {
            return -1;
        }
        if (backend_.has_playback() && backend_.prepare(Stream::Playback) < 0) {
            return -1;
        }
    }

    if (callbacks_.restart == nullptr) {
        return -1;
    }
    return callbacks_.restart(callbacks_.arg);
}

template <PcmBackend Backend>
int Cycle<Backend>::read(std::uint32_t frames)
{
    if (frames > config_.frames_per_cycle) {
        return -1;
    }
    if (!backend_.has_capture()) {
        return 0;
    }

    std::uint32_t remaining = frames;
    std::ptrdiff_t nread = 0;

    while (remaining != 0) {
        capture_map_.requested = remaining;
        if (backend_.begin(Stream::Capture, capture_map_) < 0) {
            return -1;
        }

        const std::size_t contiguous = std::min<std::size_t>(remaining, capture_map_.frames);
        if (contiguous == 0) {
            return -1;
        }

        if (callbacks_.read_input) {
            callbacks_.read_input(callbacks_.arg, *this, capture_map_, frames, nread);
        }

        if (backend_.commit(Stream::Capture, capture_map_.offset, contiguous) < 0) {
            return -1;
        }

        remaining -= static_cast<std::uint32_t>(contiguous);
        nread += static_cast<std::ptrdiff_t>(contiguous);
    }

    return 0;
}

template <PcmBackend Backend>
int Cycle<Backend>::write(std::uint32_t frames)
{
    ++process_count_;

    if (!backend_.has_playback()) {
        return 0;
    }
    if (frames > config_.frames_per_cycle) {
        return -1;
    }

    std::uint32_t remaining = frames;
    std::ptrdiff_t nwritten = 0;

    while (remaining != 0) {
        playback_map_.requested = remaining;
        if (backend_.begin(Stream::Playback, playback_map_) < 0) {
            return -1;
        }

        const std::size_t contiguous = std::min<std::size_t>(remaining, playback_map_.frames);
        if (contiguous == 0) {
            return -1;
        }

        if (callbacks_.write_output) {
            callbacks_.write_output(callbacks_.arg, *this, playback_map_, frames, nwritten);
        }

        silence_untouched(contiguous);

        const int err = backend_.commit(Stream::Playback, playback_map_.offset, contiguous);
        if (err < 0 && err != -EPIPE && err != -ESTRPIPE) {
            return -1;
        }

        remaining -= static_cast<std::uint32_t>(contiguous);
        nwritten += static_cast<std::ptrdiff_t>(contiguous);
    }

    return 0;
}

template <PcmBackend Backend>
void Cycle<Backend>::mark_channel_done(std::size_t channel)
{
    channels_not_done_[channel / 64] &= ~(std::uint64_t { 1 } << (channel % 64));
    silent_[channel] = 0;
}

template <PcmBackend Backend>
void Cycle<Backend>::silence_untouched(std::size_t frames)
{
    for (std::size_t channel = 0; channel < playback_areas_.size(); ++channel) {
        if (channel_not_done(channel) && silent_[channel] < config_.buffer_frames) {
            backend_.silence(Stream::Playback, channel, frames);
            silent_[channel] += frames;
        }
    }
}

template <PcmBackend Backend>
void Cycle<Backend>::reset_channels_not_done()
{
    std::fill(channels_not_done_.begin(), channels_not_done_.end(), ~std::uint64_t { 0 });
    if (!channels_not_done_.empty()) {
        channels_not_done_.back() = channel_mask_last_;
    }
}

template <PcmBackend Backend>
bool Cycle<Backend>::channel_not_done(std::size_t channel) const
{
    return (channels_not_done_[channel / 64] & (std::uint64_t { 1 } << (channel % 64))) != 0;
}

} // namespace jackmini::alsa

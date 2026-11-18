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
    kCapture,
    kPlayback,
};

// One channel of a mapped device buffer.  The core never looks at the sample format; the backend and the adapter do.
struct ChannelArea {
    std::byte* m_data = nullptr;
    std::size_t m_stride = 0; // bytes between two frames of this channel
};

// A contiguous piece of a device buffer.  The backend fills the channel areas; the core sets requested before begin().
struct MappedFrames {
    std::size_t m_offset = 0; // frame offset for commit
    std::size_t m_frames = 0; // contiguous frames the backend can give
    std::size_t m_requested = 0; // contiguous frames the caller wants
    ChannelArea* m_channels = nullptr;
    std::size_t m_channelCount = 0;
};

struct XrunStatus {
    bool m_suspended = false;
    bool m_xrun = false;
    float m_delayedUsecs = 0.0f;
};

struct WaitResult {
    std::ptrdiff_t m_frames = 0; // ready frames rounded down to a period; 0 after a recovered xrun
    std::int32_t m_status = -1; // 0 is a normal cycle; the negatives follow the old ALSA driver
    float m_delayedUsecs = 0.0f;
};

struct Config {
    std::uint32_t m_framesPerCycle = 0;
    std::uint32_t m_bufferFrames = 0; // frames in the device buffer, usually frames_per_cycle * periods
    std::uint64_t m_periodUsecs = 0;
    bool m_captureAndPlaybackNotSynced = false;
};

// The set of device operations the cycle needs.  The cycle is a template over this concept, so the realtime path
// makes no virtual call.  A backend is created after the device is open, and its stream and descriptor counts do not
// change while the cycle runs.
template <class B>
concept PcmBackend = requires(B& backend,
    Stream stream,
    struct pollfd* fds,
    nfds_t nfds,
    std::uint16_t* revents,
    MappedFrames& mapped,
    std::size_t offset,
    std::size_t frames,
    std::size_t channel,
    XrunStatus& xrunStatus) {
    { backend.hasCapture() } -> std::convertible_to<bool>;
    { backend.hasPlayback() } -> std::convertible_to<bool>;
    { backend.captureChannels() } -> std::convertible_to<std::size_t>;
    { backend.playbackChannels() } -> std::convertible_to<std::size_t>;
    { backend.pollDescriptorCount(stream) } -> std::convertible_to<std::int32_t>;
    { backend.pollDescriptors(stream, fds, nfds) } -> std::convertible_to<std::int32_t>;
    { backend.pollRevents(stream, fds, nfds, revents) } -> std::convertible_to<std::int32_t>;
    { backend.poll(fds, nfds, 0) } -> std::convertible_to<std::int32_t>;
    { backend.nowUs() } -> std::convertible_to<std::uint64_t>;
    { backend.avail(stream) } -> std::convertible_to<std::ptrdiff_t>;
    { backend.begin(stream, mapped) } -> std::convertible_to<std::int32_t>;
    { backend.commit(stream, offset, frames) } -> std::convertible_to<std::int32_t>;
    { backend.silence(stream, channel, frames) } -> std::same_as<void>;
    { backend.prepare(stream) } -> std::convertible_to<std::int32_t>;
    { backend.start(stream) } -> std::convertible_to<std::int32_t>;
    { backend.drop(stream) } -> std::convertible_to<std::int32_t>;
    { backend.status(xrunStatus) } -> std::convertible_to<std::int32_t>;
};

// The device cycle: start, wait, read, write, stop, and the xrun recovery.  The cycle itself allocates only in its
// constructor.  The callbacks are plain function pointers, and the backend is a template parameter.
template <PcmBackend Backend>
class Cycle {
public:
    struct Callbacks {
        void* m_arg = nullptr;
        // The mapped buffer belongs to the cycle and is valid only during the call.
        void (*m_readInput)(void* arg, Cycle& cycle, const MappedFrames& mapped,
            std::uint32_t origFrames, std::ptrdiff_t nread) = nullptr;
        void (*m_writeOutput)(void* arg, Cycle& cycle, const MappedFrames& mapped,
            std::uint32_t origFrames, std::ptrdiff_t nwritten) = nullptr;
        void (*m_clearOutput)(void* arg) = nullptr;
        std::int32_t (*m_restart)(void* arg) = nullptr;
        void (*m_setTime)(void* arg, std::uint64_t usecs) = nullptr;
    };

    Cycle(Backend& backend, const Config& config);
    Cycle(const Cycle&) = delete;
    Cycle& operator=(const Cycle&) = delete;

    void setCallbacks(const Callbacks& callbacks) { m_callbacks = callbacks; }

    std::int32_t start();
    std::int32_t stop();

    // Waits for the next period.  A returned frames == 0 with status == 0 means an xrun was recovered.
    WaitResult wait();

    std::int32_t read(std::uint32_t frames);
    std::int32_t write(std::uint32_t frames);

    // The adapter calls this for each channel it has filled in write_output().
    void markChannelDone(std::size_t channel);

    std::int32_t xrunCount() const { return m_xrunCount; }
    std::int32_t pollLate() const { return m_pollLate; }
    std::int32_t processCount() const { return m_processCount; }
    std::uint64_t lastWaitUsecs() const { return m_lastWaitUsecs; }

private:
    static constexpr std::int32_t kMaxRetryCount = 5;
    static constexpr std::int32_t kXrunReportDelay = 0;

    std::int32_t xrunRecovery(float* delayedUsecs);
    void silenceUntouched(std::size_t frames);
    void resetChannelsNotDone();
    bool channelNotDone(std::size_t channel) const;

    Backend& m_backend;
    Config m_config;
    Callbacks m_callbacks;

    std::int32_t m_pollTimeoutMs;
    std::vector<struct pollfd> m_pollFds;
    std::vector<ChannelArea> m_captureAreas;
    std::vector<ChannelArea> m_playbackAreas;
    MappedFrames m_captureMap;
    MappedFrames m_playbackMap;
    std::vector<std::uint64_t> m_channelsNotDone;
    std::vector<std::size_t> m_silent;
    std::uint64_t m_channelMaskLast = 0;

    std::uint64_t m_pollLast = 0;
    std::uint64_t m_pollNext = 0;
    std::uint64_t m_lastWaitUsecs = 0;
    std::int32_t m_pollLate = 0;
    std::int32_t m_xrunCount = 0;
    std::int32_t m_processCount = 0;
};

template <PcmBackend Backend>
Cycle<Backend>::Cycle(Backend& backend, const Config& config)
    : m_backend(backend)
    , m_config(config)
{
    const std::int32_t captureNfds = m_backend.hasCapture() ? m_backend.pollDescriptorCount(Stream::kCapture) : 0;
    const std::int32_t playbackNfds = m_backend.hasPlayback() ? m_backend.pollDescriptorCount(Stream::kPlayback) : 0;
    m_pollFds.resize(static_cast<std::size_t>(std::max(0, captureNfds) + std::max(0, playbackNfds)));

    m_captureAreas.resize(m_backend.captureChannels());
    m_playbackAreas.resize(m_backend.playbackChannels());
    m_captureMap.m_channels = m_captureAreas.data();
    m_captureMap.m_channelCount = m_captureAreas.size();
    m_playbackMap.m_channels = m_playbackAreas.data();
    m_playbackMap.m_channelCount = m_playbackAreas.size();

    const std::size_t channels = std::max(m_backend.captureChannels(), m_backend.playbackChannels());
    m_channelsNotDone.resize((channels + 63) / 64);
    if (channels % 64 != 0) {
        m_channelMaskLast = (std::uint64_t { 1 } << (channels % 64)) - 1;
    } else if (channels != 0) {
        m_channelMaskLast = ~std::uint64_t { 0 };
    }

    m_silent.assign(m_backend.playbackChannels(), 0);
    resetChannelsNotDone();

    // One and a half periods, rounded up to whole milliseconds.  The old driver passed microseconds as milliseconds,
    // so its timeout was a thousand times longer; the PCM descriptor wakes the poll long before either timeout.
    m_pollTimeoutMs = std::max(1, static_cast<std::int32_t>((m_config.m_periodUsecs * 3 + 1999) / 2000));
}

template <PcmBackend Backend>
std::int32_t Cycle<Backend>::start()
{
    m_pollLast = 0;
    m_pollNext = 0;

    if (m_backend.hasPlayback()) {
        if (m_backend.prepare(Stream::kPlayback) < 0) {
            return -1;
        }
    }

    if ((m_backend.hasCapture() && m_config.m_captureAndPlaybackNotSynced) || !m_backend.hasPlayback()) {
        if (m_backend.prepare(Stream::kCapture) < 0) {
            return -1;
        }
    }

    if (m_backend.hasPlayback()) {
        const std::size_t fullBuffer = m_config.m_bufferFrames;
        if (m_backend.avail(Stream::kPlayback) != static_cast<std::ptrdiff_t>(fullBuffer)) {
            return -1;
        }

        m_playbackMap.m_requested = fullBuffer;
        if (m_backend.begin(Stream::kPlayback, m_playbackMap) < 0) {
            return -1;
        }
        if (m_playbackMap.m_frames < fullBuffer) {
            return -1;
        }

        for (std::size_t channel = 0; channel < m_playbackAreas.size(); ++channel) {
            m_backend.silence(Stream::kPlayback, channel, fullBuffer);
            m_silent[channel] = 0;
        }

        if (m_backend.commit(Stream::kPlayback, m_playbackMap.m_offset, fullBuffer) < 0) {
            return -1;
        }
        if (m_backend.start(Stream::kPlayback) < 0) {
            return -1;
        }
    }

    if ((m_backend.hasCapture() && m_config.m_captureAndPlaybackNotSynced) || !m_backend.hasPlayback()) {
        if (m_backend.start(Stream::kCapture) < 0) {
            return -1;
        }
    }

    return 0;
}

template <PcmBackend Backend>
std::int32_t Cycle<Backend>::stop()
{
    if (m_callbacks.m_clearOutput) {
        m_callbacks.m_clearOutput(m_callbacks.m_arg);
    }

    if (m_backend.hasPlayback()) {
        if (m_backend.drop(Stream::kPlayback) < 0) {
            return -1;
        }
    }

    if ((!m_backend.hasPlayback() || m_config.m_captureAndPlaybackNotSynced) && m_backend.hasCapture()) {
        if (m_backend.drop(Stream::kCapture) < 0) {
            return -1;
        }
    }

    return 0;
}

template <PcmBackend Backend>
WaitResult Cycle<Backend>::wait()
{
    WaitResult result;
    result.m_status = -1;

    bool needCapture = m_backend.hasCapture();
    bool needPlayback = m_backend.hasPlayback();
    bool xrunDetected = false;
    std::int32_t retryCount = 0;
    std::uint64_t pollRet = 0;

    while ((needPlayback || needCapture) && !xrunDetected) {
        std::size_t nfds = 0;
        std::size_t playbackNfds = 0;
        std::size_t captureIndex = 0;

        if (needPlayback) {
            const std::int32_t count = m_backend.pollDescriptors(
                Stream::kPlayback, m_pollFds.data() + nfds,
                static_cast<nfds_t>(m_pollFds.size() - nfds));
            if (count < 0) {
                result.m_status = -6;
                return result;
            }
            nfds += static_cast<std::size_t>(count);
            playbackNfds = static_cast<std::size_t>(count);
        }

        if (needCapture) {
            captureIndex = nfds;
            const std::int32_t count = m_backend.pollDescriptors(
                Stream::kCapture, m_pollFds.data() + nfds,
                static_cast<nfds_t>(m_pollFds.size() - nfds));
            if (count < 0) {
                result.m_status = -6;
                return result;
            }
            nfds += static_cast<std::size_t>(count);
        }

        // ALSA does not always set POLLERR by itself.
        for (std::size_t i = 0; i < nfds; ++i) {
            m_pollFds[i].events |= POLLERR;
        }

        const std::uint64_t pollEnter = m_backend.nowUs();
        if (pollEnter > m_pollNext) {
            // This cycle was delayed past the next due interrupt: do not count it as a wakeup delay.
            m_pollNext = 0;
            ++m_pollLate;
        }

        const std::int32_t pollResult = m_backend.poll(m_pollFds.data(), static_cast<nfds_t>(nfds), m_pollTimeoutMs);
        if (pollResult < 0) {
            result.m_status = (errno == EINTR) ? -2 : -3;
            return result;
        }

        pollRet = m_backend.nowUs();

        if (pollResult == 0) {
            ++retryCount;
            if (retryCount > kMaxRetryCount) {
                result.m_status = -5;
                return result;
            }
            const std::int32_t status = xrunRecovery(&result.m_delayedUsecs);
            if (status != 0) {
                result.m_status = status;
                return result;
            }
        }

        if (m_callbacks.m_setTime) {
            m_callbacks.m_setTime(m_callbacks.m_arg, pollRet);
        }

        if (m_pollNext != 0 && pollRet > m_pollNext) {
            result.m_delayedUsecs = static_cast<float>(pollRet - m_pollNext);
        }
        m_pollLast = pollRet;
        m_pollNext = pollRet + m_config.m_periodUsecs;

        if (needPlayback) {
            std::uint16_t revents = 0;
            if (m_backend.pollRevents(Stream::kPlayback, m_pollFds.data(), static_cast<nfds_t>(playbackNfds),
                    &revents)
                < 0) {
                result.m_status = -6;
                return result;
            }
            if (revents & POLLNVAL) {
                result.m_status = -7;
                return result;
            }
            if (revents & POLLERR) {
                xrunDetected = true;
            }
            if (revents & POLLOUT) {
                needPlayback = false;
            }
        }

        if (needCapture) {
            std::uint16_t revents = 0;
            if (m_backend.pollRevents(Stream::kCapture, m_pollFds.data() + captureIndex,
                    static_cast<nfds_t>(nfds - captureIndex), &revents)
                < 0) {
                result.m_status = -6;
                return result;
            }
            if (revents & POLLNVAL) {
                result.m_status = -7;
                return result;
            }
            if (revents & POLLERR) {
                xrunDetected = true;
            }
            if (revents & POLLIN) {
                needCapture = false;
            }
        }
    }

    constexpr std::ptrdiff_t kNoStream = std::numeric_limits<std::ptrdiff_t>::max();
    const std::ptrdiff_t captureAvail = m_backend.hasCapture() ? m_backend.avail(Stream::kCapture) : kNoStream;
    const std::ptrdiff_t playbackAvail = m_backend.hasPlayback() ? m_backend.avail(Stream::kPlayback) : kNoStream;

    if (captureAvail == -EPIPE || playbackAvail == -EPIPE) {
        xrunDetected = true;
    } else if (captureAvail < 0 || playbackAvail < 0) {
        result.m_status = -8;
        return result;
    }

    if (xrunDetected) {
        result.m_status = xrunRecovery(&result.m_delayedUsecs);
        return result;
    }

    result.m_status = 0;
    m_lastWaitUsecs = pollRet;

    const std::ptrdiff_t avail = std::min(captureAvail, playbackAvail);
    resetChannelsNotDone();
    result.m_frames = avail - (avail % static_cast<std::ptrdiff_t>(m_config.m_framesPerCycle));
    return result;
}

template <PcmBackend Backend>
std::int32_t Cycle<Backend>::xrunRecovery(float* delayedUsecs)
{
    XrunStatus status;
    // A failed status call is not fatal: the restart below still has a chance to recover the device.
    m_backend.status(status);

    if (status.m_suspended) {
        if (m_backend.hasCapture() && m_backend.prepare(Stream::kCapture) < 0) {
            return -1;
        }
        if (m_backend.hasPlayback() && m_backend.prepare(Stream::kPlayback) < 0) {
            return -1;
        }
    }

    if (status.m_xrun && m_processCount > kXrunReportDelay) {
        ++m_xrunCount;
        *delayedUsecs = status.m_delayedUsecs;
        if (m_backend.hasCapture() && m_backend.prepare(Stream::kCapture) < 0) {
            return -1;
        }
        if (m_backend.hasPlayback() && m_backend.prepare(Stream::kPlayback) < 0) {
            return -1;
        }
    }

    if (m_callbacks.m_restart == nullptr) {
        return -1;
    }
    return m_callbacks.m_restart(m_callbacks.m_arg);
}

template <PcmBackend Backend>
std::int32_t Cycle<Backend>::read(std::uint32_t frames)
{
    if (frames > m_config.m_framesPerCycle) {
        return -1;
    }
    if (!m_backend.hasCapture()) {
        return 0;
    }

    std::uint32_t remaining = frames;
    std::ptrdiff_t nread = 0;

    while (remaining != 0) {
        m_captureMap.m_requested = remaining;
        if (m_backend.begin(Stream::kCapture, m_captureMap) < 0) {
            return -1;
        }

        const std::size_t contiguous = std::min<std::size_t>(remaining, m_captureMap.m_frames);
        if (contiguous == 0) {
            return -1;
        }

        if (m_callbacks.m_readInput) {
            m_callbacks.m_readInput(m_callbacks.m_arg, *this, m_captureMap, frames, nread);
        }

        if (m_backend.commit(Stream::kCapture, m_captureMap.m_offset, contiguous) < 0) {
            return -1;
        }

        remaining -= static_cast<std::uint32_t>(contiguous);
        nread += static_cast<std::ptrdiff_t>(contiguous);
    }

    return 0;
}

template <PcmBackend Backend>
std::int32_t Cycle<Backend>::write(std::uint32_t frames)
{
    ++m_processCount;

    if (!m_backend.hasPlayback()) {
        return 0;
    }
    if (frames > m_config.m_framesPerCycle) {
        return -1;
    }

    std::uint32_t remaining = frames;
    std::ptrdiff_t nwritten = 0;

    while (remaining != 0) {
        m_playbackMap.m_requested = remaining;
        if (m_backend.begin(Stream::kPlayback, m_playbackMap) < 0) {
            return -1;
        }

        const std::size_t contiguous = std::min<std::size_t>(remaining, m_playbackMap.m_frames);
        if (contiguous == 0) {
            return -1;
        }

        if (m_callbacks.m_writeOutput) {
            m_callbacks.m_writeOutput(m_callbacks.m_arg, *this, m_playbackMap, frames, nwritten);
        }

        silenceUntouched(contiguous);

        const std::int32_t err = m_backend.commit(Stream::kPlayback, m_playbackMap.m_offset, contiguous);
        if (err < 0 && err != -EPIPE && err != -ESTRPIPE) {
            return -1;
        }

        remaining -= static_cast<std::uint32_t>(contiguous);
        nwritten += static_cast<std::ptrdiff_t>(contiguous);
    }

    return 0;
}

template <PcmBackend Backend>
void Cycle<Backend>::markChannelDone(std::size_t channel)
{
    m_channelsNotDone[channel / 64] &= ~(std::uint64_t { 1 } << (channel % 64));
    m_silent[channel] = 0;
}

template <PcmBackend Backend>
void Cycle<Backend>::silenceUntouched(std::size_t frames)
{
    for (std::size_t channel = 0; channel < m_playbackAreas.size(); ++channel) {
        if (channelNotDone(channel) && m_silent[channel] < m_config.m_bufferFrames) {
            m_backend.silence(Stream::kPlayback, channel, frames);
            m_silent[channel] += frames;
        }
    }
}

template <PcmBackend Backend>
void Cycle<Backend>::resetChannelsNotDone()
{
    std::fill(m_channelsNotDone.begin(), m_channelsNotDone.end(), ~std::uint64_t { 0 });
    if (!m_channelsNotDone.empty()) {
        m_channelsNotDone.back() = m_channelMaskLast;
    }
}

template <PcmBackend Backend>
bool Cycle<Backend>::channelNotDone(std::size_t channel) const
{
    return (m_channelsNotDone[channel / 64] & (std::uint64_t { 1 } << (channel % 64))) != 0;
}

} // namespace jackmini::alsa

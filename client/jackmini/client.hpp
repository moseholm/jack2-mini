// Copyright (C) 2026 JACK contributors
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation; either version 2.1 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.

#pragma once

#include "error.hpp"
#include "midi.hpp"
#include "options.hpp"
#include "port.hpp"
#include "transport.hpp"
#include "uuid.hpp"

#include <concepts>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace jackmini {

// The cycle a process callback runs in.  The server computes it at the start of the cycle, so every client of a cycle
// sees the same values.  The times are in the server's clock.  In real time the next cycle is one period after the
// current one.  In freewheel mode the cycles are not paced by the clock: the times follow the cycles as the server runs
// them, and the period is measured, not nominal.  A client correcting for clock drift must skip that when freewheel is
// set.
struct Cycle {
    FrameCount m_frames = 0; // the buffer size of this cycle
    FrameCount m_currentFrames = 0; // the frame at the start of this cycle
    std::uint64_t m_currentUsecs = 0; // the time at the start of this cycle
    std::uint64_t m_nextUsecs = 0; // the expected start of the next cycle
    float m_periodUsecs = 0.0f; // the nominal period
    bool m_freewheel = false; // the server runs its cycles without the sound card
};

// The lifecycle is three states.  A client is opened, then it may be activated
// and deactivated any number of times.
//
//   opened    open() succeeded.  Ports and callbacks may be set up.
//   active    activate() succeeded.  The process callback runs each cycle.
//             Callbacks cannot be set.
//   inactive  deactivate() returned.  Callbacks may be set.  activate() may be
//             called again.
//
// The destructor deactivates an active client and then closes it.  close()
// reports errors.  Port handles stay valid across deactivate.  Connections
// are kept across deactivate.
class JACKMINI_API Client {
public:
    // Process callback: called once per cycle on the realtime thread.  Returns
    // false to report a failure, which the server treats as a shutdown request.
    using ProcessFn = std::move_only_function<bool(const Cycle& cycle)>;

    using XrunFn = std::move_only_function<void()>;
    using ShutdownFn = std::move_only_function<void()>;
    using BufferSizeFn = std::move_only_function<bool(FrameCount frames)>;
    using SampleRateFn = std::move_only_function<bool(FrameCount rate)>;
    using LatencyFn = std::move_only_function<void(LatencyMode mode)>;

    static Result<Client> open(std::string_view name, OpenOptions options = { });

    Client(Client&& other) noexcept;
    Client& operator=(Client&& other) noexcept;
    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;
    ~Client();

    // Lifecycle.
    Result<void> activate(); // opened or inactive -> active
    Result<void> deactivate(); // active -> inactive; joins the realtime thread
    Result<void> close(); // any state -> closed
    bool isActive() const noexcept;

    // Callbacks.  Each is stored while the client is inactive.  Setting one
    // while active returns Error::InvalidState.  The stored callable is used
    // from the next activate().
    template <std::invocable<const Cycle&> F>
        requires std::convertible_to<std::invoke_result_t<F&, const Cycle&>, bool>
    Result<void> onProcess(F&& fn)
    {
        return setProcess(ProcessFn(std::forward<F>(fn)));
    }
    Result<void> onXrun(XrunFn fn);
    Result<void> onShutdown(ShutdownFn fn);
    Result<void> onBufferSize(BufferSizeFn fn);
    Result<void> onSampleRate(SampleRateFn fn);
    Result<void> onLatency(LatencyFn fn); // the server asks for the latency of this client's ports

    // Latency.  The server computes the total latency of each path; a client sets the range of its ports.
    LatencyRange portLatency(Port port, LatencyMode mode) const;
    Result<void> setPortLatency(Port port, LatencyMode mode, LatencyRange range);
    Result<void> recomputeLatencies(); // asks the server to recompute the total latencies

    // Identity and timing, valid at any time.
    std::string_view name() const noexcept;
    Uuid uuid() const noexcept;
    FrameCount sampleRate() const noexcept;
    FrameCount bufferSize() const noexcept;
    float cpuLoad() const noexcept;

    // Ports.  Registering and unregistering are allowed in any state.
    Result<Port> registerPort(std::string_view name, PortType type, PortFlags flags);
    Result<void> unregisterPort(Port port);
    std::string_view portName(Port port) const;
    PortType portType(Port port) const;
    PortFlags portFlags(Port port) const;
    Result<Port> findPort(std::string_view fullName) const;
    std::vector<std::string> portNames(PortType type, PortFlags required) const;

    // Connections.  Kept across deactivate.
    Result<void> connect(Port source, Port destination);
    Result<void> disconnect(Port source, Port destination);
    bool isConnected(Port source, Port destination) const;
    std::vector<std::string> connections(Port port) const;
    // Every port of the graph, in port-number order, from one read of the graph.
    std::vector<PortInfo> ports() const;
    // The total latency of a port: the latency of the longest path through it, as the server last computed it.
    std::uint32_t totalLatency(Port port) const;

    // Realtime access: valid only inside the process callback, for the cycle
    // being processed.  These do not allocate and do not return errors.
    std::span<float> audioBuffer(Port port) noexcept;
    midi::Buffer midiBuffer(Port port) noexcept;

    // Transport.
    TransportState transportState() const noexcept;
    Result<void> transportStart();
    Result<void> transportStop();
    Result<void> transportLocate(FrameCount frame);

private:
    Client() = default;
    Result<void> setProcess(ProcessFn fn);

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace jackmini

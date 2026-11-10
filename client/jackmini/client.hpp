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
    FrameCount frames = 0; // the buffer size of this cycle
    FrameCount current_frames = 0; // the frame at the start of this cycle
    std::uint64_t current_usecs = 0; // the time at the start of this cycle
    std::uint64_t next_usecs = 0; // the expected start of the next cycle
    float period_usecs = 0.0f; // the nominal period
    bool freewheel = false; // the server runs its cycles without the sound card
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
    bool is_active() const noexcept;

    // Callbacks.  Each is stored while the client is inactive.  Setting one
    // while active returns Error::InvalidState.  The stored callable is used
    // from the next activate().
    template <std::invocable<const Cycle&> F>
        requires std::convertible_to<std::invoke_result_t<F&, const Cycle&>, bool>
    Result<void> on_process(F&& fn)
    {
        return set_process(ProcessFn(std::forward<F>(fn)));
    }
    Result<void> on_xrun(XrunFn fn);
    Result<void> on_shutdown(ShutdownFn fn);
    Result<void> on_buffer_size(BufferSizeFn fn);
    Result<void> on_sample_rate(SampleRateFn fn);
    Result<void> on_latency(LatencyFn fn); // the server asks for the latency of this client's ports

    // Latency.  The server computes the total latency of each path; a client sets the range of its ports.
    LatencyRange port_latency(Port port, LatencyMode mode) const;
    Result<void> set_port_latency(Port port, LatencyMode mode, LatencyRange range);
    Result<void> recompute_latencies(); // asks the server to recompute the total latencies

    // Identity and timing, valid at any time.
    std::string_view name() const noexcept;
    Uuid uuid() const noexcept;
    FrameCount sample_rate() const noexcept;
    FrameCount buffer_size() const noexcept;
    float cpu_load() const noexcept;

    // Ports.  Registering and unregistering are allowed in any state.
    Result<Port> register_port(std::string_view name, PortType type, PortFlags flags);
    Result<void> unregister_port(Port port);
    std::string_view port_name(Port port) const;
    PortType port_type(Port port) const;
    PortFlags port_flags(Port port) const;
    Result<Port> find_port(std::string_view full_name) const;
    std::vector<std::string> port_names(PortType type, PortFlags required) const;

    // Connections.  Kept across deactivate.
    Result<void> connect(Port source, Port destination);
    Result<void> disconnect(Port source, Port destination);
    bool is_connected(Port source, Port destination) const;
    std::vector<std::string> connections(Port port) const;
    // Every port of the graph, in port-number order, from one read of the graph.
    std::vector<PortInfo> ports() const;
    // The total latency of a port: the latency of the longest path through it, as the server last computed it.
    std::uint32_t total_latency(Port port) const;

    // Realtime access: valid only inside the process callback, for the cycle
    // being processed.  These do not allocate and do not return errors.
    std::span<float> audio_buffer(Port port) noexcept;
    midi::Buffer midi_buffer(Port port) noexcept;

    // Transport.
    TransportState transport_state() const noexcept;
    Result<void> transport_start();
    Result<void> transport_stop();
    Result<void> transport_locate(FrameCount frame);

private:
    Client() = default;
    Result<void> set_process(ProcessFn fn);

    struct Impl;
    std::unique_ptr<Impl> fImpl;
};

} // namespace jackmini

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

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

#include <pthread.h>

#include "ClientControl.h"
#include "Notification.h"
#include "ProcessContext.hpp"
#include "Request.h"
#include "ShmMem.h"
#include "Socket.h"
#include "Types.h"
#include "jackmini/Client.hpp"
#include "jackmini/Error.hpp"
#include "jackmini/Options.hpp"
#include "jackmini/Port.hpp"
#include "jackmini/Uuid.hpp"

namespace jackmini::client {

// One client's connection to the server: the request socket, the notification socket and its thread, and the shared
// memory of the client. The public Client wraps one session; the session knows nothing of the callbacks.
class JACKMINI_API ClientSession {
public:
    // Connects to the server (starting it when allowed), registers the client under name, and maps its shared
    // memory. Fails with NameTaken, Protocol, NoServer, or Io.
    static Result<std::unique_ptr<ClientSession>> open(std::string_view name, const OpenOptions& options);

    ClientSession(const ClientSession&) = delete;
    ClientSession& operator=(const ClientSession&) = delete;
    ~ClientSession();

    // The process callback. It is set while the client is inactive, and used by the next activate().
    void setProcess(std::move_only_function<bool(const Cycle&)> fn) { m_process = std::move(fn); }

    // Activates the client: with a process callback, the realtime thread runs each cycle. A client without one
    // takes part in the graph only through its connections.
    Result<void> activate();

    // Stops the realtime thread, after the server has taken the client out of the graph.
    Result<void> deactivate();

    bool isActive() const noexcept;

    // Ports, by the graph's port numbers. The public Client keeps the handles that it gives out.
    Result<PortIdC> registerPort(std::string_view shortName, const char* type, std::uint64_t flags, std::uint64_t bufferSize);
    Result<void> unregisterPort(PortIdC port);
    Result<void> connectPorts(std::string_view source, std::string_view destination);
    Result<void> disconnectPorts(std::string_view source, std::string_view destination);
    bool owns(PortIdC port) const;
    std::string_view portName(PortIdC port) const;
    const char* portType(PortIdC port) const;
    std::uint64_t portFlags(PortIdC port) const;
    std::optional<PortIdC> findPort(std::string_view fullName) const;
    std::vector<std::string> portNames(const char* type, std::uint64_t flags) const;
    std::vector<std::string> connections(PortIdC port) const;

    // Everything about each port in use, in port-number order.  The owner is the UUID of the client that registered
    // the port.  The read is not locked: a port registered or released while the list is built may be missing or
    // stale, as with the other reads of the graph.
    struct PortRecord {
        std::string m_name;
        std::string m_type;
        std::uint64_t m_flags = 0;
        LatencyRangeC m_playbackLatency { };
        LatencyRangeC m_captureLatency { };
        NframesC m_totalLatency = 0;
        std::vector<std::string> m_aliases;
        std::uint64_t m_portUuid = 0;
        std::uint64_t m_ownerUuid = 0;
        std::vector<std::string> m_connections;
    };
    std::vector<PortRecord> portRecords() const;
    NframesC portTotalLatency(PortIdC port) const;
    bool isConnected(PortIdC source, PortIdC destination) const;

    // Callbacks. They are set while the client is inactive. The notification callbacks run on the notification
    // thread, the buffer size callback also at the start of the realtime thread; they are called only while the
    // client is active, as before. The shutdown callback runs once.
    void setXrunHandler(Client::XrunFn fn) { m_xrun = std::move(fn); }
    void setShutdownHandler(Client::ShutdownFn fn) { m_shutdown = std::move(fn); }
    void setBufferSizeHandler(Client::BufferSizeFn fn) { m_bufferSize = std::move(fn); }
    void setSampleRateHandler(Client::SampleRateFn fn) { m_sampleRate = std::move(fn); }

    // Latency. The latency handler runs on the notification thread when the server asks for the latency of this
    // client's ports. Without a handler, the ports take the widest range of the ports that are measured.
    void setLatencyHandler(std::move_only_function<void(LatencyMode)> fn) { m_latency = std::move(fn); }
    LatencyRangeC portLatencyRange(PortIdC port, LatencyCallbackModeC mode) const;
    void setPortLatencyRange(PortIdC port, LatencyCallbackModeC mode, LatencyRangeC range);
    Result<void> recomputeLatencies();

    // The buffer of a port for the cycle being processed. It is valid inside the process callback. The realtime
    // path does not lock or allocate.
    void* portBuffer(PortIdC port, NframesC frames) noexcept;

    // Transport. These write the engine's transport state directly, so they are safe on the realtime path.
    void transportStart() noexcept;
    void transportStop() noexcept;
    void transportLocate(NframesC frame) noexcept;
    TransportStateC transportQuery(PositionC* position) const noexcept;

    // Identity and timing.
    Result<Uuid> lookupUuid();
    FrameCount sampleRate() const noexcept;
    FrameCount bufferSize() const noexcept;
    float cpuLoad() const noexcept;

    // Unregisters the client and stops the notifications. Calling it again does nothing. An active client is
    // deactivated first.
    Result<void> close();

    std::int32_t ref() const noexcept { return m_ref; }
    std::string_view name() const noexcept { return m_name; }
    bool serverAlive() const noexcept { return !m_serverDown.load(); }

private:
    ClientSession();

    Result<void> connectAndRegister(std::string_view name, const OpenOptions& options);
    bool startNotifications();
    void notificationLoop();
    std::int32_t handleNotification(const ClientNotification& event);
    std::int32_t handleLatency(std::int32_t status);
    void deliverShutdown();
    std::int32_t transact(Request& request, Reply& reply);
    void shutDownSockets();

    static void* realtimeEntry(void* arg);
    void waitGraphChange() const;
    bool startRealtime();
    void stopRealtime();
    void realtimeRun();
    bool waitCycle();
    void signalCycle();
    Cycle buildCycle() const;
    void endCyclesOnFailure();

    ProcessContext* m_context;
    ClientSocket m_request;
    std::mutex m_requestMutex;

    ServerSocket m_notifyListen;
    std::unique_ptr<ClientSocket> m_notify;
    std::atomic<std::int32_t> m_notifyFd { -1 }; // a copy of notify_'s descriptor, so close can shut it down
    std::jthread m_notifier;
    std::mutex m_readyMutex;
    std::condition_variable m_ready;
    bool m_readyFlag = false;

    ShmReadWritePtr<ClientControl> m_client;
    std::string m_serverName;
    std::string m_name;
    std::int32_t m_ref = -1;
    std::atomic<bool> m_closing { false };
    std::atomic<bool> m_serverDown { false };

    std::move_only_function<bool(const Cycle&)> m_process;
    std::move_only_function<void(LatencyMode)> m_latency;
    Client::XrunFn m_xrun;
    Client::ShutdownFn m_shutdown;
    Client::BufferSizeFn m_bufferSize;
    Client::SampleRateFn m_sampleRate;
    pthread_t m_realtimeThread { };
    std::mutex m_realtimeMutex; // guards the thread: start, stop, and the freewheel scheduling changes
    std::atomic<bool> m_realtimeRunning { false };
    std::atomic<bool> m_realtimeStop { false };
};

} // namespace jackmini::client

/*
Copyright (C) 2026 JACK contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU Lesser General Public License as published by
the Free Software Foundation; either version 2.1 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
*/


#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

#include <pthread.h>

#include "JackClientControl.h"
#include "JackNotification.h"
#include "JackRequest.h"
#include "JackShmMem.h"
#include "JackSocket.h"
#include "JackTypes.h"
#include "jackmini/client.hpp"
#include "jackmini/error.hpp"
#include "jackmini/options.hpp"
#include "jackmini/port.hpp"
#include "jackmini/uuid.hpp"
#include "process_context.hpp"

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
    void set_process(std::move_only_function<bool(const Cycle&)> fn) { process_ = std::move(fn); }

    // Activates the client: with a process callback, the realtime thread runs each cycle. A client without one
    // takes part in the graph only through its connections.
    Result<void> activate();

    // Stops the realtime thread, after the server has taken the client out of the graph.
    Result<void> deactivate();

    bool is_active() const noexcept;

    // Ports, by the graph's port numbers. The public Client keeps the handles that it gives out.
    Result<jack_port_id_t> register_port(std::string_view short_name, const char* type, unsigned long flags, unsigned long buffer_size);
    Result<void> unregister_port(jack_port_id_t port);
    Result<void> connect_ports(std::string_view source, std::string_view destination);
    Result<void> disconnect_ports(std::string_view source, std::string_view destination);
    bool owns(jack_port_id_t port) const;
    std::string_view port_name(jack_port_id_t port) const;
    const char* port_type(jack_port_id_t port) const;
    unsigned long port_flags(jack_port_id_t port) const;
    std::optional<jack_port_id_t> find_port(std::string_view full_name) const;
    std::vector<std::string> port_names(const char* type, unsigned long flags) const;
    std::vector<std::string> connections(jack_port_id_t port) const;
    bool is_connected(jack_port_id_t source, jack_port_id_t destination) const;

    // Callbacks. They are set while the client is inactive. The notification callbacks run on the notification
    // thread, the buffer size callback also at the start of the realtime thread; they are called only while the
    // client is active, as before. The shutdown callback runs once.
    void set_xrun_handler(Client::XrunFn fn) { xrun_ = std::move(fn); }
    void set_shutdown_handler(Client::ShutdownFn fn) { shutdown_ = std::move(fn); }
    void set_buffer_size_handler(Client::BufferSizeFn fn) { buffer_size_ = std::move(fn); }
    void set_sample_rate_handler(Client::SampleRateFn fn) { sample_rate_ = std::move(fn); }

    // Latency. The latency handler runs on the notification thread when the server asks for the latency of this
    // client's ports. Without a handler, the ports take the widest range of the ports that are measured.
    void set_latency_handler(std::move_only_function<void(LatencyMode)> fn) { latency_ = std::move(fn); }
    jack_latency_range_t port_latency_range(jack_port_id_t port, jack_latency_callback_mode_t mode) const;
    void set_port_latency_range(jack_port_id_t port, jack_latency_callback_mode_t mode, jack_latency_range_t range);
    Result<void> recompute_latencies();

    // The buffer of a port for the cycle being processed. It is valid inside the process callback. The realtime
    // path does not lock or allocate.
    void* port_buffer(jack_port_id_t port, jack_nframes_t frames) noexcept;

    // Transport. These write the engine's transport state directly, so they are safe on the realtime path.
    void transport_start() noexcept;
    void transport_stop() noexcept;
    void transport_locate(jack_nframes_t frame) noexcept;
    jack_transport_state_t transport_query(jack_position_t* position) const noexcept;

    // Identity and timing.
    Result<Uuid> lookup_uuid();
    FrameCount sample_rate() const noexcept;
    FrameCount buffer_size() const noexcept;
    float cpu_load() const noexcept;

    // Unregisters the client and stops the notifications. Calling it again does nothing. An active client is
    // deactivated first.
    Result<void> close();

    int ref() const noexcept { return ref_; }
    std::string_view name() const noexcept { return name_; }
    bool server_alive() const noexcept { return !server_down_.load(); }

private:
    ClientSession();

    Result<void> connect_and_register(std::string_view name, const OpenOptions& options);
    bool start_notifications();
    void notification_loop();
    int handle_notification(const JackClientNotification& event);
    int handle_latency(int status);
    void deliver_shutdown();
    int transact(JackRequest& request, JackResult& reply);
    void shut_down_sockets();

    static void* realtime_entry(void* arg);
    void wait_graph_change() const;
    bool start_realtime();
    void stop_realtime();
    void realtime_run();
    bool wait_cycle();
    void signal_cycle();
    Cycle build_cycle() const;
    void end_cycles_on_failure();

    ProcessContext* context_;
    JackClientSocket request_;
    std::mutex request_mutex_;

    JackServerSocket notify_listen_;
    std::unique_ptr<JackClientSocket> notify_;
    std::atomic<int> notify_fd_ { -1 }; // a copy of notify_'s descriptor, so close can shut it down
    std::jthread notifier_;
    std::mutex ready_mutex_;
    std::condition_variable ready_;
    bool ready_flag_ = false;

    JackShmReadWritePtr<JackClientControl> client_;
    std::string server_name_;
    std::string name_;
    int ref_ = -1;
    std::atomic<bool> closing_ { false };
    std::atomic<bool> server_down_ { false };

    std::move_only_function<bool(const Cycle&)> process_;
    std::move_only_function<void(LatencyMode)> latency_;
    Client::XrunFn xrun_;
    Client::ShutdownFn shutdown_;
    Client::BufferSizeFn buffer_size_;
    Client::SampleRateFn sample_rate_;
    pthread_t realtime_thread_ { };
    std::mutex realtime_mutex_; // guards the thread: start, stop, and the freewheel scheduling changes
    std::atomic<bool> realtime_running_ { false };
    std::atomic<bool> realtime_stop_ { false };
};

} // namespace jackmini::client

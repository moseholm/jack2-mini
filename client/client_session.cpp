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


#include "client_session.hpp"

#include "JackClientControl.h"
#include "JackConstants.h"
#include "JackError.h"
#include "JackGlobals.h"
#include "JackMessage.h"
#include "JackPlatformPlug.h"
#include "JackRequest.h"
#include "JackServerLaunch.h"
#include "JackTime.h"
#include "JackTools.h"

#include "JackFrameTimer.h"

#include <array>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <sys/socket.h>

#include "JackFdSegment.h"

namespace jackmini::client
{

namespace
{

// Set on the notification thread, so a call to the server from there is refused instead of deadlocking.
thread_local bool on_notification_thread = false;

// Set on the realtime thread of a client.
thread_local bool on_realtime_thread = false;

#ifndef SCHED_RESET_ON_FORK
#define SCHED_RESET_ON_FORK 0x40000000
#endif

// The realtime class (SCHED_FIFO) for a thread: the same request as the thread of the server, with the reset on fork
// when the system knows it.
int acquire_realtime(pthread_t thread, int priority)
{
    sched_param param{};
    param.sched_priority = priority;
    const int res = pthread_setschedparam(thread, SCHED_FIFO, &param);
    if (res == 0) {
        return 0;
    }
    if (pthread_setschedparam(thread, SCHED_FIFO | SCHED_RESET_ON_FORK, &param) == 0) {
        return 0;
    }
    jack_error("Cannot use real-time scheduling (RR/%d) (%d: %s)", priority, res, strerror(res));
    return -1;
}

void drop_realtime(pthread_t thread)
{
    sched_param param{};
    if (pthread_setschedparam(thread, SCHED_OTHER, &param) != 0) {
        jack_error("Cannot switch to normal scheduling priority(%s)", strerror(errno));
    }
}

std::string default_server_name()
{
    const char* name = getenv("JACK_DEFAULT_SERVER");
    return name ? name : JACK_DEFAULT_SERVER_NAME;
}

// The bits of the options that the server reads when it checks a new client.
int option_bits(const OpenOptions& options)
{
    int bits = 0;
    if (!options.start_server) {
        bits |= JackNoStartServer;
    }
    if (options.exact_name) {
        bits |= JackUseExactName;
    }
    if (!options.server_name.empty()) {
        bits |= JackServerName;
    }
    return bits;
}

void shut_down_fd(int fd)
{
    if (fd >= 0) {
        ::shutdown(fd, SHUT_RDWR);
    }
}

void setup_driver_sync(ProcessContext& context, bool freewheel)
{
    // In "async" mode the server does not wait for the drivers, so their synchro is set to flush: it keeps no
    // activations that nobody consumes. See JackClient::SetupDriverSync.
    const bool flush = !freewheel && !context.engine->fSyncMode;
    for (int i = 0; i < context.engine->fDriverNum; i++) {
        context.synchro[i].SetFlush(flush);
    }
}

} // end of anonymous namespace

ClientSession::ClientSession()
    : context_(&ProcessContext::acquire())
{}

ClientSession::~ClientSession()
{
    (void)close();
    ProcessContext::release();
}

Result<std::unique_ptr<ClientSession>> ClientSession::open(std::string_view name, const OpenOptions& options)
{
    if (name.empty() || name.size() >= JACK_CLIENT_NAME_SIZE) {
        return std::unexpected(Error::InvalidArgument);
    }

    std::unique_ptr<ClientSession> session(new ClientSession());
    if (auto opened = session->connect_and_register(name, options); !opened) {
        return std::unexpected(opened.error());
    }
    return session;
}

Result<void> ClientSession::connect_and_register(std::string_view name_view, const OpenOptions& options)
{
    const std::string name(name_view);
    server_name_ = options.server_name.empty() ? default_server_name() : options.server_name;
    const int bits = option_bits(options);

    jack_status_t status = static_cast<jack_status_t>(0);
    if (try_start_server(server_name_.c_str(), static_cast<jack_options_t>(bits), &status) < 0) {
        jack_error("jack server is not running or cannot be started");
        return std::unexpected(Error::NoServer);
    }

    if (request_.Connect(jack_server_dir, server_name_.c_str(), 0) < 0) {
        jack_error("Cannot connect to server socket");
        return std::unexpected(Error::Io);
    }

    JackClientCheckRequest check(name.c_str(), JACK_PROTOCOL_VERSION, bits, JACK_UUID_EMPTY_INITIALIZER, true);
    JackClientCheckResult check_reply;
    if (transact(check, check_reply) < 0) {
        return std::unexpected(Error::Io);
    }
    if (check_reply.fResult < 0) {
        if (check_reply.fStatus & JackVersionError) {
            jack_error("JACK protocol mismatch %d", JACK_PROTOCOL_VERSION);
            return std::unexpected(Error::Protocol);
        }
        jack_error("Client name = %s conflits with another running client", name.c_str());
        return std::unexpected(Error::NameTaken);
    }
    const std::string name_res(check_reply.fName);

    if (notify_listen_.Bind(jack_client_dir, name_res.c_str(), 0) < 0) {
        jack_error("Cannot bind socket");
        return std::unexpected(Error::Io);
    }
    if (!start_notifications()) {
        jack_error("Cannot start channel");
        return std::unexpected(Error::Io);
    }

    JackClientOpenRequest open_request(name_res.c_str(), JackTools::GetPID(), JACK_UUID_EMPTY_INITIALIZER);
    JackClientOpenResult open_reply;
    if (transact(open_request, open_reply) < 0) {
        return std::unexpected(Error::Io);
    }
    // The descriptors belong to this function: the mappings keep the segments alive after they are closed.
    std::array<UniqueFd, 3> descriptors;
    descriptors[0].reset(open_reply.fSharedEngine.fFd);
    descriptors[1].reset(open_reply.fSharedGraph.fFd);
    descriptors[2].reset(open_reply.fSharedClient.fFd);
    if (open_reply.fResult < 0) {
        jack_error("Cannot open %s client", name_res.c_str());
        return std::unexpected(Error::ServerFailure);
    }

    ProcessContext& context = *context_;
    try {
        context.engine.Map(open_reply.fSharedEngine);
        context.graph.Map(open_reply.fSharedGraph);
        client_.Map(open_reply.fSharedClient);
    } catch (...) {
        jack_error("Map shared memory segments exception");
        return std::unexpected(Error::Io);
    }
    JackGlobals::fVerbose = context.engine->fVerbose;

    ref_ = client_->fRefNum;
    name_ = name_res;
    setup_driver_sync(context, context.engine->fFreewheel);

    {
        std::lock_guard lock(context.synchro_mutex);
        if (!context.synchro[ref_].Connect(name_.c_str(), server_name_.c_str())) {
            jack_error("Cannot ConnectSemaphore %s client", name_.c_str());
            return std::unexpected(Error::Io);
        }
    }
    set_clock_source(context.engine->fClockSource);
    return {};
}

bool ClientSession::start_notifications()
{
    notifier_ = std::jthread([this](std::stop_token) { notification_loop(); });
    std::unique_lock lock(ready_mutex_);
    ready_.wait(lock, [this] { return ready_flag_; });
    return true;
}

void ClientSession::notification_loop()
{
    on_notification_thread = true;
    {
        std::lock_guard lock(ready_mutex_);
        ready_flag_ = true;
    }
    ready_.notify_all();

    // The listening socket is closed by close(), after this thread has been joined.
    notify_.reset(notify_listen_.Accept());
    if (!notify_) {
        jack_error("JackSocketClientChannel: cannot establish notification socket");
        server_down_ = true;
        return;
    }
    notify_fd_ = notify_->GetFd();

    while (true) {
        JackMsgHeader header{};
        if (int err = ReadJackMsgHeader(notify_.get(), header); err < 0) {
            if (err != JACK_REQUEST_ERR_ABORTED && !closing_) {
                jack_error("JackSocketClientChannel read fail");
            }
            break;
        }
        if (header.version != kJackMsgVersion || header.length != JackClientNotification::PayloadSize) {
            jack_error("JackSocketClientChannel: notification of protocol version %d, length %d", header.version, header.length);
            break;
        }

        JackClientNotification event;
        {
            JackPayloadReader payload(notify_.get(), header.length, {});
            if (event.Read(&payload) < 0 || !payload.Done()) {
                jack_error("JackSocketClientChannel: notification does not match its length");
                break;
            }
        }
        if (event.fNotify == kDropped) {
            jack_error("JACK server dropped %d notifications for this client (it did not read them in time)", event.fValue1);
        }

        JackResult reply;
        reply.fResult = handle_notification(event);
        if (event.fSync && SendJackReply(notify_.get(), reply, header) < 0) {
            jack_error("JackSocketClientChannel write fail");
            break;
        }
    }
    // Ending the loop by close() is not a server failure.
    if (!closing_) {
        server_down_ = true;
    }
}

int ClientSession::handle_notification(const JackClientNotification& event)
{
    ProcessContext& context = *context_;
    int res = 0;
    switch (event.fNotify) {
        case kAddClient: {
            std::lock_guard lock(context.synchro_mutex);
            res = context.synchro[event.fRefNum].Connect(event.fName, server_name_.c_str()) ? 0 : -1;
            break;
        }
        case kRemoveClient:
            if (name_ != event.fName) {
                std::lock_guard lock(context.synchro_mutex);
                res = context.synchro[event.fRefNum].Disconnect() ? 0 : -1;
            }
            break;
        case kShutDownCallback:
            server_down_ = true;
            break;
        case kStartFreewheelCallback: {
            setup_driver_sync(context, true);
            // Freewheel runs the cycles without the sound card, so the realtime class is given up while it lasts.
            std::lock_guard lock(realtime_mutex_);
            if (realtime_running_ && is_active()) {
                drop_realtime(realtime_thread_);
            }
            break;
        }
        case kStopFreewheelCallback: {
            setup_driver_sync(context, false);
            std::lock_guard lock(realtime_mutex_);
            if (context.engine->fRealTime && realtime_running_ && is_active()) {
                if (acquire_realtime(realtime_thread_, context.engine->fClientPriority) < 0) {
                    jack_error("JackClient::AcquireRealTime error");
                }
            }
            break;
        }
    }
    return res;
}

int ClientSession::transact(JackRequest& request, JackResult& reply)
{
    if (on_notification_thread) {
        jack_error("Cannot callback the server in notification thread!");
        return -1;
    }
    if (server_down_) {
        jack_error("Server is not running");
        return -1;
    }

    std::lock_guard lock(request_mutex_);
    if (request.Write(&request_) < 0) {
        jack_error("Could not write request type = %ld", request.fType);
        return -1;
    }
    if (ReadJackReply(&request_, request.fType, request.fRequestId, reply) < 0) {
        jack_error("Could not read result type = %ld", request.fType);
        return -1;
    }
    return 0;
}

Result<jack_port_id_t> ClientSession::register_port(std::string_view short_name, const char* type, unsigned long flags, unsigned long buffer_size)
{
    if (short_name.empty()) {
        jack_error("port_name is empty");
        return std::unexpected(Error::InvalidArgument);
    }
    const std::string full_name = name_ + ":" + std::string(short_name);
    if (full_name.size() >= REAL_JACK_PORT_NAME_SIZE) {
        jack_error("\"%s\" is too long to be used as a JACK port name.", full_name.c_str());
        return std::unexpected(Error::InvalidArgument);
    }

    JackPortRegisterRequest request(ref_, full_name.c_str(), type, static_cast<unsigned int>(flags), static_cast<unsigned int>(buffer_size));
    JackPortRegisterResult reply;
    if (transact(request, reply) < 0 || reply.fResult < 0) {
        return std::unexpected(Error::ServerFailure);
    }
    return reply.fPortIndex;
}

Result<void> ClientSession::unregister_port(jack_port_id_t port)
{
    if (!owns(port)) {
        jack_error("unregistering a port %ld that is not own by the client", port);
        return std::unexpected(Error::NotFound);
    }
    JackPortUnRegisterRequest request(ref_, port);
    JackResult reply;
    if (transact(request, reply) < 0 || reply.fResult < 0) {
        return std::unexpected(Error::ServerFailure);
    }
    return {};
}

Result<void> ClientSession::connect_ports(std::string_view source, std::string_view destination)
{
    if (source.size() >= REAL_JACK_PORT_NAME_SIZE || destination.size() >= REAL_JACK_PORT_NAME_SIZE) {
        return std::unexpected(Error::InvalidArgument);
    }
    const std::string src(source);
    const std::string dst(destination);
    JackPortConnectNameRequest request(ref_, src.c_str(), dst.c_str());
    JackResult reply;
    if (transact(request, reply) < 0 || reply.fResult < 0) {
        return std::unexpected(Error::ServerFailure);
    }
    return {};
}

Result<void> ClientSession::disconnect_ports(std::string_view source, std::string_view destination)
{
    if (source.size() >= REAL_JACK_PORT_NAME_SIZE || destination.size() >= REAL_JACK_PORT_NAME_SIZE) {
        return std::unexpected(Error::InvalidArgument);
    }
    const std::string src(source);
    const std::string dst(destination);
    JackPortDisconnectNameRequest request(ref_, src.c_str(), dst.c_str());
    JackResult reply;
    if (transact(request, reply) < 0 || reply.fResult < 0) {
        return std::unexpected(Error::ServerFailure);
    }
    return {};
}

bool ClientSession::owns(jack_port_id_t port) const
{
    return port > 0 && port < PORT_NUM_MAX && context_->graph->GetPort(port)->GetRefNum() == ref_;
}

std::string_view ClientSession::port_name(jack_port_id_t port) const
{
    if (port == 0 || port >= PORT_NUM_MAX) {
        return {};
    }
    return context_->graph->GetPort(port)->GetName();
}

const char* ClientSession::port_type(jack_port_id_t port) const
{
    return context_->graph->GetPort(port)->GetType();
}

unsigned long ClientSession::port_flags(jack_port_id_t port) const
{
    return context_->graph->GetPort(port)->GetFlags();
}

std::optional<jack_port_id_t> ClientSession::find_port(std::string_view full_name) const
{
    const std::string text(full_name);
    const jack_port_id_t port = context_->graph->GetPort(text.c_str());
    if (port == NO_PORT) {
        return std::nullopt;
    }
    return port;
}

namespace
{

// The graph gives a list of names that point into its own table, and a list that it allocated. The names are copied
// and the list is freed.
std::vector<std::string> take_names(const char** list)
{
    std::vector<std::string> names;
    if (list == nullptr) {
        return names;
    }
    for (const char** entry = list; *entry != nullptr; entry++) {
        names.emplace_back(*entry);
    }
    free(list);
    return names;
}

} // end of anonymous namespace

std::vector<std::string> ClientSession::port_names(const char* type, unsigned long flags) const
{
    return take_names(context_->graph->GetPorts(nullptr, type, flags));
}

std::vector<std::string> ClientSession::connections(jack_port_id_t port) const
{
    wait_graph_change();
    return take_names(context_->graph->GetConnections(port));
}

bool ClientSession::is_connected(jack_port_id_t source, jack_port_id_t destination) const
{
    wait_graph_change();
    return context_->graph->IsConnected(source, destination) != 0;
}

void ClientSession::wait_graph_change() const
{
    // A pending graph change is waited for on a client's own thread only. The realtime thread reads the current state.
    if (on_realtime_thread) {
        return;
    }
    if (context_->graph->IsPendingChange()) {
        sleep_microseconds(int(context_->engine->fPeriodUsecs * 1.1f));
    }
}

void* ClientSession::port_buffer(jack_port_id_t port, jack_nframes_t frames) noexcept
{
    return context_->graph->GetBuffer(port, frames);
}

Result<Uuid> ClientSession::lookup_uuid()
{
    JackGetUUIDRequest request(name_.c_str());
    JackUUIDResult reply;
    if (transact(request, reply) < 0 || reply.fResult < 0) {
        return std::unexpected(Error::ServerFailure);
    }
    const auto parsed = Uuid::parse(reply.fUUID);
    if (!parsed) {
        return std::unexpected(Error::Protocol);
    }
    return *parsed;
}

FrameCount ClientSession::sample_rate() const noexcept
{
    return context_->engine->fSampleRate;
}

FrameCount ClientSession::buffer_size() const noexcept
{
    return context_->engine->fBufferSize;
}

float ClientSession::cpu_load() const noexcept
{
    return context_->engine->fCPULoad;
}

bool ClientSession::is_active() const noexcept
{
    return ref_ >= 0 && client_->fActive;
}

Result<void> ClientSession::activate()
{
    if (ref_ < 0) {
        return std::unexpected(Error::InvalidState);
    }
    if (is_active()) {
        return {};
    }

    const bool real_time = static_cast<bool>(process_);
    if (real_time && !start_realtime()) {
        return std::unexpected(Error::ServerFailure);
    }

    // Insertion of the client in the graph causes a graph order notification, which the client wants. The transport
    // flags are set as they were before: the server asks the client about transport through them.
    client_->fActive = true;
    client_->fTransportSync = true;
    client_->fTransportTimebase = true;
    client_->fCallback[kRealTimeCallback] = real_time;

    JackActivateRequest request(ref_, real_time);
    JackResult reply;
    if (transact(request, reply) < 0 || reply.fResult < 0) {
        client_->fActive = false;
        client_->fTransportSync = false;
        client_->fTransportTimebase = false;
        stop_realtime();
        return std::unexpected(Error::ServerFailure);
    }
    return {};
}

Result<void> ClientSession::deactivate()
{
    if (!is_active()) {
        // A thread that ended by itself after a failed cycle still has to be joined.
        stop_realtime();
        return {};
    }

    client_->fActive = false;
    client_->fTransportSync = false;
    client_->fTransportTimebase = false;

    // The server takes the client out of the graph and waits for the cycle to finish before it answers. Only then is the
    // realtime thread stopped.
    JackDeactivateRequest request(ref_);
    JackResult reply;
    const bool ok = transact(request, reply) == 0 && reply.fResult == 0;
    stop_realtime();
    return ok ? Result<void>{} : std::unexpected(Error::ServerFailure);
}

bool ClientSession::start_realtime()
{
    std::lock_guard lock(realtime_mutex_);
    realtime_stop_ = false;
    if (pthread_create(&realtime_thread_, nullptr, &ClientSession::realtime_entry, this) != 0) {
        jack_error("Start thread error");
        return false;
    }
    realtime_running_ = true;
    return true;
}

void ClientSession::stop_realtime()
{
    std::lock_guard lock(realtime_mutex_);
    if (!realtime_running_) {
        return;
    }
    // A running thread belongs to an open client, so ref_ is valid here.
    realtime_stop_ = true;
    // The thread may be waiting for a cycle. The signal wakes it, and it checks the stop flag before it runs a cycle.
    context_->synchro[ref_].Signal();
    pthread_join(realtime_thread_, nullptr);
    realtime_running_ = false;
    // A wake-up that the thread did not take must not reach the next activation.
    context_->synchro[ref_].TimedWait(0);
}

void* ClientSession::realtime_entry(void* arg)
{
    static_cast<ClientSession*>(arg)->realtime_run();
    return nullptr;
}

void ClientSession::realtime_run()
{
    on_realtime_thread = true;
    if (context_->engine->fRealTime) {
        set_threaded_log_function();
        if (acquire_realtime(pthread_self(), context_->engine->fClientPriority) < 0) {
            jack_error("JackClient::AcquireSelfRealTime error");
        }
    }

    // The first wait is the dummy cycle that the old client ran too: the thread is in step with the graph before the
    // first process call. It signals its outputs without calling the process callback.
    if (!wait_cycle()) {
        return;
    }
    signal_cycle();

    while (wait_cycle()) {
        const Cycle cycle = build_cycle();
        if (!process_(cycle)) {
            signal_cycle();
            end_cycles_on_failure();
            return;
        }
        signal_cycle();
    }
}

bool ClientSession::wait_cycle()
{
    if (context_->graph->SuspendRefNum(client_.GetShmAddress(), context_->synchro, LONG_MAX) < 0) {
        jack_error("SuspendRefNum error");
        return false;
    }
    return !realtime_stop_;
}

void ClientSession::signal_cycle()
{
    if (context_->graph->ResumeRefNum(client_.GetShmAddress(), context_->synchro) < 0) {
        jack_error("ResumeRefNum error");
    }
}

Cycle ClientSession::build_cycle() const
{
    Cycle cycle;
    cycle.frames = context_->engine->fBufferSize;
    JackTimer timer;
    context_->engine->ReadFrameTime(&timer);
    jack_nframes_t current_frames = 0;
    jack_time_t current_usecs = 0;
    jack_time_t next_usecs = 0;
    float period_usecs = 0.0f;
    if (timer.GetCycleTimes(&current_frames, &current_usecs, &next_usecs, &period_usecs) == 0) {
        cycle.current_frames = current_frames;
        cycle.current_usecs = current_usecs;
        cycle.next_usecs = next_usecs;
        cycle.period_usecs = period_usecs;
    }
    cycle.freewheel = context_->engine->fFreewheel.load();
    return cycle;
}

void ClientSession::end_cycles_on_failure()
{
    // The process callback failed: the client leaves the graph, as the old client did. The realtime class is given up
    // first, so that the server's answer does not wait for this thread.
    drop_realtime(pthread_self());
    client_->fActive = false;
    client_->fTransportSync = false;
    client_->fTransportTimebase = false;
    JackDeactivateRequest request(ref_);
    JackResult reply;
    if (transact(request, reply) < 0 || reply.fResult < 0) {
        jack_error("Deactivation after a failed cycle did not complete");
    }
}

void ClientSession::shut_down_sockets()
{
    shut_down_fd(notify_listen_.GetFd());
    shut_down_fd(notify_fd_.load());
}

Result<void> ClientSession::close()
{
    if (closing_.exchange(true)) {
        return {};
    }

    // An active client is deactivated first, which also stops its realtime thread.
    (void)deactivate();

    // The notifications stop, so no notification arrives while the client is closed.
    shut_down_sockets();
    if (notifier_.joinable()) {
        notifier_.join();
    }
    notify_.reset();
    notify_listen_.Close();
    notify_fd_ = -1;

    int result = 0;
    if (ref_ >= 0) {
        if (server_alive()) {
            JackClientCloseRequest close_request(ref_);
            JackResult reply;
            if (transact(close_request, reply) < 0 || reply.fResult < 0) {
                result = -1;
            }
        } else {
            result = -1;
        }
        {
            std::lock_guard lock(context_->synchro_mutex);
            context_->synchro[ref_].Disconnect();
        }
        ref_ = -1;
    }
    request_.Close();

    return (result == 0) ? Result<void>{} : std::unexpected(Error::ServerFailure);
}

} // end of namespace

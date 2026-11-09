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

#include <array>
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

    // The notifications stop first, so no notification arrives while the client is closed.
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

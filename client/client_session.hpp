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


#ifndef JACKMINI_CLIENT_CLIENT_SESSION_HPP
#define JACKMINI_CLIENT_CLIENT_SESSION_HPP

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>

#include "jackmini/error.hpp"
#include "jackmini/options.hpp"
#include "jackmini/port.hpp"
#include "process_context.hpp"
#include "JackClientControl.h"
#include "JackNotification.h"
#include "JackRequest.h"
#include "JackShmMem.h"
#include "JackSocket.h"

namespace jackmini::client
{

// One client's connection to the server: the request socket, the notification socket and its thread, and the shared
// memory of the client. The public Client wraps one session; the session knows nothing of the callbacks.
class JACKMINI_API ClientSession
{
    public:

        // Connects to the server (starting it when allowed), registers the client under name, and maps its shared
        // memory. Fails with NameTaken, Protocol, NoServer, or Io.
        static Result<std::unique_ptr<ClientSession>> open(std::string_view name, const OpenOptions& options);

        ClientSession(const ClientSession&) = delete;
        ClientSession& operator=(const ClientSession&) = delete;
        ~ClientSession();

        // Unregisters the client and stops the notifications. Calling it again does nothing.
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
        int transact(JackRequest& request, JackResult& reply);
        void shut_down_sockets();

        ProcessContext* context_;
        JackClientSocket request_;
        std::mutex request_mutex_;

        JackServerSocket notify_listen_;
        std::unique_ptr<JackClientSocket> notify_;
        std::atomic<int> notify_fd_{-1};    // a copy of notify_'s descriptor, so close can shut it down
        std::jthread notifier_;
        std::mutex ready_mutex_;
        std::condition_variable ready_;
        bool ready_flag_ = false;

        JackShmReadWritePtr<JackClientControl> client_;
        std::string server_name_;
        std::string name_;
        int ref_ = -1;
        std::atomic<bool> closing_{false};
        std::atomic<bool> server_down_{false};
};

} // end of namespace

#endif

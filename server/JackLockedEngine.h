// Copyright (C) 2008 Grame
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

#include "JackEngine.h"
#include "JackException.h"
#include "JackMutex.h"
#include "JackTools.h"
#include "jackmini/error.hpp"

#include <cstdint>
#include <expected>
#include <new>
#include <type_traits>

namespace jackmini {

// \brief Locked Engine, access to methods is serialized using a mutex.
//
// The methods return Result. The engine's int codes (0 on success) are converted here.

class SERVER_EXPORT JackLockedEngine {
private:
    JackEngine fEngine;

    // An allocation failure in the engine becomes ServerFailure. Other exceptions are not handled here.
    template <class F>
    static auto Guard(F&& f) -> std::invoke_result_t<F>
    {
        using R = std::invoke_result_t<F>;
        try {
            return f();
        } catch (const std::bad_alloc&) {
            jack_error("Memory allocation error...");
            if constexpr (std::is_void_v<R>) {
                return;
            } else {
                return std::unexpected(Error::ServerFailure);
            }
        }
    }

    // Runs f with the engine locked.
    template <class F>
    auto Locked(F&& f) -> std::invoke_result_t<F>
    {
        JackLock lock(&fEngine);
        return Guard(f);
    }

    // Runs f with the engine locked, if the client is registered.
    template <class F>
    auto Client(std::int32_t refnum, F&& f) -> std::invoke_result_t<F>
    {
        JackLock lock(&fEngine);
        if (!fEngine.CheckClient(refnum)) {
            return std::unexpected(Error::NotFound);
        }
        return Guard(f);
    }

    // Closing the last client of a temporary server throws JackTemporaryException: the server quits.
    template <class F>
    Result<void> Closing(std::int32_t refnum, F&& f)
    {
        try {
            return Client(refnum, f);
        } catch (const JackTemporaryException&) {
            jack_error("JackTemporaryException : now quits...");
            JackTools::KillServer();
            return { };
        }
    }

public:
    JackLockedEngine(JackGraphManager* manager, JackSynchro* table, JackEngineControl* controler, char self_connect_mode)
        : fEngine(manager, table, controler, self_connect_mode)
    {
    }
    ~JackLockedEngine()
    {
    }

    bool Lock() { return fEngine.Lock(); }
    bool Unlock() { return fEngine.Unlock(); }
    bool Trylock() { return fEngine.Trylock(); }

    Result<void> Open()
    {
        // No lock needed
        return Guard([&] { return fEngine.Open(); });
    }
    Result<void> Close()
    {
        // No lock needed
        return Guard([&] { return fEngine.Close(); });
    }

    // Client management
    Result<void> ClientCheck(const char* name, jack_uuid_t uuid, char* name_res, std::int32_t protocol, std::int32_t options, std::int32_t* status)
    {
        return Locked([&] { return fEngine.ClientCheck(name, uuid, name_res, protocol, options, status); });
    }
    Result<void> ClientExternalOpen(const char* name, std::int32_t pid, jack_uuid_t uuid, std::int32_t* ref, JackShmSegment* shared_engine, JackShmSegment* shared_client, JackShmSegment* shared_graph_manager)
    {
        return Locked([&] { return fEngine.ClientExternalOpen(name, pid, uuid, ref, shared_engine, shared_client, shared_graph_manager); });
    }
    Result<void> RegisterDriver(const char* name, std::int32_t* ref, JackEngineControl** shared_engine, JackGraphManager** shared_manager, JackClientInterface* client, bool wait)
    {
        return Locked([&] { return fEngine.RegisterDriver(name, ref, shared_engine, shared_manager, client, wait); });
    }

    Result<void> ClientExternalClose(std::int32_t refnum)
    {
        return Closing(refnum, [&] { return fEngine.ClientExternalClose(refnum); });
    }
    Result<void> UnregisterDriver(std::int32_t refnum, bool wait)
    {
        return Closing(refnum, [&] { return fEngine.UnregisterDriver(refnum, wait); });
    }

    Result<void> ClientActivate(std::int32_t refnum, bool is_real_time, bool wait)
    {
        return Client(refnum, [&] { return fEngine.ClientActivate(refnum, is_real_time, wait); });
    }
    Result<void> ClientDeactivate(std::int32_t refnum, bool wait)
    {
        return Client(refnum, [&] { return fEngine.ClientDeactivate(refnum, wait); });
    }
    void ClientKill(std::int32_t refnum)
    {
        Locked([&] { fEngine.ClientKill(refnum); });
    }

    // Port management
    Result<void> PortRegister(std::int32_t refnum, const char* name, const char* type, std::uint32_t flags, std::uint32_t buffer_size, jack_port_id_t* port)
    {
        return Client(refnum, [&] { return fEngine.PortRegister(refnum, name, type, flags, buffer_size, port); });
    }
    Result<void> PortUnRegister(std::int32_t refnum, jack_port_id_t port)
    {
        return Client(refnum, [&] { return fEngine.PortUnRegister(refnum, port); });
    }

    Result<void> PortConnect(std::int32_t refnum, const char* src, const char* dst)
    {
        return Client(refnum, [&] { return fEngine.PortConnect(refnum, src, dst); });
    }
    Result<void> PortDisconnect(std::int32_t refnum, const char* src, const char* dst)
    {
        return Client(refnum, [&] { return fEngine.PortDisconnect(refnum, src, dst); });
    }

    Result<void> PortConnect(std::int32_t refnum, jack_port_id_t src, jack_port_id_t dst)
    {
        return Client(refnum, [&] { return fEngine.PortConnect(refnum, src, dst); });
    }
    Result<void> PortDisconnect(std::int32_t refnum, jack_port_id_t src, jack_port_id_t dst)
    {
        return Client(refnum, [&] { return fEngine.PortDisconnect(refnum, src, dst); });
    }

    Result<void> PortRename(std::int32_t refnum, jack_port_id_t port, const char* name)
    {
        return Client(refnum, [&] { return fEngine.PortRename(refnum, port, name); });
    }

    Result<void> ComputeTotalLatencies()
    {
        return Locked([&] { return fEngine.ComputeTotalLatencies(); });
    }

    // Graph
    bool Process(jack_time_t cur_cycle_begin, jack_time_t prev_cycle_end)
    {
        // RT : no lock
        return fEngine.Process(cur_cycle_begin, prev_cycle_end);
    }

    // Notifications
    void NotifyDriverXRun()
    {
        // Coming from the driver in RT : no lock
        fEngine.NotifyDriverXRun();
    }

    void NotifyClientXRun(std::int32_t refnum)
    {
        Locked([&] { fEngine.NotifyClientXRun(refnum); });
    }

    void NotifyGraphReorder()
    {
        Locked([&] { fEngine.NotifyGraphReorder(); });
    }

    void NotifyBufferSize(jack_nframes_t buffer_size)
    {
        Locked([&] { fEngine.NotifyBufferSize(buffer_size); });
    }
    void NotifySampleRate(jack_nframes_t sample_rate)
    {
        Locked([&] { fEngine.NotifySampleRate(sample_rate); });
    }
    void NotifySecondarySampleRate(const char* card, jack_nframes_t sample_rate, std::int32_t direction)
    {
        Locked([&] { fEngine.NotifySecondarySampleRate(card, sample_rate, direction); });
    }
    void NotifyFreewheel(bool onoff)
    {
        Locked([&] { fEngine.NotifyFreewheel(onoff); });
    }

    void NotifyFailure(std::int32_t code, const char* reason)
    {
        Locked([&] { fEngine.NotifyFailure(code, reason); });
    }

    Result<std::int32_t> GetClientPID(const char* name)
    {
        return Locked([&] { return fEngine.GetClientPID(name); });
    }

    Result<std::int32_t> GetClientRefNum(const char* name)
    {
        return Locked([&] { return fEngine.GetClientRefNum(name); });
    }

    void NotifyQuit()
    {
        // No lock needed
        Guard([&] { fEngine.NotifyQuit(); });
    }

    Result<void> GetUUIDForClientName(const char* client_name, char* uuid_res)
    {
        return Locked([&] { return fEngine.GetUUIDForClientName(client_name, uuid_res); });
    }

    Result<void> GetClientNameForUUID(const char* uuid, char* name_res)
    {
        return Locked([&] { return fEngine.GetClientNameForUUID(uuid, name_res); });
    }
    Result<void> ReserveClientName(const char* name, const char* uuid)
    {
        return Locked([&] { return fEngine.ReserveClientName(name, uuid); });
    }
};

} // namespace jackmini

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

#include "Engine.h"
#include "Exception.h"
#include "Mutex.h"
#include "Tools.h"
#include "jackmini/Error.hpp"

#include <cstdint>
#include <expected>
#include <new>
#include <type_traits>

namespace jackmini {

// \brief Locked Engine, access to methods is serialized using a mutex.
//
// The methods return Result. The engine's int codes (0 on success) are converted here.

class SERVER_EXPORT LockedEngine {
private:
    Engine m_engine;

    // An allocation failure in the engine becomes ServerFailure. Other exceptions are not handled here.
    template <class F>
    static auto guard(F&& f) -> std::invoke_result_t<F>
    {
        using R = std::invoke_result_t<F>;
        try {
            return f();
        } catch (const std::bad_alloc&) {
            jack_error("Memory allocation error...");
            if constexpr (std::is_void_v<R>) {
                return;
            } else {
                return std::unexpected(Error::kServerFailure);
            }
        }
    }

    // Runs f with the engine locked.
    template <class F>
    auto locked(F&& f) -> std::invoke_result_t<F>
    {
        Lock lock(&m_engine);
        return guard(f);
    }

    // Runs f with the engine locked, if the client is registered.
    template <class F>
    auto client(std::int32_t refnum, F&& f) -> std::invoke_result_t<F>
    {
        Lock lock(&m_engine);
        if (!m_engine.checkClient(refnum)) {
            return std::unexpected(Error::kNotFound);
        }
        return guard(f);
    }

    // Closing the last client of a temporary server throws JackTemporaryException: the server quits.
    template <class F>
    Result<void> closing(std::int32_t refnum, F&& f)
    {
        try {
            return client(refnum, f);
        } catch (const TemporaryException&) {
            jack_error("JackTemporaryException : now quits...");
            Tools::killServer();
            return { };
        }
    }

public:
    LockedEngine(GraphManager* manager, Synchro* table, EngineControl* controler, char selfConnectMode)
        : m_engine(manager, table, controler, selfConnectMode)
    {
    }
    ~LockedEngine()
    {
    }

    bool lock() { return m_engine.lock(); }
    bool unlock() { return m_engine.unlock(); }
    bool trylock() { return m_engine.trylock(); }

    Result<void> open()
    {
        // No lock needed
        return guard([&] { return m_engine.open(); });
    }
    Result<void> close()
    {
        // No lock needed
        return guard([&] { return m_engine.close(); });
    }

    // Client management
    Result<void> clientCheck(const char* name, UuidC uuid, char* nameRes, std::int32_t protocol, std::int32_t options, std::int32_t* status)
    {
        return locked([&] { return m_engine.clientCheck(name, uuid, nameRes, protocol, options, status); });
    }
    Result<void> clientExternalOpen(const char* name, std::int32_t pid, UuidC uuid, std::int32_t* ref, ShmSegment* sharedEngine, ShmSegment* sharedClient, ShmSegment* sharedGraphManager)
    {
        return locked([&] { return m_engine.clientExternalOpen(name, pid, uuid, ref, sharedEngine, sharedClient, sharedGraphManager); });
    }
    Result<void> registerDriver(const char* name, std::int32_t* ref, EngineControl** sharedEngine, GraphManager** sharedManager, ClientInterface* client, bool wait)
    {
        return locked([&] { return m_engine.registerDriver(name, ref, sharedEngine, sharedManager, client, wait); });
    }

    Result<void> clientExternalClose(std::int32_t refnum)
    {
        return closing(refnum, [&] { return m_engine.clientExternalClose(refnum); });
    }
    Result<void> unregisterDriver(std::int32_t refnum, bool wait)
    {
        return closing(refnum, [&] { return m_engine.unregisterDriver(refnum, wait); });
    }

    Result<void> clientActivate(std::int32_t refnum, bool isRealTime, bool wait)
    {
        return client(refnum, [&] { return m_engine.clientActivate(refnum, isRealTime, wait); });
    }
    Result<void> clientDeactivate(std::int32_t refnum, bool wait)
    {
        return client(refnum, [&] { return m_engine.clientDeactivate(refnum, wait); });
    }
    void clientKill(std::int32_t refnum)
    {
        locked([&] { m_engine.clientKill(refnum); });
    }

    // Port management
    Result<void> portRegister(std::int32_t refnum, const char* name, const char* type, std::uint32_t flags, std::uint32_t bufferSize, PortIdC* port)
    {
        return client(refnum, [&] { return m_engine.portRegister(refnum, name, type, flags, bufferSize, port); });
    }
    Result<void> portUnRegister(std::int32_t refnum, PortIdC port)
    {
        return client(refnum, [&] { return m_engine.portUnRegister(refnum, port); });
    }

    Result<void> portConnect(std::int32_t refnum, const char* src, const char* dst)
    {
        return client(refnum, [&] { return m_engine.portConnect(refnum, src, dst); });
    }
    Result<void> portDisconnect(std::int32_t refnum, const char* src, const char* dst)
    {
        return client(refnum, [&] { return m_engine.portDisconnect(refnum, src, dst); });
    }

    Result<void> portConnect(std::int32_t refnum, PortIdC src, PortIdC dst)
    {
        return client(refnum, [&] { return m_engine.portConnect(refnum, src, dst); });
    }
    Result<void> portDisconnect(std::int32_t refnum, PortIdC src, PortIdC dst)
    {
        return client(refnum, [&] { return m_engine.portDisconnect(refnum, src, dst); });
    }

    Result<void> portRename(std::int32_t refnum, PortIdC port, const char* name)
    {
        return client(refnum, [&] { return m_engine.portRename(refnum, port, name); });
    }

    Result<void> computeTotalLatencies()
    {
        return locked([&] { return m_engine.computeTotalLatencies(); });
    }

    // Graph
    bool process(TimeC curCycleBegin, TimeC prevCycleEnd)
    {
        // RT : no lock
        return m_engine.process(curCycleBegin, prevCycleEnd);
    }

    // Notifications
    void notifyDriverXRun()
    {
        // Coming from the driver in RT : no lock
        m_engine.notifyDriverXRun();
    }

    void notifyClientXRun(std::int32_t refnum)
    {
        locked([&] { m_engine.notifyClientXRun(refnum); });
    }

    void notifyGraphReorder()
    {
        locked([&] { m_engine.notifyGraphReorder(); });
    }

    void notifyBufferSize(NframesC bufferSize)
    {
        locked([&] { m_engine.notifyBufferSize(bufferSize); });
    }
    void notifySampleRate(NframesC sampleRate)
    {
        locked([&] { m_engine.notifySampleRate(sampleRate); });
    }
    void notifySecondarySampleRate(const char* card, NframesC sampleRate, std::int32_t direction)
    {
        locked([&] { m_engine.notifySecondarySampleRate(card, sampleRate, direction); });
    }
    void notifyFreewheel(bool onoff)
    {
        locked([&] { m_engine.notifyFreewheel(onoff); });
    }

    void notifyFailure(std::int32_t code, const char* reason)
    {
        locked([&] { m_engine.notifyFailure(code, reason); });
    }

    Result<std::int32_t> getClientPID(const char* name)
    {
        return locked([&] { return m_engine.getClientPID(name); });
    }

    Result<std::int32_t> getClientRefNum(const char* name)
    {
        return locked([&] { return m_engine.getClientRefNum(name); });
    }

    void notifyQuit()
    {
        // No lock needed
        guard([&] { m_engine.notifyQuit(); });
    }

    Result<void> getUUIDForClientName(const char* clientName, char* uuidRes)
    {
        return locked([&] { return m_engine.getUUIDForClientName(clientName, uuidRes); });
    }

    Result<void> getClientNameForUUID(const char* uuid, char* nameRes)
    {
        return locked([&] { return m_engine.getClientNameForUUID(uuid, nameRes); });
    }
    Result<void> reserveClientName(const char* name, const char* uuid)
    {
        return locked([&] { return m_engine.reserveClientName(name, uuid); });
    }
};

} // namespace jackmini

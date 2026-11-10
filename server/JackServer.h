// Copyright (C) 2001 Paul Davis
// Copyright (C) 2004-2008 Grame
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

#include "JackCompilerDeps.h"
#include "JackConnectionManager.h"
#include "JackDriverInfo.h"
#include "JackDriverLoader.h"
#include "JackGlobals.h"
#include "JackPlatformPlug.h"
#include "jackmini/error.hpp"
#include "jslist.h"
#include "driver_interface.h"

#include <array>
#include <cstdint>
#include <memory>

namespace jackmini {

class GraphManager;
class DriverClientInterface;
struct EngineControl;
class LockedEngine;

// \brief The Jack server.

class SERVER_EXPORT ServerCore {

private:
    std::unique_ptr<DriverInfo> m_driverInfo;
    DriverClientInterface* m_audioDriver;
    DriverClientInterface* m_freewheelDriver; // Owned by fThreadedFreewheelDriver
    std::unique_ptr<DriverClientInterface> m_threadedFreewheelDriver;
    std::unique_ptr<LockedEngine> m_engine;
    std::unique_ptr<EngineControl> m_engineControl;
    GraphManager* m_graphManager;
    ServerChannel m_requestChannel;
    ConnectionManager m_connectionState;
    std::array<Synchro, kClientNum> m_synchroTable;
    bool m_freewheel;


public:
    ServerCore(bool sync, bool temporary, std::int32_t timeout, bool rt, std::int32_t priority, std::int32_t portMax, bool verbose, TimerType clock, char selfConnectMode, const char* serverName);
    ~ServerCore();

    // Server control
    Result<void> open(DriverDesc* driverDesc, JSListC* driverParams);
    void close();

    Result<void> start(bool freewheel = false);
    Result<void> stop();

    bool isRunning();

    // RT thread
    void notify(std::int32_t refnum, std::int32_t notify, std::int32_t value);

    // From request thread : API
    Result<void> setBufferSize(NframesC bufferSize);
    Result<void> setFreewheel(bool onoff);

    // Transport management
    Result<void> releaseTimebase(std::int32_t refnum);
    Result<void> setTimebaseCallback(std::int32_t refnum, std::int32_t conditional);

    // Object access
    LockedEngine* getEngine();
    EngineControl* getEngineControl();
    Synchro* getSynchroTable();
    GraphManager* getGraphManager();
};

} // namespace jackmini

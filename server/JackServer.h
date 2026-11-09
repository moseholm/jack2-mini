/*
Copyright (C) 2001 Paul Davis
Copyright (C) 2004-2008 Grame

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

*/

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

#include <memory>

namespace jackmini {

class JackGraphManager;
class JackDriverClientInterface;
struct JackEngineControl;
class JackLockedEngine;

/*!
\brief The Jack server.
*/

class SERVER_EXPORT JackServer {

private:
    std::unique_ptr<JackDriverInfo> fDriverInfo;
    JackDriverClientInterface* fAudioDriver;
    JackDriverClientInterface* fFreewheelDriver; // Owned by fThreadedFreewheelDriver
    std::unique_ptr<JackDriverClientInterface> fThreadedFreewheelDriver;
    std::unique_ptr<JackLockedEngine> fEngine;
    std::unique_ptr<JackEngineControl> fEngineControl;
    JackGraphManager* fGraphManager;
    JackServerChannel fRequestChannel;
    JackConnectionManager fConnectionState;
    JackSynchro fSynchroTable[CLIENT_NUM];
    bool fFreewheel;


public:
    JackServer(bool sync, bool temporary, int timeout, bool rt, int priority, int port_max, bool verbose, jack_timer_type_t clock, char self_connect_mode, const char* server_name);
    ~JackServer();

    // Server control
    Result<void> Open(jack_driver_desc_t* driver_desc, JSList* driver_params);
    void Close();

    Result<void> Start(bool freewheel = false);
    Result<void> Stop();

    bool IsRunning();

    // RT thread
    void Notify(int refnum, int notify, int value);

    // From request thread : API
    Result<void> SetBufferSize(jack_nframes_t buffer_size);
    Result<void> SetFreewheel(bool onoff);

    // Transport management
    Result<void> ReleaseTimebase(int refnum);
    Result<void> SetTimebaseCallback(int refnum, int conditional);

    // Object access
    JackLockedEngine* GetEngine();
    JackEngineControl* GetEngineControl();
    JackSynchro* GetSynchroTable();
    JackGraphManager* GetGraphManager();
};

} // namespace jackmini

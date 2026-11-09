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

#include "JackServer.h"
#include "JackAudioDriver.h"
#include "JackChannel.h"
#include "JackClientControl.h"
#include "JackDriverInfo.h"
#include "JackDriverLoader.h"
#include "JackEngineControl.h"
#include "JackError.h"
#include "JackFreewheelDriver.h"
#include "JackGlobals.h"
#include "JackGraphManager.h"
#include "JackLockedEngine.h"
#include "JackMessageBuffer.h"
#include "JackServerGlobals.h"
#include "JackSystemDeps.h"
#include "JackThreadedDriver.h"
#include "JackTime.h"
#include "driver_interface.h"

const char* jack_get_self_connect_mode_description(char mode);

namespace jackmini {

// The driver reports 0 on success and -1 on failure.
static Result<void> DriverResult(int rc)
{
    return (rc < 0) ? Result<void>(std::unexpected(Error::ServerFailure)) : Result<void> { };
}

//----------------
// Server control
//----------------
JackServer::JackServer(bool sync, bool temporary, int timeout, bool rt, int priority, int port_max, bool verbose, jack_timer_type_t clock, char self_connect_mode, const char* server_name)
{
    if (rt) {
        jack_info("JACK server starting in realtime mode with priority %ld", priority);
    } else {
        jack_info("JACK server starting in non-realtime mode");
    }

    jack_info("self-connect-mode is \"%s\"", jack_get_self_connect_mode_description(self_connect_mode));

    fGraphManager = JackGraphManager::Allocate(port_max);
    fEngineControl = std::make_unique<JackEngineControl>(sync, temporary, timeout, rt, priority, verbose, clock, server_name);
    fEngine = std::make_unique<JackLockedEngine>(fGraphManager, GetSynchroTable(), fEngineControl.get(), self_connect_mode);

    // A distinction is made between the threaded freewheel driver and the
    // regular freewheel driver because the freewheel driver needs to run in
    // threaded mode when freewheel mode is active and needs to run as a slave
    // when freewheel mode isn't active.
    JackFreewheelDriver* freewheelDriver = new JackFreewheelDriver(fEngine.get(), GetSynchroTable());
    fThreadedFreewheelDriver = std::make_unique<JackThreadedDriver>(freewheelDriver);

    fFreewheelDriver = freewheelDriver;
    fDriverInfo = std::make_unique<JackDriverInfo>();
    fAudioDriver = NULL;
    fFreewheel = false;
    JackServerGlobals::fInstance = this; // Unique instance
    JackGlobals::fVerbose = verbose;
}

JackServer::~JackServer()
{
    // The engine uses the engine control, so it goes first.
    JackGraphManager::Destroy(fGraphManager);
    fDriverInfo.reset();
    fThreadedFreewheelDriver.reset();
    fEngine.reset();
    fEngineControl.reset();
}

Result<void> JackServer::Open(jack_driver_desc_t* driver_desc, JSList* driver_params)
{
    // TODO: move that in reworked JackServerGlobals::Init()
    if (!JackMessageBuffer::Create()) {
        jack_error("Cannot create message buffer");
    }

    if ((fAudioDriver = fDriverInfo->Open(driver_desc, fEngine.get(), GetSynchroTable(), driver_params)) == NULL) {
        jack_error("Cannot initialize driver");
        JackMessageBuffer::Destroy();
        return std::unexpected(Error::ServerFailure);
    }

    if (!fRequestChannel.Open(fEngineControl->fServerName, this)) {
        jack_error("Server channel open error");
        fAudioDriver->Close();
        JackMessageBuffer::Destroy();
        return std::unexpected(Error::ServerFailure);
    }

    if (!fEngine->Open()) {
        jack_error("Cannot open engine");
        fRequestChannel.Close();
        fAudioDriver->Close();
        JackMessageBuffer::Destroy();
        return std::unexpected(Error::ServerFailure);
    }

    if (fFreewheelDriver->Open() < 0) {
        jack_error("Cannot open freewheel driver");
        fEngine->Close();
        fRequestChannel.Close();
        fAudioDriver->Close();
        JackMessageBuffer::Destroy();
        return std::unexpected(Error::ServerFailure);
    }

    if (fAudioDriver->Attach() < 0) {
        jack_error("Cannot attach audio driver");
        fFreewheelDriver->Close();
        fEngine->Close();
        fRequestChannel.Close();
        fAudioDriver->Close();
        JackMessageBuffer::Destroy();
        return std::unexpected(Error::ServerFailure);
    }

    fFreewheelDriver->SetMaster(false);
    fAudioDriver->SetMaster(true);
    fAudioDriver->AddSlave(fFreewheelDriver);
    init_time();
    set_clock_source(fEngineControl->fClockSource);
    return { };
}

void JackServer::Close()
{
    jack_log("JackServer::Close");
    fRequestChannel.Close();
    fAudioDriver->Detach();
    fAudioDriver->Close();
    fFreewheelDriver->Close();
    fEngine->Close();
    // TODO: move that in reworked JackServerGlobals::Destroy()
    JackMessageBuffer::Destroy();
    end_time();
}

Result<void> JackServer::Start(bool freewheel)
{
    jack_log("JackServer::Start freewheel = %d", freewheel);
    if (freewheel) {
        // Begin in freewheel mode with the sound card never started (SetFreewheel would stop it)
        fFreewheel = true;
        fEngine->NotifyFreewheel(true);
        fAudioDriver->SetMaster(false);
        fFreewheelDriver->SetMaster(true);
        if (fThreadedFreewheelDriver->Start() < 0) {
            return std::unexpected(Error::ServerFailure);
        }
    } else if (fAudioDriver->Start() < 0) {
        return std::unexpected(Error::ServerFailure);
    }
    return fRequestChannel.Start();
}

Result<void> JackServer::Stop()
{
    jack_log("JackServer::Stop");
    int res = -1;

    if (fFreewheel) {
        if (fThreadedFreewheelDriver) {
            res = fThreadedFreewheelDriver->Stop();
        }
    } else {
        if (fAudioDriver) {
            res = fAudioDriver->Stop();
        }
    }

    fEngine->NotifyQuit();
    fRequestChannel.Stop();
    fEngine->NotifyFailure(JackFailure | JackServerError, JACK_SERVER_FAILURE);

    return DriverResult(res);
}

bool JackServer::IsRunning()
{
    jack_log("JackServer::IsRunning");
    assert(fAudioDriver);
    return fAudioDriver->IsRunning();
}

//---------------------------
// From request thread : API
//---------------------------

Result<void> JackServer::SetBufferSize(jack_nframes_t buffer_size)
{
    jack_log("JackServer::SetBufferSize nframes = %ld", buffer_size);
    jack_nframes_t current_buffer_size = fEngineControl->fBufferSize;

    if (current_buffer_size == buffer_size) {
        jack_log("SetBufferSize: requirement for new buffer size equals current value");
        return { };
    }

    if (fAudioDriver->IsFixedBufferSize()) {
        jack_log("SetBufferSize: driver only supports a fixed buffer size");
        return std::unexpected(Error::InvalidState);
    }

    if (fAudioDriver->Stop() != 0) {
        jack_error("Cannot stop audio driver");
        return std::unexpected(Error::ServerFailure);
    }

    if (fAudioDriver->SetBufferSize(buffer_size) == 0) {
        fEngine->NotifyBufferSize(buffer_size);
        return DriverResult(fAudioDriver->Start());
    } else { // Failure: try to restore current value
        jack_error("Cannot SetBufferSize for audio driver, restore current value %ld", current_buffer_size);
        fAudioDriver->SetBufferSize(current_buffer_size);
        fAudioDriver->Start();
        // SetBufferSize actually failed, so return an error...
        return std::unexpected(Error::ServerFailure);
    }
}

/*
Freewheel mode is implemented by switching from the (audio [slaves] + freewheel) driver to the freewheel driver only:

    - "global" connection state is saved
    - all audio driver and slaves ports are deconnected, thus there is no more dependencies with the audio driver and slaves
    - the freewheel driver will be synchronized with the end of graph execution : all clients are connected to the freewheel driver
    - the freewheel driver becomes the "master"

Normal mode is restored with the connections state valid before freewheel mode was done. Thus one consider that
no graph state change can be done during freewheel mode.
*/

Result<void> JackServer::SetFreewheel(bool onoff)
{
    jack_log("JackServer::SetFreewheel is = %ld want = %ld", fFreewheel, onoff);

    if (fFreewheel) {
        if (onoff) {
            return std::unexpected(Error::InvalidState);
        } else {
            fFreewheel = false;
            fThreadedFreewheelDriver->Stop();
            fGraphManager->Restore(&fConnectionState); // Restore connection state
            fEngine->NotifyFreewheel(onoff);
            fFreewheelDriver->SetMaster(false);
            fAudioDriver->SetMaster(true);
            return DriverResult(fAudioDriver->Start());
        }
    } else {
        if (onoff) {
            fFreewheel = true;
            fAudioDriver->Stop();
            fGraphManager->Save(&fConnectionState); // Save connection state
            // Disconnect all slaves
            std::list<JackDriverInterface*> slave_list = fAudioDriver->GetSlaves();
            std::list<JackDriverInterface*>::const_iterator it;
            for (it = slave_list.begin(); it != slave_list.end(); it++) {
                JackDriver* slave = dynamic_cast<JackDriver*>(*it);
                assert(slave);
                fGraphManager->DisconnectAllPorts(slave->GetClientControl()->fRefNum);
            }
            // Disconnect master
            fGraphManager->DisconnectAllPorts(fAudioDriver->GetClientControl()->fRefNum);
            fEngine->NotifyFreewheel(onoff);
            fAudioDriver->SetMaster(false);
            fFreewheelDriver->SetMaster(true);
            return DriverResult(fThreadedFreewheelDriver->Start());
        } else {
            return std::unexpected(Error::InvalidState);
        }
    }
}

//---------------------------
// Coming from the RT thread
//---------------------------

void JackServer::Notify(int refnum, int notify, int value)
{
    switch (notify) {

    case kGraphOrderCallback:
        fEngine->NotifyGraphReorder();
        break;

    case kXRunCallback:
        fEngine->NotifyClientXRun(refnum);
        break;
    }
}

//--------------------
// Backend management
//--------------------

//----------------------
// Transport management
//----------------------

Result<void> JackServer::ReleaseTimebase(int refnum)
{
    return fEngineControl->fTransport.ResetTimebase(refnum);
}

Result<void> JackServer::SetTimebaseCallback(int refnum, int conditional)
{
    return fEngineControl->fTransport.SetTimebaseMaster(refnum, conditional);
}

JackLockedEngine* JackServer::GetEngine()
{
    return fEngine.get();
}

JackSynchro* JackServer::GetSynchroTable()
{
    return fSynchroTable;
}

JackEngineControl* JackServer::GetEngineControl()
{
    return fEngineControl.get();
}

JackGraphManager* JackServer::GetGraphManager()
{
    return fGraphManager;
}

} // namespace jackmini

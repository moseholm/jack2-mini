/*
Copyright (C) 2004-2008 Grame

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

#include "JackGlobals.h"

namespace jackmini
{

bool JackGlobals::fVerbose = 0;

jack_tls_key JackGlobals::fRealTimeThread;
static bool gKeyRealtimeThreadInitialized = jack_tls_allocate_key(&JackGlobals::fRealTimeThread);

jack_tls_key JackGlobals::fNotificationThread;
static bool gKeyNotificationThreadInitialized = jack_tls_allocate_key(&JackGlobals::fNotificationThread);

jack_tls_key JackGlobals::fKeyLogFunction;
static bool fKeyLogFunctionInitialized = jack_tls_allocate_key(&JackGlobals::fKeyLogFunction);

JackMutex* JackGlobals::fOpenMutex = new JackMutex();
JackMutex* JackGlobals::fSynchroMutex = new JackMutex();
std::atomic<bool> JackGlobals::fServerRunning{false};
JackClient* JackGlobals::fClientTable[CLIENT_NUM] = {};

jack_thread_creator_t JackGlobals::fJackThreadCreator = pthread_create;

void JackGlobals::CheckContext(const char* name)
{}

} // end of namespace

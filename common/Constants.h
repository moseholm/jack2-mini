// Copyright (C) 2004-2008 Grame
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation; either version 2.1 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.

#pragma once

#include <jack/uuid.h>
#include <cstdint>

#ifdef HAVE_CONFIG_H
#include "config.h"
#else
inline constexpr std::int32_t kClientNum = 64;
inline constexpr std::int32_t kPortNumForClient = 768;
#endif

inline constexpr std::int32_t kBufferSizeMax = 8192;

inline constexpr std::int32_t kPortNameSize = 256;
inline constexpr std::int32_t kPortTypeSize = 32;

inline constexpr std::int32_t kServerNameSize = 256;
inline constexpr std::int32_t kClientNameSize = 64;
inline constexpr std::int32_t kMessageSize = 256;
inline constexpr std::int32_t kSessionCommandSize = 256;

inline constexpr std::int32_t kSyncMaxNameSize = 256;

inline constexpr std::int32_t kRealJackPortNameSize = kClientNameSize + kPortNameSize; // full name like "client_name:short_port_name"

inline constexpr std::int32_t kPortNum = 2048;

inline constexpr std::int32_t kPortNumMax = 4096; // The "max" value for ports used in connection manager, although port number in graph manager is dynamic

inline constexpr std::int32_t kDriverPortNum = 256;

inline constexpr std::int32_t kFirstAvailablePort = 1;

inline constexpr std::int32_t kConnectionNumForPort = kPortNumForClient;

inline constexpr std::int32_t kAudioDriverRefnum = 0; // Audio driver is initialized first, it will get the refnum 0
inline constexpr std::int32_t kFreewheelDriverRefnum = 1; // Freewheel driver is initialized second, it will get the refnum 1

inline constexpr const char* kDefaultServerName = "default";

inline constexpr std::int32_t kAllClients = -1; // for notification

inline constexpr std::int32_t kProtocolVersion = 13;

inline constexpr std::int32_t kSocketTimeOut = 2; // in sec
inline constexpr std::int32_t kDriverOpenTimeout = 5; // in sec
inline constexpr std::int32_t kGraphSwitchGuard = 15; // in sec: failure guard for waits on a graph switch, not the time a switch takes
inline constexpr std::int32_t kFreewheelDriverTimeout = 10; // in sec
inline constexpr std::int32_t kDriverTimeoutFactor = 10;

inline constexpr const char* kServerFailure = "JACK server has been closed";

inline constexpr std::int32_t kRequestErrAborted = -13;

inline constexpr std::int32_t kNoPort = 0xFFFE;

inline constexpr std::int32_t kEmpty = 0xFFFD;
inline constexpr std::int32_t kFree = 0xFFFC;

inline constexpr char kDefaultSelfConnectMode = ' '; // allow all requests

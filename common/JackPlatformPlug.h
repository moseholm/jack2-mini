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

#ifndef __JackPlatformPlug__
#define __JackPlatformPlug__

#define jack_server_dir "/dev/shm"
#define jack_client_dir "/dev/shm"
#define JACK_DEFAULT_DRIVER "alsa"

namespace jackmini {
struct JackRequest;
struct JackResult;

class JackPosixMutex;
class JackPosixThread;
class JackSocketServerChannel;
class JackSocketServerNotifyChannel;
class JackSocketNotifyChannel;
class JackClientSocket;
} // namespace jackmini

/* __JackPlatformMutex__ */
#include "JackPosixMutex.h"
namespace jackmini {
typedef JackPosixMutex JackMutex;
}

/* __JackPlatformThread__ */
#include "JackPosixThread.h"
namespace jackmini {
typedef JackPosixThread JackThread;
}

/* __JackPlatformSynchro__  client activation */
#include "JackLinuxFutex.h"
namespace jackmini {
typedef JackLinuxFutex JackSynchro;
}

/* __JackPlatformProcessSync__ */
#include "JackPosixProcessSync.h"
namespace jackmini {
typedef JackPosixProcessSync JackProcessSync;
}

/* __JackPlatformServerChannel__ */
#include "JackSocketServerChannel.h"
namespace jackmini {
typedef JackSocketServerChannel JackServerChannel;
}

/* __JackPlatformServerNotifyChannel__ */
#include "JackSocketServerNotifyChannel.h"
namespace jackmini {
typedef JackSocketServerNotifyChannel JackServerNotifyChannel;
}

/* __JackPlatformNotifyChannel__ */
#include "JackSocketNotifyChannel.h"
namespace jackmini {
typedef JackSocketNotifyChannel JackNotifyChannel;
}

#endif

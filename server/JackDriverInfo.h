/*
Copyright (C) 2001-2005 Paul Davis
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

#include "JackDriver.h"
#include "JackSystemDeps.h"
#include "driver_interface.h"
#include "driver_module.h"

class SERVER_EXPORT JackDriverInfo {

private:
    const jackmini::driver::Module* fModule;
    DRIVER_HANDLE fHandle;
    jackmini::JackDriverClientInterface* fBackend;

public:
    JackDriverInfo()
        : fModule(NULL)
        , fHandle(NULL)
        , fBackend(NULL)
    {
    }
    ~JackDriverInfo();

    jackmini::JackDriverClientInterface* Open(jack_driver_desc_t* driver_desc, jackmini::JackLockedEngine*, jackmini::JackSynchro*, const JSList*);
};

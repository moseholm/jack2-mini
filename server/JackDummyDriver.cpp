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

#include "JackDummyDriver.h"
#include "JackCompilerDeps.h"
#include "JackDriverLoader.h"
#include "JackThreadedDriver.h"
#include <cstdint>
#include <math.h>
#include <unistd.h>
#include <iostream>
#include "driver_module.h"


static DriverDesc* describeDriver()
{
    DriverDesc* desc;
    DriverDescFiller filler;
    DriverParamValue value;

    desc = jack_driver_descriptor_construct("dummy", kDriverMaster, "Timer based backend", &filler);

    value.m_ui = 2U;
    jack_driver_descriptor_add_parameter(desc, &filler, "capture", 'C', kDriverParamUInt, &value, nullptr, "Number of capture ports", nullptr);
    jack_driver_descriptor_add_parameter(desc, &filler, "playback", 'P', kDriverParamUInt, &value, nullptr, "Number of playback ports", nullptr);

    value.m_ui = 48000U;
    jack_driver_descriptor_add_parameter(desc, &filler, "rate", 'r', kDriverParamUInt, &value, nullptr, "Sample rate", nullptr);

    value.m_i = 0;
    jack_driver_descriptor_add_parameter(desc, &filler, "monitor", 'm', kDriverParamBool, &value, nullptr, "Provide monitor ports for the output", nullptr);

    value.m_ui = 1024U;
    jack_driver_descriptor_add_parameter(desc, &filler, "period", 'p', kDriverParamUInt, &value, nullptr, "Frames per period", nullptr);

    value.m_ui = 21333U;
    jack_driver_descriptor_add_parameter(desc, &filler, "wait", 'w', kDriverParamUInt, &value, nullptr, "Number of usecs to wait between engine processes", nullptr);

    return desc;
}

static jackmini::DriverClientInterface* initializeDriver(jackmini::LockedEngine* engine, jackmini::Synchro* table, const JSListC* params)
{
    NframesC sampleRate = 48000;
    NframesC bufferSize = 1024;
    std::uint32_t capturePorts = 2;
    std::uint32_t playbackPorts = 2;
    std::int32_t waitTime = 0;
    const JSListC* node;
    const DriverParam* param;
    bool monitor = false;

    for (node = params; node; node = jack_slist_next(node)) {
        param = (const DriverParam*)node->m_data;

        switch (param->m_character) {

        case 'C':
            capturePorts = param->m_value.m_ui;
            break;

        case 'P':
            playbackPorts = param->m_value.m_ui;
            break;

        case 'r':
            sampleRate = param->m_value.m_ui;
            break;

        case 'p':
            bufferSize = param->m_value.m_ui;
            break;

        case 'w':
            waitTime = param->m_value.m_ui;
            break;

        case 'm':
            monitor = param->m_value.m_i;
            break;
        }
    }

    if (waitTime > 0) {
        bufferSize = lroundf((float(waitTime) * float(sampleRate)) / 1000000.0f);
    }

    if (bufferSize > kBufferSizeMax) {
        bufferSize = kBufferSizeMax;
        jack_error("Buffer size set to %d", kBufferSizeMax);
    }

    jackmini::DriverClientInterface* driver = new jackmini::ThreadedDriver(new jackmini::DummyDriver("system", "dummy_pcm", engine, table));
    if (driver->open(bufferSize, sampleRate, 1, 1, capturePorts, playbackPorts, monitor, "dummy", "dummy", 0, 0) == 0) {
        return driver;
    } else {
        delete driver;
        return nullptr;
    }
}


extern "C" SERVER_EXPORT const jackmini::driver::Module* jackmini_driver_module()
{
    static const jackmini::driver::Module kDriverModule { jackmini::driver::kModuleVersion, describeDriver, initializeDriver };
    return &kDriverModule;
}

// Copyright (C) 2001 Paul Davis
// Copyright (C) 2004-2013 Grame
// Copyright (C) 2016-2022 Filipe Coelho
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

#include <getopt.h>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "CString.h"
#include "Constants.h"
#include "DriverInterface.h"
#include "Log.h"
#include "PlatformPlug.h"
#include "Types.h"
#include "jackmini/Control.hpp"

using jackmini::control::Choice;
using jackmini::control::Driver;
using jackmini::control::Parameter;
using jackmini::control::Server;
using jackmini::control::Value;

// Prototype to be found in libjackserver
void silentJackErrorCallback(const char* desc);

static void copyright(FILE* file)
{
    fprintf(file, "jackdmp " JACK_VERSION "\n"
                  "Copyright 2001-2005 Paul Davis and others.\n"
                  "Copyright 2004-2016 Grame.\n"
                  "Copyright 2016-2026 Filipe Coelho.\n"
                  "jackdmp comes with ABSOLUTELY NO WARRANTY\n"
                  "This is free software, and you are welcome to redistribute it\n"
                  "under certain conditions; see the file COPYING for details\n");
}

static void printServerDrivers(const Server& server, FILE* file)
{
    fprintf(file, "Available backends:\n");
    for (const Driver& driver : server.drivers()) {
        fprintf(file, "      %s (%s)\n", driver.m_name.c_str(), driver.m_master ? "master" : "slave");
    }
    fprintf(file, "\n");
}

static void usage(FILE* file, const Server& server, bool full = true)
{
    fprintf(file, "\n"
                  "Usage: jackdmp [ --no-realtime OR -r ]\n"
                  "               [ --realtime OR -R [ --realtime-priority OR -P priority ] ]\n"
                  "      (the two previous arguments are mutually exclusive. The default is --realtime)\n"
                  "               [ --name OR -n server-name ]\n"
                  "               [ --timeout OR -t client-timeout-in-msecs ]\n"
                  "               [ --port-max OR -p maximum-number-of-ports]\n"
                  "               [ --verbose OR -v ]\n"
                  "               [ --clocksource OR -c [ h(pet) | s(ystem) ]\n"
                  "               [ --autoconnect OR -a <modechar>]\n");

    for (const Parameter& parameter : server.parameters()) {
        if (parameter.m_name != "self-connect-mode") {
            continue;
        }
        fprintf(file, "                 where <modechar> is one of:\n");
        for (const Choice& choice : parameter.m_choices) {
            const char mode = std::get<char>(choice.m_value);
            fprintf(file, "                   '%c' - %s", mode, choice.m_description.c_str());
            if (mode == std::get<char>(parameter.m_defaultValue)) {
                fprintf(file, " (default)");
            }
            fprintf(file, "\n");
        }
    }

    fprintf(file,
        "               [ --silent OR -s ]\n"
        "               [ --sync OR -S ]\n"
        "               [ --temporary OR -T ]\n"
        "               [ --freewheel OR -F ]\n"
        "               [ --version OR -V ]\n"
        "         -d master-backend-name [ ... master-backend args ... ]\n"
        "       jackdmp -d master-backend-name --help\n"
        "             to display options for each master backend\n\n");

    if (full) {
        printServerDrivers(server, file);
    }
}

static void printVersion()
{
    printf("jackdmp version " JACK_VERSION " tmpdir %s protocol %d"
           "\n",
        kServerDir, kProtocolVersion);
}

// The self connect mode is one of the characters the server offers.
static bool selfConnectModeIsValid(const Server& server, char mode)
{
    for (const Parameter& parameter : server.parameters()) {
        if (parameter.m_name != "self-connect-mode") {
            continue;
        }
        for (const Choice& choice : parameter.m_choices) {
            if (std::get<char>(choice.m_value) == mode) {
                return true;
            }
        }
    }
    return false;
}

int main(std::int32_t argc, char** argv)
{
    for (std::int32_t a = 1; a < argc; ++a) {
        if (!strcmp(argv[a], "--version") || !strcmp(argv[a], "-V")) {
            printVersion();
            return 0;
        }
    }

    const char* options = "-d:P:vshrRSTFt:n:p:a:c:";

    std::array<struct option, 16> longOptions = { { { "clock-source", 1, 0, 'c' },
        { "audio-driver", 1, 0, 'd' },
        { "verbose", 0, 0, 'v' },
        { "help", 0, 0, 'h' },
        { "port-max", 1, 0, 'p' },
        { "name", 1, 0, 'n' },
        { "realtime", 0, 0, 'R' },
        { "no-realtime", 0, 0, 'r' },
        { "realtime-priority", 1, 0, 'P' },
        { "timeout", 1, 0, 't' },
        { "temporary", 0, 0, 'T' },
        { "freewheel", 0, 0, 'F' },
        { "silent", 0, 0, 's' },
        { "sync", 0, 0, 'S' },
        { "autoconnect", 1, 0, 'a' },
        { 0, 0, 0, 0 } } };

    char* masterDriverName = nullptr;

    copyright(stdout);
    auto created = Server::create("");
    if (!created) {
        fprintf(stderr, "Failed to create server object\n");
        return -1;
    }
    Server server = std::move(*created);

    opterr = 0;
    std::int32_t opt = 0;
    std::int32_t optionIndex = 0;
    while (!masterDriverName && (opt = getopt_long(argc, argv, options, longOptions.data(), &optionIndex)) != EOF) {
        switch (opt) {

        case 'c':
            if (tolower(optarg[0]) == 'h') {
                server.setParameter("clock-source", static_cast<std::uint64_t>(kTimerHpet));
            } else if (tolower(optarg[0]) == 'c' || tolower(optarg[0]) == 's') {
                // The cycle clock is not offered: a request for it gets the system clock, as before.
                server.setParameter("clock-source", static_cast<std::uint64_t>(kTimerSystemClock));
            } else {
                usage(stdout, server);
                return -1;
            }
            break;

        case 'a':
            if (!selfConnectModeIsValid(server, optarg[0])) {
                usage(stdout, server);
                return -1;
            }
            server.setParameter("self-connect-mode", Value { optarg[0] });
            break;

        case 'd':
            masterDriverName = optarg;
            break;

        case 'p':
            server.setParameter("port-max", static_cast<std::uint64_t>(jackmini::parseLeadingInteger<std::uint32_t>(optarg)));
            break;

        case 'v':
            server.setParameter("verbose", true);
            break;

        case 's':
            gJackErrorCallback = silentJackErrorCallback;
            break;

        case 'S':
            server.setParameter("sync", true);
            break;

        case 'n':
            server.setParameter("name", std::string(optarg).substr(0, kDriverParamStringMax));
            break;

        case 'P':
            server.setParameter("realtime-priority", static_cast<std::int64_t>(jackmini::parseLeadingInteger<std::int32_t>(optarg)));
            break;

        case 'r':
            server.setParameter("realtime", false);
            break;

        case 'R':
            server.setParameter("realtime", true);
            break;

        case 'T':
            server.setParameter("temporary", true);
            break;

        case 'F':
            server.setParameter("freewheel", true);
            break;

        case 't':
            server.setParameter("client-timeout", static_cast<std::int64_t>(jackmini::parseLeadingInteger<std::int32_t>(optarg)));
            break;

        case 'h':
            usage(stdout, server);
            return 0;

        default:
            fprintf(stderr, "unknown option character %c\n", optopt);
            usage(stdout, server);
            return -1;
        }
    }

    if (!masterDriverName) {
        usage(stderr, server, false);
        return -1;
    }

    if (!server.selectDriver(masterDriverName)) {
        return -1;
    }

    // The driver's arguments are the rest of the command line, after its name.
    std::vector<char*> driverArgs { masterDriverName };
    for (std::int32_t i = optind; i < argc; i++) {
        driverArgs.push_back(argv[i]);
    }
    if (!server.parseDriverArguments(static_cast<std::int32_t>(driverArgs.size()), driverArgs.data())) {
        return -1;
    }

    const auto signals = jackmini::control::setupSignals();

    if (!server.start()) {
        fprintf(stderr, "Failed to start server\n");
        return -1;
    }

    jackmini::control::waitForSignal(signals);

    if (!server.stop()) {
        fprintf(stderr, "Cannot stop server...\n");
    }
    return 0;
}

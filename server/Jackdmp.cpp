/*
Copyright (C) 2001 Paul Davis
Copyright (C) 2004-2013 Grame
Copyright (C) 2016-2022 Filipe Coelho

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

#include <getopt.h>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <vector>

#include "JackCString.h"
#include "JackConstants.h"
#include "JackLog.h"
#include "JackPlatformPlug.h"
#include "JackTypes.h"
#include "jackmini/control.hpp"
#include "driver_interface.h"

using jackmini::control::Choice;
using jackmini::control::Driver;
using jackmini::control::Parameter;
using jackmini::control::Server;
using jackmini::control::Value;

// Prototype to be found in libjackserver
void silent_jack_error_callback(const char* desc);

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

static void print_server_drivers(const Server& server, FILE* file)
{
    fprintf(file, "Available backends:\n");
    for (const Driver& driver : server.drivers()) {
        fprintf(file, "      %s (%s)\n", driver.name.c_str(), driver.master ? "master" : "slave");
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
        if (parameter.name != "self-connect-mode") {
            continue;
        }
        fprintf(file, "                 where <modechar> is one of:\n");
        for (const Choice& choice : parameter.choices) {
            const char mode = std::get<char>(choice.value);
            fprintf(file, "                   '%c' - %s", mode, choice.description.c_str());
            if (mode == std::get<char>(parameter.default_value)) {
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
        print_server_drivers(server, file);
    }
}

static void print_version()
{
    printf("jackdmp version " JACK_VERSION " tmpdir " jack_server_dir " protocol %d"
           "\n",
        JACK_PROTOCOL_VERSION);
}

// The self connect mode is one of the characters the server offers.
static bool self_connect_mode_is_valid(const Server& server, char mode)
{
    for (const Parameter& parameter : server.parameters()) {
        if (parameter.name != "self-connect-mode") {
            continue;
        }
        for (const Choice& choice : parameter.choices) {
            if (std::get<char>(choice.value) == mode) {
                return true;
            }
        }
    }
    return false;
}

int main(int argc, char** argv)
{
    for (int a = 1; a < argc; ++a) {
        if (!strcmp(argv[a], "--version") || !strcmp(argv[a], "-V")) {
            print_version();
            return 0;
        }
    }

    const char* options = "-d:P:vshrRSTFt:n:p:a:c:";

    struct option long_options[] = {
        { "clock-source", 1, 0, 'c' },
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
        { 0, 0, 0, 0 }
    };

    char* master_driver_name = NULL;

    copyright(stdout);
    auto created = Server::create("");
    if (!created) {
        fprintf(stderr, "Failed to create server object\n");
        return -1;
    }
    Server server = std::move(*created);

    opterr = 0;
    int opt = 0;
    int option_index = 0;
    while (!master_driver_name && (opt = getopt_long(argc, argv, options, long_options, &option_index)) != EOF) {
        switch (opt) {

        case 'c':
            if (tolower(optarg[0]) == 'h') {
                server.set_parameter("clock-source", static_cast<std::uint64_t>(JACK_TIMER_HPET));
            } else if (tolower(optarg[0]) == 'c' || tolower(optarg[0]) == 's') {
                // The cycle clock is not offered: a request for it gets the system clock, as before.
                server.set_parameter("clock-source", static_cast<std::uint64_t>(JACK_TIMER_SYSTEM_CLOCK));
            } else {
                usage(stdout, server);
                return -1;
            }
            break;

        case 'a':
            if (!self_connect_mode_is_valid(server, optarg[0])) {
                usage(stdout, server);
                return -1;
            }
            server.set_parameter("self-connect-mode", Value { optarg[0] });
            break;

        case 'd':
            master_driver_name = optarg;
            break;

        case 'p':
            server.set_parameter("port-max", static_cast<std::uint64_t>(jackmini::ParseLeadingInteger<unsigned int>(optarg)));
            break;

        case 'v':
            server.set_parameter("verbose", true);
            break;

        case 's':
            jack_error_callback = silent_jack_error_callback;
            break;

        case 'S':
            server.set_parameter("sync", true);
            break;

        case 'n':
            server.set_parameter("name", std::string(optarg).substr(0, JACK_DRIVER_PARAM_STRING_MAX));
            break;

        case 'P':
            server.set_parameter("realtime-priority", static_cast<std::int64_t>(jackmini::ParseLeadingInteger<int>(optarg)));
            break;

        case 'r':
            server.set_parameter("realtime", false);
            break;

        case 'R':
            server.set_parameter("realtime", true);
            break;

        case 'T':
            server.set_parameter("temporary", true);
            break;

        case 'F':
            server.set_parameter("freewheel", true);
            break;

        case 't':
            server.set_parameter("client-timeout", static_cast<std::int64_t>(jackmini::ParseLeadingInteger<int>(optarg)));
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

    if (!master_driver_name) {
        usage(stderr, server, false);
        return -1;
    }

    if (!server.select_driver(master_driver_name)) {
        return -1;
    }

    // The driver's arguments are the rest of the command line, after its name.
    std::vector<char*> driver_args { master_driver_name };
    for (int i = optind; i < argc; i++) {
        driver_args.push_back(argv[i]);
    }
    if (!server.parse_driver_arguments(static_cast<int>(driver_args.size()), driver_args.data())) {
        return -1;
    }

    const auto signals = jackmini::control::setup_signals();

    if (!server.start()) {
        fprintf(stderr, "Failed to start server\n");
        return -1;
    }

    jackmini::control::wait_for_signal(signals);

    if (!server.stop()) {
        fprintf(stderr, "Cannot stop server...\n");
    }
    return 0;
}

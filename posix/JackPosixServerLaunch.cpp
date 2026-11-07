/*
Copyright (C) 2001-2003 Paul Davis
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
#include <array>
#include "JackCString.h"
#include "JackConstants.h"
#include "JackChannel.h"
#include "JackLibGlobals.h"
#include "JackServerLaunch.h"
#include "JackPlatformPlug.h"

#include <sys/wait.h>

using namespace Jack;

/* Exec the JACK server in this process.  Does not return. */
static void start_server_classic_aux(const char* server_name)
{
    FILE* fp = 0;
    std::array<char, 255> filename{};
    std::array<char, 255> arguments{};
    std::array<char, 255> buffer{};
    char* command = 0;
    size_t pos = 0;
    size_t result = 0;
    char** argv = 0;
    int i = 0;
    int good = 0;
    int res;

    if (const char* home = getenv("HOME")) {
        Jack::FormatCString(std::span<char>(filename.data(), 255), "{}/.jackdrc", home);
    } else {
        filename[0] = '\0';
    }
    fp = fopen(filename.data(), "r");

    if (!fp) {
        fp = fopen("/etc/jackdrc", "r");
    }
    /* if still not found, check old config name for backwards compatibility */
    if (!fp) {
        fp = fopen("/etc/jackd.conf", "r");
    }

    if (fp) {
        arguments[0] = '\0';
        res = fscanf(fp, "%s", buffer.data());
        while (res != 0 && res != EOF) {
            strcat(arguments.data(), buffer.data());
            strcat(arguments.data(), " ");
            res = fscanf(fp, "%s", buffer.data());
        }
        if (strlen(arguments.data()) > 0) {
            good = 1;
        }
        fclose(fp);
    }

    if (!good) {
        command = (char*)(JACK_LOCATION "/jackd");
        strncpy(arguments.data(), JACK_LOCATION "/jackd -T -d " JACK_DEFAULT_DRIVER, 255);
    } else {
        result = strcspn(arguments.data(), " ");
        command = (char*)malloc(result + 1);
        strncpy(command, arguments.data(), result);
        command[result] = '\0';
    }

    argv = (char**)malloc(255);

    while (1) {
        /* insert -T and -nserver_name in front of arguments */
        if (i == 1) {
            argv[i] = (char*)malloc(strlen ("-T") + 1);
            strcpy (argv[i++], "-T");
            if (server_name) {
                size_t optlen = strlen("-n");
                char* buf = (char*)malloc(optlen + strlen(server_name) + 1);
                strcpy(buf, "-n");
                strcpy(buf + optlen, server_name);
                argv[i++] = buf;
            }
        }

        /* skip whitespace */
        while (pos < strlen(arguments.data()) && arguments[pos] && arguments[pos] == ' ') {
            ++pos;
        }

        if (pos >= strlen(arguments.data())) {
            break;
        }

        if (arguments[pos] == '\"') {
            ++pos;
            result = strcspn(arguments.data() + pos, "\"");
        } else {
            result = strcspn(arguments.data() + pos, " ");
        }

        if (0 == result) {
            break;
        }

        argv[i] = (char*)malloc(result + 1);
        strncpy(argv[i], arguments.data() + pos, result);
        argv[i][result] = '\0';
        pos += result + 1;

        if (++i > 253) {
            break;
        }
    }

    argv[i] = 0;
    execv(command, argv);

    /* If execv() succeeds, it does not return. There's no point
     * in calling jack_error() here in the child process. */
    fprintf(stderr, "exec of JACK server (command = \"%s\") failed: %s\n", command, strerror(errno));
}

static int start_server_classic(const char* server_name)
{
    /* The double fork() forces the server to become a child of
     * init, which will always clean up zombie process state on
     * termination. This even works in cases where the server
     * terminates but this client does not.
     *
     * Since fork() is usually implemented using copy-on-write
     * virtual memory tricks, the overhead of the second fork() is
     * probably relatively small.
     */
     
    int status;
    pid_t first_child_pid;
    
    first_child_pid = fork();
    switch (first_child_pid) {
        case 0:					/* child process */
            switch (fork()) {
                case 0:			/* grandchild process */
                    start_server_classic_aux(server_name);
                    _exit(99);	/* exec failed */
                case - 1:
                    _exit(98);
                default:
                    _exit(0);
            }
        case - 1:			/* fork() error */
            return 1;		/* failed to start server */
    }
    waitpid(first_child_pid, &status, 0);

    /* only the original parent process goes here */
    return 0;			/* (probably) successful */
}

static int start_server(const char* server_name, jack_options_t options)
{
    if ((options & JackNoStartServer) || getenv("JACK_NO_START_SERVER")) {
        return 1;
    }

    return start_server_classic(server_name);
}

static int server_connect(char* server_name)
{
    JackClientChannel channel;
    int res = channel.ServerCheck(server_name);
    channel.Close();
    JackSleep(2000); // Added by JE - 02-01-2009 (gives
                     // the channel some time to close)
    return res;
}

int try_start_server(jack_varargs_t* va, jack_options_t options, jack_status_t* status)
{
    if (server_connect(va->server_name) < 0) {
        int trys;
        if (start_server(va->server_name, options)) {
            int my_status1 = *status | JackFailure | JackServerFailed;
            *status = (jack_status_t)my_status1;
            return -1;
        }
        trys = 5;
        do {
            sleep(1);
            if (--trys < 0) {
                int my_status1 = *status | JackFailure | JackServerFailed;
                *status = (jack_status_t)my_status1;
                return -1;
            }
        } while (server_connect(va->server_name) < 0);
        int my_status1 = *status | JackServerStarted;
        *status = (jack_status_t)my_status1;
    }

    return 0;
}

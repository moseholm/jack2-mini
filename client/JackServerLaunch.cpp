// Copyright (C) 2001-2003 Paul Davis
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
#include "JackServerLaunch.h"
#include "JackConstants.h"
#include "JackPlatformPlug.h"
#include "JackSocket.h"
#include "JackTime.h"
#include <cstdint>
#include <format>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include <sys/wait.h>

using namespace jackmini;

// Split a command line into words.  Words are separated by spaces, and a word
// that starts with a double quote runs to the closing quote.
static std::vector<std::string> splitCommandLine(std::string_view text)
{
    std::vector<std::string> words;
    size_t pos = 0;
    while (true) {
        while (pos < text.size() && text[pos] == ' ') {
            pos++;
        }
        if (pos >= text.size()) {
            break;
        }

        const std::string_view rest = text.substr(pos);
        size_t length;
        if (rest[0] == '"') {
            pos++;
            length = text.substr(pos).find('"');
        } else {
            length = rest.find(' ');
        }
        if (length == std::string_view::npos) {
            length = text.size() - pos;
        }
        if (length == 0) {
            break;
        }
        words.emplace_back(text.substr(pos, length));
        pos += length + 1;
    }
    return words;
}

// The words of the first .jackdrc that exists, joined by single spaces.
static std::string readJackdrc()
{
    std::vector<std::string> candidates;
    if (const char* home = getenv("HOME")) {
        candidates.push_back(std::format("{}/.jackdrc", home));
    }
    candidates.emplace_back("/etc/jackdrc");

    for (const std::string& path : candidates) {
        std::ifstream file(path);
        if (!file.is_open()) {
            continue;
        }
        std::string joined;
        for (std::string word; file >> word;) {
            if (!joined.empty()) {
                joined += ' ';
            }
            joined += word;
        }
        return joined;
    }
    return { };
}

// Exec the JACK server in this process.  Does not return.
static void startServerClassicAux(const char* serverName)
{
    std::vector<std::string> words = splitCommandLine(readJackdrc());
    if (words.empty()) {
        words = splitCommandLine(std::format("{}/jackd -T -d {}", JACK_LOCATION, kDefaultDriver));
    }

    // insert -T and -nserver_name in front of arguments
    std::vector<std::string> argvWords;
    argvWords.push_back(words[0]);
    argvWords.emplace_back("-T");
    if (serverName) {
        argvWords.push_back(std::format("-n{}", serverName));
    }
    argvWords.insert(argvWords.end(), words.begin() + 1, words.end());

    std::vector<char*> argv;
    for (std::string& word : argvWords) {
        argv.push_back(word.data());
    }
    argv.push_back(nullptr);

    execv(argv[0], argv.data());

    // If execv() succeeds, it does not return. There's no point
    // in calling jack_error() here in the child process.
    fprintf(stderr, "exec of JACK server (command = \"%s\") failed: %s\n", argv[0], strerror(errno));
}

static std::int32_t startServerClassic(const char* serverName)
{
    // The double fork() forces the server to become a child of
    // init, which will always clean up zombie process state on
    // termination. This even works in cases where the server
    // terminates but this client does not.
    //
    // Since fork() is usually implemented using copy-on-write
    // virtual memory tricks, the overhead of the second fork() is
    // probably relatively small.

    std::int32_t status;
    pid_t firstChildPid;

    firstChildPid = fork();
    switch (firstChildPid) {
    case 0:  // child process
        switch (fork()) {
        case 0:  // grandchild process
            startServerClassicAux(serverName);
            _exit(99);  // exec failed
        case -1:
            _exit(98);
        default:
            _exit(0);
        }
    case -1:  // fork() error
        return 1;  // failed to start server
    }
    waitpid(firstChildPid, &status, 0);

    // only the original parent process goes here
    return 0;  // (probably) successful
}

static std::int32_t startServer(const char* serverName, OptionsC options)
{
    if ((options & kNoStartServer) || getenv("JACK_NO_START_SERVER")) {
        return 1;
    }

    return startServerClassic(serverName);
}

static std::int32_t serverConnect(const char* serverName)
{
    ClientSocket socket;
    std::int32_t res = socket.connect(kServerDir, serverName, 0);
    socket.close();
    sleepMicroseconds(2000); // Added by JE - 02-01-2009 (gives
                              // the channel some time to close)
    return res;
}

std::int32_t tryStartServer(const char* serverName, OptionsC options, StatusC* status)
{
    if (serverConnect(serverName) < 0) {
        std::int32_t trys;
        if (startServer(serverName, options)) {
            std::int32_t myStatus1 = *status | kFailure | kServerFailed;
            *status = (StatusC)myStatus1;
            return -1;
        }
        trys = 5;
        do {
            sleep(1);
            if (--trys < 0) {
                std::int32_t myStatus1 = *status | kFailure | kServerFailed;
                *status = (StatusC)myStatus1;
                return -1;
            }
        } while (serverConnect(serverName) < 0);
        std::int32_t myStatus1 = *status | kServerStarted;
        *status = (StatusC)myStatus1;
    }

    return 0;
}

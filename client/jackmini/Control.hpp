// Copyright (C) 2026 JACK contributors
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

#include "Error.hpp"

#include <csignal>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

// Server management, for applications that run the server themselves instead
// of starting jackd.  The server is not a client: this configures and starts
// it in the calling process.  The implementation is in the server library.
// The server library hides its symbols; these are the ones an application uses.
#define JACKMINI_CONTROL_API __attribute__((visibility("default")))

namespace jackmini::control {

// A parameter value.  The alternative is the parameter's type, and it does
// not change: a value of another alternative is refused.
using Value = std::variant<std::int64_t, std::uint64_t, bool, char, std::string>;

// One value that a parameter may take, with what it means.
struct Choice {
    Value m_value;
    std::string m_description;
};

struct Parameter {
    std::string m_name;
    char m_shortName = 0; // the driver option letter; 0 for server parameters
    std::string m_description;
    Value m_value;
    Value m_defaultValue;
    bool m_isSet = false; // whether the value was given, rather than left at the default
    std::vector<Choice> m_choices; // empty unless the parameter takes one of a set of values
};

struct Driver {
    std::string m_name;
    std::string m_description;
    bool m_master = false;
    std::vector<Parameter> m_parameters;
};

// Callbacks for the audio devices that the server acquires and releases, for
// a desktop session that shares them.  All may be null.
struct DeviceHooks {
    bool (*m_onAcquire)(const char* deviceName) = nullptr;
    void (*m_onRelease)(const char* deviceName) = nullptr;
    void (*m_onReservationLoop)() = nullptr;
};

class JACKMINI_CONTROL_API Server {
public:
    // Creates a server object with the given name, which may be empty for the
    // default name.  Nothing is started until start().
    static Result<Server> create(std::string_view name, DeviceHooks hooks = { });

    Server(Server&& other) noexcept;
    Server& operator=(Server&& other) noexcept;
    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;
    ~Server();

    // Opens the selected master driver and starts the engine.  The server
    // name is locked until stop().
    Result<void> start();
    // Stops the engine and releases the server name.
    Result<void> stop();
    bool isRunning() const noexcept;

    std::vector<Driver> drivers() const;
    Result<void> selectDriver(std::string_view driverName);

    // Parses the selected driver's options: argv[0] is the driver name, the
    // rest are its options, as on the jackd command line.  Prints the help and
    // fails for "-h" or an unknown option.
    Result<void> parseDriverArguments(std::int32_t argc, char* argv[]);
    Result<void> setDriverParameter(std::string_view name, Value value);

    std::vector<Parameter> parameters() const;
    Result<void> setParameter(std::string_view name, Value value);

private:
    Server() = default;
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

// The signals that stop the server are blocked in the calling thread and
// received in wait_for_signal(), which returns when the server should stop.
struct SignalSet {
    sigset_t m_signals;
};

JACKMINI_CONTROL_API SignalSet setupSignals();
JACKMINI_CONTROL_API void waitForSignal(const SignalSet& signals);

} // namespace jackmini::control

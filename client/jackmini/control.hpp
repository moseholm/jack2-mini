/*
Copyright (C) 2026 JACK contributors

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

#ifndef JACKMINI_CONTROL_HPP
#define JACKMINI_CONTROL_HPP

#include "error.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

// Server management, for applications that run the server themselves
// instead of starting jackd.  The server is not a client: this is a separate
// connection that configures and starts it.
namespace jackmini::control
{

// A parameter value.  The set of parameters comes from the driver, so the
// type is known only at run time.
using Value = std::variant<std::int64_t, std::uint64_t, bool, char, std::string>;

struct Parameter {
    std::string name;
    char short_name;
    std::string description;
    Value value;
};

struct Driver {
    std::string name;
    std::string description;
    std::vector<Parameter> parameters;
};

class Server {
public:
    // Creates a server object.  Nothing is started until start().
    static Result<Server> create(std::string_view name);

    Server(Server&& other) noexcept;
    Server& operator=(Server&& other) noexcept;
    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;
    ~Server();

    Result<void> start();
    Result<void> stop();
    bool is_running() const noexcept;

    std::vector<Driver> drivers() const;
    Result<void> select_driver(std::string_view driver_name);

    std::vector<Parameter> parameters() const;
    Result<void> set_parameter(std::string_view name, Value value);

    // Internal clients, loaded into the server.
    Result<void> load_internal_client(std::string_view name, std::string_view library,
                                      std::string_view parameters);
    Result<void> unload_internal_client(std::string_view name);

private:
    Server() = default;
    struct Impl;
    std::unique_ptr<Impl> fImpl;
};

} // namespace jackmini::control

#endif

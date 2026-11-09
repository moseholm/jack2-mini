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

#include "jackmini/control.hpp"

#include "JackConstants.h"
#include "JackCString.h"
#include "JackDriverLoader.h"
#include "JackError.h"
#include "JackServer.h"
#include "JackServerGlobals.h"
#include "JackServerLock.h"
#include "JackThread.h"
#include "JackTools.h"
#include "driver_interface.h"
#include "JackDriverInfo.h"
#include "jslist.h"

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <getopt.h>
#include <optional>
#include <pthread.h>
#include <signal.h>
#include <span>
#include <strings.h>
#include <system_error>
#include <unistd.h>
#include <utility>

// The choices of the self connect mode.  The engine relies on these characters.
struct SelfConnectChoice {
    char mode;
    const char* description;
};

static constexpr SelfConnectChoice kSelfConnectChoices[] = {
    { ' ', "Don't restrict self connect requests" },
    { 'E', "Fail self connect requests to external ports only" },
    { 'e', "Ignore self connect requests to external ports only" },
    { 'A', "Fail all self connect requests" },
    { 'a', "Ignore all self connect requests" },
};

const char* jack_get_self_connect_mode_description(char mode)
{
    for (const SelfConnectChoice& choice : kSelfConnectChoices) {
        if (choice.mode == mode) {
            return choice.description;
        }
    }
    return nullptr;
}

namespace jackmini::control
{

namespace
{

// The index of each alternative of Value.  The C descriptors use the same five kinds.
constexpr std::size_t kInt = 0;
constexpr std::size_t kUInt = 1;
constexpr std::size_t kBool = 2;
constexpr std::size_t kChar = 3;
constexpr std::size_t kString = 4;

constexpr std::size_t kNoDriver = static_cast<std::size_t>(-1);

struct ConstraintDeleter {
    void operator()(jack_driver_param_constraint_desc_t* constraint) const noexcept
    {
        jack_constraint_free(constraint);
    }
};
using ConstraintPtr = std::unique_ptr<jack_driver_param_constraint_desc_t, ConstraintDeleter>;

struct DescriptorDeleter {
    void operator()(jack_driver_desc_t* desc) const noexcept
    {
        if (desc != nullptr) {
            free(desc->params);
            free(desc);
        }
    }
};
using DescriptorPtr = std::unique_ptr<jack_driver_desc_t, DescriptorDeleter>;

Value value_from_c(std::size_t kind, const jack_driver_param_value_t& value)
{
    switch (kind) {
    case kInt: return static_cast<std::int64_t>(value.i);
    case kUInt: return static_cast<std::uint64_t>(value.ui);
    case kBool: return value.i != 0;
    case kChar: return value.c;
    default: return std::string(value.str);
    }
}

jack_driver_param_value_t value_to_c(const Value& value)
{
    jack_driver_param_value_t out{};
    switch (value.index()) {
    case kInt: out.i = static_cast<int32_t>(std::get<std::int64_t>(value)); break;
    case kUInt: out.ui = static_cast<uint32_t>(std::get<std::uint64_t>(value)); break;
    case kBool: out.i = std::get<bool>(value) ? 1 : 0; break;
    case kChar: out.c = std::get<char>(value); break;
    default: {
        const std::string& text = std::get<std::string>(value);
        strncpy(out.str, text.c_str(), JACK_DRIVER_PARAM_STRING_MAX);
        break;
    }
    }
    return out;
}

std::size_t kind_of(jack_driver_param_type_t type)
{
    switch (type) {
    case JackDriverParamInt: return kInt;
    case JackDriverParamUInt: return kUInt;
    case JackDriverParamChar: return kChar;
    case JackDriverParamString: return kString;
    default: return kBool;
    }
}

// A value of the same kind as the parameter, or nothing when the kind does not match.
bool assign(Parameter& parameter, Value value)
{
    if (value.index() != parameter.default_value.index()) {
        jack_error("parameter '%s' takes a different kind of value", parameter.name.c_str());
        return false;
    }
    parameter.value = std::move(value);
    parameter.is_set = true;
    return true;
}

Parameter* find_parameter(std::vector<Parameter>& parameters, std::string_view name)
{
    for (Parameter& parameter : parameters) {
        if (parameter.name == name) {
            return &parameter;
        }
    }
    return nullptr;
}

const Parameter* find_parameter(const std::vector<Parameter>& parameters, std::string_view name)
{
    for (const Parameter& parameter : parameters) {
        if (parameter.name == name) {
            return &parameter;
        }
    }
    return nullptr;
}

std::vector<Parameter> server_parameters(const std::string& name)
{
    std::vector<Parameter> parameters;
    auto add = [&parameters](std::string parameter_name, std::string description, Value value) {
        Parameter parameter;
        parameter.name = std::move(parameter_name);
        parameter.description = std::move(description);
        parameter.value = value;
        parameter.default_value = std::move(value);
        parameters.push_back(std::move(parameter));
    };

    add("name", "Server name to use.", name);
    add("realtime", "Whether to use realtime mode.", true);
    add("realtime-priority", "Scheduler priority when running in realtime mode.", std::int64_t{10});
    add("temporary", "Exit once all clients have closed their connections.", false);
    add("freewheel", "Start in freewheel mode: cycles run as fast as possible, without the sound card.", false);
    add("verbose", "Verbose mode.", false);
    add("client-timeout", "Client timeout limit in milliseconds.", std::int64_t{0});
    add("clock-source", "Clocksource type : c(ycle) | h(pet) | s(ystem).", std::uint64_t{0});
    add("port-max", "Maximum number of ports.", static_cast<std::uint64_t>(PORT_NUM));
    add("sync", "Use server synchronous mode.", false);
    add("self-connect-mode", "Whether JACK clients are allowed to connect their own ports",
        Value{static_cast<char>(JACK_DEFAULT_SELF_CONNECT_MODE)});

    Parameter& mode = parameters.back();
    for (const SelfConnectChoice& choice : kSelfConnectChoices) {
        mode.choices.push_back(Choice{ Value{choice.mode}, choice.description });
    }
    return parameters;
}

// One driver: its descriptor, the constraints the descriptor points to, and what the API shows.
struct DriverEntry {
    DescriptorPtr descriptor;
    std::vector<ConstraintPtr> constraints;
    Driver view;
};

std::optional<DriverEntry> make_driver(DescriptorPtr descriptor)
{
    DriverEntry entry;
    entry.descriptor = std::move(descriptor);
    jack_driver_desc_t& desc = *entry.descriptor;

    entry.view.name = desc.name;
    entry.view.description = desc.desc;
    entry.view.master = desc.type == JackDriverMaster;

    for (uint32_t i = 0; i < desc.nparams; i++) {
        const jack_driver_param_desc_t& descriptor_param = desc.params[i];
        const std::size_t kind = kind_of(descriptor_param.type);

        Parameter parameter;
        parameter.name = descriptor_param.name;
        parameter.short_name = descriptor_param.character;
        parameter.description = descriptor_param.short_desc;
        parameter.value = value_from_c(kind, descriptor_param.value);
        parameter.default_value = parameter.value;

        jack_driver_param_constraint_desc_t* constraint = descriptor_param.constraint;
        if (constraint != nullptr) {
            entry.constraints.emplace_back(constraint);
            const bool is_range = (constraint->flags & JACK_CONSTRAINT_FLAG_RANGE) != 0;
            if (!is_range) {
                for (uint32_t c = 0; c < constraint->constraint.enumeration.count; c++) {
                    const jack_driver_param_value_enum_t& possible = constraint->constraint.enumeration.possible_values_array[c];
                    parameter.choices.push_back(Choice{ value_from_c(kind, possible.value),
                                                        possible.short_desc ? possible.short_desc : "" });
                }
            }
        }
        entry.view.parameters.push_back(std::move(parameter));
    }
    return entry;
}

bool load_drivers(std::vector<DriverEntry>& drivers)
{
    JSList* list = jack_drivers_load(NULL);
    if (list == NULL) {
        jack_error("Could not find any drivers in driver directory!");
        return false;
    }

    while (list != NULL) {
        JSList* node = list;
        list = list->next;

        DescriptorPtr descriptor(static_cast<jack_driver_desc_t*>(node->data));
        free(node);

        std::optional<DriverEntry> entry = make_driver(std::move(descriptor));
        if (entry) {
            drivers.push_back(std::move(*entry));
        }
    }
    return true;
}

// The options the engine receives for a driver: the ones that were set.
Result<JSList*> create_param_list(const std::vector<Parameter>& parameters)
{
    JSList* list = NULL;
    for (const Parameter& parameter : parameters) {
        if (!parameter.is_set) {
            continue;
        }
        auto* param = static_cast<jack_driver_param_t*>(malloc(sizeof(jack_driver_param_t)));
        if (param == NULL) {
            jack_error("Allocation of jack_driver_param_t structure failed");
            for (JSList* node = list; node != NULL;) {
                JSList* next = node->next;
                free(node->data);
                free(node);
                node = next;
            }
            return std::unexpected(Error::ServerFailure);
        }
        param->character = parameter.short_name;
        param->value = value_to_c(parameter.value);
        list = jack_slist_append(list, param);
    }
    return list;
}

void destroy_param_list(JSList* list)
{
    while (list != NULL) {
        JSList* next = list->next;
        free(list->data);
        free(list);
        list = next;
    }
}

} // namespace

struct Server::Impl {
    DeviceHooks hooks;
    std::string name;
    std::vector<Parameter> parameters;
    std::vector<DriverEntry> drivers;
    std::size_t selected = kNoDriver;
    std::unique_ptr<JackServer> engine;
    bool running = false;

    // Closes the engine, cleans up its files, and releases the server name.
    void close_engine()
    {
        running = false;
        engine->Close();
        engine.reset();

        jack_log("Cleaning up files");
        JackTools::CleanupFiles(name.c_str());
        jack_log("Releasing lock of server `%s'", name.c_str());
        ReleaseServerLock();
    }
};

Result<Server> Server::create(std::string_view name, DeviceHooks hooks)
{
    Server server;
    server.fImpl = std::make_unique<Impl>();
    Impl& impl = *server.fImpl;

    impl.name = name.empty() ? std::string(JackTools::DefaultServerName()) : std::string(name);
    impl.parameters = server_parameters(impl.name);
    impl.hooks = hooks;

    JackServerGlobals::on_device_acquire = hooks.on_acquire;
    JackServerGlobals::on_device_release = hooks.on_release;
    JackServerGlobals::on_device_reservation_loop = hooks.on_reservation_loop;

    if (!load_drivers(impl.drivers)) {
        return std::unexpected(Error::NotFound);
    }
    return server;
}

Server::Server(Server&& other) noexcept = default;

Server& Server::operator=(Server&& other) noexcept
{
    if (this != &other) {
        if (fImpl && fImpl->engine) {
            fImpl->close_engine();
        }
        fImpl = std::move(other.fImpl);
    }
    return *this;
}

Server::~Server()
{
    if (fImpl && fImpl->engine) {
        fImpl->close_engine();
    }
}

bool Server::is_running() const noexcept
{
    return fImpl && fImpl->running;
}

std::vector<Driver> Server::drivers() const
{
    std::vector<Driver> views;
    if (fImpl) {
        for (const DriverEntry& entry : fImpl->drivers) {
            views.push_back(entry.view);
        }
    }
    return views;
}

Result<void> Server::select_driver(std::string_view driver_name)
{
    if (!fImpl) {
        return std::unexpected(Error::InvalidState);
    }
    for (std::size_t i = 0; i < fImpl->drivers.size(); i++) {
        if (fImpl->drivers[i].view.name != driver_name) {
            continue;
        }
        if (!fImpl->drivers[i].view.master) {
            jack_error("Driver \"%s\" is not a master", fImpl->drivers[i].view.name.c_str());
            return std::unexpected(Error::InvalidArgument);
        }
        fImpl->selected = i;
        return {};
    }
    jack_error("Unknown driver \"%.*s\"", static_cast<int>(driver_name.size()), driver_name.data());
    return std::unexpected(Error::NotFound);
}

std::vector<Parameter> Server::parameters() const
{
    return fImpl ? fImpl->parameters : std::vector<Parameter>{};
}

Result<void> Server::set_parameter(std::string_view name, Value value)
{
    if (!fImpl) {
        return std::unexpected(Error::InvalidState);
    }
    Parameter* parameter = find_parameter(fImpl->parameters, name);
    if (parameter == nullptr) {
        return std::unexpected(Error::NotFound);
    }
    if (!assign(*parameter, std::move(value))) {
        return std::unexpected(Error::InvalidArgument);
    }
    return {};
}

Result<void> Server::set_driver_parameter(std::string_view name, Value value)
{
    if (!fImpl || fImpl->selected == kNoDriver) {
        return std::unexpected(Error::InvalidState);
    }
    Parameter* parameter = find_parameter(fImpl->drivers[fImpl->selected].view.parameters, name);
    if (parameter == nullptr) {
        return std::unexpected(Error::NotFound);
    }
    if (!assign(*parameter, std::move(value))) {
        return std::unexpected(Error::InvalidArgument);
    }
    return {};
}

Result<void> Server::parse_driver_arguments(int argc, char* argv[])
{
    if (!fImpl || fImpl->selected == kNoDriver) {
        return std::unexpected(Error::InvalidState);
    }
    DriverEntry& entry = fImpl->drivers[fImpl->selected];
    jack_driver_desc_t& desc = *entry.descriptor;
    std::vector<Parameter>& parameters = entry.view.parameters;

    if (argc <= 1) {
        return {};
    }

    if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
        if (argc > 2) {
            for (uint32_t i = 0; i < desc.nparams; i++) {
                if (strcmp(desc.params[i].name, argv[2]) == 0) {
                    jack_print_driver_param_usage(&desc, i, stdout);
                    return std::unexpected(Error::InvalidArgument);
                }
            }
            fprintf(stderr, "Jackd: unknown option '%s' for driver '%s'\n", argv[2], desc.name);
        }
        jack_log("Parameters for driver '%s' (all parameters are optional):", desc.name);
        jack_print_driver_options(&desc, stdout);
        return std::unexpected(Error::InvalidArgument);
    }

    // The getopt string and the long options, one per parameter of the driver.
    std::vector<char> options(parameters.size() * 3 + 1, '\0');
    std::vector<option> long_options(parameters.size() + 1, option{});
    char* options_ptr = options.data();
    for (std::size_t i = 0; i < parameters.size(); i++) {
        jackmini::FormatCString(std::span<char>(options_ptr, 4), "{}::", parameters[i].short_name);
        options_ptr += 3;
        long_options[i].name = desc.params[i].name;
        long_options[i].flag = NULL;
        long_options[i].val = parameters[i].short_name;
        long_options[i].has_arg = optional_argument;
    }

    optind = 0;
    opterr = 0;
    int opt = 0;
    while ((opt = getopt_long(argc, argv, options.data(), long_options.data(), NULL)) != -1) {
        if (opt == ':' || opt == '?') {
            if (opt == ':') {
                fprintf(stderr, "Missing option to argument '%c'\n", optopt);
            } else {
                fprintf(stderr, "Unknownage with option '%c'\n", optopt);
            }
            fprintf(stderr, "Options for driver '%s':\n", desc.name);
            jack_print_driver_options(&desc, stderr);
            return std::unexpected(Error::InvalidArgument);
        }

        Parameter* parameter = nullptr;
        for (Parameter& candidate : parameters) {
            if (candidate.short_name == opt) {
                parameter = &candidate;
                break;
            }
        }
        if (parameter == nullptr) {
            continue;
        }

        if (!optarg && optind < argc && strlen(argv[optind]) && argv[optind][0] != '-') {
            optarg = argv[optind];
        }

        if (optarg) {
            switch (parameter->value.index()) {
            case kInt:
                assign(*parameter, static_cast<std::int64_t>(jackmini::ParseLeadingInteger<int>(optarg)));
                break;
            case kUInt:
                assign(*parameter, static_cast<std::uint64_t>(jackmini::ParseLeadingInteger<unsigned int>(optarg)));
                break;
            case kChar:
                assign(*parameter, optarg[0]);
                break;
            case kString:
                assign(*parameter, std::string(optarg).substr(0, JACK_DRIVER_PARAM_STRING_MAX));
                break;
            default: {
                const bool off = strcasecmp("false", optarg) == 0 || strcasecmp("off", optarg) == 0 ||
                                 strcasecmp("no", optarg) == 0 || strcasecmp("0", optarg) == 0 ||
                                 strcasecmp("(null)", optarg) == 0;
                assign(*parameter, !off);
                break;
            }
            }
        } else if (parameter->value.index() == kBool) {
            assign(*parameter, true);
        } else {
            parameter->value = parameter->default_value;
            parameter->is_set = true;
        }
    }
    return {};
}

Result<void> Server::start()
{
    if (!fImpl) {
        return std::unexpected(Error::InvalidState);
    }
    Impl& impl = *fImpl;
    if (impl.running || impl.engine) {
        return std::unexpected(Error::InvalidState);
    }
    if (impl.selected == kNoDriver) {
        jack_error("no master driver is selected");
        return std::unexpected(Error::InvalidState);
    }
    DriverEntry& master = impl.drivers[impl.selected];

    const std::string& name = impl.name;
    auto lock = AcquireServerLock(jack_server_dir);
    if (!lock) {
        if (lock.error() == std::errc::file_exists) {
            jack_error("`%s' server already active", name.c_str());
            return std::unexpected(Error::InvalidState);
        }
        jack_error("Cannot lock `%s' server (%s)", name.c_str(), lock.error().message().c_str());
        return std::unexpected(Error::ServerFailure);
    }
    jack_log("Server `%s' locked", name.c_str());

    // Clean up files from any previous instance of this server name.
    JackTools::CleanupFiles(name.c_str());

    auto fail = [&impl, &name](Error error) {
        jack_log("Cleaning up files");
        JackTools::CleanupFiles(name.c_str());
        jack_log("Releasing lock of server `%s'", name.c_str());
        ReleaseServerLock();
        impl.engine.reset();
        return std::unexpected(error);
    };

    const bool realtime = std::get<bool>(find_parameter(impl.parameters, "realtime")->value);
    std::int64_t client_timeout = std::get<std::int64_t>(find_parameter(impl.parameters, "client-timeout")->value);
    const std::uint64_t port_max = std::get<std::uint64_t>(find_parameter(impl.parameters, "port-max")->value);
    if (!realtime && client_timeout == 0) {
        client_timeout = 500;  // 0.5 sec; usable when non realtime.
    }
    if (port_max > PORT_NUM_MAX) {
        jack_error("Jack server started with too much ports %llu (when port max can be %d)",
                   static_cast<unsigned long long>(port_max), PORT_NUM_MAX);
        return fail(Error::InvalidArgument);
    }

    try {
        impl.engine = std::make_unique<JackServer>(
            std::get<bool>(find_parameter(impl.parameters, "sync")->value),
            std::get<bool>(find_parameter(impl.parameters, "temporary")->value),
            static_cast<int>(client_timeout),
            realtime,
            static_cast<int>(std::get<std::int64_t>(find_parameter(impl.parameters, "realtime-priority")->value)),
            static_cast<int>(port_max),
            std::get<bool>(find_parameter(impl.parameters, "verbose")->value),
            static_cast<jack_timer_type_t>(std::get<std::uint64_t>(find_parameter(impl.parameters, "clock-source")->value)),
            std::get<char>(find_parameter(impl.parameters, "self-connect-mode")->value),
            name.c_str());
    } catch (std::exception&) {
        jack_error("Failed to create new JackServer object");
        return fail(Error::ServerFailure);
    }

    Result<JSList*> params = create_param_list(master.view.parameters);
    if (!params) {
        return fail(params.error());
    }
    auto opened = impl.engine->Open(master.descriptor.get(), *params);
    destroy_param_list(*params);
    if (!opened) {
        jack_error("JackServer::Open failed");
        return fail(opened.error());
    }

    const bool freewheel = std::get<bool>(find_parameter(impl.parameters, "freewheel")->value);
    auto started = impl.engine->Start(freewheel);
    if (!started) {
        jack_error("JackServer::Start() failed");
        impl.engine->Close();
        return fail(started.error());
    }

    impl.running = true;
    return {};
}

Result<void> Server::stop()
{
    if (!fImpl || !fImpl->engine) {
        return std::unexpected(Error::InvalidState);
    }
    fImpl->engine->Stop();
    fImpl->close_engine();
    return {};
}

SignalSet setup_signals()
{
    // Ensure that this process has its own process group, so that kill(SIG, -pgrp) reaches it.
    setsid();

    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

    // The signals are blocked here, and every thread created later inherits the mask.  The thread that waits in
    // wait_for_signal() is the one that receives them, so the shutdown can use any function.
    SignalSet set{};
    sigemptyset(&set.signals);
    sigaddset(&set.signals, SIGHUP);
    sigaddset(&set.signals, SIGINT);
    sigaddset(&set.signals, SIGQUIT);
    sigaddset(&set.signals, SIGPIPE);
    sigaddset(&set.signals, SIGTERM);
    sigaddset(&set.signals, SIGUSR1);
    sigaddset(&set.signals, SIGUSR2);
    pthread_sigmask(SIG_BLOCK, &set.signals, nullptr);

    // A handler that does nothing: the signals must have one before sigwait() is entered.
    struct sigaction action {};
    action.sa_handler = [](int) {};
    sigfillset(&action.sa_mask);
    action.sa_flags = SA_RESTART | SA_RESETHAND;
    for (int i = 1; i < NSIG; i++) {
        if (sigismember(&set.signals, i)) {
            sigaction(i, &action, nullptr);
        }
    }
    return set;
}

void wait_for_signal(const SignalSet& set)
{
    int sig = 0;
    for (;;) {
        sigwait(&set.signals, &sig);
        fprintf(stderr, "Jack main caught signal %d\n", sig);
        if (sig != SIGUSR1 && sig != SIGTTOU) {
            break;
        }
    }

    if (sig != SIGSEGV) {
        // Unblock the signals so that a fault during the shutdown is seen.
        sigprocmask(SIG_UNBLOCK, &set.signals, nullptr);
    }
}

} // namespace jackmini::control

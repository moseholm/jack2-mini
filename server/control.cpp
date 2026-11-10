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

#include "jackmini/control.hpp"

#include "JackCString.h"
#include "JackConstants.h"
#include "JackDriverInfo.h"
#include "JackDriverLoader.h"
#include "JackError.h"
#include "JackServer.h"
#include "JackServerGlobals.h"
#include "JackServerLock.h"
#include "JackThread.h"
#include "JackTools.h"
#include "jslist.h"
#include "driver_interface.h"

#include <array>
#include <cstdint>
#include <getopt.h>
#include <pthread.h>
#include <signal.h>
#include <strings.h>
#include <unistd.h>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <span>
#include <system_error>
#include <utility>

// The choices of the self connect mode.  The engine relies on these characters.
struct SelfConnectChoice {
    char m_mode;
    const char* m_description;
};

static constexpr std::array<SelfConnectChoice, 5> kSelfConnectChoices = {{
    { ' ', "Don't restrict self connect requests" },
    { 'E', "Fail self connect requests to external ports only" },
    { 'e', "Ignore self connect requests to external ports only" },
    { 'A', "Fail all self connect requests" },
    { 'a', "Ignore all self connect requests" },
}};

const char* getSelfConnectModeDescription(char mode)
{
    for (const SelfConnectChoice& choice : kSelfConnectChoices) {
        if (choice.m_mode == mode) {
            return choice.m_description;
        }
    }
    return nullptr;
}

namespace jackmini::control {

namespace {

// The index of each alternative of Value.  The C descriptors use the same five kinds.
constexpr std::size_t kInt = 0;
constexpr std::size_t kUInt = 1;
constexpr std::size_t kBool = 2;
constexpr std::size_t kChar = 3;
constexpr std::size_t kString = 4;

constexpr std::size_t kNoDriver = static_cast<std::size_t>(-1);

struct ConstraintDeleter {
    void operator()(DriverParamConstraintDesc* constraint) const noexcept
    {
        jack_constraint_free(constraint);
    }
};
using ConstraintPtr = std::unique_ptr<DriverParamConstraintDesc, ConstraintDeleter>;

struct DescriptorDeleter {
    void operator()(DriverDesc* desc) const noexcept
    {
        if (desc != nullptr) {
            free(desc->m_params);
            free(desc);
        }
    }
};
using DescriptorPtr = std::unique_ptr<DriverDesc, DescriptorDeleter>;

Value valueFromC(std::size_t kind, const DriverParamValue& value)
{
    switch (kind) {
    case kInt:
        return static_cast<std::int64_t>(value.m_i);
    case kUInt:
        return static_cast<std::uint64_t>(value.m_ui);
    case kBool:
        return value.m_i != 0;
    case kChar:
        return value.m_c;
    default:
        return std::string(value.m_str.data());
    }
}

DriverParamValue valueToC(const Value& value)
{
    DriverParamValue out { };
    switch (value.index()) {
    case kInt:
        out.m_i = static_cast<std::int32_t>(std::get<std::int64_t>(value));
        break;
    case kUInt:
        out.m_ui = static_cast<std::uint32_t>(std::get<std::uint64_t>(value));
        break;
    case kBool:
        out.m_i = std::get<bool>(value) ? 1 : 0;
        break;
    case kChar:
        out.m_c = std::get<char>(value);
        break;
    default: {
        const std::string& text = std::get<std::string>(value);
        strncpy(out.m_str.data(), text.c_str(), kDriverParamStringMax);
        break;
    }
    }
    return out;
}

std::size_t kindOf(DriverParamType type)
{
    switch (type) {
    case kDriverParamInt:
        return kInt;
    case kDriverParamUInt:
        return kUInt;
    case kDriverParamChar:
        return kChar;
    case kDriverParamString:
        return kString;
    default:
        return kBool;
    }
}

// A value of the same kind as the parameter, or nothing when the kind does not match.
bool assign(Parameter& parameter, Value value)
{
    if (value.index() != parameter.m_defaultValue.index()) {
        jack_error("parameter '%s' takes a different kind of value", parameter.m_name.c_str());
        return false;
    }
    parameter.m_value = std::move(value);
    parameter.m_isSet = true;
    return true;
}

Parameter* findParameter(std::vector<Parameter>& parameters, std::string_view name)
{
    for (Parameter& parameter : parameters) {
        if (parameter.m_name == name) {
            return &parameter;
        }
    }
    return nullptr;
}

const Parameter* findParameter(const std::vector<Parameter>& parameters, std::string_view name)
{
    for (const Parameter& parameter : parameters) {
        if (parameter.m_name == name) {
            return &parameter;
        }
    }
    return nullptr;
}

std::vector<Parameter> serverParameters(const std::string& name)
{
    std::vector<Parameter> parameters;
    auto add = [&parameters](std::string parameterName, std::string description, Value value) {
        Parameter parameter;
        parameter.m_name = std::move(parameterName);
        parameter.m_description = std::move(description);
        parameter.m_value = value;
        parameter.m_defaultValue = std::move(value);
        parameters.push_back(std::move(parameter));
    };

    add("name", "Server name to use.", name);
    add("realtime", "Whether to use realtime mode.", true);
    add("realtime-priority", "Scheduler priority when running in realtime mode.", std::int64_t { 10 });
    add("temporary", "Exit once all clients have closed their connections.", false);
    add("freewheel", "Start in freewheel mode: cycles run as fast as possible, without the sound card.", false);
    add("verbose", "Verbose mode.", false);
    add("client-timeout", "Client timeout limit in milliseconds.", std::int64_t { 0 });
    add("clock-source", "Clocksource type : c(ycle) | h(pet) | s(ystem).", std::uint64_t { 0 });
    add("port-max", "Maximum number of ports.", static_cast<std::uint64_t>(kPortNum));
    add("sync", "Use server synchronous mode.", false);
    add("self-connect-mode", "Whether JACK clients are allowed to connect their own ports",
        Value { static_cast<char>(kDefaultSelfConnectMode) });

    Parameter& mode = parameters.back();
    for (const SelfConnectChoice& choice : kSelfConnectChoices) {
        mode.m_choices.push_back(Choice { Value { choice.m_mode }, choice.m_description });
    }
    return parameters;
}

// One driver: its descriptor, the constraints the descriptor points to, and what the API shows.
struct DriverEntry {
    DescriptorPtr m_descriptor;
    std::vector<ConstraintPtr> m_constraints;
    Driver m_view;
};

std::optional<DriverEntry> makeDriver(DescriptorPtr descriptor)
{
    DriverEntry entry;
    entry.m_descriptor = std::move(descriptor);
    DriverDesc& desc = *entry.m_descriptor;

    entry.m_view.m_name = desc.m_name.data();
    entry.m_view.m_description = desc.m_desc.data();
    entry.m_view.m_master = desc.m_type == kDriverMaster;

    for (std::uint32_t i = 0; i < desc.m_nparams; i++) {
        const DriverParamDesc& descriptorParam = desc.m_params[i];
        const std::size_t kind = kindOf(descriptorParam.m_type);

        Parameter parameter;
        parameter.m_name = descriptorParam.m_name.data();
        parameter.m_shortName = descriptorParam.m_character;
        parameter.m_description = descriptorParam.m_shortDesc.data();
        parameter.m_value = valueFromC(kind, descriptorParam.m_value);
        parameter.m_defaultValue = parameter.m_value;

        DriverParamConstraintDesc* constraint = descriptorParam.m_constraint;
        if (constraint != nullptr) {
            entry.m_constraints.emplace_back(constraint);
            const bool isRange = (constraint->m_flags & kConstraintFlagRange) != 0;
            if (!isRange) {
                for (std::uint32_t c = 0; c < constraint->m_constraint.m_enumeration.m_count; c++) {
                    const DriverParamValueEnum& possible = constraint->m_constraint.m_enumeration.m_possibleValuesArray[c];
                    parameter.m_choices.push_back(Choice { valueFromC(kind, possible.m_value),
                        possible.m_shortDesc.data() });
                }
            }
        }
        entry.m_view.m_parameters.push_back(std::move(parameter));
    }
    return entry;
}

bool loadDrivers(std::vector<DriverEntry>& drivers)
{
    JSListC* list = driversLoad(nullptr);
    if (list == nullptr) {
        jack_error("Could not find any drivers in driver directory!");
        return false;
    }

    while (list != nullptr) {
        JSListC* node = list;
        list = list->m_next;

        DescriptorPtr descriptor(static_cast<DriverDesc*>(node->m_data));
        free(node);

        std::optional<DriverEntry> entry = makeDriver(std::move(descriptor));
        if (entry) {
            drivers.push_back(std::move(*entry));
        }
    }
    return true;
}

// The options the engine receives for a driver: the ones that were set.
Result<JSListC*> createParamList(const std::vector<Parameter>& parameters)
{
    JSListC* list = nullptr;
    for (const Parameter& parameter : parameters) {
        if (!parameter.m_isSet) {
            continue;
        }
        auto* param = static_cast<DriverParam*>(malloc(sizeof(DriverParam)));
        if (param == nullptr) {
            jack_error("Allocation of jack_driver_param_t structure failed");
            for (JSListC* node = list; node != nullptr;) {
                JSListC* next = node->m_next;
                free(node->m_data);
                free(node);
                node = next;
            }
            return std::unexpected(Error::kServerFailure);
        }
        param->m_character = parameter.m_shortName;
        param->m_value = valueToC(parameter.m_value);
        list = jack_slist_append(list, param);
    }
    return list;
}

void destroyParamList(JSListC* list)
{
    while (list != nullptr) {
        JSListC* next = list->m_next;
        free(list->m_data);
        free(list);
        list = next;
    }
}

} // namespace

struct Server::Impl {
    DeviceHooks m_hooks;
    std::string m_name;
    std::vector<Parameter> m_parameters;
    std::vector<DriverEntry> m_drivers;
    std::size_t m_selected = kNoDriver;
    std::unique_ptr<ServerCore> m_engine;
    bool m_running = false;

    // Closes the engine, cleans up its files, and releases the server name.
    void closeEngine()
    {
        m_running = false;
        m_engine->close();
        m_engine.reset();

        jack_log("Cleaning up files");
        Tools::cleanupFiles(m_name.c_str());
        jack_log("Releasing lock of server `%s'", m_name.c_str());
        releaseServerLock();
    }
};

Result<Server> Server::create(std::string_view name, DeviceHooks hooks)
{
    Server server;
    server.m_impl = std::make_unique<Impl>();
    Impl& impl = *server.m_impl;

    impl.m_name = name.empty() ? std::string(Tools::defaultServerName()) : std::string(name);
    impl.m_parameters = serverParameters(impl.m_name);
    impl.m_hooks = hooks;

    ServerGlobals::s_onDeviceAcquire = hooks.m_onAcquire;
    ServerGlobals::s_onDeviceRelease = hooks.m_onRelease;
    ServerGlobals::s_onDeviceReservationLoop = hooks.m_onReservationLoop;

    if (!loadDrivers(impl.m_drivers)) {
        return std::unexpected(Error::kNotFound);
    }
    return server;
}

Server::Server(Server&& other) noexcept = default;

Server& Server::operator=(Server&& other) noexcept
{
    if (this != &other) {
        if (m_impl && m_impl->m_engine) {
            m_impl->closeEngine();
        }
        m_impl = std::move(other.m_impl);
    }
    return *this;
}

Server::~Server()
{
    if (m_impl && m_impl->m_engine) {
        m_impl->closeEngine();
    }
}

bool Server::isRunning() const noexcept
{
    return m_impl && m_impl->m_running;
}

std::vector<Driver> Server::drivers() const
{
    std::vector<Driver> views;
    if (m_impl) {
        for (const DriverEntry& entry : m_impl->m_drivers) {
            views.push_back(entry.m_view);
        }
    }
    return views;
}

Result<void> Server::selectDriver(std::string_view driverName)
{
    if (!m_impl) {
        return std::unexpected(Error::kInvalidState);
    }
    for (std::size_t i = 0; i < m_impl->m_drivers.size(); i++) {
        if (m_impl->m_drivers[i].m_view.m_name != driverName) {
            continue;
        }
        if (!m_impl->m_drivers[i].m_view.m_master) {
            jack_error("Driver \"%s\" is not a master", m_impl->m_drivers[i].m_view.m_name.c_str());
            return std::unexpected(Error::kInvalidArgument);
        }
        m_impl->m_selected = i;
        return { };
    }
    jack_error("Unknown driver \"%.*s\"", static_cast<std::int32_t>(driverName.size()), driverName.data());
    return std::unexpected(Error::kNotFound);
}

std::vector<Parameter> Server::parameters() const
{
    return m_impl ? m_impl->m_parameters : std::vector<Parameter> { };
}

Result<void> Server::setParameter(std::string_view name, Value value)
{
    if (!m_impl) {
        return std::unexpected(Error::kInvalidState);
    }
    Parameter* parameter = findParameter(m_impl->m_parameters, name);
    if (parameter == nullptr) {
        return std::unexpected(Error::kNotFound);
    }
    if (!assign(*parameter, std::move(value))) {
        return std::unexpected(Error::kInvalidArgument);
    }
    // The name the server starts with is the one set here (jackd -n), not only the parameter's copy of it.
    if (name == "name") {
        const std::string& serverName = std::get<std::string>(parameter->m_value);
        m_impl->m_name = serverName.empty() ? std::string(Tools::defaultServerName()) : serverName;
    }
    return { };
}

Result<void> Server::setDriverParameter(std::string_view name, Value value)
{
    if (!m_impl || m_impl->m_selected == kNoDriver) {
        return std::unexpected(Error::kInvalidState);
    }
    Parameter* parameter = findParameter(m_impl->m_drivers[m_impl->m_selected].m_view.m_parameters, name);
    if (parameter == nullptr) {
        return std::unexpected(Error::kNotFound);
    }
    if (!assign(*parameter, std::move(value))) {
        return std::unexpected(Error::kInvalidArgument);
    }
    return { };
}

Result<void> Server::parseDriverArguments(std::int32_t argc, char* argv[])
{
    if (!m_impl || m_impl->m_selected == kNoDriver) {
        return std::unexpected(Error::kInvalidState);
    }
    DriverEntry& entry = m_impl->m_drivers[m_impl->m_selected];
    DriverDesc& desc = *entry.m_descriptor;
    std::vector<Parameter>& parameters = entry.m_view.m_parameters;

    if (argc <= 1) {
        return { };
    }

    if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
        if (argc > 2) {
            for (std::uint32_t i = 0; i < desc.m_nparams; i++) {
                if (strcmp(desc.m_params[i].m_name.data(), argv[2]) == 0) {
                    printDriverParamUsage(&desc, i, stdout);
                    return std::unexpected(Error::kInvalidArgument);
                }
            }
            fprintf(stderr, "Jackd: unknown option '%s' for driver '%s'\n", argv[2], desc.m_name);
        }
        jack_log("Parameters for driver '%s' (all parameters are optional):", desc.m_name);
        printDriverOptions(&desc, stdout);
        return std::unexpected(Error::kInvalidArgument);
    }

    // The getopt string and the long options, one per parameter of the driver.
    std::vector<char> options(parameters.size() * 3 + 1, '\0');
    std::vector<option> longOptions(parameters.size() + 1, option { });
    char* optionsPtr = options.data();
    for (std::size_t i = 0; i < parameters.size(); i++) {
        jackmini::formatCString(std::span<char>(optionsPtr, 4), "{}::", parameters[i].m_shortName);
        optionsPtr += 3;
        longOptions[i].name = desc.m_params[i].m_name.data();
        longOptions[i].flag = nullptr;
        longOptions[i].val = parameters[i].m_shortName;
        longOptions[i].has_arg = optional_argument;
    }

    optind = 0;
    opterr = 0;
    std::int32_t opt = 0;
    while ((opt = getopt_long(argc, argv, options.data(), longOptions.data(), nullptr)) != -1) {
        if (opt == ':' || opt == '?') {
            if (opt == ':') {
                fprintf(stderr, "Missing option to argument '%c'\n", optopt);
            } else {
                fprintf(stderr, "Unknownage with option '%c'\n", optopt);
            }
            fprintf(stderr, "Options for driver '%s':\n", desc.m_name);
            printDriverOptions(&desc, stderr);
            return std::unexpected(Error::kInvalidArgument);
        }

        Parameter* parameter = nullptr;
        for (Parameter& candidate : parameters) {
            if (candidate.m_shortName == opt) {
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
            switch (parameter->m_value.index()) {
            case kInt:
                assign(*parameter, static_cast<std::int64_t>(jackmini::parseLeadingInteger<std::int32_t>(optarg)));
                break;
            case kUInt:
                assign(*parameter, static_cast<std::uint64_t>(jackmini::parseLeadingInteger<std::uint32_t>(optarg)));
                break;
            case kChar:
                assign(*parameter, optarg[0]);
                break;
            case kString:
                assign(*parameter, std::string(optarg).substr(0, kDriverParamStringMax));
                break;
            default: {
                const bool off = strcasecmp("false", optarg) == 0 || strcasecmp("off", optarg) == 0 || strcasecmp("no", optarg) == 0 || strcasecmp("0", optarg) == 0 || strcasecmp("(null)", optarg) == 0;
                assign(*parameter, !off);
                break;
            }
            }
        } else if (parameter->m_value.index() == kBool) {
            assign(*parameter, true);
        } else {
            parameter->m_value = parameter->m_defaultValue;
            parameter->m_isSet = true;
        }
    }
    return { };
}

Result<void> Server::start()
{
    if (!m_impl) {
        return std::unexpected(Error::kInvalidState);
    }
    Impl& impl = *m_impl;
    if (impl.m_running || impl.m_engine) {
        return std::unexpected(Error::kInvalidState);
    }
    if (impl.m_selected == kNoDriver) {
        jack_error("no master driver is selected");
        return std::unexpected(Error::kInvalidState);
    }
    DriverEntry& master = impl.m_drivers[impl.m_selected];

    const std::string& name = impl.m_name;
    auto lock = acquireServerLock(kServerDir);
    if (!lock) {
        if (lock.error() == std::errc::file_exists) {
            jack_error("`%s' server already active", name.c_str());
            return std::unexpected(Error::kInvalidState);
        }
        jack_error("Cannot lock `%s' server (%s)", name.c_str(), lock.error().message().c_str());
        return std::unexpected(Error::kServerFailure);
    }
    jack_log("Server `%s' locked", name.c_str());

    // Clean up files from any previous instance of this server name.
    Tools::cleanupFiles(name.c_str());

    auto fail = [&impl, &name](Error error) {
        jack_log("Cleaning up files");
        Tools::cleanupFiles(name.c_str());
        jack_log("Releasing lock of server `%s'", name.c_str());
        releaseServerLock();
        impl.m_engine.reset();
        return std::unexpected(error);
    };

    const bool realtime = std::get<bool>(findParameter(impl.m_parameters, "realtime")->m_value);
    std::int64_t clientTimeout = std::get<std::int64_t>(findParameter(impl.m_parameters, "client-timeout")->m_value);
    const std::uint64_t portMax = std::get<std::uint64_t>(findParameter(impl.m_parameters, "port-max")->m_value);
    if (!realtime && clientTimeout == 0) {
        clientTimeout = 500; // 0.5 sec; usable when non realtime.
    }
    if (portMax > kPortNumMax) {
        jack_error("Jack server started with too much ports %llu (when port max can be %d)",
            static_cast<std::uint64_t>(portMax), kPortNumMax);
        return fail(Error::kInvalidArgument);
    }

    try {
        impl.m_engine = std::make_unique<ServerCore>(
            std::get<bool>(findParameter(impl.m_parameters, "sync")->m_value),
            std::get<bool>(findParameter(impl.m_parameters, "temporary")->m_value),
            static_cast<std::int32_t>(clientTimeout),
            realtime,
            static_cast<std::int32_t>(std::get<std::int64_t>(findParameter(impl.m_parameters, "realtime-priority")->m_value)),
            static_cast<std::int32_t>(portMax),
            std::get<bool>(findParameter(impl.m_parameters, "verbose")->m_value),
            static_cast<TimerType>(std::get<std::uint64_t>(findParameter(impl.m_parameters, "clock-source")->m_value)),
            std::get<char>(findParameter(impl.m_parameters, "self-connect-mode")->m_value),
            name.c_str());
    } catch (std::exception&) {
        jack_error("Failed to create new JackServer object");
        return fail(Error::kServerFailure);
    }

    Result<JSListC*> params = createParamList(master.m_view.m_parameters);
    if (!params) {
        return fail(params.error());
    }
    auto opened = impl.m_engine->open(master.m_descriptor.get(), *params);
    destroyParamList(*params);
    if (!opened) {
        jack_error("JackServer::Open failed");
        return fail(opened.error());
    }

    const bool freewheel = std::get<bool>(findParameter(impl.m_parameters, "freewheel")->m_value);
    auto started = impl.m_engine->start(freewheel);
    if (!started) {
        jack_error("JackServer::Start() failed");
        impl.m_engine->close();
        return fail(started.error());
    }

    impl.m_running = true;
    return { };
}

Result<void> Server::stop()
{
    if (!m_impl || !m_impl->m_engine) {
        return std::unexpected(Error::kInvalidState);
    }
    m_impl->m_engine->stop();
    m_impl->closeEngine();
    return { };
}

SignalSet setupSignals()
{
    // Ensure that this process has its own process group, so that kill(SIG, -pgrp) reaches it.
    setsid();

    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, nullptr);

    // The signals are blocked here, and every thread created later inherits the mask.  The thread that waits in
    // wait_for_signal() is the one that receives them, so the shutdown can use any function.
    SignalSet set { };
    sigemptyset(&set.m_signals);
    sigaddset(&set.m_signals, SIGHUP);
    sigaddset(&set.m_signals, SIGINT);
    sigaddset(&set.m_signals, SIGQUIT);
    sigaddset(&set.m_signals, SIGPIPE);
    sigaddset(&set.m_signals, SIGTERM);
    sigaddset(&set.m_signals, SIGUSR1);
    sigaddset(&set.m_signals, SIGUSR2);
    pthread_sigmask(SIG_BLOCK, &set.m_signals, nullptr);

    // A handler that does nothing: the signals must have one before sigwait() is entered.
    struct sigaction action { };
    action.sa_handler = [](std::int32_t) { };
    sigfillset(&action.sa_mask);
    action.sa_flags = SA_RESTART | SA_RESETHAND;
    for (std::int32_t i = 1; i < NSIG; i++) {
        if (sigismember(&set.m_signals, i)) {
            sigaction(i, &action, nullptr);
        }
    }
    return set;
}

void waitForSignal(const SignalSet& set)
{
    std::int32_t sig = 0;
    for (;;) {
        sigwait(&set.m_signals, &sig);
        fprintf(stderr, "Jack main caught signal %d\n", sig);
        if (sig != SIGUSR1 && sig != SIGTTOU) {
            break;
        }
    }

    if (sig != SIGSEGV) {
        // Unblock the signals so that a fault during the shutdown is seen.
        sigprocmask(SIG_UNBLOCK, &set.m_signals, nullptr);
    }
}

} // namespace jackmini::control

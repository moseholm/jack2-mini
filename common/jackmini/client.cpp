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

// The client lifecycle, the callbacks, and the identity of a client (client-api.md, lifecycle).
//
// This is the first area of the client library moved behind the jackmini headers.  It is built on the C library
// (jack/jack.h), which is still the implementation underneath; the C functions are called from here and nowhere
// else in the new API.  Ports, connections, buffers, and transport are not ported yet: their declarations are in the
// headers, and calling them is a link error until they are.

#include "jackmini/client.hpp"

#include "jack/jack.h"
#include "jack/uuid.h"

#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace jackmini
{

struct Client::Impl {
    jack_client_t* handle = nullptr;
    bool active = false;

    // Stored while inactive; the trampolines call them from the realtime thread.
    ProcessFn process;
    XrunFn xrun;
    ShutdownFn shutdown;
    BufferSizeFn buffer_size;
    SampleRateFn sample_rate;

    // The ports this client has handed out, by id.  The id is the index; index 0 is no port.  Ports of other clients get
    // an id too, when find_port returns them.  A port that is unregistered keeps its id, with no port behind it.
    std::vector<jack_port_t*> ports{ nullptr };
    std::unordered_map<jack_port_t*, std::uint32_t> ids;

    std::uint32_t id_of(jack_port_t* port)
    {
        if (port == nullptr) {
            return 0;
        }
        const auto found = ids.find(port);
        if (found != ids.end()) {
            return found->second;
        }
        const auto id = static_cast<std::uint32_t>(ports.size());
        ports.push_back(port);
        ids.emplace(port, id);
        return id;
    }

    jack_port_t* port_of(Port port) const
    {
        return (port.id() > 0 && port.id() < ports.size()) ? ports[port.id()] : nullptr;
    }

    void forget(jack_port_t* port)
    {
        ids.erase(port);
        for (auto& entry : ports) {
            if (entry == port) {
                entry = nullptr;
            }
        }
    }
};

namespace
{

unsigned long to_c_flags(PortFlags flags)
{
    unsigned long result = 0;
    if (has_flag(flags, PortFlags::Input)) {
        result |= JackPortIsInput;
    }
    if (has_flag(flags, PortFlags::Output)) {
        result |= JackPortIsOutput;
    }
    if (has_flag(flags, PortFlags::Physical)) {
        result |= JackPortIsPhysical;
    }
    if (has_flag(flags, PortFlags::CanMonitor)) {
        result |= JackPortCanMonitor;
    }
    if (has_flag(flags, PortFlags::Terminal)) {
        result |= JackPortIsTerminal;
    }
    return result;
}

PortFlags from_c_flags(int flags)
{
    PortFlags result = PortFlags::None;
    if (flags & JackPortIsInput) {
        result = result | PortFlags::Input;
    }
    if (flags & JackPortIsOutput) {
        result = result | PortFlags::Output;
    }
    if (flags & JackPortIsPhysical) {
        result = result | PortFlags::Physical;
    }
    if (flags & JackPortCanMonitor) {
        result = result | PortFlags::CanMonitor;
    }
    if (flags & JackPortIsTerminal) {
        result = result | PortFlags::Terminal;
    }
    return result;
}

const char* to_c_type(PortType type)
{
    return type == PortType::Midi ? JACK_DEFAULT_MIDI_TYPE : JACK_DEFAULT_AUDIO_TYPE;
}

// Takes the strings the C library allocated for a name list, and frees them.
std::vector<std::string> take_names(const char** list)
{
    std::vector<std::string> names;
    if (list == nullptr) {
        return names;
    }
    for (const char** entry = list; *entry != nullptr; entry++) {
        names.emplace_back(*entry);
    }
    jack_free(list);
    return names;
}

Error to_error(jack_status_t status)
{
    if (status & JackNameNotUnique) {
        return Error::NameTaken;
    }
    if (status & JackVersionError) {
        return Error::Protocol;
    }
    if (status & JackInvalidOption) {
        return Error::InvalidArgument;
    }
    if (status & JackServerFailed) {
        return Error::NoServer;
    }
    if (status & JackServerError) {
        return Error::Io;
    }
    return Error::ServerFailure;
}

} // namespace

// The C callbacks.  They call what was stored while the client was inactive; ClientBridge is the friend of Client that
// can reach the stored callables.
struct ClientBridge {
    static int process(jack_nframes_t frames, void* arg)
    {
        auto* impl = static_cast<Client::Impl*>(arg);
        if (!impl->process) {
            return 0;
        }
        return impl->process(frames) ? 0 : -1;
    }

    static int xrun(void* arg)
    {
        auto* impl = static_cast<Client::Impl*>(arg);
        if (impl->xrun) {
            impl->xrun();
        }
        return 0;
    }

    static void shutdown(void* arg)
    {
        auto* impl = static_cast<Client::Impl*>(arg);
        if (impl->shutdown) {
            impl->shutdown();
        }
    }

    static int buffer_size(jack_nframes_t frames, void* arg)
    {
        auto* impl = static_cast<Client::Impl*>(arg);
        if (!impl->buffer_size) {
            return 0;
        }
        return impl->buffer_size(frames) ? 0 : -1;
    }

    static int sample_rate(jack_nframes_t rate, void* arg)
    {
        auto* impl = static_cast<Client::Impl*>(arg);
        if (!impl->sample_rate) {
            return 0;
        }
        return impl->sample_rate(rate) ? 0 : -1;
    }
};

Result<Client> Client::open(std::string_view name, OpenOptions options)
{
    int bits = 0;
    if (!options.start_server) {
        bits |= JackNoStartServer;
    }
    if (options.exact_name) {
        bits |= JackUseExactName;
    }
    if (!options.server_name.empty()) {
        bits |= JackServerName;
    }

    const std::string client_name(name);
    jack_status_t status = static_cast<jack_status_t>(0);
    jack_client_t* handle = options.server_name.empty()
        ? jack_client_open(client_name.c_str(), static_cast<jack_options_t>(bits), &status)
        : jack_client_open(client_name.c_str(), static_cast<jack_options_t>(bits), &status, options.server_name.c_str());
    if (handle == nullptr) {
        return std::unexpected(to_error(status));
    }

    Client client;
    client.fImpl = std::make_unique<Impl>();
    client.fImpl->handle = handle;

    // The trampolines are installed once: they call whatever is stored, so the stored callbacks decide what runs.
    jack_set_process_callback(handle, ClientBridge::process, client.fImpl.get());
    jack_set_xrun_callback(handle, ClientBridge::xrun, client.fImpl.get());
    jack_on_shutdown(handle, ClientBridge::shutdown, client.fImpl.get());
    jack_set_buffer_size_callback(handle, ClientBridge::buffer_size, client.fImpl.get());
    jack_set_sample_rate_callback(handle, ClientBridge::sample_rate, client.fImpl.get());
    return client;
}

Client::Client(Client&& other) noexcept = default;

Client& Client::operator=(Client&& other) noexcept
{
    if (this != &other) {
        if (fImpl && fImpl->handle) {
            (void)close();
        }
        fImpl = std::move(other.fImpl);
    }
    return *this;
}

Client::~Client()
{
    if (fImpl && fImpl->handle) {
        (void)close();
    }
}

Result<void> Client::activate()
{
    if (!fImpl || !fImpl->handle || fImpl->active) {
        return std::unexpected(Error::InvalidState);
    }
    if (jack_activate(fImpl->handle) != 0) {
        return std::unexpected(Error::ServerFailure);
    }
    fImpl->active = true;
    return {};
}

Result<void> Client::deactivate()
{
    if (!fImpl || !fImpl->handle || !fImpl->active) {
        return std::unexpected(Error::InvalidState);
    }
    const int rc = jack_deactivate(fImpl->handle);
    fImpl->active = false;
    if (rc != 0) {
        return std::unexpected(Error::ServerFailure);
    }
    return {};
}

Result<void> Client::close()
{
    if (!fImpl || !fImpl->handle) {
        return {};
    }
    if (fImpl->active) {
        (void)deactivate();
    }
    const int rc = jack_client_close(fImpl->handle);
    fImpl->handle = nullptr;
    return (rc == 0) ? Result<void>{} : std::unexpected(Error::ServerFailure);
}

Result<Port> Client::register_port(std::string_view name, PortType type, PortFlags flags)
{
    if (!fImpl || !fImpl->handle) {
        return std::unexpected(Error::InvalidState);
    }
    // The name is the port's short name; the full name is client:name, so the short name has no colon.
    if (name.empty() || name.find(':') != std::string_view::npos) {
        return std::unexpected(Error::InvalidArgument);
    }
    const std::string short_name(name);
    jack_port_t* port = jack_port_register(fImpl->handle, short_name.c_str(), to_c_type(type), to_c_flags(flags), 0);
    if (port == nullptr) {
        const std::string full = std::string(this->name()) + ":" + short_name;
        return std::unexpected(jack_port_by_name(fImpl->handle, full.c_str()) != nullptr ? Error::NameTaken : Error::ServerFailure);
    }
    return Port(fImpl->id_of(port));
}

Result<void> Client::unregister_port(Port port)
{
    if (!fImpl || !fImpl->handle) {
        return std::unexpected(Error::InvalidState);
    }
    jack_port_t* ptr = fImpl->port_of(port);
    if (ptr == nullptr) {
        return std::unexpected(Error::NotFound);
    }
    if (jack_port_unregister(fImpl->handle, ptr) != 0) {
        return std::unexpected(Error::ServerFailure);
    }
    fImpl->forget(ptr);
    return {};
}

// For a port that is not known, the name is empty, the type is audio, and the flags are none: the accessors have no error
// report, so they say nothing rather than fail.
std::string_view Client::port_name(Port port) const
{
    jack_port_t* ptr = fImpl ? fImpl->port_of(port) : nullptr;
    return ptr ? jack_port_name(ptr) : std::string_view();
}

PortType Client::port_type(Port port) const
{
    jack_port_t* ptr = fImpl ? fImpl->port_of(port) : nullptr;
    if (ptr == nullptr) {
        return PortType::Audio;
    }
    return std::strcmp(jack_port_type(ptr), JACK_DEFAULT_MIDI_TYPE) == 0 ? PortType::Midi : PortType::Audio;
}

PortFlags Client::port_flags(Port port) const
{
    jack_port_t* ptr = fImpl ? fImpl->port_of(port) : nullptr;
    return ptr ? from_c_flags(jack_port_flags(ptr)) : PortFlags::None;
}

Result<Port> Client::find_port(std::string_view full_name) const
{
    if (!fImpl || !fImpl->handle) {
        return std::unexpected(Error::InvalidState);
    }
    const std::string text(full_name);
    jack_port_t* ptr = jack_port_by_name(fImpl->handle, text.c_str());
    if (ptr == nullptr) {
        return std::unexpected(Error::NotFound);
    }
    return Port(fImpl->id_of(ptr));
}

std::vector<std::string> Client::port_names(PortType type, PortFlags required) const
{
    if (!fImpl || !fImpl->handle) {
        return {};
    }
    return take_names(jack_get_ports(fImpl->handle, nullptr, to_c_type(type), to_c_flags(required)));
}

Result<void> Client::connect(Port source, Port destination)
{
    if (!fImpl || !fImpl->handle) {
        return std::unexpected(Error::InvalidState);
    }
    jack_port_t* src = fImpl->port_of(source);
    jack_port_t* dst = fImpl->port_of(destination);
    if (src == nullptr || dst == nullptr) {
        return std::unexpected(Error::NotFound);
    }
    // The C library reports a refused connection without a reason, so the reasons are checked here first.
    if (port_type(source) != port_type(destination) ||
        !has_flag(port_flags(source), PortFlags::Output) || !has_flag(port_flags(destination), PortFlags::Input)) {
        return std::unexpected(Error::InvalidArgument);  // different types, or not an output to an input
    }
    if (is_connected(source, destination)) {
        return std::unexpected(Error::InvalidState);
    }
    if (jack_connect(fImpl->handle, jack_port_name(src), jack_port_name(dst)) != 0) {
        return std::unexpected(Error::ServerFailure);
    }
    return {};
}

Result<void> Client::disconnect(Port source, Port destination)
{
    if (!fImpl || !fImpl->handle) {
        return std::unexpected(Error::InvalidState);
    }
    jack_port_t* src = fImpl->port_of(source);
    jack_port_t* dst = fImpl->port_of(destination);
    if (src == nullptr || dst == nullptr || !is_connected(source, destination)) {
        return std::unexpected(Error::NotFound);
    }
    if (jack_disconnect(fImpl->handle, jack_port_name(src), jack_port_name(dst)) != 0) {
        return std::unexpected(Error::ServerFailure);
    }
    return {};
}

bool Client::is_connected(Port source, Port destination) const
{
    jack_port_t* src = fImpl ? fImpl->port_of(source) : nullptr;
    jack_port_t* dst = fImpl ? fImpl->port_of(destination) : nullptr;
    if (src == nullptr || dst == nullptr) {
        return false;
    }
    return jack_port_connected_to(src, jack_port_name(dst)) != 0;
}

std::vector<std::string> Client::connections(Port port) const
{
    jack_port_t* ptr = fImpl ? fImpl->port_of(port) : nullptr;
    if (ptr == nullptr) {
        return {};
    }
    return take_names(jack_port_get_all_connections(fImpl->handle, ptr));
}

bool Client::is_active() const noexcept
{
    return fImpl && fImpl->active;
}

Result<void> Client::set_process(ProcessFn fn)
{
    if (!fImpl || fImpl->active) {
        return std::unexpected(Error::InvalidState);
    }
    fImpl->process = std::move(fn);
    return {};
}

Result<void> Client::on_xrun(XrunFn fn)
{
    if (!fImpl || fImpl->active) {
        return std::unexpected(Error::InvalidState);
    }
    fImpl->xrun = std::move(fn);
    return {};
}

Result<void> Client::on_shutdown(ShutdownFn fn)
{
    if (!fImpl || fImpl->active) {
        return std::unexpected(Error::InvalidState);
    }
    fImpl->shutdown = std::move(fn);
    return {};
}

Result<void> Client::on_buffer_size(BufferSizeFn fn)
{
    if (!fImpl || fImpl->active) {
        return std::unexpected(Error::InvalidState);
    }
    fImpl->buffer_size = std::move(fn);
    return {};
}

Result<void> Client::on_sample_rate(SampleRateFn fn)
{
    if (!fImpl || fImpl->active) {
        return std::unexpected(Error::InvalidState);
    }
    fImpl->sample_rate = std::move(fn);
    return {};
}

std::string_view Client::name() const noexcept
{
    if (!fImpl || !fImpl->handle) {
        return {};
    }
    return jack_get_client_name(fImpl->handle);
}

Uuid Client::uuid() const noexcept
{
    if (!fImpl || !fImpl->handle) {
        return {};
    }
    // The C library returns a string it allocates, which is freed here.
    char* text = jack_get_uuid_for_client_name(fImpl->handle, jack_get_client_name(fImpl->handle));
    if (text == nullptr) {
        return {};
    }
    jack_uuid_t value = 0;
    const bool parsed = jack_uuid_parse(text, &value) == 0;
    jack_free(text);
    return parsed ? Uuid(static_cast<std::uint64_t>(value)) : Uuid();
}

FrameCount Client::sample_rate() const noexcept
{
    return fImpl && fImpl->handle ? static_cast<FrameCount>(jack_get_sample_rate(fImpl->handle)) : 0;
}

FrameCount Client::buffer_size() const noexcept
{
    return fImpl && fImpl->handle ? static_cast<FrameCount>(jack_get_buffer_size(fImpl->handle)) : 0;
}

float Client::cpu_load() const noexcept
{
    return fImpl && fImpl->handle ? jack_cpu_load(fImpl->handle) : 0.0f;
}

} // namespace jackmini

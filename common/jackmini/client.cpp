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

#include <memory>
#include <string>

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
};

namespace
{

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

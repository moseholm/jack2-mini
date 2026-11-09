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


// The public client (client.hpp) over a client::ClientSession. The session does the work; this file keeps the handles
// that the program sees, checks the state of the client, and converts the values.

#include "jackmini/client.hpp"

#include "client_session.hpp"

#include "JackMidiPort.h"
#include "jack/transport.h"

#include <array>
#include <atomic>
#include <cstring>
#include <mutex>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace jackmini {

struct Client::Impl {
    std::unique_ptr<client::ClientSession> session;

    // The handles of the ports this client has handed out, by id: the id is the slot.  Slot 0 is no port.  A slot is
    // written under the lock by the control thread and read without a lock by the realtime thread, so a slot is never
    // reused: a handle to an unregistered port stays invalid.  The table holds kMaxPortHandles; a client that uses more
    // fails.
    static constexpr std::uint32_t kMaxPortHandles = 4096;
    std::array<std::atomic<jack_port_id_t>, kMaxPortHandles> slots { };
    std::uint32_t used = 1;
    // The token of this client: its handles carry it, and a handle with another token is not a port of this client.
    std::uint32_t token = 0;
    std::unordered_map<jack_port_id_t, std::uint32_t> ids;
    std::mutex control;

    // Control thread.  Returns 0 when the table is full.
    std::uint32_t id_of(jack_port_id_t port)
    {
        std::lock_guard lock(control);
        const auto found = ids.find(port);
        if (found != ids.end()) {
            return found->second;
        }
        if (used >= kMaxPortHandles) {
            return 0;
        }
        const std::uint32_t id = used++;
        slots[id].store(port);
        ids.emplace(port, id);
        return id;
    }

    // Realtime thread: no lock.  Returns 0 for a port that is not this client's.
    jack_port_id_t port_of(Port port) const
    {
        const std::uint32_t id = port.id();
        if (port.owner() != token) {
            return 0;
        }
        return (id > 0 && id < kMaxPortHandles) ? slots[id].load() : 0;
    }

    // Control thread.
    void forget(jack_port_id_t port)
    {
        std::lock_guard lock(control);
        const auto found = ids.find(port);
        if (found != ids.end()) {
            slots[found->second].store(0);
            ids.erase(found);
        }
    }
};

namespace {

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

PortFlags from_c_flags(unsigned long flags)
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

jack_latency_callback_mode_t to_c_mode(LatencyMode mode)
{
    return mode == LatencyMode::Capture ? JackCaptureLatency : JackPlaybackLatency;
}

} // namespace

Result<Client> Client::open(std::string_view name, OpenOptions options)
{
    auto session = client::ClientSession::open(name, options);
    if (!session) {
        return std::unexpected(session.error());
    }

    Client client;
    client.fImpl = std::make_unique<Impl>();
    static std::atomic<std::uint32_t> next_token { 1 };
    client.fImpl->token = next_token.fetch_add(1);
    client.fImpl->session = std::move(*session);
    return client;
}

Client::Client(Client&& other) noexcept = default;

Client& Client::operator=(Client&& other) noexcept
{
    if (this != &other) {
        if (fImpl && fImpl->session) {
            (void)close();
        }
        fImpl = std::move(other.fImpl);
    }
    return *this;
}

Client::~Client()
{
    if (fImpl && fImpl->session) {
        (void)close();
    }
}

Result<void> Client::activate()
{
    if (!fImpl || !fImpl->session || fImpl->session->is_active()) {
        return std::unexpected(Error::InvalidState);
    }
    return fImpl->session->activate();
}

Result<void> Client::deactivate()
{
    if (!fImpl || !fImpl->session || !fImpl->session->is_active()) {
        return std::unexpected(Error::InvalidState);
    }
    return fImpl->session->deactivate();
}

Result<void> Client::close()
{
    if (!fImpl || !fImpl->session) {
        return { };
    }
    auto result = fImpl->session->close();
    fImpl->session.reset();
    return result;
}

bool Client::is_active() const noexcept
{
    return fImpl && fImpl->session && fImpl->session->is_active();
}

Result<Port> Client::register_port(std::string_view name, PortType type, PortFlags flags)
{
    if (!fImpl || !fImpl->session) {
        return std::unexpected(Error::InvalidState);
    }
    // The name is the port's short name; the full name is client:name, so the short name has no colon.
    if (name.empty() || name.find(':') != std::string_view::npos) {
        return std::unexpected(Error::InvalidArgument);
    }
    auto index = fImpl->session->register_port(name, to_c_type(type), to_c_flags(flags), 0);
    if (!index) {
        const std::string full = std::string(this->name()) + ":" + std::string(name);
        return std::unexpected(fImpl->session->find_port(full) ? Error::NameTaken : Error::ServerFailure);
    }
    return Port(fImpl->id_of(*index), fImpl->token);
}

Result<void> Client::unregister_port(Port port)
{
    if (!fImpl || !fImpl->session) {
        return std::unexpected(Error::InvalidState);
    }
    const jack_port_id_t index = fImpl->port_of(port);
    if (index == 0) {
        return std::unexpected(Error::NotFound);
    }
    if (auto result = fImpl->session->unregister_port(index); !result) {
        return result;
    }
    fImpl->forget(index);
    return { };
}

// For a port that is not known, the name is empty, the type is audio, and the flags are none: the accessors have no error
// report, so they say nothing rather than fail.
std::string_view Client::port_name(Port port) const
{
    const jack_port_id_t index = fImpl && fImpl->session ? fImpl->port_of(port) : 0;
    return index ? fImpl->session->port_name(index) : std::string_view();
}

PortType Client::port_type(Port port) const
{
    const jack_port_id_t index = fImpl && fImpl->session ? fImpl->port_of(port) : 0;
    if (index == 0) {
        return PortType::Audio;
    }
    return std::strcmp(fImpl->session->port_type(index), JACK_DEFAULT_MIDI_TYPE) == 0 ? PortType::Midi : PortType::Audio;
}

PortFlags Client::port_flags(Port port) const
{
    const jack_port_id_t index = fImpl && fImpl->session ? fImpl->port_of(port) : 0;
    return index ? from_c_flags(fImpl->session->port_flags(index)) : PortFlags::None;
}

Result<Port> Client::find_port(std::string_view full_name) const
{
    if (!fImpl || !fImpl->session) {
        return std::unexpected(Error::InvalidState);
    }
    const auto index = fImpl->session->find_port(full_name);
    if (!index) {
        return std::unexpected(Error::NotFound);
    }
    return Port(fImpl->id_of(*index), fImpl->token);
}

std::vector<std::string> Client::port_names(PortType type, PortFlags required) const
{
    if (!fImpl || !fImpl->session) {
        return { };
    }
    return fImpl->session->port_names(to_c_type(type), to_c_flags(required));
}

Result<void> Client::connect(Port source, Port destination)
{
    if (!fImpl || !fImpl->session) {
        return std::unexpected(Error::InvalidState);
    }
    const jack_port_id_t src = fImpl->port_of(source);
    const jack_port_id_t dst = fImpl->port_of(destination);
    if (src == 0 || dst == 0) {
        return std::unexpected(Error::NotFound);
    }
    // The server refuses a connection without a reason, so the reasons are checked here first.
    if (port_type(source) != port_type(destination) || !has_flag(port_flags(source), PortFlags::Output) || !has_flag(port_flags(destination), PortFlags::Input)) {
        return std::unexpected(Error::InvalidArgument); // different types, or not an output to an input
    }
    if (is_connected(source, destination)) {
        return std::unexpected(Error::InvalidState);
    }
    return fImpl->session->connect_ports(fImpl->session->port_name(src), fImpl->session->port_name(dst));
}

Result<void> Client::disconnect(Port source, Port destination)
{
    if (!fImpl || !fImpl->session) {
        return std::unexpected(Error::InvalidState);
    }
    const jack_port_id_t src = fImpl->port_of(source);
    const jack_port_id_t dst = fImpl->port_of(destination);
    if (src == 0 || dst == 0 || !is_connected(source, destination)) {
        return std::unexpected(Error::NotFound);
    }
    return fImpl->session->disconnect_ports(fImpl->session->port_name(src), fImpl->session->port_name(dst));
}

bool Client::is_connected(Port source, Port destination) const
{
    const jack_port_id_t src = fImpl && fImpl->session ? fImpl->port_of(source) : 0;
    const jack_port_id_t dst = fImpl && fImpl->session ? fImpl->port_of(destination) : 0;
    if (src == 0 || dst == 0) {
        return false;
    }
    return fImpl->session->is_connected(src, dst);
}

std::vector<std::string> Client::connections(Port port) const
{
    const jack_port_id_t index = fImpl && fImpl->session ? fImpl->port_of(port) : 0;
    if (index == 0) {
        return { };
    }
    return fImpl->session->connections(index);
}

Result<void> Client::set_process(ProcessFn fn)
{
    if (!fImpl || !fImpl->session || fImpl->session->is_active()) {
        return std::unexpected(Error::InvalidState);
    }
    fImpl->session->set_process(std::move(fn));
    return { };
}

Result<void> Client::on_xrun(XrunFn fn)
{
    if (!fImpl || !fImpl->session || fImpl->session->is_active()) {
        return std::unexpected(Error::InvalidState);
    }
    fImpl->session->set_xrun_handler(std::move(fn));
    return { };
}

Result<void> Client::on_shutdown(ShutdownFn fn)
{
    if (!fImpl || !fImpl->session || fImpl->session->is_active()) {
        return std::unexpected(Error::InvalidState);
    }
    fImpl->session->set_shutdown_handler(std::move(fn));
    return { };
}

Result<void> Client::on_buffer_size(BufferSizeFn fn)
{
    if (!fImpl || !fImpl->session || fImpl->session->is_active()) {
        return std::unexpected(Error::InvalidState);
    }
    fImpl->session->set_buffer_size_handler(std::move(fn));
    return { };
}

Result<void> Client::on_sample_rate(SampleRateFn fn)
{
    if (!fImpl || !fImpl->session || fImpl->session->is_active()) {
        return std::unexpected(Error::InvalidState);
    }
    fImpl->session->set_sample_rate_handler(std::move(fn));
    return { };
}

Result<void> Client::on_latency(LatencyFn fn)
{
    if (!fImpl || !fImpl->session || fImpl->session->is_active()) {
        return std::unexpected(Error::InvalidState);
    }
    fImpl->session->set_latency_handler(std::move(fn));
    return { };
}

LatencyRange Client::port_latency(Port port, LatencyMode mode) const
{
    const jack_port_id_t index = fImpl && fImpl->session ? fImpl->port_of(port) : 0;
    if (index == 0) {
        return { };
    }
    const jack_latency_range_t range = fImpl->session->port_latency_range(index, to_c_mode(mode));
    return LatencyRange { range.min, range.max };
}

Result<void> Client::set_port_latency(Port port, LatencyMode mode, LatencyRange range)
{
    if (!fImpl || !fImpl->session) {
        return std::unexpected(Error::InvalidState);
    }
    const jack_port_id_t index = fImpl->port_of(port);
    if (index == 0) {
        return std::unexpected(Error::NotFound);
    }
    fImpl->session->set_port_latency_range(index, to_c_mode(mode), jack_latency_range_t { range.min, range.max });
    return { };
}

Result<void> Client::recompute_latencies()
{
    if (!fImpl || !fImpl->session) {
        return std::unexpected(Error::InvalidState);
    }
    return fImpl->session->recompute_latencies();
}

std::string_view Client::name() const noexcept
{
    return fImpl && fImpl->session ? fImpl->session->name() : std::string_view();
}

Uuid Client::uuid() const noexcept
{
    if (!fImpl || !fImpl->session) {
        return { };
    }
    auto uuid = fImpl->session->lookup_uuid();
    return uuid ? *uuid : Uuid();
}

FrameCount Client::sample_rate() const noexcept
{
    return fImpl && fImpl->session ? fImpl->session->sample_rate() : 0;
}

FrameCount Client::buffer_size() const noexcept
{
    return fImpl && fImpl->session ? fImpl->session->buffer_size() : 0;
}

float Client::cpu_load() const noexcept
{
    return fImpl && fImpl->session ? fImpl->session->cpu_load() : 0.0f;
}

// Realtime access.  These do not allocate, lock, or return errors; a port that is not known gives an empty buffer.
std::span<float> Client::audio_buffer(Port port) noexcept
{
    const jack_port_id_t index = fImpl ? fImpl->port_of(port) : 0;
    if (index == 0 || std::strcmp(fImpl->session->port_type(index), JACK_DEFAULT_AUDIO_TYPE) != 0) {
        return { };
    }
    const jack_nframes_t frames = fImpl->session->buffer_size();
    return std::span<float>(static_cast<float*>(fImpl->session->port_buffer(index, frames)), frames);
}

midi::Buffer Client::midi_buffer(Port port) noexcept
{
    const jack_port_id_t index = fImpl ? fImpl->port_of(port) : 0;
    if (index == 0 || std::strcmp(fImpl->session->port_type(index), JACK_DEFAULT_MIDI_TYPE) != 0) {
        return midi::Buffer(nullptr);
    }
    return midi::Buffer(fImpl->session->port_buffer(index, fImpl->session->buffer_size()));
}

namespace midi {

std::size_t Buffer::count() const noexcept
{
    return fHandle ? jack_midi_get_event_count(fHandle) : 0;
}

Event Buffer::event(std::size_t index) const noexcept
{
    jack_midi_event_t raw { };
    if (fHandle == nullptr || jack_midi_event_get(&raw, fHandle, index) != 0) {
        return { };
    }
    return Event { raw.time, std::span<const std::byte>(reinterpret_cast<const std::byte*>(raw.buffer), raw.size) };
}

bool Buffer::write(FrameCount time, std::span<const std::byte> data) noexcept
{
    if (fHandle == nullptr) {
        return false;
    }
    return jack_midi_event_write(fHandle, time, reinterpret_cast<const jack_midi_data_t*>(data.data()), data.size()) == 0;
}

void Buffer::clear() noexcept
{
    if (fHandle != nullptr) {
        jack_midi_clear_buffer(fHandle);
    }
}

} // namespace midi

// Transport.
TransportState Client::transport_state() const noexcept
{
    if (!fImpl || !fImpl->session) {
        return TransportState::Stopped;
    }
    jack_position_t position { };
    switch (fImpl->session->transport_query(&position)) {
    case JackTransportRolling:
        return TransportState::Rolling;
    case JackTransportStarting:
        return TransportState::Starting;
    case JackTransportLooping:
        return TransportState::Looping;
    default:
        return TransportState::Stopped;
    }
}

Result<void> Client::transport_start()
{
    if (!fImpl || !fImpl->session) {
        return std::unexpected(Error::InvalidState);
    }
    fImpl->session->transport_start();
    return { };
}

Result<void> Client::transport_stop()
{
    if (!fImpl || !fImpl->session) {
        return std::unexpected(Error::InvalidState);
    }
    fImpl->session->transport_stop();
    return { };
}

Result<void> Client::transport_locate(FrameCount frame)
{
    if (!fImpl || !fImpl->session) {
        return std::unexpected(Error::InvalidState);
    }
    fImpl->session->transport_locate(frame);
    return { };
}

} // namespace jackmini

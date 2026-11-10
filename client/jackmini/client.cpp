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


// The public client (client.hpp) over a client::ClientSession. The session does the work; this file keeps the handles
// that the program sees, checks the state of the client, and converts the values.

#include "jackmini/client.hpp"

#include "client_session.hpp"

#include "JackMidiPort.h"
#include "jack/transport.h"

#include <cstddef>
#include <cstdint>
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
    std::unique_ptr<client::ClientSession> m_session;

    // The handles of the ports this client has handed out, by id: the id is the slot.  Slot 0 is no port.  A slot is
    // written under the lock by the control thread and read without a lock by the realtime thread, so a slot is never
    // reused: a handle to an unregistered port stays invalid.  The table holds kMaxPortHandles; a client that uses more
    // fails.
    static constexpr std::uint32_t kMaxPortHandles = 4096;
    std::array<std::atomic<PortIdC>, kMaxPortHandles> m_slots { };
    std::uint32_t m_used = 1;
    // The token of this client: its handles carry it, and a handle with another token is not a port of this client.
    std::uint32_t m_token = 0;
    std::unordered_map<PortIdC, std::uint32_t> m_ids;
    std::mutex m_control;

    // Control thread.  Returns 0 when the table is full.
    std::uint32_t idOf(PortIdC port)
    {
        std::lock_guard lock(m_control);
        const auto found = m_ids.find(port);
        if (found != m_ids.end()) {
            return found->second;
        }
        if (m_used >= kMaxPortHandles) {
            return 0;
        }
        const std::uint32_t id = m_used++;
        m_slots[id].store(port);
        m_ids.emplace(port, id);
        return id;
    }

    // Realtime thread: no lock.  Returns 0 for a port that is not this client's.
    PortIdC portOf(Port port) const
    {
        const std::uint32_t id = port.id();
        if (port.owner() != m_token) {
            return 0;
        }
        return (id > 0 && id < kMaxPortHandles) ? m_slots[id].load() : 0;
    }

    // Control thread.
    void forget(PortIdC port)
    {
        std::lock_guard lock(m_control);
        const auto found = m_ids.find(port);
        if (found != m_ids.end()) {
            m_slots[found->second].store(0);
            m_ids.erase(found);
        }
    }
};

namespace {

std::uint64_t toCFlags(PortFlags flags)
{
    std::uint64_t result = 0;
    if (hasFlag(flags, PortFlags::kInput)) {
        result |= kPortIsInput;
    }
    if (hasFlag(flags, PortFlags::kOutput)) {
        result |= kPortIsOutput;
    }
    if (hasFlag(flags, PortFlags::kPhysical)) {
        result |= kPortIsPhysical;
    }
    if (hasFlag(flags, PortFlags::kCanMonitor)) {
        result |= kPortCanMonitor;
    }
    if (hasFlag(flags, PortFlags::kTerminal)) {
        result |= kPortIsTerminal;
    }
    return result;
}

PortFlags fromCFlags(std::uint64_t flags)
{
    PortFlags result = PortFlags::kNone;
    if (flags & kPortIsInput) {
        result = result | PortFlags::kInput;
    }
    if (flags & kPortIsOutput) {
        result = result | PortFlags::kOutput;
    }
    if (flags & kPortIsPhysical) {
        result = result | PortFlags::kPhysical;
    }
    if (flags & kPortCanMonitor) {
        result = result | PortFlags::kCanMonitor;
    }
    if (flags & kPortIsTerminal) {
        result = result | PortFlags::kTerminal;
    }
    return result;
}

const char* toCType(PortType type)
{
    return type == PortType::kMidi ? kDefaultMidiType : kDefaultAudioType;
}

LatencyCallbackModeC toCMode(LatencyMode mode)
{
    return mode == LatencyMode::kCapture ? kCaptureLatency : kPlaybackLatency;
}

} // namespace

Result<Client> Client::open(std::string_view name, OpenOptions options)
{
    auto session = client::ClientSession::open(name, options);
    if (!session) {
        return std::unexpected(session.error());
    }

    Client client;
    client.m_impl = std::make_unique<Impl>();
    static std::atomic<std::uint32_t> nextToken { 1 };
    client.m_impl->m_token = nextToken.fetch_add(1);
    client.m_impl->m_session = std::move(*session);
    return client;
}

Client::Client(Client&& other) noexcept = default;

Client& Client::operator=(Client&& other) noexcept
{
    if (this != &other) {
        if (m_impl && m_impl->m_session) {
            (void)close();
        }
        m_impl = std::move(other.m_impl);
    }
    return *this;
}

Client::~Client()
{
    if (m_impl && m_impl->m_session) {
        (void)close();
    }
}

Result<void> Client::activate()
{
    if (!m_impl || !m_impl->m_session || m_impl->m_session->isActive()) {
        return std::unexpected(Error::kInvalidState);
    }
    return m_impl->m_session->activate();
}

Result<void> Client::deactivate()
{
    if (!m_impl || !m_impl->m_session || !m_impl->m_session->isActive()) {
        return std::unexpected(Error::kInvalidState);
    }
    return m_impl->m_session->deactivate();
}

Result<void> Client::close()
{
    if (!m_impl || !m_impl->m_session) {
        return { };
    }
    auto result = m_impl->m_session->close();
    m_impl->m_session.reset();
    return result;
}

bool Client::isActive() const noexcept
{
    return m_impl && m_impl->m_session && m_impl->m_session->isActive();
}

Result<Port> Client::registerPort(std::string_view name, PortType type, PortFlags flags)
{
    if (!m_impl || !m_impl->m_session) {
        return std::unexpected(Error::kInvalidState);
    }
    // The name is the port's short name; the full name is client:name, so the short name has no colon.
    if (name.empty() || name.find(':') != std::string_view::npos) {
        return std::unexpected(Error::kInvalidArgument);
    }
    auto index = m_impl->m_session->registerPort(name, toCType(type), toCFlags(flags), 0);
    if (!index) {
        const std::string full = std::string(this->name()) + ":" + std::string(name);
        return std::unexpected(m_impl->m_session->findPort(full) ? Error::kNameTaken : Error::kServerFailure);
    }
    return Port(m_impl->idOf(*index), m_impl->m_token);
}

Result<void> Client::unregisterPort(Port port)
{
    if (!m_impl || !m_impl->m_session) {
        return std::unexpected(Error::kInvalidState);
    }
    const PortIdC index = m_impl->portOf(port);
    if (index == 0) {
        return std::unexpected(Error::kNotFound);
    }
    if (auto result = m_impl->m_session->unregisterPort(index); !result) {
        return result;
    }
    m_impl->forget(index);
    return { };
}

// For a port that is not known, the name is empty, the type is audio, and the flags are none: the accessors have no error
// report, so they say nothing rather than fail.
std::string_view Client::portName(Port port) const
{
    const PortIdC index = m_impl && m_impl->m_session ? m_impl->portOf(port) : 0;
    return index ? m_impl->m_session->portName(index) : std::string_view();
}

PortType Client::portType(Port port) const
{
    const PortIdC index = m_impl && m_impl->m_session ? m_impl->portOf(port) : 0;
    if (index == 0) {
        return PortType::kAudio;
    }
    return std::strcmp(m_impl->m_session->portType(index), kDefaultMidiType) == 0 ? PortType::kMidi : PortType::kAudio;
}

PortFlags Client::portFlags(Port port) const
{
    const PortIdC index = m_impl && m_impl->m_session ? m_impl->portOf(port) : 0;
    return index ? fromCFlags(m_impl->m_session->portFlags(index)) : PortFlags::kNone;
}

Result<Port> Client::findPort(std::string_view fullName) const
{
    if (!m_impl || !m_impl->m_session) {
        return std::unexpected(Error::kInvalidState);
    }
    const auto index = m_impl->m_session->findPort(fullName);
    if (!index) {
        return std::unexpected(Error::kNotFound);
    }
    return Port(m_impl->idOf(*index), m_impl->m_token);
}

std::vector<std::string> Client::portNames(PortType type, PortFlags required) const
{
    if (!m_impl || !m_impl->m_session) {
        return { };
    }
    return m_impl->m_session->portNames(toCType(type), toCFlags(required));
}

std::vector<PortInfo> Client::ports() const
{
    if (!m_impl || !m_impl->m_session) {
        return { };
    }
    std::vector<PortInfo> infos;
    for (auto& record : m_impl->m_session->portRecords()) {
        PortInfo info;
        info.m_owner = record.m_name.substr(0, record.m_name.find(':'));
        info.m_name = std::move(record.m_name);
        info.m_type = std::move(record.m_type);
        info.m_flags = fromCFlags(record.m_flags);
        info.m_playbackLatency = LatencyRange { record.m_playbackLatency.m_min, record.m_playbackLatency.m_max };
        info.m_captureLatency = LatencyRange { record.m_captureLatency.m_min, record.m_captureLatency.m_max };
        info.m_totalLatency = record.m_totalLatency;
        info.m_aliases = std::move(record.m_aliases);
        info.m_portUuid = Uuid(record.m_portUuid);
        info.m_ownerUuid = Uuid(record.m_ownerUuid);
        info.m_connections = std::move(record.m_connections);
        infos.push_back(std::move(info));
    }
    return infos;
}

std::uint32_t Client::totalLatency(Port port) const
{
    const PortIdC index = m_impl && m_impl->m_session ? m_impl->portOf(port) : 0;
    return index ? m_impl->m_session->portTotalLatency(index) : 0;
}

Result<void> Client::connect(Port source, Port destination)
{
    if (!m_impl || !m_impl->m_session) {
        return std::unexpected(Error::kInvalidState);
    }
    const PortIdC src = m_impl->portOf(source);
    const PortIdC dst = m_impl->portOf(destination);
    if (src == 0 || dst == 0) {
        return std::unexpected(Error::kNotFound);
    }
    // The server refuses a connection without a reason, so the reasons are checked here first.
    if (portType(source) != portType(destination) || !hasFlag(portFlags(source), PortFlags::kOutput) || !hasFlag(portFlags(destination), PortFlags::kInput)) {
        return std::unexpected(Error::kInvalidArgument); // different types, or not an output to an input
    }
    if (isConnected(source, destination)) {
        return std::unexpected(Error::kInvalidState);
    }
    return m_impl->m_session->connectPorts(m_impl->m_session->portName(src), m_impl->m_session->portName(dst));
}

Result<void> Client::disconnect(Port source, Port destination)
{
    if (!m_impl || !m_impl->m_session) {
        return std::unexpected(Error::kInvalidState);
    }
    const PortIdC src = m_impl->portOf(source);
    const PortIdC dst = m_impl->portOf(destination);
    if (src == 0 || dst == 0 || !isConnected(source, destination)) {
        return std::unexpected(Error::kNotFound);
    }
    return m_impl->m_session->disconnectPorts(m_impl->m_session->portName(src), m_impl->m_session->portName(dst));
}

bool Client::isConnected(Port source, Port destination) const
{
    const PortIdC src = m_impl && m_impl->m_session ? m_impl->portOf(source) : 0;
    const PortIdC dst = m_impl && m_impl->m_session ? m_impl->portOf(destination) : 0;
    if (src == 0 || dst == 0) {
        return false;
    }
    return m_impl->m_session->isConnected(src, dst);
}

std::vector<std::string> Client::connections(Port port) const
{
    const PortIdC index = m_impl && m_impl->m_session ? m_impl->portOf(port) : 0;
    if (index == 0) {
        return { };
    }
    return m_impl->m_session->connections(index);
}

Result<void> Client::setProcess(ProcessFn fn)
{
    if (!m_impl || !m_impl->m_session || m_impl->m_session->isActive()) {
        return std::unexpected(Error::kInvalidState);
    }
    m_impl->m_session->setProcess(std::move(fn));
    return { };
}

Result<void> Client::onXrun(XrunFn fn)
{
    if (!m_impl || !m_impl->m_session || m_impl->m_session->isActive()) {
        return std::unexpected(Error::kInvalidState);
    }
    m_impl->m_session->setXrunHandler(std::move(fn));
    return { };
}

Result<void> Client::onShutdown(ShutdownFn fn)
{
    if (!m_impl || !m_impl->m_session || m_impl->m_session->isActive()) {
        return std::unexpected(Error::kInvalidState);
    }
    m_impl->m_session->setShutdownHandler(std::move(fn));
    return { };
}

Result<void> Client::onBufferSize(BufferSizeFn fn)
{
    if (!m_impl || !m_impl->m_session || m_impl->m_session->isActive()) {
        return std::unexpected(Error::kInvalidState);
    }
    m_impl->m_session->setBufferSizeHandler(std::move(fn));
    return { };
}

Result<void> Client::onSampleRate(SampleRateFn fn)
{
    if (!m_impl || !m_impl->m_session || m_impl->m_session->isActive()) {
        return std::unexpected(Error::kInvalidState);
    }
    m_impl->m_session->setSampleRateHandler(std::move(fn));
    return { };
}

Result<void> Client::onLatency(LatencyFn fn)
{
    if (!m_impl || !m_impl->m_session || m_impl->m_session->isActive()) {
        return std::unexpected(Error::kInvalidState);
    }
    m_impl->m_session->setLatencyHandler(std::move(fn));
    return { };
}

LatencyRange Client::portLatency(Port port, LatencyMode mode) const
{
    const PortIdC index = m_impl && m_impl->m_session ? m_impl->portOf(port) : 0;
    if (index == 0) {
        return { };
    }
    const LatencyRangeC range = m_impl->m_session->portLatencyRange(index, toCMode(mode));
    return LatencyRange { range.m_min, range.m_max };
}

Result<void> Client::setPortLatency(Port port, LatencyMode mode, LatencyRange range)
{
    if (!m_impl || !m_impl->m_session) {
        return std::unexpected(Error::kInvalidState);
    }
    const PortIdC index = m_impl->portOf(port);
    if (index == 0) {
        return std::unexpected(Error::kNotFound);
    }
    m_impl->m_session->setPortLatencyRange(index, toCMode(mode), LatencyRangeC { range.m_min, range.m_max });
    return { };
}

Result<void> Client::recomputeLatencies()
{
    if (!m_impl || !m_impl->m_session) {
        return std::unexpected(Error::kInvalidState);
    }
    return m_impl->m_session->recomputeLatencies();
}

std::string_view Client::name() const noexcept
{
    return m_impl && m_impl->m_session ? m_impl->m_session->name() : std::string_view();
}

Uuid Client::uuid() const noexcept
{
    if (!m_impl || !m_impl->m_session) {
        return { };
    }
    auto uuid = m_impl->m_session->lookupUuid();
    return uuid ? *uuid : Uuid();
}

FrameCount Client::sampleRate() const noexcept
{
    return m_impl && m_impl->m_session ? m_impl->m_session->sampleRate() : 0;
}

FrameCount Client::bufferSize() const noexcept
{
    return m_impl && m_impl->m_session ? m_impl->m_session->bufferSize() : 0;
}

float Client::cpuLoad() const noexcept
{
    return m_impl && m_impl->m_session ? m_impl->m_session->cpuLoad() : 0.0f;
}

// Realtime access.  These do not allocate, lock, or return errors; a port that is not known gives an empty buffer.
std::span<float> Client::audioBuffer(Port port) noexcept
{
    const PortIdC index = m_impl ? m_impl->portOf(port) : 0;
    if (index == 0 || std::strcmp(m_impl->m_session->portType(index), kDefaultAudioType) != 0) {
        return { };
    }
    const NframesC frames = m_impl->m_session->bufferSize();
    return std::span<float>(static_cast<float*>(m_impl->m_session->portBuffer(index, frames)), frames);
}

midi::Buffer Client::midiBuffer(Port port) noexcept
{
    const PortIdC index = m_impl ? m_impl->portOf(port) : 0;
    if (index == 0 || std::strcmp(m_impl->m_session->portType(index), kDefaultMidiType) != 0) {
        return midi::Buffer(nullptr);
    }
    return midi::Buffer(m_impl->m_session->portBuffer(index, m_impl->m_session->bufferSize()));
}

namespace midi {

std::size_t Buffer::count() const noexcept
{
    return m_handle ? jack_midi_get_event_count(m_handle) : 0;
}

Event Buffer::event(std::size_t index) const noexcept
{
    MidiEventC raw { };
    if (m_handle == nullptr || jack_midi_event_get(&raw, m_handle, index) != 0) {
        return { };
    }
    return Event { raw.m_time, std::span<const std::byte>(reinterpret_cast<const std::byte*>(raw.m_buffer), raw.m_size) };
}

bool Buffer::write(FrameCount time, std::span<const std::byte> data) noexcept
{
    if (m_handle == nullptr) {
        return false;
    }
    return jack_midi_event_write(m_handle, time, reinterpret_cast<const MidiData*>(data.data()), data.size()) == 0;
}

void Buffer::clear() noexcept
{
    if (m_handle != nullptr) {
        jack_midi_clear_buffer(m_handle);
    }
}

} // namespace midi

// Transport.
TransportState Client::transportState() const noexcept
{
    if (!m_impl || !m_impl->m_session) {
        return TransportState::kStopped;
    }
    PositionC position { };
    switch (m_impl->m_session->transportQuery(&position)) {
    case kTransportRolling:
        return TransportState::kRolling;
    case kTransportStarting:
        return TransportState::kStarting;
    case kTransportLooping:
        return TransportState::kLooping;
    default:
        return TransportState::kStopped;
    }
}

Result<void> Client::transportStart()
{
    if (!m_impl || !m_impl->m_session) {
        return std::unexpected(Error::kInvalidState);
    }
    m_impl->m_session->transportStart();
    return { };
}

Result<void> Client::transportStop()
{
    if (!m_impl || !m_impl->m_session) {
        return std::unexpected(Error::kInvalidState);
    }
    m_impl->m_session->transportStop();
    return { };
}

Result<void> Client::transportLocate(FrameCount frame)
{
    if (!m_impl || !m_impl->m_session) {
        return std::unexpected(Error::kInvalidState);
    }
    m_impl->m_session->transportLocate(frame);
    return { };
}

} // namespace jackmini

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


#include "ClientSession.hpp"

#include "ClientControl.h"
#include "Constants.h"
#include "Error.h"
#include "Globals.h"
#include "Message.h"
#include "PlatformPlug.h"
#include "Request.h"
#include "ServerLaunch.h"
#include "Time.h"
#include "Tools.h"

#include "FrameTimer.h"

#include <sys/socket.h>
#include <array>
#include <cerrno>
#include <charconv>
#include <climits>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "CString.h"
#include "FdSegment.h"

namespace jackmini::client {

namespace {

// Set on the notification thread, so a call to the server from there is refused instead of deadlocking.
thread_local bool gOnNotificationThread = false;

// Set on the realtime thread of a client.
thread_local bool gOnRealtimeThread = false;

#ifndef SCHED_RESET_ON_FORK
#define SCHED_RESET_ON_FORK 0x40000000
#endif

// The realtime class (SCHED_FIFO) for a thread: the same request as the thread of the server, with the reset on fork
// when the system knows it.
std::int32_t acquireRealtime(pthread_t thread, std::int32_t priority)
{
    sched_param param { };
    param.sched_priority = priority;
    const std::int32_t res = pthread_setschedparam(thread, SCHED_FIFO, &param);
    if (res == 0) {
        return 0;
    }
    if (pthread_setschedparam(thread, SCHED_FIFO | SCHED_RESET_ON_FORK, &param) == 0) {
        return 0;
    }
    jack_error("Cannot use real-time scheduling (RR/%d) (%d: %s)", priority, res, strerror(res));
    return -1;
}

void dropRealtime(pthread_t thread)
{
    sched_param param { };
    if (pthread_setschedparam(thread, SCHED_OTHER, &param) != 0) {
        jack_error("Cannot switch to normal scheduling priority(%s)", strerror(errno));
    }
}

std::string defaultServerName()
{
    const char* name = getenv("JACK_DEFAULT_SERVER");
    return name ? name : kDefaultServerName;
}

// The bits of the options that the server reads when it checks a new client.
std::int32_t optionBits(const OpenOptions& options)
{
    std::int32_t bits = 0;
    if (!options.m_startServer) {
        bits |= kNoStartServer;
    }
    if (options.m_exactName) {
        bits |= kUseExactName;
    }
    if (!options.m_serverName.empty()) {
        bits |= kServerName;
    }
    return bits;
}

void shutDownFd(std::int32_t fd)
{
    if (fd >= 0) {
        ::shutdown(fd, SHUT_RDWR);
    }
}

// The server writes a uuid as a decimal number. A uuid has the type bits set, so a smaller number is not one.
std::optional<std::uint64_t> parseServerUuid(std::string_view text)
{
    text = skipNumericPrefix(text);
    std::uint64_t value = 0;
    const auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (ec != std::errc() || value < (std::uint64_t(1) << 32)) {
        return std::nullopt;
    }
    return value;
}

void setupDriverSync(ProcessContext& context, bool freewheel)
{
    // In "async" mode the server does not wait for the drivers, so their synchro is set to flush: it keeps no
    // activations that nobody consumes. See JackClient::SetupDriverSync.
    const bool flush = !freewheel && !context.m_engine->m_syncMode;
    for (std::int32_t i = 0; i < context.m_engine->m_driverNum; i++) {
        context.m_synchro[i].setFlush(flush);
    }
}

} // end of anonymous namespace

ClientSession::ClientSession()
    : m_context(&ProcessContext::acquire())
{
}

ClientSession::~ClientSession()
{
    (void)close();
    ProcessContext::release();
}

Result<std::unique_ptr<ClientSession>> ClientSession::open(std::string_view name, const OpenOptions& options)
{
    if (name.empty() || name.size() >= kClientNameSize) {
        return std::unexpected(Error::kInvalidArgument);
    }

    std::unique_ptr<ClientSession> session(new ClientSession());
    if (auto opened = session->connectAndRegister(name, options); !opened) {
        return std::unexpected(opened.error());
    }
    return session;
}

Result<void> ClientSession::connectAndRegister(std::string_view nameView, const OpenOptions& options)
{
    const std::string name(nameView);
    m_serverName = options.m_serverName.empty() ? defaultServerName() : options.m_serverName;
    const std::int32_t bits = optionBits(options);

    StatusC status = static_cast<StatusC>(0);
    if (tryStartServer(m_serverName.c_str(), static_cast<OptionsC>(bits), &status) < 0) {
        return std::unexpected(Error::kNoServer);
    }

    if (m_request.connect(kServerDir, m_serverName.c_str(), 0) < 0) {
        return std::unexpected(Error::kIo);
    }

    ClientCheckRequest check(name.c_str(), kProtocolVersion, bits, kUuidEmptyInitializer, true);
    ClientCheckResult checkReply;
    if (transact(check, checkReply) < 0) {
        return std::unexpected(Error::kIo);
    }
    if (checkReply.m_result < 0) {
        if (checkReply.m_status & kVersionError) {
            jack_error("JACK protocol mismatch %d", kProtocolVersion);
            return std::unexpected(Error::kProtocol);
        }
        jack_error("Client name = %s conflits with another running client", name.c_str());
        return std::unexpected(Error::kNameTaken);
    }
    const std::string nameRes(checkReply.m_name.data());

    if (m_notifyListen.bind(kClientDir, nameRes.c_str(), 0) < 0) {
        jack_error("Cannot bind socket");
        return std::unexpected(Error::kIo);
    }
    if (!startNotifications()) {
        jack_error("Cannot start channel");
        return std::unexpected(Error::kIo);
    }

    ClientOpenRequest openRequest(nameRes.c_str(), Tools::getPID(), kUuidEmptyInitializer);
    ClientOpenResult openReply;
    if (transact(openRequest, openReply) < 0) {
        return std::unexpected(Error::kIo);
    }
    // The descriptors belong to this function: the mappings keep the segments alive after they are closed.
    std::array<UniqueFd, 3> descriptors;
    descriptors[0].reset(openReply.m_sharedEngine.m_fd);
    descriptors[1].reset(openReply.m_sharedGraph.m_fd);
    descriptors[2].reset(openReply.m_sharedClient.m_fd);
    if (openReply.m_result < 0) {
        jack_error("Cannot open %s client", nameRes.c_str());
        return std::unexpected(Error::kServerFailure);
    }

    ProcessContext& context = *m_context;
    try {
        context.m_engine.map(openReply.m_sharedEngine);
        context.m_graph.map(openReply.m_sharedGraph);
        m_client.map(openReply.m_sharedClient);
    } catch (...) {
        jack_error("Map shared memory segments exception");
        return std::unexpected(Error::kIo);
    }
    Globals::s_verbose = context.m_engine->m_verbose;

    m_ref = m_client->m_refNum;
    m_name = nameRes;
    setupDriverSync(context, context.m_engine->m_freewheel);

    {
        std::lock_guard lock(context.m_synchroMutex);
        if (!context.m_synchro[m_ref].connect(m_name.c_str(), m_serverName.c_str())) {
            jack_error("Cannot ConnectSemaphore %s client", m_name.c_str());
            return std::unexpected(Error::kIo);
        }
    }
    setClockSource(context.m_engine->m_clockSource);
    return { };
}

bool ClientSession::startNotifications()
{
    m_notifier = std::jthread([this](std::stop_token) { notificationLoop(); });
    std::unique_lock lock(m_readyMutex);
    m_ready.wait(lock, [this] { return m_readyFlag; });
    return true;
}

void ClientSession::notificationLoop()
{
    gOnNotificationThread = true;
    {
        std::lock_guard lock(m_readyMutex);
        m_readyFlag = true;
    }
    m_ready.notify_all();

    // The listening socket is closed by close(), after this thread has been joined.
    m_notify.reset(m_notifyListen.accept());
    if (!m_notify) {
        jack_error("JackSocketClientChannel: cannot establish notification socket");
        m_serverDown = true;
        return;
    }
    m_notifyFd = m_notify->getFd();

    while (true) {
        MsgHeader header { };
        if (std::int32_t err = readJackMsgHeader(m_notify.get(), header); err < 0) {
            if (err != kRequestErrAborted && !m_closing) {
                jack_error("JackSocketClientChannel read fail");
            }
            break;
        }
        if (header.m_version != kJackMsgVersion || header.m_length != ClientNotification::kPayloadSize) {
            jack_error("JackSocketClientChannel: notification of protocol version %d, length %d", header.m_version, header.m_length);
            break;
        }

        ClientNotification event;
        {
            PayloadReader payload(m_notify.get(), header.m_length, { });
            if (event.read(&payload) < 0 || !payload.done()) {
                jack_error("JackSocketClientChannel: notification does not match its length");
                break;
            }
        }
        if (event.m_notify == kDropped) {
            jack_error("JACK server dropped %d notifications for this client (it did not read them in time)", event.m_value1);
        }

        Reply reply;
        reply.m_result = handleNotification(event);
        if (event.m_sync && sendJackReply(m_notify.get(), reply, header) < 0) {
            jack_error("JackSocketClientChannel write fail");
            break;
        }
    }
    // Ending the loop by close() is not a server failure. A lost connection is reported to the client once.
    if (!m_closing) {
        m_serverDown = true;
        deliverShutdown();
    }
}

void ClientSession::deliverShutdown()
{
    if (m_shutdown) {
        auto fn = std::move(m_shutdown);
        m_shutdown = nullptr;
        fn();
    }
}

std::int32_t ClientSession::handleNotification(const ClientNotification& event)
{
    ProcessContext& context = *m_context;
    std::int32_t res = 0;
    switch (event.m_notify) {
    case kAddClient: {
        std::lock_guard lock(context.m_synchroMutex);
        res = context.m_synchro[event.m_refNum].connect(event.m_name.data(), m_serverName.c_str()) ? 0 : -1;
        break;
    }
    case kRemoveClient:
        if (m_name != event.m_name.data()) {
            std::lock_guard lock(context.m_synchroMutex);
            res = context.m_synchro[event.m_refNum].disconnect() ? 0 : -1;
        }
        break;
    case kShutDownCallback:
        m_serverDown = true;
        if (isActive()) {
            deliverShutdown();
        }
        break;
    case kLatencyCallback:
        if (isActive()) {
            res = handleLatency(event.m_value1);
        }
        break;
    case kXRunCallback:
        if (isActive() && m_xrun) {
            m_xrun();
        }
        break;
    case kBufferSizeCallback:
        if (isActive() && m_bufferSize) {
            res = m_bufferSize(event.m_value1) ? 0 : -1;
        }
        break;
    case kSampleRateCallback:
        if (isActive() && m_sampleRate) {
            res = m_sampleRate(event.m_value1) ? 0 : -1;
        }
        break;
    case kStartFreewheelCallback: {
        setupDriverSync(context, true);
        // Freewheel runs the cycles without the sound card, so the realtime class is given up while it lasts.
        std::lock_guard lock(m_realtimeMutex);
        if (m_realtimeRunning && isActive()) {
            dropRealtime(m_realtimeThread);
        }
        break;
    }
    case kStopFreewheelCallback: {
        setupDriverSync(context, false);
        std::lock_guard lock(m_realtimeMutex);
        if (context.m_engine->m_realTime && m_realtimeRunning && isActive()) {
            if (acquireRealtime(m_realtimeThread, context.m_engine->m_clientPriority) < 0) {
                jack_error("JackClient::AcquireRealTime error");
            }
        }
        break;
    }
    }
    return res;
}

std::int32_t ClientSession::transact(Request& request, Reply& reply)
{
    if (gOnNotificationThread) {
        jack_error("Cannot callback the server in notification thread!");
        return -1;
    }
    if (m_serverDown) {
        jack_error("Server is not running");
        return -1;
    }

    std::lock_guard lock(m_requestMutex);
    if (request.write(&m_request) < 0) {
        jack_error("Could not write request type = %ld", request.m_type);
        return -1;
    }
    if (readJackReply(&m_request, request.m_type, request.m_requestId, reply) < 0) {
        jack_error("Could not read result type = %ld", request.m_type);
        return -1;
    }
    return 0;
}

Result<PortIdC> ClientSession::registerPort(std::string_view shortName, const char* type, std::uint64_t flags, std::uint64_t bufferSize)
{
    if (shortName.empty()) {
        jack_error("port_name is empty");
        return std::unexpected(Error::kInvalidArgument);
    }
    const std::string fullName = m_name + ":" + std::string(shortName);
    if (fullName.size() >= kRealJackPortNameSize) {
        jack_error("\"%s\" is too long to be used as a JACK port name.", fullName.c_str());
        return std::unexpected(Error::kInvalidArgument);
    }

    PortRegisterRequest request(m_ref, fullName.c_str(), type, static_cast<std::uint32_t>(flags), static_cast<std::uint32_t>(bufferSize));
    PortRegisterResult reply;
    if (transact(request, reply) < 0 || reply.m_result < 0) {
        return std::unexpected(Error::kServerFailure);
    }
    return reply.m_portIndex;
}

Result<void> ClientSession::unregisterPort(PortIdC port)
{
    if (!owns(port)) {
        jack_error("unregistering a port %ld that is not own by the client", port);
        return std::unexpected(Error::kNotFound);
    }
    PortUnRegisterRequest request(m_ref, port);
    Reply reply;
    if (transact(request, reply) < 0 || reply.m_result < 0) {
        return std::unexpected(Error::kServerFailure);
    }
    return { };
}

Result<void> ClientSession::connectPorts(std::string_view source, std::string_view destination)
{
    if (source.size() >= kRealJackPortNameSize || destination.size() >= kRealJackPortNameSize) {
        return std::unexpected(Error::kInvalidArgument);
    }
    const std::string src(source);
    const std::string dst(destination);
    PortConnectNameRequest request(m_ref, src.c_str(), dst.c_str());
    Reply reply;
    if (transact(request, reply) < 0 || reply.m_result < 0) {
        return std::unexpected(Error::kServerFailure);
    }
    return { };
}

Result<void> ClientSession::disconnectPorts(std::string_view source, std::string_view destination)
{
    if (source.size() >= kRealJackPortNameSize || destination.size() >= kRealJackPortNameSize) {
        return std::unexpected(Error::kInvalidArgument);
    }
    const std::string src(source);
    const std::string dst(destination);
    PortDisconnectNameRequest request(m_ref, src.c_str(), dst.c_str());
    Reply reply;
    if (transact(request, reply) < 0 || reply.m_result < 0) {
        return std::unexpected(Error::kServerFailure);
    }
    return { };
}

bool ClientSession::owns(PortIdC port) const
{
    return port > 0 && port < kPortNumMax && m_context->m_graph->getPort(port)->getRefNum() == m_ref;
}

std::string_view ClientSession::portName(PortIdC port) const
{
    if (port == 0 || port >= kPortNumMax) {
        return { };
    }
    return m_context->m_graph->getPort(port)->getName();
}

const char* ClientSession::portType(PortIdC port) const
{
    return m_context->m_graph->getPort(port)->getType();
}

std::uint64_t ClientSession::portFlags(PortIdC port) const
{
    return m_context->m_graph->getPort(port)->getFlags();
}

std::optional<PortIdC> ClientSession::findPort(std::string_view fullName) const
{
    const std::string text(fullName);
    const PortIdC port = m_context->m_graph->getPort(text.c_str());
    if (port == kNoPort) {
        return std::nullopt;
    }
    return port;
}

namespace {

// The graph gives a list of names that point into its own table, and a list that it allocated. The names are copied
// and the list is freed.
std::vector<std::string> takeNames(const char** list)
{
    std::vector<std::string> names;
    if (list == nullptr) {
        return names;
    }
    for (const char** entry = list; *entry != nullptr; entry++) {
        names.emplace_back(*entry);
    }
    free(list);
    return names;
}

} // end of anonymous namespace

std::vector<std::string> ClientSession::portNames(const char* type, std::uint64_t flags) const
{
    return takeNames(m_context->m_graph->getPorts(nullptr, type, flags));
}

std::vector<std::string> ClientSession::connections(PortIdC port) const
{
    waitGraphChange();
    return takeNames(m_context->m_graph->getConnections(port));
}

std::vector<ClientSession::PortRecord> ClientSession::portRecords() const
{
    waitGraphChange();
    std::vector<PortRecord> records;
    const PortIdC max = m_context->m_graph->portMax();
    for (PortIdC index = kFirstAvailablePort; index < max; index++) {
        ServerPort* port = m_context->m_graph->getPort(index);
        if (!port->inUse()) {
            continue;
        }
        PortRecord record;
        record.m_name = port->getName();
        record.m_type = port->getType();
        record.m_flags = port->getFlags();
        port->getLatencyRange(kPlaybackLatency, &record.m_playbackLatency);
        port->getLatencyRange(kCaptureLatency, &record.m_captureLatency);
        record.m_totalLatency = port->getTotalLatency();

        std::array<char, kRealJackPortNameSize + 1> alias1 { }, alias2 { };
        std::array<char* const, 2> aliases = { alias1.data(), alias2.data() };
        const std::int32_t count = port->getAliases(aliases.data());
        for (std::int32_t i = 0; i < count; i++) {
            record.m_aliases.emplace_back(aliases[i]);
        }

        record.m_portUuid = port->getUUID();
        record.m_ownerUuid = m_context->m_graph->getClientUUID(port->getRefNum());
        record.m_connections = takeNames(m_context->m_graph->getConnections(index));
        records.push_back(std::move(record));
    }
    return records;
}

NframesC ClientSession::portTotalLatency(PortIdC port) const
{
    waitGraphChange();
    return m_context->m_graph->getPort(port)->getTotalLatency();
}

bool ClientSession::isConnected(PortIdC source, PortIdC destination) const
{
    waitGraphChange();
    return m_context->m_graph->isConnected(source, destination);
}

void ClientSession::waitGraphChange() const
{
    // A pending graph change is waited for on a client's own thread only. The realtime thread reads the current state.
    if (gOnRealtimeThread) {
        return;
    }
    if (m_context->m_graph->isPendingChange()) {
        sleepMicroseconds(std::int32_t(m_context->m_engine->m_periodUsecs * 1.1f));
    }
}

// The server asks for the latency of the ports, in one direction (status 0 is capture, the others playback). The ports
// measured in that direction get their range from their connections first; then the handler decides, or the default does.
std::int32_t ClientSession::handleLatency(std::int32_t status)
{
    const LatencyCallbackModeC mode = (status == 0) ? kCaptureLatency : kPlaybackLatency;
    ProcessContext& context = *m_context;
    std::vector<PortIdC> ports;
    context.m_graph->getOwnedPorts(m_ref, ports);

    for (PortIdC port : ports) {
        const std::uint64_t flags = context.m_graph->getPort(port)->getFlags();
        if ((flags & kPortIsOutput) && mode == kPlaybackLatency) {
            context.m_graph->recalculateLatency(port, mode);
        }
        if ((flags & kPortIsInput) && mode == kCaptureLatency) {
            context.m_graph->recalculateLatency(port, mode);
        }
    }

    if (m_latency) {
        m_latency(mode == kCaptureLatency ? LatencyMode::kCapture : LatencyMode::kPlayback);
        return 0;
    }

    // Playback: the input ports take the widest range of the output ports. Capture: the output ports take the widest
    // range of the input ports. This is the default that every port without a handler gets.
    const bool playback = mode == kPlaybackLatency;
    const std::uint64_t measured = playback ? kPortIsOutput : kPortIsInput;
    const std::uint64_t setOn = playback ? kPortIsInput : kPortIsOutput;
    LatencyRangeC latency = { UINT32_MAX, 0 };
    for (PortIdC port : ports) {
        ServerPort* p = context.m_graph->getPort(port);
        if (p->getFlags() & measured) {
            LatencyRangeC other { };
            p->getLatencyRange(mode, &other);
            latency.m_max = std::max(latency.m_max, other.m_max);
            latency.m_min = std::min(latency.m_min, other.m_min);
        }
    }
    if (latency.m_min == UINT32_MAX) {
        latency.m_min = 0;
    }
    for (PortIdC port : ports) {
        ServerPort* p = context.m_graph->getPort(port);
        if (p->getFlags() & setOn) {
            p->setLatencyRange(mode, &latency);
        }
    }
    return 0;
}

LatencyRangeC ClientSession::portLatencyRange(PortIdC port, LatencyCallbackModeC mode) const
{
    waitGraphChange();
    LatencyRangeC range { };
    m_context->m_graph->getPort(port)->getLatencyRange(mode, &range);
    return range;
}

void ClientSession::setPortLatencyRange(PortIdC port, LatencyCallbackModeC mode, LatencyRangeC range)
{
    waitGraphChange();
    m_context->m_graph->getPort(port)->setLatencyRange(mode, &range);
}

Result<void> ClientSession::recomputeLatencies()
{
    ComputeTotalLatenciesRequest request;
    Reply reply;
    if (transact(request, reply) < 0 || reply.m_result < 0) {
        return std::unexpected(Error::kServerFailure);
    }
    return { };
}

void ClientSession::transportStart() noexcept
{
    m_context->m_engine->m_transport.setCommand(kTransportCommandStart);
}

void ClientSession::transportStop() noexcept
{
    m_context->m_engine->m_transport.setCommand(kTransportCommandStop);
}

void ClientSession::transportLocate(NframesC frame) noexcept
{
    PositionC position { };
    position.m_frame = frame;
    position.m_valid = static_cast<PositionBitsC>(0);
    m_context->m_engine->m_transport.requestNewPos(&position);
}

TransportStateC ClientSession::transportQuery(PositionC* position) const noexcept
{
    return m_context->m_engine->m_transport.query(position);
}

void* ClientSession::portBuffer(PortIdC port, NframesC frames) noexcept
{
    return m_context->m_graph->getBuffer(port, frames);
}

Result<Uuid> ClientSession::lookupUuid()
{
    GetUUIDRequest request(m_name.c_str());
    UUIDResult reply;
    if (transact(request, reply) < 0 || reply.m_result < 0) {
        return std::unexpected(Error::kServerFailure);
    }
    const auto parsed = parseServerUuid(reply.m_uUID.data());
    if (!parsed) {
        return std::unexpected(Error::kProtocol);
    }
    return Uuid(*parsed);
}

FrameCount ClientSession::sampleRate() const noexcept
{
    return m_context->m_engine->m_sampleRate;
}

FrameCount ClientSession::bufferSize() const noexcept
{
    return m_context->m_engine->m_bufferSize;
}

float ClientSession::cpuLoad() const noexcept
{
    return m_context->m_engine->m_cPULoad;
}

bool ClientSession::isActive() const noexcept
{
    return m_ref >= 0 && std::atomic_ref<bool>(m_client->m_active).load();
}

Result<void> ClientSession::activate()
{
    if (m_ref < 0) {
        return std::unexpected(Error::kInvalidState);
    }
    if (isActive()) {
        return { };
    }

    const bool realTime = static_cast<bool>(m_process);
    if (realTime && !startRealtime()) {
        return std::unexpected(Error::kServerFailure);
    }

    // Insertion of the client in the graph causes a graph order notification, which the client wants. The transport
    // flags are set as they were before: the server asks the client about transport through them.
    std::atomic_ref<bool>(m_client->m_active).store(true);
    m_client->m_transportSync = true;
    m_client->m_transportTimebase = true;
    m_client->m_callback[kRealTimeCallback] = realTime;

    ActivateRequest request(m_ref, realTime);
    Reply reply;
    if (transact(request, reply) < 0 || reply.m_result < 0) {
        std::atomic_ref<bool>(m_client->m_active).store(false);
        m_client->m_transportSync = false;
        m_client->m_transportTimebase = false;
        stopRealtime();
        return std::unexpected(Error::kServerFailure);
    }
    return { };
}

Result<void> ClientSession::deactivate()
{
    if (!isActive()) {
        // A thread that ended by itself after a failed cycle still has to be joined.
        stopRealtime();
        return { };
    }

    std::atomic_ref<bool>(m_client->m_active).store(false);
    m_client->m_transportSync = false;
    m_client->m_transportTimebase = false;

    // The server takes the client out of the graph and waits for the cycle to finish before it answers. Only then is the
    // realtime thread stopped.
    DeactivateRequest request(m_ref);
    Reply reply;
    const bool ok = transact(request, reply) == 0 && reply.m_result == 0;
    stopRealtime();
    return ok ? Result<void> { } : std::unexpected(Error::kServerFailure);
}

bool ClientSession::startRealtime()
{
    std::lock_guard lock(m_realtimeMutex);
    m_realtimeStop = false;
    if (pthread_create(&m_realtimeThread, nullptr, &ClientSession::realtimeEntry, this) != 0) {
        jack_error("Start thread error");
        return false;
    }
    m_realtimeRunning = true;
    return true;
}

void ClientSession::stopRealtime()
{
    std::lock_guard lock(m_realtimeMutex);
    if (!m_realtimeRunning) {
        return;
    }
    // A running thread belongs to an open client, so ref_ is valid here.
    m_realtimeStop = true;
    // The thread may be waiting for a cycle. The signal wakes it, and it checks the stop flag before it runs a cycle.
    m_context->m_synchro[m_ref].signal();
    pthread_join(m_realtimeThread, nullptr);
    m_realtimeRunning = false;
    // A wake-up that the thread did not take must not reach the next activation.
    m_context->m_synchro[m_ref].timedWait(0);
}

void* ClientSession::realtimeEntry(void* arg)
{
    static_cast<ClientSession*>(arg)->realtimeRun();
    return nullptr;
}

void ClientSession::realtimeRun()
{
    gOnRealtimeThread = true;
    if (m_context->m_engine->m_realTime) {
        setThreadedLogFunction();
        if (acquireRealtime(pthread_self(), m_context->m_engine->m_clientPriority) < 0) {
            jack_error("JackClient::AcquireSelfRealTime error");
        }
    }
    // The buffer size is reported before the first cycle, so no buffer size callback runs during a cycle.
    if (m_bufferSize) {
        (void)m_bufferSize(m_context->m_engine->m_bufferSize);
    }

    // The first wait is the dummy cycle that the old client ran too: the thread is in step with the graph before the
    // first process call. It signals its outputs without calling the process callback.
    if (!waitCycle()) {
        if (!m_realtimeStop) {
            // The graph wait failed: the server is gone. The client is deactivated and told so.
            endCyclesOnFailure();
            deliverShutdown();
        }
        return;
    }
    signalCycle();

    while (true) {
        if (!waitCycle()) {
            if (!m_realtimeStop) {
                endCyclesOnFailure();
                deliverShutdown();
            }
            return;
        }
        const Cycle cycle = buildCycle();
        if (!m_process(cycle)) {
            signalCycle();
            endCyclesOnFailure();
            return;
        }
        signalCycle();
    }
}

bool ClientSession::waitCycle()
{
    if (!m_context->m_graph->suspendRefNum(m_client.getShmAddress(), m_context->m_synchro.data(), LONG_MAX)) {
        jack_error("SuspendRefNum error");
        return false;
    }
    return !m_realtimeStop;
}

void ClientSession::signalCycle()
{
    if (!m_context->m_graph->resumeRefNum(m_client.getShmAddress(), m_context->m_synchro.data())) {
        jack_error("ResumeRefNum error");
    }
}

Cycle ClientSession::buildCycle() const
{
    Cycle cycle;
    cycle.m_frames = m_context->m_engine->m_bufferSize;
    Timer timer;
    m_context->m_engine->readFrameTime(&timer);
    NframesC currentFrames = 0;
    TimeC currentUsecs = 0;
    TimeC nextUsecs = 0;
    float periodUsecs = 0.0f;
    if (timer.getCycleTimes(&currentFrames, &currentUsecs, &nextUsecs, &periodUsecs) == 0) {
        cycle.m_currentFrames = currentFrames;
        cycle.m_currentUsecs = currentUsecs;
        cycle.m_nextUsecs = nextUsecs;
        cycle.m_periodUsecs = periodUsecs;
    }
    cycle.m_freewheel = m_context->m_engine->m_freewheel.load();
    return cycle;
}

void ClientSession::endCyclesOnFailure()
{
    // The process callback failed: the client leaves the graph, as the old client did. The realtime class is given up
    // first, so that the server's answer does not wait for this thread.
    dropRealtime(pthread_self());
    std::atomic_ref<bool>(m_client->m_active).store(false);
    m_client->m_transportSync = false;
    m_client->m_transportTimebase = false;
    DeactivateRequest request(m_ref);
    Reply reply;
    if (transact(request, reply) < 0 || reply.m_result < 0) {
        jack_error("Deactivation after a failed cycle did not complete");
    }
}

void ClientSession::shutDownSockets()
{
    shutDownFd(m_notifyListen.getFd());
    shutDownFd(m_notifyFd.load());
}

Result<void> ClientSession::close()
{
    if (m_closing.exchange(true)) {
        return { };
    }

    // An active client is deactivated first, which also stops its realtime thread.
    (void)deactivate();

    // The notifications stop, so no notification arrives while the client is closed.
    shutDownSockets();
    if (m_notifier.joinable()) {
        m_notifier.join();
    }
    m_notify.reset();
    m_notifyListen.close();
    m_notifyFd = -1;

    std::int32_t result = 0;
    if (m_ref >= 0) {
        if (serverAlive()) {
            ClientCloseRequest closeRequest(m_ref);
            Reply reply;
            if (transact(closeRequest, reply) < 0 || reply.m_result < 0) {
                result = -1;
            }
        } else {
            result = -1;
        }
        {
            std::lock_guard lock(m_context->m_synchroMutex);
            m_context->m_synchro[m_ref].disconnect();
        }
        m_ref = -1;
    }
    m_request.close();

    return (result == 0) ? Result<void> { } : std::unexpected(Error::kServerFailure);
}

} // namespace jackmini::client

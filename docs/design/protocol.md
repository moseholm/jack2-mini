# Client–server protocol and shared memory (P2 specification)

Status: proposed for implementation. Nothing here is implemented yet. The
current code is the reference for what must keep working, and every
replacement is named against the current code.

## Scope

This specifies the messages between a `jackmini` client and the server, and
the shared-memory structures they use. It replaces the wire format in
`common/JackRequest.h`, `common/JackNotification.h`, and the channels in
`common/JackChannel.h`, and the shared structures listed in section 4. The
architecture does not change: a server process, clients on a Unix socket,
a shared-memory graph, and futex synchronization.

Compatibility with stock JACK is not a goal, so the layout and message numbers
can change.

## 1. Processes and channels

| Channel | Direction | Carries |
|---|---|---|
| Control socket | client → server, with a reply | requests (section 3), each answered by one reply |
| Notification socket | server → client | notifications (section 3.2) |
| Shared memory | both | audio buffers and the port array, engine control, transport, client control, activation counts (section 4) |
| Futex words | both | cycle wake-up and client completion (section 5) |

The two sockets are separate. Requests and replies are never mixed with
notifications, so a client that is busy in its realtime thread cannot delay a
reply, and a notification cannot be taken for a reply.

## 2. Framing and handshake

Every message on either socket has the same header, 16 bytes:

```
struct MsgHeader {
    std::uint32_t magic;    // constant, rejects a stray connection
    std::uint16_t version;  // protocol version, see below
    std::uint16_t type;     // message type, section 3
    std::uint32_t length;   // payload length in bytes, not including this header
    std::uint32_t request;  // request id, echoed in the reply; 0 for notifications
};
static_assert(sizeof(MsgHeader) == 16);
```

- Integers are fixed width. Each payload struct has explicit padding and a
  `static_assert` on its size and on the offset of every field.
- Variable-length data (names, parameter strings) is a length-prefixed byte
  string at the end of the payload.
- The client sends its version in the first request. The server answers with
  its own version. If they differ, the server refuses the connection with
  `Error::Protocol`. There is no negotiation.

## 3. Messages

### 3.1 Requests

Each request is answered by exactly one reply, which has the same request id.
The reply carries a status (`0` for success, otherwise an error code that maps
to `jackmini::Error`) and the reply payload.

Current requests (`JackRequest::RequestType`, 40 types) and what happens to
each:

| Current | New | Notes |
|---|---|---|
| kClientOpen, kClientClose, kClientCheck | keep | `Client::open`, `close`, and the name check |
| kActivateClient, kDeactivateClient | keep | section 6 for what deactivate does |
| kRegisterPort, kUnRegisterPort, kPortRename | keep | |
| kConnectPorts, kConnectNamePorts, kDisconnectPorts, kDisconnectNamePorts, kDisconnectPort | keep, merged | one connect and one disconnect by port id, plus by name |
| kGetPortConnections, kGetPortNConnections | keep | |
| kSetBufferSize, kSetFreeWheel, kSetClientCapabilities | keep | |
| kSetTimeBaseClient, kSetTimebaseCallback, kReleaseTimebase | keep | transport |
| kComputeTotalLatencies | keep | |
| kGetInternalClientName, kInternalClientHandle, kInternalClientLoad, kInternalClientUnload | keep | used by internal clients and the control API |
| kGetClientByUUID, kGetUUIDByClient, kReserveClientName | keep | `jackmini::Uuid` |
| kNotification | keep | a client's request to register notification interest |
| kSessionNotify, kSessionReply, kClientHasSessionCallback | **drop** | session API is dropped |
| kPropertyChangeNotify | **drop** | metadata is dropped |

The 40 current types become about 30. The numbers are reassigned.

### 3.2 Notifications

Server → client, on the notification socket. Current kinds (`NotificationType`,
21 used) and the new set:

| Current | New | Notes |
|---|---|---|
| kAddClient, kRemoveClient, kActivateClient | keep | |
| kXRunCallback, kGraphOrderCallback, kBufferSizeCallback, kSampleRateCallback | keep | |
| kStartFreewheelCallback, kStopFreewheelCallback | keep | |
| kPortRegistrationOnCallback, kPortRegistrationOffCallback | keep | |
| kPortConnectCallback, kPortDisconnectCallback, kPortRenameCallback | keep | |
| kRealTimeCallback, kShutDownCallback, kQUIT | keep | |
| kLatencyCallback | keep | |
| kSecondarySampleRateCallback | keep | |
| kSessionCallback, kPropertyChangeCallback | **drop** | |

Notifications are sent with a bounded per-client queue. If a client does not
read them, the server drops the oldest one and counts the drop. A client
learns of drops from a `kDropped` notification (new) with the count, instead of
the server blocking on it.

## 4. Shared-memory structures

### 4.1 Segment handoff: replacing the registry

**What the registry is.** `/dev/shm/jack-shm-registry` is a shared-memory
segment that the server and its clients use to find the other segments. It
has three parts (`common/shm.h`):

- a header, with a magic number and an array of `MAX_SERVERS` (8) slots, one
  per server name that is running;
- a table of `MAX_SHM_ID` (256) entries. Each entry records one segment: its
  name, size, the PID of the process that created it, and its index;
- a lock, taken with a semaphore, around every change to the table.

A process that wants a segment asks the registry for its index, then maps the
segment by name. The server creates the registry the first time it starts,
and it removes a server's entries when that server closes cleanly.

**Why it is a problem.** A server that is killed does not run its cleanup, so
its entries stay in the table, and its slot stays taken. Once eight servers
have been killed this way, every new server fails with "Too many servers
already active". The P0 startup failures were this. A segment that was not
removed also keeps its memory, which is how a 107 MB segment was left behind
after one killed run.

**One server or several.** The registry's server table exists only because
several servers can run at once under different names. If jackmini runs one
server per machine, that table goes. The single-instance rule can then be a
lock on the server's socket path. Clients still find the server by its socket,
and they still get their audio segments by descriptor. Open: do we keep server
names for running more than one?

**What replaces it.** Nothing global. The server passes each client the file
descriptors it needs when the client opens, and the client maps them. A killed
server leaves no table entries, because there is no table: its segments go when
the last process holding them exits.

New rule: the server creates each segment with a name, passes its file
descriptor to the client over the control socket with `SCM_RIGHTS` in the
reply to `kClientOpen`, and the client maps it with `mmap`. The registry is
removed. A server that dies leaves only the segment names, which are removed
when the last reference closes or when the next server starts with the same
name.

### 4.2 Layout rules

- **No `PRE_PACKED_STRUCTURE` or packed attributes.** Every shared structure
  is standard layout with natural alignment. Field offsets and sizes are
  checked with `static_assert`, not with the packed-struct workarounds.
- **Fixed-width types only.** `std::uint32_t`, `std::int64_t`, and so on.
  No `bool` in shared memory, and no enum without a fixed underlying type.
- **Atomics are `std::atomic<T>`** for the types that are always lock-free on
  the target (checked with `static_assert(std::atomic<T>::is_always_lock_free)`).
  This is the first time they can be used in shared memory directly, because
  the layout is ours to change.
- **Fixed-size arrays with a compile-time bound** wherever the current code
  uses a variable size. The port count is fixed at `PORT_NUM` (section 4.4).

### 4.3 The structures

| Current | Contents | New |
|---|---|---|
| `JackClientControl` | name, callback flags, transport state, activation | keep, aligned; callback flags as `std::uint8_t[]` |
| `JackEngineControl` | period, sample rate, CPU statistics, rolling usecs | keep, aligned; statistics as `std::atomic<std::uint64_t>` and `std::atomic<float>` |
| `JackGraphManager` | port array, client timing, connection state | keep, see 4.4 |
| `JackConnectionManager` | connection matrix, activation counts | keep, see 4.5 |
| `JackPort` | name, aliases, type, flags, buffer offset | keep, aligned |
| `JackTransportEngine` | transport state and position | keep, aligned |
| `JackClientTiming` | signaled, awake, finished times and status | keep, times as `std::uint64_t` at aligned offsets |
| `JackActivationCount` | activation value and count | keep, see 4.5 |
| `JackAtomicState`, `JackAtomicArrayState` | two- and three-buffer switching protocols | keep the protocol, see 4.5 |

### 4.4 Graph segment

Audio buffers are part of the port array: each `JackPort` holds its own buffer
of `BUFFER_SIZE_MAX` (8192) samples. So audio already travels through shared
memory, and the realtime thread reads and writes the buffers in place. The new
layout keeps this. At `PORT_NUM` (2048) ports that is about 67 MB of buffer
storage, most of the 107 MB segment seen in the baseline.

The port array has a fixed maximum, `PORT_NUM` (2048 ports), and is allocated
in the graph segment when the server starts. It is never reallocated, so
there is no port generation counter, and the realtime thread reads the port
table without any check. The maximum sets the size of the connection matrix,
so the segment size is fixed too. The current code allocates the same way, so
the cost does not change; only the reallocation path is removed.

### 4.5 The copy-on-write counters, redesigned

The current `JackAtomicState` copies the whole connection state into the next
buffer while the realtime thread is still decrementing activation counts in
the current one. The copy can capture a counter mid-decrement, and TSAN reports
it. The copy is not safe, and making each read atomic does not fix it.

New design: split the state into two parts.

- **Configuration** (connections, client list, counts of inputs per client).
  Written only by the control path, copied on write, and switched atomically as
  now. The copy reads only configuration, which the realtime thread never
  writes, so the copy is race-free.
- **Live counters** (activation values for the current cycle). Kept outside the
  copied buffer, in a separate array indexed by client. The realtime thread
  sets them at the start of a cycle from the configuration, and decrements them
  as clients finish. Nothing copies them.

Each live counter is a `std::atomic<std::int32_t>`, so the decrements are
atomic and TSAN sees them.

### 4.6 CPU statistics

`JackEngineControl`'s statistics are misaligned in the current packed layout.
In the new layout they are aligned atomics, so the statistics can be read from
any process without a lock. Readers get a consistent value per field, not a
consistent set of fields.

## 5. Synchronization

- Each client has a futex word in shared memory, as now. The engine wakes the
  client for a cycle by setting the word and calling `futex_wake`. The client
  reports completion by decrementing its activation count, and the last client
  to finish wakes the engine.
- The futex code is wrapped in C++ with the same wait and wake semantics. The
  futex word is a `std::atomic<std::uint32_t>` in shared memory.
- The engine never blocks on a client in the cycle. A client that does not
  finish in time is an xrun, as now.

## 6. Deactivate keeps connections

`deactivate` changes the server in two ways:

1. The engine stops waiting for the client. Its activation count is no longer
   part of the cycle.
2. Its ports stay connected. The engine skips them: an inactive client's input
   ports receive silence, and its output ports are not mixed into any input.
   No audio is routed through an inactive client.

Clients are not notified of disconnection, because nothing was disconnected.
A `kActivateClient` notification is sent on the next `activate`.

Tests for this rule are part of the P2 gate. They must show, with the existing
graph tests extended: connections survive a deactivate; audio does not pass
through an inactive client; the cycle continues without the client; and a
reactivate restores the audio path on the next cycle.

## 7. Errors

Reply status is a `std::uint32_t`. Zero is success. Every other value is one of
the `jackmini::Error` codes, so the enum values are fixed in this spec:

| Code | Error |
|---|---|
| 1 | InvalidArgument |
| 2 | InvalidState |
| 3 | NameTaken |
| 4 | NotFound |
| 5 | NoServer |
| 6 | ServerFailure |
| 7 | Protocol |
| 8 | Io |

`Io` is set by the client side when the socket fails, not sent by the server.

## 8. Testing

- **Fake server.** A test double that speaks this protocol over a `socketpair`,
  with the shared memory replaced by anonymous mappings created in the test.
  It implements the message handlers as state machines, so it can be driven
  to every reply and notification.
- **Conformance tests** for each message: a well-formed request gets its reply;
  a malformed request gets `Protocol`; a version mismatch is refused.
- **Static checks** for every shared structure: size, alignment, and the
  offset of each field.
- **The graph tests** of section 6, run against the real server code.
- **TSAN** on the fake-server tests and on the in-process server test.

## 9. Open questions

1. **Registry removal (section 4.1).** Decided: remove it.
2. **Port count (section 4.4).** Decided: fixed maximum, `PORT_NUM`.
3. **Notification queue size.** The bound is not chosen. It should be large
   enough for a burst of port registrations during startup, and that is to be
   measured.

## 10. Order of work

1. Static layout checks for the new structures, with the packed attributes
   removed.
2. Segment handoff with `SCM_RIGHTS` and no registry.
3. Message framing and the handshake.
4. Requests, then notifications.
5. Counter redesign (section 4.5), with its TSAN test.
6. Deactivate with connections (section 6), with the graph tests.
7. The fake server and the conformance tests.

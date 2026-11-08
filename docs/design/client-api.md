# C++ client API (P1 design)

Status: decided. The headers are written in `common/jackmini/` and compile
standalone; the implementation is not started.

## Scope

The current client library exports about 179 C functions (`common/jack/*.h`
plus `JackAPI.cpp`, `JackLibAPI.cpp`), and the control API adds 52 more
(`jackctl_*`). The new API is C++23, in namespace `jackmini`, with no
`extern "C"` exports. The architecture stays the same: the client connects to
the server over a Unix socket, the graph lives in shared memory, and the sync
primitives are futexes.

## Decisions

| Question | Decision |
|---|---|
| Namespace | `jackmini` |
| Control API (`jackctl_*`) | Kept, as `jackmini::control`. An application can start and manage the server with it, instead of running `jackd`. |
| Session API | Dropped. |
| Internal clients | Use the same client API as external clients. |
| Metadata | Dropped. The current functions are stubs that return `-1`, and there is no database build. |
| Lifecycle | Opened, active, inactive. See below. |
| Connections across deactivate | Kept. This is a server change, planned for P2, with a risk noted below. |

## Lifecycle

Construction and activation are separate.

1. **Opened.** `jackmini::Client::open(name, options)` registers the client.
   Ports and callbacks can be set up. The graph is not running.
2. **Active.** `activate()` starts the realtime thread, and the process
   callback runs each cycle. Callbacks cannot be set in this state.
3. **Inactive.** `deactivate()` stops the realtime thread. Callbacks can be set
   again, and `activate()` can be called again. A new process callback set
   while inactive takes effect at the next `activate()`.

The destructor deactivates the client if it is active, and then closes it.
`close()` is available as an explicit call, and it reports errors.

Rules that follow from this:

- Setting a callback while active returns `Error::InvalidState`. The current
  library returns `-1` from 18 setters with the same restriction, and the new
  API keeps the rule.
- `deactivate()` joins the realtime thread before it returns. The current code
  calls `fThread.Kill()`, which does not wait for the thread. The new code
  must, so that a callback cannot change while the thread is still running it.
- Port handles stay valid across deactivate. Ports are not unregistered on
  deactivate.
- Connections are kept across deactivate. The current server disconnects every
  port of a deactivated client (`JackEngine::ClientDeactivate`). Keeping them
  means changing the server, which is part of P2. The engine must treat an
  inactive client's ports as silent, so no audio goes through them.

## Surface

| Area | Current | New |
|---|---|---|
| Client lifecycle | `jack_client_open/close`, `jack_activate/deactivate` | `Client` (RAII). `open`, `activate`, `deactivate`, `close`. |
| Ports | `jack_port_register/unregister`, `jack_port_get_*`, aliases, connections | `Port` handle; `Client::register_port(name, type, flags)`. Aliases and connections are methods on the client. |
| Audio buffers | `void* jack_port_get_buffer(port, n)` | `std::span<float>`, valid for the cycle only. |
| MIDI | `jack_midi_*` (8 functions) | `midi::Buffer`, a span view of events. |
| Process callback | `jack_set_process_callback(fn, arg)` | `Client::on_process(F)`. The callable is stored while inactive. Calling it does not allocate. Returns `bool`. |
| Other callbacks | `jack_set_*_callback` (xrun, shutdown, buffer size, sample rate, latency, and so on) | `Client::on_xrun`, `on_shutdown`, and so on, with the same storage rule. |
| Transport | `jack_transport_*` (1 header) | `Transport` member functions. |
| Ring buffer | `jack_ringbuffer_*` (3 functions) | `RingBuffer<T>`, an SPSC ring with `std::atomic`. Part of the client API because clients use it. |
| UUID | `jack_uuid_*` (10) | `Uuid`, a value type. |
| Statistics | `jack_cpu_load`, and so on | `Client::cpu_load()` and the like. |
| Control API | `jackctl_*` (52) | `jackmini::control`. Same server-management operations, C++ types. |

## Error handling

- Setup calls return `std::expected<T, jackmini::Error>`. `Error` is an
  `enum class` with the failure reasons the server reports today, including
  `InvalidState`, plus a message.
- Calls on the RT path (`Port::buffer()`, `Client::frame_time()`, and the
  like) do not return errors. They are valid only inside the process callback.
- No exceptions are thrown across the callback boundary.

## Callbacks and the RT rules

- Each callback is a `std::move_only_function` set while inactive. Setting it
  may allocate. Calling it does not.
- The process callback runs on the client's RT thread. The library creates that
  thread with `pthread` (SCHED_FIFO), not `std::jthread`.
- The process callback may not allocate, lock, or call the server. In a debug
  build the library checks the allocation count inside the cycle.

## Ownership and lifetime

- `Client` owns the socket, the shared-memory mappings, and the threads.
- `Port` is a value handle. It is valid until the client closes or the port is
  unregistered.
- Buffers from `Port::buffer()` are valid only for the cycle they were
  obtained in.

## Risks

- **Connections across deactivate** need a server change in the engine's graph
  code. An inactive client's ports must be skipped in every cycle, and the
  change must be tested against the existing graph tests before P2 can close.

## Next steps

1. Done: the headers are in `common/jackmini/`, and `tests/jackmini_api_unit.cpp`
   checks the types and overloads at compile time.
2. Write the tests for the surface against a fake server, so they run without
   audio hardware.
3. Port the client library behind the new headers, one area at a time, with
   the existing `tests/*.cpp` programs ported as they go.

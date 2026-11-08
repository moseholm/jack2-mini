# C++ client API (P1 design)

Status: draft for review. Nothing here is implemented yet.

## Scope

The current client library exports about 179 C functions (`common/jack/*.h`
plus `JackAPI.cpp`, `JackLibAPI.cpp`), and the control API adds 52 more
(`jackctl_*`). The new API is C++23, in namespace `jack`, with no `extern "C"`
exports. The architecture stays the same: the client connects to the server
over a Unix socket, the graph lives in shared memory, and the sync primitives
are futexes.

## Surface

| Area | Current | New |
|---|---|---|
| Client lifecycle | `jack_client_open/close`, `jack_activate/deactivate` | `jack::Client` (RAII). `Client::open(name, Options) -> std::expected<Client, Error>`. Destructor deactivates and closes. |
| Ports | `jack_port_register/unregister`, `jack_port_get_*`, aliases, connections | `jack::Port` handle; `Client::register_port(name, type, flags)`. Aliases and connections are methods on the client. |
| Audio buffers | `void* jack_port_get_buffer(port, n)` | `std::span<float>`, valid for the cycle only. |
| MIDI | `jack_midi_*` (8 functions) | `jack::midi::Buffer`, a span view of events. |
| Process callback | `jack_set_process_callback(fn, arg)` | `Client::on_process(F)`. The callable is stored at setup, and the call is a plain call with no allocation. Returns `bool`. |
| Xrun, shutdown, buffer size, sample rate, latency, graph order callbacks | `jack_set_*_callback` | `Client::on_xrun`, `on_shutdown`, and so on, with the same setup-time storage. |
| Transport | `jack_transport_*` (1 header) | `jack::Transport` member functions. |
| Ring buffer | `jack_ringbuffer_*` (3 functions) | `jack::RingBuffer<T>`, an SPSC ring with `std::atomic`. Part of the client API because clients use it. |
| UUID | `jack_uuid_*` (10) | `jack::Uuid`, a value type. |
| Metadata | `jack_*_property` (15, all stubs) | Removed. The library returns `-1` for all of them today, and there's no database build. |
| Session | `jack_session_*` (6) | Open question, see below. |
| Control API | `jackctl_*` (52) | Open question, see below. |
| Statistics | `jack_cpu_load`, and so on | `Client::cpu_load()` and the like. |

## Error handling

- Setup calls return `std::expected<T, jack::Error>`. `Error` is an `enum class`
  with the failure reasons the server reports today, plus a message.
- Calls on the RT path (`Port::buffer()`, `Client::frame_time()`, and the
  like) do not return errors. They are valid only inside the process callback.
- No exceptions are thrown across the callback boundary.

## Callbacks and the RT rules

- Each callback is a `std::move_only_function` set during setup. Setting it
  may allocate. Calling it does not.
- The process callback runs on the client's RT thread. The client library
  creates that thread with `pthread` (SCHED_FIFO), not `std::jthread`.
- The process callback may not allocate, lock, or call the server. The
  library enforces this for its own calls (a debug build checks the
  allocation count inside the cycle).

## Ownership and lifetime

- `Client` owns the socket, the shared-memory mappings, and the threads.
- `Port` is a value handle. It is valid until the client closes or the port
  is unregistered.
- Buffers from `Port::buffer()` are valid only for the cycle they were
  obtained in.

## Open questions

1. **Control API (`jackctl_*`).** It is used by server frontends to start,
   stop, and configure the server. Keep it as `jack::control`, or drop it? A
   server needs no client-side API to run, but a GUI does.
2. **Session API.** Keep or drop? It has no known users in this project.
3. **Internal clients.** They use the same client API on the server side, so
   their API follows the same design. Confirm.
4. **Naming.** `jack::` as the namespace, or `jackpp::`? `jack::` is used
   below.

## Next steps (after review)

1. Write `common/jack/*.hpp` for the surface above. No implementation yet.
2. Write the tests for the surface against a fake server, so they run without
   audio hardware.
3. Port the client library behind the new headers, one area at a time, with
   the existing `tests/*.cpp` programs ported as they go.

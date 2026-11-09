# jackmini code style

jackmini is the name of the whole project: the client library, the server, the drivers, and the tools. The code uses one
style everywhere. The client code (`client/jackmini/`) is the reference.

## Names

- Namespace `jackmini`. The server is in `jackmini::server`, the drivers in `jackmini::driver`, the shared memory in
  `jackmini::shm`, the graph in `jackmini::graph`, and logging in `jackmini::log`. There is no `Jack` namespace and no
  `Jack` prefix on internal names.
- Types are CamelCase: `Client`, `Port`, `Error`, `Cycle`, `Options`.
- Methods and free functions are snake_case: `register_port`, `activate`, `buffer_size`.
- Constants are `kCamelCase`. Macros are not used for constants.
- Files are lowercase `snake_case`, named after the main type they hold: `client.hpp`, `port.hpp`, `port_table.hpp`.
- The public C names that the library keeps for its users (`jack_ringbuffer_*`, `jack_midi_*`) are the only names with a
  `jack_` prefix. They are listed in `docs/design/client-api.md`.

## Errors

- Fallible functions return `Result<T>`, which is `std::expected<T, Error>`. `Error` is an `enum class` with a
  `to_string` function.
- Exceptions are used on the control plane of the server only. The realtime cycle does not throw and does not allocate.
- An error is reported once, where it is detected. No function prints an error and also returns it.

## Ownership

- Resources (file descriptors, shared memory, threads, the client handle) have owning types with RAII. Each has a clear
  close or join, and the destructor does it when the caller has not.
- Shared memory layouts are plain structs. Each has a `static_assert` for its size, alignment, and the offset of each
  field that another process reads.

## Callbacks and data

- Callbacks are `std::move_only_function` with a typed signature, such as `bool(const Cycle&)`.
- Buffers are `std::span`. Names are `std::string_view` where the caller keeps the storage.
- Atomics on shared fields use `std::atomic_ref` or `std::atomic` in the shared structures.

## Realtime

- The realtime cycle does not allocate, take a lock, or make a system call, except the futex wait and wake that the
  design requires. Each such call is named in the code where it is made.
- A realtime function is marked with a comment saying so.

## Comments

- A comment says why, not what. Behaviour that is not obvious from the code is described once, at the place it happens.

## Headers

- A header starts with `#pragma once`, not an include guard.

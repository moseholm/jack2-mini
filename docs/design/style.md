# jackmini code style

jackmini is the name of the whole project: the client library, the server, the drivers, and the tools. The code uses one
style everywhere. The base is the WebKit C++ style. The tweaks are in `.clang-format` at the top of the repository, and
the file is the reference for layout: run `clang-format -i` on the files you change.

## Names

- Namespace `jackmini`, with the nested namespaces `jackmini::alsa`, `jackmini::client`, `jackmini::control`,
  `jackmini::midi` and `jackmini::tools`. Namespaces are lowercase.
- Types are CamelCase, without a `Jack` prefix: `Client`, `Port`, `Error`, `Cycle`, `Options`, `GraphManager`.
- Functions and methods are camelCase: `registerPort`, `activate`, `bufferSize`, `toString`.
- Enumerators and constants are `kCamelCase`: `kNoPort`, `kClientNum`, `kInvalidState`. Constants are `constexpr`, not
  macros.
- Data members have an `m_` prefix: `m_name`, `m_refNum`. This holds for classes and for structs. Static data members
  have an `s_` prefix: `s_verbose`. Namespace-scope variables have a `g` prefix: `gPortTypes`.
- Locals and parameters are camelCase.
- Files are CamelCase and named after the main type they hold: `Client.hpp`, `ServerPort.h`, `PcmCycle.hpp`. Tests are
  named the same way: `MemopsUnit.cpp`.
- The public C API headers in `common/jack/` keep their file names, and the functions declared there keep their names,
  because plugins and programs look them up by name: `jack_ringbuffer_read`, `jack_midi_event_get`. The logging functions
  `jack_error`, `jack_info` and `jack_log`, and the `jack_slist_*` list helpers, keep their names too. The public C names
  that the library keeps for its users (`jack_ringbuffer_*`, `jack_midi_*`) are listed in `docs/design/client-api.md`.

## Types and syntax

- Integers are the fixed-width types from `<cstdint>`: `std::int32_t`, `std::uint32_t`, `std::int64_t`, `std::uint64_t`.
  `int` is written as `std::int32_t`, except as the return type of `main`. `bool` stays `bool`.
- `char` is for text only: C strings, names, format strings. Raw bytes, such as audio samples and shared memory, are
  `std::byte`. Counters that are byte-sized are `std::uint8_t`.
- Fixed-size arrays are `std::array`. A trailing array that is sized at run time (the struct hack) stays a C array.
- `nullptr`, never `NULL`.
- Macros are not used for constants. The macros that remain are the ones C++ cannot replace: export and attribute
  macros, feature-test and system macros, and the statement macros that return from the caller or use `#` and `##`
  (`CHECK`, `ThrowIf`, `CheckRes`, `CheckRead`, the constraint-enum generators).
- Comments are `//` comments, also for the license header. There are no `/* */` comments.

## Errors

- Fallible functions return `Result<T>`, which is `std::expected<T, Error>`. `Error` is an `enum class` with a
  `toString` function.
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
- A header includes what it uses (`<cstdint>`, `<array>`, `<cstddef>` for `std::byte`), and does not rely on the files
  that happen to be included before it.

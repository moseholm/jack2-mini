# 0002: One server per user, enforced by a kernel-held lock

Status: Accepted

## Context

One audio graph and one clock domain should own the hardware for a user session. A server name is only a socket name, so
checking it does not prevent a second `jackd` from starting under another name. After a crash, stale socket files must
not block the next start; ownership therefore has to live in the kernel, not in a file's existence.

## Decision

`Server::start()` acquires a kernel `flock` on `/dev/shm/jack_<uid>_server.lock` before it creates the engine or opens
the driver. If another process holds the lock, startup is refused with `Error::kInvalidState`; the lock is released when
the descriptor is closed, including on process death. Source:
[ServerLock.cpp](../../../server/core/ServerLock.cpp#L42),
[Control.cpp](../../../server/control/Control.cpp#L556).

```mermaid
sequenceDiagram
    autonumber
    participant Jackd as jackd
    participant Server as control::Server
    participant Lock as ServerLock
    participant File as /dev/shm/jack_&lt;uid&gt;_server.lock
    Jackd->>Server: start(freewheel)
    Server->>Lock: acquireServerLock(kServerDir)
    Lock->>File: open(O_RDWR|O_CREAT|O_CLOEXEC), flock(LOCK_EX|LOCK_NB)
    alt another server holds the lock
        File-->>Lock: EWOULDBLOCK
        Lock-->>Server: std::errc::file_exists
        Server-->>Jackd: Error::kInvalidState ("server already active")
    else lock acquired
        File-->>Lock: 0
        Server->>Server: construct engine, open driver, bind sockets
        Note over Server: descriptor held for the server's lifetime
    end
```

*Source: [Control.cpp](../../../server/control/Control.cpp#L541),
[ServerLock.cpp](../../../server/core/ServerLock.cpp#L42),
[Constants.h](../../../ipc/protocol/Constants.h#L60).*

| Lock property | Value | Source |
|---|---|---|
| Path | `{dir}/jack_<uid>_server.lock`, with `dir = /dev/shm` | [ServerLock.cpp](../../../server/core/ServerLock.cpp#L44), [Constants.h](../../../ipc/protocol/Constants.h#L61) |
| Operation | `flock(fd, LOCK_EX \| LOCK_NB)` | [ServerLock.cpp](../../../server/core/ServerLock.cpp#L52) |
| Already-held result | `EWOULDBLOCK` → `std::errc::file_exists` → `Error::kInvalidState` | [ServerLock.cpp](../../../server/core/ServerLock.cpp#L53), [Control.cpp](../../../server/control/Control.cpp#L559) |
| Release | `gLockFd.reset()` closes the owning descriptor | [ServerLock.cpp](../../../server/core/ServerLock.cpp#L32), [ServerLock.cpp](../../../server/core/ServerLock.cpp#L63) |

*Source: [ServerLock.cpp](../../../server/core/ServerLock.cpp#L42).*

## Consequences

- The rule is per user, not per server name: a second server is refused even with a different `-n` name. Source:
  [ServerLock.cpp](../../../server/core/ServerLock.cpp#L44).
- A crashed server cannot leave the lock held; socket files from the old instance are still cleaned up explicitly after
  the lock is acquired. Source: [Control.cpp](../../../server/control/Control.cpp#L568).
- Test harnesses must stop any other `jackd` for the user before starting a private server. Source:
  [rtsan_e2e.sh](../../../tests/rtsan_e2e.sh#L70).
- The lock does not reserve the audio devices themselves; other audio users must be stopped separately. Source:
  [README.rst](../../../README.rst#L67).

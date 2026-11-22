# 0004: Shared memory is handed off as file descriptors, not by a registry

Status: Accepted

## Context

The graph, engine control and per-client control live in shared memory that both processes map. A named registry of
segments would need cleanup rules for crashed clients and servers, and would expose those segments to unrelated clients.
Unix sockets can pass descriptor rights directly with each `ClientOpen` reply, which avoids both problems.

## Decision

The server allocates each segment with `memfd_create(..., MFD_CLOEXEC)`, sizes it with `ftruncate` and maps it
`MAP_SHARED`. The `ClientOpen` reply carries exactly three descriptors — engine control, graph, client control — using
`SCM_RIGHTS`. The client receives them with `MSG_CMSG_CLOEXEC`, maps each one, then closes the received descriptors;
mappings keep the segments alive. There is no name registry. Source:
[ShmMem.cpp](../../../ipc/memory/ShmMem.cpp#L55),
[FdSegment.cpp](../../../ipc/memory/FdSegment.cpp#L71),
[ClientSession.cpp](../../../client/session/ClientSession.cpp#L176).

```mermaid
sequenceDiagram
    autonumber
    participant Srv as Server (ShmMem)
    participant Memfd as memfd_create segments
    participant Sock as AF_UNIX request socket
    participant Cli as ClientSession

    Srv->>Memfd: create + ftruncate + mmap(MAP_SHARED)
    Note over Srv: engine control, graph, per-client control
    Srv->>Sock: ClientOpenResult + SCM_RIGHTS(fd[0..2])
    Sock->>Cli: recvWithFds (MSG_CMSG_CLOEXEC)
    Cli->>Cli: map engine, graph, client control
    Cli->>Cli: close received descriptors; mappings remain
    Cli->>Cli: connect per-client futex
    Note over Cli: no registry and no named segment table
```

*Source: [ShmMem.cpp](../../../ipc/memory/ShmMem.cpp#L55),
[FdSegment.cpp](../../../ipc/memory/FdSegment.cpp#L112),
[Request.h](../../../ipc/protocol/Request.h#L420),
[ClientSession.cpp](../../../client/session/ClientSession.cpp#L176).*

| Handoff property | Value | Source |
|---|---|---|
| Allocation | `memfd_create(name, MFD_CLOEXEC)` + `ftruncate` | [FdSegment.cpp](../../../ipc/memory/FdSegment.cpp#L148) |
| Mapping | `mmap(..., PROT_READ \| PROT_WRITE, MAP_SHARED, fd, 0)` | [FdSegment.cpp](../../../ipc/memory/FdSegment.cpp#L161) |
| Descriptor passing | `SCM_RIGHTS` on the request socket | [FdSegment.cpp](../../../ipc/memory/FdSegment.cpp#L94) |
| Descriptor limit per message | 8 (`kMaxFds`) | [FdSegment.cpp](../../../ipc/memory/FdSegment.cpp#L39) |
| Descriptors in `ClientOpen` | Exactly 3, in engine/graph/client order | [Request.h](../../../ipc/protocol/Request.h#L420), [ClientSession.cpp](../../../client/session/ClientSession.cpp#L182) |
| Receive flags | `MSG_CMSG_CLOEXEC` | [FdSegment.cpp](../../../ipc/memory/FdSegment.cpp#L129) |

*Source: [FdSegment.cpp](../../../ipc/memory/FdSegment.cpp#L148),
[ClientSession.cpp](../../../client/session/ClientSession.cpp#L182).*

## Consequences

- Segment lifetime follows the mappings and the kernel's descriptor references; there is no registry entry to leak or
  clean up. Source: [ShmMem.cpp](../../../ipc/memory/ShmMem.cpp#L83).
- Only the client that receives the descriptors can map those segments; access control is the descriptor, not a pathname.
  Source: [FdSegment.cpp](../../../ipc/memory/FdSegment.cpp#L129).
- The eight-descriptor message limit is generous for three segments but caps the protocol if more are added later.
  Source: [FdSegment.cpp](../../../ipc/memory/FdSegment.cpp#L39).
- Descriptor receipt, mapping and futex connection are all failure points in `connectAndRegister`, each mapped to an
  error rather than ignored. Source: [ClientSession.cpp](../../../client/session/ClientSession.cpp#L181).
- The wire change breaks stock JACK2 interoperability; see [0001](0001-no-jack2-compatibility.md). Source:
  [README.rst](../../../README.rst#L172).

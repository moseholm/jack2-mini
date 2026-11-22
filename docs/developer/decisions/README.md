# Architecture decisions

Short records in the style introduced by Michael Nygard: **context**, **decision**, **consequences**. A record says why
a choice was made and what it costs; it does not re-describe the implementation. Status is `Accepted` unless stated
otherwise.

| ADR | Decision |
|---|---|
| [0001](0001-no-jack2-compatibility.md) | No compatibility with stock JACK2 clients or servers |
| [0002](0002-one-server-per-user.md) | One server per user, enforced by a kernel-held lock |
| [0003](0003-alsa-only.md) | ALSA is the only hardware backend; MIDI is client-to-client |
| [0004](0004-fd-shm-handoff.md) | Shared memory is handed off as file descriptors, not by a registry |
| [0005](0005-split-counters.md) | Copy-on-write configuration is split from live activation counters |
| [0006](0006-realtime-contract.md) | The cycle contract is enforced by annotation, review and sanitizers |
| [0007](0007-cxx23-client-api.md) | The public client surface is C++23 with no installed C API |

## Template

```markdown
# NNNN: Short title

Status: Accepted

## Context
Why the choice was needed.

## Decision
What was chosen, in one or two sentences.

## Consequences
What becomes easier, what becomes harder, and what must be tested.
```

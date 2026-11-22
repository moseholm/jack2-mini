# Contributor and developer documentation

Start with [the architecture overview](architecture/01-context-and-goals.md), then follow the reading path that matches your task.

| Task | Read |
|---|---|
| Understand the system boundary and non-goals | [01 context and goals](architecture/01-context-and-goals.md), [02 solution strategy](architecture/02-solution-strategy.md) |
| Find where code lives and how it is built | [03 building blocks](architecture/03-building-blocks.md) |
| Change startup, cycle, client lifecycle or shutdown | [04 runtime view](architecture/04-runtime-view.md) |
| Change the API, sockets or notifications | [05 interfaces](architecture/05-interfaces.md), [protocol reference](../reference/protocol.md), [client API reference](../reference/client-api.md) |
| Change shared memory, graph or synchronisation | [06 memory and sync](architecture/06-memory-and-sync.md) |
| Change ALSA, secondary cards or resampling | [07 ALSA driver](architecture/07-alsa-driver.md) |
| Check RT constraints, tests or known risks | [08 quality and validation](architecture/08-quality-and-validation.md), [testing](testing.md) |
| Understand a decision | [decisions](decisions/README.md) |
| Follow naming and layout rules | [style](style.md) |
| Look up a term | [09 glossary](architecture/09-glossary.md) |

Every document in this track is diagram-led. Diagram captions cite the code that defines what is shown.

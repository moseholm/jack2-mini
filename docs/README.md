# jack2-mini documentation

This is the documentation hub. It has two reading tracks and one reference shelf:

- **[Users and operators](user/README.md)** — build, configure, run, observe and troubleshoot a server.
- **[Contributors and developers](developer/README.md)** — architecture, runtime design, code structure and extension points.
- **[Reference](reference/)** — verified protocol and client-API fact sheets. These are the canonical statements of the
  wire contract and the public C++ surface.

Historical design and status records live in [archive/](archive/README.md). Raw measurement output lives in
[measurements/](measurements/README.md). Neither is authoritative when it disagrees with this guide.

## How this documentation is organised

The structure follows established architecture-documentation practice, adapted to a small realtime/embedded system:

| Source | Use here |
|---|---|
| [arc42](https://arc42.org/overview) (Hruschka & Starke, 2005) | Backbone: goals, context, solution strategy, building blocks, **runtime view**, cross-cutting concepts, decisions, quality, risks, glossary. The mapping below shows where each chapter lives. |
| [C4 model](https://c4model.com/) (Simon Brown) | System context and container/component diagrams in [01](developer/architecture/01-context-and-goals.md) and [03](developer/architecture/03-building-blocks.md). Code-level C4 is deliberately omitted. |
| [Documenting Software Architectures: Views and Beyond, 2nd ed.](https://www.sei.cmu.edu/library/documenting-software-architectures-views-and-beyond-second-edition) (Clements et al., 2010) | Separate interaction, module/component and data views: protocol sequences, source/target structure and shared-memory layout get their own diagrams. |
| Kruchten's 4+1 views (1995) | The scenario view is the set of end-to-end sequence diagrams in [04](developer/architecture/04-runtime-view.md); the process view is the explicit thread and futex ownership model. |
| [Architecture Decision Records](https://cognitect.com/blog/2011/11/15/documenting-architecture-decisions) (Nygard, 2011) | Short context → decision → consequences records in [decisions/](developer/decisions/README.md). |

Mermaid is the only diagram notation. GitHub renders it directly, so no generated images are checked in.

### arc42 coverage

| arc42 chapter | Where |
|---|---|
| 1 Introduction and goals | [01 context and goals](developer/architecture/01-context-and-goals.md) |
| 2 Constraints | [01](developer/architecture/01-context-and-goals.md), [08 quality and validation](developer/architecture/08-quality-and-validation.md) |
| 3 Context and scope | [01](developer/architecture/01-context-and-goals.md) |
| 4 Solution strategy | [02 solution strategy](developer/architecture/02-solution-strategy.md), [decisions](developer/decisions/README.md) |
| 5 Building block view | [03 building blocks](developer/architecture/03-building-blocks.md), [07 ALSA driver](developer/architecture/07-alsa-driver.md) |
| 6 Runtime view | [04 runtime view](developer/architecture/04-runtime-view.md) |
| 7 Deployment view | [01](developer/architecture/01-context-and-goals.md), [user running jackd](user/running-jackd.md) |
| 8 Crosscutting concepts | [06 memory and sync](developer/architecture/06-memory-and-sync.md), [08](developer/architecture/08-quality-and-validation.md) |
| 9 Architecture decisions | [decisions](developer/decisions/README.md) |
| 10 Quality requirements | [08](developer/architecture/08-quality-and-validation.md) |
| 11 Risks and technical debt | [08](developer/architecture/08-quality-and-validation.md), [user troubleshooting](user/troubleshooting.md) |
| 12 Glossary | [09 glossary](developer/architecture/09-glossary.md) |

## Accuracy conventions

- Code is the only authority. Facts were verified against commit `3807a0f2` (the code state at the start of this
  documentation work). Diagrams and constant tables cite `path:line` sources.
- A diagram caption ends with `Source: …` pointing at the code that defines what the diagram shows.
- Tables of exact values include a source column.
- Claims that cannot be derived from source — hardware runs and performance measurements — are labelled
  **recorded observation** and kept under [measurements/](measurements/README.md).
- Statements known to be stale are not repeated. See [known documentation drift](developer/architecture/08-quality-and-validation.md#known-documentation-drift).

## Conventions for maintainers

1. Keep prose short; prefer a diagram or a table.
2. Put `<!-- fact: <path>:<line> pattern="<regex>" -->` before a table or figure whose exact text matters.
3. Put `<!-- source: <path>:<line> -->` or a `Source:` caption on every diagram.
4. Run `python3 docs/scripts/check_docs.py --check` before committing doc changes. It verifies fact anchors, links,
   Mermaid captions and the generated source tree. `--update` refreshes only marked generated regions.
5. New ADRs follow the template in [decisions/README.md](developer/decisions/README.md).

## Diagram index

| Diagram | Document |
|---|---|
| System context | [01](developer/architecture/01-context-and-goals.md) |
| Deployment / runtime artifact placement | [01](developer/architecture/01-context-and-goals.md) |
| Quality-goal map | [01](developer/architecture/01-context-and-goals.md) |
| Decision landscape | [02](developer/architecture/02-solution-strategy.md) |
| C4 container view | [03](developer/architecture/03-building-blocks.md) |
| Component view of server and client | [03](developer/architecture/03-building-blocks.md) |
| CMake target and install graph | [03](developer/architecture/03-building-blocks.md) |
| Source tree (generated) | [03](developer/architecture/03-building-blocks.md) |
| jackd startup sequence | [04](developer/architecture/04-runtime-view.md) |
| Thread and ownership map | [04](developer/architecture/04-runtime-view.md) |
| Client open handshake | [04](developer/architecture/04-runtime-view.md) |
| One engine cycle | [04](developer/architecture/04-runtime-view.md) |
| Activation tree and futex wake | [04](developer/architecture/04-runtime-view.md) |
| Client lifecycle | [04](developer/architecture/04-runtime-view.md) |
| Shutdown path | [04](developer/architecture/04-runtime-view.md) |
| Public API class map | [05](developer/architecture/05-interfaces.md) |
| Request/reply sequence | [05](developer/architecture/05-interfaces.md) |
| Notification queue path | [05](developer/architecture/05-interfaces.md) |
| Driver module ABI | [05](developer/architecture/05-interfaces.md) |
| Shared-memory segments and FD handoff | [06](developer/architecture/06-memory-and-sync.md) |
| Graph segment layout | [06](developer/architecture/06-memory-and-sync.md) |
| Copy-on-write state switch | [06](developer/architecture/06-memory-and-sync.md) |
| Futex wait/wake sequence | [06](developer/architecture/06-memory-and-sync.md) |
| Deactivate data path | [06](developer/architecture/06-memory-and-sync.md) |
| Port buffer layout | [06](developer/architecture/06-memory-and-sync.md) |
| ALSA component map | [07](developer/architecture/07-alsa-driver.md) |
| Primary capture/playback flow | [07](developer/architecture/07-alsa-driver.md) |
| Secondary sample flows | [07](developer/architecture/07-alsa-driver.md) |
| Hot-plug reconcile state machine | [07](developer/architecture/07-alsa-driver.md) |
| Secondary rate-change sequence | [07](developer/architecture/07-alsa-driver.md) |
| Resampler and drift pipeline | [07](developer/architecture/07-alsa-driver.md) |
| Realtime/control boundary | [08](developer/architecture/08-quality-and-validation.md) |
| Test and sanitizer map | [08](developer/architecture/08-quality-and-validation.md) |
| Wire frame layout | [protocol reference](reference/protocol.md) |
| Request/notification classification | [protocol reference](reference/protocol.md) |
| Reply status mapping | [protocol reference](reference/protocol.md) |
| Client lifecycle in the public API | [client API reference](reference/client-api.md) |
| Callback threading | [client API reference](reference/client-api.md) |
| Quickstart workflow | [quickstart](user/quickstart.md) |
| Client/server processes | [quickstart](user/quickstart.md) |
| jackd option handoff | [running jackd](user/running-jackd.md) |
| Runtime artifacts | [running jackd](user/running-jackd.md) |
| Server shutdown sequence | [running jackd](user/running-jackd.md) |
| Primary and secondary topology | [secondary cards](user/secondary-cards.md) |
| Eight secondary slots | [secondary cards](user/secondary-cards.md) |
| udev reconcile | [secondary cards](user/secondary-cards.md) |
| Rate-control sequence | [secondary cards](user/secondary-cards.md) |
| Secondary capture/playback pipeline | [secondary cards](user/secondary-cards.md) |
| Tool to API call map | [tools](user/tools.md) |
| Troubleshooting decision tree | [troubleshooting](user/troubleshooting.md) |
| Error-code map | [troubleshooting](user/troubleshooting.md) |

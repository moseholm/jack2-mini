# The command-line tools

The tools are built on the jackmini client API (`client/jackmini/`) and are installed with the other binaries. They
never start a server: with no server running they print an error and exit with status 1. Each takes `-s <name>` to
choose a server, `-v` for the version and `-h` for the usage. The build option is `JACK_TOOLS` (on by default).

| Tool | What it does |
|---|---|
| `jackmini_lsp` | Lists the ports of the server. Options as `jack_lsp`: `-A` aliases, `-c` connections, `-l` port latency, `-L` total latency, `-p` properties, `-t` type, `-u` owner UUID, `-U` port UUID. A filter argument keeps the ports whose names contain all the strings given. |
| `jackmini_connect` | Connects two ports, one output and one input, in either order. `-u` takes the ports as `<client UUID>:<port>` (as `jackmini_lsp -u` prints them). |
| `jackmini_disconnect` | Disconnects two ports, as `jackmini_connect` does. |
| `jackmini_samplerate` | Prints the server's sample rate on one line. |
| `jackmini_wait` | `-c` (the default) reports `running` or `not running`; `-w` waits until the server runs (`server is available`); `-q` waits until it quits (`server is gone`); `-t <seconds>` gives up with `timeout`. `-n <name>` names the probe client. |
| `jackmini_midi_dump` | Prints the MIDI events of its input port (`midi-monitor:input`, or the name given as argument): the time in frames within the cycle, `-a` the frames since the program started, `-r` the frames since the previous event. |
| `jackmini_monitor_client <client>` | Copies each audio input of `<client>` to an output `monitor_<n>` of its own, every cycle, until stopped. See the note below. |

## Output compatibility

`jackmini_lsp` prints the lines of `jack_lsp`, in the same order, with the same indentation and wording. Two differences:

- UUIDs are printed as the server writes them: the decimal of the 64-bit value.
- `-A` prints nothing yet, since no aliases are set.

## Not ported

These old tools need API or server features that do not exist, or were dropped on purpose. Each needs its own decision:
`jack_bufsize`, `jack_freewheel`, `jack_transport`, `jack_evmon`, `jack_alias`, `jack_session_notify`,
`jack_property`, `jack_ipload`, `jack_ipunload`, the net and ALSA bridge tools, and the example clients.

## Note on `jackmini_monitor_client`

`jack_monitor_client` asked the server to monitor a port for 30 seconds. The server keeps that request, but no audio
path reads it, so the old tool had no audible effect. `jackmini_monitor_client` does the copy itself instead, as
described in the handoff.

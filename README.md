# ToastyLink

A from-scratch C++17 client for **XBDM** (Xbox Debug Monitor), the network
control/debug protocol exposed by Xbox 360 kernels and dashboards that have
debugging enabled. On a softmodded console (RGH/JTAG running a custom
dashboard such as Aurora, Freestyle Dash, or XeXMenu with Dashlaunch) this
is the same class of protocol tools like Xbox 360 Neighborhood or Cheat
Engine 360 talk to — ToastyLink implements the wire protocol itself, in
plain C++, with no third-party SDK.

It gives you an interactive shell (and a scriptable one-shot CLI mode) for:

- Reading and writing console memory
- Listing loaded modules, running threads, and mapped memory regions
- Inspecting the currently running title
- AOB (array-of-bytes) pattern scanning over a memory range or the whole
  mapped address space, with wildcard bytes
- Sending **any** raw XBDM command and seeing the exact response — the
  protocol framing is correct regardless of which command you send, so
  the tool stays useful even for commands it doesn't have a dedicated
  wrapper for

No game-specific memory offsets are baked in anywhere. Offsets are
build-specific and go stale the moment a title updates, so instead of
shipping a pile of addresses that would rot on day one, ToastyLink gives
you the primitives (`getmem`, `setmem`, `scan`) to find and use your own.

## Why this exists

Most public XBDM tooling is old, closed-source, C#/.NET, or bundled into
a much larger GUI app. ToastyLink is a small, readable, single-purpose C++
implementation of the protocol itself — useful on its own, and as a
reference for anyone writing their own Xbox 360 tooling.

## Requirements

- A Windows, Linux, or macOS machine on the same network as the console
- An Xbox 360 with a softmod (RGH or JTAG) running a dashboard with XBDM
  enabled (e.g. Dashlaunch's `xbdm` hook), **or** a devkit-mode console.
  XBDM listens on **TCP port 730** by default.
- A C++17 compiler + CMake 3.15+ to build

## Building

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

This produces `toastylink` (or `toastylink.exe` on Windows) in `build/`.
Tested with MSVC (Visual Studio 2022 toolset) on Windows; it also builds
clean with GCC/Clang on Linux/macOS since it only uses the standard
library plus BSD sockets / Winsock.

## Usage

```bash
# Interactive shell
toastylink 192.168.1.50

# One-shot: run a single command and exit
toastylink 192.168.1.50 -- dbgname
toastylink 192.168.1.50 730 -- getmem 0x82000000 0x100
```

### Shell commands

| Command | Description |
|---|---|
| `dbgname` | Show the console's debug name |
| `modules` | List loaded modules (name, base, size) |
| `threads` | List running thread IDs |
| `walkmem` | List mapped memory regions |
| `getmem <addr> <len>` | Read memory; unmapped bytes print as `??` |
| `setmem <addr> <hexbytes>` | Write bytes, e.g. `setmem 0x82000000 DEADBEEF` |
| `xbeinfo` | Info about the currently running title |
| `scan <addr> <len> <pattern>` | AOB scan a range, e.g. `scan 0x82000000 0x10000 48 65 ?? 6F` |
| `scanall <pattern>` | AOB scan every mapped region (slower) |
| `reboot [title\|cold]` | Reboot the console |
| `raw <xbdm command...>` | Send any raw XBDM command, print the reply verbatim |
| `help` | List commands |
| `quit` / `exit` | Disconnect and exit |

Any input that isn't one of the above is forwarded to the console as a raw
XBDM command automatically, so the shell doubles as a general XBDM
terminal.

## How it works

- **`Socket`** — a small blocking TCP wrapper (Winsock on Windows, BSD
  sockets elsewhere) with a buffered line reader.
- **`XbdmClient`** — implements XBDM's line-oriented framing: a command is
  one CRLF-terminated line; the reply starts with a `<code>- <message>`
  status line, and a `2xx` "multiline" reply is followed by additional
  lines up to a terminating `.` line. This framing is implemented once,
  correctly, and everything else is built on it — including
  `SendCommand()`, which is exposed directly for any command without a
  dedicated wrapper.
- **`MemoryScanner`** — chunked, overlap-aware AOB scanning built purely
  on top of `GetMemory()`/`WalkMemory()`, so it works against any console
  regardless of protocol-detail edge cases in higher-level parsing.
- **`Shell`** — the REPL / one-shot command dispatcher.

## Scope and intent

This is offline/homebrew tooling for **your own** console: memory
inspection, debugging, and scripting against a devkit or a private
RGH/JTAG box. It talks to nothing but the IP address you give it.

## License

MIT — see [LICENSE](LICENSE).

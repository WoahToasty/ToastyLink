# ToastyLink

A from-scratch C++17 **trainer and debug toolkit** for **XBDM** (Xbox Debug
Monitor), the network control/debug protocol exposed by Xbox 360 kernels
and dashboards that have debugging enabled. On a softmodded console
(RGH/JTAG running a custom dashboard such as Aurora, Freestyle Dash, or
XeXMenu with Dashlaunch) this is the same class of protocol tools like
Xbox 360 Neighborhood or Cheat Engine 360 talk to — ToastyLink implements
the wire protocol itself, in plain C++, with no third-party SDK.

It's built around the workflow the RGH/JTAG community actually uses day to
day: find an address, make sure it survives a reboot, freeze it, share the
result.

- **Typed, endian-correct memory access** — `read`/`write` for i8 through
  f64, handled explicitly for Xenon's big-endian PowerPC byte order (the
  #1 source of silent bugs when a little-endian PC talks to it)
- **Pointer chains** — `base,off1,off2,...` resolves live against the
  console, so an address survives game restarts instead of rotting the
  moment ASLR-free-but-dynamic allocations move
- **Cheat Engine-style progressive value scanning** (`vscan`) — first
  scan captures every candidate matching a value/type over a range, then
  each `vscan next changed|unchanged|increased|decreased|exact` re-reads
  and narrows the set, exactly how you find an unknown health/ammo/
  currency address by playing between scans
- **A real freeze/trainer engine** (`freeze`) — named entries with a
  literal address or pointer chain, continuously rewritten by a
  background thread, saved and loaded as small **JSON cheat-table files**
  you can hand to someone else running ToastyLink, or auto-saved/loaded
  under a name derived from whatever title is currently running
  (`freeze autosave` / `freeze autoload`)
- **Toggleable code patches with a tiny built-in PPC/Xenon assembler**
  (`patch`, `asm`) — install writes new bytes at an address and remembers
  the originals so it can be reverted exactly; `asm` hand-assembles
  `nop`/`blr`/`b`/`bl`/`li` so common patches (skip a check, redirect a
  call) don't need an external toolchain
- **AOB (array-of-bytes) pattern scanning** with wildcards, over a range
  or the whole mapped address space
- **LAN console discovery** (`discover`) — probes a subnet for anything
  answering on the XBDM port so you don't have to go find the console's
  IP yourself
- Module/thread/memory-region listing, filesystem browsing
  (`dirlist`/`mkdir`/`delete`), best-effort on-screen `notify`, a live
  `watch` view for one value, and a console address book so you can
  `connect myrgh` instead of memorizing an IP
- Batch scripting (`--script file.txt`) so a whole setup — attach,
  resolve pointers, load a cheat table, start freezing — is one command
- Raw XBDM passthrough for anything without a dedicated wrapper — the
  protocol framing is correct regardless of which command you send

No game-specific memory offsets ship in the code. Offsets are
build-specific and go stale the moment a title updates, so instead of a
pile of addresses that would rot on day one, ToastyLink gives you the
primitives — `scan`, `vscan`, pointer chains — to find and pin down your
own, and a file format (the cheat table) to save and share what you find.

## Why this exists

Most public XBDM tooling is old, closed-source, C#/.NET, or bundled into
a much larger GUI app, and almost none of it does progressive value
scanning or pointer-chain freezing without a much heavier Cheat Engine
setup on top. ToastyLink is a small, readable, single-purpose C++
implementation of the protocol *and* the trainer workflow on top of it —
useful on its own, and as a reference for anyone writing their own Xbox
360 tooling.

## Requirements

- A Windows, Linux, or macOS machine on the same network as the console
- An Xbox 360 with a softmod (RGH or JTAG) running a dashboard with XBDM
  enabled (e.g. Dashlaunch's `xbdm` hook), **or** a devkit-mode console.
  XBDM listens on **TCP port 730** by default.
- A C++17 compiler + CMake 3.15+ to build. Uses `std::thread`/
  `std::filesystem` from the standard library only — no external
  dependencies.

## Building

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

This produces `toastylink` (or `toastylink.exe` on Windows) in `build/`.
Tested with MSVC (Visual Studio 2022 toolset) on Windows; it also builds
clean with GCC/Clang on Linux/macOS.

## Usage

```bash
# Interactive shell
toastylink 192.168.1.50

# One-shot: run a single command and exit
toastylink 192.168.1.50 -- dbgname

# Batch: run every line of a script and exit
toastylink 192.168.1.50 --script trainer.txt

# Nicknames saved with 'consoles add' work anywhere an IP does
toastylink myrgh -- dbgname
```

### Quick tour: find the console, then find and freeze a value

```
toastylink> discover 192.168.1
scanning 192.168.1.1-254 on port 730...
1 console(s) found:
  192.168.1.50  (201- connected)
tip: 'connect 192.168.1.50' or 'consoles add <name> 192.168.1.50'
```

```
toastylink> vscan new i32 0x82000000 0x200000 exact 100
scanning 0x82000000 + 0x00200000 as i32...
414 candidate(s). Play/change the value, then run 'vscan next ...'.

(take damage in-game, so the value is no longer 100)

toastylink> vscan next changed
6 candidate(s) remaining.

(take damage again)

toastylink> vscan next decreased
1 candidate(s) remaining.

toastylink> vscan list
0x82045a10 = 73
1 of 1 candidate(s) shown

toastylink> freeze add health i32 0x82045a10 100
added
toastylink> freeze start
freeze engine started (interval 200ms)
```

That address will move next time you restart the game. To pin it down
with a pointer chain instead (so it survives restarts), walk backwards
from a module's static data with a memory-analysis tool of your choice,
then use the chain directly:

```
toastylink> read i32 0x82000100,0x18,0x4
0x820abcd0 = 100
toastylink> freeze add health i32 0x82000100,0x18,0x4 100
```

`base,off1,off2,...`: `base` is the address of the first pointer; each
offset reads the pointer at the current address and adds the offset,
producing the next address to dereference (or, on the last offset, the
final target address).

Once you're happy with a set of frozen values, `freeze autosave` writes it
under a name derived from the running title (its name plus its main
module's checksum/timestamp), so `freeze autoload` on a later session
picks it back up automatically without you tracking a filename.

### Quick tour: patching code instead of freezing a value

Freezing rewrites a value forever; sometimes you'd rather patch the
instruction that checks it once, so nothing has to keep re-writing it.
`asm` hand-assembles the handful of PowerPC instructions this comes up
for constantly:

```
toastylink> asm nop
60000000
toastylink> asm b 0x82001000 0x82001010
48000010
```

`patch install` reads and remembers the original bytes before writing, so
`patch revert` restores them exactly:

```
toastylink> patch install skipcheck 0x82001000 asm nop
installed (60000000)
toastylink> patch list
[on]  skipcheck  addr=0x82001000  new=60000000  orig=7C0802A6
toastylink> patch revert skipcheck
ok
toastylink> patch reinstall skipcheck
ok
toastylink> patch save mytitle_patches.json
saved
```

`patch install <name> <addr> asm b <target>` / `bl <target>` compute the
branch relative to `<addr>` (where the patch itself lives) automatically.
`patch load` never re-applies patches by itself — call `patch reinstall
<name>` for each one you actually want live, so loading a file is never a
silent write.

### Command reference

| Command | Description |
|---|---|
| `discover <subnet-prefix>` | Find XBDM consoles on your LAN, e.g. `discover 192.168.1` |
| `connect <ip\|nickname> [port]` | Disconnect and connect to a different console |
| `consoles add/list/rm` | Manage the saved console address book |
| `dbgname` | Show the console's debug name |
| `modules` | List loaded modules (name, base, size) |
| `threads` | List running thread IDs |
| `walkmem` | List mapped memory regions |
| `xbeinfo` | Info about the currently running title |
| `reboot [title\|cold]` | Reboot the console |
| `getmem <addr> <len>` | Read raw bytes; unmapped bytes print as `??` |
| `setmem <addr> <hexbytes>` | Write raw bytes |
| `read <type> <addr>` | Typed read; `<addr>` may be a pointer chain |
| `write <type> <addr> <value>` | Typed write; `<addr>` may be a pointer chain |
| `watch <type> <addr> [count] [ms]` | Repeatedly re-read a value (default 20x @ 500ms) |
| `scan <addr> <len> <pattern>` | AOB scan a range, e.g. `scan 0x82000000 0x10000 48 65 ?? 6F` |
| `scanall <pattern>` | AOB scan every mapped region |
| `vscan new/next/list/reset` | Progressive value scan (see above) |
| `freeze add/rm/enable/disable/list/start/stop/save/load/autosave/autoload` | Trainer engine (see above) |
| `asm nop\|blr\|b\|bl\|li` | Hand-assemble one PPC instruction, print its bytes |
| `patch install/revert/reinstall/rm/list/save/load` | Toggleable code patches (see above) |
| `dirlist <path>` (alias `ls`) | Browse a drive/directory |
| `mkdir <path>` | Create a directory |
| `delete <path> [dir]` (alias `rm`) | Delete a file or directory |
| `notify <text>` | Best-effort on-screen popup |
| `sleep <ms>` | Pause (useful in scripts) |
| `raw <xbdm command...>` | Send any raw XBDM command, print the reply verbatim |
| `help` | List commands |
| `quit` / `exit` | Disconnect and exit |

Types for `read`/`write`/`vscan`/`freeze`: `i8 u8 i16 u16 i32 u32 i64 u64
f32 f64`. Anything not in the table above is forwarded to the console as
a raw XBDM command automatically.

### Cheat tables

`freeze save mytitle.json` writes every entry as JSON:

```json
[
  {
    "name": "health",
    "address": "0x82000100,0x18,0x4",
    "type": "i32",
    "value": "100",
    "enabled": true
  }
]
```

`freeze load mytitle.json` reads it back (merging by name). This is a
plain text format anyone can hand-edit or generate — a shareable trainer
definition for a given title that anyone running ToastyLink can load.

### Batch scripts

`toastylink <console> --script setup.txt` runs one shell command per
non-empty line (lines starting with `#` are comments) — the same commands
you'd type interactively, so a whole session (connect, load a cheat
table, start freezing) is reproducible as one file.

## How it works

- **`Socket`** — a small blocking TCP wrapper (Winsock on Windows, BSD
  sockets elsewhere) with a buffered line reader.
- **`XbdmClient`** — implements XBDM's line-oriented framing: a command is
  one CRLF-terminated line; the reply starts with a `<code>- <message>`
  status line, and a `2xx` "multiline" reply is followed by additional
  lines up to a terminating `.` line. Thread-safe (internally
  mutex-guarded) so the freeze engine's background thread and the shell
  can share one connection without corrupting the protocol stream.
  `SendCommand()` is exposed directly for any command without a
  dedicated wrapper.
- **`TypedValue`** — packs/unpacks `i8..f64` to/from Xenon's big-endian
  wire format by hand (bit shifts, not `memcpy`), so it's correct
  regardless of the host machine's own endianness.
- **`AddressResolver`** — resolves `base,off1,off2,...` pointer chains
  live against the console.
- **`MemoryScanner`** — chunked, overlap-aware AOB scanning.
- **`ValueScanner`** — progressive value scanning; re-reads on `vscan
  next` are batched by merging nearby candidate addresses into single
  bulk reads rather than one round trip per address.
- **`FreezeEngine`** — a background `std::thread` that re-resolves each
  entry's address (so pointer chains keep working across the target
  moving) and rewrites its value on an interval; entries persist as JSON
  via a small hand-rolled parser/writer (`Json.h`/`.cpp`, no external
  dependency).
- **`ConsoleBook`** — a JSON-backed nickname → IP/port address book, and
  the base directory for auto-saved cheat tables.
- **`Assembler`** — hand-assembles `nop`/`blr`/`b`/`bl`/`li` using the
  fixed PowerPC ISA encodings; branch displacements outside the 24-bit
  (+/-32MB) field are rejected rather than silently truncated.
- **`PatchEngine`** — install/revert/reinstall for named byte patches;
  reads and stores the original bytes before ever writing, so a patch is
  always exactly reversible. Persists as JSON like cheat tables, but
  never re-applies a loaded patch automatically.
- **`Discovery`** — probes TCP port 730 across a subnet with a bounded
  per-host timeout (via a non-blocking connect + `select`, not the OS's
  much longer default TCP timeout) across a small thread pool, and
  reports any host that answers with an XBDM-shaped greeting line. Not a
  protocol-specific broadcast discovery scheme — there's no XBDM
  discovery packet format this project could implement with confidence,
  so it does the honest equivalent instead: a banner probe.
- **`Shell`** — the REPL / one-shot / batch-script command dispatcher.

## Scope and intent

This is offline/homebrew tooling for **your own** console: memory
inspection, trainers, and debugging against a devkit or a private
RGH/JTAG box, playable offline or in private lobbies. It talks to
nothing but the IP address you give it, and file get/put (bulk transfer
of files to/from the console's filesystem) is intentionally not
implemented — that command's binary framing varies enough between XBDM
implementations that shipping a guessed version risked silently
corrupting the exact file you're trying to deploy. `dirlist`/`mkdir`/
`delete` (all plain text protocol, well-documented) are implemented; for
anything else filesystem-related, `raw` gets you there to inspect the
exact reply your console's XBDM sends before relying on it.

## License

MIT — see [LICENSE](LICENSE).

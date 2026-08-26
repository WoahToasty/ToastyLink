#!/usr/bin/env python3
"""End-to-end tests for ToastyLink against the mock XBDM server.

Builds nothing -- point it at an already-built binary:

    python tests/run_tests.py build/toastylink        (or build\\toastylink.exe)

Each case runs the real binary against a real socket server and asserts on
its output. The suite runs against several getmem caps, because a console
capping a single read is exactly the condition that hides chunking bugs.
"""

import os
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
MOCK = os.path.join(HERE, "mock_xbdm.py")

# Values the mock seeds; see mock_xbdm.py.
PTR_CHAIN = "0x82000100,0x10"   # resolves to 0x82000210
PTR_TARGET = "0x82000210"
ALIGNED_PROBE_ADDR = "0x82018000"
ALIGNED_PROBE_VAL = 23294        # 0x5AFE, past the first scan chunk
STRADDLE_ADDR = "0x8200FFFC"     # AOB pattern spanning a chunk boundary
STRADDLE_PATTERN = "C0 FF EE 11 22 BA DD 1E"

failures = []
passed = 0


def run(binary, port, script_lines):
    """Runs a batch script through the binary, returns combined output."""
    with tempfile.NamedTemporaryFile("w", suffix=".txt", delete=False) as f:
        f.write("\n".join(script_lines) + "\n")
        path = f.name
    try:
        proc = subprocess.run(
            [binary, "127.0.0.1", str(port), "--script", path],
            capture_output=True, text=True, timeout=120,
        )
        return proc.stdout + proc.stderr
    finally:
        os.unlink(path)


def check(name, condition, detail=""):
    global passed
    if condition:
        passed += 1
        print(f"  PASS  {name}")
    else:
        failures.append(f"{name}: {detail}")
        print(f"  FAIL  {name}  {detail}")


def start_mock(port, cap=0):
    args = [sys.executable, MOCK, str(port)]
    if cap:
        args.append(hex(cap))
    proc = subprocess.Popen(args, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(1.0)
    return proc


def suite(binary, port, cap):
    label = f"getmem cap={hex(cap) if cap else 'none'}"
    print(f"\n== {label} ==")

    out = run(binary, port, [
        "dbgname",
        f"read i32 {PTR_TARGET}",
        f"read i32 {PTR_CHAIN}",
        "read i32 0x82000000,",
        "read i32 0x99999999,0x10",
    ])
    check("dbgname", "TESTCONSOLE" in out, out.strip()[:120])
    check("pointer chain resolves to same address as literal",
          out.count(f"{PTR_TARGET} = 1234") == 2, out.strip()[:200])
    check("trailing comma rejected",
          "could not parse '' as a number" in out, "should not silently read a literal")
    check("unmapped pointer reported",
          "is not mapped" in out, out.strip()[:120])

    # Every value type round-trips exactly at its boundaries. u64/i64 catch
    # precision loss from routing integers through a double.
    cases = [
        ("u64", "0x82000350", "18446744073709551615"),
        ("u64", "0x82000358", "12345678901234567890"),
        ("i64", "0x82000368", "-9223372036854775808"),
        ("i64", "0x82000370", "1234567890123456789"),
        ("u32", "0x82000380", "4294967295"),
        ("i32", "0x82000384", "-2147483648"),
        ("u16", "0x82000390", "65535"),
        ("i16", "0x82000394", "-32768"),
        ("u8", "0x82000398", "255"),
        ("i8", "0x82000399", "-128"),
    ]
    script = []
    for ty, addr, val in cases:
        script += [f"write {ty} {addr} {val}", f"read {ty} {addr}"]
    out = run(binary, port, script)
    for ty, addr, val in cases:
        check(f"{ty} round-trips exactly ({val})", f"{addr} = {val}" in out,
              f"expected '{addr} = {val}'")

    # Scans must find values/patterns that live past the first chunk and
    # across chunk boundaries -- both aligned and unaligned.
    out = run(binary, port, [
        f"vscan new i32 0x82000000 0x30000 exact {ALIGNED_PROBE_VAL}",
        "vscan list",
    ])
    check("aligned vscan finds value past first chunk",
          f"{ALIGNED_PROBE_ADDR} = {ALIGNED_PROBE_VAL}" in out and "1 of 1" in out,
          out.strip()[-200:])

    out = run(binary, port, [
        f"vscan new i32 0x82000000 0x30000 unaligned exact {ALIGNED_PROBE_VAL}",
        "vscan list",
    ])
    check("unaligned vscan finds same value",
          f"{ALIGNED_PROBE_ADDR} = {ALIGNED_PROBE_VAL}" in out and "1 of 1" in out,
          out.strip()[-200:])

    out = run(binary, port, [f"scan 0x82000000 0x30000 {STRADDLE_PATTERN}"])
    check("AOB finds pattern straddling chunk boundary, exactly once",
          "1 match(es)" in out and STRADDLE_ADDR in out, out.strip()[-200:])

    out = run(binary, port, [f"scan 0x82000000 0x30000 C0 FF ?? 11 ?? BA DD 1E"])
    check("AOB wildcards work", "1 match(es)" in out and STRADDLE_ADDR in out,
          out.strip()[-200:])

    # Exact 64-bit comparison: these two values are indistinguishable as
    # doubles, so a double-based compare would drop the candidate.
    out = run(binary, port, [
        "write u64 0x82001100 18446744073709551614",
        "vscan new u64 0x82001100 0x8 exact 18446744073709551614",
        "write u64 0x82001100 18446744073709551615",
        "vscan next increased",
    ])
    check("vscan 'increased' compares 64-bit values exactly",
          "1 candidate(s) remaining" in out, out.strip()[-200:])

    # Patch lifecycle, including exact revert.
    out = run(binary, port, [
        "setmem 0x82000400 AABBCCDD",
        "patch install p 0x82000400 asm nop",
        "getmem 0x82000400 0x4",
        "patch install p 0x82000400 hex 11111111",
        "patch revert p",
        "getmem 0x82000400 0x4",
        "patch reinstall p",
        "getmem 0x82000400 0x4",
    ])
    check("patch installs assembled nop", "60 00 00 00" in out, out.strip()[:400])
    check("duplicate patch name rejected", "already exists" in out, out.strip()[:400])
    check("patch revert restores original bytes", "AA BB CC DD" in out, out.strip()[:400])

    # Freeze engine actually holds a value against a competing write.
    out = run(binary, port, [
        "freeze add hp i32 0x82000210 777",
        "freeze start 30",
        "sleep 100",
        "raw setmem addr=0x82000210 data=00000001",
        "sleep 200",
        "read i32 0x82000210",
        "freeze start 50",
        "freeze stop",
        "freeze stop",
    ])
    check("freeze restores a value overwritten behind its back",
          "0x82000210 = 777" in out, out.strip()[-300:])
    check("double freeze start reports already-running",
          "already running" in out, out.strip()[-300:])
    check("double freeze stop reports not-running",
          "is not running" in out, out.strip()[-300:])


def assembler_suite(binary, port):
    print("\n== assembler (fixed PPC encodings) ==")
    expected = [
        ("asm nop", "60000000"),
        ("asm blr", "4E800020"),
        ("asm b 0x82001000 0x82001010", "48000010"),
        ("asm bl 0x82001000 0x82000FF0", "4BFFFFF1"),
        ("asm li 3 1234", "386004D2"),
        ("asm li 3 -1", "3860FFFF"),
    ]
    out = run(binary, port, [cmd for cmd, _ in expected])
    for cmd, enc in expected:
        check(f"{cmd} -> {enc}", enc in out, out.strip()[:400])

    out = run(binary, port, [
        "asm b 0x82000000 0x99000000",
        "asm b 0x82000001 0x82000010",
        "asm li 99 5",
        "asm li 3 99999",
        "asm bogus",
    ])
    check("out-of-range branch rejected", "out of range" in out, out[:400])
    check("unaligned branch rejected", "multiple of 4" in out, out[:400])
    check("bad register rejected", "r0..r31" in out, out[:400])
    check("oversized li rejected", "16 bits" in out, out[:400])
    check("unknown mnemonic rejected", "unknown mnemonic" in out, out[:400])


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    # Absolute path: CreateProcess on Windows won't resolve a bare
    # relative path the way a shell does.
    binary = os.path.abspath(sys.argv[1])
    if not os.path.exists(binary):
        print(f"binary not found: {binary}")
        return 2

    mocks = []
    try:
        for i, cap in enumerate([0, 0x800, 0x10]):
            port = 7401 + i
            mocks.append(start_mock(port, cap))
            suite(binary, port, cap)
        assembler_suite(binary, 7401)
    finally:
        for m in mocks:
            m.terminate()

    print(f"\n{passed} passed, {len(failures)} failed")
    for f in failures:
        print(f"  FAILED: {f}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())

"""A mock XBDM server for testing ToastyLink without a console.

Speaks the same line-oriented protocol a debug-enabled Xbox 360 does, over
a small simulated big-endian address space, and deliberately reproduces the
awkward parts of real implementations -- capping how much a single getmem
returns, reporting unmapped bytes as "??" -- so the client's chunking and
short-read handling actually get exercised.

    python mock_xbdm.py [port] [max-getmem-bytes]

`max-getmem-bytes` defaults to 0 (unlimited); pass e.g. 0x800 to emulate a
console that caps each read. Run tests/run_tests.py to drive it.
"""

import re, socket, struct, sys, threading

HOST = "127.0.0.1"
PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 7301
# Max bytes a single getmem will return (0 = unlimited). Real XBDM caps this.
MAX_GETMEM = int(sys.argv[2], 0) if len(sys.argv) > 2 else 0

BASE = 0x82000000
SIZE = 0x30000          # deliberately spans several 0x10000 scan chunks
MEM = bytearray(SIZE)

# A uniquely-valued i32 at a 4-byte-aligned address PAST the first chunk
# boundary, to catch aligned-scan chunking bugs.
ALIGNED_PROBE_OFF = 0x18000        # 4-byte aligned, inside chunk 2
struct.pack_into(">i", MEM, ALIGNED_PROBE_OFF, 0x5AFE)

# An 8-byte AOB pattern deliberately straddling the 0x10000 chunk
# boundary, to catch chunk-overlap bugs in the pattern scanner.
STRADDLE_OFF = 0xFFFC
MEM[STRADDLE_OFF:STRADDLE_OFF + 8] = bytes([0xC0, 0xFF, 0xEE, 0x11, 0x22, 0xBA, 0xDD, 0x1E])

# Seed a pointer chain: MEM[0x100] holds a BE pointer to BASE+0x200; reading
# that pointer and adding offset 0x10 should land on BASE+0x210, where we
# put a known i32 value (1234).
struct.pack_into(">I", MEM, 0x100, BASE + 0x200)
struct.pack_into(">i", MEM, 0x210, 1234)

# Seed a small block of "candidate" i32 values at BASE+0x1000.. for vscan
# testing (some equal to 100, so an exact-value first scan has hits).
for i, v in enumerate([100, 55, 100, 7, 100, 999]):
    struct.pack_into(">i", MEM, 0x1000 + i * 4, v)

LOCK = threading.Lock()


def get_mem_hex(addr, length):
    out = []
    with LOCK:
        for i in range(length):
            a = addr + i
            if BASE <= a < BASE + SIZE:
                out.append("%02X" % MEM[a - BASE])
            else:
                out.append("??")
    return "".join(out)


def set_mem(addr, data_hex):
    data = bytes.fromhex(data_hex)
    with LOCK:
        for i, b in enumerate(data):
            a = addr + i
            if BASE <= a < BASE + SIZE:
                MEM[a - BASE] = b


def handle(conn):
    conn.sendall(b"201- connected\r\n")
    f = conn.makefile("rwb")
    while True:
        line = f.readline()
        if not line:
            break
        cmd = line.decode(errors="replace").strip()

        if cmd == "dbgname":
            conn.sendall(b"200- TESTCONSOLE\r\n")
        elif cmd == "modules":
            conn.sendall(b"202- multiline response follows\r\n")
            conn.sendall(b'name="xam.xex" base=0x82000000 size=0x00080000 check=0x12345678 timestamp=0x5f000000\r\n')
            conn.sendall(b'name="default.xex" base=0x82080000 size=0x00040000 check=0xaabbccdd timestamp=0x5f000001\r\n')
            conn.sendall(b".\r\n")
        elif cmd == "threads":
            conn.sendall(b"202- multiline response follows\r\n")
            conn.sendall(b"4\r\n8\r\n12\r\n")
            conn.sendall(b".\r\n")
        elif cmd == "walkmem":
            conn.sendall(b"202- multiline response follows\r\n")
            conn.sendall(("base=0x%08X size=0x%08X protect=0x00000004\r\n" % (BASE, SIZE)).encode())
            conn.sendall(b".\r\n")
        elif cmd.startswith("getmem"):
            # Accept length as decimal OR 0x-hex, like a real parser would.
            m = re.search(r"addr=(0x[0-9a-fA-F]+)\s+length=(0x[0-9a-fA-F]+|\d+)", cmd)
            addr = int(m.group(1), 16)
            length = int(m.group(2), 0)
            # Real XBDM implementations cap how much a single getmem
            # returns. Emulate that so short reads are exercised.
            if MAX_GETMEM:
                length = min(length, MAX_GETMEM)
            conn.sendall(b"202- multiline response follows\r\n")
            hexstr = get_mem_hex(addr, length)
            # split into ~64-char lines like real XBDM tends to
            for i in range(0, len(hexstr), 64):
                conn.sendall(hexstr[i:i+64].encode() + b"\r\n")
            conn.sendall(b".\r\n")
        elif cmd.startswith("setmem"):
            m = re.search(r"addr=(0x[0-9a-fA-F]+)\s+data=([0-9a-fA-F]+)", cmd)
            addr = int(m.group(1), 16)
            set_mem(addr, m.group(2))
            conn.sendall(b"200- OK\r\n")
        elif cmd == "xbeinfo running":
            conn.sendall(b"202- multiline response follows\r\n")
            conn.sendall(b'name="default.xex" launchpath="hdd:\\\\game\\\\default.xex"\r\n')
            conn.sendall(b".\r\n")
        elif cmd.startswith("dirlist"):
            conn.sendall(b"202- multiline response follows\r\n")
            conn.sendall(b'name="default.xex" sizehi=0x00000000 sizelo=0x00080000 attributes=0x00000020\r\n')
            conn.sendall(b'name="save" sizehi=0x00000000 sizelo=0x00000000 attributes=0x00000010\r\n')
            conn.sendall(b".\r\n")
        elif cmd.startswith("delete"):
            conn.sendall(b"200- OK\r\n")
        elif cmd.startswith("mkdir"):
            conn.sendall(b"200- OK\r\n")
        elif cmd.startswith("notify"):
            conn.sendall(b"200- OK\r\n")
        elif cmd.startswith("reboot"):
            conn.sendall(b"200- OK\r\n")
            break
        elif cmd == "bogus command":
            conn.sendall(b"407- unknown command\r\n")
        else:
            conn.sendall(b"400- unexpected error\r\n")
    conn.close()


def main():
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.bind((HOST, PORT))
    s.listen(5)
    print(f"mock xbdm listening on {HOST}:{PORT}", flush=True)
    while True:
        conn, _ = s.accept()
        t = threading.Thread(target=handle, args=(conn,), daemon=True)
        t.start()


if __name__ == "__main__":
    main()

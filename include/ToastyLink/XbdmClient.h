// XbdmClient implements the wire protocol spoken by XBDM (Xbox Debug
// Monitor), the network debug/control service exposed by Xbox 360
// dashboards/kernels that have debugging enabled -- which on RGH/JTAG
// consoles means Dashlaunch (or an equivalent softmod) with the "xbdm"
// plugin/hook loaded, or a devkit-mode kernel. Default port is 730.
//
// The protocol is a simple line-oriented text protocol:
//   - The server greets a new connection with a single status line.
//   - A command is a single CRLF-terminated ASCII line.
//   - A response starts with a line "<code>- <message>\r\n" where <code>
//     is a 3-digit status code. 2xx means success, 4xx/6xx mean failure.
//   - If the message indicates a multiline response ("...follows"), the
//     server sends additional lines until a line containing only "."
//     which terminates the block.
//
// This class only implements the always-correct framing (connect, send
// line, read status line, read multiline body up to the "."
// terminator) plus thin convenience wrappers for the handful of commands
// whose reply format is unambiguous and well documented. Anything else
// can be issued with SendCommand() directly -- the framing is correct
// regardless of which command you send, so the raw passthrough is always
// usable even for commands this header doesn't wrap.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "ToastyLink/Socket.h"

namespace tl {

struct XbdmResponse {
    int code = 0;                    // e.g. 200, 202, 410
    bool success = false;            // true if code is in [200, 299]
    bool multiline = false;          // true if a body followed the status line
    std::string statusLine;          // full first line, e.g. "200- OK"
    std::string message;             // status line with the "CODE- " prefix stripped
    std::vector<std::string> lines;  // body lines (multiline responses only)
};

// One entry from "getmem": either a concrete byte or an unmapped address
// (XBDM reports unmapped/unreadable bytes as "??" rather than failing the
// whole request).
struct MemoryByte {
    uint8_t value = 0;
    bool mapped = true;
};

struct ModuleInfo {
    std::string name;
    uint64_t base = 0;
    uint64_t size = 0;
    uint64_t checksum = 0;
    uint64_t timestamp = 0;
    std::string raw; // original line, always populated
};

struct MemoryRegion {
    uint64_t base = 0;
    uint64_t size = 0;
    uint32_t protect = 0;
    std::string raw; // original line, always populated
};

class XbdmClient {
public:
    XbdmClient() = default;

    // Connects to host:port and reads the initial greeting line. Returns
    // true on success. On failure, LastError() explains why.
    bool Connect(const std::string& host, uint16_t port = 730);
    void Disconnect();
    bool IsConnected() const { return m_socket.IsOpen(); }

    const std::string& Greeting() const { return m_greeting; }
    const std::string& LastError() const { return m_lastError; }

    // Sends a raw command line and parses the response according to the
    // framing rules described above. This is always safe to call with any
    // command string -- it is the mechanism every higher-level method
    // below is built on.
    XbdmResponse SendCommand(const std::string& command);

    // --- Convenience wrappers over well-documented commands ---------------

    // "dbgname" -> the console's debug name.
    std::optional<std::string> GetDebugName();

    // "modules" -> loaded module list. Parsing is best-effort key=value
    // extraction; ModuleInfo::raw always holds the untouched line so
    // nothing is lost if a field isn't recognized.
    std::optional<std::vector<ModuleInfo>> ListModules();

    // "threads" -> list of thread IDs currently running in the title.
    std::optional<std::vector<uint32_t>> ListThreads();

    // "walkmem" -> enumerates mapped memory regions (address-space map).
    std::optional<std::vector<MemoryRegion>> WalkMemory();

    // "getmem addr=<hex> length=<n>" -> reads `length` bytes starting at
    // `address`. Bytes XBDM reports as unmapped ("??") come back with
    // MemoryByte::mapped == false and value == 0.
    std::optional<std::vector<MemoryByte>> GetMemory(uint64_t address, uint32_t length);

    // "setmem addr=<hex> data=<hex>" -> writes bytes at `address`. Returns
    // true if the console reported success.
    bool SetMemory(uint64_t address, const std::vector<uint8_t>& bytes);

    // "xbeinfo running" -> raw multiline info about the currently running
    // title (name=, launchpath=, etc. as reported by the console).
    std::optional<std::vector<std::string>> GetRunningXbeInfo();

    // "reboot" (optionally "reboot cold" / "reboot title" upstream can
    // pass extra args) -> true if the console acknowledged the request.
    bool Reboot(const std::string& mode = "");

private:
    TcpSocket m_socket;
    std::string m_greeting;
    std::string m_lastError;

    bool ReadStatusLine(XbdmResponse& resp);
    void ReadMultilineBody(XbdmResponse& resp);
};

} // namespace tl

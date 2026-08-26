#include "ToastyLink/XbdmClient.h"

#include <algorithm>
#include <cctype>
#include <sstream>

#include "ToastyLink/HexUtils.h"

namespace tl {

namespace {

std::string ToLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                    [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string Trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

// Best-effort key=value / key="value" tokenizer for XBDM's semi-structured
// response lines (e.g. `name="xam.xex" base=0x82000000 size=0x1a2b3c`).
std::vector<std::pair<std::string, std::string>> ParseKeyValues(const std::string& line) {
    std::vector<std::pair<std::string, std::string>> out;
    size_t i = 0;
    const size_t n = line.size();
    while (i < n) {
        while (i < n && std::isspace(static_cast<unsigned char>(line[i]))) ++i;
        size_t keyStart = i;
        while (i < n && line[i] != '=' && !std::isspace(static_cast<unsigned char>(line[i]))) ++i;
        if (i >= n || line[i] != '=') {
            // No '=' found for this token; skip to next whitespace.
            while (i < n && !std::isspace(static_cast<unsigned char>(line[i]))) ++i;
            continue;
        }
        std::string key = line.substr(keyStart, i - keyStart);
        ++i; // skip '='
        std::string value;
        if (i < n && line[i] == '"') {
            ++i;
            size_t valStart = i;
            while (i < n && line[i] != '"') ++i;
            value = line.substr(valStart, i - valStart);
            if (i < n) ++i; // skip closing quote
        } else {
            size_t valStart = i;
            while (i < n && !std::isspace(static_cast<unsigned char>(line[i]))) ++i;
            value = line.substr(valStart, i - valStart);
        }
        out.emplace_back(ToLower(key), value);
    }
    return out;
}

std::optional<uint64_t> FindHex(const std::vector<std::pair<std::string, std::string>>& kv,
                                 const std::string& key) {
    for (auto& p : kv) {
        if (p.first == key) return ParseIntArg(p.second);
    }
    return std::nullopt;
}

std::optional<std::string> FindStr(const std::vector<std::pair<std::string, std::string>>& kv,
                                    const std::string& key) {
    for (auto& p : kv) {
        if (p.first == key) return p.second;
    }
    return std::nullopt;
}

} // namespace

bool XbdmClient::Connect(const std::string& host, uint16_t port) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_lastError.clear();
    m_greeting.clear();

    if (!m_socket.Connect(host, port, &m_lastError)) {
        return false;
    }

    // The server sends a single greeting line immediately on connect,
    // e.g. "201- connected".
    std::string line;
    if (!m_socket.ReadLine(line)) {
        m_lastError = "connected, but console closed the connection before greeting";
        m_socket.Close();
        return false;
    }
    m_greeting = line;
    return true;
}

void XbdmClient::Disconnect() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_socket.Close();
}

bool XbdmClient::IsConnected() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_socket.IsOpen();
}

std::string XbdmClient::Greeting() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_greeting;
}

std::string XbdmClient::LastError() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_lastError;
}

bool XbdmClient::ReadStatusLine(XbdmResponse& resp) {
    std::string line;
    if (!m_socket.ReadLine(line)) {
        m_lastError = "connection closed while waiting for a response";
        return false;
    }
    resp.statusLine = line;

    size_t dash = line.find('-');
    if (dash == std::string::npos) {
        // Not a well-formed status line; surface it as-is rather than
        // silently failing.
        resp.code = 0;
        resp.message = line;
        resp.success = false;
        return true;
    }

    std::string codeStr = Trim(line.substr(0, dash));
    resp.message = Trim(line.substr(dash + 1));
    try {
        resp.code = std::stoi(codeStr);
    } catch (...) {
        resp.code = 0;
    }
    resp.success = (resp.code >= 200 && resp.code < 300);

    std::string lowerMsg = ToLower(resp.message);
    resp.multiline = resp.code == 202 || lowerMsg.find("multiline") != std::string::npos;
    return true;
}

void XbdmClient::ReadMultilineBody(XbdmResponse& resp) {
    std::string line;
    while (m_socket.ReadLine(line)) {
        if (line == ".") break;
        resp.lines.push_back(line);
    }
}

XbdmResponse XbdmClient::SendCommand(const std::string& command) {
    std::lock_guard<std::mutex> lock(m_mutex);
    XbdmResponse resp;
    if (!m_socket.IsOpen()) {
        m_lastError = "not connected";
        return resp;
    }
    if (!m_socket.SendString(command + "\r\n")) {
        m_lastError = "failed to send command (connection lost)";
        return resp;
    }
    if (!ReadStatusLine(resp)) {
        return resp;
    }
    if (resp.multiline) {
        ReadMultilineBody(resp);
    }
    return resp;
}

std::optional<std::string> XbdmClient::GetDebugName() {
    XbdmResponse resp = SendCommand("dbgname");
    if (!resp.success) return std::nullopt;
    return resp.message;
}

std::optional<std::vector<ModuleInfo>> XbdmClient::ListModules() {
    XbdmResponse resp = SendCommand("modules");
    if (!resp.success) return std::nullopt;

    std::vector<ModuleInfo> out;
    for (auto& line : resp.lines) {
        auto kv = ParseKeyValues(line);
        ModuleInfo mi;
        mi.raw = line;
        if (auto v = FindStr(kv, "name")) mi.name = *v;
        if (auto v = FindHex(kv, "base")) mi.base = *v;
        if (auto v = FindHex(kv, "size")) mi.size = *v;
        if (auto v = FindHex(kv, "check")) mi.checksum = *v;
        if (auto v = FindHex(kv, "timestamp")) mi.timestamp = *v;
        out.push_back(std::move(mi));
    }
    return out;
}

std::optional<std::vector<uint32_t>> XbdmClient::ListThreads() {
    XbdmResponse resp = SendCommand("threads");
    if (!resp.success) return std::nullopt;

    std::vector<uint32_t> out;
    for (auto& line : resp.lines) {
        std::string t = Trim(line);
        if (t.empty()) continue;
        if (auto v = ParseIntArg(t)) out.push_back(static_cast<uint32_t>(*v));
    }
    return out;
}

std::optional<std::vector<MemoryRegion>> XbdmClient::WalkMemory() {
    XbdmResponse resp = SendCommand("walkmem");
    if (!resp.success) return std::nullopt;

    std::vector<MemoryRegion> out;
    for (auto& line : resp.lines) {
        auto kv = ParseKeyValues(line);
        MemoryRegion mr;
        mr.raw = line;
        if (auto v = FindHex(kv, "base")) mr.base = *v;
        if (auto v = FindHex(kv, "size")) mr.size = *v;
        if (auto v = FindHex(kv, "protect")) mr.protect = static_cast<uint32_t>(*v);
        out.push_back(std::move(mr));
    }
    return out;
}

std::optional<std::vector<MemoryByte>> XbdmClient::GetMemory(uint64_t address, uint32_t length) {
    std::ostringstream cmd;
    cmd << "getmem addr=" << FormatAddress(address) << " length=0x" << std::hex << length;
    XbdmResponse resp = SendCommand(cmd.str());
    if (!resp.success) return std::nullopt;

    // Body is a contiguous hex stream (optionally split across several
    // lines); unmapped/unreadable bytes are reported as "??" pairs.
    std::string joined;
    for (auto& l : resp.lines) joined += l;

    std::vector<MemoryByte> out;
    out.reserve(length);
    for (size_t i = 0; i + 1 < joined.size() && out.size() < length; i += 2) {
        char a = joined[i];
        char b = joined[i + 1];
        MemoryByte mb;
        if (a == '?' || b == '?') {
            mb.mapped = false;
            mb.value = 0;
        } else {
            auto byte = HexToBytes(std::string(1, a) + std::string(1, b));
            if (!byte || byte->empty()) {
                mb.mapped = false;
                mb.value = 0;
            } else {
                mb.mapped = true;
                mb.value = (*byte)[0];
            }
        }
        out.push_back(mb);
    }
    return out;
}

bool XbdmClient::SetMemory(uint64_t address, const std::vector<uint8_t>& bytes) {
    std::ostringstream cmd;
    cmd << "setmem addr=" << FormatAddress(address) << " data=" << BytesToHex(bytes);
    XbdmResponse resp = SendCommand(cmd.str());
    return resp.success;
}

std::optional<std::vector<std::string>> XbdmClient::GetRunningXbeInfo() {
    XbdmResponse resp = SendCommand("xbeinfo running");
    if (!resp.success) return std::nullopt;
    if (resp.multiline) return resp.lines;
    // Some XBDM implementations answer this as a single status line
    // rather than a multiline block; surface it either way.
    return std::vector<std::string>{resp.message};
}

bool XbdmClient::Reboot(const std::string& mode) {
    std::string cmd = "reboot";
    if (!mode.empty()) cmd += " " + mode;
    XbdmResponse resp = SendCommand(cmd);
    return resp.success;
}

std::optional<std::vector<DirEntry>> XbdmClient::DirList(const std::string& path) {
    XbdmResponse resp = SendCommand("dirlist name=\"" + path + "\"");
    if (!resp.success) return std::nullopt;

    std::vector<DirEntry> out;
    for (auto& line : resp.lines) {
        auto kv = ParseKeyValues(line);
        DirEntry de;
        de.raw = line;
        if (auto v = FindStr(kv, "name")) de.name = *v;
        uint64_t sizeHi = FindHex(kv, "sizehi").value_or(0);
        uint64_t sizeLo = FindHex(kv, "sizelo").value_or(0);
        de.size = (sizeHi << 32) | sizeLo;
        uint64_t attrs = FindHex(kv, "attributes").value_or(0);
        de.isDirectory = (attrs & 0x10) != 0; // FILE_ATTRIBUTE_DIRECTORY
        out.push_back(std::move(de));
    }
    return out;
}

bool XbdmClient::Delete(const std::string& path, bool isDirectory) {
    std::string cmd = "delete name=\"" + path + "\"";
    if (isDirectory) cmd += " dir";
    XbdmResponse resp = SendCommand(cmd);
    return resp.success;
}

bool XbdmClient::MakeDirectory(const std::string& path) {
    XbdmResponse resp = SendCommand("mkdir name=\"" + path + "\"");
    return resp.success;
}

bool XbdmClient::Notify(const std::string& text) {
    XbdmResponse resp = SendCommand("notify text=\"" + text + "\"");
    return resp.success;
}

} // namespace tl

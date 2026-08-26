#include "ToastyLink/Shell.h"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>

#include "ToastyLink/AddressResolver.h"
#include "ToastyLink/HexUtils.h"
#include "ToastyLink/MemoryScanner.h"

namespace tl {

namespace {

void PrintProgress(uint64_t scanned, uint64_t total) {
    if (total == 0) return;
    static uint64_t lastPct = 100;
    uint64_t pct = (scanned * 100) / total;
    if (pct != lastPct) {
        lastPct = pct;
        std::cout << "\r  " << pct << "%   " << std::flush;
    }
}

} // namespace

std::vector<std::string> Tokenize(const std::string& line) {
    std::vector<std::string> out;
    std::istringstream iss(line);
    std::string tok;
    while (iss >> tok) out.push_back(tok);
    return out;
}

Shell::Shell(XbdmClient& client) : m_client(client), m_scanner(client), m_freeze(client) {
    m_consoleBook.Load(ConsoleBook::DefaultPath());
}

void Shell::PrintHelp() const {
    std::cout <<
        "Connection & console book:\n"
        "  connect <ip|nickname> [port]     disconnect and connect to a different console\n"
        "  consoles add <name> <ip> [port]  save a console under a nickname\n"
        "  consoles list                    list saved consoles\n"
        "  consoles rm <name>               remove a saved console\n"
        "\n"
        "Basics:\n"
        "  dbgname                          show the console's debug name\n"
        "  modules                          list loaded modules\n"
        "  threads                          list running thread IDs\n"
        "  walkmem                          list mapped memory regions\n"
        "  xbeinfo                          info about the running title\n"
        "  reboot [title|cold]              reboot the console\n"
        "\n"
        "Raw memory:\n"
        "  getmem <addr> <len>              read raw bytes; unmapped bytes print as ??\n"
        "  setmem <addr> <hexbytes>         write raw bytes, e.g. setmem 0x82000000 DEADBEEF\n"
        "\n"
        "Typed memory (addr accepts a pointer chain: base,off1,off2,...):\n"
        "  read <type> <addr>               e.g. read f32 0x82000000  or  read i32 0x82000000,10,4\n"
        "  write <type> <addr> <value>      e.g. write i32 0x82000000 9999\n"
        "  types: i8 u8 i16 u16 i32 u32 i64 u64 f32 f64\n"
        "\n"
        "AOB pattern scan:\n"
        "  scan <addr> <len> <pattern>      e.g. scan 0x82000000 0x10000 48 65 ?? 6F\n"
        "  scanall <pattern>                scan every mapped region (slower)\n"
        "\n"
        "Cheat Engine-style value scan (find an unknown address by narrowing candidates):\n"
        "  vscan new <type> <addr> <len> [unaligned] [exact <value>]\n"
        "  vscan next <changed|unchanged|increased|decreased>\n"
        "  vscan next exact <value>\n"
        "  vscan list [count]               show current candidates (default 50)\n"
        "  vscan reset\n"
        "\n"
        "Freeze / trainer engine (background thread rewrites frozen values):\n"
        "  freeze add <name> <type> <addr> <value>    addr may be a pointer chain\n"
        "  freeze rm <name>\n"
        "  freeze enable <name>  /  freeze disable <name>\n"
        "  freeze list\n"
        "  freeze start [intervalMs]   /   freeze stop\n"
        "  freeze save <file.json>     /   freeze load <file.json>\n"
        "\n"
        "Filesystem:\n"
        "  dirlist <path>  (alias: ls)      e.g. dirlist hdd:\\  or  ls usb0:\\Games\n"
        "  mkdir <path>\n"
        "  delete <path> [dir]  (alias: rm)\n"
        "\n"
        "Misc:\n"
        "  notify <text>                    best-effort on-screen popup (dashboard-dependent)\n"
        "  sleep <ms>                       pause for a bit (useful in scripts, e.g. after reboot)\n"
        "  raw <xbdm command...>            send a raw XBDM command, print the reply verbatim\n"
        "  help                             show this text\n"
        "  quit / exit                      close the connection and exit\n"
        "\n"
        "Anything else is forwarded verbatim as a raw XBDM command.\n";
}

void Shell::CmdRaw(const std::vector<std::string>& tokens) {
    if (tokens.empty()) {
        std::cout << "usage: raw <xbdm command...>\n";
        return;
    }
    std::ostringstream cmd;
    for (size_t i = 0; i < tokens.size(); ++i) {
        if (i) cmd << ' ';
        cmd << tokens[i];
    }
    XbdmResponse resp = m_client.SendCommand(cmd.str());
    std::cout << resp.statusLine << "\n";
    for (auto& l : resp.lines) std::cout << l << "\n";
}

void Shell::CmdDbgName() {
    auto name = m_client.GetDebugName();
    if (!name) { std::cout << "error: " << m_client.LastError() << "\n"; return; }
    std::cout << *name << "\n";
}

void Shell::CmdModules() {
    auto mods = m_client.ListModules();
    if (!mods) { std::cout << "error: " << m_client.LastError() << "\n"; return; }
    for (auto& m : *mods) {
        if (!m.name.empty()) {
            std::cout << m.name << "  base=" << FormatAddress(m.base)
                       << "  size=" << FormatAddress(m.size) << "\n";
        } else {
            std::cout << m.raw << "\n";
        }
    }
    std::cout << mods->size() << " module(s)\n";
}

void Shell::CmdThreads() {
    auto threads = m_client.ListThreads();
    if (!threads) { std::cout << "error: " << m_client.LastError() << "\n"; return; }
    for (auto id : *threads) std::cout << id << "\n";
    std::cout << threads->size() << " thread(s)\n";
}

void Shell::CmdWalkMem() {
    auto regions = m_client.WalkMemory();
    if (!regions) { std::cout << "error: " << m_client.LastError() << "\n"; return; }
    for (auto& r : *regions) {
        std::cout << FormatAddress(r.base) << "  size=" << FormatAddress(r.size)
                   << "  protect=0x" << std::hex << r.protect << std::dec << "\n";
    }
    std::cout << regions->size() << " region(s)\n";
}

void Shell::CmdGetMem(const std::vector<std::string>& tokens) {
    if (tokens.size() < 2) { std::cout << "usage: getmem <addr> <len>\n"; return; }
    auto addr = ParseIntArg(tokens[0]);
    auto len = ParseIntArg(tokens[1]);
    if (!addr || !len) { std::cout << "error: could not parse address/length\n"; return; }
    auto bytes = m_client.GetMemory(*addr, static_cast<uint32_t>(*len));
    if (!bytes) { std::cout << "error: " << m_client.LastError() << "\n"; return; }
    for (size_t i = 0; i < bytes->size(); ++i) {
        if (i % 16 == 0) {
            if (i) std::cout << "\n";
            std::cout << FormatAddress(*addr + i) << ": ";
        }
        const MemoryByte& mb = (*bytes)[i];
        if (mb.mapped) {
            char buf[4];
            std::snprintf(buf, sizeof(buf), "%02X ", mb.value);
            std::cout << buf;
        } else {
            std::cout << "?? ";
        }
    }
    std::cout << "\n";
}

void Shell::CmdSetMem(const std::vector<std::string>& tokens) {
    if (tokens.size() < 2) { std::cout << "usage: setmem <addr> <hexbytes>\n"; return; }
    auto addr = ParseIntArg(tokens[0]);
    auto bytes = HexToBytes(tokens[1]);
    if (!addr || !bytes) { std::cout << "error: could not parse address/hex bytes\n"; return; }
    bool ok = m_client.SetMemory(*addr, *bytes);
    std::cout << (ok ? "OK" : ("error: " + m_client.LastError())) << "\n";
}

void Shell::CmdXbeInfo() {
    auto info = m_client.GetRunningXbeInfo();
    if (!info) { std::cout << "error: " << m_client.LastError() << "\n"; return; }
    for (auto& l : *info) std::cout << l << "\n";
}

void Shell::CmdReboot(const std::vector<std::string>& tokens) {
    std::string mode = tokens.empty() ? "" : tokens[0];
    bool ok = m_client.Reboot(mode);
    std::cout << (ok ? "reboot requested" : ("error: " + m_client.LastError())) << "\n";
}

void Shell::CmdScan(const std::vector<std::string>& tokens) {
    if (tokens.size() < 3) {
        std::cout << "usage: scan <addr> <len> <pattern bytes, e.g. 48 65 ?? 6F>\n";
        return;
    }
    auto addr = ParseIntArg(tokens[0]);
    auto len = ParseIntArg(tokens[1]);
    if (!addr || !len) { std::cout << "error: could not parse address/length\n"; return; }

    std::ostringstream patternText;
    for (size_t i = 2; i < tokens.size(); ++i) {
        if (i > 2) patternText << ' ';
        patternText << tokens[i];
    }
    auto pattern = ParsePattern(patternText.str());
    if (!pattern) { std::cout << "error: could not parse pattern (use two hex digits or ?? per byte)\n"; return; }

    MemoryScanner scanner(m_client);
    std::cout << "scanning " << FormatAddress(*addr) << " + " << FormatAddress(*len) << "...\n";
    auto matches = scanner.ScanRange(*addr, *len, *pattern, PrintProgress);
    std::cout << "\r" << matches.size() << " match(es):\n";
    for (auto m : matches) std::cout << "  " << FormatAddress(m) << "\n";
}

void Shell::CmdScanAll(const std::vector<std::string>& tokens) {
    if (tokens.empty()) { std::cout << "usage: scanall <pattern>\n"; return; }
    std::ostringstream patternText;
    for (size_t i = 0; i < tokens.size(); ++i) {
        if (i) patternText << ' ';
        patternText << tokens[i];
    }
    auto pattern = ParsePattern(patternText.str());
    if (!pattern) { std::cout << "error: could not parse pattern\n"; return; }

    MemoryScanner scanner(m_client);
    std::cout << "scanning all mapped regions (this can take a while)...\n";
    auto matches = scanner.ScanAllRegions(*pattern, PrintProgress);
    std::cout << "\r" << matches.size() << " match(es):\n";
    for (auto m : matches) std::cout << "  " << FormatAddress(m) << "\n";
}

void Shell::CmdRead(const std::vector<std::string>& tokens) {
    if (tokens.size() < 2) {
        std::cout << "usage: read <type> <addr>   (addr may be base,off1,off2,... for a pointer chain)\n";
        return;
    }
    auto type = ParseValueType(tokens[0]);
    if (!type) { std::cout << "error: unknown type '" << tokens[0] << "'\n"; return; }

    std::string err;
    auto addr = ResolveAddress(m_client, tokens[1], &err);
    if (!addr) { std::cout << "error: " << err << "\n"; return; }

    auto val = ReadTyped(m_client, *addr, *type);
    if (!val) { std::cout << "error: could not read " << ValueTypeName(*type) << " at "
                           << FormatAddress(*addr) << " (" << m_client.LastError() << ")\n"; return; }
    std::cout << FormatAddress(*addr) << " = " << val->ToString() << "\n";
}

void Shell::CmdWrite(const std::vector<std::string>& tokens) {
    if (tokens.size() < 3) {
        std::cout << "usage: write <type> <addr> <value>   (addr may be base,off1,off2,... for a pointer chain)\n";
        return;
    }
    auto type = ParseValueType(tokens[0]);
    if (!type) { std::cout << "error: unknown type '" << tokens[0] << "'\n"; return; }

    std::string err;
    auto addr = ResolveAddress(m_client, tokens[1], &err);
    if (!addr) { std::cout << "error: " << err << "\n"; return; }

    auto val = ParseTypedValue(*type, tokens[2]);
    if (!val) { std::cout << "error: could not parse '" << tokens[2] << "' as " << ValueTypeName(*type) << "\n"; return; }

    bool ok = WriteTyped(m_client, *addr, *val);
    std::cout << (ok ? ("OK @ " + FormatAddress(*addr)) : ("error: " + m_client.LastError())) << "\n";
}

void Shell::CmdVScan(const std::vector<std::string>& tokens) {
    if (tokens.empty()) {
        std::cout << "usage: vscan <new|next|list|reset> ...  (see 'help')\n";
        return;
    }
    const std::string& sub = tokens[0];

    if (sub == "reset") { m_scanner.Reset(); std::cout << "scan cleared\n"; return; }

    if (sub == "new") {
        if (tokens.size() < 4) {
            std::cout << "usage: vscan new <type> <addr> <len> [unaligned] [exact <value>]\n";
            return;
        }
        auto type = ParseValueType(tokens[1]);
        auto addr = ParseIntArg(tokens[2]);
        auto len = ParseIntArg(tokens[3]);
        if (!type || !addr || !len) { std::cout << "error: could not parse type/addr/len\n"; return; }

        bool alignedOnly = true;
        std::optional<TypedValue> exact;
        for (size_t i = 4; i < tokens.size(); ++i) {
            if (tokens[i] == "unaligned") {
                alignedOnly = false;
            } else if (tokens[i] == "exact" && i + 1 < tokens.size()) {
                exact = ParseTypedValue(*type, tokens[i + 1]);
                if (!exact) { std::cout << "error: could not parse exact value\n"; return; }
                ++i;
            }
        }

        std::cout << "scanning " << FormatAddress(*addr) << " + " << FormatAddress(*len) << " as "
                   << ValueTypeName(*type) << "...\n";
        size_t n = m_scanner.FirstScan(*addr, *len, *type, exact, alignedOnly, PrintProgress);
        std::cout << "\r" << n << " candidate(s). Play/change the value, then run 'vscan next ...'.\n";
        return;
    }

    if (sub == "next") {
        if (!m_scanner.HasScan()) { std::cout << "error: no active scan -- run 'vscan new' first\n"; return; }
        if (tokens.size() < 2) {
            std::cout << "usage: vscan next <changed|unchanged|increased|decreased>  or  vscan next exact <value>\n";
            return;
        }
        NextScanMode mode;
        std::optional<TypedValue> exact;
        if (tokens[1] == "changed") mode = NextScanMode::Changed;
        else if (tokens[1] == "unchanged") mode = NextScanMode::Unchanged;
        else if (tokens[1] == "increased") mode = NextScanMode::Increased;
        else if (tokens[1] == "decreased") mode = NextScanMode::Decreased;
        else if (tokens[1] == "exact" && tokens.size() >= 3) {
            mode = NextScanMode::Exact;
            exact = ParseTypedValue(m_scanner.Type(), tokens[2]);
            if (!exact) { std::cout << "error: could not parse exact value\n"; return; }
        } else {
            std::cout << "error: unknown mode '" << tokens[1] << "'\n";
            return;
        }

        size_t n = m_scanner.NextScan(mode, exact);
        std::cout << n << " candidate(s) remaining.\n";
        return;
    }

    if (sub == "list") {
        size_t limit = 50;
        if (tokens.size() >= 2) {
            if (auto v = ParseIntArg(tokens[1])) limit = static_cast<size_t>(*v);
        }
        auto& candidates = m_scanner.Candidates();
        size_t shown = 0;
        for (auto& c : candidates) {
            if (shown >= limit) break;
            std::cout << FormatAddress(c.address) << " = " << c.value.ToString() << "\n";
            ++shown;
        }
        std::cout << shown << " of " << candidates.size() << " candidate(s) shown\n";
        return;
    }

    std::cout << "error: unknown vscan subcommand '" << sub << "'\n";
}

void Shell::CmdFreeze(const std::vector<std::string>& tokens) {
    if (tokens.empty()) {
        std::cout << "usage: freeze <add|rm|enable|disable|list|start|stop|save|load> ...  (see 'help')\n";
        return;
    }
    const std::string& sub = tokens[0];

    if (sub == "add") {
        if (tokens.size() < 5) { std::cout << "usage: freeze add <name> <type> <addr> <value>\n"; return; }
        auto type = ParseValueType(tokens[2]);
        if (!type) { std::cout << "error: unknown type '" << tokens[2] << "'\n"; return; }
        auto value = ParseTypedValue(*type, tokens[4]);
        if (!value) { std::cout << "error: could not parse value\n"; return; }

        FreezeEntry e;
        e.name = tokens[1];
        e.type = *type;
        e.addressExpr = tokens[3];
        e.value = *value;
        e.enabled = true;
        bool ok = m_freeze.Add(e);
        std::cout << (ok ? "added" : "error: an entry with that name already exists") << "\n";
        return;
    }

    if (sub == "rm") {
        if (tokens.size() < 2) { std::cout << "usage: freeze rm <name>\n"; return; }
        std::cout << (m_freeze.Remove(tokens[1]) ? "removed" : "error: no such entry") << "\n";
        return;
    }

    if (sub == "enable" || sub == "disable") {
        if (tokens.size() < 2) { std::cout << "usage: freeze " << sub << " <name>\n"; return; }
        bool ok = m_freeze.SetEnabled(tokens[1], sub == "enable");
        std::cout << (ok ? "ok" : "error: no such entry") << "\n";
        return;
    }

    if (sub == "list") {
        auto entries = m_freeze.List();
        std::cout << "engine: " << (m_freeze.IsRunning() ? "running" : "stopped") << "\n";
        for (auto& e : entries) {
            std::cout << (e.enabled ? "[on]  " : "[off] ") << e.name << "  " << ValueTypeName(e.type)
                       << "  addr=" << e.addressExpr << "  value=" << e.value.ToString();
            if (!e.lastStatus.empty()) std::cout << "  (" << e.lastStatus << ")";
            std::cout << "\n";
        }
        std::cout << entries.size() << " entrie(s)\n";
        return;
    }

    if (sub == "start") {
        int interval = 200;
        if (tokens.size() >= 2) {
            if (auto v = ParseIntArg(tokens[1])) interval = static_cast<int>(*v);
        }
        m_freeze.Start(interval);
        std::cout << "freeze engine started (interval " << interval << "ms)\n";
        return;
    }

    if (sub == "stop") {
        m_freeze.Stop();
        std::cout << "freeze engine stopped\n";
        return;
    }

    if (sub == "save") {
        if (tokens.size() < 2) { std::cout << "usage: freeze save <file.json>\n"; return; }
        std::cout << (m_freeze.SaveToFile(tokens[1]) ? "saved" : "error: could not write file") << "\n";
        return;
    }

    if (sub == "load") {
        if (tokens.size() < 2) { std::cout << "usage: freeze load <file.json>\n"; return; }
        std::string err;
        bool ok = m_freeze.LoadFromFile(tokens[1], &err);
        std::cout << (ok ? "loaded" : ("error: " + err)) << "\n";
        return;
    }

    std::cout << "error: unknown freeze subcommand '" << sub << "'\n";
}

void Shell::CmdDirList(const std::vector<std::string>& tokens) {
    std::string path = tokens.empty() ? "hdd:\\" : tokens[0];
    auto entries = m_client.DirList(path);
    if (!entries) { std::cout << "error: " << m_client.LastError() << "\n"; return; }
    for (auto& e : *entries) {
        if (!e.name.empty()) {
            std::cout << (e.isDirectory ? "<DIR> " : "      ") << e.name;
            if (!e.isDirectory) std::cout << "  (" << e.size << " bytes)";
            std::cout << "\n";
        } else {
            std::cout << e.raw << "\n";
        }
    }
    std::cout << entries->size() << " entrie(s)\n";
}

void Shell::CmdDelete(const std::vector<std::string>& tokens) {
    if (tokens.empty()) { std::cout << "usage: delete <path> [dir]\n"; return; }
    bool isDir = tokens.size() >= 2 && tokens[1] == "dir";
    bool ok = m_client.Delete(tokens[0], isDir);
    std::cout << (ok ? "deleted" : ("error: " + m_client.LastError())) << "\n";
}

void Shell::CmdMkdir(const std::vector<std::string>& tokens) {
    if (tokens.empty()) { std::cout << "usage: mkdir <path>\n"; return; }
    bool ok = m_client.MakeDirectory(tokens[0]);
    std::cout << (ok ? "created" : ("error: " + m_client.LastError())) << "\n";
}

void Shell::CmdNotify(const std::vector<std::string>& tokens) {
    if (tokens.empty()) { std::cout << "usage: notify <text>\n"; return; }
    std::ostringstream text;
    for (size_t i = 0; i < tokens.size(); ++i) {
        if (i) text << ' ';
        text << tokens[i];
    }
    bool ok = m_client.Notify(text.str());
    std::cout << (ok ? "sent" : "note: console did not acknowledge (this dashboard may not support notify)") << "\n";
}

void Shell::CmdConsoles(const std::vector<std::string>& tokens) {
    if (tokens.empty()) { std::cout << "usage: consoles <add|list|rm> ...\n"; return; }
    const std::string& sub = tokens[0];

    if (sub == "add") {
        if (tokens.size() < 3) { std::cout << "usage: consoles add <name> <ip> [port]\n"; return; }
        ConsoleEntry e;
        e.name = tokens[1];
        e.host = tokens[2];
        e.port = 730;
        if (tokens.size() >= 4) {
            if (auto p = ParseIntArg(tokens[3])) e.port = static_cast<uint16_t>(*p);
        }
        m_consoleBook.Upsert(e);
        m_consoleBook.Save(ConsoleBook::DefaultPath());
        std::cout << "saved '" << e.name << "' -> " << e.host << ":" << e.port << "\n";
        return;
    }

    if (sub == "list") {
        for (auto& e : m_consoleBook.All()) {
            std::cout << e.name << "  " << e.host << ":" << e.port << "\n";
        }
        std::cout << m_consoleBook.All().size() << " saved console(s)\n";
        return;
    }

    if (sub == "rm") {
        if (tokens.size() < 2) { std::cout << "usage: consoles rm <name>\n"; return; }
        bool ok = m_consoleBook.Remove(tokens[1]);
        if (ok) m_consoleBook.Save(ConsoleBook::DefaultPath());
        std::cout << (ok ? "removed" : "error: no such entry") << "\n";
        return;
    }

    std::cout << "error: unknown consoles subcommand '" << sub << "'\n";
}

void Shell::CmdConnect(const std::vector<std::string>& tokens) {
    if (tokens.empty()) { std::cout << "usage: connect <ip|nickname> [port]\n"; return; }

    std::string host = tokens[0];
    uint16_t port = 730;
    if (auto saved = m_consoleBook.Find(tokens[0])) {
        host = saved->host;
        port = saved->port;
    }
    if (tokens.size() >= 2) {
        if (auto p = ParseIntArg(tokens[1])) port = static_cast<uint16_t>(*p);
    }

    m_client.Disconnect();
    std::cout << "connecting to " << host << ":" << port << "...\n";
    if (!m_client.Connect(host, port)) {
        std::cout << "error: " << m_client.LastError() << "\n";
        return;
    }
    std::cout << "connected: " << m_client.Greeting() << "\n";
}

void Shell::CmdSleep(const std::vector<std::string>& tokens) {
    if (tokens.empty()) { std::cout << "usage: sleep <ms>\n"; return; }
    auto ms = ParseIntArg(tokens[0]);
    if (!ms) { std::cout << "error: could not parse milliseconds\n"; return; }
    std::this_thread::sleep_for(std::chrono::milliseconds(*ms));
}

bool Shell::Dispatch(const std::vector<std::string>& tokens) {
    if (tokens.empty()) return true;
    if (!tokens[0].empty() && tokens[0][0] == '#') return true; // comment line (script mode)

    const std::string& cmd = tokens[0];
    std::vector<std::string> rest(tokens.begin() + 1, tokens.end());

    if (cmd == "quit" || cmd == "exit") return false;
    if (cmd == "help" || cmd == "?") { PrintHelp(); return true; }
    if (cmd == "raw") { CmdRaw(rest); return true; }
    if (cmd == "dbgname") { CmdDbgName(); return true; }
    if (cmd == "modules") { CmdModules(); return true; }
    if (cmd == "threads") { CmdThreads(); return true; }
    if (cmd == "walkmem") { CmdWalkMem(); return true; }
    if (cmd == "getmem") { CmdGetMem(rest); return true; }
    if (cmd == "setmem") { CmdSetMem(rest); return true; }
    if (cmd == "xbeinfo") { CmdXbeInfo(); return true; }
    if (cmd == "reboot") { CmdReboot(rest); return true; }
    if (cmd == "scan") { CmdScan(rest); return true; }
    if (cmd == "scanall") { CmdScanAll(rest); return true; }
    if (cmd == "read") { CmdRead(rest); return true; }
    if (cmd == "write") { CmdWrite(rest); return true; }
    if (cmd == "vscan") { CmdVScan(rest); return true; }
    if (cmd == "freeze") { CmdFreeze(rest); return true; }
    if (cmd == "dirlist" || cmd == "ls") { CmdDirList(rest); return true; }
    if (cmd == "delete" || cmd == "rm") { CmdDelete(rest); return true; }
    if (cmd == "mkdir") { CmdMkdir(rest); return true; }
    if (cmd == "notify") { CmdNotify(rest); return true; }
    if (cmd == "consoles") { CmdConsoles(rest); return true; }
    if (cmd == "connect") { CmdConnect(rest); return true; }
    if (cmd == "sleep") { CmdSleep(rest); return true; }

    // Unknown local command: forward it verbatim as a raw XBDM command so
    // the shell stays useful for commands it doesn't specifically wrap.
    CmdRaw(tokens);
    return true;
}

void Shell::Run() {
    std::cout << "Connected: " << m_client.Greeting() << "\n";
    std::cout << "Type 'help' for commands, 'quit' to exit.\n";
    std::string line;
    for (;;) {
        std::cout << "toastylink> " << std::flush;
        if (!std::getline(std::cin, line)) break;
        auto tokens = Tokenize(line);
        if (!Dispatch(tokens)) break;
        if (!m_client.IsConnected()) {
            std::cout << "connection closed by console.\n";
            break;
        }
    }
}

void Shell::RunScript(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        std::cout << "error: could not open script '" << path << "'\n";
        return;
    }
    std::string line;
    int lineNo = 0;
    while (std::getline(in, line)) {
        ++lineNo;
        auto tokens = Tokenize(line);
        if (tokens.empty()) continue;
        std::cout << "[" << lineNo << "] " << line << "\n";
        if (!Dispatch(tokens)) break;
    }
}

} // namespace tl

#include "ToastyLink/Shell.h"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <thread>

#include "ToastyLink/AddressResolver.h"
#include "ToastyLink/Assembler.h"
#include "ToastyLink/Discovery.h"
#include "ToastyLink/HexUtils.h"
#include "ToastyLink/MemoryScanner.h"

#include <algorithm>
#include <cctype>
#include <filesystem>

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

Shell::Shell(XbdmClient& client) : m_client(client), m_scanner(client), m_freeze(client), m_patch(client) {
    m_consoleBook.Load(ConsoleBook::DefaultPath());
}

std::string Shell::TitleFingerprint() {
    std::string name = "unknowntitle";
    if (auto info = m_client.GetRunningXbeInfo()) {
        for (auto& line : *info) {
            size_t pos = line.find("name=\"");
            if (pos != std::string::npos) {
                size_t start = pos + 6;
                size_t end = line.find('"', start);
                if (end != std::string::npos) { name = line.substr(start, end - start); break; }
            }
        }
    }

    uint64_t checksum = 0, timestamp = 0;
    if (auto mods = m_client.ListModules()) {
        uint64_t bestSize = 0;
        for (auto& m : *mods) {
            if (m.size > bestSize) { bestSize = m.size; checksum = m.checksum; timestamp = m.timestamp; }
        }
    }

    std::string safe;
    for (char c : name) {
        safe.push_back(std::isalnum(static_cast<unsigned char>(c)) ? c : '_');
    }
    if (safe.empty()) safe = "unknowntitle";

    char suffix[32];
    std::snprintf(suffix, sizeof(suffix), "_%08llX%08llX", static_cast<unsigned long long>(checksum),
                  static_cast<unsigned long long>(timestamp));
    return safe + suffix;
}

void Shell::PrintHelp() const {
    std::cout <<
        "Connection & console book:\n"
        "  discover <subnet-prefix>         find XBDM consoles on your LAN, e.g. discover 192.168.1\n"
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
        "  watch <type> <addr> [count] [ms] repeatedly re-read a value (default 20x @ 500ms)\n"
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
        "  freeze autosave / autoload  save/load under a name derived from the running title\n"
        "\n"
        "Code patches (PPC/Xenon; one-shot install with tracked, revertible original bytes):\n"
        "  asm nop | asm blr | asm b <from> <target> | asm bl <from> <target> | asm li <reg> <value>\n"
        "  patch install <name> <addr> hex <hexbytes>\n"
        "  patch install <name> <addr> asm <nop|blr|b <target>|bl <target>|li <reg> <value>>\n"
        "  patch revert <name>  /  patch reinstall <name>  /  patch rm <name>\n"
        "  patch list\n"
        "  patch save <file.json>      /   patch load <file.json>\n"
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

    if (sub == "autosave") {
        std::string dir = ConsoleBook::ConfigDir() + "/tables";
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        std::string path = dir + "/" + TitleFingerprint() + ".json";
        bool ok = m_freeze.SaveToFile(path);
        std::cout << (ok ? ("saved to " + path) : "error: could not write file") << "\n";
        return;
    }

    if (sub == "autoload") {
        std::string path = ConsoleBook::ConfigDir() + "/tables/" + TitleFingerprint() + ".json";
        std::string err;
        bool ok = m_freeze.LoadFromFile(path, &err);
        std::cout << (ok ? ("loaded from " + path) : ("error: " + err)) << "\n";
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

void Shell::CmdDiscover(const std::vector<std::string>& tokens) {
    if (tokens.empty()) {
        std::cout << "usage: discover <subnet-prefix>   e.g. discover 192.168.1\n";
        return;
    }
    std::cout << "scanning " << tokens[0] << ".1-254 on port 730...\n";
    // DiscoverConsoles runs many worker threads concurrently, so this
    // callback can be invoked from several of them at once -- guard the
    // shared "last printed percent" state and the actual write together,
    // or concurrent writes to std::cout interleave into garbage.
    std::mutex progressMutex;
    int lastPct = -1;
    auto found = DiscoverConsoles(tokens[0], 730, 300, 64, [&](int scanned, int total) {
        std::lock_guard<std::mutex> lock(progressMutex);
        int pct = total > 0 ? (scanned * 100) / total : 100;
        if (pct != lastPct) { lastPct = pct; std::cout << "\r  " << pct << "%   " << std::flush; }
    });
    std::cout << "\r" << found.size() << " console(s) found:\n";
    for (auto& c : found) std::cout << "  " << c.host << "  (" << c.greeting << ")\n";
    if (!found.empty()) {
        std::cout << "tip: 'connect " << found[0].host << "' or 'consoles add <name> " << found[0].host << "'\n";
    }
}

void Shell::CmdAsm(const std::vector<std::string>& tokens) {
    if (tokens.empty()) {
        std::cout << "usage: asm nop | asm blr | asm b <from> <target> | asm bl <from> <target> | asm li <reg> <value>\n";
        return;
    }
    const std::string& op = tokens[0];
    std::optional<std::vector<uint8_t>> bytes;
    std::string err;

    if (op == "nop") {
        bytes = AssembleNop();
    } else if (op == "blr") {
        bytes = AssembleBlr();
    } else if (op == "b" || op == "bl") {
        if (tokens.size() < 3) { std::cout << "usage: asm " << op << " <from> <target>\n"; return; }
        auto from = ParseIntArg(tokens[1]);
        auto target = ParseIntArg(tokens[2]);
        if (!from || !target) { std::cout << "error: could not parse from/target\n"; return; }
        bytes = AssembleBranch(*from, *target, op == "bl", &err);
    } else if (op == "li") {
        if (tokens.size() < 3) { std::cout << "usage: asm li <reg 0-31> <value>\n"; return; }
        bytes = AssembleLine(tokens, 0, &err);
    } else {
        std::cout << "error: unknown mnemonic '" << op << "' (supported: nop, blr, b, bl, li)\n";
        return;
    }

    if (!bytes) { std::cout << "error: " << err << "\n"; return; }
    std::cout << BytesToHex(*bytes) << "\n";
}

void Shell::CmdPatch(const std::vector<std::string>& tokens) {
    if (tokens.empty()) {
        std::cout << "usage: patch <install|revert|reinstall|rm|list|save|load> ...  (see 'help')\n";
        return;
    }
    const std::string& sub = tokens[0];

    if (sub == "install") {
        if (tokens.size() < 4) {
            std::cout << "usage: patch install <name> <addr> hex <hexbytes>\n"
                          "       patch install <name> <addr> asm <nop|blr|b <target>|bl <target>|li <reg> <value>>\n";
            return;
        }
        auto addr = ParseIntArg(tokens[2]);
        if (!addr) { std::cout << "error: could not parse address\n"; return; }

        std::vector<uint8_t> bytes;
        if (tokens[3] == "hex") {
            if (tokens.size() < 5) { std::cout << "usage: patch install <name> <addr> hex <hexbytes>\n"; return; }
            auto parsed = HexToBytes(tokens[4]);
            if (!parsed) { std::cout << "error: could not parse hex bytes\n"; return; }
            bytes = *parsed;
        } else if (tokens[3] == "asm") {
            std::vector<std::string> asmTokens(tokens.begin() + 4, tokens.end());
            std::string asmErr;
            auto parsed = AssembleLine(asmTokens, *addr, &asmErr);
            if (!parsed) { std::cout << "error: " << asmErr << "\n"; return; }
            bytes = *parsed;
        } else {
            std::cout << "error: expected 'hex' or 'asm' after the address\n";
            return;
        }

        std::string err;
        bool ok = m_patch.Install(tokens[1], *addr, bytes, &err);
        std::cout << (ok ? ("installed (" + BytesToHex(bytes) + ")") : ("error: " + err)) << "\n";
        return;
    }

    if (sub == "revert" || sub == "reinstall") {
        if (tokens.size() < 2) { std::cout << "usage: patch " << sub << " <name>\n"; return; }
        std::string err;
        bool ok = sub == "revert" ? m_patch.Revert(tokens[1], &err) : m_patch.Reinstall(tokens[1], &err);
        std::cout << (ok ? "ok" : ("error: " + err)) << "\n";
        return;
    }

    if (sub == "rm") {
        if (tokens.size() < 2) { std::cout << "usage: patch rm <name>\n"; return; }
        std::cout << (m_patch.Remove(tokens[1]) ? "removed" : "error: no such patch") << "\n";
        return;
    }

    if (sub == "list") {
        auto entries = m_patch.List();
        for (auto& e : entries) {
            std::cout << (e.installed ? "[on]  " : "[off] ") << e.name << "  addr=" << FormatAddress(e.address)
                       << "  new=" << BytesToHex(e.newBytes) << "  orig=" << BytesToHex(e.originalBytes) << "\n";
        }
        std::cout << entries.size() << " patch(es)\n";
        return;
    }

    if (sub == "save") {
        if (tokens.size() < 2) { std::cout << "usage: patch save <file.json>\n"; return; }
        std::cout << (m_patch.SaveToFile(tokens[1]) ? "saved" : "error: could not write file") << "\n";
        return;
    }

    if (sub == "load") {
        if (tokens.size() < 2) { std::cout << "usage: patch load <file.json>\n"; return; }
        std::string err;
        bool ok = m_patch.LoadFromFile(tokens[1], &err);
        std::cout << (ok ? "loaded (use 'patch reinstall <name>' to apply)" : ("error: " + err)) << "\n";
        return;
    }

    std::cout << "error: unknown patch subcommand '" << sub << "'\n";
}

void Shell::CmdWatch(const std::vector<std::string>& tokens) {
    if (tokens.size() < 2) {
        std::cout << "usage: watch <type> <addr> [count=20] [intervalMs=500]\n";
        return;
    }
    auto type = ParseValueType(tokens[0]);
    if (!type) { std::cout << "error: unknown type '" << tokens[0] << "'\n"; return; }

    std::string err;
    auto addr = ResolveAddress(m_client, tokens[1], &err);
    if (!addr) { std::cout << "error: " << err << "\n"; return; }

    int count = 20;
    int intervalMs = 500;
    if (tokens.size() >= 3) { if (auto v = ParseIntArg(tokens[2])) count = static_cast<int>(*v); }
    if (tokens.size() >= 4) { if (auto v = ParseIntArg(tokens[3])) intervalMs = static_cast<int>(*v); }

    for (int i = 0; i < count; ++i) {
        auto val = ReadTyped(m_client, *addr, *type);
        std::cout << "[" << (i + 1) << "/" << count << "] " << FormatAddress(*addr) << " = "
                   << (val ? val->ToString() : std::string("??")) << "\n";
        if (i + 1 < count) std::this_thread::sleep_for(std::chrono::milliseconds(intervalMs));
    }
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
    if (cmd == "discover") { CmdDiscover(rest); return true; }
    if (cmd == "asm") { CmdAsm(rest); return true; }
    if (cmd == "patch") { CmdPatch(rest); return true; }
    if (cmd == "watch") { CmdWatch(rest); return true; }

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

#include "XenonLink/Shell.h"

#include <cstdio>
#include <iostream>
#include <sstream>

#include "XenonLink/HexUtils.h"
#include "XenonLink/MemoryScanner.h"

namespace xl {

std::vector<std::string> Tokenize(const std::string& line) {
    std::vector<std::string> out;
    std::istringstream iss(line);
    std::string tok;
    while (iss >> tok) out.push_back(tok);
    return out;
}

void Shell::PrintHelp() const {
    std::cout <<
        "Commands:\n"
        "  dbgname                          show the console's debug name\n"
        "  modules                          list loaded modules\n"
        "  threads                          list running thread IDs\n"
        "  walkmem                          list mapped memory regions\n"
        "  getmem <addr> <len>              read memory (hex/decimal addr & len)\n"
        "  setmem <addr> <hexbytes>         write bytes, e.g. setmem 0x82000000 DEADBEEF\n"
        "  xbeinfo                          info about the running title\n"
        "  scan <addr> <len> <pattern>      AOB scan a range, e.g. scan 0x82000000 0x10000 48 65 ?? 6F\n"
        "  scanall <pattern>                AOB scan every mapped region (slow)\n"
        "  reboot [title|cold]              reboot the console\n"
        "  raw <xbdm command...>            send a raw XBDM command and print the reply verbatim\n"
        "  help                             show this text\n"
        "  quit / exit                      close the connection and exit\n"
        "\n"
        "Anything that isn't one of the above is sent verbatim as a raw XBDM\n"
        "command, so any command your console's XBDM implementation supports\n"
        "works even if this shell doesn't have a dedicated wrapper for it.\n";
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
    if (!name) {
        std::cout << "error: " << m_client.LastError() << "\n";
        return;
    }
    std::cout << *name << "\n";
}

void Shell::CmdModules() {
    auto mods = m_client.ListModules();
    if (!mods) {
        std::cout << "error: " << m_client.LastError() << "\n";
        return;
    }
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
    if (!threads) {
        std::cout << "error: " << m_client.LastError() << "\n";
        return;
    }
    for (auto id : *threads) std::cout << id << "\n";
    std::cout << threads->size() << " thread(s)\n";
}

void Shell::CmdWalkMem() {
    auto regions = m_client.WalkMemory();
    if (!regions) {
        std::cout << "error: " << m_client.LastError() << "\n";
        return;
    }
    for (auto& r : *regions) {
        std::cout << FormatAddress(r.base) << "  size=" << FormatAddress(r.size)
                   << "  protect=0x" << std::hex << r.protect << std::dec << "\n";
    }
    std::cout << regions->size() << " region(s)\n";
}

void Shell::CmdGetMem(const std::vector<std::string>& tokens) {
    if (tokens.size() < 2) {
        std::cout << "usage: getmem <addr> <len>\n";
        return;
    }
    auto addr = ParseIntArg(tokens[0]);
    auto len = ParseIntArg(tokens[1]);
    if (!addr || !len) {
        std::cout << "error: could not parse address/length\n";
        return;
    }
    auto bytes = m_client.GetMemory(*addr, static_cast<uint32_t>(*len));
    if (!bytes) {
        std::cout << "error: " << m_client.LastError() << "\n";
        return;
    }
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
    if (tokens.size() < 2) {
        std::cout << "usage: setmem <addr> <hexbytes>\n";
        return;
    }
    auto addr = ParseIntArg(tokens[0]);
    auto bytes = HexToBytes(tokens[1]);
    if (!addr || !bytes) {
        std::cout << "error: could not parse address/hex bytes\n";
        return;
    }
    bool ok = m_client.SetMemory(*addr, *bytes);
    std::cout << (ok ? "OK" : ("error: " + m_client.LastError())) << "\n";
}

void Shell::CmdXbeInfo() {
    auto info = m_client.GetRunningXbeInfo();
    if (!info) {
        std::cout << "error: " << m_client.LastError() << "\n";
        return;
    }
    for (auto& l : *info) std::cout << l << "\n";
}

void Shell::CmdReboot(const std::vector<std::string>& tokens) {
    std::string mode = tokens.empty() ? "" : tokens[0];
    bool ok = m_client.Reboot(mode);
    std::cout << (ok ? "reboot requested" : ("error: " + m_client.LastError())) << "\n";
}

void Shell::CmdScan(const std::vector<std::string>& tokens) {
    // scan <addr> <len> <pattern...>
    if (tokens.size() < 3) {
        std::cout << "usage: scan <addr> <len> <pattern bytes, e.g. 48 65 ?? 6F>\n";
        return;
    }
    auto addr = ParseIntArg(tokens[0]);
    auto len = ParseIntArg(tokens[1]);
    if (!addr || !len) {
        std::cout << "error: could not parse address/length\n";
        return;
    }
    std::ostringstream patternText;
    for (size_t i = 2; i < tokens.size(); ++i) {
        if (i > 2) patternText << ' ';
        patternText << tokens[i];
    }
    auto pattern = ParsePattern(patternText.str());
    if (!pattern) {
        std::cout << "error: could not parse pattern (use two hex digits or ?? per byte)\n";
        return;
    }

    MemoryScanner scanner(m_client);
    std::cout << "scanning " << FormatAddress(*addr) << " + " << FormatAddress(*len) << "...\n";
    auto matches = scanner.ScanRange(*addr, *len, *pattern, [](uint64_t scanned, uint64_t total) {
        if (total == 0) return;
        static uint64_t lastPct = 100;
        uint64_t pct = (scanned * 100) / total;
        if (pct != lastPct) {
            lastPct = pct;
            std::cout << "\r  " << pct << "%   " << std::flush;
        }
    });
    std::cout << "\r";
    std::cout << matches.size() << " match(es):\n";
    for (auto m : matches) std::cout << "  " << FormatAddress(m) << "\n";
}

bool Shell::Dispatch(const std::vector<std::string>& tokens) {
    if (tokens.empty()) return true;
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
    if (cmd == "scanall") {
        if (rest.empty()) { std::cout << "usage: scanall <pattern>\n"; return true; }
        std::ostringstream patternText;
        for (size_t i = 0; i < rest.size(); ++i) {
            if (i) patternText << ' ';
            patternText << rest[i];
        }
        auto pattern = ParsePattern(patternText.str());
        if (!pattern) {
            std::cout << "error: could not parse pattern\n";
            return true;
        }
        MemoryScanner scanner(m_client);
        std::cout << "scanning all mapped regions (this can take a while)...\n";
        auto matches = scanner.ScanAllRegions(*pattern, [](uint64_t scanned, uint64_t total) {
            if (total == 0) return;
            static uint64_t lastPct = 100;
            uint64_t pct = (scanned * 100) / total;
            if (pct != lastPct) {
                lastPct = pct;
                std::cout << "\r  " << pct << "%   " << std::flush;
            }
        });
        std::cout << "\r" << matches.size() << " match(es):\n";
        for (auto m : matches) std::cout << "  " << FormatAddress(m) << "\n";
        return true;
    }

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
        std::cout << "xenonlink> " << std::flush;
        if (!std::getline(std::cin, line)) break;
        auto tokens = Tokenize(line);
        if (!Dispatch(tokens)) break;
        if (!m_client.IsConnected()) {
            std::cout << "connection closed by console.\n";
            break;
        }
    }
}

} // namespace xl

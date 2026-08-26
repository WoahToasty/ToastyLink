// XenonLink -- a from-scratch C++ client for the XBDM debug protocol used
// by Xbox 360 dashboards/kernels with debugging enabled (RGH/JTAG consoles
// running Dashlaunch or an equivalent softmod, or devkit-mode consoles).
//
// Usage:
//   xenonlink <console-ip> [port]              interactive shell
//   xenonlink <console-ip> [port] -- <command...>   run one command, print, exit
//
// See README.md for setup notes and the full command list ('help' inside
// the shell also lists them).
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "XenonLink/Shell.h"
#include "XenonLink/Socket.h"
#include "XenonLink/XbdmClient.h"

namespace {

void PrintUsage(const char* argv0) {
    std::cout <<
        "XenonLink - C++ XBDM client for Xbox 360 RGH/JTAG consoles\n\n"
        "Usage:\n"
        "  " << argv0 << " <console-ip> [port]                interactive shell (default port 730)\n"
        "  " << argv0 << " <console-ip> [port] -- <command...> run one command and exit\n\n"
        "Examples:\n"
        "  " << argv0 << " 192.168.1.50\n"
        "  " << argv0 << " 192.168.1.50 730 -- dbgname\n"
        "  " << argv0 << " 192.168.1.50 -- getmem 0x82000000 0x100\n";
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        PrintUsage(argv[0]);
        return 1;
    }

    std::string host = argv[1];
    uint16_t port = 730;
    int nextArg = 2;

    if (argc > 2) {
        std::string maybePort = argv[2];
        if (maybePort != "--") {
            try {
                int p = std::stoi(maybePort);
                if (p > 0 && p <= 65535) {
                    port = static_cast<uint16_t>(p);
                    nextArg = 3;
                }
            } catch (...) {
                // Not a number; leave default port and let the "--"
                // check below handle it (or fall through to usage).
            }
        }
    }

    std::vector<std::string> oneShotCommand;
    if (nextArg < argc) {
        std::string tok = argv[nextArg];
        if (tok == "--") {
            for (int i = nextArg + 1; i < argc; ++i) oneShotCommand.emplace_back(argv[i]);
        } else {
            std::cerr << "unexpected argument: " << tok << "\n\n";
            PrintUsage(argv[0]);
            return 1;
        }
    }

    std::string sockErr;
    if (!xl::InitSockets(&sockErr)) {
        std::cerr << "socket init failed: " << sockErr << "\n";
        return 1;
    }

    xl::XbdmClient client;
    std::cout << "connecting to " << host << ":" << port << "...\n";
    if (!client.Connect(host, port)) {
        std::cerr << "connect failed: " << client.LastError() << "\n";
        xl::ShutdownSockets();
        return 1;
    }

    xl::Shell shell(client);
    int exitCode = 0;
    if (oneShotCommand.empty()) {
        shell.Run();
    } else {
        shell.Dispatch(oneShotCommand);
    }

    client.Disconnect();
    xl::ShutdownSockets();
    return exitCode;
}

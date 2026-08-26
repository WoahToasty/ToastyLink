// ToastyLink -- a from-scratch C++ trainer/debug toolkit for the XBDM
// protocol used by Xbox 360 dashboards/kernels with debugging enabled
// (RGH/JTAG consoles running Dashlaunch or an equivalent softmod, or
// devkit-mode consoles).
//
// Usage:
//   toastylink <console-ip-or-nickname> [port]                  interactive shell
//   toastylink <console-ip-or-nickname> [port] -- <command...>  run one command, print, exit
//   toastylink <console-ip-or-nickname> [port] --script <file>  run every line of a script, exit
//
// See README.md for setup notes and the full command list ('help' inside
// the shell also lists them).
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "ToastyLink/ConsoleBook.h"
#include "ToastyLink/Shell.h"
#include "ToastyLink/Socket.h"
#include "ToastyLink/Version.h"
#include "ToastyLink/XbdmClient.h"

namespace {

void PrintUsage(const char* argv0) {
    std::cout <<
        "ToastyLink " << tl::kVersion << " - C++ XBDM trainer/debug toolkit for Xbox 360 RGH/JTAG consoles\n\n"
        "Usage:\n"
        "  " << argv0 << " <console-ip-or-nickname> [port]                  interactive shell (default port 730)\n"
        "  " << argv0 << " <console-ip-or-nickname> [port] -- <command...>  run one command and exit\n"
        "  " << argv0 << " <console-ip-or-nickname> [port] --script <file>  run a batch script and exit\n\n"
        "Examples:\n"
        "  " << argv0 << " 192.168.1.50\n"
        "  " << argv0 << " myrgh -- dbgname\n"
        "  " << argv0 << " 192.168.1.50 730 -- getmem 0x82000000 0x100\n"
        "  " << argv0 << " myrgh --script trainer.txt\n\n"
        "A nickname saved with the shell's 'consoles add' command can be used\n"
        "anywhere an IP is expected.\n";
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        PrintUsage(argv[0]);
        return 1;
    }

    if (std::string(argv[1]) == "--version" || std::string(argv[1]) == "-v") {
        std::cout << "ToastyLink " << tl::kVersion << "\n";
        return 0;
    }

    std::string hostArg = argv[1];
    uint16_t port = 730;
    int nextArg = 2;

    if (argc > 2) {
        std::string maybePort = argv[2];
        if (maybePort != "--" && maybePort != "--script") {
            try {
                int p = std::stoi(maybePort);
                if (p > 0 && p <= 65535) {
                    port = static_cast<uint16_t>(p);
                    nextArg = 3;
                }
            } catch (...) {
                // Not a number; fall through and let the flag check below
                // handle it (or fail with a clear "unexpected argument").
            }
        }
    }

    std::vector<std::string> oneShotCommand;
    std::string scriptPath;
    if (nextArg < argc) {
        std::string tok = argv[nextArg];
        if (tok == "--") {
            for (int i = nextArg + 1; i < argc; ++i) oneShotCommand.emplace_back(argv[i]);
        } else if (tok == "--script") {
            if (nextArg + 1 >= argc) {
                std::cerr << "--script requires a file path\n";
                return 1;
            }
            scriptPath = argv[nextArg + 1];
        } else {
            std::cerr << "unexpected argument: " << tok << "\n\n";
            PrintUsage(argv[0]);
            return 1;
        }
    }

    // Resolve a saved nickname to an IP before connecting.
    tl::ConsoleBook book;
    book.Load(tl::ConsoleBook::DefaultPath());
    std::string host = hostArg;
    if (auto saved = book.Find(hostArg)) {
        host = saved->host;
        if (nextArg != 3) port = saved->port; // an explicit port argument still wins
    }

    std::string sockErr;
    if (!tl::InitSockets(&sockErr)) {
        std::cerr << "socket init failed: " << sockErr << "\n";
        return 1;
    }

    tl::XbdmClient client;
    std::cout << "connecting to " << host << ":" << port << "...\n";
    if (!client.Connect(host, port)) {
        std::cerr << "connect failed: " << client.LastError() << "\n";
        tl::ShutdownSockets();
        return 1;
    }

    // Scoped so the Shell -- and with it the FreezeEngine's background
    // thread -- is fully destroyed and joined BEFORE the socket layer is
    // torn down. Calling WSACleanup() while that thread is still doing
    // socket I/O is undefined behaviour, even though it often appears to
    // work.
    {
        tl::Shell shell(client);
        if (!scriptPath.empty()) {
            shell.RunScript(scriptPath);
        } else if (oneShotCommand.empty()) {
            shell.Run();
        } else {
            shell.Dispatch(oneShotCommand);
        }
    }

    client.Disconnect();
    tl::ShutdownSockets();
    return 0;
}

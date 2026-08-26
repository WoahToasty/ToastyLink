// Interactive REPL and single-shot command dispatcher shared by both
// invocation modes (`xenonlink <ip>` for the REPL, `xenonlink <ip> <cmd...>`
// to run one command and exit).
#pragma once

#include <string>
#include <vector>

#include "XenonLink/XbdmClient.h"

namespace xl {

class Shell {
public:
    explicit Shell(XbdmClient& client) : m_client(client) {}

    // Runs one command (already split into tokens) and prints its result.
    // Returns false only for the "quit"/"exit" command, signalling the
    // caller to stop the REPL loop.
    bool Dispatch(const std::vector<std::string>& tokens);

    void Run(); // interactive loop: prompt, read, dispatch, repeat

private:
    XbdmClient& m_client;

    void PrintHelp() const;
    void CmdRaw(const std::vector<std::string>& tokens);
    void CmdDbgName();
    void CmdModules();
    void CmdThreads();
    void CmdWalkMem();
    void CmdGetMem(const std::vector<std::string>& tokens);
    void CmdSetMem(const std::vector<std::string>& tokens);
    void CmdXbeInfo();
    void CmdReboot(const std::vector<std::string>& tokens);
    void CmdScan(const std::vector<std::string>& tokens);
};

std::vector<std::string> Tokenize(const std::string& line);

} // namespace xl

// Interactive REPL and single-shot/batch command dispatcher. Owns the
// higher-level engines (value scanner, freeze/trainer engine, console
// address book) that sit on top of the raw XbdmClient connection.
#pragma once

#include <string>
#include <vector>

#include "ToastyLink/ConsoleBook.h"
#include "ToastyLink/FreezeEngine.h"
#include "ToastyLink/PatchEngine.h"
#include "ToastyLink/ValueScanner.h"
#include "ToastyLink/XbdmClient.h"

namespace tl {

class Shell {
public:
    explicit Shell(XbdmClient& client);

    // Runs one command (already split into tokens) and prints its result.
    // Returns false only for the "quit"/"exit" command, signalling the
    // caller to stop.
    bool Dispatch(const std::vector<std::string>& tokens);

    void Run();                                  // interactive loop
    void RunScript(const std::string& path);      // batch mode: one command per line

private:
    XbdmClient& m_client;
    ValueScanner m_scanner;
    FreezeEngine m_freeze;
    PatchEngine m_patch;
    ConsoleBook m_consoleBook;

    std::string TitleFingerprint(); // stable-ish filename-safe id for the running title

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
    void CmdScanAll(const std::vector<std::string>& tokens);

    void CmdRead(const std::vector<std::string>& tokens);
    void CmdWrite(const std::vector<std::string>& tokens);

    void CmdVScan(const std::vector<std::string>& tokens);
    void CmdFreeze(const std::vector<std::string>& tokens);

    void CmdDirList(const std::vector<std::string>& tokens);
    void CmdDelete(const std::vector<std::string>& tokens);
    void CmdMkdir(const std::vector<std::string>& tokens);
    void CmdNotify(const std::vector<std::string>& tokens);

    void CmdConsoles(const std::vector<std::string>& tokens);
    void CmdConnect(const std::vector<std::string>& tokens);
    void CmdSleep(const std::vector<std::string>& tokens);
    void CmdDiscover(const std::vector<std::string>& tokens);
    void CmdAsm(const std::vector<std::string>& tokens);
    void CmdPatch(const std::vector<std::string>& tokens);
    void CmdWatch(const std::vector<std::string>& tokens);
};

std::vector<std::string> Tokenize(const std::string& line);

} // namespace tl

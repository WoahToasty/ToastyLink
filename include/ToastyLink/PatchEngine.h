// Named, revertible byte patches -- the code-patching counterpart to
// FreezeEngine's memory freezing. Installing a patch reads and remembers
// the original bytes at the target address before overwriting them, so it
// can be reverted exactly, and toggled back on without needing to
// re-derive the new bytes. Patch sets save/load as JSON, same as cheat
// tables, so a title's patch set (remove a check, redirect a call, ...)
// can be shared.
#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace tl {

class XbdmClient;

struct PatchEntry {
    std::string name;
    uint64_t address = 0;
    std::vector<uint8_t> originalBytes; // captured at install time
    std::vector<uint8_t> newBytes;
    bool installed = false; // true if newBytes is currently the live content at address
};

class PatchEngine {
public:
    explicit PatchEngine(XbdmClient& client) : m_client(client) {}

    // Reads len(newBytes) original bytes at `address` (fails if any are
    // unmapped or a patch with this name already exists), stores them,
    // then writes newBytes. The entry is left installed=true.
    bool Install(const std::string& name, uint64_t address, const std::vector<uint8_t>& newBytes,
                 std::string* err = nullptr);

    // Writes the saved original bytes back, leaving the entry loaded but
    // installed=false so it can be re-applied later with Reinstall().
    bool Revert(const std::string& name, std::string* err = nullptr);

    // Re-writes newBytes for an entry that was previously reverted (or
    // loaded from a file), without re-reading "original" bytes again.
    bool Reinstall(const std::string& name, std::string* err = nullptr);

    // Reverts (if installed) and forgets the entry entirely.
    bool Remove(const std::string& name);

    std::vector<PatchEntry> List() const;

    bool SaveToFile(const std::string& path) const;
    // Loaded entries are always marked installed=false regardless of the
    // saved state -- loading a file never writes to the console by
    // itself; call Reinstall() explicitly for each entry you want applied.
    bool LoadFromFile(const std::string& path, std::string* err = nullptr);

private:
    XbdmClient& m_client;
    mutable std::mutex m_mutex;
    std::vector<PatchEntry> m_entries;

    PatchEntry* Find(const std::string& name); // caller must hold m_mutex
};

} // namespace tl

// A tiny address book mapping a nickname to a console's IP/port, stored as
// JSON in the user's home directory. Lets you type `toastylink myrgh`
// instead of memorizing an IP, and is the natural place to remember every
// console you've ever connected to on a LAN with more than one.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace tl {

struct ConsoleEntry {
    std::string name;
    std::string host;
    uint16_t port = 730;
};

class ConsoleBook {
public:
    ConsoleBook() = default;

    bool Load(const std::string& path, std::string* err = nullptr);
    bool Save(const std::string& path) const;

    void Upsert(const ConsoleEntry& entry);
    bool Remove(const std::string& name);
    std::optional<ConsoleEntry> Find(const std::string& name) const;
    const std::vector<ConsoleEntry>& All() const { return m_entries; }

    // ~/.toastylink/consoles.json (or %USERPROFILE%\.toastylink\consoles.json
    // on Windows); created on first Save() if missing.
    static std::string DefaultPath();

    // ~/.toastylink (or %USERPROFILE%\.toastylink on Windows) -- the base
    // directory DefaultPath() lives in, also used for saved cheat/patch
    // tables (see Shell's `freeze autosave`/`patch autosave`).
    static std::string ConfigDir();

private:
    std::vector<ConsoleEntry> m_entries;
};

} // namespace tl

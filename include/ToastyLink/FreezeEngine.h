// A small trainer engine: named "cheat" entries (an address expression --
// literal or pointer chain, see AddressResolver -- a value type, and a
// value to hold) that a background thread continuously rewrites at a
// fixed interval, the classic "freeze" behaviour from Cheat Engine-style
// tools. Entries can be saved/loaded as a JSON "cheat table" file, so a
// trainer for a given title can be written once and shared with anyone
// else running ToastyLink.
#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "ToastyLink/TypedValue.h"

namespace tl {

class XbdmClient;

struct FreezeEntry {
    std::string name;
    std::string addressExpr; // literal address or "base,off1,off2,..." pointer chain
    ValueType type = ValueType::I32;
    TypedValue value;
    bool enabled = true;
    std::string lastStatus; // most recent write result, for `freeze list`
};

class FreezeEngine {
public:
    explicit FreezeEngine(XbdmClient& client) : m_client(client) {}
    ~FreezeEngine();

    FreezeEngine(const FreezeEngine&) = delete;
    FreezeEngine& operator=(const FreezeEngine&) = delete;

    bool Add(const FreezeEntry& entry);           // false if name already exists
    bool Remove(const std::string& name);
    bool SetEnabled(const std::string& name, bool enabled);
    void SetAllEnabled(bool enabled);
    std::vector<FreezeEntry> List() const;

    void Start(int intervalMs = 200); // no-op if already running
    void Stop();
    bool IsRunning() const { return m_running.load(); }

    bool SaveToFile(const std::string& path) const;
    bool LoadFromFile(const std::string& path, std::string* err); // appends/replaces by name

private:
    XbdmClient& m_client;
    mutable std::mutex m_mutex;
    std::vector<FreezeEntry> m_entries;

    std::thread m_thread;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_stopRequested{false};
    int m_intervalMs = 200;

    void ThreadMain();
};

} // namespace tl

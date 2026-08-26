#include "ToastyLink/FreezeEngine.h"

#include <chrono>
#include <fstream>
#include <sstream>

#include "ToastyLink/AddressResolver.h"
#include "ToastyLink/HexUtils.h"
#include "ToastyLink/Json.h"
#include "ToastyLink/XbdmClient.h"

namespace tl {

FreezeEngine::~FreezeEngine() { Stop(); }

bool FreezeEngine::Add(const FreezeEntry& entry) {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& e : m_entries) {
        if (e.name == entry.name) return false;
    }
    m_entries.push_back(entry);
    return true;
}

bool FreezeEngine::Remove(const std::string& name) {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto it = m_entries.begin(); it != m_entries.end(); ++it) {
        if (it->name == name) {
            m_entries.erase(it);
            return true;
        }
    }
    return false;
}

bool FreezeEngine::SetEnabled(const std::string& name, bool enabled) {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& e : m_entries) {
        if (e.name == name) {
            e.enabled = enabled;
            return true;
        }
    }
    return false;
}

void FreezeEngine::SetAllEnabled(bool enabled) {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& e : m_entries) e.enabled = enabled;
}

std::vector<FreezeEntry> FreezeEngine::List() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_entries;
}

void FreezeEngine::Start(int intervalMs) {
    if (m_running.load()) return;
    m_intervalMs = intervalMs > 0 ? intervalMs : 200;
    m_stopRequested.store(false);
    m_running.store(true);
    m_thread = std::thread(&FreezeEngine::ThreadMain, this);
}

void FreezeEngine::Stop() {
    if (!m_running.load()) return;
    m_stopRequested.store(true);
    if (m_thread.joinable()) m_thread.join();
    m_running.store(false);
}

void FreezeEngine::ThreadMain() {
    while (!m_stopRequested.load()) {
        std::vector<FreezeEntry> snapshot;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            for (auto& e : m_entries) {
                if (e.enabled) snapshot.push_back(e);
            }
        }

        std::vector<std::pair<std::string, std::string>> results; // name -> status
        for (auto& e : snapshot) {
            std::string err;
            auto addr = ResolveAddress(m_client, e.addressExpr, &err);
            if (!addr) {
                results.emplace_back(e.name, "error: " + err);
                continue;
            }
            bool ok = WriteTyped(m_client, *addr, e.value);
            results.emplace_back(e.name, ok ? "ok @ " + FormatAddress(*addr)
                                             : "write failed @ " + FormatAddress(*addr));
        }

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            for (auto& r : results) {
                for (auto& e : m_entries) {
                    if (e.name == r.first) { e.lastStatus = r.second; break; }
                }
            }
        }

        // Sleep in short slices so Stop() is responsive even with a long interval.
        int remaining = m_intervalMs;
        while (remaining > 0 && !m_stopRequested.load()) {
            int slice = remaining < 50 ? remaining : 50;
            std::this_thread::sleep_for(std::chrono::milliseconds(slice));
            remaining -= slice;
        }
    }
}

bool FreezeEngine::SaveToFile(const std::string& path) const {
    JsonValue root = JsonValue::MakeArray();
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto& e : m_entries) {
            JsonValue obj = JsonValue::MakeObject();
            obj.Set("name", JsonValue::MakeString(e.name));
            obj.Set("address", JsonValue::MakeString(e.addressExpr));
            obj.Set("type", JsonValue::MakeString(ValueTypeName(e.type)));
            obj.Set("value", JsonValue::MakeString(e.value.ToString()));
            obj.Set("enabled", JsonValue::MakeBool(e.enabled));
            root.arrVal.push_back(std::move(obj));
        }
    }
    std::ofstream out(path, std::ios::binary);
    if (!out) return false;
    out << root.Dump(0) << "\n";
    return static_cast<bool>(out);
}

bool FreezeEngine::LoadFromFile(const std::string& path, std::string* err) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        if (err) *err = "could not open '" + path + "'";
        return false;
    }
    std::ostringstream ss;
    ss << in.rdbuf();

    std::string parseErr;
    auto root = ParseJson(ss.str(), &parseErr);
    if (!root || root->kind != JsonValue::Kind::Array) {
        if (err) *err = "invalid cheat table JSON" + (parseErr.empty() ? "" : (": " + parseErr));
        return false;
    }

    for (auto& item : root->arrVal) {
        if (item.kind != JsonValue::Kind::Object) continue;
        auto name = item.Find("name");
        auto address = item.Find("address");
        auto typeStr = item.Find("type");
        auto valueStr = item.Find("value");
        auto enabled = item.Find("enabled");
        if (!name || !address || !typeStr || !valueStr) continue;

        auto type = ParseValueType(typeStr->strVal);
        if (!type) continue;
        auto value = ParseTypedValue(*type, valueStr->strVal);
        if (!value) continue;

        FreezeEntry e;
        e.name = name->strVal;
        e.addressExpr = address->strVal;
        e.type = *type;
        e.value = *value;
        e.enabled = enabled ? enabled->boolVal : true;

        std::lock_guard<std::mutex> lock(m_mutex);
        bool replaced = false;
        for (auto& existing : m_entries) {
            if (existing.name == e.name) { existing = e; replaced = true; break; }
        }
        if (!replaced) m_entries.push_back(e);
    }
    return true;
}

} // namespace tl

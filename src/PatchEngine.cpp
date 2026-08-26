#include "ToastyLink/PatchEngine.h"

#include <fstream>
#include <sstream>

#include "ToastyLink/HexUtils.h"
#include "ToastyLink/Json.h"
#include "ToastyLink/XbdmClient.h"

namespace tl {

PatchEntry* PatchEngine::Find(const std::string& name) {
    for (auto& e : m_entries) {
        if (e.name == name) return &e;
    }
    return nullptr;
}

bool PatchEngine::Install(const std::string& name, uint64_t address, const std::vector<uint8_t>& newBytes,
                           std::string* err) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (Find(name)) {
        if (err) *err = "a patch named '" + name + "' already exists";
        return false;
    }
    if (newBytes.empty()) {
        if (err) *err = "no bytes to install";
        return false;
    }

    auto original = m_client.GetMemory(address, static_cast<uint32_t>(newBytes.size()));
    if (!original || original->size() != newBytes.size()) {
        if (err) *err = "failed to read original bytes at " + FormatAddress(address) + " (" + m_client.LastError() + ")";
        return false;
    }
    std::vector<uint8_t> origBytes;
    origBytes.reserve(original->size());
    for (auto& b : *original) {
        if (!b.mapped) {
            if (err) *err = "address " + FormatAddress(address) + " is not fully mapped";
            return false;
        }
        origBytes.push_back(b.value);
    }

    if (!m_client.SetMemory(address, newBytes)) {
        if (err) *err = "failed to write patch bytes (" + m_client.LastError() + ")";
        return false;
    }

    PatchEntry e;
    e.name = name;
    e.address = address;
    e.originalBytes = origBytes;
    e.newBytes = newBytes;
    e.installed = true;
    m_entries.push_back(std::move(e));
    return true;
}

bool PatchEngine::Revert(const std::string& name, std::string* err) {
    std::lock_guard<std::mutex> lock(m_mutex);
    PatchEntry* e = Find(name);
    if (!e) {
        if (err) *err = "no such patch";
        return false;
    }
    if (!m_client.SetMemory(e->address, e->originalBytes)) {
        if (err) *err = "failed to write original bytes back (" + m_client.LastError() + ")";
        return false;
    }
    e->installed = false;
    return true;
}

bool PatchEngine::Reinstall(const std::string& name, std::string* err) {
    std::lock_guard<std::mutex> lock(m_mutex);
    PatchEntry* e = Find(name);
    if (!e) {
        if (err) *err = "no such patch";
        return false;
    }
    if (!m_client.SetMemory(e->address, e->newBytes)) {
        if (err) *err = "failed to write patch bytes (" + m_client.LastError() + ")";
        return false;
    }
    e->installed = true;
    return true;
}

bool PatchEngine::Remove(const std::string& name) {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto it = m_entries.begin(); it != m_entries.end(); ++it) {
        if (it->name == name) {
            if (it->installed) m_client.SetMemory(it->address, it->originalBytes);
            m_entries.erase(it);
            return true;
        }
    }
    return false;
}

std::vector<PatchEntry> PatchEngine::List() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_entries;
}

bool PatchEngine::SaveToFile(const std::string& path) const {
    JsonValue root = JsonValue::MakeArray();
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto& e : m_entries) {
            JsonValue obj = JsonValue::MakeObject();
            obj.Set("name", JsonValue::MakeString(e.name));
            obj.Set("address", JsonValue::MakeString(FormatAddress(e.address)));
            obj.Set("original", JsonValue::MakeString(BytesToHex(e.originalBytes)));
            obj.Set("bytes", JsonValue::MakeString(BytesToHex(e.newBytes)));
            root.arrVal.push_back(std::move(obj));
        }
    }
    std::ofstream out(path, std::ios::binary);
    if (!out) return false;
    out << root.Dump(0) << "\n";
    return static_cast<bool>(out);
}

bool PatchEngine::LoadFromFile(const std::string& path, std::string* err) {
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
        if (err) *err = "invalid patch set JSON" + (parseErr.empty() ? "" : (": " + parseErr));
        return false;
    }

    for (auto& item : root->arrVal) {
        if (item.kind != JsonValue::Kind::Object) continue;
        auto name = item.Find("name");
        auto address = item.Find("address");
        auto original = item.Find("original");
        auto bytes = item.Find("bytes");
        if (!name || !address || !original || !bytes) continue;

        auto addr = ParseIntArg(address->strVal);
        auto origBytes = HexToBytes(original->strVal);
        auto newBytes = HexToBytes(bytes->strVal);
        if (!addr || !origBytes || !newBytes) continue;

        PatchEntry e;
        e.name = name->strVal;
        e.address = *addr;
        e.originalBytes = *origBytes;
        e.newBytes = *newBytes;
        e.installed = false;

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

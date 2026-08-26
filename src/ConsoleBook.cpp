#include "ToastyLink/ConsoleBook.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "ToastyLink/Json.h"

namespace tl {

bool ConsoleBook::Load(const std::string& path, std::string* err) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        // A missing address book is not an error -- it just means no
        // consoles have been saved yet.
        return true;
    }
    std::ostringstream ss;
    ss << in.rdbuf();

    std::string parseErr;
    auto root = ParseJson(ss.str(), &parseErr);
    if (!root || root->kind != JsonValue::Kind::Array) {
        if (err) *err = "invalid console book JSON" + (parseErr.empty() ? "" : (": " + parseErr));
        return false;
    }

    m_entries.clear();
    for (auto& item : root->arrVal) {
        if (item.kind != JsonValue::Kind::Object) continue;
        auto name = item.Find("name");
        auto host = item.Find("host");
        auto port = item.Find("port");
        if (!name || !host) continue;
        ConsoleEntry e;
        e.name = name->strVal;
        e.host = host->strVal;
        e.port = port ? static_cast<uint16_t>(port->numVal) : 730;
        m_entries.push_back(std::move(e));
    }
    return true;
}

bool ConsoleBook::Save(const std::string& path) const {
    std::filesystem::path p(path);
    std::error_code ec;
    if (p.has_parent_path()) std::filesystem::create_directories(p.parent_path(), ec);

    JsonValue root = JsonValue::MakeArray();
    for (auto& e : m_entries) {
        JsonValue obj = JsonValue::MakeObject();
        obj.Set("name", JsonValue::MakeString(e.name));
        obj.Set("host", JsonValue::MakeString(e.host));
        obj.Set("port", JsonValue::MakeNumber(e.port));
        root.arrVal.push_back(std::move(obj));
    }

    std::ofstream out(path, std::ios::binary);
    if (!out) return false;
    out << root.Dump(0) << "\n";
    return static_cast<bool>(out);
}

void ConsoleBook::Upsert(const ConsoleEntry& entry) {
    for (auto& e : m_entries) {
        if (e.name == entry.name) { e = entry; return; }
    }
    m_entries.push_back(entry);
}

bool ConsoleBook::Remove(const std::string& name) {
    for (auto it = m_entries.begin(); it != m_entries.end(); ++it) {
        if (it->name == name) { m_entries.erase(it); return true; }
    }
    return false;
}

std::optional<ConsoleEntry> ConsoleBook::Find(const std::string& name) const {
    for (auto& e : m_entries) {
        if (e.name == name) return e;
    }
    return std::nullopt;
}

namespace {
// std::getenv is flagged by MSVC as "unsafe" (it recommends the
// non-portable _dupenv_s instead); it's perfectly safe for a read-only
// lookup of a well-known variable like this, so the warning is silenced
// locally rather than dropping portability for one call.
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
const char* GetEnvVar(const char* name) { return std::getenv(name); }
#ifdef _MSC_VER
#pragma warning(pop)
#endif
} // namespace

std::string ConsoleBook::DefaultPath() {
    std::filesystem::path home;
#ifdef _WIN32
    const char* profile = GetEnvVar("USERPROFILE");
    home = profile ? std::filesystem::path(profile) : std::filesystem::path(".");
#else
    const char* homeEnv = GetEnvVar("HOME");
    home = homeEnv ? std::filesystem::path(homeEnv) : std::filesystem::path(".");
#endif
    return (home / ".toastylink" / "consoles.json").string();
}

} // namespace tl

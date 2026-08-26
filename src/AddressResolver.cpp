#include "ToastyLink/AddressResolver.h"

#include <cctype>
#include <sstream>
#include <vector>

#include "ToastyLink/HexUtils.h"
#include "ToastyLink/XbdmClient.h"

namespace tl {

namespace {

// Splits on ',' WITHOUT discarding empty components, so a malformed
// expression like "0x82000000," or "a,,b" surfaces as an empty component
// the caller rejects. Silently dropping them would quietly demote a
// mistyped pointer chain to a plain literal address -- the kind of
// misinterpretation that writes to entirely the wrong memory.
std::vector<std::string> SplitComma(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == ',') {
            out.push_back(cur);
            cur.clear();
        } else if (!std::isspace(static_cast<unsigned char>(c))) {
            cur.push_back(c);
        }
    }
    out.push_back(cur);
    return out;
}

} // namespace

std::optional<uint64_t> ResolveAddress(XbdmClient& client, const std::string& expr, std::string* err) {
    std::vector<std::string> tokens = SplitComma(expr);
    if (tokens.empty()) {
        if (err) *err = "empty address expression";
        return std::nullopt;
    }

    std::vector<uint64_t> values;
    values.reserve(tokens.size());
    for (auto& t : tokens) {
        auto v = ParseIntArg(t);
        if (!v) {
            if (err) *err = "could not parse '" + t + "' as a number";
            return std::nullopt;
        }
        values.push_back(*v);
    }

    if (values.size() == 1) return values[0];

    uint64_t cursor = values[0];
    for (size_t i = 1; i < values.size(); ++i) {
        auto bytes = client.GetMemory(cursor, 4);
        if (!bytes || bytes->size() != 4) {
            if (err) *err = "failed to read pointer at " + FormatAddress(cursor) + " (" + client.LastError() + ")";
            return std::nullopt;
        }
        for (auto& b : *bytes) {
            if (!b.mapped) {
                if (err) *err = "pointer at " + FormatAddress(cursor) + " is not mapped";
                return std::nullopt;
            }
        }
        uint32_t ptrVal = (static_cast<uint32_t>((*bytes)[0].value) << 24) |
                           (static_cast<uint32_t>((*bytes)[1].value) << 16) |
                           (static_cast<uint32_t>((*bytes)[2].value) << 8) |
                           static_cast<uint32_t>((*bytes)[3].value);
        cursor = static_cast<uint64_t>(ptrVal) + values[i];
    }
    return cursor;
}

} // namespace tl

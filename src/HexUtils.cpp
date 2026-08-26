#include "ToastyLink/HexUtils.h"

#include <cctype>
#include <cstdio>
#include <sstream>

namespace tl {

namespace {

int HexNibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

} // namespace

std::string BytesToHex(const std::vector<uint8_t>& bytes) {
    static const char* kDigits = "0123456789ABCDEF";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (uint8_t b : bytes) {
        out.push_back(kDigits[(b >> 4) & 0xF]);
        out.push_back(kDigits[b & 0xF]);
    }
    return out;
}

std::optional<std::vector<uint8_t>> HexToBytes(const std::string& hex) {
    std::vector<uint8_t> out;
    int hi = -1;
    for (char c : hex) {
        if (std::isspace(static_cast<unsigned char>(c))) continue;
        int nibble = HexNibble(c);
        if (nibble < 0) return std::nullopt;
        if (hi < 0) {
            hi = nibble;
        } else {
            out.push_back(static_cast<uint8_t>((hi << 4) | nibble));
            hi = -1;
        }
    }
    if (hi >= 0) return std::nullopt; // odd number of hex digits
    return out;
}

std::optional<std::vector<PatternByte>> ParsePattern(const std::string& pattern) {
    std::vector<PatternByte> out;
    std::istringstream iss(pattern);
    std::string token;
    while (iss >> token) {
        if (token == "?" || token == "??") {
            PatternByte pb;
            pb.wildcard = true;
            out.push_back(pb);
            continue;
        }
        if (token.size() != 2) return std::nullopt;
        int hi = HexNibble(token[0]);
        int lo = HexNibble(token[1]);
        if (hi < 0 || lo < 0) return std::nullopt;
        PatternByte pb;
        pb.wildcard = false;
        pb.value = static_cast<uint8_t>((hi << 4) | lo);
        out.push_back(pb);
    }
    if (out.empty()) return std::nullopt;
    return out;
}

std::optional<uint64_t> ParseIntArg(const std::string& text) {
    if (text.empty()) return std::nullopt;
    std::string t = text;
    int base = 10;
    if (t.size() > 2 && t[0] == '0' && (t[1] == 'x' || t[1] == 'X')) {
        base = 16;
        t = t.substr(2);
    }
    if (t.empty()) return std::nullopt;
    for (char c : t) {
        if (HexNibble(c) < 0) return std::nullopt;
        if (base == 10 && !std::isdigit(static_cast<unsigned char>(c))) return std::nullopt;
    }
    try {
        size_t consumed = 0;
        uint64_t value = std::stoull(t, &consumed, base);
        if (consumed != t.size()) return std::nullopt;
        return value;
    } catch (...) {
        return std::nullopt;
    }
}

std::string FormatAddress(uint64_t addr) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "0x%08llX", static_cast<unsigned long long>(addr));
    return std::string(buf);
}

} // namespace tl

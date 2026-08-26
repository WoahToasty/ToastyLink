#include "ToastyLink/Assembler.h"

#include "ToastyLink/HexUtils.h"

namespace tl {

namespace {

std::vector<uint8_t> PackBE32(uint32_t v) {
    return {static_cast<uint8_t>((v >> 24) & 0xFF), static_cast<uint8_t>((v >> 16) & 0xFF),
            static_cast<uint8_t>((v >> 8) & 0xFF), static_cast<uint8_t>(v & 0xFF)};
}

// ParseIntArg (HexUtils) only accepts non-negative values, which is right
// for addresses but not for li's signed immediate. This accepts an
// optional leading '-' in addition to everything ParseIntArg supports.
std::optional<int64_t> ParseSignedArg(const std::string& text) {
    if (!text.empty() && text[0] == '-') {
        auto mag = ParseIntArg(text.substr(1));
        if (!mag) return std::nullopt;
        return -static_cast<int64_t>(*mag);
    }
    auto v = ParseIntArg(text);
    if (!v) return std::nullopt;
    return static_cast<int64_t>(*v);
}

} // namespace

std::vector<uint8_t> AssembleNop() { return PackBE32(0x60000000u); }

std::vector<uint8_t> AssembleBlr() { return PackBE32(0x4E800020u); }

std::optional<std::vector<uint8_t>> AssembleBranch(uint64_t fromAddr, uint64_t targetAddr, bool link,
                                                     std::string* err) {
    int64_t disp = static_cast<int64_t>(targetAddr) - static_cast<int64_t>(fromAddr);
    if (disp % 4 != 0) {
        if (err) *err = "branch displacement must be a multiple of 4 (unaligned target/source)";
        return std::nullopt;
    }
    // LI field is 24 bits representing a word displacement, i.e. +/-32MB.
    constexpr int64_t kMax = 0x01FFFFFF; // 0x02000000 - 1, matches the 26-bit signed byte range
    constexpr int64_t kMin = -0x02000000;
    if (disp > kMax || disp < kMin) {
        if (err) *err = "branch target is out of range for a relative b/bl (+/-32MB)";
        return std::nullopt;
    }
    uint32_t instr = 0x48000000u | (static_cast<uint32_t>(disp) & 0x03FFFFFCu);
    if (link) instr |= 0x1u;
    return PackBE32(instr);
}

std::optional<std::vector<uint8_t>> AssembleLoadImmediate(int reg, int64_t value, std::string* err) {
    if (reg < 0 || reg > 31) {
        if (err) *err = "register must be r0..r31";
        return std::nullopt;
    }
    if (value < -32768 || value > 65535) {
        if (err) *err = "li value must fit in 16 bits (-32768..65535)";
        return std::nullopt;
    }
    uint32_t instr = 0x38000000u | (static_cast<uint32_t>(reg) << 21) | (static_cast<uint32_t>(value) & 0xFFFFu);
    return PackBE32(instr);
}

std::optional<std::vector<uint8_t>> AssembleLine(const std::vector<std::string>& tokens, uint64_t atAddr,
                                                   std::string* err) {
    if (tokens.empty()) {
        if (err) *err = "empty instruction";
        return std::nullopt;
    }
    const std::string& op = tokens[0];

    if (op == "nop") return AssembleNop();
    if (op == "blr") return AssembleBlr();

    if (op == "b" || op == "bl") {
        if (tokens.size() < 2) {
            if (err) *err = "usage: " + op + " <target>";
            return std::nullopt;
        }
        auto target = ParseIntArg(tokens[1]);
        if (!target) {
            if (err) *err = "could not parse branch target";
            return std::nullopt;
        }
        return AssembleBranch(atAddr, *target, op == "bl", err);
    }

    if (op == "li") {
        if (tokens.size() < 3) {
            if (err) *err = "usage: li <reg 0-31> <value>";
            return std::nullopt;
        }
        auto reg = ParseIntArg(tokens[1]);
        auto val = ParseSignedArg(tokens[2]);
        if (!reg || !val) {
            if (err) *err = "could not parse register/value";
            return std::nullopt;
        }
        return AssembleLoadImmediate(static_cast<int>(*reg), *val, err);
    }

    if (err) *err = "unknown mnemonic '" + op + "' (supported: nop, blr, b, bl, li)";
    return std::nullopt;
}

} // namespace tl

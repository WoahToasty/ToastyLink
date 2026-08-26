// Small hex/byte-pattern helpers shared by the XBDM client and the memory
// scanner. No dependency on XBDM specifics — pure text/byte manipulation.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace tl {

// One byte of a search pattern: either a concrete value to match, or a
// wildcard ("??") that matches anything.
struct PatternByte {
    uint8_t value = 0;
    bool wildcard = false;
};

// Formats bytes as a contiguous uppercase hex string, e.g. {0xDE,0xAD} -> "DEAD".
std::string BytesToHex(const std::vector<uint8_t>& bytes);

// Parses a contiguous hex string ("DEADBEEF" or "de ad be ef") into bytes.
// Returns std::nullopt if the string contains an odd number of hex digits
// or non-hex characters (other than whitespace, which is ignored).
std::optional<std::vector<uint8_t>> HexToBytes(const std::string& hex);

// Parses a space-separated byte pattern where each token is either a two
// hex-digit byte ("4D") or a wildcard ("??" or "?"). Example:
//   "48 65 ?? ?? 6F" matches 'H' 'e' <any> <any> 'o'.
std::optional<std::vector<PatternByte>> ParsePattern(const std::string& pattern);

// Parses a hex or decimal integer address/length, accepting an optional
// "0x"/"0X" prefix. Returns std::nullopt on malformed input.
std::optional<uint64_t> ParseIntArg(const std::string& text);

// Formats an address consistently as "0xXXXXXXXX" (8 hex digits, more if
// the value needs them).
std::string FormatAddress(uint64_t addr);

} // namespace tl

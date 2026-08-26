// Resolves address expressions: either a plain literal address, or a
// CE-style pointer path "base,off1,off2,..." that gets re-resolved live
// against the console every time it's used. Pointer paths matter because
// raw addresses shift between game sessions/reboots, but a pointer chain
// rooted at a stable base (a module's static data, typically) tends to
// stay valid across restarts.
//
// Semantics of "base,off1,off2,...,offN": `base` is the address OF the
// first pointer (i.e. the console holds a 32-bit pointer value AT that
// address). For each offset in turn: read a 32-bit big-endian pointer at
// the current address, add the offset, and that sum becomes either the
// next address to dereference (if more offsets follow) or the final
// resolved address (for the last offset). A single token with no commas
// is just a literal address and is never dereferenced.
#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace tl {

class XbdmClient;

// Returns the resolved address, or nullopt with *err set on failure
// (malformed expression, or a pointer in the chain pointing at unmapped
// memory).
std::optional<uint64_t> ResolveAddress(XbdmClient& client, const std::string& expr,
                                        std::string* err = nullptr);

} // namespace tl

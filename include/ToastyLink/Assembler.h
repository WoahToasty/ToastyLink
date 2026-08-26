// A tiny hand-rolled assembler for the handful of PowerPC/Xenon
// instructions that come up constantly when patching Xbox 360 title code:
// nop, blr, unconditional branch (with or without link), and load-
// immediate. These encodings are fixed by the PowerPC ISA (not build- or
// title-specific), so unlike memory offsets there's nothing here that can
// go stale -- the risk with a wrong branch offset is a crash, so out-of-
// range offsets are rejected rather than silently truncated.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace tl {

// A 4-byte NOP ("ori r0,r0,0"): 0x60000000.
std::vector<uint8_t> AssembleNop();

// "blr" (branch to link register, used to return early from a function): 0x4E800020.
std::vector<uint8_t> AssembleBlr();

// Unconditional relative branch from `fromAddr` to `targetAddr`. `link`
// selects `b`/`bl` (sets the LK bit, so the return address is saved --
// use this to call out to injected code and return). Fails if the
// displacement doesn't fit in the 24-bit (+/-32MB) branch field.
std::optional<std::vector<uint8_t>> AssembleBranch(uint64_t fromAddr, uint64_t targetAddr, bool link,
                                                     std::string* err = nullptr);

// "li rD, value" (load a 16-bit signed immediate into rD; pseudo-op for
// "addi rD,0,value"). Fails if reg is out of [0,31] or value doesn't fit
// in a signed/unsigned 16-bit field.
std::optional<std::vector<uint8_t>> AssembleLoadImmediate(int reg, int64_t value, std::string* err = nullptr);

// Parses a small assembly snippet -- one of "nop", "blr", "b <target>",
// "bl <target>", "li <reg> <value>" -- into its encoded bytes. `atAddr` is
// the address the resulting instruction will be placed at (used as the
// branch source for b/bl). Returns nullopt and sets *err on a syntax
// error or an out-of-range operand.
std::optional<std::vector<uint8_t>> AssembleLine(const std::vector<std::string>& tokens, uint64_t atAddr,
                                                   std::string* err = nullptr);

} // namespace tl

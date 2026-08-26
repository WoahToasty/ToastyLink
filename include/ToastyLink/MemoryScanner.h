// Byte-pattern (AOB) and value scanning over a live XBDM connection.
// Built entirely on top of XbdmClient::GetMemory()/WalkMemory(), so its
// correctness does not depend on any game- or build-specific assumptions.
#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include "ToastyLink/HexUtils.h"
#include "ToastyLink/XbdmClient.h"

namespace tl {

// Called periodically during a scan with (bytesScanned, bytesTotal) so a
// caller can render progress. May be null.
using ScanProgressFn = std::function<void(uint64_t scanned, uint64_t total)>;

class MemoryScanner {
public:
    explicit MemoryScanner(XbdmClient& client) : m_client(client) {}

    // Scans [start, start+length) for `pattern`, reading memory in
    // chunks. Returns every address where the pattern matched. Regions
    // reported as unmapped by the console are skipped without aborting
    // the scan.
    std::vector<uint64_t> ScanRange(uint64_t start, uint64_t length,
                                     const std::vector<PatternByte>& pattern,
                                     const ScanProgressFn& progress = nullptr,
                                     uint32_t chunkSize = 0x10000);

    // Scans every writable/readable region reported by "walkmem". Useful
    // when the caller doesn't know a specific address range to target.
    std::vector<uint64_t> ScanAllRegions(const std::vector<PatternByte>& pattern,
                                          const ScanProgressFn& progress = nullptr,
                                          uint32_t chunkSize = 0x10000);

private:
    XbdmClient& m_client;
};

} // namespace tl

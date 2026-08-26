#include "ToastyLink/MemoryScanner.h"

#include <algorithm>

namespace tl {

namespace {

bool MatchesAt(const std::vector<MemoryByte>& buf, size_t offset,
                const std::vector<PatternByte>& pattern) {
    for (size_t i = 0; i < pattern.size(); ++i) {
        const MemoryByte& mb = buf[offset + i];
        if (!mb.mapped) return false;
        if (!pattern[i].wildcard && mb.value != pattern[i].value) return false;
    }
    return true;
}

} // namespace

std::vector<uint64_t> MemoryScanner::ScanRange(uint64_t start, uint64_t length,
                                                const std::vector<PatternByte>& pattern,
                                                const ScanProgressFn& progress,
                                                uint32_t chunkSize) {
    std::vector<uint64_t> matches;
    if (pattern.empty() || length == 0) return matches;

    const size_t patLen = pattern.size();
    // Ensure chunks are comfortably larger than the pattern so the overlap
    // logic below always has room to work with.
    if (chunkSize < patLen * 4) chunkSize = static_cast<uint32_t>(patLen * 4);

    uint64_t pos = start;
    const uint64_t end = start + length;
    const uint32_t overlap = static_cast<uint32_t>(patLen - 1);

    while (pos < end) {
        uint64_t remaining = end - pos;
        uint32_t readLen = static_cast<uint32_t>(std::min<uint64_t>(chunkSize, remaining));
        auto bytes = m_client.GetMemory(pos, readLen);
        if (!bytes || bytes->empty()) {
            // Unreadable region (module unmapped, console busy, etc.) --
            // skip forward rather than aborting the whole scan.
            uint64_t advance = std::min<uint64_t>(readLen, remaining);
            pos += advance;
            if (progress) progress(pos - start, length);
            continue;
        }

        const size_t n = bytes->size();
        if (n >= patLen) {
            for (size_t i = 0; i + patLen <= n; ++i) {
                if (MatchesAt(*bytes, i, pattern)) {
                    matches.push_back(pos + i);
                }
            }
        }

        if (progress) progress(std::min<uint64_t>(pos + readLen - start, length), length);

        // Step forward by (readLen - overlap) so a match straddling the
        // chunk boundary is still found in the next iteration.
        uint64_t step = (readLen > overlap) ? (readLen - overlap) : readLen;
        if (step == 0) step = readLen; // guard against pathological chunkSize
        pos += step;
    }

    return matches;
}

std::vector<uint64_t> MemoryScanner::ScanAllRegions(const std::vector<PatternByte>& pattern,
                                                      const ScanProgressFn& progress,
                                                      uint32_t chunkSize) {
    std::vector<uint64_t> matches;
    auto regions = m_client.WalkMemory();
    if (!regions) return matches;

    uint64_t totalSize = 0;
    for (auto& r : *regions) totalSize += r.size;

    uint64_t scannedBefore = 0;
    for (auto& region : *regions) {
        if (region.size == 0) continue;
        auto regionMatches = ScanRange(
            region.base, region.size, pattern,
            [&](uint64_t scanned, uint64_t /*regionTotal*/) {
                if (progress) progress(scannedBefore + scanned, totalSize);
            },
            chunkSize);
        matches.insert(matches.end(), regionMatches.begin(), regionMatches.end());
        scannedBefore += region.size;
    }
    return matches;
}

} // namespace tl

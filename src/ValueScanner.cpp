#include "ToastyLink/ValueScanner.h"

#include <algorithm>

#include "ToastyLink/XbdmClient.h"

namespace tl {

namespace {

constexpr uint64_t kMergeThreshold = 0x1000;   // merge candidates within this gap into one read
constexpr uint64_t kMaxClusterSpan = 0x40000;  // cap a single bulk read's span

} // namespace

size_t ValueScanner::FirstScan(uint64_t start, uint64_t length, ValueType type,
                                const std::optional<TypedValue>& exactValue, bool alignedOnly,
                                const ScanProgressFn& progress) {
    m_candidates.clear();
    m_type = type;
    m_hasScan = true;

    const size_t valSize = ValueTypeSize(type);
    if (valSize == 0 || length < valSize) return 0;

    const uint32_t chunkSize = 0x10000;
    const uint64_t end = start + length;
    uint64_t pos = start;

    while (pos < end) {
        uint64_t remaining = end - pos;
        // Overlap by (valSize - 1) so a value straddling a chunk boundary
        // is never missed.
        uint32_t readLen = static_cast<uint32_t>(std::min<uint64_t>(chunkSize, remaining));
        auto bytes = m_client.GetMemory(pos, readLen);
        if (!bytes || bytes->empty()) {
            pos += readLen;
            if (progress) progress(pos - start, length);
            continue;
        }

        const size_t n = bytes->size();
        const size_t step = alignedOnly ? valSize : 1;
        for (size_t i = 0; i + valSize <= n; i += step) {
            std::vector<uint8_t> raw;
            raw.reserve(valSize);
            bool mapped = true;
            for (size_t j = 0; j < valSize; ++j) {
                const MemoryByte& mb = (*bytes)[i + j];
                if (!mb.mapped) { mapped = false; break; }
                raw.push_back(mb.value);
            }
            if (!mapped) continue;

            auto tv = TypedValueFromBigEndianBytes(type, raw);
            if (!tv) continue;
            if (exactValue && !tv->EqualsBytes(*exactValue)) continue;

            ScanCandidate c;
            c.address = pos + i;
            c.value = *tv;
            m_candidates.push_back(std::move(c));
        }

        if (progress) progress(std::min<uint64_t>(pos + readLen - start, length), length);

        uint64_t overlap = valSize > 0 ? valSize - 1 : 0;
        uint64_t adv = (readLen > overlap) ? (readLen - overlap) : readLen;
        if (adv == 0) adv = readLen;
        pos += adv;
    }

    return m_candidates.size();
}

std::vector<std::optional<TypedValue>> ValueScanner::ReadValuesAt(const std::vector<uint64_t>& addresses,
                                                                    ValueType type) const {
    std::vector<std::optional<TypedValue>> results(addresses.size());
    if (addresses.empty()) return results;

    const size_t valSize = ValueTypeSize(type);

    // Sort indices by address so nearby candidates can share one bulk read.
    std::vector<size_t> order(addresses.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::sort(order.begin(), order.end(),
              [&](size_t a, size_t b) { return addresses[a] < addresses[b]; });

    size_t i = 0;
    while (i < order.size()) {
        size_t j = i;
        uint64_t clusterStart = addresses[order[i]];
        uint64_t clusterEnd = clusterStart + valSize;
        while (j + 1 < order.size()) {
            uint64_t nextAddr = addresses[order[j + 1]];
            uint64_t nextEnd = nextAddr + valSize;
            if (nextAddr <= clusterEnd + kMergeThreshold && (nextEnd - clusterStart) <= kMaxClusterSpan) {
                clusterEnd = nextEnd;
                ++j;
            } else {
                break;
            }
        }

        auto bytes = m_client.GetMemory(clusterStart, static_cast<uint32_t>(clusterEnd - clusterStart));
        if (bytes) {
            for (size_t k = i; k <= j; ++k) {
                uint64_t addr = addresses[order[k]];
                size_t offset = static_cast<size_t>(addr - clusterStart);
                if (offset + valSize > bytes->size()) continue;
                std::vector<uint8_t> raw;
                raw.reserve(valSize);
                bool mapped = true;
                for (size_t b = 0; b < valSize; ++b) {
                    const MemoryByte& mb = (*bytes)[offset + b];
                    if (!mb.mapped) { mapped = false; break; }
                    raw.push_back(mb.value);
                }
                if (mapped) results[order[k]] = TypedValueFromBigEndianBytes(type, raw);
            }
        }

        i = j + 1;
    }

    return results;
}

size_t ValueScanner::NextScan(NextScanMode mode, const std::optional<TypedValue>& exactValue) {
    if (m_candidates.empty()) return 0;

    std::vector<uint64_t> addrs;
    addrs.reserve(m_candidates.size());
    for (auto& c : m_candidates) addrs.push_back(c.address);

    auto fresh = ReadValuesAt(addrs, m_type);

    std::vector<ScanCandidate> kept;
    kept.reserve(m_candidates.size());
    for (size_t i = 0; i < m_candidates.size(); ++i) {
        if (!fresh[i]) continue; // now unmapped/unreadable -- drop
        const TypedValue& oldVal = m_candidates[i].value;
        const TypedValue& newVal = *fresh[i];

        bool keep = false;
        switch (mode) {
            case NextScanMode::Changed: keep = !newVal.EqualsBytes(oldVal); break;
            case NextScanMode::Unchanged: keep = newVal.EqualsBytes(oldVal); break;
            case NextScanMode::Increased: keep = newVal.AsDouble() > oldVal.AsDouble(); break;
            case NextScanMode::Decreased: keep = newVal.AsDouble() < oldVal.AsDouble(); break;
            case NextScanMode::Exact: keep = exactValue && newVal.EqualsBytes(*exactValue); break;
        }

        if (keep) {
            ScanCandidate c;
            c.address = m_candidates[i].address;
            c.value = newVal;
            kept.push_back(std::move(c));
        }
    }

    m_candidates = std::move(kept);
    return m_candidates.size();
}

void ValueScanner::Reset() {
    m_candidates.clear();
    m_hasScan = false;
}

} // namespace tl

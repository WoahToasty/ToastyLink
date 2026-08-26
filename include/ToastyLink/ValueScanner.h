// Progressive ("Cheat Engine style") value scanning: a first scan collects
// every address matching a value/type over a range, then successive scans
// filter that candidate set down by re-reading current values and keeping
// only the ones matching a comparison mode (changed / unchanged / bigger /
// smaller / a new exact value) -- the standard way to find an unknown
// address (health, ammo, currency, ...) by playing the game between scans.
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

#include "ToastyLink/TypedValue.h"

namespace tl {

class XbdmClient;

struct ScanCandidate {
    uint64_t address = 0;
    TypedValue value;
};

enum class NextScanMode { Changed, Unchanged, Increased, Decreased, Exact };

using ScanProgressFn = std::function<void(uint64_t scanned, uint64_t total)>;

class ValueScanner {
public:
    explicit ValueScanner(XbdmClient& client) : m_client(client) {}

    // Starts a new scan: reads [start, start+length) and keeps every
    // type-aligned (or every byte offset, if alignedOnly is false)
    // position as a candidate, optionally filtered to those equal to
    // `exactValue` if provided. Returns the number of candidates found.
    size_t FirstScan(uint64_t start, uint64_t length, ValueType type,
                      const std::optional<TypedValue>& exactValue, bool alignedOnly,
                      const ScanProgressFn& progress = nullptr);

    // Re-reads every current candidate's value and keeps only those
    // matching `mode` (comparing against each candidate's previously
    // stored value, or against `exactValue` for NextScanMode::Exact).
    // Returns the number of candidates remaining.
    size_t NextScan(NextScanMode mode, const std::optional<TypedValue>& exactValue = std::nullopt);

    const std::vector<ScanCandidate>& Candidates() const { return m_candidates; }
    size_t Count() const { return m_candidates.size(); }
    bool HasScan() const { return m_hasScan; }
    ValueType Type() const { return m_type; }
    void Reset();

private:
    XbdmClient& m_client;
    std::vector<ScanCandidate> m_candidates;
    ValueType m_type = ValueType::I32;
    bool m_hasScan = false;

    // Re-reads the current value at each address as efficiently as
    // possible by merging nearby addresses into single bulk reads. The
    // result is aligned with `addresses`; an entry is nullopt if that
    // address (or any byte of the value at it) is unmapped.
    std::vector<std::optional<TypedValue>> ReadValuesAt(const std::vector<uint64_t>& addresses,
                                                          ValueType type) const;
};

} // namespace tl

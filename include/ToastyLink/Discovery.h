// LAN discovery for XBDM-speaking consoles. There's no reliable, well-
// documented UDP broadcast discovery packet for XBDM that this project can
// implement with confidence, so instead of guessing at one, this probes
// TCP port 730 directly across a subnet -- any host that answers with an
// XBDM-style greeting line is almost certainly a debug-enabled Xbox 360.
// This is plain application-level banner probing (like a targeted nmap
// -p730 sweep of your own LAN), not a protocol-specific discovery scheme.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace tl {

struct DiscoveredConsole {
    std::string host;
    std::string greeting;
};

using DiscoveryProgressFn = std::function<void(int scanned, int total)>;

// Probes <subnetPrefix>.1 through <subnetPrefix>.254 (e.g. "192.168.1") on
// `port`, using `concurrency` worker threads and a per-host connect+read
// budget of `timeoutMs`. Returns every host that answered with something
// that looks like an XBDM greeting.
std::vector<DiscoveredConsole> DiscoverConsoles(const std::string& subnetPrefix, uint16_t port = 730,
                                                 int timeoutMs = 300, int concurrency = 64,
                                                 const DiscoveryProgressFn& progress = nullptr);

} // namespace tl

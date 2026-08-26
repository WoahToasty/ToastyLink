#include "ToastyLink/Discovery.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <mutex>
#include <thread>

#include "ToastyLink/Socket.h"

namespace tl {

namespace {

bool LooksLikeXbdmGreeting(const std::string& line) {
    // Real greetings look like "201- connected". Require the "<digits>-"
    // status-line shape so a stray banner from an unrelated service on
    // port 730 doesn't get reported as a console.
    size_t dash = line.find('-');
    if (dash == std::string::npos || dash == 0) return false;
    for (size_t i = 0; i < dash; ++i) {
        if (!std::isdigit(static_cast<unsigned char>(line[i]))) return false;
    }
    return true;
}

} // namespace

std::vector<DiscoveredConsole> DiscoverConsoles(const std::string& subnetPrefix, uint16_t port,
                                                 int timeoutMs, int concurrency,
                                                 const DiscoveryProgressFn& progress) {
    std::vector<DiscoveredConsole> found;
    std::mutex foundMutex;
    std::atomic<int> nextHost{1};
    std::atomic<int> scanned{0};
    constexpr int kTotal = 254;

    auto worker = [&]() {
        for (;;) {
            int host = nextHost.fetch_add(1);
            if (host > kTotal) break;

            std::string ip = subnetPrefix + "." + std::to_string(host);
            TcpSocket sock;
            if (sock.ConnectWithTimeout(ip, port, timeoutMs)) {
                sock.SetReceiveTimeout(timeoutMs);
                std::string line;
                if (sock.ReadLine(line) && LooksLikeXbdmGreeting(line)) {
                    std::lock_guard<std::mutex> lock(foundMutex);
                    found.push_back({ip, line});
                }
                sock.Close();
            }

            int done = scanned.fetch_add(1) + 1;
            if (progress) progress(done, kTotal);
        }
    };

    int workerCount = std::max(1, std::min(concurrency, kTotal));
    std::vector<std::thread> threads;
    threads.reserve(static_cast<size_t>(workerCount));
    for (int i = 0; i < workerCount; ++i) threads.emplace_back(worker);
    for (auto& t : threads) t.join();

    std::sort(found.begin(), found.end(),
              [](const DiscoveredConsole& a, const DiscoveredConsole& b) { return a.host < b.host; });
    return found;
}

} // namespace tl

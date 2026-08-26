#include "XenonLink/Socket.h"

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    using socket_t = SOCKET;
    static constexpr socket_t kInvalidSocket = INVALID_SOCKET;
#else
    #include <sys/socket.h>
    #include <netdb.h>
    #include <unistd.h>
    #include <cerrno>
    #include <cstring>
    using socket_t = int;
    static constexpr socket_t kInvalidSocket = -1;
#endif

namespace xl {

bool InitSockets(std::string* errorOut) {
#ifdef _WIN32
    WSADATA wsaData;
    int rc = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (rc != 0) {
        if (errorOut) *errorOut = "WSAStartup failed with code " + std::to_string(rc);
        return false;
    }
#else
    (void)errorOut;
#endif
    return true;
}

void ShutdownSockets() {
#ifdef _WIN32
    WSACleanup();
#endif
}

static void CloseRaw(socket_t fd) {
#ifdef _WIN32
    closesocket(fd);
#else
    close(fd);
#endif
}

TcpSocket::TcpSocket() : m_fd(static_cast<intptr_t>(kInvalidSocket)), m_open(false) {}

TcpSocket::~TcpSocket() { Close(); }

bool TcpSocket::Connect(const std::string& host, uint16_t port, std::string* errorOut) {
    Close();

    struct addrinfo hints;
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    struct addrinfo* result = nullptr;
    const std::string portStr = std::to_string(port);
    int rc = getaddrinfo(host.c_str(), portStr.c_str(), &hints, &result);
    if (rc != 0 || result == nullptr) {
        if (errorOut) *errorOut = "failed to resolve host '" + host + "'";
        return false;
    }

    socket_t fd = kInvalidSocket;
    struct addrinfo* attempt = nullptr;
    for (attempt = result; attempt != nullptr; attempt = attempt->ai_next) {
        fd = socket(attempt->ai_family, attempt->ai_socktype, attempt->ai_protocol);
        if (fd == kInvalidSocket) continue;
        if (connect(fd, attempt->ai_addr, static_cast<int>(attempt->ai_addrlen)) == 0) {
            break; // connected
        }
        CloseRaw(fd);
        fd = kInvalidSocket;
    }
    freeaddrinfo(result);

    if (fd == kInvalidSocket) {
        if (errorOut) *errorOut = "could not connect to " + host + ":" + portStr +
                                   " (is XBDM enabled and the console reachable?)";
        return false;
    }

    m_fd = static_cast<intptr_t>(fd);
    m_open = true;
    m_recvBuf.clear();
    return true;
}

void TcpSocket::Close() {
    if (m_open) {
        CloseRaw(static_cast<socket_t>(m_fd));
        m_open = false;
    }
    m_fd = static_cast<intptr_t>(kInvalidSocket);
}

bool TcpSocket::SendAll(const void* data, size_t len) {
    if (!m_open) return false;
    const char* p = static_cast<const char*>(data);
    size_t sent = 0;
    while (sent < len) {
        int rc = send(static_cast<socket_t>(m_fd), p + sent, static_cast<int>(len - sent), 0);
        if (rc <= 0) return false;
        sent += static_cast<size_t>(rc);
    }
    return true;
}

bool TcpSocket::SendString(const std::string& s) { return SendAll(s.data(), s.size()); }

int TcpSocket::RecvSome(void* buf, size_t bufLen) {
    if (!m_open) return -1;
    int rc = recv(static_cast<socket_t>(m_fd), static_cast<char*>(buf), static_cast<int>(bufLen), 0);
    return rc;
}

bool TcpSocket::ReadLine(std::string& outLine) {
    for (;;) {
        // Look for a newline already buffered from a previous recv().
        size_t pos = m_recvBuf.find('\n');
        if (pos != std::string::npos) {
            std::string line = m_recvBuf.substr(0, pos);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            m_recvBuf.erase(0, pos + 1);
            outLine = line;
            return true;
        }

        if (!m_open) return false;

        char chunk[4096];
        int n = RecvSome(chunk, sizeof(chunk));
        if (n <= 0) {
            // Connection closed or errored. Surface any trailing partial
            // line as a last "line" so callers see it instead of it being
            // silently dropped.
            m_open = false;
            if (!m_recvBuf.empty()) {
                outLine = m_recvBuf;
                m_recvBuf.clear();
                return true;
            }
            return false;
        }
        m_recvBuf.append(chunk, static_cast<size_t>(n));
    }
}

} // namespace xl

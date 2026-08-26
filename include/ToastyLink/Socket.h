// Minimal cross-platform blocking TCP socket wrapper.
#pragma once

#include <cstdint>
#include <string>

namespace tl {

class TcpSocket {
public:
    TcpSocket();
    ~TcpSocket();

    TcpSocket(const TcpSocket&) = delete;
    TcpSocket& operator=(const TcpSocket&) = delete;

    // Resolves host and connects. Returns true on success; on failure,
    // errorOut (if non-null) receives a human-readable message.
    bool Connect(const std::string& host, uint16_t port, std::string* errorOut = nullptr);

    void Close();
    bool IsOpen() const { return m_open; }

    // Sends the entire buffer, looping on partial writes. Returns false on
    // any socket error.
    bool SendAll(const void* data, size_t len);
    bool SendString(const std::string& s);

    // Reads up to bufLen bytes into buf. Returns the number of bytes read,
    // 0 on orderly shutdown, or -1 on error.
    int RecvSome(void* buf, size_t bufLen);

    // Buffered line reader used by the XBDM text protocol. Reads until a
    // '\n' is seen, strips a trailing '\r\n' or '\n', and returns true.
    // Returns false if the connection closed or errored before a full
    // line was available.
    bool ReadLine(std::string& outLine);

private:
    // Platform socket handle. On Windows this is a SOCKET (unsigned
    // pointer-sized type); on POSIX it is a plain int file descriptor.
    // Stored as intptr_t so this header stays free of platform includes.
    intptr_t m_fd;
    bool m_open;
    std::string m_recvBuf; // leftover bytes after the last full line
};

// Must be called once before any sockets are used, and ShutdownSockets()
// once at program exit. On POSIX these are no-ops.
bool InitSockets(std::string* errorOut = nullptr);
void ShutdownSockets();

} // namespace tl

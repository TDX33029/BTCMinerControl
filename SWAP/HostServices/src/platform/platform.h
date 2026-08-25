#pragma once

#include <cstdint>
#include <climits>
#include <string>

#ifdef _WIN32
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <windows.h>
#else
    #include <arpa/inet.h>
    #include <cerrno>
    #include <netdb.h>
    #include <netinet/in.h>
    #include <netinet/tcp.h>
    #include <sys/socket.h>
    #include <sys/time.h>
    #include <unistd.h>

    using SOCKET = int;
    constexpr SOCKET INVALID_SOCKET = -1;
    constexpr int SOCKET_ERROR = -1;
    constexpr int SD_BOTH = SHUT_RDWR;
    #define closesocket(s) ::close(s)
    #define WSAGetLastError() (errno)
    #define WSAETIMEDOUT ETIMEDOUT
    #define WSAEWOULDBLOCK EWOULDBLOCK
    #define WSAEINTR EINTR
    #define WSAENOTSOCK ENOTSOCK
    #define WSAEACCES EACCES
    #define WSAEADDRINUSE EADDRINUSE
    #define WSAECONNRESET ECONNRESET
#endif

namespace platform {

/// Monotonic milliseconds since an unspecified epoch. Safe for timeouts and
/// interval measurement; not a wall clock.
uint64_t tick_ms();

/// Cryptographically strong random 64-bit value when the OS provides one.
uint64_t secure_random_u64();

void sleep_ms(unsigned int milliseconds);

/// Local wall-clock timestamp in the same format as the dashboard event log:
/// YYYY:MM:DD HH:MM:SS.mmm
std::string local_timestamp_ms();

/// Last socket error as an integer (errno on POSIX, WSA error on Windows).
int last_socket_error();

/// Human-readable text for a socket error value.
std::string socket_error_text(int error);

/// Human-readable text for a getaddrinfo() return value.
std::string gai_error_text(int code);

/// Put a socket in blocking or non-blocking mode.
int set_nonblocking(SOCKET socket, bool enabled);

/// Set SO_RCVTIMEO on a socket. The native representation differs between
/// Windows (DWORD ms) and POSIX (struct timeval).
int set_recv_timeout(SOCKET socket, int timeout_ms);

/// Atomically replace destination with temporary_path.
bool atomic_replace_file(const std::string& temporary_path,
                         const std::string& destination_path);

/// Remove a file if it exists.
void remove_file_if_present(const std::string& path);

} // namespace platform

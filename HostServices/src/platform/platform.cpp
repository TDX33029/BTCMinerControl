#include "platform.h"

#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <ctime>

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace platform {

uint64_t tick_ms() {
#ifdef _WIN32
    return GetTickCount64();
#else
    timespec ts{};
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
    return static_cast<uint64_t>(ts.tv_sec) * 1000ULL +
           static_cast<uint64_t>(ts.tv_nsec) / 1000000ULL;
#endif
}

void sleep_ms(unsigned int milliseconds) {
#ifdef _WIN32
    Sleep(milliseconds);
#else
    usleep(static_cast<useconds_t>(milliseconds) * 1000U);
#endif
}

std::string local_timestamp_ms() {
    const auto now = std::chrono::system_clock::now();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        now.time_since_epoch()) % 1000;
    const std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm tm_value{};
#ifdef _WIN32
    localtime_s(&tm_value, &time);
#else
    localtime_r(&time, &tm_value);
#endif
    char buffer[64]{};
    std::snprintf(buffer, sizeof(buffer), "%04d:%02d:%02d %02d:%02d:%02d.%03d",
                  tm_value.tm_year + 1900, tm_value.tm_mon + 1,
                  tm_value.tm_mday, tm_value.tm_hour, tm_value.tm_min,
                  tm_value.tm_sec, static_cast<int>(ms.count()));
    return buffer;
}

int last_socket_error() {
#ifdef _WIN32
    return WSAGetLastError();
#else
    return errno;
#endif
}

std::string socket_error_text(int error) {
#ifdef _WIN32
    (void)error;
    return "socket error " + std::to_string(error);
#else
    return std::strerror(error);
#endif
}

std::string gai_error_text(int code) {
#ifdef _WIN32
    return gai_strerrorA(code);
#else
    return gai_strerror(code);
#endif
}

int set_nonblocking(SOCKET socket, bool enabled) {
#ifdef _WIN32
    u_long mode = enabled ? 1UL : 0UL;
    return ioctlsocket(socket, FIONBIO, &mode);
#else
    const int flags = fcntl(socket, F_GETFL, 0);
    if (flags < 0) return -1;
    return fcntl(socket, F_SETFL, enabled ? (flags | O_NONBLOCK)
                                          : (flags & ~O_NONBLOCK));
#endif
}

int set_recv_timeout(SOCKET socket, int timeout_ms) {
#ifdef _WIN32
    DWORD timeout = timeout_ms <= 0 ? 1 : static_cast<DWORD>(timeout_ms);
    return setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO,
                      reinterpret_cast<const char*>(&timeout),
                      sizeof(timeout));
#else
    timeval timeout{};
    timeout.tv_sec = timeout_ms / 1000;
    timeout.tv_usec = (timeout_ms % 1000) * 1000;
    return setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, &timeout,
                      sizeof(timeout));
#endif
}

bool atomic_replace_file(const std::string& temporary_path,
                         const std::string& destination_path) {
#ifdef _WIN32
    return MoveFileExA(temporary_path.c_str(), destination_path.c_str(),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    // rename() atomically replaces the destination on all POSIX filesystems
    // used by Ubuntu. fsync the file before rename when the caller has already
    // closed the temporary file (see config.cpp).
    if (::rename(temporary_path.c_str(), destination_path.c_str()) != 0) {
        return false;
    }
    return true;
#endif
}

void remove_file_if_present(const std::string& path) {
#ifdef _WIN32
    DeleteFileA(path.c_str());
#else
    ::remove(path.c_str());
#endif
}

namespace {
struct PlatformLifecycle {
    PlatformLifecycle() {
#ifdef _WIN32
        WSADATA data{};
        WSAStartup(MAKEWORD(2, 2), &data);
#else
        // A send() on a closed peer socket must return EPIPE instead of
        // killing the whole mining proxy.
        ::signal(SIGPIPE, SIG_IGN);
#endif
    }
    ~PlatformLifecycle() {
#ifdef _WIN32
        WSACleanup();
#endif
    }
};
PlatformLifecycle g_platform_lifecycle;
} // namespace

} // namespace platform

#pragma once
#include <string>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>
#include <vector>
#include <winsock2.h>
#include "../dispatch/manager.h"

// ---------------------------------------------------------------------------
// Lightweight HTTP+SSE dashboard server.
//
// Serves:
//   GET /          → Dashboard HTML page
//   GET /api/stats → JSON snapshot (for SSE)
//   GET /events    → Server-Sent Events stream
// ---------------------------------------------------------------------------

class DashboardServer {
public:
    DashboardServer();

    // Start on port. The BoardManager provides live stats.
    bool start(uint16_t port, BoardManager* board_mgr);

    void stop();

    bool isRunning() const { return m_running; }

    // Set pool-level stats displayed on the dashboard.
    void setPoolStats(const std::string& pool_url, bool connected,
                      uint64_t accepted, uint64_t rejected, double hashrate_total);

private:
    void acceptLoop();
    void handleClient(SOCKET client);

    SOCKET m_listen_sock = INVALID_SOCKET;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_stop{false};
    std::thread m_thread;
    BoardManager* m_boards = nullptr;

    mutable std::mutex m_pool_mutex;
    std::string m_pool_url;
    bool m_pool_connected = false;
    uint64_t m_shares_accepted = 0;
    uint64_t m_shares_rejected = 0;
    double m_hashrate_total = 0.0;
};

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
    ~DashboardServer();

    // Start on port. The BoardManager provides live stats.
    bool start(uint16_t port, BoardManager* board_mgr,
               const std::string& bind_address = "127.0.0.1",
               const std::string& config_path = std::string(),
               uint16_t configured_board_port = 0,
               uint32_t detection_interval_ms = 5000);

    void stop();

    bool isRunning() const { return m_running; }

    // Set pool-level stats displayed on the dashboard.
    void setPoolStats(const std::string& pool_url, bool connected,
                      uint64_t accepted, uint64_t rejected, double hashrate_total);
    void setPoolManagementUrl(const std::string& url);

    // Non-empty in local hardware-test modes where no mining pool is used.
    void setTestMode(const std::string& mode);

private:
    void acceptLoop();
    void handleClient(SOCKET client);

    SOCKET m_listen_sock = INVALID_SOCKET;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_stop{false};
    std::thread m_thread;
    BoardManager* m_boards = nullptr;
    std::string m_config_path;
    uint16_t m_dashboard_port = 0;
    uint16_t m_configured_board_port = 0;
    uint16_t m_configured_dashboard_port = 0;
    uint64_t m_started_ms = 0;

    std::mutex m_clients_mutex;
    std::vector<SOCKET> m_client_sockets;

    mutable std::mutex m_pool_mutex;
    std::string m_test_mode;
    std::string m_pool_url;
    std::string m_pool_management_url;
    bool m_pool_connected = false;
    uint64_t m_shares_accepted = 0;
    uint64_t m_shares_rejected = 0;
    double m_hashrate_total = 0.0;
};

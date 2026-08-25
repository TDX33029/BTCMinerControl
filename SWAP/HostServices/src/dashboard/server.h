#pragma once
#include <string>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <vector>
#include "../platform/platform.h"
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
               uint32_t detection_interval_ms = 5000,
               const std::string& dashboard_password = std::string(),
               const std::string& password_sha256 = std::string(),
               const std::string& password_salt = std::string(),
               bool frequency_control_enabled = false);

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

    enum class LoginStatus {
        Ok,
        BadPassword,
        Locked
    };

    LoginStatus tryLogin(const std::string& password,
                         const std::string& client_ip,
                         std::string& out_token);
    bool isAuthorized(const std::string& request,
                      const std::string& client_ip);
    void logoutSession(const std::string& request);

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
    std::string m_dashboard_password;
    std::string m_dashboard_password_sha256;
    std::string m_dashboard_password_salt;
    bool m_frequency_control_enabled = false;

    struct LoginFailure {
        int count = 0;
        uint64_t first_failure_ms = 0;
        uint64_t locked_until_ms = 0;
    };

    mutable std::mutex m_auth_mutex;
    std::unordered_map<std::string, uint64_t> m_sessions; // token -> expiry
    std::unordered_map<std::string, LoginFailure> m_login_failures;
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

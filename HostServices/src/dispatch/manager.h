#pragma once
#include "protocol.h"
#include <vector>
#include <mutex>
#include <thread>
#include <functional>
#include <atomic>
#include <winsock2.h>

// ---------------------------------------------------------------------------
// Board Manager — accepts incoming STM32 board connections, tracks state,
// provides per-board stats for the dashboard.
// ---------------------------------------------------------------------------

struct BoardStats {
    BoardInfo info;
    uint64_t  jobs_sent;
    uint64_t  nonces_returned;
    double    hashrate_1m;    // GH/s, last 1 minute
    double    hashrate_10m;   // GH/s, last 10 minutes
    uint64_t  best_diff;
    uint64_t  last_job_time;
    bool      online;
};

class BoardManager {
public:
    // Callback: a board sent us a nonce result
    std::function<void(const NonceResult&)> onNonceResult;
    // Callback: a board sent us ASIC register data
    std::function<void(uint64_t board_id, const AsicRegister&)> onAsicRegister;

    BoardManager();
    ~BoardManager();

    // Start listening on port. Returns true on success.
    bool start(uint16_t port);

    // Stop the server.
    void stop();

    // Send a job to a specific board. Returns false if board offline.
    bool sendJob(uint64_t board_id, const std::vector<uint8_t>& job_data);

    // Broadcast a job to ALL connected boards.
    void broadcastJob(const std::vector<uint8_t>& job_data);

    // Send freq/voltage setting to a board.
    bool setBoardParams(uint64_t board_id, uint16_t freq_mhz, uint16_t voltage_mv);

    // Get stats for all boards (thread-safe snapshot).
    std::vector<BoardStats> getStats();

    // Get total accepted/rejected share counts.
    void addAcceptedShare(uint64_t board_id);
    void addRejectedShare(uint64_t board_id);

    bool isRunning() const { return m_running; }
    uint16_t port() const { return m_port; }

private:
    void acceptLoop();
    void recvLoop(SOCKET sock, BoardInfo board);

    SOCKET m_listen_sock = INVALID_SOCKET;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_stop{false};
    uint16_t m_port = 0;
    std::thread m_accept_thread;

    mutable std::mutex m_mutex;
    std::vector<BoardStats> m_boards;
};

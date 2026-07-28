#pragma once

#include "protocol.h"
#include <atomic>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>
#include <winsock2.h>

struct BoardStats {
    BoardInfo info{};
    uint64_t jobs_sent = 0;
    uint64_t nonces_returned = 0;
    double hashrate_1m = 0.0;
    double hashrate_10m = 0.0;
    double best_diff = 0.0;
    uint64_t last_job_time = 0;
    uint64_t connected_since = 0;
    uint64_t connection_id = 0;
    bool online = false;
};

class BoardManager {
public:
    std::function<void(const NonceResult&)> onNonceResult;
    std::function<void(uint64_t, const AsicRegister&)> onAsicRegister;
    std::function<void(uint64_t)> onBoardConnected;

    BoardManager() = default;
    ~BoardManager();

    bool start(uint16_t port);
    void stop();

    bool sendJob(uint64_t board_id, const std::vector<uint8_t>& job_data);
    void broadcastJob(const std::vector<uint8_t>& job_data);
    bool setBoardParams(uint64_t board_id, uint16_t freq_mhz,
                        uint16_t voltage_mv);

    std::vector<BoardStats> getStats() const;
    void recordNonce(uint64_t board_id, double difficulty);
    void addAcceptedShare(uint64_t board_id);
    void addRejectedShare(uint64_t board_id);

    bool isRunning() const { return m_running; }
    uint16_t port() const { return m_port; }

private:
    void acceptLoop();
    void recvLoop(SOCKET sock, BoardInfo board, uint64_t connection_id,
                  MessageReader reader);
    bool sendToBoard(uint64_t board_id, const std::vector<uint8_t>& data,
                     bool count_as_job);

    SOCKET m_listen_sock = INVALID_SOCKET;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_stop{false};
    std::atomic<uint64_t> m_next_connection_id{1};
    uint16_t m_port = 0;
    std::thread m_accept_thread;

    mutable std::mutex m_mutex;
    std::vector<BoardStats> m_boards;
    std::unordered_map<uint64_t, std::deque<uint64_t>> m_nonce_times;

    // WinSock permits concurrent send calls, but byte streams can interleave.
    std::mutex m_send_mutex;
    std::mutex m_threads_mutex;
    std::vector<std::thread> m_receiver_threads;
};

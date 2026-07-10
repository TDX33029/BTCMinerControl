#include "manager.h"
#include <iostream>
#include <ws2tcpip.h>
#include "../mine/job.h"

BoardManager::BoardManager() {}
BoardManager::~BoardManager() {
    stop();
}

bool BoardManager::start(uint16_t port) {
    m_listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (m_listen_sock == INVALID_SOCKET) {
        std::cerr << "[boards] socket() failed: " << WSAGetLastError() << std::endl;
        return false;
    }

    int reuse = 1;
    setsockopt(m_listen_sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));

    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(m_listen_sock, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        std::cerr << "[boards] bind() failed: " << WSAGetLastError() << std::endl;
        closesocket(m_listen_sock);
        return false;
    }

    if (listen(m_listen_sock, SOMAXCONN) == SOCKET_ERROR) {
        std::cerr << "[boards] listen() failed: " << WSAGetLastError() << std::endl;
        closesocket(m_listen_sock);
        return false;
    }

    m_port = port;
    m_running = true;
    m_stop = false;
    m_accept_thread = std::thread(&BoardManager::acceptLoop, this);

    std::cout << "[boards] Listening on port " << port << std::endl;
    return true;
}

void BoardManager::stop() {
    m_stop = true;
    m_running = false;
    if (m_listen_sock != INVALID_SOCKET) {
        closesocket(m_listen_sock);
        m_listen_sock = INVALID_SOCKET;
    }
    if (m_accept_thread.joinable()) {
        m_accept_thread.join();
    }
    // Close all board sockets
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& b : m_boards) {
        if (b.info.socket != INVALID_SOCKET) {
            closesocket(b.info.socket);
        }
        b.online = false;
    }
}

void BoardManager::acceptLoop() {
    while (!m_stop) {
        sockaddr_in client_addr = {};
        int addr_len = sizeof(client_addr);

        // Set a timeout on accept so we can check m_stop
        int timeout = 1000;
        setsockopt(m_listen_sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));

        SOCKET client = accept(m_listen_sock, (sockaddr*)&client_addr, &addr_len);
        if (client == INVALID_SOCKET) {
            if (m_stop) break;
            continue; // timeout, loop again
        }

        // Disable timeout for the client socket
        timeout = 0;
        setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));

        char ip_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, ip_str, sizeof(ip_str));

        // Expect a BoardHello as the first message
        auto data = recv_message(client, 5000);
        BoardHello hello;
        if (!decode_board_hello(data.data(), data.size(), hello)) {
            std::cerr << "[boards] Invalid hello from " << ip_str << std::endl;
            closesocket(client);
            continue;
        }

        BoardInfo info = {};
        info.board_id = hello.board_id;
        info.asic_count = hello.asic_count;
        info.firmware_version = hello.fw_version;
        info.status = hello.status;
        info.socket = client;
        info.ip_addr = ip_str;
        UINT64 currentTime = GetTickCount64();
        info.last_heartbeat = currentTime;

        std::cout << "[boards] Board " << std::hex << hello.board_id << std::dec
                  << " connected from " << ip_str
                  << " (" << int(hello.asic_count) << " ASICs)" << std::endl;

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            BoardStats stats;
            stats.info = info;
            stats.online = true;
            m_boards.push_back(stats);
        }

        // Start receive thread for this board
        std::thread(&BoardManager::recvLoop, this, client, info).detach();
    }
}

void BoardManager::recvLoop(SOCKET sock, BoardInfo board) {
    while (!m_stop) {
        auto data = recv_message(sock, 1000);
        if (data.empty()) {
            if (GetTickCount64() - board.last_heartbeat > 30000) {
                // Board timed out
                std::cerr << "[boards] Board " << board.board_id << " timed out" << std::endl;
                break;
            }
            continue;
        }
        board.last_heartbeat = GetTickCount64();
        MsgType type = MsgType(data[0]);
        size_t payload_len = data.size() - 1;
        const uint8_t* payload = (payload_len > 0) ? (data.data() + 1) : nullptr;

        switch (type) {
        case MsgType::NonceResult: {
            NonceResult nr;
            nr.board_id = board.board_id;
            if (decode_nonce_result(payload, payload_len, nr)) {
                // Update stats
                std::lock_guard<std::mutex> lock(m_mutex);
                for (auto& b : m_boards) {
                    if (b.info.board_id == board.board_id) {
                        b.nonces_returned++;
                        break;
                    }
                }
                if (onNonceResult) onNonceResult(nr);
            }
            break;
        }
        case MsgType::AsicRegister: {
            AsicRegister reg;
            if (decode_asic_register(payload, payload_len, reg)) {
                if (onAsicRegister) onAsicRegister(board.board_id, reg);
            }
            break;
        }
        case MsgType::BoardHello: {
            // Heartbeat, update last_heartbeat only (already set above)
            break;
        }
        default:
            break;
        }
    }

    // Board disconnected
    closesocket(sock);
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& b : m_boards) {
        if (b.info.board_id == board.board_id) {
            b.online = false;
            break;
        }
    }
}

bool BoardManager::sendJob(uint64_t board_id, const std::vector<uint8_t>& data) {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& b : m_boards) {
        if (b.info.board_id == board_id && b.online) {
            int sent = ::send(b.info.socket, (const char*)data.data(), (int)data.size(), 0);
            if (sent == (int)data.size()) {
                b.jobs_sent++;
                b.last_job_time = GetTickCount64();
                return true;
            }
            return false;
        }
    }
    return false;
}

void BoardManager::broadcastJob(const std::vector<uint8_t>& data) {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& b : m_boards) {
        if (b.online) {
            int sent = ::send(b.info.socket, (const char*)data.data(), (int)data.size(), 0);
            if (sent == (int)data.size()) {
                b.jobs_sent++;
                b.last_job_time = GetTickCount64();
            }
        }
    }
}

bool BoardManager::setBoardParams(uint64_t board_id, uint16_t freq_mhz, uint16_t voltage_mv) {
    auto data = encode_set_params(freq_mhz, voltage_mv);
    return sendJob(board_id, data);
}

std::vector<BoardStats> BoardManager::getStats() {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_boards;
}

void BoardManager::addAcceptedShare(uint64_t board_id) {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& b : m_boards) {
        if (b.info.board_id == board_id) {
            b.info.shares_accepted++;
            return;
        }
    }
}

void BoardManager::addRejectedShare(uint64_t board_id) {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& b : m_boards) {
        if (b.info.board_id == board_id) {
            b.info.shares_rejected++;
            return;
        }
    }
}

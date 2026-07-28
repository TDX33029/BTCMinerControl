#include "manager.h"
#include "../mine/job.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <ws2tcpip.h>

namespace {

bool send_all(SOCKET socket, const uint8_t* data, size_t size) {
    size_t offset = 0;
    while (offset < size) {
        const int chunk = static_cast<int>(std::min<size_t>(
            size - offset, static_cast<size_t>(INT_MAX)));
        const int sent = ::send(socket,
            reinterpret_cast<const char*>(data + offset), chunk, 0);
        if (sent <= 0) return false;
        offset += static_cast<size_t>(sent);
    }
    return true;
}

} // namespace

BoardManager::~BoardManager() {
    stop();
}

bool BoardManager::start(uint16_t port) {
    if (m_running || port == 0) return false;
    m_stop = false;

    m_listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (m_listen_sock == INVALID_SOCKET) {
        std::cerr << "[boards] socket() failed: " << WSAGetLastError() << std::endl;
        return false;
    }

    int reuse = 1;
    setsockopt(m_listen_sock, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&reuse), sizeof(reuse));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(m_listen_sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr))
        == SOCKET_ERROR) {
        std::cerr << "[boards] bind() failed: " << WSAGetLastError() << std::endl;
        closesocket(m_listen_sock);
        m_listen_sock = INVALID_SOCKET;
        return false;
    }
    if (listen(m_listen_sock, SOMAXCONN) == SOCKET_ERROR) {
        std::cerr << "[boards] listen() failed: " << WSAGetLastError() << std::endl;
        closesocket(m_listen_sock);
        m_listen_sock = INVALID_SOCKET;
        return false;
    }

    m_port = port;
    m_running = true;
    m_accept_thread = std::thread(&BoardManager::acceptLoop, this);
    std::cout << "[boards] Listening on port " << port << std::endl;
    return true;
}

void BoardManager::stop() {
    if (m_stop.exchange(true) && !m_running) return;
    m_running = false;

    if (m_listen_sock != INVALID_SOCKET) {
        closesocket(m_listen_sock);
        m_listen_sock = INVALID_SOCKET;
    }

    // shutdown() wakes the owning receive thread; that thread performs the
    // single closesocket(), avoiding handle-reuse races and double closes.
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto& board : m_boards) {
            if (board.online && board.info.socket != INVALID_SOCKET) {
                shutdown(board.info.socket, SD_BOTH);
            }
        }
    }

    if (m_accept_thread.joinable()) m_accept_thread.join();

    std::vector<std::thread> receivers;
    {
        std::lock_guard<std::mutex> lock(m_threads_mutex);
        receivers.swap(m_receiver_threads);
    }
    for (auto& receiver : receivers) {
        if (receiver.joinable()) receiver.join();
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& board : m_boards) {
        board.online = false;
        board.info.socket = INVALID_SOCKET;
    }
}

void BoardManager::acceptLoop() {
    while (!m_stop) {
        sockaddr_in client_addr{};
        int addr_len = sizeof(client_addr);
        SOCKET client = accept(m_listen_sock,
            reinterpret_cast<sockaddr*>(&client_addr), &addr_len);
        if (client == INVALID_SOCKET) {
            if (!m_stop) {
                const int error = WSAGetLastError();
                if (error != WSAEINTR && error != WSAENOTSOCK) {
                    std::cerr << "[boards] accept() failed: " << error << std::endl;
                }
            }
            break;
        }

        char ip_str[INET_ADDRSTRLEN]{};
        inet_ntop(AF_INET, &client_addr.sin_addr, ip_str, sizeof(ip_str));

        MessageReader reader;
        std::vector<uint8_t> data;
        const ReceiveResult hello_result = reader.receive(client, data, 5000);
        BoardHello hello{};
        if (hello_result != ReceiveResult::Message || data.empty() ||
            data[0] != static_cast<uint8_t>(MsgType::BoardHello) ||
            !decode_board_hello(data.data() + 1, data.size() - 1, hello)) {
            std::cerr << "[boards] Invalid hello from " << ip_str << std::endl;
            closesocket(client);
            continue;
        }

        BoardInfo info{};
        info.board_id = hello.board_id;
        info.asic_count = hello.asic_count;
        info.firmware_version = hello.fw_version;
        info.status = hello.status;
        info.socket = client;
        info.ip_addr = ip_str;
        info.last_heartbeat = GetTickCount64();

        const uint64_t connection_id = m_next_connection_id.fetch_add(1);
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            auto board_it = std::find_if(m_boards.begin(), m_boards.end(),
                [&](const BoardStats& board) {
                    return board.info.board_id == hello.board_id;
                });

            if (board_it != m_boards.end()) {
                if (board_it->online && board_it->info.socket != INVALID_SOCKET) {
                    shutdown(board_it->info.socket, SD_BOTH);
                }
                *board_it = BoardStats{};
                board_it->info = info;
                board_it->online = true;
                board_it->connected_since = info.last_heartbeat;
                board_it->connection_id = connection_id;
            } else {
                BoardStats stats{};
                stats.info = info;
                stats.online = true;
                stats.connected_since = info.last_heartbeat;
                stats.connection_id = connection_id;
                m_boards.push_back(stats);
            }
            m_nonce_times[hello.board_id].clear();
        }

        std::cout << "[boards] Board " << std::hex << hello.board_id << std::dec
                  << " connected from " << ip_str << " ("
                  << int(hello.asic_count) << " ASICs)" << std::endl;

        {
            std::lock_guard<std::mutex> lock(m_threads_mutex);
            m_receiver_threads.emplace_back(&BoardManager::recvLoop, this,
                client, info, connection_id, std::move(reader));
        }

        if (onBoardConnected) onBoardConnected(hello.board_id);
    }
}

void BoardManager::recvLoop(SOCKET sock, BoardInfo board,
                            uint64_t connection_id, MessageReader reader) {
    uint64_t last_heartbeat = board.last_heartbeat;

    while (!m_stop) {
        std::vector<uint8_t> data;
        const ReceiveResult result = reader.receive(sock, data, 1000);
        if (result == ReceiveResult::Timeout) {
            if (GetTickCount64() - last_heartbeat > 30000) {
                std::cerr << "[boards] Board " << board.board_id
                          << " timed out" << std::endl;
                break;
            }
            continue;
        }
        if (result != ReceiveResult::Message) break;

        last_heartbeat = GetTickCount64();
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            for (auto& current : m_boards) {
                if (current.info.board_id == board.board_id &&
                    current.connection_id == connection_id) {
                    current.info.last_heartbeat = last_heartbeat;
                    break;
                }
            }
        }

        if (data.empty()) continue;
        const MsgType type = static_cast<MsgType>(data[0]);
        const size_t payload_len = data.size() - 1;
        const uint8_t* payload = payload_len ? data.data() + 1 : nullptr;

        if (type == MsgType::NonceResult) {
            NonceResult nonce{};
            nonce.board_id = board.board_id;
            if (decode_nonce_result(payload, payload_len, nonce)) {
                if (onNonceResult) onNonceResult(nonce);
            }
        } else if (type == MsgType::AsicRegister) {
            AsicRegister reg{};
            if (decode_asic_register(payload, payload_len, reg) && onAsicRegister) {
                onAsicRegister(board.board_id, reg);
            }
        } else if (type == MsgType::BoardHello) {
            BoardHello heartbeat{};
            if (decode_board_hello(payload, payload_len, heartbeat) &&
                heartbeat.board_id == board.board_id) {
                std::lock_guard<std::mutex> lock(m_mutex);
                for (auto& current : m_boards) {
                    if (current.info.board_id == board.board_id &&
                        current.connection_id == connection_id) {
                        current.info.asic_count = heartbeat.asic_count;
                        current.info.status = heartbeat.status;
                        break;
                    }
                }
            }
        }
    }

    closesocket(sock);
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& current : m_boards) {
        if (current.info.board_id == board.board_id &&
            current.connection_id == connection_id) {
            current.online = false;
            current.info.socket = INVALID_SOCKET;
            break;
        }
    }
}

bool BoardManager::sendToBoard(uint64_t board_id,
                               const std::vector<uint8_t>& data,
                               bool count_as_job) {
    SOCKET socket = INVALID_SOCKET;
    uint64_t connection_id = 0;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (const auto& board : m_boards) {
            if (board.info.board_id == board_id && board.online) {
                socket = board.info.socket;
                connection_id = board.connection_id;
                break;
            }
        }
    }
    if (socket == INVALID_SOCKET) return false;

    bool sent = false;
    {
        std::lock_guard<std::mutex> send_lock(m_send_mutex);
        sent = send_all(socket, data.data(), data.size());
    }
    if (!sent) return false;

    if (count_as_job) {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto& board : m_boards) {
            if (board.info.board_id == board_id && board.online &&
                board.connection_id == connection_id) {
                ++board.jobs_sent;
                board.last_job_time = GetTickCount64();
                break;
            }
        }
    }
    return true;
}

bool BoardManager::sendJob(uint64_t board_id,
                           const std::vector<uint8_t>& data) {
    return sendToBoard(board_id, data, true);
}

void BoardManager::broadcastJob(const std::vector<uint8_t>& data) {
    const auto boards = getStats();
    for (const auto& board : boards) {
        if (board.online) sendToBoard(board.info.board_id, data, true);
    }
}

bool BoardManager::setBoardParams(uint64_t board_id, uint16_t freq_mhz,
                                  uint16_t voltage_mv) {
    return sendToBoard(board_id, encode_set_params(freq_mhz, voltage_mv), false);
}

std::vector<BoardStats> BoardManager::getStats() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_boards;
}

void BoardManager::recordNonce(uint64_t board_id, double difficulty) {
    const uint64_t now = GetTickCount64();
    std::lock_guard<std::mutex> lock(m_mutex);

    auto& times = m_nonce_times[board_id];
    times.push_back(now);
    while (!times.empty() && now - times.front() > 600000) times.pop_front();

    for (auto& board : m_boards) {
        if (board.info.board_id != board_id) continue;
        ++board.nonces_returned;
        if (std::isfinite(difficulty) && difficulty > board.best_diff) {
            board.best_diff = difficulty;
        }

        const size_t count_10m = times.size();
        const size_t count_1m = static_cast<size_t>(std::count_if(
            times.begin(), times.end(), [&](uint64_t timestamp) {
                return now - timestamp <= 60000;
            }));
        const double seconds_online = std::max(1.0,
            double(now - board.connected_since) / 1000.0);
        const double seconds_1m = std::min(60.0, seconds_online);
        const double seconds_10m = std::min(600.0, seconds_online);
        constexpr double hashes_per_result = 256.0 * 4294967296.0;
        board.hashrate_1m = count_1m * hashes_per_result / seconds_1m / 1.0e9;
        board.hashrate_10m = count_10m * hashes_per_result / seconds_10m / 1.0e9;
        break;
    }
}

void BoardManager::addAcceptedShare(uint64_t board_id) {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& board : m_boards) {
        if (board.info.board_id == board_id) {
            ++board.info.shares_accepted;
            break;
        }
    }
}

void BoardManager::addRejectedShare(uint64_t board_id) {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& board : m_boards) {
        if (board.info.board_id == board_id) {
            ++board.info.shares_rejected;
            break;
        }
    }
}

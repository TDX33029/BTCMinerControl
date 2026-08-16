#include "self_test.h"
#include "dispatch/protocol.h"
#include "dispatch/manager.h"
#include "dispatch/scheduler.h"
#include "dashboard/server.h"
#include "mine/coinbase.h"
#include "mine/merkle.h"
#include "mine/midstate.h"
#include "mine/sha256.h"
#include "mine/verify.h"
#include "stratum/client.h"
#include <cmath>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <winsock2.h>
#include <ws2tcpip.h>

namespace {

bool check(bool condition, const char* name) {
    std::cout << "[self-test] " << name << ": "
              << (condition ? "PASS" : "FAIL") << std::endl;
    return condition;
}

bool socket_send_all(SOCKET socket, const std::string& data) {
    size_t offset = 0;
    while (offset < data.size()) {
        const int sent = ::send(socket, data.data() + offset,
                                static_cast<int>(data.size() - offset), 0);
        if (sent <= 0) return false;
        offset += static_cast<size_t>(sent);
    }
    return true;
}

bool socket_receive_line(SOCKET socket, std::string& line) {
    line.clear();
    char byte = 0;
    while (line.size() < 65536) {
        const int received = recv(socket, &byte, 1, 0);
        if (received != 1) return false;
        if (byte == '\n') return true;
        line.push_back(byte);
    }
    return false;
}

SOCKET make_loopback_listener(uint16_t& port) {
    SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == INVALID_SOCKET) return INVALID_SOCKET;
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    if (bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 ||
        listen(listener, 1) != 0) {
        closesocket(listener);
        return INVALID_SOCKET;
    }
    int length = sizeof(address);
    if (getsockname(listener, reinterpret_cast<sockaddr*>(&address), &length) != 0) {
        closesocket(listener);
        return INVALID_SOCKET;
    }
    port = ntohs(address.sin_port);
    return listener;
}

bool test_fragmented_board_frames() {
    uint16_t port = 0;
    SOCKET listener = make_loopback_listener(port);
    if (listener == INVALID_SOCKET) return false;

    SOCKET client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(port);
    if (::connect(client, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
        closesocket(client);
        closesocket(listener);
        return false;
    }
    SOCKET server = accept(listener, nullptr, nullptr);
    closesocket(listener);
    if (server == INVALID_SOCKET) {
        closesocket(client);
        return false;
    }

    const uint8_t combined[] = {
        0x00, 0x00, 0x00, 0x02, 0x04, 0xaa,
        0x00, 0x00, 0x00, 0x02, 0x06, 0xbb,
    };
    ::send(client, reinterpret_cast<const char*>(combined), 2, 0);
    MessageReader reader;
    std::vector<uint8_t> message;
    bool ok = reader.receive(server, message, 10) == ReceiveResult::Timeout;
    ::send(client, reinterpret_cast<const char*>(combined + 2),
           static_cast<int>(sizeof(combined) - 2), 0);
    ok &= reader.receive(server, message, 1000) == ReceiveResult::Message &&
          message == std::vector<uint8_t>({0x04, 0xaa});
    ok &= reader.receive(server, message, 1000) == ReceiveResult::Message &&
          message == std::vector<uint8_t>({0x06, 0xbb});

    closesocket(client);
    closesocket(server);
    return ok;
}

bool test_stratum_response_routing() {
    uint16_t port = 0;
    SOCKET listener = make_loopback_listener(port);
    if (listener == INVALID_SOCKET) return false;

    bool server_ok = true;
    std::thread server_thread([&] {
        SOCKET peer = accept(listener, nullptr, nullptr);
        if (peer == INVALID_SOCKET) { server_ok = false; return; }
        std::string line;
        if (!socket_receive_line(peer, line)) server_ok = false;
        if (server_ok) {
            const auto request = nlohmann::json::parse(line);
            server_ok &= request["method"] == "mining.authorize";
            socket_send_all(peer, nlohmann::json({
                {"id", request["id"]}, {"result", true}, {"error", nullptr}
            }).dump() + "\n");
        }
        if (!socket_receive_line(peer, line)) server_ok = false;
        if (server_ok) {
            const auto request = nlohmann::json::parse(line);
            server_ok &= request["method"] == "mining.submit";
            server_ok &= request["params"][3] == "00000001";
            server_ok &= request["params"][4] == "0000000a";
            socket_send_all(peer, nlohmann::json({
                {"id", request["id"]}, {"result", true}, {"error", nullptr}
            }).dump() + "\n");
        }
        closesocket(peer);
    });

    StratumClient client;
    bool authorize_called = false;
    bool authorize_ok = false;
    bool share_called = false;
    bool share_ok = false;
    uint64_t response_board = 0;
    client.onAuthorizeResult = [&](bool accepted, const std::string&) {
        authorize_called = true;
        authorize_ok = accepted;
    };
    client.onShareResponse = [&](int, uint64_t board_id, bool accepted,
                                 const std::string&) {
        share_called = true;
        share_ok = accepted;
        response_board = board_id;
    };

    bool ok = client.connect("127.0.0.1", port);
    ok &= client.authorize("self-test.worker", "x");
    if (ok) client.readResponse(2000);

    ShareSubmit share{};
    share.username = "self-test.worker";
    share.job_id = "job";
    share.extranonce_2 = "00000000";
    share.ntime = 1;
    share.nonce = 10;
    share.version_bits = 0;
    share.board_id = 0x1234;
    int request_id = 0;
    ok &= client.submitShare(share, request_id);
    if (ok) client.readResponse(2000);
    client.stop();

    server_thread.join();
    closesocket(listener);
    return ok && server_ok && authorize_called && authorize_ok &&
           share_called && share_ok && response_board == 0x1234;
}

SOCKET connect_test_board(uint16_t port, uint64_t board_id) {
    SOCKET socket_handle = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(port);
    if (::connect(socket_handle, reinterpret_cast<sockaddr*>(&address),
                  sizeof(address)) != 0) {
        closesocket(socket_handle);
        return INVALID_SOCKET;
    }

    uint8_t hello[17] = {0, 0, 0, 13, 0x04};
    for (int i = 0; i < 8; ++i) {
        hello[5 + i] = static_cast<uint8_t>(board_id >> (56 - i * 8));
    }
    hello[13] = 0; // no ASIC is intentional in this integration test
    hello[14] = 0;
    hello[15] = 2;
    hello[16] = 0;
    if (::send(socket_handle, reinterpret_cast<const char*>(hello), 3, 0) != 3 ||
        ::send(socket_handle, reinterpret_cast<const char*>(hello + 3), 14, 0) != 14) {
        closesocket(socket_handle);
        return INVALID_SOCKET;
    }
    return socket_handle;
}

bool send_test_telemetry(SOCKET socket_handle, bool power_enabled = true) {
    uint8_t frame[25]{};
    frame[3] = 21; // type byte plus 20-byte payload
    frame[4] = 0x07;
    frame[5] = 0x1F; // devices, readings, and TPS power state are valid
    frame[6] = 0x24;
    frame[7] = 0x4A;
    frame[8] = power_enabled ? 1 : 0;
    frame[9] = 0x04; frame[10] = 0xB0; // 1200 mV
    frame[11] = 0x00; frame[12] = 0x00; frame[13] = 0x88; frame[14] = 0xB8; // 35000 mA
    frame[15] = 0x00; frame[16] = 0x00; frame[17] = 0xA4; frame[18] = 0x10; // 42000 mW
    frame[19] = 0x18; frame[20] = 0xB5; // TMP1075 63.25 C
    frame[21] = 0x16; frame[22] = 0xDA; // TPS 58.50 C
    size_t offset = 0;
    while (offset < sizeof(frame)) {
        const int sent = ::send(socket_handle,
            reinterpret_cast<const char*>(frame + offset),
            static_cast<int>(sizeof(frame) - offset), 0);
        if (sent <= 0) return false;
        offset += static_cast<size_t>(sent);
    }
    return true;
}

bool receive_test_frame(SOCKET socket_handle, std::vector<uint8_t>& frame) {
    int timeout = 2000;
    setsockopt(socket_handle, SOL_SOCKET, SO_RCVTIMEO,
               reinterpret_cast<const char*>(&timeout), sizeof(timeout));
    uint8_t header[4]{};
    size_t offset = 0;
    while (offset < sizeof(header)) {
        int received = recv(socket_handle, reinterpret_cast<char*>(header + offset),
                            static_cast<int>(sizeof(header) - offset), 0);
        if (received <= 0) return false;
        offset += static_cast<size_t>(received);
    }
    const uint32_t length = (uint32_t(header[0]) << 24) |
                            (uint32_t(header[1]) << 16) |
                            (uint32_t(header[2]) << 8) | header[3];
    if (length == 0 || length > 4096) return false;
    frame.assign(length, 0);
    offset = 0;
    while (offset < frame.size()) {
        int received = recv(socket_handle,
            reinterpret_cast<char*>(frame.data() + offset),
            static_cast<int>(frame.size() - offset), 0);
        if (received <= 0) return false;
        offset += static_cast<size_t>(received);
    }
    return true;
}

bool send_test_frame(SOCKET socket_handle, const std::vector<uint8_t>& frame) {
    if (frame.empty() || frame.size() > 4096) return false;
    std::vector<uint8_t> wire(frame.size() + 4);
    const uint32_t length = static_cast<uint32_t>(frame.size());
    wire[0] = static_cast<uint8_t>(length >> 24);
    wire[1] = static_cast<uint8_t>(length >> 16);
    wire[2] = static_cast<uint8_t>(length >> 8);
    wire[3] = static_cast<uint8_t>(length);
    std::copy(frame.begin(), frame.end(), wire.begin() + 4);
    size_t offset = 0;
    while (offset < wire.size()) {
        const int sent = ::send(socket_handle,
            reinterpret_cast<const char*>(wire.data() + offset),
            static_cast<int>(wire.size() - offset), 0);
        if (sent <= 0) return false;
        offset += static_cast<size_t>(sent);
    }
    return true;
}

bool fetch_dashboard_stats(uint16_t port, nlohmann::json& stats) {
    SOCKET client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (client == INVALID_SOCKET) return false;

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(port);
    if (::connect(client, reinterpret_cast<sockaddr*>(&address),
                  sizeof(address)) != 0) {
        closesocket(client);
        return false;
    }

    int timeout = 2000;
    setsockopt(client, SOL_SOCKET, SO_RCVTIMEO,
               reinterpret_cast<const char*>(&timeout), sizeof(timeout));
    const std::string request =
        "GET /api/stats HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n";
    bool ok = socket_send_all(client, request);
    std::string response;
    char buffer[2048];
    while (ok) {
        const int received = recv(client, buffer, sizeof(buffer), 0);
        if (received == 0) break;
        if (received < 0) { ok = false; break; }
        response.append(buffer, static_cast<size_t>(received));
    }
    closesocket(client);

    const size_t body_offset = response.find("\r\n\r\n");
    if (!ok || response.rfind("HTTP/1.1 200", 0) != 0 ||
        body_offset == std::string::npos) return false;
    try {
        stats = nlohmann::json::parse(response.substr(body_offset + 4));
        return true;
    } catch (...) {
        return false;
    }
}

int dashboard_get_status(uint16_t port, const std::string& target,
                         bool& has_auth_challenge) {
    has_auth_challenge = false;
    SOCKET client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (client == INVALID_SOCKET) return 0;

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(port);
    if (::connect(client, reinterpret_cast<sockaddr*>(&address),
                  sizeof(address)) != 0) {
        closesocket(client);
        return 0;
    }

    int timeout = 2000;
    setsockopt(client, SOL_SOCKET, SO_RCVTIMEO,
               reinterpret_cast<const char*>(&timeout), sizeof(timeout));
    const std::string request = "GET " + target +
        " HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n";
    bool ok = socket_send_all(client, request);
    std::string response;
    char buffer[1024];
    while (ok) {
        const int received = recv(client, buffer, sizeof(buffer), 0);
        if (received == 0) break;
        if (received < 0) { ok = false; break; }
        response.append(buffer, static_cast<size_t>(received));
    }
    closesocket(client);
    if (!ok || response.rfind("HTTP/1.1 ", 0) != 0 ||
        response.size() < 12) return 0;
    has_auth_challenge = response.find("\r\nWWW-Authenticate:") !=
                         std::string::npos;
    try {
        return std::stoi(response.substr(9, 3));
    } catch (...) {
        return 0;
    }
}

bool post_dashboard_power(uint16_t port, uint64_t board_id, bool enabled) {
    SOCKET client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (client == INVALID_SOCKET) return false;

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(port);
    if (::connect(client, reinterpret_cast<sockaddr*>(&address),
                  sizeof(address)) != 0) {
        closesocket(client);
        return false;
    }

    std::ostringstream id;
    id << std::hex << std::uppercase << board_id;
    const std::string request =
        "POST /api/board-power?id=" + id.str() +
        "&enabled=" + (enabled ? "1" : "0") +
        " HTTP/1.1\r\nHost: 127.0.0.1\r\n"
        "X-BTCMiner-Control: 1\r\nConnection: close\r\n\r\n";
    bool ok = socket_send_all(client, request);
    std::string response;
    char buffer[512];
    while (ok) {
        const int received = recv(client, buffer, sizeof(buffer), 0);
        if (received == 0) break;
        if (received < 0) { ok = false; break; }
        response.append(buffer, static_cast<size_t>(received));
    }
    closesocket(client);
    return ok && response.rfind("HTTP/1.1 200", 0) == 0;
}

bool post_dashboard_action(uint16_t port, const std::string& target) {
    SOCKET client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (client == INVALID_SOCKET) return false;

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(port);
    if (::connect(client, reinterpret_cast<sockaddr*>(&address),
                  sizeof(address)) != 0) {
        closesocket(client);
        return false;
    }

    const std::string request =
        "POST " + target + " HTTP/1.1\r\nHost: 127.0.0.1\r\n"
        "X-BTCMiner-Control: 1\r\nConnection: close\r\n\r\n";
    bool ok = socket_send_all(client, request);
    std::string response;
    char buffer[512];
    while (ok) {
        const int received = recv(client, buffer, sizeof(buffer), 0);
        if (received == 0) break;
        if (received < 0) { ok = false; break; }
        response.append(buffer, static_cast<size_t>(received));
    }
    closesocket(client);
    return ok && response.rfind("HTTP/1.1 200", 0) == 0;
}

bool test_multiboard_work_construction() {
    uint16_t port = 0;
    SOCKET probe = make_loopback_listener(port);
    if (probe == INVALID_SOCKET) return false;
    closesocket(probe);

    BoardManager boards;
    if (!boards.start(port)) return false;
    WorkScheduler scheduler(boards);

    SOCKET first = connect_test_board(port, 0x1111);
    SOCKET second = connect_test_board(port, 0x2222);
    bool ok = first != INVALID_SOCKET && second != INVALID_SOCKET;

    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::seconds(2);
    while (ok && boards.getStats().size() < 2 &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ok &= boards.getStats().size() == 2;

    WorkDefinition work{};
    work.pool_job_id = "two-board-test";
    work.prev_block_hash = std::string(64, '0');
    work.coinbase_1 = "01000000";
    work.coinbase_2 = "00";
    work.extranonce_1 = "abcd";
    work.version = 0x20000000U;
    work.ntime = 0x65000000U;
    work.nbits = 0x1d00ffffU;
    work.pool_difficulty = 256.0;
    work.extranonce_2_len = 8;
    work.clean_jobs = true;
    if (ok) scheduler.dispatchNewWork(work);

    std::vector<uint8_t> first_frame, second_frame;
    ok &= receive_test_frame(first, first_frame);
    ok &= receive_test_frame(second, second_frame);
    if (ok) {
        // Returned vectors begin with the type byte. With one midstate, the
        // merkle root begins at offset 71 in this vector.
        ok &= first_frame.size() == 115 && second_frame.size() == 115;
        ok &= first_frame[0] == 0x01 && second_frame[0] == 0x01;
        ok &= (first_frame[1] & 7U) == 0 && (second_frame[1] & 7U) == 0;
        ok &= !std::equal(first_frame.begin() + 71, first_frame.begin() + 103,
                          second_frame.begin() + 71);

        const auto first_job = scheduler.getJob(0x1111, first_frame[1]);
        const auto second_job = scheduler.getJob(0x2222, second_frame[1]);
        ok &= first_job && second_job;
        if (first_job && second_job) {
            ok &= first_job->extranonce_2.size() == 16;
            ok &= second_job->extranonce_2.size() == 16;
            ok &= first_job->extranonce_2 != second_job->extranonce_2;
        }
    }

    if (first != INVALID_SOCKET) closesocket(first);
    if (second != INVALID_SOCKET) closesocket(second);
    boards.stop();
    return ok;
}

bool test_chip_test_work_rotation() {
    uint16_t port = 0, dashboard_port = 0;
    SOCKET probe = make_loopback_listener(port);
    if (probe == INVALID_SOCKET) return false;
    closesocket(probe);
    do {
        probe = make_loopback_listener(dashboard_port);
        if (probe == INVALID_SOCKET) return false;
        closesocket(probe);
    } while (dashboard_port == port);

    constexpr uint64_t board_id = 0x1366;
    BoardManager boards;
    if (!boards.start(port)) return false;
    WorkScheduler scheduler(boards);
    DashboardServer dashboard;
    if (!dashboard.start(dashboard_port, &boards)) {
        boards.stop();
        return false;
    }
    dashboard.setTestMode("CHIP TEST");
    dashboard.setPoolStats("CHIP TEST", false, 0, 0, 0.0);
    SOCKET board = connect_test_board(port, board_id);
    bool ok = board != INVALID_SOCKET;

    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::seconds(2);
    while (ok && boards.getStats().empty() &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ok &= !boards.getStats().empty();
    if (ok) ok &= send_test_telemetry(board);
    const auto telemetry_deadline = std::chrono::steady_clock::now() +
                                    std::chrono::seconds(2);
    while (ok && !boards.getStats().empty() &&
           boards.getStats()[0].telemetry_updated == 0 &&
           std::chrono::steady_clock::now() < telemetry_deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ok &= !boards.getStats().empty() &&
          boards.getStats()[0].telemetry_updated != 0;

    std::vector<uint8_t> power_frame;
    if (ok) ok &= post_dashboard_power(dashboard_port, board_id, false);
    if (ok) ok &= receive_test_frame(board, power_frame);
    if (ok) ok &= power_frame.size() == 2 &&
                  power_frame[0] == 0x08 && power_frame[1] == 0x00;
    if (ok) ok &= send_test_telemetry(board, false);
    const auto power_deadline = std::chrono::steady_clock::now() +
                                std::chrono::seconds(2);
    while (ok && !boards.getStats().empty() &&
           boards.getStats()[0].telemetry.powerEnabled() &&
           std::chrono::steady_clock::now() < power_deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ok &= !boards.getStats().empty() &&
          boards.getStats()[0].telemetry.powerStateValid() &&
          !boards.getStats()[0].telemetry.powerEnabled();

    std::ostringstream latency_id;
    latency_id << std::hex << std::uppercase << board_id;
    std::vector<uint8_t> latency_frame;
    if (ok) ok &= post_dashboard_action(
        dashboard_port, "/api/board-latency?id=" + latency_id.str());
    if (ok) ok &= receive_test_frame(board, latency_frame);
    if (ok) ok &= latency_frame.size() == 9 &&
                  latency_frame[0] == static_cast<uint8_t>(MsgType::LatencyProbe);
    if (ok) ok &= send_test_frame(board, latency_frame);
    const auto latency_deadline = std::chrono::steady_clock::now() +
                                  std::chrono::seconds(2);
    while (ok && !boards.getStats().empty() &&
           !boards.getStats()[0].latency_valid &&
           std::chrono::steady_clock::now() < latency_deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    ok &= !boards.getStats().empty() && boards.getStats()[0].latency_valid;

    uint64_t first_latency_update =
        ok && !boards.getStats().empty() ? boards.getStats()[0].latency_updated : 0;
    latency_frame.clear();
    if (ok) ok &= post_dashboard_action(dashboard_port, "/api/board-latency-all");
    if (ok) ok &= receive_test_frame(board, latency_frame);
    if (ok) ok &= latency_frame.size() == 9 &&
                  latency_frame[0] == static_cast<uint8_t>(MsgType::LatencyProbe);
    if (ok) ok &= send_test_frame(board, latency_frame);
    const auto all_latency_deadline = std::chrono::steady_clock::now() +
                                      std::chrono::seconds(2);
    while (ok && !boards.getStats().empty() &&
           boards.getStats()[0].latency_updated <= first_latency_update &&
           std::chrono::steady_clock::now() < all_latency_deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    ok &= !boards.getStats().empty() && boards.getStats()[0].latency_valid &&
          boards.getStats()[0].latency_updated > first_latency_update;

    std::vector<uint8_t> first_frame, second_frame;
    if (ok) ok &= scheduler.dispatchChipTestWork(board_id);
    if (ok) ok &= receive_test_frame(board, first_frame);
    if (ok) ok &= scheduler.dispatchChipTestWork(board_id);
    if (ok) ok &= receive_test_frame(board, second_frame);

    if (ok) {
        ok &= first_frame.size() == 115 && second_frame.size() == 115;
        ok &= first_frame[0] == 0x01 && second_frame[0] == 0x01;
        ok &= first_frame[1] != second_frame[1];

        const auto first_job = scheduler.getJob(board_id, first_frame[1]);
        const auto second_job = scheduler.getJob(board_id, second_frame[1]);
        ok &= first_job && second_job;
        if (first_job && second_job) {
            ok &= first_job->pool_job_id.rfind("chip-test-", 0) == 0;
            ok &= second_job->pool_job_id.rfind("chip-test-", 0) == 0;
            ok &= first_job->pool_job_id != second_job->pool_job_id;
            ok &= first_job->pool_difficulty == 256.0;
            ok &= second_job->pool_difficulty == 256.0;
            ok &= first_job->ntime != second_job->ntime;
            ok &= std::memcmp(first_job->merkle_root, second_job->merkle_root,
                              sizeof(first_job->merkle_root)) != 0;
        }
    }

    boards.recordNonce(board_id, 300.0);
    bool auth_challenge = false;
    ok &= dashboard_get_status(dashboard_port, "/", auth_challenge) == 200;
    ok &= !auth_challenge;
    ok &= dashboard_get_status(dashboard_port, "/account", auth_challenge) == 404;
    ok &= !auth_challenge;
    nlohmann::json stats;
    ok &= fetch_dashboard_stats(dashboard_port, stats);
    if (ok) {
        ok &= stats.value("test_mode", std::string()) == "CHIP TEST";
        ok &= stats.value("board_port", 0) == port;
        ok &= stats.value("dashboard_port", 0) == dashboard_port;
        ok &= stats.value("board_detection_interval_ms", 0) == 5000;
        ok &= stats.value("uptime_ms", -1LL) >= 0;
        ok &= stats.contains("boards") && stats["boards"].size() == 1;
        if (stats.contains("boards") && stats["boards"].size() == 1) {
            const auto& board_stats = stats["boards"][0];
            ok &= board_stats.value("asic_count", 1) == 0;
            ok &= board_stats.value("jobs_sent", 0) == 2;
            ok &= board_stats.value("nonces_returned", 0) == 1;
            ok &= board_stats.value("best_diff", 0.0) == 300.0;
            ok &= board_stats.value("tps_detected", false);
            ok &= board_stats.value("tmp1075_detected", false);
            ok &= board_stats.value("tps_telemetry_valid", false);
            ok &= board_stats.value("tmp1075_telemetry_valid", false);
            ok &= board_stats.value("power_state_valid", false);
            ok &= !board_stats.value("power_enabled", true);
            ok &= board_stats.value("board_id_hex", std::string()) == "1366";
            ok &= std::abs(board_stats.value("power_w", 0.0) - 42.0) < 0.001;
            ok &= std::abs(board_stats.value("temperature_c", 0.0) - 63.25) < 0.001;
            ok &= board_stats.value("latency_valid", false);
            ok &= !board_stats.value("latency_pending", true);
            ok &= !board_stats.value("latency_timeout", true);
            ok &= board_stats.value("latency_ms", -1.0) >= 0.0;
        }
        ok &= stats.contains("events") && !stats["events"].empty();
        if (stats.contains("events") && !stats["events"].empty()) {
            const std::string timestamp =
                stats["events"][0].value("timestamp", std::string());
            ok &= timestamp.size() == 23 && timestamp[4] == ':' &&
                  timestamp[7] == ':' && timestamp[10] == ' ' &&
                  timestamp[13] == ':' && timestamp[16] == ':' &&
                  timestamp[19] == '.';
        }
    }

    const uint64_t periodic_latency_before =
        ok && !boards.getStats().empty()
            ? boards.getStats()[0].latency_updated : 0;
    if (ok) ok &= boards.setDetectionIntervalMs(1000);
    latency_frame.clear();
    if (ok) ok &= receive_test_frame(board, latency_frame);
    if (ok) ok &= latency_frame.size() == 9 &&
                  latency_frame[0] == static_cast<uint8_t>(MsgType::LatencyProbe);
    if (ok) ok &= send_test_frame(board, latency_frame);
    const auto periodic_latency_deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (ok && !boards.getStats().empty() &&
           boards.getStats()[0].latency_updated <= periodic_latency_before &&
           std::chrono::steady_clock::now() < periodic_latency_deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    ok &= !boards.getStats().empty() && boards.getStats()[0].latency_valid &&
          boards.getStats()[0].latency_updated > periodic_latency_before;

    if (board != INVALID_SOCKET) closesocket(board);
    dashboard.stop();
    boards.stop();
    return ok;
}

bool test_stratum_reference_job_construction() {
    // Reference mining.notify from ESP-Miner's stratum verifier
    // (components/stratum/test/verifiers/bm1397.py). This pins the two
    // byte-order conversions that pool-side header reconstruction depends on:
    //   1. merkle root = raw SHA256d(coinbase || branches)
    //   2. prev_block_hash words are byte-swapped before ASIC word-reversal
    const char* coinbase_1 =
        "01000000010000000000000000000000000000000000000000000000000000000000000000"
        "ffffffff4b03e60e0cfabe6d6d7595fc426909f3a63c563a88773a618ec42cc51188ed06"
        "32b69f1c3053a8f8180100000000000000";
    const char* coinbase_2 =
        "2c28569a1f2f736c7573682f0000000003a1f3a22b000000001976a9147c154ed1dc5960"
        "9e3d26abb2df2ea3d587cd8c4188ac00000000000000002c6a4c2952534b424c4f434b3a"
        "799d4c611eff5765ba06d2c58ad71b5734d677cea10942664b2a712d005108ae00000000"
        "00000000266a24aa21a9ed5c4d2056e3eef09b05d95897adec38c5c3f460a919e95f87e1"
        "5664957c70305a00000000";
    const char* extranonce_1 = "1165060344b679";
    const char* extranonce_2 = "0000000000000000";
    const char* branch_hex[] = {
        "4ea53a030256c37391b891b0d5060537df63944ce3fcd45121215596376bb3db",
        "22cd1dde2c1b083237bbadd62ed1d51ee455265b7defe04dc8bcae7e5acacb33",
        "60c781a8b02c07544cb3a91de3b4d7a13f9939c8579f3ac92fa28e802ace1b39",
        "d89820b36568adc0705d71d639e69ccb7c168a1051697846cf5d98e5725ee4e3",
        "73f0f773a3b6097388984f934ba1b01afc771c33db6df126cd6971cfea9f8f49",
        "420958bbb39f6b8ad30e5b45b38a3825bf76f619b7dbb73a0366605ff882e91d",
        "75f9ef87931104db956c88d65198596049af51017af4685c4548f2c31ec75b6d",
        "70dd7189d5b927ac10a750062e5ab9f8b83fb784068e1c80d0df919bcf22e1b2",
        "b34f2440b2b4609e44594885a397086339f4a2d880fb2d50ac585f757b895832",
        "4c62d861fb259a743d1e2787eeac5bdd22a9883b5cc0b025843cff9441ea6b74",
        "62522d5d8e2ff9d721a9a4b91931ec61069fff7c8ad23119718c068a035b9b1b",
        "a0e7cf5509d9d0d87ff9a4f6332f76a243de01f4e93289b290e937e7fd03224f",
    };

    uint8_t branches[12][32]{};
    bool ok = true;
    for (int i = 0; i < 12; ++i) {
        ok &= hex2bin(branch_hex[i], branches[i], sizeof(branches[i]));
    }

    const auto coinbase_hash = calculate_coinbase_tx_hash(
        coinbase_1, coinbase_2, extranonce_1, extranonce_2);
    const auto merkle_root = calculate_merkle_root(
        coinbase_hash.data(), &branches[0][0], 12);

    ok &= check(bin2hex(merkle_root) ==
                    "cd1be82132ef0d12053dcece1fa0247fcfdb61d4dbd3eb32ea9ef9b4c604a846",
                "reference Stratum merkle root");

    JobParams params{};
    params.version = 0x20000004U;
    params.version_mask = 0;
    params.prev_block_hash =
        "bf44fd3513dc7b837d60e5c628b572b448d204a8000007490000000000000000";
    std::memcpy(params.merkle_root, merkle_root.data(), merkle_root.size());
    params.ntime = 0x64658bd8U;
    params.nbits = 0x1705dd01U;
    params.pool_difficulty = 1000.0;
    params.pool_job_id = "reference";
    params.extranonce_2 = extranonce_2;

    const MinerJob job = build_job(params, 0x00);

    ok &= check(bin2hex(job.midstates[0]) ==
                    "4d3185f25f7d5de0e3591741cb21cae11574ddd65d490d3d68739f8a52eadf91",
                "reference ASIC midstate / prevhash byte order");

    uint8_t built_header[80]{};
    uint32_t version_le = params.version;
    std::memcpy(built_header, &version_le, 4);
    const auto prev_for_header = sha256::reverse_32bit_words(job.prev_block_hash);
    const auto merkle_for_header = sha256::reverse_32bit_words(job.merkle_root);
    std::memcpy(built_header + 4, prev_for_header.data(), 32);
    std::memcpy(built_header + 36, merkle_for_header.data(), 32);
    uint32_t ntime_le = job.ntime;
    uint32_t nbits_le = job.nbits;
    uint32_t nonce_le = 0;
    std::memcpy(built_header + 68, &ntime_le, 4);
    std::memcpy(built_header + 72, &nbits_le, 4);
    std::memcpy(built_header + 76, &nonce_le, 4);

    ok &= check(bin2hex(built_header, sizeof(built_header)) ==
                    "0400002035fd44bf837bdc13c6e5607db472b528a804d248490700000000000000000000"
                    "cd1be82132ef0d12053dcece1fa0247fcfdb61d4dbd3eb32ea9ef9b4c604a846"
                    "d88b656401dd051700000000",
                "reference 80-byte block header reconstruction");

    return ok;
}

} // namespace

bool run_self_tests() {
    bool ok = true;

    // Bitcoin genesis block header and raw double-SHA digest.
    const std::string header_hex =
        "01000000"
        "0000000000000000000000000000000000000000000000000000000000000000"
        "3ba3edfd7a7b12b27ac72c3e67768f617fc81bc3888a51323a9fb8aa4b1e5e4a"
        "29ab5f49ffff001d1dac2b7c";
    uint8_t header[80]{};
    ok &= check(hex2bin(header_hex, header, sizeof(header)), "genesis header decode");
    const auto genesis_hash = sha256::double_sha256(header, sizeof(header));
    const std::string actual_hash = bin2hex(genesis_hash);
    const bool hash_ok = actual_hash ==
        "6fe28c0ab6f1b372c1a6a246ae63f74f931e8365e15a089c68d6190000000000";
    ok &= check(hash_ok, "genesis double SHA-256");
    if (!hash_ok) std::cout << "[self-test] actual hash: " << actual_hash << std::endl;
    ok &= check(std::abs(hash_to_pdiff(genesis_hash.data()) -
                         2536.42629844531) < 1.0e-9,
                "genesis pool difficulty");
    ok &= check(std::abs(network_difficulty(0x1d00ffffU) - 1.0) < 1.0e-12,
                "compact nBits difficulty");

    ok &= check(test_stratum_reference_job_construction(),
                "reference Stratum job construction");

    JobParams genesis_params{};
    genesis_params.version = 1;
    genesis_params.prev_block_hash = std::string(64, '0');
    hex2bin("3ba3edfd7a7b12b27ac72c3e67768f617fc81bc3888a51323a9fb8aa4b1e5e4a",
            genesis_params.merkle_root, 32);
    genesis_params.ntime = 0x495fab29U;
    genesis_params.nbits = 0x1d00ffffU;
    genesis_params.pool_difficulty = 1.0;
    genesis_params.pool_job_id = "genesis";
    genesis_params.extranonce_2 = "00000000";
    const MinerJob genesis_job = build_job(genesis_params, 0x08);
    NonceResult genesis_nonce{};
    genesis_nonce.job_id = 0x08;
    genesis_nonce.nonce = 0x7c2bac1dU;   /* LE value of header nonce bytes 1d ac 2b 7c */
    genesis_nonce.rolled_version = 1;
    const VerifiedNonce verified_genesis = verify_nonce(genesis_job, genesis_nonce);
    ok &= check(verified_genesis.result == VerifyResult::Submit &&
                std::abs(verified_genesis.difficulty - 2536.42629844531) < 1.0e-9,
                "full MinerJob nonce verification endianness");

    uint32_t rolled = 0;
    rolled = sha256::increment_bitmask(rolled, 0x0aU);
    ok &= check(rolled == 0x02U, "version mask step 1");
    rolled = sha256::increment_bitmask(rolled, 0x0aU);
    ok &= check(rolled == 0x08U, "version mask carry across gap");
    rolled = sha256::increment_bitmask(rolled, 0x0aU);
    ok &= check(rolled == 0x0aU, "version mask step 3");
    rolled = sha256::increment_bitmask(rolled | 0x100U, 0x0aU);
    ok &= check(rolled == 0x100U, "version mask wrap preserves outside bits");

    ok &= check(extranonce_2_generate(1, 12) ==
                    "000000000000000000000001",
                "wide extranonce2 generation");

    JobParams params{};
    params.version = 0x20000000U;
    params.prev_block_hash = std::string(64, '0');
    std::memset(params.merkle_root, 0x5a, sizeof(params.merkle_root));
    params.ntime = 0x65000000U;
    params.nbits = 0x1d00ffffU;
    params.pool_difficulty = 256.0;
    params.pool_job_id = "self-test";
    params.extranonce_2 = "00000000";
    const MinerJob job = build_job(params, 0x08);
    const auto frame = encode_job(job);
    ok &= check(frame.size() == 119 && frame[4] == 0x01 && frame[5] == 0x08,
                "PC-to-board job frame");
    const uint8_t telemetry_payload[20] = {
        0x1F, 0x24, 0x4A, 0x01, 0x04, 0xB0,
        0x00, 0x00, 0x88, 0xB8, 0x00, 0x00, 0xA4, 0x10,
        0xFF, 0x9C, 0x16, 0xDA, 0x12, 0x34
    };
    BoardTelemetry telemetry{};
    ok &= check(decode_board_telemetry(telemetry_payload,
                                       sizeof(telemetry_payload), telemetry) &&
                telemetry.tpsDetected() && telemetry.tmp1075Detected() &&
                telemetry.powerStateValid() && telemetry.powerEnabled() &&
                telemetry.vout_mv == 1200 && telemetry.iout_ma == 35000 &&
                telemetry.power_mw == 42000 &&
                telemetry.tmp1075_temperature_centi_c == -100 &&
                telemetry.tps_temperature_centi_c == 5850 &&
                telemetry.tps_status_word == 0x1234,
                "board telemetry decode");
    const auto power_off_frame = encode_set_power(false);
    ok &= check(power_off_frame.size() == 6 &&
                power_off_frame[3] == 2 && power_off_frame[4] == 0x08 &&
                power_off_frame[5] == 0,
                "per-board TPS power command");
    const uint64_t latency_token = 0x0123456789ABCDEFULL;
    const auto latency_probe = encode_latency_probe(latency_token);
    uint64_t decoded_latency_token = 0;
    ok &= check(latency_probe.size() == 13 && latency_probe[3] == 9 &&
                latency_probe[4] == 0x09 &&
                decode_latency_probe(latency_probe.data() + 5, 8,
                                     decoded_latency_token) &&
                decoded_latency_token == latency_token,
                "board latency probe frame");
    ok &= check(test_fragmented_board_frames(),
                "fragmented/coalesced board TCP frames");
    ok &= check(test_stratum_response_routing(),
                "Stratum boolean response routing and hex padding");
    ok &= check(test_multiboard_work_construction(),
                "two-board extranonce2 and merkle isolation");
    ok &= check(test_chip_test_work_rotation(),
                "password-free dashboard and rotating chip-test jobs");

    std::cout << "[self-test] Overall: " << (ok ? "PASS" : "FAIL") << std::endl;
    return ok;
}

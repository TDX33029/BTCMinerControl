#include "self_test.h"
#include "dispatch/protocol.h"
#include "dispatch/manager.h"
#include "dispatch/scheduler.h"
#include "dashboard/server.h"
#include "mine/midstate.h"
#include "mine/sha256.h"
#include "mine/verify.h"
#include "stratum/client.h"
#include <cmath>
#include <cstring>
#include <iostream>
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
    nlohmann::json stats;
    ok &= fetch_dashboard_stats(dashboard_port, stats);
    if (ok) {
        ok &= stats.value("test_mode", std::string()) == "CHIP TEST";
        ok &= stats.contains("boards") && stats["boards"].size() == 1;
        if (stats.contains("boards") && stats["boards"].size() == 1) {
            const auto& board_stats = stats["boards"][0];
            ok &= board_stats.value("asic_count", 1) == 0;
            ok &= board_stats.value("jobs_sent", 0) == 2;
            ok &= board_stats.value("nonces_returned", 0) == 1;
            ok &= board_stats.value("best_diff", 0.0) == 300.0;
        }
    }

    if (board != INVALID_SOCKET) closesocket(board);
    dashboard.stop();
    boards.stop();
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
    genesis_nonce.nonce = 0x7c2bac1dU;
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
    ok &= check(test_fragmented_board_frames(),
                "fragmented/coalesced board TCP frames");
    ok &= check(test_stratum_response_routing(),
                "Stratum boolean response routing and hex padding");
    ok &= check(test_multiboard_work_construction(),
                "two-board extranonce2 and merkle isolation");
    ok &= check(test_chip_test_work_rotation(),
                "chip-test difficulty and rotating synthetic jobs");

    std::cout << "[self-test] Overall: " << (ok ? "PASS" : "FAIL") << std::endl;
    return ok;
}

#include "config.h"
#include "dashboard/server.h"
#include "dispatch/manager.h"
#include "dispatch/scheduler.h"
#include "mine/sha256.h"
#include "self_test.h"
#include "stratum/client.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>

/* ===== Self-test (chip-test) latency / job rotation timeout =====
   In chip-test mode, if a board finds no nonce within this time (ms), the
   test job rotates to the next. Adjust here. */
#define CHIP_TEST_JOB_TIMEOUT_MS  15000

/* ===== Board detection / latency probe interval (shown as "Detect: Ns" in
   the web UI). Periodic probe of all online boards. Adjust here. */
#define BOARD_DETECTION_INTERVAL_MS  10000

namespace {

std::atomic<bool> g_shutdown_requested{false};
std::atomic<uint64_t> g_shares_accepted{0};
std::atomic<uint64_t> g_shares_rejected{0};

void handle_signal(int) {
    g_shutdown_requested = true;
}

void update_dashboard(DashboardServer& dashboard, BoardManager& boards,
                      const std::string& pool, bool connected) {
    double total_hashrate = 0.0;
    for (const auto& board : boards.getStats()) {
        if (board.online) total_hashrate += board.hashrate_1m;
    }
    dashboard.setPoolStats(pool, connected, g_shares_accepted.load(),
                           g_shares_rejected.load(), total_hashrate);
}

void wait_offline(DashboardServer& dashboard, BoardManager& boards,
                  const std::string& label) {
    while (!g_shutdown_requested) {
        update_dashboard(dashboard, boards, label, false);
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }
}

} // namespace

int main(int argc, char* argv[]) {
    bool uart_test_mode = false;
    bool chip_test_mode = false;
    bool self_test_mode = false;
    std::string config_path = "config.json";

    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--uart-test") uart_test_mode = true;
        else if (argument == "--chip-test") chip_test_mode = true;
        else if (argument == "--self-test") self_test_mode = true;
        else if (argument == "--help" || argument == "-h") {
            std::cout << "Usage: BTCMinerControl.exe [config.json] [--uart-test|--chip-test] [--self-test]\n";
            return 0;
        } else if (!argument.empty() && argument[0] == '-') {
            std::cerr << "Unknown option: " << argument << std::endl;
            return 2;
        } else {
            config_path = argument;
        }
    }

    if (self_test_mode) return run_self_tests() ? 0 : 1;
    if (uart_test_mode && chip_test_mode) {
        std::cerr << "--uart-test and --chip-test are mutually exclusive" << std::endl;
        return 2;
    }

    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    std::cout << "============================================\n"
                 "  BTCMinerControl v0.2.0\n"
                 "  Distributed BM1366 Mining Proxy\n"
                 "==================......==========================" << std::endl;

    const AppConfig config = load_config(config_path);
    const std::string pool_label = config.pool_host + ':' +
                                   std::to_string(config.pool_port);

    BoardManager board_manager;
    if (!board_manager.setDetectionIntervalMs(
            BOARD_DETECTION_INTERVAL_MS)) {
        std::cerr << "[main] Invalid board detection interval" << std::endl;
        return 1;
    }
    const bool board_server_started = board_manager.start(config.board_port);
    if (!board_server_started) {
        std::cerr << "[main] Board server is unavailable on port "
                  << config.board_port << "; continuing with the Web UI so "
                     "the listener settings can be corrected."
                  << std::endl;
    }

    WorkScheduler scheduler(board_manager);
    DashboardServer dashboard;
    if (!dashboard.start(config.dashboard_port, &board_manager,
                         config.dashboard_bind, config_path,
                         config.board_port,
                         BOARD_DETECTION_INTERVAL_MS)) {
        std::cerr << "[main] Failed to start dashboard" << std::endl;
        board_manager.stop();
        return 1;
    }
    dashboard.setPoolManagementUrl(config.pool_management_url);

    StratumClient stratum;

    board_manager.onBoardConnected = [&](uint64_t board_id) {
        if (uart_test_mode) {
            scheduler.dispatchUartTestWork(board_id);
            return;
        }
        board_manager.setBoardParams(
            board_id,
            static_cast<uint16_t>(config.default_frequency_mhz),
            static_cast<uint16_t>(config.default_voltage_mv));
        if (chip_test_mode) {
            uint8_t detected = 0;
            for (const auto& board : board_manager.getStats()) {
                if (board.info.board_id == board_id) {
                    detected = board.info.asic_count;
                    break;
                }
            }
            std::cout << "[chip-test] Board 0x" << std::hex << board_id
                      << std::dec << " reports " << unsigned(detected)
                      << " ASIC(s)" << std::endl;
            scheduler.dispatchChipTestWork(board_id);
            return;
        }
        scheduler.dispatchLatestToBoard(board_id);
    };

    board_manager.onNonceResult = [&](const NonceResult& nonce) {
        auto job = scheduler.getJob(nonce.board_id, nonce.job_id);
        if (!job) {
            /* TEMP: the board re-dispatches jobs every 5s with its own advancing
               job_id, which the host never issued. Verify against the latest
               issued job (identical header content). */
            job = scheduler.getLatestJob(nonce.board_id);
            if (!job) {
                std::cout << "[main] Nonce for unknown board/job: 0x" << std::hex
                          << nonce.board_id << '/' << unsigned(nonce.job_id)
                          << std::dec << std::endl;
                return;
            }
            std::cout << "[main] re-dispatched job 0x" << std::hex
                      << unsigned(nonce.job_id) << std::dec
                      << " verified against latest job" << std::endl;
        }

        const VerifiedNonce verified = scheduler.processNonce(*job, nonce);
        std::cout << "[main] nonce board=0x" << std::hex << nonce.board_id << std::dec
                  << " job=" << unsigned(nonce.job_id)
                  << " nonce=0x" << std::hex << nonce.nonce << std::dec
                  << " diff=" << verified.difficulty
                  << " pool_diff=" << job->pool_difficulty
                  << " result=" << int(verified.result) << std::endl;
        board_manager.recordNonce(nonce.board_id, verified.difficulty);
        if (chip_test_mode) {
            const bool passed = verified.result == VerifyResult::Submit &&
                                verified.difficulty >= 256.0;
            std::cout << "[chip-test] " << (passed ? "PASS" : "FAIL")
                      << " board=0x" << std::hex << nonce.board_id << std::dec
                      << " asic=" << unsigned(nonce.asic_nr)
                      << " job=" << unsigned(nonce.job_id)
                      << " nonce=0x" << std::hex << nonce.nonce << std::dec
                      << " difficulty=" << verified.difficulty << std::endl;
            scheduler.dispatchChipTestWork(nonce.board_id);
            return;
        }
        if (verified.result != VerifyResult::Submit || !stratum.isConnected()) {
            return;
        }

        ShareSubmit share{};
        share.username = config.pool_user;
        share.job_id = verified.submit.pool_job_id;
        share.extranonce_2 = verified.submit.extranonce_2;
        share.ntime = verified.submit.ntime;
        share.nonce = verified.submit.nonce;
        share.version_bits = verified.submit.version_bits;
        share.board_id = nonce.board_id;

        int message_id = 0;
        if (!stratum.submitShare(share, message_id)) {
            std::cerr << "[main] Failed to send share request" << std::endl;
        }
    };

    if (uart_test_mode) {
        dashboard.setTestMode("USART1 TEST");
        std::cout << "[main] USART1 test mode: pool connection is disabled\n"
                     "[main] A deterministic BM1366 job will be sent whenever a board connects\n"
                     "[main] Press Ctrl+C to stop" << std::endl;
        wait_offline(dashboard, board_manager, "USART1 test mode");
        dashboard.stop();
        board_manager.stop();
        return 0;
    }

    if (chip_test_mode) {
        dashboard.setTestMode("CHIP TEST");
        std::cout << "[main] Chip test mode: pool connection is disabled\n"
                     "[main] Test threshold: difficulty 256; jobs rotate after each nonce or "
                  << (CHIP_TEST_JOB_TIMEOUT_MS / 1000) << " s\n"
                     "[main] Press Ctrl+C to stop" << std::endl;
        while (!g_shutdown_requested) {
            const uint64_t now = GetTickCount64();
            for (const auto& board : board_manager.getStats()) {
                if (board.online && board.last_job_time != 0 &&
                    now - board.last_job_time >= CHIP_TEST_JOB_TIMEOUT_MS) {
                    std::cout << "[chip-test] TIMEOUT board=0x" << std::hex
                              << board.info.board_id << std::dec
                              << "; rotating test job" << std::endl;
                    scheduler.dispatchChipTestWork(board.info.board_id);
                }
            }
            update_dashboard(dashboard, board_manager, "CHIP TEST", false);
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
        }
        dashboard.stop();
        board_manager.stop();
        return 0;
    }

    struct PoolState {
        std::string extranonce_1;
        int extranonce_2_len = 0;
        uint32_t version_mask = 0;
        double difficulty = 0.0;
        bool subscribed = false;
        bool configured = false;
        bool authorize_answered = false;
        bool authorized = false;
    } pool_state;
    pool_state.difficulty = config.min_difficulty;

    stratum.onSubscribeResult = [&](const SubscribeResult& result) {
        pool_state.extranonce_1 = result.extranonce;
        pool_state.extranonce_2_len = result.extranonce_2_len;
        pool_state.subscribed = true;
        std::cout << "[main] Subscription established (extranonce2="
                  << result.extranonce_2_len << " bytes)" << std::endl;
    };

    stratum.onSetExtranonce = [&](const SubscribeResult& result) {
        pool_state.extranonce_1 = result.extranonce;
        pool_state.extranonce_2_len = result.extranonce_2_len;
        scheduler.clearLatestWork();
        std::cout << "[main] Pool changed extranonce; waiting for a fresh job"
                  << std::endl;
    };

    stratum.onConfigureResult = [&](const ConfigureResult& result) {
        pool_state.configured = true;
        pool_state.version_mask = result.enabled ? result.version_mask : 0;
        std::cout << "[main] Version rolling: "
                  << (result.enabled ? "enabled" : "disabled")
                  << " mask=0x" << std::hex << pool_state.version_mask
                  << std::dec << std::endl;
    };

    stratum.onSetDifficulty = [&](double difficulty) {
        pool_state.difficulty = std::max(difficulty, config.min_difficulty);
        std::cout << "[main] Pool difficulty: " << pool_state.difficulty
                  << std::endl;
    };

    stratum.onNotify = [&](const MiningNotify& notify) {
        if (!pool_state.subscribed || pool_state.extranonce_2_len <= 0) {
            std::cerr << "[main] Ignoring notify received before subscription data"
                      << std::endl;
            return;
        }

        WorkDefinition work{};
        work.pool_job_id = notify.job_id;
        work.prev_block_hash = notify.prev_block_hash;
        work.coinbase_1 = notify.coinbase_1;
        work.coinbase_2 = notify.coinbase_2;
        work.extranonce_1 = pool_state.extranonce_1;
        work.version = notify.version;
        work.version_mask = pool_state.version_mask;
        work.ntime = notify.ntime;
        work.nbits = notify.nbits;
        work.pool_difficulty = pool_state.difficulty;
        work.extranonce_2_len =
            static_cast<uint8_t>(pool_state.extranonce_2_len);
        work.clean_jobs = notify.clean_jobs;

        for (const std::string& branch_hex : notify.merkle_branches) {
            std::array<uint8_t, 32> branch{};
            if (!hex2bin(branch_hex, branch.data(), branch.size())) {
                std::cerr << "[main] Invalid merkle branch in validated notify"
                          << std::endl;
                return;
            }
            work.merkle_branches.push_back(branch);
        }

        std::cout << "[main] New pool job: " << notify.job_id
                  << " clean=" << notify.clean_jobs << std::endl;
        scheduler.dispatchNewWork(work);
    };

    stratum.onAuthorizeResult = [&](bool accepted, const std::string& error) {
        pool_state.authorize_answered = true;
        pool_state.authorized = accepted;
        std::cout << "[main] Authorization: "
                  << (accepted ? "accepted" : "rejected");
        if (!error.empty()) std::cout << " (" << error << ')';
        std::cout << std::endl;
    };

    stratum.onShareResponse = [&](int message_id, uint64_t board_id,
                                  bool accepted, const std::string& error) {
        if (accepted) {
            ++g_shares_accepted;
            board_manager.addAcceptedShare(board_id);
        } else {
            ++g_shares_rejected;
            board_manager.addRejectedShare(board_id);
        }
        std::cout << "[main] Share " << message_id << ": "
                  << (accepted ? "ACCEPTED" : "REJECTED");
        if (!error.empty()) std::cout << " (" << error << ')';
        std::cout << std::endl;
    };

    auto wait_for = [&](const std::function<bool()>& condition,
                        int timeout_ms) {
        const auto deadline = std::chrono::steady_clock::now() +
                              std::chrono::milliseconds(timeout_ms);
        while (!g_shutdown_requested && stratum.isConnected() &&
               !condition()) {
            const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - std::chrono::steady_clock::now()).count();
            if (remaining <= 0) break;
            stratum.readResponse(static_cast<int>(std::min<int64_t>(remaining, 1000)));
        }
        return condition();
    };

    std::cout << "[main] Connecting to pool: " << pool_label << std::endl;
    if (!stratum.connect(config.pool_host, config.pool_port)) {
        std::cerr << "[main] Pool unavailable; board/dashboard services remain active\n"
                     "[main] Press Ctrl+C to stop" << std::endl;
        wait_offline(dashboard, board_manager, pool_label);
        dashboard.stop();
        board_manager.stop();
        return 1;
    }

    bool handshake_ok = stratum.subscribe("BTCMinerControl/0.2") &&
        wait_for([&] { return pool_state.subscribed; }, 15000);

    if (handshake_ok && config.version_rolling) {
        if (!stratum.configureVersionRolling() ||
            !wait_for([&] { return pool_state.configured; }, 10000)) {
            std::cerr << "[main] Version rolling negotiation timed out; disabling it"
                      << std::endl;
            pool_state.version_mask = 0;
        }
    }

    if (handshake_ok) {
        handshake_ok = stratum.authorize(config.pool_user, config.pool_pass) &&
            wait_for([&] { return pool_state.authorize_answered; }, 15000) &&
            pool_state.authorized;
    }

    if (!handshake_ok || g_shutdown_requested) {
        std::cerr << "[main] Pool handshake did not complete successfully" << std::endl;
        stratum.stop();
        if (!g_shutdown_requested) wait_offline(dashboard, board_manager, pool_label);
        dashboard.stop();
        board_manager.stop();
        return 1;
    }

    std::thread stratum_thread([&] { stratum.run(); });
    std::cout << "[main] Pool and board services are active; press Ctrl+C to stop"
              << std::endl;

    auto last_tick = std::chrono::steady_clock::now();
    while (!g_shutdown_requested && stratum.isConnected()) {
        update_dashboard(dashboard, board_manager, pool_label, true);
        const auto now_t = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now_t - last_tick).count() >= 5000) {
            last_tick = now_t;
            scheduler.tick();  /* TEMP: fresh midstate every 5s, like Bitaxe create_jobs_task */
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }

    stratum.stop();
    if (stratum_thread.joinable()) stratum_thread.join();
    update_dashboard(dashboard, board_manager, pool_label, false);
    dashboard.stop();
    board_manager.stop();
    std::cout << "[main] Shutdown complete" << std::endl;
    return 0;
}

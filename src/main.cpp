// BTCMinerControl — PC-side proxy for distributed BM1366 Bitcoin miners.
//
// Architecture:
//   Stratum Pool ←→ [StratumClient] ←→ [WorkScheduler] ←→ [BoardManager] ←→ N× STM32 Boards
//                                        ↕
//                                  [DashboardServer] (Web UI on :8080)
//
// Usage:
//   BTCMinerControl.exe [config.json]
//
// config.json example:
// {
//   "pool": { "host": "stratum.braiins.com", "port": 3333,
//             "user": "username.worker", "pass": "x" },
//   "board_port": 4028,
//   "dashboard_port": 8080,
//   "version_rolling": true,
//   "frequency_mhz": 485
// }

#include "mine/job.h"
#include "mine/sha256.h"
#include "mine/coinbase.h"
#include "mine/merkle.h"
#include "mine/midstate.h"
#include "mine/verify.h"
#include "stratum/client.h"
#include "dispatch/manager.h"
#include "dispatch/scheduler.h"
#include "dispatch/protocol.h"
#include "dashboard/server.h"
#include "config.h"

#include <iostream>
#include <csignal>
#include <thread>
#include <chrono>

// Global state for graceful shutdown
static BoardManager*    g_board_mgr = nullptr;
static DashboardServer* g_dashboard = nullptr;
static StratumClient*   g_stratum  = nullptr;
static WorkScheduler*   g_scheduler = nullptr;

// Stats
static std::atomic<uint64_t> g_shares_accepted{0};
static std::atomic<uint64_t> g_shares_rejected{0};
static std::atomic<double>   g_hashrate_total{0.0};
static std::string           g_pool_user;

static void cleanup() {
    std::cout << "\nShutting down..." << std::endl;
    if (g_stratum)  g_stratum->stop();
    if (g_dashboard) g_dashboard->stop();
    if (g_board_mgr) g_board_mgr->stop();
}

int main(int argc, char* argv[]) {
    std::cout << "============================================" << std::endl;
    std::cout << "  BTCMinerControl v0.1.0" << std::endl;
    std::cout << "  Distributed BM1366 Mining Proxy" << std::endl;
    std::cout << "============================================" << std::endl;

    // Load configuration
    std::string config_path = (argc > 1) ? argv[1] : "config.json";
    AppConfig cfg = load_config(config_path);
    g_pool_user = cfg.pool_user;

    // --- Start Board TCP Server ---
    BoardManager board_mgr;
    g_board_mgr = &board_mgr;

    if (!board_mgr.start(cfg.board_port)) {
        std::cerr << "FAILED to start board server!" << std::endl;
        return 1;
    }

    // --- Start Work Scheduler ---
    WorkScheduler scheduler(board_mgr);
    g_scheduler = &scheduler;

    // Wire board nonce results → scheduler
    board_mgr.onNonceResult = [&](const NonceResult& nr) {
        const MinerJob* job = scheduler.getJob(nr.job_id);
        if (!job) {
            std::cout << "[main] Nonce for unknown job " << int(nr.job_id) << std::endl;
            return;
        }

        auto [submit, info] = scheduler.processNonce(*job, nr);

        if (submit) {
            int msg_id;
            if (g_stratum && g_stratum->isConnected()) {
                ShareSubmit share;
                share.username     = g_pool_user;
                share.job_id       = info.pool_job_id;
                share.extranonce_2 = info.extranonce_2;
                share.ntime        = info.ntime;
                share.nonce        = info.nonce;
                share.version_bits = info.version_bits;

                g_stratum->submitShare(share, msg_id);
                g_shares_accepted++;
                board_mgr.addAcceptedShare(nr.board_id);
            }
        }
    };

    // --- Start Dashboard ---
    DashboardServer dashboard;
    g_dashboard = &dashboard;
    dashboard.start(cfg.dashboard_port, &board_mgr);

    // --- Connect to Mining Pool ---
    StratumClient stratum;
    g_stratum = &stratum;

    // Track current mining.notify data for job construction
    struct {
        MiningNotify notify;
        std::string  extranonce;
        int          extranonce_2_len = 4;
        uint32_t     version_mask = 0;
        double       pool_difficulty = 256.0;
        bool         has_notify = false;
    } pool_state;
    pool_state.pool_difficulty = cfg.min_difficulty;

    // Pool callbacks
    stratum.onSubscribeResult = [&](const SubscribeResult& sr) {
        pool_state.extranonce = sr.extranonce;
        pool_state.extranonce_2_len = sr.extranonce_2_len;
    };

    stratum.onConfigureResult = [&](const ConfigureResult& cr) {
        pool_state.version_mask = cr.version_mask;
        std::cout << "[main] Version-rolling mask: 0x"
                  << std::hex << cr.version_mask << std::dec << std::endl;
    };

    stratum.onSetDifficulty = [&](double diff) {
        pool_state.pool_difficulty = (std::max)(diff, cfg.min_difficulty);
        std::cout << "[main] Pool difficulty: " << pool_state.pool_difficulty << std::endl;
    };

    stratum.onNotify = [&](const MiningNotify& n) {
        pool_state.notify = n;
        pool_state.has_notify = true;

        std::cout << "[main] New block! Job: " << n.job_id
                  << " (clean_jobs=" << n.clean_jobs << ")" << std::endl;

        // Step 1: Decode merkle branches from hex to binary
        std::vector<uint8_t> merkle_bin;
        for (const auto& branch_hex : n.merkle_branches) {
            uint8_t branch[32];
            hex2bin(branch_hex, branch, 32);
            merkle_bin.insert(merkle_bin.end(), branch, branch + 32);
        }

        // Generate extranonce_2 for the first board
        std::string en2 = extranonce_2_generate(0, uint8_t(pool_state.extranonce_2_len));

        // Step 2: Compute coinbase tx hash
        auto coinbase_hash = calculate_coinbase_tx_hash(
            n.coinbase_1, n.coinbase_2,
            pool_state.extranonce, en2);

        // Step 3: Compute merkle root
        auto merkle_root = calculate_merkle_root(
            coinbase_hash.data(),
            merkle_bin.data(),
            n.merkle_branches.size());

        // Step 4: Build job template
        JobParams params;
        params.version         = n.version;
        params.version_mask    = pool_state.version_mask;
        params.prev_block_hash = n.prev_block_hash;
        memcpy(params.merkle_root, merkle_root.data(), 32);
        params.ntime           = n.ntime;
        params.nbits           = n.nbits;
        params.pool_difficulty = pool_state.pool_difficulty;
        params.pool_job_id     = n.job_id;
        params.extranonce_2    = en2;

        MinerJob template_job = build_job(params, 0);

        // Step 5: Dispatch to all boards
        scheduler.dispatchNewWork(template_job);
    };

    stratum.onAuthorizeResult = [](bool ok, const std::string&) {
        std::cout << "[main] Authorize: " << (ok ? "OK" : "FAILED") << std::endl;
    };

    stratum.onShareResponse = [&](int /*msg_id*/, bool accepted, const std::string& err) {
        if (!accepted) {
            g_shares_rejected++;
            std::cout << "[main] Share REJECTED: " << err << std::endl;
        }
    };

    // Connect and authenticate
    std::cout << "[main] Connecting to pool: " << cfg.pool_host << ":" << cfg.pool_port << std::endl;

    if (!stratum.connect(cfg.pool_host, cfg.pool_port)) {
        std::cerr << "[main] Cannot connect to pool. Board server still running." << std::endl;
        std::cerr << "[main] Press Ctrl+C to exit." << std::endl;
        // Keep board server alive so STM32 boards can still connect
        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds(5));
            auto stats = board_mgr.getStats();
            int online = 0;
            for (auto& s : stats) if (s.online) online++;
            dashboard.setPoolStats(cfg.pool_host + ":" + std::to_string(cfg.pool_port),
                                   false, g_shares_accepted, g_shares_rejected, 0.0);
        }
    }

    // Subscribe + authorize — read responses synchronously
    std::cout << "[main] Sending subscribe..." << std::endl;
    if (!stratum.subscribe("BTCMinerControl/0.1")) {
        std::cerr << "[main] Failed to send subscribe!" << std::endl;
    }
    // Read subscribe response (pool returns extranonce & extranonce2_len)
    for (int i = 0; i < 5 && stratum.isConnected(); i++) {
        std::string resp = stratum.readResponse(3000);
        if (resp.empty()) break;
    }

    if (cfg.version_rolling && stratum.isConnected()) {
        std::cout << "[main] Sending configure (version-rolling)..." << std::endl;
        stratum.configureVersionRolling();
        for (int i = 0; i < 3 && stratum.isConnected(); i++) {
            std::string resp = stratum.readResponse(3000);
            if (resp.empty()) break;
        }
    }

    if (stratum.isConnected()) {
        std::cout << "[main] Sending authorize as " << cfg.pool_user << "..." << std::endl;
        stratum.authorize(cfg.pool_user, cfg.pool_pass);
        for (int i = 0; i < 3 && stratum.isConnected(); i++) {
            std::string resp = stratum.readResponse(3000);
            if (resp.empty()) break;
        }
    }

    if (!stratum.isConnected()) {
        std::cerr << "[main] Pool disconnected during handshake!" << std::endl;
        std::cerr << "[main] Check: 1) pool host/port  2) username.workername format  3) whether pool requires TLS" << std::endl;
        std::cerr << "[main] Board server still running. Press Ctrl+C to exit." << std::endl;
        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds(5));
            auto stats = board_mgr.getStats();
            int online = 0;
            for (auto& s : stats) if (s.online) online++;
            dashboard.setPoolStats(cfg.pool_host + ":" + std::to_string(cfg.pool_port),
                                   false, g_shares_accepted, g_shares_rejected, 0.0);
        }
    }

    std::cout << "[main] Handshake complete, pool connected!" << std::endl;

    // --- Main loop ---
    std::cout << "[main] Entering receive loop..." << std::endl;
    std::cout << "[main] Web dashboard: http://localhost:" << cfg.dashboard_port << std::endl;
    std::cout << "[main] Board server: port " << cfg.board_port << std::endl;
    std::cout << "[main] Press Ctrl+C to stop." << std::endl;

    // Run stratum receive in background thread
    std::thread stratum_thread([&]() {
        stratum.run();
    });

    // Main thread — update dashboard periodically
    while (stratum.isConnected()) {
        std::this_thread::sleep_for(std::chrono::seconds(2));

        dashboard.setPoolStats(
            cfg.pool_host + ":" + std::to_string(cfg.pool_port),
            stratum.isConnected(),
            g_shares_accepted,
            g_shares_rejected,
            g_hashrate_total
        );
    }

    std::cout << "[main] Pool disconnected." << std::endl;
    cleanup();
    if (stratum_thread.joinable()) stratum_thread.join();

    return 0;
}

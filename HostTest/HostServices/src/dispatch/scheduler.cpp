#include "scheduler.h"
#include "../mine/coinbase.h"
#include "../mine/merkle.h"
#include "../mine/midstate.h"
#include "../mine/sha256.h"
#include "../mine/verify.h"
#include <cstring>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>

WorkScheduler::WorkScheduler(BoardManager& manager)
    : m_manager(manager) {}

uint8_t WorkScheduler::nextJobId() {
    // BM1366 puts the small-core number in bits 0..2. ESP-Miner rotates the
    // remaining ID through 0x00..0x78, so keep exactly the same namespace.
    const uint32_t slot = (m_job_sequence.fetch_add(1) + 1) % kJobSlots;
    return static_cast<uint8_t>(slot << 3);
}

bool WorkScheduler::dispatchPreparedJob(uint64_t board_id, MinerJob job) {
    const size_t slot = job.job_id >> 3;
    if ((job.job_id & 0x07U) != 0 || slot >= kJobSlots) return false;

    {
        std::lock_guard<std::mutex> lock(m_jobs_mutex);
        m_jobs_by_board[board_id][slot] = job;
    }

    const auto wire_data = encode_job(job);
    if (m_manager.sendJob(board_id, wire_data)) {
        std::lock_guard<std::mutex> lock(m_jobs_mutex);
        m_last_jobs[board_id] = job;   /* TEMP: fallback snapshot */
        return true;
    }

    // Do not leave a job valid if it never reached this connection.
    std::lock_guard<std::mutex> lock(m_jobs_mutex);
    auto board_it = m_jobs_by_board.find(board_id);
    if (board_it != m_jobs_by_board.end() &&
        board_it->second[slot] && board_it->second[slot]->job_id == job.job_id) {
        board_it->second[slot].reset();
    }
    return false;
}

bool WorkScheduler::dispatchToBoard(const WorkDefinition& work, uint64_t board_id) {
    if (work.extranonce_2_len == 0 || work.extranonce_2_len > 32) {
        std::cerr << "[scheduler] Invalid extranonce2 length: "
                  << unsigned(work.extranonce_2_len) << std::endl;
        return false;
    }

    const uint64_t counter = m_extranonce_counter.fetch_add(1);
    const std::string en2 = extranonce_2_generate(counter, work.extranonce_2_len);

    const auto coinbase_hash = calculate_coinbase_tx_hash(
        work.coinbase_1, work.coinbase_2, work.extranonce_1, en2);

    const uint8_t* branches = work.merkle_branches.empty()
        ? nullptr
        : reinterpret_cast<const uint8_t*>(work.merkle_branches.data());
    const auto merkle_root = calculate_merkle_root(
        coinbase_hash.data(), branches, work.merkle_branches.size());

    /* TEMP: dump coinbase/merkle data for offline pool-rebuild verification */
    {
        auto hexstr = [](const uint8_t* p, size_t n) {
            std::ostringstream oss;
            oss << std::hex << std::setfill('0');
            for (size_t i = 0; i < n; i++) oss << std::setw(2) << unsigned(p[i]);
            return oss.str();
        };
        std::cout << "[MERKLE] coinb1=" << work.coinbase_1
                  << " coinb2=" << work.coinbase_2
                  << " en1=" << work.extranonce_1
                  << " en2=" << en2
                  << " branches=" << work.merkle_branches.size()
                  << " br=";
        for (const auto& b : work.merkle_branches) {
            std::cout << hexstr(b.data(), 32) << ";";
        }
        std::cout << " root=" << hexstr(merkle_root.data(), 32)
                  << " prev=" << work.prev_block_hash
                  << " ntime=0x" << std::hex << work.ntime << std::dec
                  << " version=0x" << std::hex << work.version << std::dec
                  << " jobid=" << work.pool_job_id << std::endl;
    }
    JobParams params{};
    params.version = work.version;
    params.version_mask = work.version_mask;
    params.prev_block_hash = work.prev_block_hash;
    std::memcpy(params.merkle_root, merkle_root.data(), merkle_root.size());
    params.ntime = work.ntime;
    params.nbits = work.nbits;
    params.pool_difficulty = work.pool_difficulty;
    params.pool_job_id = work.pool_job_id;
    params.extranonce_2 = en2;

    return dispatchPreparedJob(board_id, build_job(params, nextJobId()));
}

void WorkScheduler::dispatchNewWork(const WorkDefinition& work) {
    std::lock_guard<std::mutex> dispatch_lock(m_dispatch_mutex);
    m_latest_work = work;

    if (work.clean_jobs) {
        std::lock_guard<std::mutex> jobs_lock(m_jobs_mutex);
        m_jobs_by_board.clear();
    }

    const auto boards = m_manager.getStats();
    size_t dispatched = 0;
    for (const auto& board : boards) {
        if (board.online && dispatchToBoard(work, board.info.board_id)) {
            ++dispatched;
        }
    }

    std::cout << "[scheduler] Dispatched work to " << dispatched
              << " board(s)" << std::endl;
}

bool WorkScheduler::dispatchLatestToBoard(uint64_t board_id) {
    std::lock_guard<std::mutex> dispatch_lock(m_dispatch_mutex);
    if (!m_latest_work) return false;
    return dispatchToBoard(*m_latest_work, board_id);
}

void WorkScheduler::tick() {
    std::lock_guard<std::mutex> dispatch_lock(m_dispatch_mutex);
    if (!m_latest_work) return;
    const auto boards = m_manager.getStats();
    for (const auto& board : boards) {
        if (board.online) dispatchToBoard(*m_latest_work, board.info.board_id);
    }
}

void WorkScheduler::clearLatestWork() {
    std::lock_guard<std::mutex> dispatch_lock(m_dispatch_mutex);
    m_latest_work.reset();
}

bool WorkScheduler::dispatchSyntheticWork(uint64_t board_id, double difficulty,
                                          const std::string& label,
                                          uint32_t sequence) {
    JobParams params{};
    params.version = 0x20000000U;
    params.version_mask = 0;
    params.prev_block_hash = std::string(64, '0');
    for (size_t i = 0; i < sizeof(params.merkle_root); ++i) {
        params.merkle_root[i] = static_cast<uint8_t>(
            0x5aU ^ uint8_t(sequence >> ((i % 4) * 8)) ^ uint8_t(i));
    }
    params.ntime = 0x65000000U + sequence;
    params.nbits = 0x1d00ffffU;
    params.pool_difficulty = difficulty;
    params.pool_job_id = label + '-' + std::to_string(sequence);
    params.extranonce_2 = "00000000";

    const bool sent = dispatchPreparedJob(board_id, build_job(params, nextJobId()));
    if (sent) {
        std::cout << "[scheduler] " << label << " job " << sequence
                  << " sent to board 0x"
                  << std::hex << board_id << std::dec << std::endl;
    }
    return sent;
}

bool WorkScheduler::dispatchUartTestWork(uint64_t board_id) {
    std::lock_guard<std::mutex> dispatch_lock(m_dispatch_mutex);
    return dispatchSyntheticWork(board_id,
        std::numeric_limits<double>::infinity(), "uart-test", 0);
}

bool WorkScheduler::dispatchChipTestWork(uint64_t board_id) {
    std::lock_guard<std::mutex> dispatch_lock(m_dispatch_mutex);
    const uint32_t sequence = m_test_sequence.fetch_add(1) + 1;
    return dispatchSyntheticWork(board_id, 256.0, "chip-test", sequence);
}

std::optional<MinerJob> WorkScheduler::getJob(
    uint64_t board_id, uint8_t job_id) const {
    if ((job_id & 0x07U) != 0 || job_id >= 0x80U) return std::nullopt;
    const size_t slot = job_id >> 3;

    std::lock_guard<std::mutex> lock(m_jobs_mutex);
    const auto board_it = m_jobs_by_board.find(board_id);
    if (board_it == m_jobs_by_board.end()) return std::nullopt;
    return board_it->second[slot];
}

std::optional<MinerJob> WorkScheduler::getLatestJob(uint64_t board_id) const {
    std::lock_guard<std::mutex> lock(m_jobs_mutex);
    const auto it = m_last_jobs.find(board_id);
    if (it == m_last_jobs.end()) return std::nullopt;
    return it->second;
}

VerifiedNonce WorkScheduler::processNonce(
    const MinerJob& job, const NonceResult& result) {
    return verify_nonce(job, result);
}

int WorkScheduler::boardCount() {
    const auto boards = m_manager.getStats();
    int count = 0;
    for (const auto& board : boards) {
        if (board.online) ++count;
    }
    return count;
}

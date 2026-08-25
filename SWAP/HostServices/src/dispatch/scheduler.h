#pragma once

#include "../mine/job.h"
#include "manager.h"
#include <array>
#include <atomic>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

// Everything needed to build an independent coinbase/header for a board.
// The extranonce2 value is deliberately not stored here; the scheduler assigns
// a unique value when it constructs each board's MinerJob.
struct WorkDefinition {
    std::string pool_job_id;
    std::string prev_block_hash;
    std::string coinbase_1;
    std::string coinbase_2;
    std::string extranonce_1;
    std::vector<std::array<uint8_t, 32>> merkle_branches;
    uint32_t version = 0;
    uint32_t version_mask = 0;
    uint32_t ntime = 0;
    uint32_t nbits = 0;
    double pool_difficulty = 0.0;
    uint8_t extranonce_2_len = 0;
    bool clean_jobs = false;
};

class WorkScheduler {
public:
    explicit WorkScheduler(BoardManager& manager);

    // Cache the latest notify and dispatch an independently constructed job to
    // every online board.
    void dispatchNewWork(const WorkDefinition& work);

    // Called when a board connects after the most recent mining.notify.
    bool dispatchLatestToBoard(uint64_t board_id);
    // Drop both the cached notify and all in-flight job mappings (used when
    // the pool changes extranonce so stale shares cannot be submitted).
    void clearLatestWork();

    // Send a deterministic synthetic header for the no-ASIC USART1 test path.
    bool dispatchUartTestWork(uint64_t board_id);

    // Send rotating synthetic work at the firmware's 256-difficulty result
    // threshold. Returned nonces can therefore be checked without a pool.
    bool dispatchChipTestWork(uint64_t board_id);

    // Return a protected snapshot, keyed by both board and BM1366 job ID.
    std::optional<MinerJob> getJob(uint64_t board_id, uint8_t job_id) const;

    // TEMP: fallback for board re-dispatched jobs -- the board advances job_id
    // on its own every 5s, so those ids were never issued by the host. Returns
    // the most recently dispatched job for the board (same header content).
    std::optional<MinerJob> getLatestJob(uint64_t board_id) const;

    // TEMP: periodic re-dispatch of the latest work with a fresh extranonce2
    // (new midstate every tick, like Bitaxe create_jobs_task). The BM1366 chip
    // stops reporting after ~5 jobs with unchanged content. Call every 5s.
    void tick();

    VerifiedNonce processNonce(const MinerJob& job, const NonceResult& result);
    int boardCount();

private:
    static constexpr size_t kJobSlots = 16; // 0x00..0x78 in steps of eight

    bool dispatchToBoard(const WorkDefinition& work, uint64_t board_id);
    bool dispatchPreparedJob(uint64_t board_id, MinerJob job);
    bool dispatchSyntheticWork(uint64_t board_id, double difficulty,
                               const std::string& label, uint32_t sequence);
    uint8_t nextJobId();

    BoardManager& m_manager;
    std::atomic<uint64_t> m_extranonce_counter{0};
    std::atomic<uint32_t> m_job_sequence{0};
    std::atomic<uint32_t> m_test_sequence{0};

    // Serializes notify dispatch and late-board replay, so an older cached job
    // can never be sent after a newer notification.
    mutable std::mutex m_dispatch_mutex;
    std::optional<WorkDefinition> m_latest_work;

    mutable std::mutex m_jobs_mutex;
    std::unordered_map<uint64_t,
        std::array<std::optional<MinerJob>, kJobSlots>> m_jobs_by_board;
    mutable std::unordered_map<uint64_t, MinerJob> m_last_jobs;  // TEMP fallback
};

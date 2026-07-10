#pragma once
#include "../mine/job.h"
#include "manager.h"
#include <queue>
#include <mutex>
#include <atomic>

// ---------------------------------------------------------------------------
// Work Scheduler — manages job queue, distributes work to boards.
//
// When a new mining.notify arrives, it's processed into MinerJobs and
// dispatched to all connected boards. The scheduler also tracks which
// board gets which job for nonce verification.
// ---------------------------------------------------------------------------

class WorkScheduler {
public:
    WorkScheduler(BoardManager& manager);

    // Called when a new mining.notify is received. Generates a batch of
    // jobs (one per board, with unique extranonce_2 per board) and dispatches.
    void dispatchNewWork(const MinerJob& template_job);

    // Get the job template for a specific job_id (for nonce verification).
    const MinerJob* getJob(uint8_t job_id);

    // Called when a nonce needs to be verified against its job.
    // Returns Submit info if the nonce is valid enough to submit.
    std::pair<bool, SubmitInfo> processNonce(const MinerJob& job, const NonceResult& result);

    // Count of boards currently online
    int boardCount();

private:
    BoardManager& m_manager;
    std::atomic<uint64_t> m_extranonce_counter{0};

    // Job slots (128 positions, matching BM1366's 7-bit job_id space)
    mutable std::mutex m_jobs_mutex;
    MinerJob m_jobs[128];
    bool m_jobs_valid[128] = {};  // Removed incorrect initializer, initialized in constructor
};

#include "scheduler.h"
#include "../mine/verify.h"
#include "../mine/sha256.h"
#include <iostream>

WorkScheduler::WorkScheduler(BoardManager& manager)
    : m_manager(manager)
{
    memset(m_jobs_valid, 0, sizeof(m_jobs_valid));
}

void WorkScheduler::dispatchNewWork(const MinerJob& template_job) {
    auto boards = m_manager.getStats();
    uint8_t job_index = 0;

    for (auto& board : boards) {
        if (!board.online) continue;

        // Each board gets a unique extranonce_2
        uint64_t counter = m_extranonce_counter.fetch_add(1);
        int en2_len = 4; // default

        MinerJob job = template_job;
        job.job_id = job_index;
        job.extranonce_2 = extranonce_2_generate(counter, uint8_t(en2_len));

        // Store job for later verification
        {
            std::lock_guard<std::mutex> lock(m_jobs_mutex);
            m_jobs[job_index] = job;
            m_jobs_valid[job_index] = true;
        }

        // Encode and send to board
        auto wire_data = encode_job(job);
        m_manager.sendJob(board.info.board_id, wire_data);

        job_index = (job_index + 1) % 128;
    }

    if (job_index > 0) {
        std::cout << "[scheduler] Dispatched work to " << int(job_index) << " boards" << std::endl;
    }
}

const MinerJob* WorkScheduler::getJob(uint8_t job_id) {
    std::lock_guard<std::mutex> lock(m_jobs_mutex);
    if (m_jobs_valid[job_id]) {
        return &m_jobs[job_id];
    }
    return nullptr;
}

std::pair<bool, SubmitInfo> WorkScheduler::processNonce(const MinerJob& job, const NonceResult& result) {
    auto vn = verify_nonce(job, result);

    if (vn.result == VerifyResult::Submit) {
        return {true, vn.submit};
    }
    return {false, SubmitInfo{}};
}

int WorkScheduler::boardCount() {
    auto boards = m_manager.getStats();
    int count = 0;
    for (auto& b : boards) {
        if (b.online) count++;
    }
    return count;
}

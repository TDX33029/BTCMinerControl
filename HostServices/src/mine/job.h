#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <memory>

// ---------------------------------------------------------------------------
// Shared data structures for the mining pipeline
// ---------------------------------------------------------------------------

/// Pre-built job ready to send to a board's BM1366 ASICs.
/// Contains everything in ASIC byte order — the STM32 just forwards it to UART.
struct MinerJob {
    uint8_t  job_id;
    uint8_t  num_midstates;          // 1 or 4 (version-rolling)
    std::vector<std::array<uint8_t, 32>> midstates; // ASIC byte order
    uint32_t version;                // ASIC byte order (LE)
    uint8_t  prev_block_hash[32];    // ASIC byte order (word-reversed)
    uint8_t  merkle_root[32];        // ASIC byte order (word-reversed)
    uint32_t ntime;
    uint32_t nbits;
    uint32_t starting_nonce;

    // Fields needed for share submission (NOT sent to STM32)
    std::string pool_job_id;
    std::string extranonce_2;
    double    pool_difficulty;
};

/// A nonce result returned by a board.
struct NonceResult {
    uint8_t  job_id;
    uint64_t board_id;
    uint8_t  asic_nr;
    uint32_t nonce;
    uint32_t rolled_version;
    uint64_t timestamp_us;
};

/// Outcome of nonce verification.
enum class VerifyResult {
    Invalid,
    BelowDifficulty,
    Submit
};

/// Detailed result of verification (used when Submit is the outcome).
struct SubmitInfo {
    std::string pool_job_id;
    std::string extranonce_2;
    uint32_t    ntime;
    uint32_t    nonce;
    uint32_t    version_bits; // rolled_version ^ original_version
};

/// Combined verification result.
struct VerifiedNonce {
    VerifyResult result;
    double       difficulty; // valid only if result != Invalid
    SubmitInfo   submit;     // valid only if result == Submit
};

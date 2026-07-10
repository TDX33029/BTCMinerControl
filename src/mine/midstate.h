#pragma once
#include "job.h"
#include <string>

// Build a complete MinerJob from parsed mining.notify parameters.
// This performs all the heavy lifting:
//   1. Endianness conversions for ASIC byte order
//   2. Midstate pre-computation (SHA-256 of first 64 bytes of block header)
//   3. Version-rolling midstate variants (4 total if version_mask != 0)
//
// The resulting MinerJob is ready to be serialized and sent to the STM32 board.
// No further hashing or conversion is needed on the STM32 side.

struct JobParams {
    uint32_t    version;         // Block version (host byte order)
    uint32_t    version_mask;    // BIP-310 version-rolling mask (0 = disabled)
    std::string prev_block_hash; // Hex string (64 chars)
    uint8_t     merkle_root[32]; // Binary merkle root (host byte order)
    uint32_t    ntime;           // Block timestamp
    uint32_t    nbits;           // Difficulty target (compact format)
    double      pool_difficulty; // For share verification
    std::string pool_job_id;     // Pool's job_id string
    std::string extranonce_2;    // Hex extranonce_2 for this job
};

MinerJob build_job(const JobParams& params, uint8_t job_id);

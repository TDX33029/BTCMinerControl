// Midstate computation and job construction.
// Ported from ESP-Miner components/stratum/mining.c:construct_bm_job()

#include "midstate.h"
#include "sha256.h"
#include <cstring>
#include <stdexcept>

MinerJob build_job(const JobParams& params, uint8_t job_id) {
    MinerJob job{};
    job.job_id = job_id;
    job.ntime = params.ntime;
    job.nbits = params.nbits;
    job.starting_nonce = 0;
    job.pool_difficulty = params.pool_difficulty;
    job.pool_job_id = params.pool_job_id;
    job.extranonce_2 = params.extranonce_2;

    // Step 1: Decode prev_block_hash from hex. Stratum sends this value in
    // little-endian display order, so each 32-bit word must be byte-swapped to
    // recover the canonical block-header byte order. This matches ESP-Miner's
    // construct_bm_job() and is required for pool-side header reconstruction.
    uint8_t prev_hash_bin[32];
    if (!hex2bin(params.prev_block_hash, prev_hash_bin, 32)) {
        throw std::invalid_argument("previous block hash must be 32-byte hex");
    }
    sha256::reverse_endianness_per_word(prev_hash_bin);

    // The ASIC word-reverses the prev/merkle fields it receives, so store the
    // word-reverse of the canonical bytes; the chip's interpretation then
    // yields the canonical header field order.
    auto prev_for_asic = sha256::reverse_32bit_words(prev_hash_bin);
    memcpy(job.prev_block_hash, prev_for_asic.data(), 32);

    auto merkle_for_asic = sha256::reverse_32bit_words(params.merkle_root);
    memcpy(job.merkle_root, merkle_for_asic.data(), 32);

    // Build the 64-byte midstate input (canonical header prefix)
    uint8_t midstate_data[64] = {};

    uint32_t version_le = params.version; // x86 is LE, so this is already LE in memory
    memcpy(midstate_data, &version_le, 4);
    memcpy(midstate_data + 4, prev_hash_bin, 32);

    // First 28 bytes of the canonical (little-endian) merkle root
    memcpy(midstate_data + 36, params.merkle_root, 28);

    // Step 6: Upload version to job (ASIC uses LE bytes)
    memcpy(&job.version, &version_le, 4);

    // Step 7: Compute midstates
    job.midstates.clear();

    // First midstate (original version)
    auto ms0 = sha256::midstate_sha256(midstate_data);
    job.midstates.push_back(sha256::reverse_32bit_words(ms0.data()));

    if (params.version_mask != 0) {
        // Generate 3 more version-rolled midstates
        uint32_t rolled = params.version;
        for (int i = 0; i < 3; i++) {
            rolled = sha256::increment_bitmask(rolled, params.version_mask);
            uint32_t rolled_le = rolled;
            memcpy(midstate_data, &rolled_le, 4);
            auto ms = sha256::midstate_sha256(midstate_data);
            job.midstates.push_back(sha256::reverse_32bit_words(ms.data()));
        }
    }

    job.num_midstates = uint8_t(job.midstates.size());
    return job;
}

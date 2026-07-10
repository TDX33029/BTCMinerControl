// Midstate computation and job construction.
// Ported from ESP-Miner components/stratum/mining.c:construct_bm_job()

#include "midstate.h"
#include "sha256.h"
#include <cstring>

MinerJob build_job(const JobParams& params, uint8_t job_id) {
    MinerJob job;
    job.job_id = job_id;
    job.ntime = params.ntime;
    job.nbits = params.nbits;
    job.starting_nonce = 0;
    job.pool_difficulty = params.pool_difficulty;
    job.pool_job_id = params.pool_job_id;
    job.extranonce_2 = params.extranonce_2;

    // Step 1: Decode prev_block_hash from hex
    uint8_t prev_hash_bin[32];
    hex2bin(params.prev_block_hash, prev_hash_bin, 32);

    // Step 2: Reverse endianness of each 32-bit word in prev_hash
    sha256::reverse_endianness_per_word(prev_hash_bin);

    // Step 3: Reverse 32-bit word order → ASIC format
    auto prev_hash_asic = sha256::reverse_32bit_words(prev_hash_bin);
    memcpy(job.prev_block_hash, prev_hash_asic.data(), 32);

    // Step 4: Reverse 32-bit word order of merkle_root → ASIC format
    auto merkle_root_asic = sha256::reverse_32bit_words(params.merkle_root);
    memcpy(job.merkle_root, merkle_root_asic.data(), 32);

    // Step 5: Build the 64-byte midstate input
    // Layout: version(4 LE) || prev_block_hash(32, endian-reversed) || merkle_root[0..28]
    uint8_t midstate_data[64] = {};

    uint32_t version_le = params.version; // x86 is LE, so this is already LE in memory
    memcpy(midstate_data, &version_le, 4);
    memcpy(midstate_data + 4, prev_hash_bin, 32);

    // First 28 bytes of merkle_root (host byte order, before word reversal)
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

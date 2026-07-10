// Nonce verification — reconstruct block header, double-SHA256, compare difficulty.
// Ported from ESP-Miner components/stratum/mining.c:test_nonce_value()

#include "verify.h"
#include <cstring>

VerifiedNonce verify_nonce(const MinerJob& job, const NonceResult& result) {
    VerifiedNonce vn = {};
    vn.result = VerifyResult::Invalid;
    vn.difficulty = 0.0;

    // Reverse ASIC-format data back to network/host format for hashing
    auto prev_hash_net = sha256::reverse_32bit_words(job.prev_block_hash);
    auto merkle_root_net = sha256::reverse_32bit_words(job.merkle_root);

    // Build the 80-byte block header
    uint8_t header[80] = {};

    // bytes 0-3: rolled version (little-endian)
    uint32_t rv = result.rolled_version;
    memcpy(header, &rv, 4);

    // bytes 4-35: prev_block_hash (reverse endianness per word)
    uint8_t prev_hash_for_header[32];
    memcpy(prev_hash_for_header, prev_hash_net.data(), 32);
    sha256::reverse_endianness_per_word(prev_hash_for_header);
    memcpy(header + 4, prev_hash_for_header, 32);

    // bytes 36-67: merkle_root
    memcpy(header + 36, merkle_root_net.data(), 32);

    // bytes 68-71: ntime (little-endian)
    uint32_t nt = job.ntime;
    memcpy(header + 68, &nt, 4);

    // bytes 72-75: nbits (little-endian)
    uint32_t nb = job.nbits;
    memcpy(header + 72, &nb, 4);

    // bytes 76-79: nonce (little-endian)
    uint32_t nc = result.nonce;
    memcpy(header + 76, &nc, 4);

    // Double SHA-256
    auto hash = sha256::double_sha256(header, 80);

    // Convert to difficulty
    double diff = hash_to_pdiff(hash.data());
    vn.difficulty = diff;

    if (diff >= job.pool_difficulty) {
        vn.result = VerifyResult::Submit;
        vn.submit.pool_job_id = job.pool_job_id;
        vn.submit.extranonce_2 = job.extranonce_2;
        vn.submit.ntime = job.ntime;
        vn.submit.nonce = result.nonce;
        // version_bits = the bits that were rolled in
        vn.submit.version_bits = result.rolled_version ^ job.version;
    } else if (diff > 0.0) {
        vn.result = VerifyResult::BelowDifficulty;
    } else {
        vn.result = VerifyResult::Invalid;
    }

    return vn;
}

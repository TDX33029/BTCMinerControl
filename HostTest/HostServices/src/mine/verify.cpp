// Nonce verification — reconstruct block header, double-SHA256, compare difficulty.
// Ported from ESP-Miner components/stratum/mining.c:test_nonce_value()

#include "verify.h"
#include <cstring>
#include <iomanip>
#include <iostream>

VerifiedNonce verify_nonce(const MinerJob& job, const NonceResult& result) {
    VerifiedNonce vn = {};
    vn.result = VerifyResult::Invalid;
    vn.difficulty = 0.0;

    // The ASIC word-reverses the prev/merkle fields it receives (verified by
    // 5-nonce cross-validation, combo (0,1,1,0,0,0)); undo that to recover the
    // canonical header bytes.
    auto prev_for_header = sha256::reverse_32bit_words(job.prev_block_hash);
    auto merkle_for_header = sha256::reverse_32bit_words(job.merkle_root);

    // Build the 80-byte block header
    uint8_t header[80] = {};

    // bytes 0-3: rolled version (little-endian)
    uint32_t rv = result.rolled_version;
    memcpy(header, &rv, 4);

    // bytes 4-35: prev_block_hash
    memcpy(header + 4, prev_for_header.data(), 32);

    // bytes 36-67: merkle_root
    memcpy(header + 36, merkle_for_header.data(), 32);

    // bytes 68-71: ntime (little-endian)
    uint32_t nt = job.ntime;
    memcpy(header + 68, &nt, 4);

    // bytes 72-75: nbits (little-endian)
    uint32_t nb = job.nbits;
    memcpy(header + 72, &nb, 4);

    // bytes 76-79: nonce -- the board reports the little-endian host read of the
    // frame bytes (which equals the ASIC's nonce value); writing it LE back
    // reproduces the raw frame bytes the ASIC used. Pool rebuilds the same.
    uint32_t nc = result.nonce;
    memcpy(header + 76, &nc, 4);

    // TEMP: dump the rebuilt header for offline analysis
    std::cout << "[HEADER] ";
    for (int i = 0; i < 80; i++) {
        std::cout << std::hex << std::setw(2) << std::setfill('0') << unsigned(header[i]);
    }
    std::cout << std::dec << std::endl;

    // TEMP: dump the raw job merkle for pairing with [MERKLE] output
    std::cout << "[JOBM] merkle=";
    for (int i = 0; i < 32; i++) {
        std::cout << std::hex << std::setw(2) << std::setfill('0') << unsigned(job.merkle_root[i]);
    }
    std::cout << std::dec
              << " en2=" << job.extranonce_2
              << " jobid=" << job.pool_job_id << std::endl;

    // Double SHA-256
    auto hash = sha256::double_sha256(header, 80);

    // TEMP: dump the hash actually computed by verify_nonce
    {
        std::cout << "[HASH] ";
        for (int i = 0; i < 32; i++) {
            std::cout << std::hex << std::setw(2) << std::setfill('0') << unsigned(hash[i]);
        }
        std::cout << std::dec << std::endl;
    }

    // TEMP DIAG 1: does the rolled-version header prefix (bytes 0-63) match one
    // of the host's stored midstates (one per version-roll state)? OK => prefix
    // matches what the host believes the ASIC hashed. MISMATCH => rolled_version
    // or prefix bytes differ from the ASIC's midstate.
    {
        uint8_t ms_input[64];
        memcpy(ms_input, header, 64);
        auto ms_calc = sha256::midstate_sha256(ms_input);
        bool match = false;
        for (size_t i = 0; i < job.midstates.size(); i++) {
            auto ms_stored = sha256::reverse_32bit_words(job.midstates[i].data());
            if (memcmp(ms_calc.data(), ms_stored.data(), 32) == 0) { match = true; break; }
        }
        std::cout << "[verify] midstate " << (match ? "OK" : "MISMATCH")
                  << " job=" << unsigned(job.job_id)
                  << " ver=0x" << std::hex << rv << std::dec
                  << " ntime=0x" << std::hex << job.ntime << std::dec
                  << " nbits=0x" << std::hex << job.nbits << std::dec
                  << " nonce=0x" << std::hex << result.nonce << std::dec
                  << std::endl;
    }

    // TEMP DIAG 2: with difficulty mask 256 the chip only reports hashes whose
    // low byte is 0. Brute-force the 16 prev/merkle byte-order combos; a combo
    // with a stable zero byte matches the ASIC's byte interpretation.
    {
        uint8_t combos[4][32];
        memcpy(combos[0], job.prev_block_hash, 32);                  // as-is
        auto w1 = sha256::reverse_32bit_words(job.prev_block_hash);  // word-reverse
        memcpy(combos[1], w1.data(), 32);
        uint8_t t2[32];
        memcpy(t2, job.prev_block_hash, 32);
        sha256::reverse_endianness_per_word(t2);                     // per-word reverse
        memcpy(combos[2], t2, 32);
        uint8_t t3[32];
        memcpy(t3, combos[1], 32);
        sha256::reverse_endianness_per_word(t3);                     // both
        memcpy(combos[3], t3, 32);

        // nonce byte orders: 0=LE as received, 1=BE
        uint8_t ncombos[2][4];
        memcpy(ncombos[0], &result.nonce, 4);
        uint32_t n1 = ((result.nonce & 0xFFU) << 24) | ((result.nonce & 0xFF00U) << 8) |
                      ((result.nonce & 0xFF0000U) >> 8) | ((result.nonce & 0xFF000000U) >> 24);
        memcpy(ncombos[1], &n1, 4);

        for (int pc = 0; pc < 4; pc++) {
            for (int mc = 0; mc < 4; mc++) {
                for (int nc = 0; nc < 2; nc++) {
                    uint8_t h[80];
                    memcpy(h, header, 80);
                    memcpy(h + 4, combos[pc], 32);
                    memcpy(h + 36, combos[mc], 32);
                    memcpy(h + 76, ncombos[nc], 4);
                    auto hh = sha256::double_sha256(h, 80);
                    for (int b = 0; b < 32; b++) {
                        if (hh[b] == 0) {
                            std::cout << "[combo] pc=" << pc << " mc=" << mc
                                      << " nc=" << nc << " zero_byte=" << b
                                      << " job=" << unsigned(job.job_id) << std::endl;
                        }
                    }
                }
            }
        }
    }

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

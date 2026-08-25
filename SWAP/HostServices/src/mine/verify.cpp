// Nonce verification — reconstruct block header, double-SHA256, compare difficulty.
// Ported from ESP-Miner components/stratum/mining.c:test_nonce_value()

#ifndef VERIFY_DIAG
#define VERIFY_DIAG 0
#endif

#include "verify.h"
#include <cstring>
#if VERIFY_DIAG
#include <iomanip>
#include <iostream>
#endif

VerifiedNonce verify_nonce(const MinerJob& job, const NonceResult& result) {
    VerifiedNonce vn = {};
    vn.result = VerifyResult::Invalid;
    vn.difficulty = 0.0;

    // The ASIC word-reverses the prev/merkle fields it receives; undo that to
    // recover the canonical header bytes.
    auto prev_for_header = sha256::reverse_32bit_words(job.prev_block_hash);
    auto merkle_for_header = sha256::reverse_32bit_words(job.merkle_root);

    // Build the 80-byte block header.
    uint8_t header[80] = {};

    uint32_t rv = result.rolled_version;        // bytes 0-3
    memcpy(header, &rv, 4);

    memcpy(header + 4, prev_for_header.data(), 32);   // bytes 4-35
    memcpy(header + 36, merkle_for_header.data(), 32); // bytes 36-67

    uint32_t nt = job.ntime;                    // bytes 68-71
    memcpy(header + 68, &nt, 4);

    uint32_t nb = job.nbits;                    // bytes 72-75
    memcpy(header + 72, &nb, 4);

    // bytes 76-79: nonce. The board reports the little-endian host read of the
    // frame bytes (which equals the ASIC's nonce value); writing it LE back
    // reproduces the raw frame bytes the ASIC used. Pool rebuilds the same.
    uint32_t nc = result.nonce;
    memcpy(header + 76, &nc, 4);

#if VERIFY_DIAG
    {
        std::cout << "[HEADER] ";
        for (int i = 0; i < 80; i++) {
            std::cout << std::hex << std::setw(2) << std::setfill('0') << unsigned(header[i]);
        }
        std::cout << std::dec << std::endl;

        std::cout << "[JOBM] merkle=";
        for (int i = 0; i < 32; i++) {
            std::cout << std::hex << std::setw(2) << std::setfill('0') << unsigned(job.merkle_root[i]);
        }
        std::cout << std::dec
                  << " en2=" << job.extranonce_2
                  << " jobid=" << job.pool_job_id << std::endl;
    }
#endif

    // Double SHA-256.
    auto hash = sha256::double_sha256(header, 80);

#if VERIFY_DIAG
    {
        std::cout << "[HASH] ";
        for (int i = 0; i < 32; i++) {
            std::cout << std::hex << std::setw(2) << std::setfill('0') << unsigned(hash[i]);
        }
        std::cout << std::dec << std::endl;

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
#endif

    // Convert to difficulty.
    double diff = hash_to_pdiff(hash.data());
    vn.difficulty = diff;

    if (diff >= job.pool_difficulty) {
        vn.result = VerifyResult::Submit;
        vn.submit.pool_job_id = job.pool_job_id;
        vn.submit.extranonce_2 = job.extranonce_2;
        vn.submit.ntime = job.ntime;
        vn.submit.nonce = result.nonce;
        // version_bits = the bits that were rolled in.
        vn.submit.version_bits = result.rolled_version ^ job.version;
    } else if (diff > 0.0) {
        vn.result = VerifyResult::BelowDifficulty;
    } else {
        vn.result = VerifyResult::Invalid;
    }

    return vn;
}

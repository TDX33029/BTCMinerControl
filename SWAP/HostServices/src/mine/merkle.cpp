#include "merkle.h"
#include "sha256.h"
#include <cstring>

std::array<uint8_t, 32> calculate_merkle_root(
    const uint8_t coinbase_tx_hash[32],
    const uint8_t* merkle_branches,
    size_t n_merkle_branches)
{
    // Bitcoin merkle root calculation (cgminer / ESP-Miner compatible):
    //
    //   current = SHA256d(coinbase_tx)          (raw digest bytes)
    //   current = SHA256d(current || branch[i]) (raw digest bytes)
    //
    // Both coinbase_tx_hash and the pool-provided merkle_branch strings are
    // already in the byte order used by the hash function here. No reversal
    // is performed before hashing; SHA256d's raw output is exactly what the
    // block header merkle_root field contains (canonical little-endian bytes).
    uint8_t both[64];
    memcpy(both, coinbase_tx_hash, 32);

    for (size_t i = 0; i < n_merkle_branches; i++) {
        memcpy(both + 32, merkle_branches + i * 32, 32);
        auto hash = sha256::double_sha256(both, 64);
        memcpy(both, hash.data(), 32);
    }

    std::array<uint8_t, 32> result;
    memcpy(result.data(), both, 32);
    return result;
}

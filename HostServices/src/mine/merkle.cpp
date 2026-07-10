#include "merkle.h"
#include "sha256.h"
#include <cstring>

std::array<uint8_t, 32> calculate_merkle_root(
    const uint8_t coinbase_tx_hash[32],
    const uint8_t* merkle_branches,
    size_t n_merkle_branches)
{
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

#pragma once
#include <array>

// Compute the merkle root by iteratively pairing the coinbase tx hash
// with each merkle branch through double-SHA256.
//
// result = double_sha256(coinbase_tx_hash || branch[0])
// result = double_sha256(result || branch[1])
// ...

std::array<uint8_t, 32> calculate_merkle_root(
    const uint8_t coinbase_tx_hash[32],
    const uint8_t* merkle_branches,  // flat array of N * 32 bytes
    size_t n_merkle_branches);

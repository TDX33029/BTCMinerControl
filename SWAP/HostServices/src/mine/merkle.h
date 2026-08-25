#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

// Compute the merkle root by iteratively pairing the coinbase tx hash
// with each merkle branch through double-SHA256.
//
// result = SHA256d(coinbase_tx_hash || branch[0])
// result = SHA256d(result || branch[1])
// ...
//
// The returned bytes are SHA256d's raw output, which is also the byte order
// used in the block-header merkle_root field.

std::array<uint8_t, 32> calculate_merkle_root(
    const uint8_t coinbase_tx_hash[32],
    const uint8_t* merkle_branches,  // flat array of N * 32 bytes
    size_t n_merkle_branches);

#pragma once
#include "sha256.h"
#include <array>

// Coinbase transaction hash: double-SHA256 of the reconstructed coinbase tx.
//
// coinbase_1 + extranonce + extranonce_2 + coinbase_2  (all hex strings)
// are decoded to binary, concatenated, and double-SHA256'd.

std::array<uint8_t, 32> calculate_coinbase_tx_hash(
    const std::string& coinbase_1,
    const std::string& coinbase_2,
    const std::string& extranonce,
    const std::string& extranonce_2);

// Binary-buffer variant for when extranonce data is already decoded.
std::array<uint8_t, 32> calculate_coinbase_tx_hash_bin(
    const uint8_t* prefix,       size_t prefix_len,
    const uint8_t* extranonce,   size_t extranonce_len,
    const uint8_t* extranonce_2, size_t extranonce_2_len,
    const uint8_t* suffix,       size_t suffix_len);

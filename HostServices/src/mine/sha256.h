#pragma once
#include <cstdint>
#include <array>
#include <string>

// ---------------------------------------------------------------------------
// SHA-256 and mining-specific cryptographic utilities
// Ported from ESP-Miner components/stratum/utils.c and mining.c
// ---------------------------------------------------------------------------

namespace sha256 {

/// SHA-256(SHA-256(data)) — the standard Bitcoin double hash.
std::array<uint8_t, 32> double_sha256(const void* data, size_t len);

/// Compute a SHA-256 *midstate* from exactly 64 bytes of input.
/// The midstate is the 8 internal u32 state words after processing the first
/// 64-byte chunk but before final padding. BM1366 ASICs use this to skip
/// re-hashing the first half of the 80-byte block header for every nonce.
std::array<uint8_t, 32> midstate_sha256(const uint8_t data[64]);

/// Reverse the order of 32-bit words within a 32-byte buffer.
/// word[0] <-> word[7], word[1] <-> word[6], etc.
/// BM1366 expects merkle_root and prev_block_hash in this format.
std::array<uint8_t, 32> reverse_32bit_words(const uint8_t src[32]);

/// Reverse endianness of each 32-bit word within a 32-byte buffer (in-place).
void reverse_endianness_per_word(uint8_t data[32]);

/// Increment only the bits within `mask`, propagating carries.
/// Used for BIP-310 version-rolling.
uint32_t increment_bitmask(uint32_t value, uint32_t mask);

} // namespace sha256

// ---------------------------------------------------------------------------
// Difficulty conversion utilities
// ---------------------------------------------------------------------------

/// True difficulty-1 target as a double.
constexpr double TRUE_DIFF_ONE = 26959535291011309493156476344723991336010898738574164086137773096960.0;

/// Convert a 256-bit little-endian hash to pool difficulty (pdiff).
double hash_to_pdiff(const uint8_t hash[32]);

/// Compute network difficulty from nBits (compact target format).
double network_difficulty(uint32_t nbits);

// ---------------------------------------------------------------------------
// Hex conversion utilities
// ---------------------------------------------------------------------------

std::string bin2hex(const uint8_t* data, size_t len);
std::string bin2hex(const std::array<uint8_t, 32>& data);

/// Convert hex string to fixed-size binary. Returns true on success.
bool hex2bin(const std::string& hex, uint8_t* out, size_t out_len);

/// Generate extranonce_2 hex string: counter as big-endian with `byte_len` bytes.
std::string extranonce_2_generate(uint64_t counter, uint8_t byte_len);

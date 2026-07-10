// SHA-256 and cryptographic utilities
// Ported from ESP-Miner components/stratum/utils.c
//
// Implements raw SHA-256 compression (no external library needed),
// midstate extraction, and endianness conversions for ASIC communication.

#include "sha256.h"
#include <cstring>
#include <sstream>
#include <iomanip>

namespace sha256 {

// SHA-256 round constants
static const uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
    0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
    0x391c0ef3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

// SHA-256 initial hash values
static const uint32_t H0[8] = {
    0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
    0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19,
};

// ---------------------------------------------------------------------------
// Core SHA-256 compression function
// ---------------------------------------------------------------------------

static inline uint32_t rotr(uint32_t x, unsigned n) {
    return (x >> n) | (x << (32 - n));
}

static void sha256_compress(uint32_t state[8], const uint8_t block[64]) {
    uint32_t w[64];

    // Prepare message schedule
    for (int i = 0; i < 16; i++) {
        w[i] = (uint32_t(block[i * 4])     << 24) |
               (uint32_t(block[i * 4 + 1]) << 16) |
               (uint32_t(block[i * 4 + 2]) << 8)  |
               (uint32_t(block[i * 4 + 3]));
    }
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
    uint32_t e = state[4], f = state[5], g = state[6], h = state[7];

    for (int i = 0; i < 64; i++) {
        uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        uint32_t ch = (e & f) ^ (~e & g);
        uint32_t temp1 = h + S1 + ch + K[i] + w[i];
        uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temp2 = S0 + maj;

        h = g; g = f; f = e; e = d + temp1;
        d = c; c = b; b = a; a = temp1 + temp2;
    }

    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
    state[4] += e; state[5] += f; state[6] += g; state[7] += h;
}

// ---------------------------------------------------------------------------
// Full SHA-256 (handles arbitrary-length input with padding)
// ---------------------------------------------------------------------------

static void sha256_full(const uint8_t* data, size_t len, uint8_t out[32]) {
    uint32_t state[8];
    memcpy(state, H0, sizeof(H0));

    uint8_t block[64];
    size_t offset = 0;

    // Process full 64-byte blocks
    while (offset + 64 <= len) {
        sha256_compress(state, data + offset);
        offset += 64;
    }

    // Handle last block with padding
    size_t remaining = len - offset;
    memcpy(block, data + offset, remaining);

    // Append 0x80
    block[remaining] = 0x80;

    // If not enough room for 8-byte length, pad and process
    if (remaining >= 56) {
        memset(block + remaining + 1, 0, 64 - remaining - 1);
        sha256_compress(state, block);
        memset(block, 0, 56);
    } else {
        memset(block + remaining + 1, 0, 56 - remaining - 1);
    }

    // Append 64-bit bit-length in big-endian
    uint64_t bit_len = len * 8;
    for (int i = 0; i < 8; i++) {
        block[56 + i] = uint8_t(bit_len >> (56 - i * 8));
    }
    sha256_compress(state, block);

    // Emit state as big-endian bytes
    for (int i = 0; i < 8; i++) {
        out[i * 4 + 0] = uint8_t(state[i] >> 24);
        out[i * 4 + 1] = uint8_t(state[i] >> 16);
        out[i * 4 + 2] = uint8_t(state[i] >> 8);
        out[i * 4 + 3] = uint8_t(state[i]);
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

std::array<uint8_t, 32> double_sha256(const void* data, size_t len) {
    uint8_t first[32];
    sha256_full(static_cast<const uint8_t*>(data), len, first);
    uint8_t second[32];
    sha256_full(first, 32, second);
    std::array<uint8_t, 32> result;
    memcpy(result.data(), second, 32);
    return result;
}

std::array<uint8_t, 32> midstate_sha256(const uint8_t data[64]) {
    // The midstate is the 8 state words after processing the first 64-byte
    // data block but BEFORE padding. We just run one compression and capture
    // the state.
    uint32_t state[8];
    memcpy(state, H0, sizeof(H0));
    sha256_compress(state, data);

    std::array<uint8_t, 32> out;
    for (int i = 0; i < 8; i++) {
        out[i * 4 + 0] = uint8_t(state[i] >> 24);
        out[i * 4 + 1] = uint8_t(state[i] >> 16);
        out[i * 4 + 2] = uint8_t(state[i] >> 8);
        out[i * 4 + 3] = uint8_t(state[i]);
    }
    return out;
}

std::array<uint8_t, 32> reverse_32bit_words(const uint8_t src[32]) {
    std::array<uint8_t, 32> dst;
    for (int i = 0; i < 8; i++) {
        memcpy(dst.data() + i * 4, src + (7 - i) * 4, 4);
    }
    return dst;
}

void reverse_endianness_per_word(uint8_t data[32]) {
    for (int i = 0; i < 8; i++) {
        uint8_t* w = data + i * 4;
        std::swap(w[0], w[3]);
        std::swap(w[1], w[2]);
    }
}

uint32_t increment_bitmask(uint32_t value, uint32_t mask) {
    uint32_t lsb = (~mask + 1) & mask;
    uint32_t v = value + lsb;
    return (v & ~mask) | (value & mask);
}

} // namespace sha256

// ---------------------------------------------------------------------------
// Difficulty conversion
// ---------------------------------------------------------------------------

static double le256_to_double(const uint8_t target[32]) {
    double result = 0.0;
    for (int i = 0; i < 8; i++) {
        uint32_t word = (uint32_t(target[i * 4 + 3]) << 24) |
                        (uint32_t(target[i * 4 + 2]) << 16) |
                        (uint32_t(target[i * 4 + 1]) << 8)  |
                        (uint32_t(target[i * 4 + 0]));
        if (word != 0) {
            double wordf = double(word);
            // pow(2, -32*(i+1))
            int exp = -32 * (i + 1);
            if (exp >= -1023) {
                result += wordf * pow(2.0, exp);
            } else {
                result += wordf / pow(2.0, -exp);
            }
        }
    }
    return result * TRUE_DIFF_ONE;
}

double hash_to_pdiff(const uint8_t hash[32]) {
    uint8_t hash_le[32];
    for (int i = 0; i < 32; i++) hash_le[i] = hash[31 - i];
    double d = le256_to_double(hash_le);
    if (d == 0.0) return 0.0;
    return TRUE_DIFF_ONE / d;
}

double network_difficulty(uint32_t nbits) {
    uint8_t* b = reinterpret_cast<uint8_t*>(&nbits);
    uint32_t mantissa = (uint32_t(b[1]) << 16) | (uint32_t(b[2]) << 8) | uint32_t(b[3]);
    uint32_t exponent = b[0];
    double target = double(mantissa) * pow(256.0, int(exponent) - 3);
    return TRUE_DIFF_ONE / target;
}

// ---------------------------------------------------------------------------
// Hex conversion
// ---------------------------------------------------------------------------

std::string bin2hex(const uint8_t* data, size_t len) {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (size_t i = 0; i < len; i++) {
        oss << std::setw(2) << (unsigned)(data[i]);
    }
    return oss.str();
}

std::string bin2hex(const std::array<uint8_t, 32>& data) {
    return bin2hex(data.data(), 32);
}

bool hex2bin(const std::string& hex, uint8_t* out, size_t out_len) {
    if (hex.length() != out_len * 2) return false;
    for (size_t i = 0; i < out_len; i++) {
        char hi = hex[i * 2], lo = hex[i * 2 + 1];
        auto val = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        };
        int h = val(hi), l = val(lo);
        if (h < 0 || l < 0) return false;
        out[i] = uint8_t((h << 4) | l);
    }
    return true;
}

std::string extranonce_2_generate(uint64_t counter, uint8_t byte_len) {
    std::string hex;
    hex.reserve(byte_len * 2);
    for (int i = byte_len - 1; i >= 0; i--) {
        uint8_t b = uint8_t(counter >> (i * 8));
        char buf[3];
        snprintf(buf, sizeof(buf), "%02x", b);
        hex += buf;
    }
    return hex;
}

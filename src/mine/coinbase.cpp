#include "coinbase.h"
#include <cstring>
#include <vector>

std::array<uint8_t, 32> calculate_coinbase_tx_hash(
    const std::string& coinbase_1,
    const std::string& coinbase_2,
    const std::string& extranonce,
    const std::string& extranonce_2)
{
    // Decode each hex part
    auto decode = [](const std::string& hex) -> std::vector<uint8_t> {
        std::vector<uint8_t> bin(hex.length() / 2);
        hex2bin(hex, bin.data(), bin.size());
        return bin;
    };

    auto c1 = decode(coinbase_1);
    auto en = decode(extranonce);
    auto e2 = decode(extranonce_2);
    auto c2 = decode(coinbase_2);

    size_t total = c1.size() + en.size() + e2.size() + c2.size();
    std::vector<uint8_t> combined(total);

    size_t off = 0;
    memcpy(combined.data() + off, c1.data(), c1.size()); off += c1.size();
    memcpy(combined.data() + off, en.data(), en.size()); off += en.size();
    memcpy(combined.data() + off, e2.data(), e2.size()); off += e2.size();
    memcpy(combined.data() + off, c2.data(), c2.size());

    return sha256::double_sha256(combined.data(), combined.size());
}

std::array<uint8_t, 32> calculate_coinbase_tx_hash_bin(
    const uint8_t* prefix,       size_t prefix_len,
    const uint8_t* extranonce,   size_t extranonce_len,
    const uint8_t* extranonce_2, size_t extranonce_2_len,
    const uint8_t* suffix,       size_t suffix_len)
{
    size_t total = prefix_len + extranonce_len + extranonce_2_len + suffix_len;
    std::vector<uint8_t> combined(total);

    size_t off = 0;
    memcpy(combined.data() + off, prefix, prefix_len); off += prefix_len;
    memcpy(combined.data() + off, extranonce, extranonce_len); off += extranonce_len;
    memcpy(combined.data() + off, extranonce_2, extranonce_2_len); off += extranonce_2_len;
    memcpy(combined.data() + off, suffix, suffix_len);

    return sha256::double_sha256(combined.data(), combined.size());
}

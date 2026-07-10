#pragma once
#include "job.h"
#include "sha256.h"

// Verify a nonce returned by an ASIC board.
//
// Reconstructs the 80-byte block header from job + nonce + rolled_version,
// performs double-SHA256, and checks whether the result meets pool difficulty.
//
// Block header layout (80 bytes):
//   [0..3]   version          = rolled_version (LE)
//   [4..35]  prev_block_hash  = job.prev_block_hash → reversed back
//   [36..67] merkle_root      = job.merkle_root → reversed back
//   [68..71] ntime            = job.ntime (LE)
//   [72..75] nbits            = job.nbits (LE)
//   [76..79] nonce            = result.nonce (LE)

VerifiedNonce verify_nonce(const MinerJob& job, const NonceResult& result);

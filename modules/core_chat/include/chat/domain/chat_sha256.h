/**
 * @file chat_sha256.h
 * @brief Self-contained, host-buildable SHA-256 (FIPS 180-4) in plain C++.
 *
 * Vendored for the passphrase channel-key feature. Deliberately has NO platform,
 * ESP-IDF, Arduino, or mbedtls dependency so the identical translation unit
 * compiles under MSVC on the host AND (later) inside the ESP-IDF firmware.
 * Only the C++ standard library (<cstddef>, <cstdint>) is used.
 *
 * This is a private helper of the channel-key derivation; it is intentionally
 * namespaced under chat::detail to avoid colliding with any other SHA-256 the
 * firmware may already vendor.
 */

#pragma once

#include <cstddef>
#include <cstdint>

namespace chat
{
namespace detail
{

/// Number of bytes in a SHA-256 digest.
static constexpr std::size_t kSha256DigestLen = 32;

/**
 * @brief Streaming SHA-256 context (FIPS 180-4).
 *
 * Usage: sha256Init(&ctx); sha256Update(&ctx, data, len)...; sha256Final(&ctx, out32).
 * The one-shot helper sha256() wraps all three.
 */
struct Sha256Ctx
{
    uint32_t state[8];   ///< Working hash state (a..h roots).
    uint64_t bit_len;    ///< Total message length in bits.
    uint8_t buffer[64];  ///< Partial-block buffer.
    std::size_t buf_len; ///< Bytes currently held in buffer (0..63).
};

/// Initialize a SHA-256 context to the FIPS 180-4 initial hash value.
void sha256Init(Sha256Ctx* ctx);

/// Absorb @p len bytes from @p data into the running hash.
void sha256Update(Sha256Ctx* ctx, const void* data, std::size_t len);

/// Finalize and write the 32-byte digest to @p out (must be >= 32 bytes).
void sha256Final(Sha256Ctx* ctx, uint8_t out[kSha256DigestLen]);

/// One-shot convenience: digest @p len bytes of @p data into @p out (32 bytes).
void sha256(const void* data, std::size_t len, uint8_t out[kSha256DigestLen]);

} // namespace detail
} // namespace chat

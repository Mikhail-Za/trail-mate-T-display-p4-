/**
 * @file chat_sha256.cpp
 * @brief Self-contained SHA-256 (FIPS 180-4) implementation in plain C++.
 *
 * Vendored for the passphrase channel-key feature. No platform / ESP-IDF /
 * Arduino / mbedtls dependency: only the C++ standard library. The same source
 * compiles under MSVC on the host and (later) in the ESP-IDF firmware.
 *
 * Reference: NIST FIPS PUB 180-4, "Secure Hash Standard".
 */

#include "chat/domain/chat_sha256.h"

#include <cstring>

namespace chat
{
namespace detail
{
namespace
{

// SHA-256 round constants: first 32 bits of the fractional parts of the cube
// roots of the first 64 primes (2..311).
constexpr uint32_t kK[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u,
    0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu,
    0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu,
    0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau, 0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
    0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu,
    0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu,
    0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u, 0x19a4c116u,
    0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u,
    0xc67178f2u};

inline uint32_t rotr(uint32_t x, uint32_t n)
{
    return (x >> n) | (x << (32u - n));
}

void sha256Transform(uint32_t state[8], const uint8_t block[64])
{
    uint32_t w[64];

    // Load the 16 big-endian words of the block.
    for (std::size_t i = 0; i < 16; ++i)
    {
        w[i] = (static_cast<uint32_t>(block[i * 4]) << 24) |
               (static_cast<uint32_t>(block[i * 4 + 1]) << 16) |
               (static_cast<uint32_t>(block[i * 4 + 2]) << 8) |
               (static_cast<uint32_t>(block[i * 4 + 3]));
    }

    // Extend into the remaining 48 words.
    for (std::size_t i = 16; i < 64; ++i)
    {
        const uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        const uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    uint32_t a = state[0];
    uint32_t b = state[1];
    uint32_t c = state[2];
    uint32_t d = state[3];
    uint32_t e = state[4];
    uint32_t f = state[5];
    uint32_t g = state[6];
    uint32_t h = state[7];

    for (std::size_t i = 0; i < 64; ++i)
    {
        const uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        const uint32_t ch = (e & f) ^ ((~e) & g);
        const uint32_t temp1 = h + s1 + ch + kK[i] + w[i];
        const uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        const uint32_t temp2 = s0 + maj;

        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
    state[5] += f;
    state[6] += g;
    state[7] += h;
}

} // namespace

void sha256Init(Sha256Ctx* ctx)
{
    if (ctx == nullptr)
    {
        return;
    }
    // FIPS 180-4 initial hash value: fractional parts of square roots of the
    // first 8 primes (2..19).
    ctx->state[0] = 0x6a09e667u;
    ctx->state[1] = 0xbb67ae85u;
    ctx->state[2] = 0x3c6ef372u;
    ctx->state[3] = 0xa54ff53au;
    ctx->state[4] = 0x510e527fu;
    ctx->state[5] = 0x9b05688cu;
    ctx->state[6] = 0x1f83d9abu;
    ctx->state[7] = 0x5be0cd19u;
    ctx->bit_len = 0;
    ctx->buf_len = 0;
    std::memset(ctx->buffer, 0, sizeof(ctx->buffer));
}

void sha256Update(Sha256Ctx* ctx, const void* data, std::size_t len)
{
    if (ctx == nullptr || (data == nullptr && len != 0))
    {
        return;
    }

    const uint8_t* p = static_cast<const uint8_t*>(data);
    ctx->bit_len += static_cast<uint64_t>(len) * 8u;

    // Fill any partial buffer first.
    if (ctx->buf_len > 0)
    {
        while (len > 0 && ctx->buf_len < 64)
        {
            ctx->buffer[ctx->buf_len++] = *p++;
            --len;
        }
        if (ctx->buf_len == 64)
        {
            sha256Transform(ctx->state, ctx->buffer);
            ctx->buf_len = 0;
        }
    }

    // Process full 64-byte blocks straight from the input.
    while (len >= 64)
    {
        sha256Transform(ctx->state, p);
        p += 64;
        len -= 64;
    }

    // Stash the remainder.
    while (len > 0)
    {
        ctx->buffer[ctx->buf_len++] = *p++;
        --len;
    }
}

void sha256Final(Sha256Ctx* ctx, uint8_t out[kSha256DigestLen])
{
    if (ctx == nullptr || out == nullptr)
    {
        return;
    }

    const uint64_t bit_len = ctx->bit_len;

    // Append the 0x80 padding byte.
    ctx->buffer[ctx->buf_len++] = 0x80;

    // If there's no room for the 8-byte length, flush this block first.
    if (ctx->buf_len > 56)
    {
        while (ctx->buf_len < 64)
        {
            ctx->buffer[ctx->buf_len++] = 0;
        }
        sha256Transform(ctx->state, ctx->buffer);
        ctx->buf_len = 0;
    }

    // Pad with zeros up to the length field.
    while (ctx->buf_len < 56)
    {
        ctx->buffer[ctx->buf_len++] = 0;
    }

    // Append the message length as a big-endian 64-bit integer.
    for (int i = 7; i >= 0; --i)
    {
        ctx->buffer[ctx->buf_len++] = static_cast<uint8_t>((bit_len >> (i * 8)) & 0xffu);
    }
    sha256Transform(ctx->state, ctx->buffer);

    // Emit the digest big-endian.
    for (std::size_t i = 0; i < 8; ++i)
    {
        out[i * 4] = static_cast<uint8_t>((ctx->state[i] >> 24) & 0xffu);
        out[i * 4 + 1] = static_cast<uint8_t>((ctx->state[i] >> 16) & 0xffu);
        out[i * 4 + 2] = static_cast<uint8_t>((ctx->state[i] >> 8) & 0xffu);
        out[i * 4 + 3] = static_cast<uint8_t>(ctx->state[i] & 0xffu);
    }
}

void sha256(const void* data, std::size_t len, uint8_t out[kSha256DigestLen])
{
    Sha256Ctx ctx;
    sha256Init(&ctx);
    sha256Update(&ctx, data, len);
    sha256Final(&ctx, out);
}

} // namespace detail
} // namespace chat

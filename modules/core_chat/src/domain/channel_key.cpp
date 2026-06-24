/**
 * @file channel_key.cpp
 * @brief Implementation of chat::deriveChannelKey (passphrase channel-key).
 *
 * Pure and host-buildable: depends only on the vendored chat::detail SHA-256
 * and the C++ standard library. No platform / ESP-IDF / Arduino / mbedtls.
 */

#include "chat/domain/channel_key.h"

#include "chat/domain/chat_sha256.h"

#include <cstring>

namespace chat
{
namespace
{

/// Length of a NUL-terminated string, or 0 for nullptr.
std::size_t safeLen(const char* s)
{
    return s ? std::strlen(s) : 0;
}

/// Return the value of a single hex digit (0..15), or -1 if @p c is not hex.
int hexDigit(char c)
{
    if (c >= '0' && c <= '9')
    {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f')
    {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F')
    {
        return c - 'A' + 10;
    }
    return -1;
}

/// True iff every character in [text, text+len) is a hex digit.
bool allHex(const char* text, std::size_t len)
{
    for (std::size_t i = 0; i < len; ++i)
    {
        if (hexDigit(text[i]) < 0)
        {
            return false;
        }
    }
    return true;
}

/// Decode @p len hex chars (len even) into @p len/2 raw bytes. Caller guarantees
/// every character is a valid hex digit.
void decodeHex(const char* text, std::size_t len, uint8_t* out)
{
    for (std::size_t i = 0; i < len / 2; ++i)
    {
        const int hi = hexDigit(text[i * 2]);
        const int lo = hexDigit(text[i * 2 + 1]);
        out[i] = static_cast<uint8_t>((hi << 4) | lo);
    }
}

} // namespace

bool deriveChannelKey(const char* text, uint8_t* out, std::size_t out_cap, std::size_t* out_len)
{
    if (out == nullptr || out_len == nullptr)
    {
        return false;
    }

    const std::size_t len = safeLen(text);

    // Case 1: empty or null -> open channel.
    if (len == 0)
    {
        *out_len = 0;
        return true;
    }

    // Case 2: a 32- or 64-char all-hex string -> its 16 / 32 raw bytes.
    if ((len == 32 || len == 64) && allHex(text, len))
    {
        const std::size_t bytes = len / 2; // 16 or 32
        if (out_cap < bytes)
        {
            return false;
        }
        decodeHex(text, len, out);
        *out_len = bytes;
        return true;
    }

    // Case 3: any other non-empty text -> SHA-256(text) -> 32 bytes.
    if (out_cap < detail::kSha256DigestLen)
    {
        return false;
    }
    detail::sha256(text, len, out);
    *out_len = detail::kSha256DigestLen;
    return true;
}

} // namespace chat

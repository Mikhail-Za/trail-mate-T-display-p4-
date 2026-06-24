/**
 * @file channel_key.h
 * @brief Pure, host-testable derivation of a Meshtastic channel key from any
 *        user-typed passphrase.
 *
 * Lets a user type ANY text as a channel key. The mapping is deterministic and
 * has no platform / ESP-IDF / Arduino / mbedtls dependency, so the identical
 * code compiles under MSVC on the host and (later) in the ESP-IDF firmware.
 *
 * Semantics (see deriveChannelKey):
 *   1. empty OR null text        -> *out_len = 0, return true  (open channel)
 *   2. 32 or 64 chars, ALL hex   -> decode to 16 / 32 raw bytes, return true
 *   3. any other non-empty text  -> SHA-256(text) -> 32 bytes, return true
 */

#pragma once

#include <cstddef>
#include <cstdint>

namespace chat
{

/**
 * @brief Derive a Meshtastic channel key from arbitrary user text.
 *
 * @param text     NUL-terminated passphrase, or nullptr. Not modified.
 * @param out      Destination buffer for the derived key bytes.
 * @param out_cap  Capacity of @p out in bytes (must be >= 32 to hold every case).
 * @param out_len  Set to the number of key bytes written (0, 16, or 32).
 * @return true on success (every defined case succeeds); false only on a usage
 *         error such as a null @p out / @p out_len or @p out_cap too small.
 *
 * Cases:
 *   - empty or null @p text  -> *out_len = 0 (open / default channel).
 *   - @p text is exactly 32 or 64 characters and every character is a hex digit
 *     (0-9, a-f, A-F) -> the string is decoded to its 16 or 32 raw bytes.
 *   - any other non-empty @p text -> the 32-byte SHA-256 digest of the bytes of
 *     @p text (excluding the terminating NUL).
 */
bool deriveChannelKey(const char* text, uint8_t* out, std::size_t out_cap, std::size_t* out_len);

} // namespace chat

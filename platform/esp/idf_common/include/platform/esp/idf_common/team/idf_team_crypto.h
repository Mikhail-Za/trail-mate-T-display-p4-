/**
 * @file idf_team_crypto.h
 * @brief ESP-IDF ITeamCrypto over mbedtls (ChaCha20-Poly1305 + SHA-256).
 *
 * The Arduino team build uses rweather/Crypto (ChaChaPoly + SHA256). That
 * library is NOT vendored into the pure ESP-IDF build, so this is the mbedtls
 * equivalent, modelled on the mbedtls path in
 * modules/core_chat/src/infra/meshcore/meshcore_protocol_helpers.cpp.
 *
 * WIRE COMPATIBILITY: the output MUST be byte-identical to the rweather peers so
 * an IDF P4 and an Arduino node can decrypt each other's team frames. Both
 * standards-track primitives:
 *   - deriveKey:  out = SHA256(key || info)[:out_len]   (info hashed as strlen,
 *                 no NUL), matching team_crypto.cpp::sha256Kdf.
 *   - aeadEncrypt: RFC 8439 ChaCha20-Poly1305; output = ciphertext || tag16,
 *                 the 16-byte Poly1305 tag appended after the ciphertext,
 *                 matching team_crypto.cpp::chachaEncrypt.
 * The team protocol always uses a 32-byte key and a 12-byte nonce
 * (kTeamKeySize / kTeamNonceSize), which is exactly what mbedtls_chachapoly
 * requires.
 *
 * Build note: ChaCha20/Poly1305/ChaChaPoly are OFF by default in ESP-IDF's
 * mbedtls Kconfig; the three CONFIG_MBEDTLS_*_C options are enabled in
 * builds/esp_idf/targets/tdisplayp4_tft/sdkconfig.defaults (and the live
 * sdkconfig) so mbedtls_chachapoly_* link.
 */

#pragma once

#include "team/ports/i_team_crypto.h"

namespace platform::esp::idf_common::team_infra
{

class IdfTeamCrypto final : public ::team::ITeamCrypto
{
  public:
    bool deriveKey(const uint8_t* key, size_t key_len,
                   const char* info,
                   uint8_t* out, size_t out_len) override;

    bool aeadEncrypt(const uint8_t* key, size_t key_len,
                     const uint8_t* nonce, size_t nonce_len,
                     const uint8_t* aad, size_t aad_len,
                     const uint8_t* plain, size_t plain_len,
                     std::vector<uint8_t>& out_cipher) override;

    bool aeadDecrypt(const uint8_t* key, size_t key_len,
                     const uint8_t* nonce, size_t nonce_len,
                     const uint8_t* aad, size_t aad_len,
                     const uint8_t* cipher, size_t cipher_len,
                     std::vector<uint8_t>& out_plain) override;

    /**
     * @brief Self-check against an RFC 8439 known-answer vector + a SHA-256 KDF
     *        vector. Logs PASS/FAIL via ESP_LOGx. Returns true if both match,
     *        which proves the mbedtls wire output equals the rweather peers'.
     *        Called once at facade init to fail loud if the crypto config is
     *        wrong rather than silently producing undecryptable frames.
     */
    static bool runSelfTest();
};

} // namespace platform::esp::idf_common::team_infra

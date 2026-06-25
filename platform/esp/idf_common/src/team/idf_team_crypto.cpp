#include "platform/esp/idf_common/team/idf_team_crypto.h"

#include <cstring>

#include "esp_log.h"
#include "mbedtls/chachapoly.h"
#include "mbedtls/sha256.h"

namespace platform::esp::idf_common::team_infra
{
namespace
{

constexpr const char* kTag = "idf-team-crypto";

// Fixed sizes the team protocol uses on the wire (kTeamKeySize / kTeamNonceSize)
// and the Poly1305 tag. mbedtls_chachapoly requires exactly these; the rweather
// peers also use exactly these, so enforcing them keeps the wire byte-identical.
constexpr size_t kChaChaKeySize = 32;
constexpr size_t kChaChaNonceSize = 12;
constexpr size_t kChaChaTagSize = 16;

// out = SHA256(key || info)[:out_len]. Mirrors team_crypto.cpp::sha256Kdf
// byte-for-byte: the info string is hashed as exactly strlen(info) bytes (no NUL
// terminator), and the digest is truncated to out_len (which the team code caps
// at 32). Uses the mbedtls SHA-256 path that meshcore_protocol_helpers.cpp uses.
bool sha256Kdf(const uint8_t* key, size_t key_len, const char* info,
               uint8_t* out, size_t out_len)
{
    if (key == nullptr || info == nullptr || out == nullptr || out_len > 32)
    {
        return false;
    }

    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    bool ok = mbedtls_sha256_starts(&ctx, 0) == 0;
    if (ok && key_len > 0)
    {
        ok = mbedtls_sha256_update(&ctx, key, key_len) == 0;
    }
    if (ok)
    {
        const size_t info_len = std::strlen(info);
        if (info_len > 0)
        {
            ok = mbedtls_sha256_update(
                     &ctx, reinterpret_cast<const uint8_t*>(info), info_len) == 0;
        }
    }
    uint8_t digest[32] = {};
    if (ok)
    {
        ok = mbedtls_sha256_finish(&ctx, digest) == 0;
    }
    mbedtls_sha256_free(&ctx);
    if (!ok)
    {
        return false;
    }
    std::memcpy(out, digest, out_len);
    return true;
}

} // namespace

bool IdfTeamCrypto::deriveKey(const uint8_t* key, size_t key_len,
                              const char* info,
                              uint8_t* out, size_t out_len)
{
    return sha256Kdf(key, key_len, info, out, out_len);
}

bool IdfTeamCrypto::aeadEncrypt(const uint8_t* key, size_t key_len,
                                const uint8_t* nonce, size_t nonce_len,
                                const uint8_t* aad, size_t aad_len,
                                const uint8_t* plain, size_t plain_len,
                                std::vector<uint8_t>& out_cipher)
{
    if (key == nullptr || nonce == nullptr || plain == nullptr ||
        key_len != kChaChaKeySize || nonce_len != kChaChaNonceSize)
    {
        return false;
    }

    mbedtls_chachapoly_context ctx;
    mbedtls_chachapoly_init(&ctx);
    bool ok = mbedtls_chachapoly_setkey(&ctx, key) == 0;
    if (ok)
    {
        // Output layout matches rweather's chachaEncrypt: ciphertext followed by
        // the 16-byte Poly1305 tag (tag appended after the ciphertext).
        out_cipher.assign(plain_len + kChaChaTagSize, 0);
        ok = mbedtls_chachapoly_encrypt_and_tag(
                 &ctx,
                 plain_len,
                 nonce,
                 (aad_len > 0) ? aad : nullptr,
                 aad_len,
                 (plain_len > 0) ? plain : nullptr,
                 (plain_len > 0) ? out_cipher.data() : nullptr,
                 out_cipher.data() + plain_len) == 0;
    }
    mbedtls_chachapoly_free(&ctx);
    if (!ok)
    {
        out_cipher.clear();
        return false;
    }
    return true;
}

bool IdfTeamCrypto::aeadDecrypt(const uint8_t* key, size_t key_len,
                                const uint8_t* nonce, size_t nonce_len,
                                const uint8_t* aad, size_t aad_len,
                                const uint8_t* cipher, size_t cipher_len,
                                std::vector<uint8_t>& out_plain)
{
    if (key == nullptr || nonce == nullptr || cipher == nullptr ||
        key_len != kChaChaKeySize || nonce_len != kChaChaNonceSize ||
        cipher_len < kChaChaTagSize)
    {
        return false;
    }

    const size_t plain_len = cipher_len - kChaChaTagSize;
    const uint8_t* tag = cipher + plain_len; // tag is appended after ciphertext

    mbedtls_chachapoly_context ctx;
    mbedtls_chachapoly_init(&ctx);
    bool ok = mbedtls_chachapoly_setkey(&ctx, key) == 0;
    if (ok)
    {
        out_plain.assign(plain_len, 0);
        ok = mbedtls_chachapoly_auth_decrypt(
                 &ctx,
                 plain_len,
                 nonce,
                 (aad_len > 0) ? aad : nullptr,
                 aad_len,
                 tag,
                 (plain_len > 0) ? cipher : nullptr,
                 (plain_len > 0) ? out_plain.data() : nullptr) == 0;
    }
    mbedtls_chachapoly_free(&ctx);
    if (!ok)
    {
        out_plain.clear();
        return false;
    }
    return true;
}

bool IdfTeamCrypto::runSelfTest()
{
    bool all_ok = true;

    // --- RFC 8439 Section 2.8.2 ChaCha20-Poly1305 AEAD known-answer vector ---
    // Proves the mbedtls AEAD produces the canonical ciphertext+tag, i.e. the
    // exact bytes the rweather peers produce for the same key/nonce/aad/plain.
    static const uint8_t kKey[32] = {
        0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
        0x88, 0x89, 0x8a, 0x8b, 0x8c, 0x8d, 0x8e, 0x8f,
        0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97,
        0x98, 0x99, 0x9a, 0x9b, 0x9c, 0x9d, 0x9e, 0x9f};
    static const uint8_t kNonce[12] = {
        0x07, 0x00, 0x00, 0x00, 0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47};
    static const uint8_t kAad[12] = {
        0x50, 0x51, 0x52, 0x53, 0xc0, 0xc1, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7};
    static const char kPlainText[] =
        "Ladies and Gentlemen of the class of '99: If I could offer you only "
        "one tip for the future, sunscreen would be it.";
    const size_t kPlainLen = sizeof(kPlainText) - 1; // 114 bytes, drop the NUL
    static const uint8_t kExpectedCipher[114] = {
        0xd3, 0x1a, 0x8d, 0x34, 0x64, 0x8e, 0x60, 0xdb, 0x7b, 0x86, 0xaf, 0xbc,
        0x53, 0xef, 0x7e, 0xc2, 0xa4, 0xad, 0xed, 0x51, 0x29, 0x6e, 0x08, 0xfe,
        0xa9, 0xe2, 0xb5, 0xa7, 0x36, 0xee, 0x62, 0xd6, 0x3d, 0xbe, 0xa4, 0x5e,
        0x8c, 0xa9, 0x67, 0x12, 0x82, 0xfa, 0xfb, 0x69, 0xda, 0x92, 0x72, 0x8b,
        0x1a, 0x71, 0xde, 0x0a, 0x9e, 0x06, 0x0b, 0x29, 0x05, 0xd6, 0xa5, 0xb6,
        0x7e, 0xcd, 0x3b, 0x36, 0x92, 0xdd, 0xbd, 0x7f, 0x2d, 0x77, 0x8b, 0x8c,
        0x98, 0x03, 0xae, 0xe3, 0x28, 0x09, 0x1b, 0x58, 0xfa, 0xb3, 0x24, 0xe4,
        0xfa, 0xd6, 0x75, 0x94, 0x55, 0x85, 0x80, 0x8b, 0x48, 0x31, 0xd7, 0xbc,
        0x3f, 0xf4, 0xde, 0xf0, 0x8e, 0x4b, 0x7a, 0x9d, 0xe5, 0x76, 0xd2, 0x65,
        0x86, 0xce, 0xc6, 0x4b, 0x61, 0x16};
    static const uint8_t kExpectedTag[16] = {
        0x1a, 0xe1, 0x0b, 0x59, 0x4f, 0x09, 0xe2, 0x6a,
        0x7e, 0x90, 0x2e, 0xcb, 0xd0, 0x60, 0x06, 0x91};

    IdfTeamCrypto crypto;
    std::vector<uint8_t> out_cipher;
    const bool enc_ok = crypto.aeadEncrypt(kKey, sizeof(kKey),
                                           kNonce, sizeof(kNonce),
                                           kAad, sizeof(kAad),
                                           reinterpret_cast<const uint8_t*>(kPlainText),
                                           kPlainLen, out_cipher);
    bool aead_match =
        enc_ok && out_cipher.size() == (kPlainLen + kChaChaTagSize) &&
        std::memcmp(out_cipher.data(), kExpectedCipher, kPlainLen) == 0 &&
        std::memcmp(out_cipher.data() + kPlainLen, kExpectedTag, kChaChaTagSize) == 0;

    // Round-trip decrypt of the canonical ciphertext must recover the plaintext.
    std::vector<uint8_t> out_plain;
    if (aead_match)
    {
        const bool dec_ok = crypto.aeadDecrypt(kKey, sizeof(kKey),
                                               kNonce, sizeof(kNonce),
                                               kAad, sizeof(kAad),
                                               out_cipher.data(), out_cipher.size(),
                                               out_plain);
        aead_match = dec_ok && out_plain.size() == kPlainLen &&
                     std::memcmp(out_plain.data(), kPlainText, kPlainLen) == 0;
    }
    if (!aead_match)
    {
        ESP_LOGE(kTag, "AEAD known-answer FAILED (enc_ok=%d size=%u) -- team frames will NOT interop",
                 enc_ok ? 1 : 0, static_cast<unsigned>(out_cipher.size()));
        all_ok = false;
    }

    // --- SHA-256 KDF vector: SHA256({00 01 02 03} || "team")[:16] ---
    // Confirms deriveKey hashes key||info and truncates exactly as the rweather
    // sha256Kdf does. The key is NON-NULL on purpose: both this impl and the
    // rweather sha256Kdf guard `if (!key) return false`, and production always
    // passes a real PSK pointer, so a null-key probe would test a rejected path
    // rather than the live one. A non-empty key AND a non-empty info also prove
    // the concatenation order (key bytes first, then info hashed as strlen).
    static const uint8_t kKdfKey[4] = {0x00, 0x01, 0x02, 0x03};
    static const char kKdfInfo[] = "team";
    static const uint8_t kExpectedKdf16[16] = {
        0xec, 0x3b, 0x8f, 0xb3, 0x57, 0x69, 0x2a, 0x01,
        0x5a, 0x6d, 0x17, 0x2b, 0x7a, 0x13, 0xfb, 0xaf};
    uint8_t kdf_out[16] = {};
    const bool kdf_ok =
        crypto.deriveKey(kKdfKey, sizeof(kKdfKey), kKdfInfo, kdf_out, sizeof(kdf_out));
    if (!kdf_ok || std::memcmp(kdf_out, kExpectedKdf16, sizeof(kdf_out)) != 0)
    {
        ESP_LOGE(kTag, "SHA256 KDF known-answer FAILED (ok=%d)", kdf_ok ? 1 : 0);
        all_ok = false;
    }

    if (all_ok)
    {
        ESP_LOGI(kTag, "team crypto self-test PASS (ChaCha20-Poly1305 + SHA256 wire-compatible)");
    }
    return all_ok;
}

} // namespace platform::esp::idf_common::team_infra

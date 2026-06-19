/**
 * @file idf_blob_store_io.cpp
 * @brief ESP-IDF (nvs_flash) implementation of the chat::infra Preferences blob
 *        helpers that MeshCore uses to persist its identity and peer keys.
 *
 * The Arduino implementation (platform/esp/arduino_common/src/chat/internal/
 * blob_store_io.cpp) is built on the Arduino <Preferences.h> shim and pulls in
 * <SPI.h>/<Arduino.h>, so it is excluded from the pure ESP-IDF build. MeshCore
 * (meshcore_identity.cpp + meshcore_adapter.cpp) only calls the four
 * "Preferences" entry points below, so this translation unit provides them
 * directly over the ESP-IDF NVS C API. The behaviour is matched 1:1 to the
 * Arduino version:
 *
 *   - The Arduino Preferences library IS a thin wrapper over the same NVS flash
 *     partition, so ns -> NVS namespace and key -> nvs_get_blob/nvs_set_blob is
 *     the natural mapping (an identity written by the Arduino build would be
 *     readable here and vice-versa).
 *   - Metadata: the version is stored under version_key as a single byte
 *     (Preferences::putUChar/getUChar). crc_key is nullptr at every MeshCore
 *     call site, so the CRC path is a guarded no-op.
 *   - An "empty" save (data == nullptr / len == 0) erases the key, mirroring the
 *     Arduino "writing empty clears" behaviour. Every write path commits.
 *   - A read-only open of a namespace that does not exist yet returns false
 *     (first boot: identity not present -> MeshCore generates + persists it).
 *
 * The SD-card helpers (loadRawBlobFromSd / saveRawBlobToSd) and the namespace
 * clear helpers are intentionally NOT provided here: MeshCore does not call them
 * in the IDF chat scope, and the SD variants depend on the Arduino SD runtime.
 */

#include "platform/esp/arduino_common/src/chat/internal/blob_store_io.h"

#include "esp_err.h"
#include "nvs.h"
#include "nvs_flash.h"

#include <cstring>

namespace chat
{
namespace infra
{
namespace
{

bool isValidName(const char* name)
{
    return name != nullptr && name[0] != '\0';
}

// nvs_flash_init() is already performed once at startup by bsp_runtime.cpp
// (with the no-free-pages/new-version recovery). This is an idempotent guard for
// the rare case the blob store is exercised before bsp init in a unit context;
// nvs_flash_init() returns ESP_OK if NVS is already initialised. It deliberately
// does NOT erase on a corruption error (that is bsp_runtime's responsibility and
// would destroy unrelated namespaces).
void ensureNvsInitialized()
{
    static bool attempted = false;
    if (attempted)
    {
        return;
    }
    attempted = true;
    (void)nvs_flash_init();
}

// Open the namespace; returns true and fills out_handle on success. A missing
// namespace (ESP_ERR_NVS_NOT_FOUND on a read-only open) yields false, matching
// Preferences::begin(ns, readonly) returning false for an absent namespace.
bool openNamespace(const char* ns, bool read_only, nvs_handle_t* out_handle)
{
    if (!isValidName(ns) || out_handle == nullptr)
    {
        return false;
    }
    ensureNvsInitialized();
    const nvs_open_mode_t mode = read_only ? NVS_READONLY : NVS_READWRITE;
    return nvs_open(ns, mode, out_handle) == ESP_OK;
}

// Read a blob into out. Returns:
//   present == true  + true  : blob read into out
//   present == false + true  : key absent (out left empty)
//   false                    : a real read error
bool readBlob(nvs_handle_t handle, const char* key, std::vector<uint8_t>& out, bool& present)
{
    present = false;
    if (!isValidName(key))
    {
        return false;
    }

    size_t required = 0;
    esp_err_t err = nvs_get_blob(handle, key, nullptr, &required);
    if (err == ESP_ERR_NVS_NOT_FOUND)
    {
        return true; // key simply not present yet
    }
    if (err != ESP_OK)
    {
        return false;
    }
    if (required == 0)
    {
        present = true;
        out.clear();
        return true;
    }

    out.resize(required);
    err = nvs_get_blob(handle, key, out.data(), &required);
    if (err != ESP_OK || required != out.size())
    {
        out.clear();
        return false;
    }
    present = true;
    return true;
}

// Read the u8 version stored under version_key, if present.
void readVersion(nvs_handle_t handle, const char* version_key, PreferencesBlobMetadata* meta)
{
    if (meta == nullptr || !isValidName(version_key))
    {
        return;
    }
    uint8_t version = 0;
    if (nvs_get_u8(handle, version_key, &version) == ESP_OK)
    {
        meta->has_version = true;
        meta->version = version;
    }
}

} // namespace

bool loadRawBlobFromPreferences(const char* ns, const char* key, std::vector<uint8_t>& out)
{
    out.clear();
    nvs_handle_t handle = 0;
    if (!openNamespace(ns, true, &handle))
    {
        return false;
    }

    bool present = false;
    const bool ok = readBlob(handle, key, out, present);
    nvs_close(handle);
    // Plain load reports success only when a non-empty blob was actually read,
    // mirroring the Arduino path (getBytesLength == 0 -> return false).
    return ok && present && !out.empty();
}

bool saveRawBlobToPreferences(const char* ns, const char* key, const uint8_t* data, size_t len)
{
    if (!isValidName(ns) || !isValidName(key))
    {
        return false;
    }

    nvs_handle_t handle = 0;
    if (!openNamespace(ns, false, &handle))
    {
        return false;
    }

    esp_err_t err;
    if (data != nullptr && len > 0)
    {
        err = nvs_set_blob(handle, key, data, len);
    }
    else
    {
        // Writing empty clears the key (Arduino Preferences semantics). A missing
        // key is not an error.
        err = nvs_erase_key(handle, key);
        if (err == ESP_ERR_NVS_NOT_FOUND)
        {
            err = ESP_OK;
        }
    }

    bool ok = (err == ESP_OK);
    if (ok)
    {
        ok = (nvs_commit(handle) == ESP_OK);
    }
    nvs_close(handle);
    return ok;
}

bool loadRawBlobFromPreferencesWithMetadata(const char* ns, const char* key,
                                            const char* version_key,
                                            const char* crc_key,
                                            std::vector<uint8_t>& out,
                                            PreferencesBlobMetadata* meta)
{
    (void)crc_key; // always nullptr at MeshCore call sites; CRC path is a no-op.

    out.clear();
    if (meta != nullptr)
    {
        *meta = PreferencesBlobMetadata{};
    }

    nvs_handle_t handle = 0;
    if (!openNamespace(ns, true, &handle))
    {
        return false;
    }

    bool present = false;
    if (!readBlob(handle, key, out, present))
    {
        nvs_close(handle);
        out.clear();
        return false;
    }

    PreferencesBlobMetadata local_meta{};
    local_meta.len = out.size();
    readVersion(handle, version_key, &local_meta);
    nvs_close(handle);

    if (meta != nullptr)
    {
        *meta = local_meta;
    }
    // Matches the Arduino variant: a successful namespace open returns true even
    // when the blob is absent (out empty, meta.len == 0). The caller validates
    // the blob size itself.
    return true;
}

bool saveRawBlobToPreferencesWithMetadata(const char* ns, const char* key,
                                          const char* version_key,
                                          const char* crc_key,
                                          const uint8_t* data, size_t len,
                                          const PreferencesBlobMetadata* meta,
                                          bool retry_after_clear)
{
    (void)crc_key; // always nullptr at MeshCore call sites; CRC path is a no-op.

    if (!isValidName(ns) || !isValidName(key))
    {
        return false;
    }

    nvs_handle_t handle = 0;
    if (!openNamespace(ns, false, &handle))
    {
        return false;
    }

    bool ok = true;
    if (data != nullptr && len > 0)
    {
        esp_err_t err = nvs_set_blob(handle, key, data, len);
        if (err != ESP_OK && retry_after_clear)
        {
            // Mirror the Arduino retry: drop the key, then write again. This
            // recovers from a fragmented/partial entry.
            (void)nvs_erase_key(handle, key);
            err = nvs_set_blob(handle, key, data, len);
        }
        ok = (err == ESP_OK);

        if (ok)
        {
            // Write the version byte (or clear it when no version is supplied).
            if (isValidName(version_key))
            {
                if (meta != nullptr && meta->has_version)
                {
                    ok = (nvs_set_u8(handle, version_key, meta->version) == ESP_OK);
                }
                else
                {
                    esp_err_t verr = nvs_erase_key(handle, version_key);
                    ok = (verr == ESP_OK || verr == ESP_ERR_NVS_NOT_FOUND);
                }
            }
        }
    }
    else
    {
        // Empty save clears both the blob and its version metadata.
        esp_err_t err = nvs_erase_key(handle, key);
        ok = (err == ESP_OK || err == ESP_ERR_NVS_NOT_FOUND);
        if (isValidName(version_key))
        {
            (void)nvs_erase_key(handle, version_key);
        }
    }

    if (ok)
    {
        ok = (nvs_commit(handle) == ESP_OK);
    }
    nvs_close(handle);
    return ok;
}

} // namespace infra
} // namespace chat

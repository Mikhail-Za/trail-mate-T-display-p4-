/**
 * @file idf_nvs_team_ui_snapshot_store.cpp
 * @brief NVS-backed ITeamUiSnapshotStore implementation (see header).
 *
 * Mirrors the NVS idiom in idf_blob_store_io.cpp: nvs_open (READONLY for load,
 * READWRITE for save), the two-call nvs_get_blob (size probe then read) with
 * ESP_ERR_NVS_NOT_FOUND treated as "absent" rather than an error, and
 * nvs_set_blob + nvs_commit on write. nvs_flash_init() is already performed at
 * startup by bsp_runtime.cpp, so this only nvs_open()s.
 */

#include "platform/esp/idf_common/team/idf_nvs_team_ui_snapshot_store.h"

#include "team/domain/team_types.h"     // TeamId (std::array<uint8_t, kTeamIdSize>)
#include "team/protocol/team_mgmt.h"    // kTeamChannelPskSize
#include "team/protocol/team_wire.h"    // kTeamIdSize

#include "esp_err.h"
#include "esp_log.h"
#include "nvs.h"

#include <array>
#include <cstdint>
#include <cstring>

namespace platform::esp::idf_common::team_infra
{
namespace
{

constexpr char kTag[] = "idf-team-nvs";
constexpr char kNamespace[] = "team_keys";
constexpr char kKey[] = "v1";
constexpr uint8_t kBlobVersion = 1U;

// Byte layout (see header). Offsets are explicit so a future field append cannot
// silently shift an existing one.
constexpr size_t kOffVersion = 0U;                                  // 1 byte
constexpr size_t kOffTeamId = 1U;                                   // kTeamIdSize (8)
constexpr size_t kOffKeyId = kOffTeamId + ::team::proto::kTeamIdSize;          // 4 bytes LE
constexpr size_t kOffPsk = kOffKeyId + 4U;                          // kTeamChannelPskSize (16)
constexpr size_t kOffFlags = kOffPsk + ::team::proto::kTeamChannelPskSize;     // 1 byte
constexpr size_t kBlobSize = kOffFlags + 1U;                        // 30 bytes total

constexpr uint8_t kFlagSelfIsLeader = 0x01U;
constexpr uint8_t kFlagInTeam = 0x02U;

bool teamIdIsNonZero(const ::team::TeamId& id)
{
    for (uint8_t b : id)
    {
        if (b != 0U)
        {
            return true;
        }
    }
    return false;
}

// Erase the persisted record (team left / cleared). A missing key is not an
// error. Mirrors the "writing empty clears" path in idf_blob_store_io.cpp.
void eraseRecord()
{
    nvs_handle_t handle = 0;
    if (nvs_open(kNamespace, NVS_READWRITE, &handle) != ESP_OK)
    {
        return;
    }
    esp_err_t err = nvs_erase_key(handle, kKey);
    if (err == ESP_OK)
    {
        (void)nvs_commit(handle);
    }
    nvs_close(handle);
}

} // namespace

bool IdfNvsTeamUiSnapshotStore::load(::team::ui::TeamUiSnapshot& out)
{
    nvs_handle_t handle = 0;
    // A namespace that does not exist yet (first boot, never paired) yields
    // ESP_ERR_NVS_NOT_FOUND here -> treat as absent.
    if (nvs_open(kNamespace, NVS_READONLY, &handle) != ESP_OK)
    {
        return false;
    }

    // Two-call read: probe the size, then read.
    size_t required = 0;
    esp_err_t err = nvs_get_blob(handle, kKey, nullptr, &required);
    if (err == ESP_ERR_NVS_NOT_FOUND)
    {
        nvs_close(handle);
        return false; // never saved
    }
    if (err != ESP_OK || required != kBlobSize)
    {
        // Real read error, or a record from an incompatible layout. Do not
        // partially apply -- report absent so the caller stays keyless.
        nvs_close(handle);
        return false;
    }

    std::array<uint8_t, kBlobSize> blob{};
    err = nvs_get_blob(handle, kKey, blob.data(), &required);
    nvs_close(handle);
    if (err != ESP_OK || required != kBlobSize)
    {
        return false;
    }

    if (blob[kOffVersion] != kBlobVersion)
    {
        return false; // unknown version
    }

    std::memcpy(out.team_id.data(), &blob[kOffTeamId], ::team::proto::kTeamIdSize);
    out.has_team_id = true;

    uint32_t key_id = static_cast<uint32_t>(blob[kOffKeyId]) |
                      (static_cast<uint32_t>(blob[kOffKeyId + 1U]) << 8) |
                      (static_cast<uint32_t>(blob[kOffKeyId + 2U]) << 16) |
                      (static_cast<uint32_t>(blob[kOffKeyId + 3U]) << 24);
    out.security_round = key_id;

    std::memcpy(out.team_psk.data(), &blob[kOffPsk], ::team::proto::kTeamChannelPskSize);
    out.has_team_psk = true;

    const uint8_t flags = blob[kOffFlags];
    out.self_is_leader = (flags & kFlagSelfIsLeader) != 0U;
    out.in_team = (flags & kFlagInTeam) != 0U;

    // members intentionally left empty: the roster rebuilds from live presence.

    // The team page refresh re-loads this every few seconds; log only the first success
    // so the boot-time restore is visible without spamming the console.
    static bool logged_once = false;
    if (!logged_once)
    {
        logged_once = true;
        ESP_LOGI(kTag, "loaded team keys from NVS (key_id=%lu leader=%d in_team=%d)",
                 static_cast<unsigned long>(key_id), out.self_is_leader ? 1 : 0,
                 out.in_team ? 1 : 0);
    }
    return true;
}

void IdfNvsTeamUiSnapshotStore::save(const ::team::ui::TeamUiSnapshot& in)
{
    // Only persist a real team (keys present AND a non-zero team_id). Without a
    // PSK there is nothing to rehydrate; persisting a zero team_id would write a
    // useless record. When keys are absent (team left / cleared) ERASE so a
    // deleted team cannot resurrect on the next boot.
    if (!in.has_team_psk || !teamIdIsNonZero(in.team_id))
    {
        eraseRecord();
        return;
    }

    std::array<uint8_t, kBlobSize> blob{};
    blob[kOffVersion] = kBlobVersion;
    std::memcpy(&blob[kOffTeamId], in.team_id.data(), ::team::proto::kTeamIdSize);

    const uint32_t key_id = in.security_round;
    blob[kOffKeyId] = static_cast<uint8_t>(key_id & 0xFFU);
    blob[kOffKeyId + 1U] = static_cast<uint8_t>((key_id >> 8) & 0xFFU);
    blob[kOffKeyId + 2U] = static_cast<uint8_t>((key_id >> 16) & 0xFFU);
    blob[kOffKeyId + 3U] = static_cast<uint8_t>((key_id >> 24) & 0xFFU);

    std::memcpy(&blob[kOffPsk], in.team_psk.data(), ::team::proto::kTeamChannelPskSize);

    uint8_t flags = 0U;
    if (in.self_is_leader)
    {
        flags |= kFlagSelfIsLeader;
    }
    if (in.in_team)
    {
        flags |= kFlagInTeam;
    }
    blob[kOffFlags] = flags;

    nvs_handle_t handle = 0;
    if (nvs_open(kNamespace, NVS_READWRITE, &handle) != ESP_OK)
    {
        ESP_LOGW(kTag, "nvs_open(team_keys) failed; team keys not persisted");
        return;
    }

    esp_err_t err = nvs_set_blob(handle, kKey, blob.data(), blob.size());
    if (err == ESP_OK)
    {
        err = nvs_commit(handle);
    }
    nvs_close(handle);

    if (err != ESP_OK)
    {
        ESP_LOGW(kTag, "persisting team keys failed (err=%d)", static_cast<int>(err));
    }
}

void IdfNvsTeamUiSnapshotStore::clear()
{
    // Drop the persisted record so the next boot starts keyless. Mirrors
    // TeamUiSnapshotMemoryStore::clear() (which resets its static snapshot).
    eraseRecord();
}

} // namespace platform::esp::idf_common::team_infra

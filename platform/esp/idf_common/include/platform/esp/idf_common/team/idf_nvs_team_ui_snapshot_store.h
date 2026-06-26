/**
 * @file idf_nvs_team_ui_snapshot_store.h
 * @brief NVS-backed ITeamUiSnapshotStore so a paired team's keys survive
 *        reboot / reflash on the pure ESP-IDF build.
 *
 * The default ITeamUiSnapshotStore is TeamUiSnapshotMemoryStore
 * (platform_ui_team_ui_store_runtime.cpp), which holds the snapshot in a static
 * and is wiped on every boot -- so a paired team (its 16-byte channel PSK +
 * key_id + team_id) is lost and the user must re-pair. This store terminates the
 * SAME save/restore seam (team_ui_save_keys_now -> store.save(); the team page's
 * loadOnce/applySnapshot -> store.load()) in NVS flash instead, so the keys are
 * durable.
 *
 * Persisted blob (NVS namespace "team_keys", key "v1"), a single 30-byte record:
 *   byte 0        version (1)
 *   bytes 1..8    team_id (8 bytes, kTeamIdSize)
 *   bytes 9..12   key_id  (uint32 LE, from TeamUiSnapshot::security_round)
 *   bytes 13..28  team_psk (16 bytes, kTeamChannelPskSize)
 *   byte 29       flags: bit0 = self_is_leader, bit1 = in_team
 *
 * save() with has_team_psk == false (team left / cleared) ERASES the key, so a
 * deleted team cannot resurrect on the next boot. The NVS idiom mirrors
 * idf_blob_store_io.cpp; nvs_flash_init() is already done at startup
 * (bsp_runtime.cpp), so this only nvs_open()s.
 */

#pragma once

#include "platform/ui/team_ui_snapshot_store.h"

namespace platform::esp::idf_common::team_infra
{

class IdfNvsTeamUiSnapshotStore final : public ::team::ui::ITeamUiSnapshotStore
{
  public:
    bool load(::team::ui::TeamUiSnapshot& out) override;
    void save(const ::team::ui::TeamUiSnapshot& in) override;
    void clear() override;
};

} // namespace platform::esp::idf_common::team_infra

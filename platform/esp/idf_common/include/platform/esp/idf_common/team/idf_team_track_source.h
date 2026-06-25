/**
 * @file idf_team_track_source.h
 * @brief ESP-IDF ITeamTrackSource backed by the IDF GNSS runtime.
 *
 * IDF analogue of platform/esp/arduino_common/.../team_track_source_gps.h. Reads
 * the latest fix from platform::esp::idf_common::gps_runtime::get_data() and
 * converts lat/lng (degrees, double) to the team protocol's 1e7 fixed-point
 * integers. Degrades gracefully: when there is no valid fix it zeroes the point
 * and returns false, so the track sampler simply skips that sample (no shared
 * position) without affecting chat/pairing.
 */

#pragma once

#include "team/ports/i_team_track_source.h"

namespace platform::esp::idf_common::team_infra
{

class IdfTeamTrackSource final : public ::team::ITeamTrackSource
{
  public:
    bool readTrackPoint(::team::proto::TeamTrackPoint* out_point) override;
};

} // namespace platform::esp::idf_common::team_infra

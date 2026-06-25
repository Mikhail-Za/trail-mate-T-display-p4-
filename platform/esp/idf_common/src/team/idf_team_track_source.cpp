#include "platform/esp/idf_common/team/idf_team_track_source.h"

#include "platform/esp/idf_common/gps_runtime.h"

namespace platform::esp::idf_common::team_infra
{

bool IdfTeamTrackSource::readTrackPoint(::team::proto::TeamTrackPoint* out_point)
{
    if (out_point == nullptr)
    {
        return false;
    }

    const gps::GpsState fix = gps_runtime::get_data();
    if (!fix.valid)
    {
        // No fix: zero the point and report failure so the sampler skips it
        // (mirrors team_track_source_gps.cpp's no-fix path).
        out_point->lat_e7 = 0;
        out_point->lon_e7 = 0;
        return false;
    }

    out_point->lat_e7 = static_cast<int32_t>(fix.lat * 1e7);
    out_point->lon_e7 = static_cast<int32_t>(fix.lng * 1e7);
    return true;
}

} // namespace platform::esp::idf_common::team_infra

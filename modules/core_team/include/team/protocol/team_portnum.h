#pragma once

#include <cstdint>

namespace team::proto
{

constexpr uint32_t TEAM_MGMT_APP = 300;
constexpr uint32_t TEAM_POSITION_APP = 301;
constexpr uint32_t TEAM_WAYPOINT_APP = 302;
constexpr uint32_t TEAM_CHAT_APP = 303;
constexpr uint32_t TEAM_TRACK_APP = 304;
// Pairing handshake (Beacon/Join/Key) frames. Carried over LoRa on the IDF P4
// (the Arduino build bootstraps pairing over ESP-NOW instead and never puts these
// frames on the mesh). Demuxed from steady-state team traffic so the LoRa pairing
// transport receives them while TEAM_MGMT/POSITION/WAYPOINT/CHAT/TRACK keep going
// to TeamService. TeamService treats this portnum as "unhandled app data" and
// forwards it to the pairing transport via setUnhandledAppDataObserver().
constexpr uint32_t TEAM_PAIR_APP = 305;

} // namespace team::proto

/**
 * @file team_runtime_idf.h
 * @brief ESP-IDF ITeamRuntime: sys clock + esp_random RNG.
 *
 * IDF analogue of platform/esp/arduino_common/.../team_runtime_arduino.h. Backs
 * the team services constructed by IdfChatFacade. nowMillis()/nowUnixSeconds()
 * come from the shared sys clock (sys::millis_now / sys::epoch_seconds_now, the
 * same clock the rest of the IDF build uses); fillRandomBytes() pulls from the
 * hardware RNG via esp_random().
 */

#pragma once

#include "team/ports/i_team_runtime.h"

namespace platform::esp::idf_common::team_infra
{

class TeamRuntimeIdf final : public ::team::ITeamRuntime
{
  public:
    uint32_t nowMillis() override;
    uint32_t nowUnixSeconds() override;
    void fillRandomBytes(uint8_t* out, size_t len) override;
};

} // namespace platform::esp::idf_common::team_infra

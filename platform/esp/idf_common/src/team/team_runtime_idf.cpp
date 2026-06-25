#include "platform/esp/idf_common/team/team_runtime_idf.h"

#include "esp_random.h"
#include "sys/clock.h"

namespace platform::esp::idf_common::team_infra
{

uint32_t TeamRuntimeIdf::nowMillis()
{
    return ::sys::millis_now();
}

uint32_t TeamRuntimeIdf::nowUnixSeconds()
{
    // sys::epoch_seconds_now() is the wall-clock epoch when the RTC/GNSS time is
    // valid, else the uptime-derived fallback (same provider the rest of the IDF
    // build reads). The team protocol only needs a monotonic-ish seconds source
    // for status/track timestamps, so this matches the Arduino runtime's intent.
    return ::sys::epoch_seconds_now();
}

void TeamRuntimeIdf::fillRandomBytes(uint8_t* out, size_t len)
{
    if (out == nullptr || len == 0)
    {
        return;
    }

    // esp_random() returns a hardware-RNG 32-bit word. Fill in 4-byte chunks and
    // handle a non-multiple-of-4 tail. (esp_fill_random exists too, but pulling
    // esp_random in a loop keeps the dependency surface minimal and matches the
    // per-byte intent of the Arduino impl.)
    size_t index = 0;
    while (index + 4U <= len)
    {
        const uint32_t word = esp_random();
        out[index + 0U] = static_cast<uint8_t>(word & 0xFFU);
        out[index + 1U] = static_cast<uint8_t>((word >> 8) & 0xFFU);
        out[index + 2U] = static_cast<uint8_t>((word >> 16) & 0xFFU);
        out[index + 3U] = static_cast<uint8_t>((word >> 24) & 0xFFU);
        index += 4U;
    }
    if (index < len)
    {
        uint32_t word = esp_random();
        while (index < len)
        {
            out[index++] = static_cast<uint8_t>(word & 0xFFU);
            word >>= 8;
        }
    }
}

} // namespace platform::esp::idf_common::team_infra

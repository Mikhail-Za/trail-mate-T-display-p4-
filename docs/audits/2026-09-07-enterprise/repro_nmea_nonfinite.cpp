#include "gps/protocol/nmea/nmea_parser.h"

#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>

namespace
{
std::string withChecksum(const char* body)
{
    uint8_t checksum = 0;
    for (const char* p = body; p && *p; ++p)
    {
        checksum ^= static_cast<uint8_t>(*p);
    }
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string out = "$";
    out += body;
    out += "*";
    out.push_back(kHex[(checksum >> 4) & 0x0F]);
    out.push_back(kHex[checksum & 0x0F]);
    out += "\r\n";
    return out;
}
} // namespace

#include <cmath>
#include <cstdio>
int main() {
 gps::nmea::NmeaParser parser;
 const auto wire=withChecksum("GPRMC,123519,A,NAN,N,NAN,E,0.0,0.0,231123,,,A");
 parser.feed(reinterpret_cast<const uint8_t*>(wire.data()),wire.size());
 gps::LocationFix fix{};
 assert(parser.latestFix(fix));
 assert(fix.valid && (!std::isfinite(fix.latitude) || !std::isfinite(fix.longitude)));
 std::puts("CONFIRMED: checksum-valid RMC publishes valid fix with nonfinite coordinates");
}

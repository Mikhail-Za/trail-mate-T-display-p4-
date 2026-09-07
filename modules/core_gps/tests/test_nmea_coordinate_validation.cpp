#include "gps/protocol/nmea/nmea_parser.h"
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

std::string sentence(const std::string& body)
{
    uint8_t sum = 0;
    for (unsigned char c : body) sum ^= c;
    const char* hex = "0123456789ABCDEF";
    return "$" + body + "*" + hex[sum >> 4] + hex[sum & 15] + "\r\n";
}
std::string body(bool gga, const std::string& lat, const std::string& ns,
                 const std::string& lon, const std::string& ew)
{
    return gga ? "GPGGA,123520," + lat + "," + ns + "," + lon + "," + ew + ",1,08,0.9,545.4,M,46.9,M,,"
               : "GPRMC,123519,A," + lat + "," + ns + "," + lon + "," + ew + ",0.0,0.0,231123,,,A";
}
void feed(gps::nmea::NmeaParser& parser, const std::string& text)
{
    auto wire = sentence(text);
    parser.feed(reinterpret_cast<const uint8_t*>(wire.data()), wire.size());
}
int main()
{
    struct Bad
    {
        const char* lat;
        const char* ns;
        const char* lon;
        const char* ew;
    };
    const Bad invalid[] = {
        {"NAN", "N", "01131.000", "E"}, {"4807.038", "N", "INF", "E"}, {"1e300", "N", "01131.000", "E"}, {"4807.038", "N", "1e300", "E"}, {"junk", "N", "01131.000", "E"}, {"4807.038x", "N", "01131.000", "E"}, {"-4807.038", "N", "01131.000", "E"}, {"4860.000", "N", "01131.000", "E"}, {"9100.000", "N", "01131.000", "E"}, {"9000.001", "N", "01131.000", "E"}, {"4807.038", "N", "18000.001", "E"}, {"4807.038", "N", "18100.000", "E"}, {"4807.038", "X", "01131.000", "E"}, {"4807.038", "NN", "01131.000", "E"}, {"4807.038", "E", "01131.000", "N"}, {"", "N", "01131.000", "E"}, {"4807.038", "N", "", "E"}, {"4807.038", "N", "01131.000", ""}, {"+4807.038", "N", "01131.000", "E"}, {" 4807.038", "N", "01131.000", "E"}, {"4807.038 ", "N", "01131.000", "E"}, {"48e2", "N", "01131.000", "E"}};
    int checks = 0;
    for (bool gga : {false, true})
    {
        for (const auto& bad : invalid)
        {
            gps::nmea::NmeaParser parser;
            feed(parser, body(gga, "4807.038", "N", "01131.000", "E"));
            gps::LocationFix before{}, after{};
            assert(parser.latestFix(before) && before.valid);
            const auto revision = parser.fixRevision();
            feed(parser, body(gga, bad.lat, bad.ns, bad.lon, bad.ew));
            assert(parser.fixRevision() == revision);
            assert(parser.latestFix(after) && after.valid);
            assert(after.latitude == before.latitude && after.longitude == before.longitude);
            ++checks;
        }
        for (const auto& coord : {std::string("0000.000"), std::string("9000.000")})
        {
            gps::nmea::NmeaParser parser;
            feed(parser, body(gga, coord, "S", "18000.000", "W"));
            gps::LocationFix fix{};
            assert(parser.latestFix(fix) && fix.valid);
            assert(std::isfinite(fix.latitude) && std::isfinite(fix.longitude));
            assert(fix.longitude == -180.0);
            assert(fix.latitude == (coord == "9000.000" ? -90.0 : 0.0));
            ++checks;
        }
        gps::nmea::NmeaParser parser;
        feed(parser, body(gga, "8959.999", "N", "17959.999", "E"));
        gps::LocationFix fix{};
        assert(parser.latestFix(fix) && fix.valid);
        assert(fix.latitude > 89.99 && fix.latitude < 90.0 && fix.longitude > 179.99 && fix.longitude < 180.0);
        ++checks;
    }
    std::printf("PASS: %d coordinate validation cases across RMC/GGA\n", checks);
}

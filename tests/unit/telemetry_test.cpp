#include "gb/telemetry/telemetry.hpp"

#include <iostream>

int main(const int argc, char **argv) {
  if (argc != 2) {
    std::cerr << "usage: gb_telemetry_tests <fixture>\n";
    return 2;
  }
  try {
    const auto result = gb::telemetry::extract(argv[1]);
    if (result.track_id != 1U || result.gps.size() != 1U || result.utc_anchors.size() != 1U) {
      std::cerr << "unexpected extraction shape\n";
      return 1;
    }
    const auto &point = result.gps.front();
    if (point.media_time_ticks != 0U || point.latitude != 40.1234567L ||
        point.longitude != -3.1234567L || point.altitude != 721.304L ||
        point.speed_2d != 2.184L || point.speed_3d != 2.3L ||
        point.fix != 3U || point.dop != 1.89L ||
        point.validity != gb::telemetry::Validity::valid ||
        point.utc != "2026-01-01T12:00:00.000Z" || !point.utc_recorded) {
      std::cerr << "unexpected GPS decoding\n";
      return 1;
    }
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}

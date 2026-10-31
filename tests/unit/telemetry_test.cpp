#include "gb/telemetry/export.hpp"
#include "gb/telemetry/telemetry.hpp"

#include <cmath>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

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
    const auto bearing = [](const long double latitude, const long double longitude) {
      return gb::telemetry::forward_azimuth_degrees(0.0L, 0.0L, latitude, longitude);
    };
    const auto north = bearing(1.0L, 0.0L);
    const auto east = bearing(0.0L, 1.0L);
    const auto south = bearing(-1.0L, 0.0L);
    const auto west = bearing(0.0L, -1.0L);
    if (!north.has_value() || !east.has_value() || !south.has_value() || !west.has_value() ||
        std::fabs(*north - 0.0L) > 1e-9L || std::fabs(*east - 90.0L) > 1e-9L ||
        std::fabs(*south - 180.0L) > 1e-9L || std::fabs(*west - 270.0L) > 1e-9L ||
        gb::telemetry::forward_azimuth_degrees(0.0L, 0.0L, 0.0L, 0.0L).has_value()) {
      std::cerr << "unexpected forward azimuth\n";
      return 1;
    }
    const auto fix = [](const long double latitude, const long double longitude, const long double speed,
                        const gb::telemetry::Validity validity = gb::telemetry::Validity::valid) {
      gb::telemetry::GpsPoint sample;
      sample.latitude = latitude;
      sample.longitude = longitude;
      sample.speed_2d = speed;
      sample.validity = validity;
      return sample;
    };
    // Eastbound path whose middle record repeats its predecessor; an invalid westward outlier is skipped.
    const std::vector<gb::telemetry::GpsPoint> path{
        fix(0.0L, 0.0L, 5.0L), fix(0.0L, 0.001L, 5.0L), fix(0.0L, 0.001L, 5.0L),
        fix(0.0L, -1.0L, 5.0L, gb::telemetry::Validity::invalid), fix(0.0L, 0.002L, 5.0L)};
    const auto heading = gb::telemetry::motion_azimuth_degrees(path, 2U);
    auto stationary = path;
    stationary[2].speed_2d = 0.0L;
    if (!heading.has_value() || std::fabs(*heading - 90.0L) > 1e-6L ||
        gb::telemetry::motion_azimuth_degrees(stationary, 2U).has_value() ||
        gb::telemetry::motion_azimuth_degrees(path, 3U).has_value() ||
        gb::telemetry::motion_azimuth_degrees(path, path.size()).has_value() ||
        gb::telemetry::motion_azimuth_degrees({fix(0.0L, 0.0L, 5.0L)}, 0U).has_value()) {
      std::cerr << "unexpected motion azimuth\n";
      return 1;
    }
    gb::telemetry::Extraction track;
    for (const auto ticks : {100, 200, 400}) {
      gb::telemetry::GpsPoint sample;
      sample.media_time_ticks = ticks;
      track.gps.push_back(sample);
    }
    const auto nearest = [&track](const std::int64_t ticks) {
      const auto *found = gb::telemetry::nearest_point(track, ticks);
      return found == nullptr ? -1 : found->media_time_ticks;
    };
    if (nearest(0) != 100 || nearest(240) != 200 || nearest(300) != 200 ||
        nearest(301) != 400 || nearest(10000) != 400 ||
        gb::telemetry::nearest_point(gb::telemetry::Extraction{}, 0) != nullptr) {
      std::cerr << "unexpected nearest GPS record policy\n";
      return 1;
    }

    // Chapters export in the given order as one document; each keeps its own media times.
    auto second = result;
    second.gps.front().latitude = 41.5L;
    second.gps.push_back(second.gps.front());
    second.gps.back().validity = gb::telemetry::Validity::invalid;
    const std::vector<gb::telemetry::ExportSource> chapters{{"/media/GS010047.360", result},
                                                            {"/media/GS020047.360", second}};
    const auto render = [](const std::vector<gb::telemetry::ExportSource> &sources,
                           const gb::telemetry::ExportFormat format) {
      std::ostringstream output;
      gb::telemetry::write_export(output, sources, {format, false});
      return output.str();
    };
    const auto count = [](const std::string &text, const std::string &needle) {
      std::size_t found{};
      for (auto at = text.find(needle); at != std::string::npos; at = text.find(needle, at + 1U)) ++found;
      return found;
    };
    const auto ordered = [](const std::string &text, const std::string &before, const std::string &after) {
      const auto first = text.find(before);
      return first != std::string::npos && text.find(after, first) != std::string::npos;
    };
    const auto sequence_gpx = render(chapters, gb::telemetry::ExportFormat::gpx);
    const auto sequence_json = render(chapters, gb::telemetry::ExportFormat::json);
    const auto sequence_csv = render(chapters, gb::telemetry::ExportFormat::csv);
    const auto sequence_geojson = render(chapters, gb::telemetry::ExportFormat::geojson);
    const auto single_gpx = render({chapters.front()}, gb::telemetry::ExportFormat::gpx);
    if (count(sequence_gpx, "<trkseg>") != 2U || count(sequence_gpx, "<trkpt") != 2U ||
        !ordered(sequence_gpx, "lat=\"40.1234567\"", "lat=\"41.5\"") ||
        !ordered(sequence_gpx, "<gb:source>GS010047.360</gb:source>", "<gb:source>GS020047.360</gb:source>") ||
        sequence_gpx.find("<name>GS010047.360 to GS020047.360</name>") == std::string::npos ||
        count(single_gpx, "<trkseg>") != 1U || single_gpx.find("gb:source") != std::string::npos ||
        sequence_json.find("\"schema\": \"gb.telemetry.sequence/v1\"") == std::string::npos ||
        count(sequence_json, "\"schema\": \"gb.telemetry/v1\"") != 2U ||
        !ordered(sequence_json, "GS010047.360", "GS020047.360") ||
        count(sequence_json, "\"value_status\"") != 3U ||
        sequence_csv.rfind("source_file,media_time_ticks,", 0) != 0U ||
        count(sequence_csv, "\nGS020047.360,") != 2U || count(sequence_csv, "\nGS010047.360,") != 1U ||
        count(sequence_geojson, "\"type\":\"Feature\"") != 2U ||
        !ordered(sequence_geojson, "\"source_file\":\"GS010047.360\"", "\"source_file\":\"GS020047.360\"")) {
      std::cerr << "unexpected chapter sequence export\n";
      return 1;
    }
    auto threw = false;
    try {
      std::ostringstream ignored;
      gb::telemetry::write_export(ignored, {}, {});
    } catch (const std::invalid_argument &) {
      threw = true;
    }
    if (!threw || gb::telemetry::parse_export_format("kml").has_value() ||
        gb::telemetry::parse_export_format("geojson") != gb::telemetry::ExportFormat::geojson) {
      std::cerr << "unexpected export argument handling\n";
      return 1;
    }
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}

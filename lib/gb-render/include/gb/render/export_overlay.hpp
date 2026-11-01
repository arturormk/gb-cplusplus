#pragma once

#include "gb/export/export_settings.hpp"
#include "gb/geo/route.hpp"
#include "gb/render/map_painter.hpp"
#include "gb/telemetry/telemetry.hpp"

#include <QImage>
#include <QPainter>
#include <QRectF>
#include <QString>
#include <QTimeZone>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

// The overlays of a video export (docs/plans/export/settings.md): telemetry text and the route
// map, over the picture or on a panel beside it, drawn the same way at every output resolution.
namespace gb::render {

// The GPS records of an export, across chapters, on the recording timeline. Values follow the
// player's rules (ADR-0004, ADR-0009): the recorded record nearest to a frame, with a valid fix
// within one record interval, never interpolated; otherwise unavailable.
class ExportTrack {
public:
  ExportTrack() = default;
  // `points` in recording order, their media_time_ticks already on the recording timeline, which
  // has `seconds_per_tick`. The route the map draws is the part within [range_start, range_end],
  // without implausible jumps and the outliers they isolate (gb::geo::mark_route_jumps).
  ExportTrack(std::vector<gb::telemetry::GpsPoint> points, std::int64_t range_start,
              std::int64_t range_end, double seconds_per_tick);

  [[nodiscard]] const std::vector<gb::telemetry::GpsPoint> &points() const { return points_; }
  [[nodiscard]] bool empty() const { return points_.empty(); }

  // The nearest record, preferring the earlier on ties.
  [[nodiscard]] std::optional<std::size_t> nearest(std::int64_t ticks) const;
  // The nearest record when it has a valid fix within one record interval of `ticks`.
  [[nodiscard]] const gb::telemetry::GpsPoint *fix_at(std::int64_t ticks) const;

  // The route within the range, for MapScene (indices into these, not into points()).
  [[nodiscard]] const std::vector<gb::geo::WorldPoint> &route() const { return route_; }
  [[nodiscard]] const std::vector<gb::geo::RoutePoint> &route_points() const {
    return route_points_;
  }
  [[nodiscard]] bool has_usable_route() const { return usable_in_range_; }
  // Every record with its usable and jump flags, for the automatic map scale's look-ahead.
  [[nodiscard]] const std::vector<gb::geo::RoutePoint> &all_route_points() const { return all_; }
  [[nodiscard]] double seconds_per_tick() const { return seconds_per_tick_; }
  // The route index shown at `ticks` (the nearest record, clamped to the range).
  [[nodiscard]] std::optional<std::size_t> route_index(std::int64_t ticks) const;
  // The usable route position at or before the route index, else the first usable one.
  [[nodiscard]] std::optional<std::size_t> route_anchor(std::size_t index) const;

  // Distance travelled from the start of the range to the record at `ticks`, in metres, or nothing
  // before the first valid fix. Movements under 3 m are not counted, so GPS jitter while standing
  // still does not add up, and neither are jumps or outliers.
  [[nodiscard]] std::optional<double> distance_at(std::int64_t ticks) const;

  // Records whose position the map can show: every usable record within the range, and the
  // records nearest its ends.
  [[nodiscard]] std::vector<std::size_t> range_records() const;

private:
  std::vector<gb::telemetry::GpsPoint> points_;
  std::size_t first_{}; // the range's records: [first_, last_)
  std::size_t last_{};
  std::vector<gb::geo::WorldPoint> route_;
  std::vector<gb::geo::RoutePoint> route_points_;
  std::vector<gb::geo::RoutePoint> all_;
  double seconds_per_tick_{1.0};
  std::vector<std::optional<double>> distance_; // per record in the range
  bool usable_in_range_{};
};

// Text for each field from a fix, in the settings' units and time zone. "—" when unknown.
struct FieldText {
  gb::exporting::Field field{};
  QString label;
  QString value;
  bool small{}; // the value at half size ([field-NAME] small)
};

[[nodiscard]] QTimeZone time_zone_for(const std::string &setting); // invalid when unknown

[[nodiscard]] std::vector<FieldText> field_texts(const gb::exporting::ExportSettings &settings,
                                                 const QTimeZone &zone,
                                                 const gb::telemetry::GpsPoint *fix,
                                                 std::optional<double> distance_metres);

class ExportGraphs; // gb/render/export_graph.hpp

// Everything that changes from frame to frame.
struct OverlayFrame {
  std::vector<FieldText> fields;
  // The map: centre, current record and marker, course. Zoom and scale are set when painting.
  MapScene scene;
  // The ground width across the map for an automatic scale; without it, [map] width.
  std::optional<double> map_width_metres;
  // The graphs, built once for the export; they keep their plots between frames. Without them, no
  // graphs are drawn.
  ExportGraphs *graphs{};
  std::int64_t ticks{};                    // the frame's time on the recording timeline
  std::optional<std::size_t> fix_record{}; // the track record with a valid fix shown, if any
};

struct OverlayResult {
  int missing_tiles{};
};

// The map's square in the frame for the settings and layout, or nothing without a map. In a panel
// it is the panel's map region; over the picture, its anchored square (stacked with the fields at
// the same corner, which needs the fonts, so it is computed from the same layout as painting).
[[nodiscard]] std::optional<QRectF> export_map_area(const gb::exporting::ExportSettings &settings,
                                                    const gb::exporting::FrameLayout &layout);

// Where each shown graph goes in the frame, speed first: over the picture, at its anchor inset by
// its margin, or stacked beyond an earlier graph at the same corner; in a panel, the panel's graph
// regions. Empty for placement none.
[[nodiscard]] std::vector<std::pair<gb::exporting::Graph, QRectF>>
export_graph_areas(const gb::exporting::ExportSettings &settings,
                   const gb::exporting::FrameLayout &layout);

// The map scene with its zoom for the ground width (`width_metres`, else [map] width), at the
// frame's tile scale (height / 1080).
[[nodiscard]] MapScene export_map_scene(const gb::exporting::ExportSettings &settings,
                                        const gb::exporting::FrameLayout &layout,
                                        const MapScene &scene, QSizeF map_size,
                                        std::optional<double> width_metres = std::nullopt);

// The tiles the map draws for one frame.
[[nodiscard]] std::vector<gb::geo::TileKey>
export_map_tiles(const gb::exporting::ExportSettings &settings,
                 const gb::exporting::FrameLayout &layout, const MapScene &scene,
                 std::optional<double> width_metres = std::nullopt);

// Draws the panel (black, with the map and text) or the overlays onto a frame that already holds
// the picture at layout.picture. `attribution` is required wherever tiles are shown.
OverlayResult paint_export_overlays(QPainter &painter,
                                    const gb::exporting::ExportSettings &settings,
                                    const gb::exporting::FrameLayout &layout,
                                    const OverlayFrame &frame, const QString &attribution);

// The direction-of-travel arrow, smoothed over media time with a one-second time constant, as the
// map pane smooths it over wall-clock time. Deterministic for a given frame sequence.
class CourseFilter {
public:
  std::optional<double> update(std::optional<double> course, double seconds);

private:
  std::optional<double> shown_;
  double last_seconds_{};
};

} // namespace gb::render

#pragma once

#include "gb/export/export_settings.hpp"
#include "gb/render/export_overlay.hpp"

#include <QImage>
#include <QPainter>
#include <QRectF>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

// The speed and height graphs of a video export (docs/plans/export/settings.md): the value against
// time over the export range, with a cursor at the frame shown and a dot on the GPS record the
// text and map show. They follow gb-player's graph (apps/gb-player/profile_view.cpp): records
// without a valid fix are left out, the line breaks across them and across steps longer than
// 3 s, and the smoothing is a centred mean within each joined stretch.
namespace gb::render {

class ExportGraph {
public:
  // `track` holds the records on the recording timeline; the graph covers [range_start,
  // range_end] in its ticks. Values are in the settings' units.
  ExportGraph(gb::exporting::Graph kind, const gb::exporting::ExportSettings &settings,
              const ExportTrack &track, std::int64_t range_start, std::int64_t range_end);

  [[nodiscard]] gb::exporting::Graph kind() const { return kind_; }
  // True when at least one record in the range has a valid fix.
  [[nodiscard]] bool has_data() const { return has_data_; }
  // The value axis, in the settings' units.
  [[nodiscard]] double minimum() const { return minimum_; }
  [[nodiscard]] double maximum() const { return maximum_; }

  // Draws the graph in `area` with the cursor at `ticks` and the dot on `record` (an index into
  // the track's points, the record with a valid fix shown at this frame, if any). The plot is
  // drawn once per size and time window and reused; only the cursor is drawn every frame. With a
  // [graph] window shorter than the range, the window follows `ticks`, so the plot is redrawn as
  // it scrolls.
  void paint(QPainter &painter, const QRectF &area, const gb::exporting::FrameLayout &layout,
             std::int64_t ticks, std::optional<std::size_t> record);

private:
  struct Sample {
    double seconds{}; // from the start of the range
    double value{};   // shown value: smoothed or recorded
    bool usable{};
  };

  struct Plot {
    QRectF frame; // the plot area inside the graph, relative to its top left
    double font_px{};
  };

  // The start of the time window shown at `seconds` from the start of the range.
  [[nodiscard]] double window_start(double seconds) const;
  void render(QSize size, const gb::exporting::FrameLayout &layout, double window_start);
  [[nodiscard]] Plot plot_geometry(QSizeF size, const gb::exporting::FrameLayout &layout) const;

  gb::exporting::Graph kind_;
  gb::exporting::GraphSettings settings_;
  gb::exporting::Color label_color_;
  QString title_;
  std::vector<Sample> samples_;
  std::size_t first_record_{}; // samples_[i] is the track's record first_record_ + i
  std::int64_t range_start_{};
  double seconds_per_tick_{1.0};
  double span_seconds_{};   // the range
  double window_seconds_{}; // the time across the graph, at most the range
  double minimum_{};
  double maximum_{};
  double step_{1.0};
  bool has_data_{};
  QImage cache_;
  QSize cache_size_;
  double cache_window_start_{};
};

// The graphs an export shows, speed first.
class ExportGraphs {
public:
  ExportGraphs() = default;
  ExportGraphs(const gb::exporting::ExportSettings &settings, const ExportTrack &track,
               std::int64_t range_start, std::int64_t range_end);

  [[nodiscard]] bool empty() const { return graphs_.empty(); }
  [[nodiscard]] std::vector<ExportGraph> &graphs() { return graphs_; }
  [[nodiscard]] ExportGraph *find(gb::exporting::Graph kind);

private:
  std::vector<ExportGraph> graphs_;
};

} // namespace gb::render

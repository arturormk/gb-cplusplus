#pragma once

#include "gb/export/framing.hpp"
#include "gb/image/photo.hpp"
#include "gb/render/map_painter.hpp"

#include <QImage>
#include <QString>
#include <QTimeZone>

#include <optional>
#include <utility>
#include <vector>

class QPainter;

// The side panel of a still: the route map and the telemetry at the captured frame, drawn into the
// panel part of the output frame. Sizes scale with the frame height (1080 is the reference).
namespace gb::render {

// A label and its value. The value is "—" when it is not known at the captured frame.
using TelemetryLine = std::pair<QString, QString>;

// Date and time (in `zone`, with its UTC offset), latitude, longitude, altitude and speed, from a
// GPS fix. Without a fix every value is "—".
[[nodiscard]] std::vector<TelemetryLine>
telemetry_lines(const std::optional<gb::image::PhotoGps> &fix,
                const QTimeZone &zone = QTimeZone::systemTimeZone());

struct PanelContent {
  bool map{true};
  bool telemetry{true};
  // The map: `scene.scale` is set by paint_panel from the frame height, keeping the zoom.
  MapScene scene;
  QString attribution;
  std::vector<TelemetryLine> lines;
};

struct PanelPaintResult {
  int missing_tiles{};
};

// Fills the panel black and draws its contents. Nothing is drawn without a panel.
PanelPaintResult paint_panel(QPainter &painter, const gb::exporting::FrameLayout &layout,
                             gb::exporting::PanelSide side, const PanelContent &content);

// The tiles the panel's map needs, nearest to its centre first.
[[nodiscard]] std::vector<gb::geo::TileKey> panel_tiles(const gb::exporting::FrameLayout &layout,
                                                        gb::exporting::PanelSide side,
                                                        const PanelContent &content);

// The ground distance across the panel's map, in metres, or nullopt without a map.
[[nodiscard]] std::optional<double> panel_map_width_metres(const gb::exporting::FrameLayout &layout,
                                                           gb::exporting::PanelSide side,
                                                           const PanelContent &content);

// Draws the panel onto a frame already composed with compose_frame.
PanelPaintResult paint_panel(QImage &frame, const gb::exporting::FrameLayout &layout,
                             gb::exporting::PanelSide side, const PanelContent &content);

} // namespace gb::render

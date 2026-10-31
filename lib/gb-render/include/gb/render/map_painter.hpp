#pragma once

#include "gb/geo/mercator.hpp"
#include "gb/geo/route.hpp"
#include "gb/geo/tiles.hpp"

#include <QImage>
#include <QSizeF>
#include <QString>

#include <cstddef>
#include <functional>
#include <optional>
#include <vector>

class QPainter;
class QRectF;

// The route map as gb-player draws it, shared by the live map pane and by stills (BLUEPRINT §9's
// reusable map scene): tiles, the route split into travelled and remaining runs, start and end
// markers, and the current position with its direction of travel.
namespace gb::render {

enum class MapMarker {
  none,  // no drawable position (no fix, loading, no GPS)
  valid, // a valid fix: a filled marker, with the course arrow when there is one
  gap    // inside a reported GPS gap: a hollow ring at the nearest recorded fix
};

struct MapScene {
  gb::geo::WorldPoint center{0.5, 0.5};
  // Fractional zoom: the world is 256 · 2^zoom map pixels wide.
  double zoom{2.0};
  // Target pixels per map pixel. Line widths, markers and tiles all scale with it.
  double scale{1.0};
  int min_zoom{0}; // the provider's tile zoom range
  int max_zoom{19};
  // Same indices as the GPS records; `points` carries the usable flags.
  const std::vector<gb::geo::WorldPoint> *route{};
  const std::vector<gb::geo::RoutePoint> *points{};
  std::optional<std::size_t> record;
  MapMarker marker{MapMarker::none};
  std::optional<double> course; // degrees clockwise from north
  // Whether the route and its start and end are drawn; without it only the current position is.
  bool track{true};
  // A decoded tile, or nullptr when it is not available.
  std::function<const QImage *(gb::geo::TileKey)> tile;
};

// The tile zoom drawn for a scene, within the provider's range.
[[nodiscard]] int map_tile_zoom(const MapScene &scene);

// The tiles covering a map of `size` target pixels, nearest to the centre first.
[[nodiscard]] std::vector<gb::geo::TileKey> map_tiles(QSizeF size, const MapScene &scene);

// Ground distance across a map of `width` target pixels at the centre's latitude, in metres.
[[nodiscard]] double map_ground_width_metres(double width, const MapScene &scene);

// The fractional zoom at which a map of `width` target pixels, drawn at `scale` target pixels per
// map pixel, shows `metres` of ground across at `latitude`: the inverse of
// map_ground_width_metres.
[[nodiscard]] double map_zoom_for_width(double width, double scale, double metres, double latitude);

struct MapPaintResult {
  int missing_tiles{}; // tiles drawn from an ancestor or as a placeholder
  long long marker_record{-1};
};

// Draws the map into (0, 0, size) in the painter's coordinates. Missing tiles are drawn from the
// nearest cached ancestor, scaled up, or as a placeholder.
MapPaintResult paint_map(QPainter &painter, QSizeF size, const MapScene &scene);

// The provider's attribution, in the bottom-right corner of `area`. Required wherever tiles are
// shown. `scale` is target pixels per screen pixel of the map pane's own attribution.
void paint_attribution(QPainter &painter, const QRectF &area, const QString &text, double scale);

} // namespace gb::render

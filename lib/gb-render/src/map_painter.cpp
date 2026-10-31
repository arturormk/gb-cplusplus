#include "gb/render/map_painter.hpp"

#include <QFont>
#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

namespace gb::render {
namespace {

constexpr int kAncestorLevels = 4;
const QColor kPlaceholder(0x22, 0x2d, 0x36);
const QColor kRemaining(0x3b, 0x5b, 0xdb);
const QColor kTravelled(0xf7, 0x67, 0x07);

struct Viewport {
  int tile_zoom{};
  double world_pixels{}; // size of the whole world in map pixels at the scene's zoom
  double tile_pixels{};
  double left{}; // world pixel at the map's left edge
  double top{};
  double width{}; // map pixels
  double height{};
};

Viewport viewport(const QSizeF size, const MapScene &scene) {
  Viewport view;
  view.tile_zoom = map_tile_zoom(scene);
  // One tile spans 256 map pixels at its own zoom regardless of the provider's tile size.
  view.world_pixels = 256.0 * std::pow(2.0, scene.zoom);
  view.tile_pixels = view.world_pixels / std::ldexp(1.0, view.tile_zoom);
  view.width = size.width() / scene.scale;
  view.height = size.height() / scene.scale;
  view.left = scene.center.x * view.world_pixels - view.width / 2.0;
  view.top = scene.center.y * view.world_pixels - view.height / 2.0;
  return view;
}

struct TileRange {
  int count{};
  int first_x{};
  int last_x{};
  int first_y{};
  int last_y{};
};

TileRange tile_range(const Viewport &view) {
  TileRange range;
  range.count = 1 << view.tile_zoom;
  range.first_x = static_cast<int>(std::floor(view.left / view.tile_pixels));
  range.last_x = static_cast<int>(std::floor((view.left + view.width) / view.tile_pixels));
  range.first_y = std::max(0, static_cast<int>(std::floor(view.top / view.tile_pixels)));
  range.last_y = std::min(
      range.count - 1, static_cast<int>(std::floor((view.top + view.height) / view.tile_pixels)));
  return range;
}

} // namespace

int map_tile_zoom(const MapScene &scene) {
  return std::clamp(static_cast<int>(std::lround(scene.zoom)), scene.min_zoom, scene.max_zoom);
}

std::vector<gb::geo::TileKey> map_tiles(const QSizeF size, const MapScene &scene) {
  std::vector<gb::geo::TileKey> keys;
  if (size.width() <= 0.0 || size.height() <= 0.0 || !(scene.scale > 0.0))
    return keys;
  const auto view = viewport(size, scene);
  const auto range = tile_range(view);
  const auto center_x = (view.left + view.width / 2.0) / view.tile_pixels;
  const auto center_y = (view.top + view.height / 2.0) / view.tile_pixels;
  std::vector<std::pair<double, gb::geo::TileKey>> ordered;
  for (int x = range.first_x; x <= range.last_x && x - range.first_x < range.count; ++x) {
    for (int y = range.first_y; y <= range.last_y; ++y) {
      const auto wrapped = ((x % range.count) + range.count) % range.count;
      const auto dx = x + 0.5 - center_x;
      const auto dy = y + 0.5 - center_y;
      ordered.emplace_back(dx * dx + dy * dy, gb::geo::TileKey{view.tile_zoom, wrapped, y});
    }
  }
  std::sort(ordered.begin(), ordered.end(),
            [](const auto &a, const auto &b) { return a.first < b.first; });
  for (const auto &[distance, key] : ordered)
    keys.push_back(key);
  return keys;
}

double map_ground_width_metres(const double width, const MapScene &scene) {
  const auto latitude = gb::geo::unproject(scene.center).latitude;
  const auto world_pixels = 256.0 * std::pow(2.0, scene.zoom);
  return (width / scene.scale) * gb::geo::metres_per_world_unit(latitude) / world_pixels;
}

double map_zoom_for_width(const double width, const double scale, const double metres,
                          const double latitude) {
  return std::log2((width / scale) * gb::geo::metres_per_world_unit(latitude) / (256.0 * metres));
}

MapPaintResult paint_map(QPainter &painter, const QSizeF size, const MapScene &scene) {
  MapPaintResult result;
  if (size.width() <= 0.0 || size.height() <= 0.0 || !(scene.scale > 0.0))
    return result;
  const auto view = viewport(size, scene);
  painter.save();
  painter.setClipRect(QRectF(QPointF(0.0, 0.0), size), Qt::IntersectClip);
  painter.scale(scene.scale, scene.scale);
  painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

  const auto tile = [&scene](const gb::geo::TileKey key) -> const QImage * {
    return scene.tile ? scene.tile(key) : nullptr;
  };
  const auto range = tile_range(view);
  for (int x = range.first_x; x <= range.last_x && x - range.first_x < range.count; ++x) {
    for (int y = range.first_y; y <= range.last_y; ++y) {
      const QRectF target(x * view.tile_pixels - view.left, y * view.tile_pixels - view.top,
                          view.tile_pixels, view.tile_pixels);
      const auto wrapped = ((x % range.count) + range.count) % range.count;
      if (const auto *image = tile({view.tile_zoom, wrapped, y}); image != nullptr) {
        painter.drawImage(target, *image);
        continue;
      }
      ++result.missing_tiles;
      // Placeholder: the nearest cached ancestor, scaled up, until the tile itself arrives.
      bool drawn = false;
      for (int level = 1; level <= kAncestorLevels && view.tile_zoom - level >= 0 && !drawn;
           ++level) {
        const gb::geo::TileKey parent{view.tile_zoom - level, wrapped >> level, y >> level};
        if (const auto *image = tile(parent); image != nullptr) {
          const auto fraction = 1.0 / static_cast<double>(1 << level);
          const auto sx = (wrapped - (parent.x << level)) * fraction * image->width();
          const auto sy = (y - (parent.y << level)) * fraction * image->height();
          painter.drawImage(target, *image,
                            QRectF(sx, sy, image->width() * fraction, image->height() * fraction));
          drawn = true;
        }
      }
      if (!drawn)
        painter.fillRect(target.adjusted(0.5, 0.5, -0.5, -0.5), kPlaceholder);
    }
  }

  if (scene.route == nullptr || scene.points == nullptr || scene.route->empty() ||
      scene.route->size() != scene.points->size()) {
    painter.restore();
    return result;
  }
  const auto &route = *scene.route;
  const auto &points = *scene.points;
  painter.setRenderHint(QPainter::Antialiasing, true);
  const auto screen = [&view](const gb::geo::WorldPoint point) {
    return QPointF(point.x * view.world_pixels - view.left, point.y * view.world_pixels - view.top);
  };
  const auto record = scene.record;
  // Each run of usable records becomes up to two paths, travelled and remaining, that meet at the
  // current record. Points closer than 1.5 map pixels to the previous kept point are skipped.
  QPainterPath travelled;
  QPainterPath remaining;
  const auto add = [&](QPainterPath &path, const std::size_t begin, const std::size_t end) {
    if (end < begin + 2)
      return;
    QPointF previous = screen(route[begin]);
    path.moveTo(previous);
    for (auto index = begin + 1; index < end; ++index) {
      const auto point = screen(route[index]);
      if (index + 1 < end && std::hypot(point.x() - previous.x(), point.y() - previous.y()) < 1.5)
        continue;
      path.lineTo(point);
      previous = point;
    }
  };
  for (const auto &[first, last] : gb::geo::usable_runs(points)) {
    if (!record.has_value() || *record < first) {
      add(remaining, first, last);
    } else if (*record >= last) {
      add(travelled, first, last);
    } else {
      add(travelled, first, *record + 1);
      add(remaining, *record, last);
    }
  }
  const auto stroke = [&painter](const QPainterPath &path, const QColor &color) {
    painter.strokePath(
        path, QPen(QColor(255, 255, 255, 220), 6.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.strokePath(path, QPen(color, 3.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
  };
  if (scene.track) {
    stroke(remaining, kRemaining);
    stroke(travelled, kTravelled);
  }

  // Start and end of the recorded route. A single position (a photo) has neither.
  const auto usable = [&points](const std::size_t index) { return points[index].usable; };
  std::optional<std::size_t> first_usable;
  std::optional<std::size_t> last_usable;
  for (std::size_t index = 0; index < points.size(); ++index) {
    if (usable(index)) {
      if (!first_usable)
        first_usable = index;
      last_usable = index;
    }
  }
  if (scene.track && route.size() > 1U && first_usable) {
    painter.setPen(QPen(Qt::white, 2.0));
    painter.setBrush(QColor(0x2f, 0x9e, 0x44));
    painter.drawEllipse(screen(route[*first_usable]), 5.5, 5.5);
    painter.setBrush(QColor(0xc9, 0x2a, 0x2a));
    const auto end_point = screen(route[*last_usable]);
    painter.drawRect(QRectF(end_point.x() - 5.0, end_point.y() - 5.0, 10.0, 10.0));
  }

  if (record.has_value() && *record < route.size() && usable(*record)) {
    const auto position = screen(route[*record]);
    if (scene.marker == MapMarker::valid) {
      if (scene.course.has_value()) {
        // Direction of travel from neighbouring fixes, not the camera's pointing direction.
        painter.save();
        painter.translate(position);
        painter.rotate(*scene.course);
        QPainterPath arrow;
        arrow.moveTo(0.0, -19.0);
        arrow.lineTo(7.0, -8.0);
        arrow.lineTo(-7.0, -8.0);
        arrow.closeSubpath();
        painter.setPen(QPen(Qt::white, 1.5));
        painter.setBrush(kTravelled.darker(130));
        painter.drawPath(arrow);
        painter.restore();
      }
      painter.setPen(QPen(Qt::white, 3.0));
      painter.setBrush(kTravelled);
      painter.drawEllipse(position, 8.0, 8.0);
      result.marker_record = static_cast<long long>(*record);
    } else if (scene.marker == MapMarker::gap) {
      // Inside a GPS gap: a hollow ring at the nearest recorded fix, not an estimated position.
      painter.setBrush(Qt::NoBrush);
      painter.setPen(QPen(QColor(255, 255, 255, 230), 5.0));
      painter.drawEllipse(position, 9.0, 9.0);
      painter.setPen(QPen(QColor(0x49, 0x50, 0x57), 2.5, Qt::DashLine));
      painter.drawEllipse(position, 9.0, 9.0);
      result.marker_record = static_cast<long long>(*record);
    }
  }
  painter.restore();
  return result;
}

void paint_attribution(QPainter &painter, const QRectF &area, const QString &text,
                       const double scale) {
  if (text.isEmpty() || area.isEmpty())
    return;
  painter.save();
  QFont font = painter.font();
  font.setPixelSize(std::max(6, static_cast<int>(std::lround(11.0 * scale))));
  painter.setFont(font);
  const QFontMetricsF metrics(font);
  const auto padding = 4.0 * scale;
  const auto available = area.width() - 2.0 * padding;
  const auto shown = metrics.elidedText(text, Qt::ElideMiddle, available);
  const auto width = metrics.horizontalAdvance(shown) + 2.0 * padding;
  const auto height = metrics.height() + padding;
  const QRectF box(area.right() - width, area.bottom() - height, width, height);
  painter.fillRect(box, QColor(255, 255, 255, 200));
  painter.setPen(QColor(0x33, 0x33, 0x33));
  painter.drawText(box, Qt::AlignCenter, shown);
  painter.restore();
}

} // namespace gb::render

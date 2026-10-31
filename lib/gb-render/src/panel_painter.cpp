#include "gb/render/panel_painter.hpp"

#include "gb/image/exif_writer.hpp"

#include <QDateTime>
#include <QFont>
#include <QFontMetricsF>
#include <QPainter>

#include <algorithm>
#include <cmath>

namespace gb::render {
namespace {

namespace ex = gb::exporting;

const QString kUnknown = QStringLiteral("—");
const QColor kLabel(0xa0, 0xa8, 0xb0);
const QColor kValue(0xf1, 0xf3, 0xf5);
const QColor kMapBackground(0x22, 0x2d, 0x36);

double unit(const ex::FrameLayout &layout) { return layout.height / 1080.0; }

QRectF toQRect(const ex::PixelRect &rect) {
  return {static_cast<double>(rect.x), static_cast<double>(rect.y), static_cast<double>(rect.width),
          static_cast<double>(rect.height)};
}

ex::PanelRegions regions(const ex::FrameLayout &layout, const ex::PanelSide side,
                         const PanelContent &content) {
  return ex::panel_regions(layout, side, content.map, content.telemetry && !content.lines.empty());
}

MapScene scaled(const MapScene &scene, const ex::FrameLayout &layout) {
  auto copy = scene;
  copy.scale = unit(layout);
  return copy;
}

QString degrees(const double value, const char positive, const char negative) {
  return QStringLiteral("%1° %2")
      .arg(std::abs(value), 0, 'f', 5)
      .arg(QChar(value < 0.0 ? negative : positive));
}

void paint_text(QPainter &painter, const QRectF &area, const std::vector<TelemetryLine> &lines,
                const double u) {
  if (lines.empty() || area.isEmpty())
    return;
  // Two columns when the area is much wider than tall (a bottom panel).
  const int columns = area.width() > 1.6 * area.height() && lines.size() > 1U ? 2 : 1;
  const auto count = static_cast<int>(lines.size());
  const auto rows = (count + columns - 1) / columns;
  const auto gap = 24.0 * u;
  const auto column_width = (area.width() - (columns - 1) * gap) / columns;

  QFont label_font = painter.font();
  QFont value_font = painter.font();
  value_font.setWeight(QFont::DemiBold);
  double row_height{};
  // Shrink from the reference sizes until every row and every value fits.
  for (double factor = 1.0;; factor *= 0.92) {
    label_font.setPixelSize(std::max(4, static_cast<int>(std::lround(20.0 * u * factor))));
    value_font.setPixelSize(std::max(5, static_cast<int>(std::lround(32.0 * u * factor))));
    const QFontMetricsF label_metrics(label_font);
    const QFontMetricsF value_metrics(value_font);
    row_height = label_metrics.height() + value_metrics.height() + 12.0 * u * factor;
    bool fits = rows * row_height <= area.height();
    for (const auto &[label, value] : lines) {
      fits = fits && value_metrics.horizontalAdvance(value) <= column_width &&
             label_metrics.horizontalAdvance(label) <= column_width;
    }
    if (fits || factor < 0.2)
      break;
  }
  const QFontMetricsF label_metrics(label_font);
  const QFontMetricsF value_metrics(value_font);
  painter.save();
  painter.setClipRect(area, Qt::IntersectClip);
  for (std::size_t index = 0; index < lines.size(); ++index) {
    const auto column = static_cast<int>(index) / rows;
    const auto row = static_cast<int>(index) % rows;
    const auto x = area.x() + column * (column_width + gap);
    const auto y = area.y() + row * row_height;
    painter.setFont(label_font);
    painter.setPen(kLabel);
    painter.drawText(QPointF(x, y + label_metrics.ascent()),
                     label_metrics.elidedText(lines[index].first, Qt::ElideRight, column_width));
    painter.setFont(value_font);
    painter.setPen(kValue);
    painter.drawText(QPointF(x, y + label_metrics.height() + value_metrics.ascent()),
                     value_metrics.elidedText(lines[index].second, Qt::ElideRight, column_width));
  }
  painter.restore();
}

} // namespace

std::vector<TelemetryLine> telemetry_lines(const std::optional<gb::image::PhotoGps> &fix,
                                           const QTimeZone &zone) {
  QString time = kUnknown;
  QString latitude = kUnknown;
  QString longitude = kUnknown;
  QString altitude = kUnknown;
  QString speed = kUnknown;
  if (fix) {
    if (fix->utc) {
      if (const auto utc = gb::image::parse_utc(*fix->utc); utc) {
        const QDateTime moment(
            QDate(utc->year, utc->month, utc->day),
            QTime(utc->hour, utc->minute, static_cast<int>(std::floor(utc->second))), Qt::UTC);
        const auto local = moment.toTimeZone(zone);
        const auto offset = local.offsetFromUtc();
        const auto minutes = std::abs(offset) / 60;
        time = local.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")) +
               QStringLiteral(" %1%2:%3")
                   .arg(offset < 0 ? QLatin1Char('-') : QLatin1Char('+'))
                   .arg(minutes / 60, 2, 10, QLatin1Char('0'))
                   .arg(minutes % 60, 2, 10, QLatin1Char('0'));
      }
    }
    latitude = degrees(fix->latitude, 'N', 'S');
    longitude = degrees(fix->longitude, 'E', 'W');
    if (fix->altitude)
      altitude = QStringLiteral("%1 m").arg(*fix->altitude, 0, 'f', 0);
    if (fix->speed_mps)
      speed = QStringLiteral("%1 km/h").arg(*fix->speed_mps * 3.6, 0, 'f', 1);
  }
  return {{QStringLiteral("Date and time"), time},
          {QStringLiteral("Latitude"), latitude},
          {QStringLiteral("Longitude"), longitude},
          {QStringLiteral("Altitude"), altitude},
          {QStringLiteral("Speed"), speed}};
}

PanelPaintResult paint_panel(QPainter &painter, const ex::FrameLayout &layout,
                             const ex::PanelSide side, const PanelContent &content) {
  PanelPaintResult result;
  if (!layout.panel || side == ex::PanelSide::none)
    return result;
  const auto u = unit(layout);
  painter.save();
  painter.fillRect(toQRect(*layout.panel), Qt::black);
  const auto parts = regions(layout, side, content);
  if (parts.map) {
    const auto area = toQRect(*parts.map);
    const auto scene = scaled(content.scene, layout);
    painter.save();
    painter.translate(area.topLeft());
    painter.fillRect(QRectF(QPointF(), area.size()), kMapBackground);
    result.missing_tiles = paint_map(painter, area.size(), scene).missing_tiles;
    const auto has_route = scene.route != nullptr && !scene.route->empty();
    if (!has_route) {
      QFont font = painter.font();
      font.setPixelSize(std::max(6, static_cast<int>(std::lround(24.0 * u))));
      painter.setFont(font);
      painter.setPen(kValue);
      painter.drawText(QRectF(QPointF(), area.size()), Qt::AlignCenter,
                       QStringLiteral("No GPS track"));
    }
    painter.restore();
    paint_attribution(painter, area, content.attribution, u);
  }
  if (parts.text)
    paint_text(painter, toQRect(*parts.text), content.lines, u);
  painter.restore();
  return result;
}

PanelPaintResult paint_panel(QImage &frame, const ex::FrameLayout &layout, const ex::PanelSide side,
                             const PanelContent &content) {
  QPainter painter(&frame);
  painter.setRenderHint(QPainter::TextAntialiasing, true);
  return paint_panel(painter, layout, side, content);
}

std::vector<gb::geo::TileKey> panel_tiles(const ex::FrameLayout &layout, const ex::PanelSide side,
                                          const PanelContent &content) {
  const auto parts = regions(layout, side, content);
  if (!parts.map)
    return {};
  return map_tiles(toQRect(*parts.map).size(), scaled(content.scene, layout));
}

std::optional<double> panel_map_width_metres(const ex::FrameLayout &layout,
                                             const ex::PanelSide side,
                                             const PanelContent &content) {
  const auto parts = regions(layout, side, content);
  if (!parts.map)
    return std::nullopt;
  return map_ground_width_metres(parts.map->width, scaled(content.scene, layout));
}

} // namespace gb::render

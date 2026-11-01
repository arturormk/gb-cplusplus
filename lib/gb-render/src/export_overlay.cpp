#include "gb/render/export_overlay.hpp"

#include "gb/render/export_graph.hpp"

#include "gb/geo/mercator.hpp"
#include "gb/image/exif_writer.hpp"

#include <QDateTime>
#include <QFont>
#include <QFontMetricsF>
#include <QPainterPath>

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>

namespace gb::render {
namespace {

namespace ex = gb::exporting;
namespace tm = gb::telemetry;

const QString kUnknown = QStringLiteral("—");
const QColor kMapBackground(0x22, 0x2d, 0x36);
// Movements below this are GPS jitter for the distance travelled.
constexpr double kDistanceStepMetres = 3.0;

QColor qcolor(const ex::Color color) { return {color.red, color.green, color.blue, color.alpha}; }

QRectF qrect(const ex::PixelRect &rect) {
  return {static_cast<double>(rect.x), static_cast<double>(rect.y), static_cast<double>(rect.width),
          static_cast<double>(rect.height)};
}

double unit(const ex::FrameLayout &layout) { return layout.height / 1080.0; }

// ---- text ---------------------------------------------------------------------------------------

struct Fonts {
  QFont label;
  QFont value;
  QFont small; // a small value
  double label_px{};
  double value_px{};
  double small_px{};
};

Fonts fonts_for(const double value_px, const ex::TextSettings &text) {
  Fonts fonts;
  fonts.value_px = std::max(4.0, value_px);
  fonts.label_px = std::max(4.0, value_px * text.label_size);
  fonts.small_px = std::max(4.0, value_px * ex::kSmallValue);
  fonts.value.setPixelSize(static_cast<int>(std::lround(fonts.value_px)));
  fonts.value.setWeight(QFont::DemiBold);
  fonts.small = fonts.value;
  fonts.small.setPixelSize(static_cast<int>(std::lround(fonts.small_px)));
  fonts.label.setPixelSize(static_cast<int>(std::lround(fonts.label_px)));
  return fonts;
}

const QFont &value_font(const FieldText &field, const Fonts &fonts) {
  return field.small ? fonts.small : fonts.value;
}

double value_pixels(const FieldText &field, const Fonts &fonts) {
  return field.small ? fonts.small_px : fonts.value_px;
}

bool has_label(const FieldText &field, const ex::TextSettings &text) {
  return text.labels != ex::LabelPosition::none && !field.label.isEmpty();
}

double label_gap(const Fonts &fonts) { return 0.35 * fonts.value_px; }

QSizeF measure(const FieldText &field, const Fonts &fonts, const ex::TextSettings &text) {
  const QFontMetricsF value(value_font(field, fonts));
  const auto value_width = value.horizontalAdvance(field.value);
  if (!has_label(field, text))
    return {value_width, value.height()};
  const QFontMetricsF label(fonts.label);
  const auto label_width = label.horizontalAdvance(field.label);
  if (text.labels == ex::LabelPosition::left)
    return {label_width + label_gap(fonts) + value_width, std::max(value.height(), label.height())};
  return {std::max(label_width, value_width), label.height() + value.height()};
}

void draw_text(QPainter &painter, const QPointF baseline, const QString &text, const QFont &font,
               const double pixels, const QColor &color, const ex::TextSettings &style) {
  if (text.isEmpty())
    return;
  if (!style.outline || style.outline_width <= 0.0) {
    painter.setFont(font);
    painter.setPen(color);
    painter.drawText(baseline, text);
    return;
  }
  QPainterPath path;
  path.addText(baseline, font, text);
  painter.strokePath(path, QPen(qcolor(style.outline_color), 2.0 * style.outline_width * pixels,
                                Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
  painter.fillPath(path, color);
}

// Draws a field in a block of `width` starting at `top_left`, each line aligned within it.
void draw_field_directly(QPainter &painter, const QPointF top_left, const double width,
                         const ex::Align align, const FieldText &field, const Fonts &fonts,
                         const ex::TextSettings &style) {
  const auto &font = value_font(field, fonts);
  const auto value_px = value_pixels(field, fonts);
  const QFontMetricsF value_metrics(font);
  const QFontMetricsF label_metrics(fonts.label);
  const auto x_for = [&](const double line_width) {
    switch (align) {
    case ex::Align::right:
      return top_left.x() + width - line_width;
    case ex::Align::center:
      return top_left.x() + (width - line_width) / 2.0;
    case ex::Align::left:
    case ex::Align::automatic:
      break;
    }
    return top_left.x();
  };
  const auto label_color = qcolor(style.label_color);
  const auto value_color = qcolor(style.value_color);
  const auto value_width = value_metrics.horizontalAdvance(field.value);
  if (!has_label(field, style)) {
    draw_text(painter, QPointF(x_for(value_width), top_left.y() + value_metrics.ascent()),
              field.value, font, value_px, value_color, style);
    return;
  }
  const auto label_width = label_metrics.horizontalAdvance(field.label);
  if (style.labels == ex::LabelPosition::left) {
    const auto line_height = std::max(value_metrics.height(), label_metrics.height());
    const auto baseline =
        top_left.y() + line_height - std::max(value_metrics.descent(), label_metrics.descent());
    const auto x = x_for(label_width + label_gap(fonts) + value_width);
    draw_text(painter, QPointF(x, baseline), field.label, fonts.label, fonts.label_px, label_color,
              style);
    draw_text(painter, QPointF(x + label_width + label_gap(fonts), baseline), field.value, font,
              value_px, value_color, style);
    return;
  }
  draw_text(painter, QPointF(x_for(label_width), top_left.y() + label_metrics.ascent()),
            field.label, fonts.label, fonts.label_px, label_color, style);
  draw_text(
      painter,
      QPointF(x_for(value_width), top_left.y() + label_metrics.height() + value_metrics.ascent()),
      field.value, font, value_px, value_color, style);
}

// The same, through a cache of drawn fields: values change a few times a second at most, while
// stroking outlined text for every frame is slow. The cached picture is placed at whole pixels.
void draw_field(QPainter &painter, const QPointF top_left, const double width,
                const ex::Align align, const FieldText &field, const Fonts &fonts,
                const ex::TextSettings &style) {
  static thread_local std::map<QString, QImage> cache;
  const auto pad = static_cast<int>(std::ceil(2.0 * style.outline_width * fonts.value_px)) + 2;
  const auto size = measure(field, fonts, style);
  const QSize image_size(static_cast<int>(std::ceil(width)) + 2 * pad,
                         static_cast<int>(std::ceil(size.height())) + 2 * pad);
  if (image_size.isEmpty() || image_size.width() > 8192 || image_size.height() > 8192) {
    draw_field_directly(painter, top_left, width, align, field, fonts, style);
    return;
  }
  const auto key =
      QStringLiteral("%1|%2|%3|%4|%5|%6|%7|%8|%9")
          .arg(field.label, (field.small ? QStringLiteral("s|") : QString()) + field.value)
          .arg(fonts.value_px)
          .arg(fonts.label_px)
          .arg(width)
          .arg(static_cast<int>(align))
          .arg(QString::fromStdString(ex::color_text(style.label_color) +
                                      ex::color_text(style.value_color) +
                                      ex::color_text(style.outline_color)))
          .arg(style.outline ? style.outline_width : -1.0)
          .arg(static_cast<int>(style.labels));
  auto found = cache.find(key);
  if (found == cache.end()) {
    if (cache.size() >= 512U)
      cache.clear();
    QImage image(image_size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    {
      QPainter text(&image);
      text.setRenderHint(QPainter::Antialiasing, true);
      text.setRenderHint(QPainter::TextAntialiasing, true);
      draw_field_directly(text, QPointF(pad, pad), width, align, field, fonts, style);
    }
    found = cache.emplace(key, std::move(image)).first;
  }
  painter.drawImage(QPoint(static_cast<int>(std::lround(top_left.x())) - pad,
                           static_cast<int>(std::lround(top_left.y())) - pad),
                    found->second);
}

// ---- map ----------------------------------------------------------------------------------------

// Draws the attribution inside a circle, where its bottom chord is wide enough for it.
void paint_round_attribution(QPainter &painter, const QSizeF size, const QString &text,
                             const double scale) {
  if (text.isEmpty())
    return;
  painter.save();
  QFont font = painter.font();
  font.setPixelSize(std::max(6, static_cast<int>(std::lround(11.0 * scale))));
  painter.setFont(font);
  const QFontMetricsF metrics(font);
  const auto padding = 4.0 * scale;
  const auto radius = size.width() / 2.0;
  const auto height = metrics.height() + padding;
  const auto wanted = metrics.horizontalAdvance(text) + 2.0 * padding;
  // The lowest box whose top corners are inside the circle, no higher than its centre.
  auto bottom = size.height() - 2.0 * scale;
  for (; bottom > radius + height; bottom -= scale) {
    const auto from_centre =
        std::max(std::abs(bottom - radius), std::abs(bottom - height - radius));
    const auto chord = 2.0 * std::sqrt(std::max(0.0, radius * radius - from_centre * from_centre));
    if (chord >= wanted)
      break;
  }
  const auto from_centre = std::abs(bottom - height - radius);
  const auto chord = 2.0 * std::sqrt(std::max(0.0, radius * radius - from_centre * from_centre));
  const auto shown =
      metrics.elidedText(text, Qt::ElideMiddle, std::max(0.0, chord - 2.0 * padding));
  const auto width = metrics.horizontalAdvance(shown) + 2.0 * padding;
  const QRectF box(radius - width / 2.0, bottom - height, width, height);
  painter.fillRect(box, QColor(255, 255, 255, 200));
  painter.setPen(QColor(0x33, 0x33, 0x33));
  painter.drawText(box, Qt::AlignCenter, shown);
  painter.restore();
}

int paint_map_box(QPainter &painter, const QRectF &area, const ex::ExportSettings &settings,
                  const ex::FrameLayout &layout, const MapScene &base,
                  const std::optional<double> width_metres, const QString &attribution,
                  const double opacity) {
  const QSize size(static_cast<int>(std::lround(area.width())),
                   static_cast<int>(std::lround(area.height())));
  if (size.isEmpty())
    return 0;
  const auto u = unit(layout);
  const auto circle = settings.map.shape == ex::MapShape::circle;
  QImage image(size, QImage::Format_ARGB32_Premultiplied);
  image.fill(kMapBackground);
  int missing = 0;
  {
    QPainter map(&image);
    map.setRenderHint(QPainter::Antialiasing, true);
    map.setRenderHint(QPainter::TextAntialiasing, true);
    const auto scene = export_map_scene(settings, layout, base, QSizeF(size), width_metres);
    const auto has_route = scene.route != nullptr && !scene.route->empty();
    if (has_route)
      missing = paint_map(map, QSizeF(size), scene).missing_tiles;
    if (!has_route) {
      QFont font = map.font();
      font.setPixelSize(std::max(6, static_cast<int>(std::lround(24.0 * u))));
      map.setFont(font);
      map.setPen(QColor(0xf1, 0xf3, 0xf5));
      map.drawText(QRectF(QPointF(), QSizeF(size)), Qt::AlignCenter,
                   QStringLiteral("No GPS track"));
    }
    if (circle) {
      QImage mask(size, QImage::Format_ARGB32_Premultiplied);
      mask.fill(Qt::transparent);
      {
        QPainter shape(&mask);
        shape.setRenderHint(QPainter::Antialiasing, true);
        shape.setPen(Qt::NoPen);
        shape.setBrush(Qt::white);
        shape.drawEllipse(QRectF(QPointF(), QSizeF(size)));
      }
      map.setCompositionMode(QPainter::CompositionMode_DestinationIn);
      map.drawImage(QPoint(), mask);
      map.setCompositionMode(QPainter::CompositionMode_SourceOver);
      if (has_route)
        paint_round_attribution(map, QSizeF(size), attribution, u);
    } else if (has_route) {
      paint_attribution(map, QRectF(QPointF(), QSizeF(size)), attribution, u);
    }
    // A thin light edge keeps the map apart from the picture.
    const auto edge = std::max(1.0, 2.0 * u);
    map.setBrush(Qt::NoBrush);
    map.setPen(QPen(QColor(255, 255, 255, 200), edge));
    const QRectF inner(edge / 2.0, edge / 2.0, size.width() - edge, size.height() - edge);
    if (circle)
      map.drawEllipse(inner);
    else
      map.drawRect(inner);
  }
  painter.save();
  painter.setOpacity(opacity);
  painter.drawImage(area.topLeft(), image);
  painter.restore();
  return missing;
}

// ---- overlay layout -----------------------------------------------------------------------------

bool left_corner(const ex::Corner corner) {
  return corner == ex::Corner::top_left || corner == ex::Corner::bottom_left;
}
bool right_corner(const ex::Corner corner) {
  return corner == ex::Corner::top_right || corner == ex::Corner::bottom_right;
}
bool top_corner(const ex::Corner corner) {
  return corner == ex::Corner::top_left || corner == ex::Corner::top ||
         corner == ex::Corner::top_right;
}

ex::Align resolved_align(const ex::Align align, const ex::Corner corner) {
  if (align != ex::Align::automatic)
    return align;
  if (right_corner(corner))
    return ex::Align::right;
  if (corner == ex::Corner::top || corner == ex::Corner::bottom)
    return ex::Align::center;
  return ex::Align::left;
}

// The x of a block of `width` placed at a corner, inside the margin.
double corner_x(const ex::Corner corner, const double width, const ex::FrameLayout &layout,
                const double margin) {
  if (left_corner(corner))
    return margin;
  if (right_corner(corner))
    return layout.width - margin - width;
  return (layout.width - width) / 2.0;
}

std::optional<QRectF> overlay_map_area(const ex::ExportSettings &settings,
                                       const ex::FrameLayout &layout) {
  const auto side = settings.map.size * layout.height;
  const auto margin = settings.margin * layout.height;
  const auto &anchor = settings.map.anchor;
  if (anchor.corner == ex::Corner::free)
    return QRectF(anchor.x * layout.width, anchor.y * layout.height, side, side);
  const auto x = corner_x(anchor.corner, side, layout, margin);
  const auto y = top_corner(anchor.corner) ? margin : layout.height - margin - side;
  return QRectF(x, y, side, side);
}

void paint_overlay_fields(QPainter &painter, const ex::ExportSettings &settings,
                          const ex::FrameLayout &layout, const std::vector<FieldText> &fields,
                          const std::optional<QRectF> &map_area) {
  const auto H = static_cast<double>(layout.height);
  const auto margin = settings.margin * H;
  const auto gap = 0.015 * H;
  struct Item {
    const FieldText *text;
    const ex::FieldSettings *settings;
    Fonts fonts;
    QSizeF size;
  };
  std::map<ex::Corner, std::vector<Item>> stacks;
  for (std::size_t index = 0; index < fields.size() && index < settings.fields.size(); ++index) {
    const auto &field = settings.fields[index];
    Item item{&fields[index],
              &field,
              fonts_for(field.font_size.value_or(settings.text.font_size) * H, settings.text),
              {}};
    item.size = measure(*item.text, item.fonts, settings.text);
    if (field.anchor.corner == ex::Corner::free) {
      const auto align = resolved_align(field.align, ex::Corner::free);
      auto x = field.anchor.x * layout.width;
      if (align == ex::Align::right)
        x -= item.size.width();
      else if (align == ex::Align::center)
        x -= item.size.width() / 2.0;
      draw_field(painter, QPointF(x, field.anchor.y * H), item.size.width(), align, *item.text,
                 item.fonts, settings.text);
      continue;
    }
    stacks[field.anchor.corner].push_back(item);
  }
  for (const auto &[corner, items] : stacks) {
    // A map at the same corner is nearest the edge; the fields stack beyond it, in list order.
    const auto shares_map = map_area && settings.map.anchor.corner == corner;
    auto total = 0.0;
    for (const auto &item : items)
      total += item.size.height();
    total += gap * 0.5 * static_cast<double>(items.size() - 1U);
    auto y = top_corner(corner) ? margin : H - margin - total;
    if (shares_map) {
      if (top_corner(corner))
        y = map_area->bottom() + gap;
      else
        y = map_area->top() - gap - total;
    }
    for (const auto &item : items) {
      const auto align = resolved_align(item.settings->align, corner);
      const auto x = corner_x(corner, item.size.width(), layout, margin);
      draw_field(painter, QPointF(x, y), item.size.width(), align, *item.text, item.fonts,
                 settings.text);
      y += item.size.height() + gap * 0.5;
    }
  }
}

// ---- panel --------------------------------------------------------------------------------------

void paint_panel_fields(QPainter &painter, const QRectF &area, const ex::ExportSettings &settings,
                        const ex::FrameLayout &layout, const std::vector<FieldText> &fields) {
  if (fields.empty() || area.isEmpty())
    return;
  const auto u = unit(layout);
  const auto columns = area.width() > 1.6 * area.height() && fields.size() > 1U ? 2 : 1;
  const auto count = static_cast<int>(fields.size());
  const auto rows = (count + columns - 1) / columns;
  const auto column_gap = 24.0 * u;
  const auto column_width = (area.width() - (columns - 1) * column_gap) / columns;
  Fonts fonts;
  double row_height{};
  // From the configured size down, until every row and every value fits.
  for (double factor = 1.0;; factor *= 0.92) {
    fonts = fonts_for(settings.text.font_size * layout.height * factor, settings.text);
    double tallest{};
    bool fits = true;
    for (const auto &field : fields) {
      const auto size = measure(field, fonts, settings.text);
      tallest = std::max(tallest, size.height());
      fits = fits && size.width() <= column_width;
    }
    row_height = tallest + 0.4 * fonts.value_px;
    fits = fits && rows * row_height <= area.height();
    if (fits || factor < 0.15)
      break;
  }
  painter.save();
  painter.setClipRect(area, Qt::IntersectClip);
  for (int index = 0; index < count; ++index) {
    const auto column = index / rows;
    const auto row = index % rows;
    draw_field(
        painter,
        QPointF(area.x() + column * (column_width + column_gap), area.y() + row * row_height),
        column_width, ex::Align::left, fields[static_cast<std::size_t>(index)], fonts,
        settings.text);
  }
  painter.restore();
}

} // namespace

// ---- ExportTrack --------------------------------------------------------------------------------

ExportTrack::ExportTrack(std::vector<tm::GpsPoint> points, const std::int64_t range_start,
                         const std::int64_t range_end, const double seconds_per_tick)
    : points_(std::move(points)), seconds_per_tick_(seconds_per_tick) {
  const auto ticks_less = [](const tm::GpsPoint &point, const std::int64_t ticks) {
    return point.media_time_ticks < ticks;
  };
  first_ = static_cast<std::size_t>(
      std::lower_bound(points_.begin(), points_.end(), range_start, ticks_less) - points_.begin());
  last_ = static_cast<std::size_t>(
      std::upper_bound(points_.begin(), points_.end(), range_end,
                       [](const std::int64_t ticks, const tm::GpsPoint &point) {
                         return ticks < point.media_time_ticks;
                       }) -
      points_.begin());
  // Include the records nearest the ends, which frames at the very ends may show.
  if (first_ > 0U)
    --first_;
  if (last_ < points_.size())
    ++last_;
  // Jumps are judged over every record, so the range's ends keep their context.
  std::vector<gb::geo::RoutePoint> all;
  all.reserve(points_.size());
  for (const auto &point : points_) {
    all.push_back({{static_cast<double>(point.latitude), static_cast<double>(point.longitude)},
                   point.media_time_ticks,
                   point.validity == tm::Validity::valid,
                   static_cast<double>(point.speed_2d),
                   false});
  }
  static_cast<void>(gb::geo::mark_route_jumps(all, seconds_per_tick));
  route_points_.assign(all.begin() + static_cast<std::ptrdiff_t>(first_),
                       all.begin() + static_cast<std::ptrdiff_t>(last_));
  all_ = std::move(all);
  std::optional<gb::geo::LatLon> anchor;
  double travelled = 0.0;
  for (const auto &route_point : route_points_) {
    const auto &position = route_point.position;
    const auto usable = route_point.usable;
    route_.push_back(gb::geo::project(position));
    usable_in_range_ = usable_in_range_ || usable;
    if (usable) {
      // A jump is not travelled: counting restarts after it.
      if (!anchor || route_point.break_before) {
        anchor = position;
      } else if (const auto step = gb::geo::distance_metres(*anchor, position);
                 step >= kDistanceStepMetres) {
        travelled += step;
        anchor = position;
      }
    }
    distance_.push_back(anchor ? std::optional<double>(travelled) : std::nullopt);
  }
}

std::optional<std::size_t> ExportTrack::nearest(const std::int64_t ticks) const {
  if (points_.empty())
    return std::nullopt;
  const auto after = std::lower_bound(points_.begin(), points_.end(), ticks,
                                      [](const tm::GpsPoint &point, const std::int64_t value) {
                                        return point.media_time_ticks < value;
                                      });
  if (after == points_.begin())
    return 0U;
  if (after == points_.end())
    return points_.size() - 1U;
  const auto before = after - 1;
  // Ties go to the earlier record, as gb::telemetry::nearest_point does.
  const auto index = (ticks - before->media_time_ticks) <= (after->media_time_ticks - ticks)
                         ? before - points_.begin()
                         : after - points_.begin();
  return static_cast<std::size_t>(index);
}

const tm::GpsPoint *ExportTrack::fix_at(const std::int64_t ticks) const {
  const auto index = nearest(ticks);
  if (!index)
    return nullptr;
  const auto &point = points_[*index];
  if (point.validity != tm::Validity::valid)
    return nullptr;
  if (points_.size() > 1U) {
    auto interval = std::numeric_limits<std::int64_t>::max();
    if (*index > 0U)
      interval = point.media_time_ticks - points_[*index - 1U].media_time_ticks;
    if (*index + 1U < points_.size())
      interval = std::min(interval, points_[*index + 1U].media_time_ticks - point.media_time_ticks);
    if (std::llabs(point.media_time_ticks - ticks) > std::max<std::int64_t>(interval, 1))
      return nullptr;
  }
  return &point;
}

std::optional<std::size_t> ExportTrack::route_index(const std::int64_t ticks) const {
  const auto index = nearest(ticks);
  if (!index || route_.empty())
    return std::nullopt;
  return std::clamp(*index, first_, last_ - 1U) - first_;
}

std::optional<std::size_t> ExportTrack::route_anchor(const std::size_t index) const {
  for (auto candidate = std::min(index, route_points_.size() - 1U) + 1U; candidate > 0U;
       --candidate) {
    if (route_points_[candidate - 1U].usable)
      return candidate - 1U;
  }
  for (std::size_t candidate = 0; candidate < route_points_.size(); ++candidate) {
    if (route_points_[candidate].usable)
      return candidate;
  }
  return std::nullopt;
}

std::optional<double> ExportTrack::distance_at(const std::int64_t ticks) const {
  const auto index = route_index(ticks);
  if (!index)
    return std::nullopt;
  return distance_[*index];
}

std::vector<std::size_t> ExportTrack::range_records() const {
  std::vector<std::size_t> records;
  for (auto index = first_; index < last_; ++index)
    records.push_back(index - first_);
  return records;
}

// ---- fields -------------------------------------------------------------------------------------

QTimeZone time_zone_for(const std::string &setting) {
  if (setting == "local")
    return QTimeZone::systemTimeZone();
  if (setting == "utc" || setting == "UTC")
    return QTimeZone::utc();
  if ((setting.size() == 6U) && (setting[0] == '+' || setting[0] == '-') && setting[3] == ':') {
    const auto digits = setting.substr(1, 2) + setting.substr(4, 2);
    if (std::all_of(digits.begin(), digits.end(),
                    [](const char c) { return c >= '0' && c <= '9'; })) {
      const auto hours = std::stoi(setting.substr(1, 2));
      const auto minutes = std::stoi(setting.substr(4, 2));
      if (hours <= 14 && minutes < 60) {
        const auto seconds = (hours * 3600 + minutes * 60) * (setting[0] == '-' ? -1 : 1);
        return QTimeZone(seconds);
      }
    }
    return {};
  }
  const auto id = QByteArray::fromStdString(setting);
  if (!QTimeZone::isTimeZoneIdAvailable(id))
    return {};
  return QTimeZone(id);
}

std::vector<FieldText> field_texts(const ex::ExportSettings &settings, const QTimeZone &zone,
                                   const tm::GpsPoint *fix,
                                   const std::optional<double> distance_metres) {
  std::optional<QDateTime> moment;
  if (fix != nullptr && fix->utc) {
    if (const auto utc = gb::image::parse_utc(*fix->utc); utc) {
      moment = QDateTime(QDate(utc->year, utc->month, utc->day),
                         QTime(utc->hour, utc->minute, static_cast<int>(std::floor(utc->second))),
                         Qt::UTC)
                   .toTimeZone(zone);
    }
  }
  const auto degrees = [](const long double value, const char positive, const char negative) {
    return QStringLiteral("%1° %2")
        .arg(static_cast<double>(std::abs(value)), 0, 'f', 5)
        .arg(QChar(value < 0.0L ? negative : positive));
  };
  // [date] format, with the month and weekday names from the settings (dayOfWeek: 1 is Monday).
  const auto date_text = [&settings, &moment] {
    const auto date = moment->date();
    return QString::fromStdString(
        ex::format_date(settings.date, date.year(), date.month(), date.day(), date.dayOfWeek()));
  };
  std::vector<FieldText> texts;
  for (const auto &field : settings.fields) {
    FieldText text{field.field, QString::fromStdString(field.label), kUnknown, field.small};
    switch (field.field) {
    case ex::Field::datetime:
      if (moment)
        text.value = date_text() + QLatin1Char(' ') + moment->toString(QStringLiteral("HH:mm:ss"));
      break;
    case ex::Field::date:
      if (moment)
        text.value = date_text();
      break;
    case ex::Field::time:
      if (moment)
        text.value = moment->toString(QStringLiteral("HH:mm:ss"));
      break;
    case ex::Field::latitude:
      if (fix != nullptr)
        text.value = degrees(fix->latitude, 'N', 'S');
      break;
    case ex::Field::longitude:
      if (fix != nullptr)
        text.value = degrees(fix->longitude, 'E', 'W');
      break;
    case ex::Field::altitude:
      if (fix != nullptr) {
        const auto metres = static_cast<double>(fix->altitude);
        text.value = settings.units.altitude == ex::AltitudeUnit::ft
                         ? QStringLiteral("%1 ft").arg(metres / 0.3048, 0, 'f', 0)
                         : QStringLiteral("%1 m").arg(metres, 0, 'f', 0);
      }
      break;
    case ex::Field::speed:
      if (fix != nullptr) {
        const auto mps = static_cast<double>(fix->speed_2d);
        switch (settings.units.speed) {
        case ex::SpeedUnit::kmh:
          text.value = QStringLiteral("%1 km/h").arg(mps * 3.6, 0, 'f', 0);
          break;
        case ex::SpeedUnit::mph:
          text.value = QStringLiteral("%1 mph").arg(mps / 0.44704, 0, 'f', 0);
          break;
        case ex::SpeedUnit::ms:
          text.value = QStringLiteral("%1 m/s").arg(mps, 0, 'f', 1);
          break;
        case ex::SpeedUnit::kn:
          text.value = QStringLiteral("%1 kn").arg(mps * 3600.0 / 1852.0, 0, 'f', 1);
          break;
        }
      }
      break;
    case ex::Field::distance:
      if (distance_metres) {
        text.value = settings.units.distance == ex::DistanceUnit::mi
                         ? QStringLiteral("%1 mi").arg(*distance_metres / 1609.344, 0, 'f', 2)
                         : QStringLiteral("%1 km").arg(*distance_metres / 1000.0, 0, 'f', 2);
      }
      break;
    }
    texts.push_back(text);
  }
  return texts;
}

// ---- map scene and tiles ------------------------------------------------------------------------

std::optional<QRectF> export_map_area(const ex::ExportSettings &settings,
                                      const ex::FrameLayout &layout) {
  if (!settings.map.enabled || settings.placement == ex::Placement::none)
    return std::nullopt;
  if (settings.placement == ex::Placement::overlay)
    return overlay_map_area(settings, layout);
  const auto regions = ex::panel_regions(layout, ex::panel_side(settings.placement), true,
                                         settings.telemetry && !settings.fields.empty(),
                                         static_cast<int>(settings.shown_graphs().size()));
  if (!regions.map)
    return std::nullopt;
  return qrect(*regions.map);
}

std::vector<std::pair<ex::Graph, QRectF>> export_graph_areas(const ex::ExportSettings &settings,
                                                             const ex::FrameLayout &layout) {
  std::vector<std::pair<ex::Graph, QRectF>> areas;
  const auto shown = settings.shown_graphs();
  if (shown.empty() || settings.placement == ex::Placement::none)
    return areas;
  if (settings.placement != ex::Placement::overlay) {
    const auto regions = ex::panel_regions(
        layout, ex::panel_side(settings.placement), settings.map.enabled,
        settings.telemetry && !settings.fields.empty(), static_cast<int>(shown.size()));
    for (std::size_t index = 0; index < shown.size() && index < regions.graphs.size(); ++index)
      areas.emplace_back(shown[index], qrect(regions.graphs[index]));
    return areas;
  }
  const auto H = static_cast<double>(layout.height);
  const auto gap = 0.015 * H;
  for (const auto which : shown) {
    const auto &graph = settings.graph(which);
    const auto width = graph.width * H;
    const auto height = graph.height * H;
    const auto &anchor = graph.anchor;
    if (anchor.corner == ex::Corner::free) {
      areas.emplace_back(which, QRectF(anchor.x * layout.width, anchor.y * H, width, height));
      continue;
    }
    const auto margin_x = graph.margin_x.value_or(settings.margin) * H;
    const auto margin_y = graph.margin_y.value_or(settings.margin) * H;
    const auto x = corner_x(anchor.corner, width, layout, margin_x);
    auto y = top_corner(anchor.corner) ? margin_y : H - margin_y - height;
    // Beyond an earlier graph at the same corner.
    for (const auto &[other, area] : areas) {
      if (settings.graph(other).anchor.corner != anchor.corner)
        continue;
      y = top_corner(anchor.corner) ? std::max(y, area.bottom() + gap)
                                    : std::min(y, area.top() - gap - height);
    }
    areas.emplace_back(which, QRectF(x, y, width, height));
  }
  return areas;
}

MapScene export_map_scene(const ex::ExportSettings &settings, const ex::FrameLayout &layout,
                          const MapScene &scene, const QSizeF map_size,
                          const std::optional<double> width_metres) {
  auto result = scene;
  result.scale = unit(layout);
  result.track = settings.map.track;
  result.smooth = true;
  const auto latitude = gb::geo::unproject(scene.center).latitude;
  result.zoom = map_zoom_for_width(map_size.width(), result.scale,
                                   width_metres.value_or(settings.map.width_metres), latitude);
  return result;
}

std::vector<gb::geo::TileKey> export_map_tiles(const ex::ExportSettings &settings,
                                               const ex::FrameLayout &layout, const MapScene &scene,
                                               const std::optional<double> width_metres) {
  const auto area = export_map_area(settings, layout);
  if (!area)
    return {};
  const QSizeF size(std::round(area->width()), std::round(area->height()));
  return map_tiles(size, export_map_scene(settings, layout, scene, size, width_metres));
}

OverlayResult paint_export_overlays(QPainter &painter, const ex::ExportSettings &settings,
                                    const ex::FrameLayout &layout, const OverlayFrame &frame,
                                    const QString &attribution) {
  OverlayResult result;
  if (settings.placement == ex::Placement::none)
    return result;
  painter.save();
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setRenderHint(QPainter::TextAntialiasing, true);
  const auto map_area = export_map_area(settings, layout);
  const auto fields = settings.telemetry ? frame.fields : std::vector<FieldText>{};
  if (settings.placement == ex::Placement::overlay) {
    if (map_area) {
      result.missing_tiles =
          paint_map_box(painter, *map_area, settings, layout, frame.scene, frame.map_width_metres,
                        attribution, settings.map.opacity);
    }
    paint_overlay_fields(painter, settings, layout, fields, map_area);
  } else if (layout.panel) {
    painter.fillRect(qrect(*layout.panel), Qt::black);
    const auto regions =
        ex::panel_regions(layout, ex::panel_side(settings.placement), settings.map.enabled,
                          !fields.empty(), static_cast<int>(settings.shown_graphs().size()));
    if (map_area)
      result.missing_tiles = paint_map_box(painter, *map_area, settings, layout, frame.scene,
                                           frame.map_width_metres, attribution, 1.0);
    if (regions.text)
      paint_panel_fields(painter, qrect(*regions.text), settings, layout, fields);
  }
  if (frame.graphs != nullptr) {
    for (const auto &[which, area] : export_graph_areas(settings, layout)) {
      if (auto *graph = frame.graphs->find(which); graph != nullptr)
        graph->paint(painter, area, layout, frame.ticks, frame.fix_record);
    }
  }
  painter.restore();
  return result;
}

std::optional<double> CourseFilter::update(const std::optional<double> course,
                                           const double seconds) {
  constexpr double kTimeConstant = 1.0;
  constexpr double kJump = 2.0; // seconds without a course, or a jump in time: start afresh
  if (!course) {
    if (seconds - last_seconds_ > kJump)
      shown_.reset();
    return shown_;
  }
  const auto dt = seconds - last_seconds_;
  last_seconds_ = seconds;
  if (!shown_ || dt < 0.0 || dt > kJump) {
    shown_ = *course;
    return shown_;
  }
  const auto difference = std::remainder(*course - *shown_, 360.0);
  *shown_ = std::fmod(*shown_ + difference * (1.0 - std::exp(-dt / kTimeConstant)) + 360.0, 360.0);
  return shown_;
}

} // namespace gb::render

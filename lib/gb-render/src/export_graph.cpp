#include "gb/render/export_graph.hpp"

#include <QFont>
#include <QFontMetricsF>
#include <QPainterPath>

#include <algorithm>
#include <cmath>
#include <limits>

namespace gb::render {
namespace {

namespace ex = gb::exporting;
namespace tm = gb::telemetry;

// Records further apart than this are not joined, as in gb-player's graph.
constexpr double kBreakSeconds = 3.0;

QColor qcolor(const ex::Color color) { return {color.red, color.green, color.blue, color.alpha}; }

// The smallest 1/2/5 × 10^n step that splits span into at most target intervals.
double nice_step(const double span, const int target) {
  if (!(span > 0.0))
    return 1.0;
  const auto raw = span / target;
  const auto magnitude = std::pow(10.0, std::floor(std::log10(raw)));
  for (const auto factor : {1.0, 2.0, 5.0, 10.0}) {
    if (factor * magnitude >= raw)
      return factor * magnitude;
  }
  return 10.0 * magnitude;
}

// Clock-friendly steps for the time axis, in seconds.
double time_step(const double span, const int target) {
  for (const auto step :
       {1.0, 2.0, 5.0, 10.0, 15.0, 30.0, 60.0, 120.0, 300.0, 600.0, 900.0, 1800.0, 3600.0}) {
    if (span / step <= target)
      return step;
  }
  return nice_step(span / 3600.0, target) * 3600.0;
}

QString time_label(const double seconds) {
  const auto total = static_cast<long long>(std::llround(seconds));
  const auto hours = total / 3600;
  const auto minutes = (total / 60) % 60;
  const auto secs = total % 60;
  if (hours > 0) {
    return QStringLiteral("%1:%2:%3")
        .arg(hours)
        .arg(minutes, 2, 10, QLatin1Char('0'))
        .arg(secs, 2, 10, QLatin1Char('0'));
  }
  return QStringLiteral("%1:%2").arg(minutes).arg(secs, 2, 10, QLatin1Char('0'));
}

double shown_value(const ex::Graph kind, const ex::UnitSettings &units, const tm::GpsPoint &point) {
  if (kind == ex::Graph::height) {
    const auto metres = static_cast<double>(point.altitude);
    return units.altitude == ex::AltitudeUnit::ft ? metres / 0.3048 : metres;
  }
  const auto mps = static_cast<double>(point.speed_2d);
  switch (units.speed) {
  case ex::SpeedUnit::kmh:
    return mps * 3.6;
  case ex::SpeedUnit::mph:
    return mps / 0.44704;
  case ex::SpeedUnit::ms:
    return mps;
  case ex::SpeedUnit::kn:
    return mps * 3600.0 / 1852.0;
  }
  return mps;
}

QString value_label(const double value, const double step) {
  return QString::number(value, 'f', step < 1.0 ? 1 : 0);
}

QFont graph_font(const double pixels) {
  QFont font;
  font.setPixelSize(std::max(1, static_cast<int>(std::lround(pixels))));
  return font;
}

} // namespace

ExportGraph::ExportGraph(const ex::Graph kind, const ex::ExportSettings &settings,
                         const ExportTrack &track, const std::int64_t range_start,
                         const std::int64_t range_end)
    : kind_(kind), settings_(settings.graph(kind)), label_color_(settings.text.label_color),
      range_start_(range_start), seconds_per_tick_(track.seconds_per_tick()) {
  span_seconds_ = static_cast<double>(range_end - range_start) * seconds_per_tick_;
  window_seconds_ = std::min(span_seconds_, settings_.window_seconds.value_or(span_seconds_));
  title_ = kind == ex::Graph::speed
               ? QStringLiteral("Speed, %1")
                     .arg(QString::fromStdString(ex::speed_unit_name(settings.units.speed)))
               : QStringLiteral("Height, %1")
                     .arg(QString::fromStdString(ex::altitude_unit_name(settings.units.altitude)));

  // Every record, so the smoothing at the ends of the range has the records beyond them.
  const auto &points = track.points();
  std::vector<Sample> all;
  all.reserve(points.size());
  for (const auto &point : points) {
    all.push_back({static_cast<double>(point.media_time_ticks - range_start) * seconds_per_tick_,
                   shown_value(kind, settings.units, point),
                   point.validity == tm::Validity::valid});
  }
  if (settings_.smooth_seconds > 0.0) {
    // Sliding sums over each stretch of joined records; a window never reaches across a gap.
    const auto half = settings_.smooth_seconds / 2.0;
    std::vector<double> smoothed(all.size());
    std::size_t begin = 0;
    while (begin < all.size()) {
      if (!all[begin].usable) {
        ++begin;
        continue;
      }
      auto end = begin + 1U;
      while (end < all.size() && all[end].usable &&
             all[end].seconds - all[end - 1U].seconds <= kBreakSeconds) {
        ++end;
      }
      double sum = 0.0;
      auto low = begin;
      auto high = begin;
      for (auto i = begin; i < end; ++i) {
        while (high < end && all[high].seconds <= all[i].seconds + half) {
          sum += all[high].value;
          ++high;
        }
        while (all[low].seconds < all[i].seconds - half) {
          sum -= all[low].value;
          ++low;
        }
        smoothed[i] = sum / static_cast<double>(high - low);
      }
      begin = end;
    }
    for (std::size_t i = 0; i < all.size(); ++i) {
      if (all[i].usable)
        all[i].value = smoothed[i];
    }
  }

  // The records within the range, and the nearest one beyond each end, so the line reaches the
  // edges of the plot.
  const auto first =
      std::lower_bound(all.begin(), all.end(), 0.0, [](const Sample &sample, const double seconds) {
        return sample.seconds < seconds;
      });
  const auto last = std::upper_bound(
      all.begin(), all.end(), span_seconds_,
      [](const double seconds, const Sample &sample) { return seconds < sample.seconds; });
  auto from = static_cast<std::size_t>(first - all.begin());
  auto to = static_cast<std::size_t>(last - all.begin());
  if (from > 0U)
    --from;
  if (to < all.size())
    ++to;
  first_record_ = from;
  samples_.assign(all.begin() + static_cast<std::ptrdiff_t>(from),
                  all.begin() + static_cast<std::ptrdiff_t>(to));

  // The value axis covers what is shown within the range. Speed starts at zero; height gets a
  // margin so a flat stretch does not sit on the frame.
  auto low = std::numeric_limits<double>::infinity();
  auto high = -std::numeric_limits<double>::infinity();
  for (const auto &sample : samples_) {
    if (!sample.usable || sample.seconds < 0.0 || sample.seconds > span_seconds_)
      continue;
    low = std::min(low, sample.value);
    high = std::max(high, sample.value);
  }
  has_data_ = std::isfinite(low);
  if (!has_data_)
    return;
  const auto height = kind == ex::Graph::height;
  if (!height)
    low = 0.0;
  const auto spread = std::max(high - low, height ? 10.0 : 5.0);
  if (height) {
    const auto middle = (high + low) / 2.0;
    low = middle - spread * 0.55;
    high = middle + spread * 0.55;
  } else {
    high = low + spread * 1.08;
  }
  step_ = nice_step(high - low, 3);
  minimum_ = std::floor(low / step_) * step_;
  maximum_ = std::ceil(high / step_) * step_;
}

ExportGraph::Plot ExportGraph::plot_geometry(const QSizeF size,
                                             const ex::FrameLayout &layout) const {
  Plot plot;
  plot.font_px = std::max(5.0, std::min(0.022 * layout.height, 0.12 * size.height()));
  const auto f = plot.font_px;
  const auto pad = 0.45 * f;
  const auto title_height = 1.3 * f;
  const auto time_height = has_data_ ? 1.3 * f : 0.0;
  double labels_width = 0.0;
  if (has_data_) {
    const QFontMetricsF metrics(graph_font(f));
    for (auto v = minimum_; v <= maximum_ + step_ / 2.0; v += step_)
      labels_width = std::max(labels_width, metrics.horizontalAdvance(value_label(v, step_)));
    labels_width += 0.4 * f;
  }
  plot.frame = QRectF(pad + labels_width, pad + title_height,
                      std::max(1.0, size.width() - 2.0 * pad - labels_width),
                      std::max(1.0, size.height() - 2.0 * pad - title_height - time_height));
  return plot;
}

double ExportGraph::window_start(const double seconds) const {
  // The cursor at the centre, but the window never extends past the range.
  return std::clamp(seconds - window_seconds_ / 2.0, 0.0, span_seconds_ - window_seconds_);
}

void ExportGraph::render(const QSize size, const ex::FrameLayout &layout,
                         const double window_start) {
  cache_size_ = size;
  cache_window_start_ = window_start;
  cache_ = QImage(size, QImage::Format_ARGB32_Premultiplied);
  cache_.fill(Qt::transparent);
  const auto u = layout.height / 1080.0;
  const auto plot = plot_geometry(QSizeF(size), layout);
  const auto &frame = plot.frame;
  const auto f = plot.font_px;

  QPainter painter(&cache_);
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setRenderHint(QPainter::TextAntialiasing, true);
  painter.setPen(Qt::NoPen);
  painter.setBrush(qcolor(settings_.background));
  const auto radius = std::min(8.0 * u, 0.1 * size.height());
  painter.drawRoundedRect(QRectF(0.0, 0.0, size.width(), size.height()), radius, radius);

  const auto label = qcolor(label_color_);
  auto grid = label;
  grid.setAlpha(grid.alpha() / 4);
  painter.setFont(graph_font(f));
  painter.setPen(label);
  painter.drawText(QRectF(frame.left(), frame.top() - 1.3 * f, frame.width(), 1.3 * f),
                   Qt::AlignLeft | Qt::AlignVCenter, title_);
  if (!has_data_) {
    painter.drawText(frame, Qt::AlignCenter, QStringLiteral("—"));
    return;
  }

  const auto to_y = [&](const double v) {
    return frame.bottom() - (v - minimum_) / (maximum_ - minimum_) * frame.height();
  };
  const auto window_end = window_start + window_seconds_;
  const auto to_x = [&](const double seconds) {
    return frame.left() + (seconds - window_start) / window_seconds_ * frame.width();
  };
  const auto scrolling = window_seconds_ < span_seconds_;
  const auto line_width = std::max(1.0, u);
  for (auto v = minimum_; v <= maximum_ + step_ / 2.0; v += step_) {
    const auto y = to_y(v);
    painter.setPen(QPen(grid, line_width));
    painter.drawLine(QPointF(frame.left(), y), QPointF(frame.right(), y));
    painter.setPen(label);
    painter.drawText(QRectF(0.0, y - f, frame.left() - 0.4 * f, 2.0 * f),
                     Qt::AlignRight | Qt::AlignVCenter, value_label(v, step_));
  }
  const auto seconds_step =
      time_step(window_seconds_, std::max(2, static_cast<int>(frame.width() / (4.5 * f))));
  const auto label_width = 4.0 * f;
  painter.save();
  if (scrolling) {
    // Scrolling labels are cut at the plot's sides rather than jumping inwards near them.
    painter.setClipRect(
        QRectF(frame.left(), frame.top(), frame.width(), size.height() - frame.top()));
  }
  for (auto s = std::ceil(window_start / seconds_step - 1e-9) * seconds_step;
       s <= window_end + 1e-9; s += seconds_step) {
    const auto x = to_x(s);
    painter.setPen(QPen(grid, line_width));
    painter.drawLine(QPointF(x, frame.top()), QPointF(x, frame.bottom()));
    painter.setPen(label);
    if (scrolling) {
      // A label on a side of the plot (the start or end of the range) is aligned inwards; the
      // others are centred and cut at the sides as they scroll past.
      const auto on_left = x - frame.left() < 1.0;
      const auto on_right = frame.right() - x < 1.0;
      const auto left = on_left ? x : on_right ? x - label_width : x - label_width / 2.0;
      const auto alignment = on_left ? Qt::AlignLeft : on_right ? Qt::AlignRight : Qt::AlignHCenter;
      painter.drawText(QRectF(left, frame.bottom(), label_width, 1.3 * f),
                       static_cast<int>(alignment | Qt::AlignVCenter), time_label(s));
      continue;
    }
    // Labels at the plot's edges stay inside it; elsewhere they are centred on the line.
    const auto at_left = x - frame.left() < label_width / 2.0;
    const auto at_right = !at_left && frame.right() - x < label_width / 2.0;
    if (at_right && frame.right() - x > 1.0)
      continue; // near the end but not at it: its label would be cut or crowded
    const auto left = at_left ? x : at_right ? x - label_width : x - label_width / 2.0;
    const auto alignment = at_left ? Qt::AlignLeft : at_right ? Qt::AlignRight : Qt::AlignHCenter;
    painter.drawText(QRectF(left, frame.bottom(), label_width, 1.3 * f),
                     static_cast<int>(alignment | Qt::AlignVCenter), time_label(s));
  }
  painter.restore();

  // The records in the window, and one beyond each side so the line reaches the edges.
  auto begin = static_cast<std::size_t>(
      std::lower_bound(samples_.begin(), samples_.end(), window_start,
                       [](const Sample &sample, const double s) { return sample.seconds < s; }) -
      samples_.begin());
  auto end = static_cast<std::size_t>(
      std::upper_bound(samples_.begin(), samples_.end(), window_end,
                       [](const double s, const Sample &sample) { return s < sample.seconds; }) -
      samples_.begin());
  if (begin > 0U)
    --begin;
  if (end < samples_.size())
    ++end;
  QPainterPath path;
  bool open = false;
  double last_seconds = 0.0;
  for (auto index = begin; index < end; ++index) {
    const auto &sample = samples_[index];
    if (!sample.usable) {
      open = false;
      continue;
    }
    const QPointF point(to_x(sample.seconds), to_y(sample.value));
    if (open && sample.seconds - last_seconds <= kBreakSeconds)
      path.lineTo(point);
    else
      path.moveTo(point);
    open = true;
    last_seconds = sample.seconds;
  }
  painter.setClipRect(frame);
  painter.setPen(
      QPen(qcolor(settings_.line_color), 2.0 * u, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
  painter.setBrush(Qt::NoBrush);
  painter.drawPath(path);
}

void ExportGraph::paint(QPainter &painter, const QRectF &area, const ex::FrameLayout &layout,
                        const std::int64_t ticks, const std::optional<std::size_t> record) {
  const QPoint origin(static_cast<int>(std::lround(area.x())),
                      static_cast<int>(std::lround(area.y())));
  const QSize size(static_cast<int>(std::lround(area.width())),
                   static_cast<int>(std::lround(area.height())));
  if (size.width() < 2 || size.height() < 2)
    return;
  const auto seconds = static_cast<double>(ticks - range_start_) * seconds_per_tick_;
  const auto start = window_start(seconds);
  if (size != cache_size_ || start != cache_window_start_ || cache_.isNull())
    render(size, layout, start);
  painter.drawImage(origin, cache_);
  if (!has_data_)
    return;

  const auto u = layout.height / 1080.0;
  const auto frame = plot_geometry(QSizeF(size), layout).frame.translated(origin);
  const auto x_for = [&](const double s) {
    return frame.left() + (s - start) / window_seconds_ * frame.width();
  };
  const auto x = std::clamp(x_for(seconds), frame.left(), frame.right());
  painter.save();
  painter.setRenderHint(QPainter::Antialiasing, true);
  const auto cursor = qcolor(settings_.cursor_color);
  painter.setPen(QPen(cursor, std::max(1.0, 1.5 * u)));
  painter.drawLine(QPointF(x, frame.top()), QPointF(x, frame.bottom()));
  if (record && *record >= first_record_ && *record - first_record_ < samples_.size()) {
    const auto &sample = samples_[*record - first_record_];
    if (sample.usable && sample.seconds >= start && sample.seconds <= start + window_seconds_) {
      const QPointF point(x_for(sample.seconds), frame.bottom() - (sample.value - minimum_) /
                                                                      (maximum_ - minimum_) *
                                                                      frame.height());
      painter.setPen(QPen(Qt::white, std::max(1.0, 1.5 * u)));
      painter.setBrush(cursor);
      const auto radius = std::max(2.0, 4.5 * u);
      painter.drawEllipse(point, radius, radius);
    }
  }
  painter.restore();
}

ExportGraphs::ExportGraphs(const ex::ExportSettings &settings, const ExportTrack &track,
                           const std::int64_t range_start, const std::int64_t range_end) {
  for (const auto kind : settings.shown_graphs())
    graphs_.emplace_back(kind, settings, track, range_start, range_end);
}

ExportGraph *ExportGraphs::find(const ex::Graph kind) {
  for (auto &graph : graphs_) {
    if (graph.kind() == kind)
      return &graph;
  }
  return nullptr;
}

} // namespace gb::render

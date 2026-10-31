#include "profile_view.hpp"

#include "gb/telemetry/telemetry.hpp"
#include "player_controller.hpp"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QQuickWindow>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

// Records further apart than this are not joined: the stretch between them has no data.
constexpr double kBreakSeconds = 3.0;
// Width of the centred smoothing window. Measured on walking recordings (18 Hz GPS): 1-2 s still
// leaves speed swinging by 1-3 km/h within a second or two, which a walker does not do; 5 s
// starts to fill in real stops. Height is clean from 1-2 s, and 3 s does not flatten it.
constexpr double kSmoothSeconds = 3.0;
constexpr double kLeftMargin = 46.0;
constexpr double kRightMargin = 8.0;
constexpr double kTopMargin = 18.0;
constexpr double kBottomMargin = 18.0;
// Narrowest time window, and the window change per wheel notch.
constexpr double kMinimumWindowSeconds = 30.0;
constexpr double kZoomPerNotch = 1.25;

const QColor kBackground(0x12, 0x1a, 0x21);
const QColor kPlotBackground(0x0d, 0x14, 0x1a);
const QColor kGrid(0x22, 0x30, 0x3b);
const QColor kAxisText(0x7f, 0x91, 0x9f);
const QColor kLine(0x3b, 0x9b, 0xdb);
const QColor kCursor(0xf7, 0x67, 0x07);

// The smallest 1/2/5 × 10^n step that splits span into at most target intervals.
double niceStep(const double span, const int target) {
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
double timeStep(const double span, const int target) {
  for (const auto step :
       {1.0, 2.0, 5.0, 10.0, 15.0, 30.0, 60.0, 120.0, 300.0, 600.0, 900.0, 1800.0, 3600.0}) {
    if (span / step <= target)
      return step;
  }
  return niceStep(span / 3600.0, target) * 3600.0;
}

QString timeLabel(const double seconds) {
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

} // namespace

ProfileView::ProfileView(QQuickItem *parent) : QQuickPaintedItem(parent) {
  setAcceptedMouseButtons(Qt::LeftButton | Qt::MiddleButton);
  setFillColor(kBackground);
  setOpaquePainting(true);
}

QObject *ProfileView::controller() const { return player_; }

void ProfileView::setController(QObject *object) {
  auto *controller = qobject_cast<PlayerController *>(object);
  if (controller == player_)
    return;
  if (player_ != nullptr)
    player_->disconnect(this);
  player_ = controller;
  if (player_ != nullptr) {
    connect(player_, &PlayerController::mapRouteChanged, this, &ProfileView::onDataChanged);
    connect(player_, &PlayerController::fileChanged, this, &ProfileView::onDataChanged);
    connect(player_, &PlayerController::positionChanged, this, &ProfileView::onPositionChanged);
    connect(player_, &PlayerController::mapPositionChanged, this, &ProfileView::onPositionChanged);
  }
  emit controllerChanged();
  onDataChanged();
}

void ProfileView::setQuantity(const Quantity quantity) {
  if (quantity == quantity_)
    return;
  quantity_ = quantity;
  emit quantityChanged();
  plot_dirty_ = true;
  update();
}

void ProfileView::setSmoothed(const bool smoothed) {
  if (smoothed == smoothed_)
    return;
  smoothed_ = smoothed;
  emit smoothedChanged();
  plot_dirty_ = true;
  update();
}

void ProfileView::onDataChanged() {
  samples_.clear();
  duration_ = player_ != nullptr && player_->hasMedia() ? player_->duration() : 0.0;
  window_start_ = 0.0;
  window_span_ = duration_;
  const auto *telemetry = player_ != nullptr ? player_->telemetry() : nullptr;
  if (telemetry != nullptr && telemetry->time_base.denominator != 0U) {
    const auto base = static_cast<double>(telemetry->time_base.numerator) /
                      static_cast<double>(telemetry->time_base.denominator);
    samples_.reserve(telemetry->gps.size());
    for (const auto &point : telemetry->gps) {
      samples_.push_back({static_cast<double>(point.media_time_ticks) * base,
                          static_cast<double>(point.altitude),
                          static_cast<double>(point.speed_2d) * 3.6, 0.0, 0.0,
                          point.validity == gb::telemetry::Validity::valid});
    }
  }
  smoothSamples();
  plot_dirty_ = true;
  update();
}

void ProfileView::smoothSamples() {
  // Stretches are the runs of usable records the plot joins; a window never reaches across a gap.
  std::size_t begin = 0;
  while (begin < samples_.size()) {
    if (!samples_[begin].usable) {
      ++begin;
      continue;
    }
    auto end = begin + 1;
    while (end < samples_.size() && samples_[end].usable &&
           samples_[end].seconds - samples_[end - 1].seconds <= kBreakSeconds) {
      ++end;
    }
    // Sliding sums over [low, high), the records within half a window of the current one.
    double height_sum = 0.0;
    double speed_sum = 0.0;
    auto low = begin;
    auto high = begin;
    for (auto i = begin; i < end; ++i) {
      auto &sample = samples_[i];
      while (high < end && samples_[high].seconds <= sample.seconds + kSmoothSeconds / 2.0) {
        height_sum += samples_[high].height;
        speed_sum += samples_[high].speed;
        ++high;
      }
      while (samples_[low].seconds < sample.seconds - kSmoothSeconds / 2.0) {
        height_sum -= samples_[low].height;
        speed_sum -= samples_[low].speed;
        ++low;
      }
      const auto count = static_cast<double>(high - low);
      sample.smooth_height = height_sum / count;
      sample.smooth_speed = speed_sum / count;
    }
    begin = end;
  }
}

void ProfileView::onPositionChanged() {
  if (cursorX() != painted_cursor_ ||
      (player_ != nullptr ? player_->mapRecord() : std::nullopt) != painted_record_) {
    update();
  }
}

void ProfileView::geometryChange(const QRectF &new_geometry, const QRectF &old_geometry) {
  QQuickPaintedItem::geometryChange(new_geometry, old_geometry);
  if (new_geometry.size() != old_geometry.size()) {
    plot_dirty_ = true;
    update();
  }
}

QRectF ProfileView::plotArea() const {
  return {kLeftMargin, kTopMargin, std::max(1.0, width() - kLeftMargin - kRightMargin),
          std::max(1.0, height() - kTopMargin - kBottomMargin)};
}

void ProfileView::setWindow(const double start, const double span) {
  if (!(duration_ > 0.0))
    return;
  const auto clamped_span = std::clamp(span, std::min(kMinimumWindowSeconds, duration_), duration_);
  const auto clamped_start = std::clamp(start, 0.0, duration_ - clamped_span);
  if (clamped_start == window_start_ && clamped_span == window_span_)
    return;
  window_start_ = clamped_start;
  window_span_ = clamped_span;
  plot_dirty_ = true;
  update();
}

double ProfileView::xForSeconds(const double seconds) const {
  const auto area = plotArea();
  return area.left() + (seconds - window_start_) / window_span_ * area.width();
}

double ProfileView::secondsForX(const double x) const {
  const auto area = plotArea();
  return window_start_ + std::clamp((x - area.left()) / area.width(), 0.0, 1.0) * window_span_;
}

double ProfileView::value(const Sample &sample) const {
  if (quantity_ == Height)
    return smoothed_ ? sample.smooth_height : sample.height;
  return smoothed_ ? sample.smooth_speed : sample.speed;
}

std::optional<double> ProfileView::cursorX() const {
  if (player_ == nullptr || !player_->hasMedia() || !(window_span_ > 0.0))
    return std::nullopt;
  const auto position = std::clamp(player_->position(), 0.0, duration_);
  if (position < window_start_ || position > window_start_ + window_span_)
    return std::nullopt;
  // Whole pixels: a repaint is only worth it when the cursor visibly moves.
  return std::round(xForSeconds(position));
}

std::optional<QPointF> ProfileView::markerPoint() const {
  if (player_ == nullptr || !(window_span_ > 0.0) || !(maximum_ > minimum_))
    return std::nullopt;
  const auto record = player_->mapRecord();
  if (!record.has_value() || *record >= samples_.size() || !samples_[*record].usable)
    return std::nullopt;
  if (player_->mapState() != QStringLiteral("valid"))
    return std::nullopt;
  const auto area = plotArea();
  const auto &sample = samples_[*record];
  if (sample.seconds < window_start_ || sample.seconds > window_start_ + window_span_)
    return std::nullopt;
  return QPointF(xForSeconds(sample.seconds), area.bottom() - (value(sample) - minimum_) /
                                                                  (maximum_ - minimum_) *
                                                                  area.height());
}

void ProfileView::renderPlot() {
  plot_dirty_ = false;
  const auto ratio = window() != nullptr ? window()->effectiveDevicePixelRatio() : 1.0;
  const QSize pixels(std::max(1, static_cast<int>(std::ceil(width() * ratio))),
                     std::max(1, static_cast<int>(std::ceil(height() * ratio))));
  plot_ = QImage(pixels, QImage::Format_ARGB32_Premultiplied);
  plot_.setDevicePixelRatio(ratio);
  plot_.fill(kBackground);

  QPainter painter(&plot_);
  painter.setRenderHint(QPainter::Antialiasing);
  auto font = painter.font();
  font.setPixelSize(10);
  painter.setFont(font);
  const auto area = plotArea();
  painter.fillRect(area, kPlotBackground);

  auto title =
      quantity_ == Height ? QStringLiteral("Height (WGS 84), m") : QStringLiteral("Speed 2D, km/h");
  if (smoothed_)
    title += QStringLiteral(" \u00b7 %1 s mean").arg(kSmoothSeconds);
  painter.setPen(kAxisText);
  painter.drawText(QRectF(area.left(), 0.0, area.width(), kTopMargin),
                   Qt::AlignLeft | Qt::AlignVCenter, title);

  // The range comes from the raw values even when smoothed, so toggling keeps the axis.
  auto low = std::numeric_limits<double>::infinity();
  auto high = -std::numeric_limits<double>::infinity();
  for (const auto &sample : samples_) {
    if (!sample.usable)
      continue;
    const auto raw = quantity_ == Height ? sample.height : sample.speed;
    low = std::min(low, raw);
    high = std::max(high, raw);
  }
  if (!(window_span_ > 0.0) || !std::isfinite(low)) {
    minimum_ = maximum_ = 0.0;
    const auto message = player_ == nullptr || !player_->hasMedia() ? QStringLiteral("No recording")
                         : player_->metadataLoading() ? QStringLiteral("Loading GPS telemetry…")
                         : samples_.empty()           ? QStringLiteral("No GPS in this recording")
                                            : QStringLiteral("No valid GPS fix in this recording");
    painter.drawText(area, Qt::AlignCenter, message);
    return;
  }

  // The value axis covers the whole recording, so it stays put while zooming and panning.
  // Speed starts at zero; height gets a margin so a flat stretch does not sit on the frame.
  if (quantity_ == Speed)
    low = 0.0;
  const auto spread = std::max(high - low, quantity_ == Height ? 10.0 : 5.0);
  const auto middle = (high + low) / 2.0;
  if (quantity_ == Height) {
    low = middle - spread * 0.55;
    high = middle + spread * 0.55;
  } else {
    high = low + spread * 1.08;
  }
  const auto step = niceStep(high - low, 4);
  minimum_ = std::floor(low / step) * step;
  maximum_ = std::ceil(high / step) * step;
  const auto toY = [&](const double v) {
    return area.bottom() - (v - minimum_) / (maximum_ - minimum_) * area.height();
  };

  for (auto v = minimum_; v <= maximum_ + step / 2.0; v += step) {
    const auto y = std::round(toY(v)) + 0.5;
    painter.setPen(kGrid);
    painter.drawLine(QPointF(area.left(), y), QPointF(area.right(), y));
    painter.setPen(kAxisText);
    painter.drawText(QRectF(0.0, y - 8.0, kLeftMargin - 5.0, 16.0),
                     Qt::AlignRight | Qt::AlignVCenter,
                     QString::number(v, 'f', step < 1.0 ? 1 : 0));
  }
  const auto seconds_step =
      timeStep(window_span_, std::max(2, static_cast<int>(area.width() / 70.0)));
  const auto window_end = window_start_ + window_span_;
  for (auto s = std::ceil(window_start_ / seconds_step - 1e-9) * seconds_step;
       s <= window_end + 1e-9; s += seconds_step) {
    const auto x = std::round(xForSeconds(s)) + 0.5;
    painter.setPen(kGrid);
    painter.drawLine(QPointF(x, area.top()), QPointF(x, area.bottom()));
    painter.setPen(kAxisText);
    // Labels at the plot's edges stay inside it; elsewhere they are centred on the line.
    const auto at_left = x - area.left() < 20.0;
    const auto at_right = area.right() - x < 20.0;
    const auto left = at_left ? x : at_right ? x - 60.0 : x - 30.0;
    const auto alignment = at_left ? Qt::AlignLeft : at_right ? Qt::AlignRight : Qt::AlignHCenter;
    painter.drawText(QRectF(left, area.bottom() + 2.0, 60.0, kBottomMargin - 2.0),
                     static_cast<int>(alignment | Qt::AlignVCenter), timeLabel(s));
  }

  QPainterPath path;
  bool open = false;
  double last_seconds = 0.0;
  for (const auto &sample : samples_) {
    if (!sample.usable) {
      open = false;
      continue;
    }
    const QPointF point(xForSeconds(sample.seconds), toY(value(sample)));
    if (open && sample.seconds - last_seconds <= kBreakSeconds) {
      path.lineTo(point);
    } else {
      path.moveTo(point);
    }
    open = true;
    last_seconds = sample.seconds;
  }
  painter.setClipRect(area);
  painter.setPen(QPen(kLine, 1.5));
  painter.drawPath(path);
}

void ProfileView::paint(QPainter *painter) {
  if (plot_dirty_)
    renderPlot();
  painter->drawImage(QPointF(0.0, 0.0), plot_);
  painted_cursor_ = cursorX();
  painted_record_ = player_ != nullptr ? player_->mapRecord() : std::nullopt;
  if (!painted_cursor_.has_value())
    return;
  const auto area = plotArea();
  painter->setPen(QPen(kCursor, 1.0));
  painter->drawLine(QPointF(*painted_cursor_ + 0.5, area.top()),
                    QPointF(*painted_cursor_ + 0.5, area.bottom()));
  if (const auto marker = markerPoint()) {
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setPen(QPen(Qt::white, 1.5));
    painter->setBrush(kCursor);
    painter->drawEllipse(*marker, 3.5, 3.5);
  }
}

void ProfileView::seekTo(const double x) {
  if (player_ == nullptr || !(duration_ > 0.0))
    return;
  player_->seekFraction(secondsForX(x) / duration_);
}

void ProfileView::mousePressEvent(QMouseEvent *event) {
  if (player_ == nullptr || !player_->hasMedia()) {
    event->ignore();
    return;
  }
  // A scrolling parent must not take over the drag.
  setKeepMouseGrab(true);
  if (event->button() == Qt::MiddleButton) {
    panning_ = true;
    pan_press_x_ = event->position().x();
    pan_press_start_ = window_start_;
  } else {
    scrubbing_ = true;
    player_->beginScrub();
    seekTo(event->position().x());
  }
  event->accept();
}

void ProfileView::mouseMoveEvent(QMouseEvent *event) {
  if (panning_) {
    const auto dx = event->position().x() - pan_press_x_;
    setWindow(pan_press_start_ - dx / plotArea().width() * window_span_, window_span_);
  } else if (scrubbing_) {
    seekTo(event->position().x());
  }
  event->accept();
}

void ProfileView::mouseReleaseEvent(QMouseEvent *event) {
  mouseUngrabEvent();
  event->accept();
}

void ProfileView::mouseUngrabEvent() {
  setKeepMouseGrab(false);
  panning_ = false;
  if (!scrubbing_)
    return;
  scrubbing_ = false;
  if (player_ != nullptr)
    player_->endScrub();
}

void ProfileView::wheelEvent(QWheelEvent *event) {
  // Taken even at the zoom limits, so the wheel never scrolls the panel from over the graph.
  event->accept();
  if (!(window_span_ > 0.0) || panning_)
    return;
  const auto steps = event->angleDelta().y() / 120.0;
  if (steps == 0.0)
    return;
  const auto area = plotArea();
  const auto fraction = std::clamp((event->position().x() - area.left()) / area.width(), 0.0, 1.0);
  const auto anchor = window_start_ + fraction * window_span_;
  const auto span = std::clamp(window_span_ / std::pow(kZoomPerNotch, steps),
                               std::min(kMinimumWindowSeconds, duration_), duration_);
  setWindow(anchor - fraction * span, span);
}

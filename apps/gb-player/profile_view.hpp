#pragma once

#include <QImage>
#include <QPointer>
#include <QQuickPaintedItem>

#include <optional>
#include <vector>

class PlayerController;

// Height or 2D speed of the recording's GPS records against media time, with a cursor at the
// displayed frame and a dot on the record the map marker shows. Records without a valid fix are
// left out, and the line breaks across them and across time steps longer than a few seconds.
// The time axis shows a window of the video: the whole of it at first, down to 30 seconds with the
// wheel, which keeps the time under the pointer fixed; dragging with the middle button pans. The
// window never extends past the start or end of the video. Pressing the left button seeks to the
// time under the pointer; dragging scrubs. The plot is drawn once per data, size or window change
// and cached; the cursor is painted over the cached image.
class ProfileView : public QQuickPaintedItem {
  Q_OBJECT
  Q_PROPERTY(QObject *controller READ controller WRITE setController NOTIFY controllerChanged)
  Q_PROPERTY(Quantity quantity READ quantity WRITE setQuantity NOTIFY quantityChanged)

public:
  enum Quantity { Height, Speed };
  Q_ENUM(Quantity)

  explicit ProfileView(QQuickItem *parent = nullptr);

  [[nodiscard]] QObject *controller() const;
  void setController(QObject *controller);
  [[nodiscard]] Quantity quantity() const { return quantity_; }
  void setQuantity(Quantity quantity);

  void paint(QPainter *painter) override;

signals:
  void controllerChanged();
  void quantityChanged();

protected:
  void geometryChange(const QRectF &new_geometry, const QRectF &old_geometry) override;
  void mousePressEvent(QMouseEvent *event) override;
  void mouseMoveEvent(QMouseEvent *event) override;
  void mouseReleaseEvent(QMouseEvent *event) override;
  void mouseUngrabEvent() override;
  void wheelEvent(QWheelEvent *event) override;

private:
  struct Sample {
    double seconds{};
    double height{};  // WGS 84 altitude, metres
    double speed{};   // 2D speed, km/h
    bool usable{};
  };

  void onDataChanged();
  void onPositionChanged();
  void seekTo(double x);
  // Sets the visible time window, clamped to the video and to the zoom limits.
  void setWindow(double start, double span);
  [[nodiscard]] double xForSeconds(double seconds) const;
  [[nodiscard]] double secondsForX(double x) const;
  void renderPlot();
  [[nodiscard]] QRectF plotArea() const;
  [[nodiscard]] double value(const Sample &sample) const;
  [[nodiscard]] std::optional<double> cursorX() const;
  [[nodiscard]] std::optional<QPointF> markerPoint() const;

  QPointer<PlayerController> player_;
  Quantity quantity_{Height};
  std::vector<Sample> samples_;
  double duration_{};
  double window_start_{};  // visible time window, seconds
  double window_span_{};
  double minimum_{};  // value range of the plot area
  double maximum_{};
  QImage plot_;
  bool plot_dirty_{true};
  std::optional<double> painted_cursor_;
  std::optional<std::size_t> painted_record_;
  bool scrubbing_{};
  bool panning_{};
  double pan_press_x_{};
  double pan_press_start_{};
};

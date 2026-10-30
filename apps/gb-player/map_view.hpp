#pragma once

#include "gb/geo/mercator.hpp"
#include "gb/geo/tiles.hpp"

#include <QCache>
#include <QElapsedTimer>
#include <QImage>
#include <QPointer>
#include <QQuickPaintedItem>

#include <atomic>
#include <map>
#include <optional>
#include <set>
#include <vector>

class PlayerController;

// Slippy map of the recording's GPS route with the current position. Tiles come from the
// controller's TileFetcher; only tiles covering the visible area (and their cached ancestors as
// placeholders) are ever requested. The route is drawn per run of valid fixes, so invalid
// stretches show as breaks. Dragging pans and stops following; a click without a drag reports the
// position so QML can offer seek targets.
class MapView : public QQuickPaintedItem {
  Q_OBJECT
  Q_PROPERTY(QObject *controller READ controller WRITE setController NOTIFY controllerChanged)
  Q_PROPERTY(double zoom READ zoom WRITE setZoom NOTIFY viewChanged)
  Q_PROPERTY(double minimumZoom READ minimumZoom NOTIFY controllerChanged)
  Q_PROPERTY(double maximumZoom READ maximumZoom NOTIFY controllerChanged)
  Q_PROPERTY(bool follow READ follow WRITE setFollow NOTIFY followChanged)
  Q_PROPERTY(QString tileStatus READ tileStatus NOTIFY tileStatusChanged)
  Q_PROPERTY(bool hasRoute READ hasRoute NOTIFY routeChanged)

public:
  explicit MapView(QQuickItem *parent = nullptr);

  [[nodiscard]] QObject *controller() const;
  void setController(QObject *controller);
  [[nodiscard]] double zoom() const { return zoom_; }
  void setZoom(double zoom);
  [[nodiscard]] double minimumZoom() const;
  [[nodiscard]] double maximumZoom() const;
  [[nodiscard]] bool follow() const { return follow_; }
  void setFollow(bool follow);
  [[nodiscard]] QString tileStatus() const { return tile_status_; }
  [[nodiscard]] bool hasRoute() const { return !route_.empty(); }
  // GPS record index of the marker in the most recent paint, or -1 when none was drawn. Used by
  // the benchmark to check that the drawn map follows the displayed frame.
  [[nodiscard]] long long paintedRecord() const { return painted_record_.load(); }
  [[nodiscard]] std::uint64_t paintCount() const { return paint_count_.load(); }

  Q_INVOKABLE void fitRoute();
  Q_INVOKABLE void zoomBy(double steps);

  void paint(QPainter *painter) override;

signals:
  void controllerChanged();
  void viewChanged();
  void followChanged();
  void tileStatusChanged();
  void routeChanged();
  void routeClicked(double latitude, double longitude, double toleranceMetres);

protected:
  void geometryChange(const QRectF &new_geometry, const QRectF &old_geometry) override;
  void mousePressEvent(QMouseEvent *event) override;
  void mouseMoveEvent(QMouseEvent *event) override;
  void mouseReleaseEvent(QMouseEvent *event) override;
  void mouseDoubleClickEvent(QMouseEvent *event) override;
  void wheelEvent(QWheelEvent *event) override;

private:
  struct Viewport {
    int tile_zoom{};
    double world_pixels{};  // size of the whole world in item pixels at the current zoom
    double tile_pixels{};
    double left{};          // world pixel at the item's left edge
    double top{};
  };

  [[nodiscard]] Viewport viewport() const;
  [[nodiscard]] std::vector<gb::geo::TileKey> visibleTiles(const Viewport &view) const;
  void onRouteChanged();
  void onPositionChanged();
  void onTileReady(int zoom, int x, int y, const QByteArray &data);
  void onTileUnavailable(int zoom, int x, int y, const QString &reason);
  void refreshTiles();
  void updateTileStatus();
  void zoomAround(double new_zoom, QPointF anchor);
  [[nodiscard]] const QImage *tileImage(gb::geo::TileKey key) const;

  QPointer<PlayerController> player_;
  gb::geo::WorldPoint center_{0.5, 0.5};
  double zoom_{2.0};
  bool follow_{true};
  bool fit_pending_{};
  QString tile_status_;

  std::vector<gb::geo::WorldPoint> route_;  // projected record positions, same indices as records
  std::vector<bool> usable_;
  QCache<quint64, QImage> images_;
  std::set<gb::geo::TileKey> pending_;
  std::map<gb::geo::TileKey, qint64> unavailable_;  // key -> time of failure (ms)
  int missing_tiles_{};
  QString last_unavailable_reason_;
  QElapsedTimer clock_;

  QPointF press_position_;
  QPointF last_position_;
  bool dragging_{};

  std::atomic<long long> painted_record_{-1};
  std::atomic<std::uint64_t> paint_count_{};
};

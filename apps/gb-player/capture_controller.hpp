#pragma once

#include <QCache>
#include <QElapsedTimer>
#include <QImage>
#include <QObject>
#include <QPointer>
#include <QRectF>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantList>

#include <memory>
#include <optional>

class MapView;
class PlayerController;
class QPainter;
class VideoView;

// Q_OBJECT headers avoid <filesystem>: Qt 6.2 moc cannot parse the GCC 11 header.
namespace gb::image {
struct PhotoGps;
}
namespace gb::render {
class OffscreenRenderer;
struct PanelContent;
} // namespace gb::render

// Capture mode (docs/plans/export/capture.md): the frozen canvas, the frame's aspect ratio and
// side panel, the capture rectangle over the canvas, stills of the displayed frame, and the
// framing shown as gb-render settings. The rectangle is kept in canvas units (0..1 on each axis),
// so it keeps its place on the picture when the window is resized.
class CaptureController final : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
  Q_PROPERTY(QObject *view READ view WRITE setView NOTIFY viewObjectChanged)
  // The canvas: frozen when Capture mode is first entered; square while `square` is set.
  Q_PROPERTY(double canvasAspect READ canvasAspect NOTIFY canvasChanged)
  Q_PROPERTY(bool square READ square WRITE setSquare NOTIFY canvasChanged)

  Q_PROPERTY(QStringList aspectNames READ aspectNames CONSTANT)
  Q_PROPERTY(int aspectIndex READ aspectIndex WRITE setAspectIndex NOTIFY frameChanged)
  Q_PROPERTY(int freeWidth READ freeWidth WRITE setFreeWidth NOTIFY frameChanged)
  Q_PROPERTY(int freeHeight READ freeHeight WRITE setFreeHeight NOTIFY frameChanged)
  Q_PROPERTY(QString aspectText READ aspectText NOTIFY frameChanged)
  Q_PROPERTY(QString panelSide READ panelSide WRITE setPanelSide NOTIFY frameChanged)
  Q_PROPERTY(double panelSize READ panelSize WRITE setPanelSize NOTIFY frameChanged)
  // What the panel shows: the route map, at the map pane's zoom, and the telemetry text.
  Q_PROPERTY(bool panelMap READ panelMap WRITE setPanelMap NOTIFY frameChanged)
  Q_PROPERTY(bool panelTelemetry READ panelTelemetry WRITE setPanelTelemetry NOTIFY frameChanged)
  Q_PROPERTY(QObject *mapView READ mapView WRITE setMapView NOTIFY viewObjectChanged)
  // True while a capture waits for map tiles.
  Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
  Q_PROPERTY(double pictureAspect READ pictureAspect NOTIFY frameChanged)
  Q_PROPERTY(QRectF crop READ crop NOTIFY cropChanged)

  Q_PROPERTY(QVariantList widthPresets READ widthPresets NOTIFY frameChanged)
  Q_PROPERTY(QStringList widthPresetNames READ widthPresetNames NOTIFY frameChanged)
  Q_PROPERTY(int outputWidth READ outputWidth WRITE setOutputWidth NOTIFY frameChanged)
  Q_PROPERTY(QString outputSizeText READ outputSizeText NOTIFY frameChanged)
  Q_PROPERTY(bool png READ png WRITE setPng NOTIFY outputChanged)
  Q_PROPERTY(int jpegQuality READ jpegQuality WRITE setJpegQuality NOTIFY outputChanged)
  Q_PROPERTY(bool gpsExif READ gpsExif WRITE setGpsExif NOTIFY outputChanged)
  Q_PROPERTY(bool gpsAvailable READ gpsAvailable NOTIFY gpsChanged)
  Q_PROPERTY(QString gpsReason READ gpsReason NOTIFY gpsChanged)
  Q_PROPERTY(QString folder READ folder NOTIFY outputChanged)
  Q_PROPERTY(QString resolutionWarning READ resolutionWarning NOTIFY settingsChanged)

  Q_PROPERTY(bool hasRange READ hasRange NOTIFY rangeChanged)
  Q_PROPERTY(QString positionText READ positionText NOTIFY rangeChanged)
  Q_PROPERTY(QString startText READ startText NOTIFY rangeChanged)
  Q_PROPERTY(QString endText READ endText NOTIFY rangeChanged)
  Q_PROPERTY(QString settingsText READ settingsText NOTIFY settingsChanged)

public:
  CaptureController(PlayerController &player, QString version, QObject *parent = nullptr);
  ~CaptureController() override;

  [[nodiscard]] bool active() const { return active_; }
  void setActive(bool active);
  [[nodiscard]] QObject *view() const;
  void setView(QObject *view);
  [[nodiscard]] double canvasAspect() const;
  [[nodiscard]] bool square() const { return square_; }
  void setSquare(bool square);

  [[nodiscard]] QStringList aspectNames() const;
  [[nodiscard]] int aspectIndex() const { return aspect_index_; }
  void setAspectIndex(int index);
  [[nodiscard]] int freeWidth() const { return free_width_; }
  void setFreeWidth(int value);
  [[nodiscard]] int freeHeight() const { return free_height_; }
  void setFreeHeight(int value);
  [[nodiscard]] QString aspectText() const;
  [[nodiscard]] QString panelSide() const;
  void setPanelSide(const QString &side);
  [[nodiscard]] double panelSize() const { return panel_size_; }
  void setPanelSize(double size);
  [[nodiscard]] bool panelMap() const { return panel_map_; }
  void setPanelMap(bool enabled);
  [[nodiscard]] bool panelTelemetry() const { return panel_telemetry_; }
  void setPanelTelemetry(bool enabled);
  [[nodiscard]] QObject *mapView() const;
  void setMapView(QObject *map);
  [[nodiscard]] bool busy() const { return prepared_ != nullptr; }
  [[nodiscard]] double pictureAspect() const;
  [[nodiscard]] QRectF crop() const { return crop_; }

  [[nodiscard]] QVariantList widthPresets() const;
  [[nodiscard]] QStringList widthPresetNames() const;
  [[nodiscard]] int outputWidth() const { return output_width_; }
  void setOutputWidth(int width);
  [[nodiscard]] QString outputSizeText() const;
  [[nodiscard]] bool png() const { return png_; }
  void setPng(bool png);
  [[nodiscard]] int jpegQuality() const { return jpeg_quality_; }
  void setJpegQuality(int quality);
  [[nodiscard]] bool gpsExif() const { return gps_exif_; }
  void setGpsExif(bool enabled);
  [[nodiscard]] bool gpsAvailable() const { return gps_available_; }
  [[nodiscard]] QString gpsReason() const { return gps_reason_; }
  [[nodiscard]] QString folder() const { return folder_; }
  [[nodiscard]] QString resolutionWarning() const { return resolution_warning_; }

  [[nodiscard]] bool hasRange() const;
  [[nodiscard]] QString positionText() const;
  [[nodiscard]] QString startText() const { return start_.value_or(QString()); }
  [[nodiscard]] QString endText() const { return end_.value_or(QString()); }
  [[nodiscard]] QString settingsText() const { return settings_text_; }

  // Freezes the canvas at the aspect ratio of the area the view has, the first time it is called
  // in a session.
  Q_INVOKABLE void freezeCanvas(double available_aspect);
  // Rectangle editing, in canvas units. Corners: 0 top left, 1 top right, 2 bottom right,
  // 3 bottom left; (x, y) is where the dragged corner should go.
  Q_INVOKABLE void moveCrop(double dx, double dy);
  Q_INVOKABLE void dragCorner(int corner, double x, double y);
  Q_INVOKABLE void scaleCrop(double factor);
  Q_INVOKABLE void resetCrop();

  Q_INVOKABLE void setStart();
  Q_INVOKABLE void setEnd();
  Q_INVOKABLE void clearRange();

  // Captures the displayed frame, pausing playback first. With askName, asks for the file name;
  // otherwise writes a suggested name in the capture folder, never replacing a file.
  Q_INVOKABLE void capture(bool ask_name);
  Q_INVOKABLE void chooseFolder();
  Q_INVOKABLE void copySettings();
  Q_INVOKABLE void saveSettingsAs();

  // Writes a capture to `path` (format from the extension) and returns the warnings, for tests
  // and the GUI. Map tiles not yet available are drawn as placeholders and reported. Throws
  // std::runtime_error on failure.
  QStringList captureTo(const QString &path);

  // Captures the displayed frame to `path` once the panel's map tiles have arrived or the wait
  // has run out; reports the outcome with captureFinished. False when a capture is already
  // waiting.
  bool startCapture(const QString &path);

  // Draws the panel of the output frame scaled into `size`, for the preview next to the
  // rectangle. Nothing without a panel.
  void paintPanelPreview(QPainter &painter, QSizeF size);

signals:
  void activeChanged();
  void viewObjectChanged();
  void canvasChanged();
  void frameChanged();
  void cropChanged();
  void outputChanged();
  void gpsChanged();
  void rangeChanged();
  void settingsChanged();
  void busyChanged();
  // A capture started with startCapture or capture is written (`error` empty) or failed.
  void captureFinished(const QString &path, const QStringList &warnings, const QString &error);
  // The panel's contents changed: the position, the map or a tile.
  void panelChanged();

private:
  [[nodiscard]] double currentPictureAspect() const;
  void refitCrop();
  void setCrop(const QRectF &crop);
  void updateGps();
  void updateSettings();
  [[nodiscard]] QString suggestedPath() const;
  struct Prepared;
  [[nodiscard]] gb::render::PanelContent panelContent() const;
  // Renders the picture and takes the panel's contents at the displayed frame.
  [[nodiscard]] std::unique_ptr<Prepared> prepareCapture(const QString &path);
  // Draws the panel with the tiles available now and writes the still; returns the warnings.
  QStringList completeCapture(Prepared &prepared);
  // Requests the tiles of the panel's map that are not cached; returns how many are awaited.
  int requestPanelTiles(const Prepared &prepared);
  void onTileAnswered(int zoom, int x, int y, const QByteArray *data);
  void finishCapture();
  void cancelCapture(const QString &reason);

  PlayerController &player_;
  QString version_;
  QPointer<VideoView> view_;
  bool active_{};
  std::optional<double> frozen_aspect_;
  bool square_{};
  int aspect_index_{};
  int free_width_{2};
  int free_height_{1};
  int panel_side_{};
  double panel_size_{0.32};
  bool panel_map_{true};
  bool panel_telemetry_{true};
  QPointer<MapView> map_;
  QRectF crop_{0.0, 0.0, 1.0, 1.0};
  bool crop_set_{};
  int output_width_{1920};
  bool png_{};
  int jpeg_quality_{92};
  bool gps_exif_{};
  bool gps_available_{};
  QString gps_reason_;
  std::shared_ptr<const gb::image::PhotoGps> gps_fix_;
  QString folder_;
  QString resolution_warning_;
  std::optional<QString> start_;
  std::optional<QString> end_;
  QString settings_text_;
  std::unique_ptr<gb::render::OffscreenRenderer> renderer_;

  // Tiles for the panel's map, fetched for captures; the map pane's cache is used first.
  QCache<quint64, QImage> tiles_{256};
  QSet<quint64> unavailable_;          // answered without an image during the current capture
  std::unique_ptr<Prepared> prepared_; // a capture waiting for tiles
  QTimer tile_timer_;
  QElapsedTimer tile_clock_;
};

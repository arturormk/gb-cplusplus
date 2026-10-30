#pragma once

#include <QPointer>
#include <QQuickItem>

class PlaybackEngine;

// Displays the engine's current frame set through an OpenGL scene-graph render node. Angles are
// degrees; zoom and pan apply to the 2D diagnostic modes.
class VideoView : public QQuickItem {
  Q_OBJECT
  Q_PROPERTY(QObject *engine READ engine WRITE setEngine NOTIFY engineChanged)
  Q_PROPERTY(int mode READ mode WRITE setMode NOTIFY viewChanged)
  Q_PROPERTY(int sourceLayout READ sourceLayout WRITE setSourceLayout NOTIFY viewChanged)
  Q_PROPERTY(int overlap READ overlap WRITE setOverlap NOTIFY viewChanged)
  Q_PROPERTY(double yaw READ yaw WRITE setYaw NOTIFY viewChanged)
  Q_PROPERTY(double pitch READ pitch WRITE setPitch NOTIFY viewChanged)
  Q_PROPERTY(double roll READ roll WRITE setRoll NOTIFY viewChanged)
  Q_PROPERTY(double fieldOfView READ fieldOfView WRITE setFieldOfView NOTIFY viewChanged)
  Q_PROPERTY(double zoom READ zoom WRITE setZoom NOTIFY viewChanged)
  Q_PROPERTY(double panX READ panX WRITE setPanX NOTIFY viewChanged)
  Q_PROPERTY(double panY READ panY WRITE setPanY NOTIFY viewChanged)
  Q_PROPERTY(bool gpuAvailable READ gpuAvailable CONSTANT)

public:
  enum Mode {
    Rectilinear = 0,
    Fisheye = 1,
    Stereographic = 2,
    LittlePlanet = 3,
    Panorama = 4,
    RawFirst = 5,
    RawSecond = 6,
    Original = 7
  };
  Q_ENUM(Mode)

  explicit VideoView(QQuickItem *parent = nullptr);

  [[nodiscard]] QObject *engine() const;
  void setEngine(QObject *engine);
  [[nodiscard]] int mode() const { return mode_; }
  void setMode(int value);
  [[nodiscard]] int sourceLayout() const { return source_layout_; }
  void setSourceLayout(int value);
  [[nodiscard]] int overlap() const { return overlap_; }
  void setOverlap(int value);
  [[nodiscard]] double yaw() const { return yaw_; }
  void setYaw(double value);
  [[nodiscard]] double pitch() const { return pitch_; }
  void setPitch(double value);
  [[nodiscard]] double roll() const { return roll_; }
  void setRoll(double value);
  [[nodiscard]] double fieldOfView() const { return field_of_view_; }
  void setFieldOfView(double value);
  [[nodiscard]] double zoom() const { return zoom_; }
  void setZoom(double value);
  [[nodiscard]] double panX() const { return pan_x_; }
  void setPanX(double value);
  [[nodiscard]] double panY() const { return pan_y_; }
  void setPanY(double value);
  [[nodiscard]] static bool gpuAvailable();

signals:
  void engineChanged();
  void viewChanged();

protected:
  QSGNode *updatePaintNode(QSGNode *old, UpdatePaintNodeData *data) override;

private:
  void setValue(double &field, double value);
  void attachWindow(QQuickWindow *window);
  void connectEngine();

  QPointer<PlaybackEngine> engine_;
  QMetaObject::Connection animation_connection_;
  int mode_{Rectilinear};
  int source_layout_{};
  int overlap_{};
  double yaw_{};
  double pitch_{};
  double roll_{};
  double field_of_view_{90.0};
  double zoom_{1.0};
  double pan_x_{};
  double pan_y_{};
};

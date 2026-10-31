#pragma once

#include <QPointer>
#include <QQuickPaintedItem>

class CaptureController;

// The side panel of a capture, drawn as the still will have it, scaled down to the item. Shown next
// to the capture rectangle in Capture mode.
class PanelPreview : public QQuickPaintedItem {
  Q_OBJECT
  Q_PROPERTY(QObject *controller READ controller WRITE setController NOTIFY controllerChanged)

public:
  explicit PanelPreview(QQuickItem *parent = nullptr);

  [[nodiscard]] QObject *controller() const;
  void setController(QObject *capture);

  void paint(QPainter *painter) override;

signals:
  void controllerChanged();

private:
  QPointer<CaptureController> capture_;
};

#include "panel_preview.hpp"

#include "capture_controller.hpp"

#include <QPainter>

PanelPreview::PanelPreview(QQuickItem *parent) : QQuickPaintedItem(parent) {
  setOpaquePainting(true);
  setFillColor(Qt::black);
}

QObject *PanelPreview::controller() const { return capture_; }

void PanelPreview::setController(QObject *capture) {
  auto *typed = qobject_cast<CaptureController *>(capture);
  if (typed == capture_)
    return;
  if (capture_ != nullptr)
    capture_->disconnect(this);
  capture_ = typed;
  if (capture_ != nullptr) {
    connect(capture_, &CaptureController::panelChanged, this, [this] { update(); });
    connect(capture_, &CaptureController::frameChanged, this, [this] { update(); });
  }
  emit controllerChanged();
  update();
}

void PanelPreview::paint(QPainter *painter) {
  if (capture_ != nullptr)
    capture_->paintPanelPreview(*painter, size());
}

#pragma once

#include <QHash>
#include <QImage>
#include <QMutex>
#include <QObject>
#include <QQuickImageProvider>
#include <QString>

#include <memory>

namespace gb::media {
struct RgbFrame;
}

class FrameImageProvider final : public QQuickImageProvider {
public:
  FrameImageProvider();
  QImage requestImage(const QString &id, QSize *size, const QSize &requested_size) override;
  void setImage(const QString &id, QImage image);

private:
  mutable QMutex mutex_;
  QHash<QString, QImage> images_;
};

class ProjectController final : public QObject {
  Q_OBJECT
  Q_PROPERTY(QString inputPath READ inputPath NOTIFY inputChanged)
  Q_PROPERTY(QString inputName READ inputName NOTIFY inputChanged)
  Q_PROPERTY(QString status READ status NOTIFY statusChanged)
  Q_PROPERTY(QString rawFrontSource READ rawFrontSource NOTIFY imagesChanged)
  Q_PROPERTY(QString rawRearSource READ rawRearSource NOTIFY imagesChanged)
  Q_PROPERTY(QString panoramaSource READ panoramaSource NOTIFY imagesChanged)
  Q_PROPERTY(QString outputSource READ outputSource NOTIFY imagesChanged)
  Q_PROPERTY(bool ready READ ready NOTIFY imagesChanged)

public:
  explicit ProjectController(FrameImageProvider *provider, QObject *parent = nullptr);
  ~ProjectController() override;

  [[nodiscard]] QString inputPath() const;
  [[nodiscard]] QString inputName() const;
  [[nodiscard]] QString status() const;
  [[nodiscard]] QString rawFrontSource() const;
  [[nodiscard]] QString rawRearSource() const;
  [[nodiscard]] QString panoramaSource() const;
  [[nodiscard]] QString outputSource() const;
  [[nodiscard]] bool ready() const;

  Q_INVOKABLE void openFile(double seconds, int panorama_width);
  Q_INVOKABLE void loadFile(const QString &path, double seconds, int panorama_width);
  Q_INVOKABLE void saveProjection(int mode, double yaw, double pitch, double roll,
                                  double field_of_view);
  Q_INVOKABLE void updateProjection(int mode, double yaw, double pitch, double roll,
                                    double field_of_view);

signals:
  void inputChanged();
  void statusChanged();
  void imagesChanged();

private:
  static QImage image(const gb::media::RgbFrame &frame);
  void setStatus(QString value);
  void publish(const gb::media::RgbFrame &front, const gb::media::RgbFrame &rear,
               const gb::media::RgbFrame &panorama, const gb::media::RgbFrame &output);
  void publishOutput(const gb::media::RgbFrame &output);

  FrameImageProvider *provider_{};
  QString input_path_;
  QString status_{"Open a MAX .360 file to begin"};
  QString raw_front_source_;
  QString raw_rear_source_;
  QString panorama_source_;
  QString output_source_;
  std::unique_ptr<gb::media::RgbFrame> panorama_;
  int revision_{};
};

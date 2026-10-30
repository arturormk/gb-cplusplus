#pragma once

#include "playback_engine.hpp"
#include "process_usage.hpp"

#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <thread>

struct PlayerOptions {
  int decoder_threads{0};
  std::size_t queue_capacity{4};
};

// QML-facing application state: file, transport, telemetry, metadata and diagnostics.
class PlayerController final : public QObject {
  Q_OBJECT
  Q_PROPERTY(QString displayName READ displayName CONSTANT)
  Q_PROPERTY(QObject *engine READ engine CONSTANT)
  Q_PROPERTY(QString inputPath READ inputPath NOTIFY fileChanged)
  Q_PROPERTY(QString inputName READ inputName NOTIFY fileChanged)
  Q_PROPERTY(bool hasMedia READ hasMedia NOTIFY fileChanged)
  Q_PROPERTY(int sourceLayout READ sourceLayout NOTIFY fileChanged)
  Q_PROPERTY(QString layoutDescription READ layoutDescription NOTIFY fileChanged)
  Q_PROPERTY(int overlap READ overlap NOTIFY fileChanged)
  Q_PROPERTY(double duration READ duration NOTIFY fileChanged)
  Q_PROPERTY(QString durationText READ durationText NOTIFY fileChanged)
  Q_PROPERTY(bool playing READ playing NOTIFY playingChanged)
  Q_PROPERTY(double position READ position NOTIFY positionChanged)
  Q_PROPERTY(QString positionText READ positionText NOTIFY positionChanged)
  Q_PROPERTY(QString ptsText READ ptsText NOTIFY positionChanged)
  Q_PROPERTY(QString status READ status NOTIFY statusChanged)
  Q_PROPERTY(bool statusIsError READ statusIsError NOTIFY statusChanged)
  Q_PROPERTY(QString telemetryState READ telemetryState NOTIFY telemetryChanged)
  Q_PROPERTY(QVariantList telemetryRows READ telemetryRows NOTIFY telemetryChanged)
  Q_PROPERTY(QVariantList timelineSamples READ timelineSamples NOTIFY timelineChanged)
  Q_PROPERTY(QVariantList timelineGaps READ timelineGaps NOTIFY timelineChanged)
  Q_PROPERTY(QVariantMap gpsCoverage READ gpsCoverage NOTIFY timelineChanged)
  Q_PROPERTY(QVariantList fileRows READ fileRows NOTIFY metadataChanged)
  Q_PROPERTY(QVariantList trackRows READ trackRows NOTIFY metadataChanged)
  Q_PROPERTY(QString gpmfTitle READ gpmfTitle NOTIFY gpmfChanged)
  Q_PROPERTY(QVariantList gpmfRows READ gpmfRows NOTIFY gpmfChanged)
  Q_PROPERTY(QVariantList diagnosticsRows READ diagnosticsRows NOTIFY diagnosticsChanged)

public:
  explicit PlayerController(PlayerOptions options, QObject *parent = nullptr);
  ~PlayerController() override;

  [[nodiscard]] QString displayName() const;
  [[nodiscard]] QObject *engine();
  [[nodiscard]] PlaybackEngine &playbackEngine();
  [[nodiscard]] const PlayerOptions &options() const { return options_; }
  [[nodiscard]] QString inputPath() const { return input_path_; }
  [[nodiscard]] QString inputName() const;
  [[nodiscard]] bool hasMedia() const { return engine_.isOpen(); }
  [[nodiscard]] int sourceLayout() const;
  [[nodiscard]] QString layoutDescription() const;
  [[nodiscard]] int overlap() const;
  [[nodiscard]] double duration() const;
  [[nodiscard]] QString durationText() const;
  [[nodiscard]] bool playing() const { return engine_.playing(); }
  [[nodiscard]] double position() const;
  [[nodiscard]] QString positionText() const;
  [[nodiscard]] QString ptsText() const;
  [[nodiscard]] QString status() const { return status_; }
  [[nodiscard]] bool statusIsError() const { return status_is_error_; }
  [[nodiscard]] QString telemetryState() const { return telemetry_state_; }
  [[nodiscard]] QVariantList telemetryRows() const { return telemetry_rows_; }
  [[nodiscard]] QVariantList timelineSamples() const { return timeline_samples_; }
  [[nodiscard]] QVariantList timelineGaps() const { return timeline_gaps_; }
  [[nodiscard]] QVariantMap gpsCoverage() const { return gps_coverage_; }
  [[nodiscard]] QVariantList fileRows() const { return file_rows_; }
  [[nodiscard]] QVariantList trackRows() const { return track_rows_; }
  [[nodiscard]] QString gpmfTitle() const { return gpmf_title_; }
  [[nodiscard]] QVariantList gpmfRows() const { return gpmf_rows_; }
  [[nodiscard]] QVariantList diagnosticsRows() const { return diagnostics_rows_; }
  [[nodiscard]] bool metadataLoading() const { return loader_.joinable() && !metadata_loaded_; }

  Q_INVOKABLE void openDialog();
  Q_INVOKABLE bool open(const QString &path, double start_seconds = 0.0);
  Q_INVOKABLE void togglePlay();
  Q_INVOKABLE void play();
  Q_INVOKABLE void pause();
  Q_INVOKABLE void seekSeconds(double seconds);
  Q_INVOKABLE void seekFraction(double fraction);
  Q_INVOKABLE void stepFrames(int frames);

signals:
  void fileChanged();
  void playingChanged();
  void positionChanged();
  void statusChanged();
  void telemetryChanged();
  void timelineChanged();
  void metadataChanged();
  void gpmfChanged();
  void diagnosticsChanged();
  void metadataReady();

private:
  struct LoadedMetadata;
  struct Source;

  void setStatus(const QString &message, bool error = false);
  void startMetadataLoad(const std::string &path);
  void applyMetadata(std::uint64_t generation, std::shared_ptr<LoadedMetadata> loaded);
  void onFrameChanged();
  void updateTelemetry();
  void updateGpmfSample();
  void updateDiagnostics();
  void buildFileRows();
  [[nodiscard]] std::int64_t ticksForSeconds(double seconds) const;
  [[nodiscard]] QString formatTicks(std::int64_t ticks) const;

  PlayerOptions options_;
  PlaybackEngine engine_;
  std::unique_ptr<Source> source_;
  QString input_path_;
  QString status_;
  bool status_is_error_{};

  std::thread loader_;
  std::uint64_t load_generation_{};
  bool metadata_loaded_{};
  std::shared_ptr<LoadedMetadata> metadata_;
  QString telemetry_state_;
  QVariantList telemetry_rows_;
  QVariantList timeline_samples_;
  QVariantList timeline_gaps_;
  QVariantMap gps_coverage_;
  QVariantList file_rows_;
  QVariantList track_rows_;
  QString gpmf_title_;
  QVariantList gpmf_rows_;
  std::optional<std::size_t> gpmf_sample_;

  QTimer diagnostics_timer_;
  QElapsedTimer diagnostics_clock_;
  PlaybackStatistics previous_statistics_;
  ProcessUsage previous_usage_;
  QVariantList diagnostics_rows_;
};

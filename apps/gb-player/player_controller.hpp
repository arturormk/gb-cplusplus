#pragma once

#include "playback_engine.hpp"
#include "process_usage.hpp"

#include "gb/geo/route.hpp"
#include "gb/telemetry/telemetry.hpp"
#include "gb/tiles/tile_fetcher.hpp"

#include <QElapsedTimer>
#include <QObject>
#include <QRectF>
#include <QString>
#include <QStringList>
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
  bool audio{true};
  // At the end of a chapter during playback, open and play the next one.
  bool continue_sequence{true};
  gb::geo::TileProvider map_provider;
  gb::tiles::FetcherOptions map_tiles;
};

// QML-facing application state: file, transport, telemetry, metadata and diagnostics.
class PlayerController final : public QObject {
  Q_OBJECT
  Q_PROPERTY(QString displayName READ displayName CONSTANT)
  Q_PROPERTY(QObject *engine READ engine CONSTANT)
  Q_PROPERTY(QString inputPath READ inputPath NOTIFY fileChanged)
  Q_PROPERTY(QString inputName READ inputName NOTIFY fileChanged)
  Q_PROPERTY(QStringList sequenceNames READ sequenceNames NOTIFY fileChanged)
  Q_PROPERTY(int sequenceIndex READ sequenceIndex NOTIFY fileChanged)
  Q_PROPERTY(bool hasMedia READ hasMedia NOTIFY fileChanged)
  Q_PROPERTY(bool isPhoto READ isPhoto NOTIFY fileChanged)
  Q_PROPERTY(QRectF panoramaCrop READ panoramaCrop NOTIFY fileChanged)
  Q_PROPERTY(QVariantList photoRows READ photoRows NOTIFY fileChanged)
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
  Q_PROPERTY(bool hasAudio READ hasAudio NOTIFY audioChanged)
  Q_PROPERTY(bool audioMuted READ audioMuted WRITE setAudioMuted NOTIFY audioChanged)
  Q_PROPERTY(QString audioState READ audioState NOTIFY audioChanged)
  Q_PROPERTY(QString mapState READ mapState NOTIFY mapPositionChanged)
  Q_PROPERTY(QString mapMessage READ mapMessage NOTIFY mapPositionChanged)
  Q_PROPERTY(QString mapAttribution READ mapAttribution CONSTANT)
  Q_PROPERTY(QString mapAttributionUrl READ mapAttributionUrl CONSTANT)
  Q_PROPERTY(QString mapProviderName READ mapProviderName CONSTANT)
  Q_PROPERTY(bool exportingTrack READ exportingTrack NOTIFY trackExportChanged)

public:
  explicit PlayerController(PlayerOptions options, QObject *parent = nullptr);
  ~PlayerController() override;

  [[nodiscard]] QString displayName() const;
  [[nodiscard]] QObject *engine();
  [[nodiscard]] PlaybackEngine &playbackEngine();
  [[nodiscard]] const PlayerOptions &options() const { return options_; }
  [[nodiscard]] QString inputPath() const { return input_path_; }
  [[nodiscard]] QString inputName() const;
  // File names of the chapters of the open recording (GSnnmmmm.360 / GHnnmmmm.MP4 /
  // GXnnmmmm.MP4 with the same mmmm), in chapter order, and the open one's index. A file that is not chaptered is alone.
  [[nodiscard]] QStringList sequenceNames() const;
  [[nodiscard]] int sequenceIndex() const { return sequence_index_; }
  [[nodiscard]] bool hasMedia() const { return engine_.isOpen(); }
  // A JPEG photo is shown as a still: no transport, one GPS position at most, no track.
  [[nodiscard]] bool isPhoto() const { return engine_.isStill(); }
  [[nodiscard]] QRectF panoramaCrop() const { return panorama_crop_; }
  // Capture settings of an open photo: classification, exposure and GoPro settings.
  [[nodiscard]] QVariantList photoRows() const { return photo_rows_; }
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
  [[nodiscard]] bool hasAudio() const { return engine_.hasAudio(); }
  [[nodiscard]] bool audioMuted() const { return engine_.audioMuted(); }
  void setAudioMuted(bool muted) { engine_.setAudioMuted(muted); }
  [[nodiscard]] QString audioState() const { return engine_.audioState(); }
  [[nodiscard]] QString audioDescription() const { return audio_description_; }
  [[nodiscard]] bool metadataLoading() const { return loader_.joinable() && !metadata_loaded_; }

  // Map: one route point per GPS record (same indices as the telemetry), the record nearest to
  // the displayed frame, and its state: "nomedia", "loading", "none" (no GPS), "valid",
  // "invalid" (nearest record has no valid fix) or "gap" (time falls in a reported GPS gap).
  [[nodiscard]] gb::tiles::TileFetcher *tileFetcher() { return tiles_; }
  // Extracted telemetry once metadata has loaded, else nullptr.
  [[nodiscard]] const gb::telemetry::Extraction *telemetry() const;
  [[nodiscard]] const std::vector<gb::geo::RoutePoint> &mapRoute() const { return map_route_; }
  [[nodiscard]] std::optional<std::size_t> mapRecord() const { return map_record_; }
  [[nodiscard]] std::optional<double> mapCourse() const { return map_course_; }
  [[nodiscard]] QString mapState() const { return map_state_; }
  [[nodiscard]] QString mapMessage() const { return map_message_; }
  [[nodiscard]] QString mapAttribution() const;
  [[nodiscard]] QString mapAttributionUrl() const;
  [[nodiscard]] QString mapProviderName() const;
  [[nodiscard]] bool exportingTrack() const { return exporter_.joinable(); }

  Q_INVOKABLE void openDialog();
  Q_INVOKABLE bool open(const QString &path, double start_seconds = 0.0);
  // Opens another chapter of the open recording from its start, keeping play/pause.
  Q_INVOKABLE void openSequenceIndex(int index);
  Q_INVOKABLE void togglePlay();
  Q_INVOKABLE void play();
  Q_INVOKABLE void pause();
  Q_INVOKABLE void seekSeconds(double seconds);
  Q_INVOKABLE void seekFraction(double fraction);
  Q_INVOKABLE void stepFrames(int frames);
  // Timeline drag: the clock holds and audio stays silent until the scrub ends and settles.
  Q_INVOKABLE void beginScrub();
  Q_INVOKABLE void endScrub();
  // Seek targets near a clicked map position, in media order: one per separate pass of the route,
  // each {ticks, timeText, distanceMetres, record, nearestToNow}. ticks are video ticks.
  Q_INVOKABLE QVariantList routeCandidates(double latitude, double longitude, double toleranceMetres) const;
  Q_INVOKABLE void seekTicks(double ticks);
  // Asks for a format and file, then writes the GPS track of every chapter of the open recording,
  // in chapter order, in the background. The result is reported in the status line.
  Q_INVOKABLE void exportTrackDialog();

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
  void audioChanged();
  void mapRouteChanged();
  void mapPositionChanged();
  void trackExportChanged();

private:
  struct LoadedMetadata;
  struct Source;

  bool openPhoto(const QString &path);
  void setStatus(const QString &message, bool error = false);
  void finishTrackExport(const QString &message, bool error);
  void onEndReached();
  void startMetadataLoad(const std::string &path);
  void applyMetadata(std::uint64_t generation, std::shared_ptr<LoadedMetadata> loaded);
  void onFrameChanged();
  void updateTelemetry();
  void updateGpmfSample();
  void updateDiagnostics();
  void buildFileRows();
  void setMapPosition(const QString &state, const QString &message, std::optional<std::size_t> record,
                      std::optional<double> course);
  [[nodiscard]] std::int64_t ticksForSeconds(double seconds) const;
  [[nodiscard]] QString formatTicks(std::int64_t ticks) const;

  PlayerOptions options_;
  PlaybackEngine engine_;
  std::unique_ptr<Source> source_;
  QString input_path_;
  QStringList sequence_paths_;
  int sequence_index_{};
  QString audio_description_;
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
  QVariantList photo_rows_;
  QRectF panorama_crop_{0.0, 0.0, 1.0, 1.0};
  QVariantList track_rows_;
  QString gpmf_title_;
  QVariantList gpmf_rows_;
  std::optional<std::size_t> gpmf_sample_;

  QTimer diagnostics_timer_;
  QElapsedTimer diagnostics_clock_;
  PlaybackStatistics previous_statistics_;
  ProcessUsage previous_usage_;
  QVariantList diagnostics_rows_;

  gb::tiles::TileFetcher *tiles_{};
  std::vector<gb::geo::RoutePoint> map_route_;
  std::optional<std::size_t> map_record_;
  std::optional<double> map_course_;
  QString map_state_{QStringLiteral("nomedia")};
  QString map_message_{QStringLiteral("Open a recording to see its GPS route")};

  std::thread exporter_;
  QString export_directory_;
  QString export_filter_;
};

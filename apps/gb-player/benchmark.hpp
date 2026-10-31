#pragma once

#include "playback_engine.hpp"
#include "process_usage.hpp"

#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QTimer>

#include <cstdint>
#include <functional>
#include <random>
#include <utility>
#include <vector>

class MapView;
class PlayerController;
class QQuickWindow;

struct BenchmarkOptions {
  double play_seconds{30.0};
  int seeks{10};
  std::uint32_t seed{1};
  int view_mode{};
  // When positive, block the GUI thread for this long while playing (see startStall()).
  double stall_seconds{};
};

// Drives the real player through continuous playback, an audio gating check (timeline scrubbing
// and a burst of seeks while playing must stay silent, and audio must resume afterwards), and
// optionally a GUI-thread stall (the decode worker must keep pace with the clock on its own), and
// random paused seeks, then prints a schema-versioned JSON report (gb.player.benchmark/v1).
// Throughout, it checks that the marker the map actually painted is the GPS record nearest to the
// displayed frame, computed independently from the frame's PTS.
class BenchmarkRunner final : public QObject {
  Q_OBJECT

public:
  BenchmarkRunner(PlayerController &controller, QQuickWindow *window, BenchmarkOptions options,
                  QObject *parent = nullptr);
  void start();

signals:
  void finished(int exit_code);

private:
  void waitUntilReady();
  void startPlayback();
  void finishPlayback();
  void checkAudioGating();
  void startStall();
  void stallGuiThread();
  void startSeeks();
  void runSteps();
  [[nodiscard]] std::uint64_t audibleFrames() const;
  void nextSeek();
  void seekCompleted(double latency_ms);
  void report();
  void fail(const QString &message);
  // GPS record the map must show for the displayed frame: the nearest record if it is a valid fix,
  // otherwise -1 (no filled marker).
  [[nodiscard]] long long expectedMapRecord() const;
  [[nodiscard]] double recordSeparationMs(long long first, long long second) const;
  void sampleMapDuringPlayback();
  void checkMapStill(int &checks, int &mismatches);

  PlayerController &controller_;
  PlaybackEngine &engine_;
  QPointer<QQuickWindow> window_;
  BenchmarkOptions options_;
  bool gpu_{};
  QTimer poll_;
  QTimer timeout_;
  QElapsedTimer wall_;
  std::mt19937 random_;
  PlaybackStatistics start_statistics_;
  ProcessUsage start_usage_;
  std::int64_t start_ticks_{};
  QJsonObject playback_;
  QJsonObject audio_;
  bool audio_ok_{true};
  QJsonObject stall_;
  bool stall_ok_{true};
  std::vector<std::pair<int, std::function<void()>>> steps_;
  std::size_t step_{};
  QJsonArray seeks_;
  std::vector<double> latencies_;
  int seek_index_{};
  std::int64_t seek_target_{};
  bool awaiting_seek_{};
  QMetaObject::Connection seek_connection_;

  QPointer<MapView> map_;
  QTimer map_poll_;
  int map_playback_samples_{};
  int map_playback_lagging_{};
  double map_playback_max_lag_ms_{};
  int map_pause_checks_{};
  int map_pause_mismatches_{};
  int map_seek_checks_{};
  int map_seek_mismatches_{};
};

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
#include <random>

class PlayerController;
class QQuickWindow;

struct BenchmarkOptions {
  double play_seconds{30.0};
  int seeks{10};
  std::uint32_t seed{1};
  int view_mode{};
};

// Drives the real player through continuous playback and random paused seeks, then prints a
// schema-versioned JSON report (gb.player.benchmark/v1) to stdout.
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
  void nextSeek();
  void seekCompleted(double latency_ms);
  void report();
  void fail(const QString &message);

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
  QJsonArray seeks_;
  std::vector<double> latencies_;
  int seek_index_{};
  std::int64_t seek_target_{};
  bool awaiting_seek_{};
  QMetaObject::Connection seek_connection_;
};

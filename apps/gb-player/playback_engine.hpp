#pragma once

#include <QObject>
#include <QString>
#include <QTimer>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

// Q_OBJECT headers avoid <filesystem>: Qt 6.2 moc cannot parse the GCC 11 header.
namespace gb::media {
struct FrameSet;
struct PlaybackLayout;
struct Rational;
class SynchronizedReader;
} // namespace gb::media

class AudioOutput;
struct AudioStatistics;

struct PresentedFrame {
  std::shared_ptr<const gb::media::FrameSet> frames;
  std::uint64_t serial{};
  std::uint64_t generation{};
};

struct PlaybackStatistics {
  std::uint64_t decoded_sets{};
  std::uint64_t presented_frames{};
  std::uint64_t rendered_frames{};
  std::uint64_t late_drops{};
  std::uint64_t render_skips{};
  std::uint64_t underruns{};
  std::uint64_t unpaired_frames{};
  std::size_t queue_depth{};
  std::size_t queue_capacity{};
  double decode_busy_ms{};
  double upload_ms_total{};
  double upload_ms_max{};
  double gpu_ms_total{};
  double gpu_ms_max{};
  std::uint64_t gpu_samples{};
  std::vector<double> seek_present_ms;
  std::vector<double> seek_render_ms;
  std::string renderer;
};

// Owns a decode worker and presents frame sets against a monotonic clock. Late frames are skipped
// rather than slowing the timeline. Audio is heard only during stable playback: playing, not
// scrubbing, no seek in flight, and settled for a short hold after the last seek. While audio is
// heard, its device position is the master clock; otherwise the wall clock continues from it.
class PlaybackEngine final : public QObject {
  Q_OBJECT

public:
  explicit PlaybackEngine(QObject *parent = nullptr);
  ~PlaybackEngine() override;

  void open(const std::string &path, const gb::media::PlaybackLayout &layout,
            int decoder_threads, std::size_t queue_capacity, std::int64_t start_ticks,
            std::optional<int> audio_stream, const std::string &application_name);
  // Shows one decoded picture (a photo) with no decoder, clock, audio or seeking.
  void openStill(std::shared_ptr<const gb::media::FrameSet> frames);
  void close();
  void play();
  void pause();
  void seek(std::int64_t ticks);
  // While scrubbing, seeks are shown as they complete, the clock holds, and audio is silent.
  void setScrubbing(bool scrubbing);
  void setAudioMuted(bool muted);

  [[nodiscard]] bool isOpen() const noexcept;
  [[nodiscard]] bool isStill() const noexcept { return still_; }
  [[nodiscard]] bool playing() const noexcept;
  [[nodiscard]] bool seeking() const;
  [[nodiscard]] bool scrubbing() const noexcept { return scrubbing_; }
  [[nodiscard]] bool hasAudio() const noexcept { return audio_ != nullptr; }
  [[nodiscard]] bool audioMuted() const noexcept { return audio_muted_; }
  [[nodiscard]] QString audioError() const { return audio_error_; }
  // Why audio is or is not heard right now, for diagnostics.
  [[nodiscard]] QString audioState() const;
  [[nodiscard]] AudioStatistics audioStatistics() const;
  [[nodiscard]] std::int64_t positionTicks() const;
  [[nodiscard]] std::int64_t frameDurationTicks() const;
  [[nodiscard]] std::int64_t durationTicks() const noexcept;
  [[nodiscard]] gb::media::Rational timeBase() const noexcept;
  [[nodiscard]] PresentedFrame current() const;
  [[nodiscard]] PlaybackStatistics statistics() const;
  void resetStatistics();

  // Called by the render thread after a frame set has been uploaded and drawn.
  void reportRendered(std::uint64_t serial, std::uint64_t generation, double upload_ms,
                      std::optional<double> gpu_ms);
  void setRendererDescription(std::string description);

  // When frame-driven, playback presentation happens from advanceFrame(), called once per
  // scene-graph frame, so every presented frame is synchronized for drawing. The internal timer
  // still completes seeks and detects the end of the stream.
  void setFrameDriven(bool enabled);
  void advanceFrame();

signals:
  void frameChanged();
  void playingChanged();
  void errorOccurred(const QString &message);
  void seekPresented(double latency_ms);
  void seekRendered(double latency_ms);
  void endReached();
  void audioChanged();

private:
  struct QueuedSet {
    std::uint64_t generation{};
    std::shared_ptr<const gb::media::FrameSet> frames;
  };

  void worker();
  void tick(bool from_frame = false);
  void present(QueuedSet item);
  [[nodiscard]] std::int64_t clockTicks() const;
  void rebaseClock(std::int64_t ticks, std::chrono::steady_clock::time_point at);
  void syncClockToAudio();
  void updateAudio(bool seek_pending);
  void disengageAudio();
  void stopWorker();

  std::unique_ptr<gb::media::SynchronizedReader> reader_;
  bool still_{};
  std::int32_t time_base_numerator_{1};
  std::int32_t time_base_denominator_{1};
  std::int64_t duration_ticks_{};

  mutable std::mutex mutex_;
  std::condition_variable condition_;
  std::thread thread_;
  std::deque<QueuedSet> queue_;
  std::size_t capacity_{4};
  std::optional<std::int64_t> seek_request_;
  std::uint64_t generation_{};
  bool stop_{};
  bool end_of_stream_{};
  std::optional<QString> worker_error_;

  mutable std::mutex current_mutex_;
  PresentedFrame current_;

  QTimer timer_;
  std::chrono::steady_clock::time_point clock_origin_;
  std::int64_t clock_base_ticks_{};
  bool playing_{};
  bool scrubbing_{};

  std::unique_ptr<AudioOutput> audio_;
  QString audio_error_;
  bool audio_engaged_{};
  bool audio_muted_{};
  std::chrono::steady_clock::time_point audio_hold_until_;
  bool frame_driven_{};
  bool awaiting_seek_{};
  bool in_underrun_{};
  std::chrono::steady_clock::time_point seek_started_;
  std::uint64_t seek_serial_{};
  bool seek_render_pending_{};
  std::uint64_t serial_{};

  mutable std::mutex statistics_mutex_;
  PlaybackStatistics statistics_;
  std::uint64_t last_rendered_serial_{};
  std::uint64_t last_rendered_generation_{};
};

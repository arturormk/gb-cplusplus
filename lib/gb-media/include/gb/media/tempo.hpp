#pragma once

#include <cstdint>
#include <memory>
#include <vector>

namespace gb::media {

// Speeds stereo audio up or slows it down without changing its pitch, through FFmpeg's atempo
// filter (a WSOLA time stretch). Input and output are interleaved stereo float samples at the
// same rate; `tempo` frames of input become about one frame of output. Throws MediaError when the
// filter cannot be set up or fails.
class TempoStretcher {
public:
  TempoStretcher(int sample_rate, double tempo); // tempo in 0.5..100
  ~TempoStretcher();
  TempoStretcher(const TempoStretcher &) = delete;
  TempoStretcher &operator=(const TempoStretcher &) = delete;

  // Adds `frames` input frames and appends whatever output is ready to `output`.
  void push(const float *samples, std::int64_t frames, std::vector<float> &output);

  // Ends the input and appends the remaining output. Push nothing afterwards.
  void finish(std::vector<float> &output);

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace gb::media

#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

constexpr uint32_t kRuntimeSampleRate = 16000;
constexpr int64_t kRuntimeNoPts = std::numeric_limits<int64_t>::min();

struct RuntimeAudioPacket {
    std::vector<float> samples;
    int64_t pts_us = kRuntimeNoPts;
};

class RuntimeAudioConverter {
  public:
    std::vector<float> convert(const float *interleaved, size_t frames, uint32_t channels,
                               uint32_t sample_rate);
    void reset();

  private:
    double source_offset_ = 0.0;
    uint32_t source_rate_ = 0;
};

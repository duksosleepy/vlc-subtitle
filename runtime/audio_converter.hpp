#ifndef VLC_SUBTITLE_AUDIO_CONVERTER_HPP
#define VLC_SUBTITLE_AUDIO_CONVERTER_HPP

#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

constexpr unsigned kRuntimeSampleRate = 16000;
constexpr int64_t kRuntimeNoPts = std::numeric_limits<int64_t>::min();

struct RuntimeAudioPacket
{
    std::vector<float> samples;
    int64_t pts_us = kRuntimeNoPts;
};

class RuntimeAudioConverter
{
public:
    std::vector<float> convert(const float *interleaved, size_t frames,
                               unsigned channels, unsigned sample_rate);
    void reset();

private:
    double source_offset_ = 0.0;
    unsigned source_rate_ = 0;
};

#endif

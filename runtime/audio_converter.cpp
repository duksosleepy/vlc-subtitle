#include "audio_converter.hpp"

#include <algorithm>
#include <cmath>

std::vector<float> RuntimeAudioConverter::convert(
    const float *interleaved, size_t frames, unsigned channels,
    unsigned sample_rate)
{
    if (interleaved == nullptr || frames == 0 || channels == 0 ||
        sample_rate == 0)
        return {};

    if (source_rate_ != sample_rate)
    {
        source_rate_ = sample_rate;
        source_offset_ = 0.0;
    }

    std::vector<float> mono(frames);
    for (size_t frame = 0; frame < frames; ++frame)
    {
        float sum = 0.0f;
        for (unsigned channel = 0; channel < channels; ++channel)
            sum += interleaved[frame * channels + channel];
        mono[frame] = sum / static_cast<float>(channels);
    }

    if (sample_rate == kRuntimeSampleRate)
    {
        source_offset_ = 0.0;
        return mono;
    }

    const double step = static_cast<double>(sample_rate) / kRuntimeSampleRate;
    std::vector<float> output;
    output.reserve(static_cast<size_t>(std::ceil(frames / step)) + 1);
    while (source_offset_ < frames)
    {
        const double source = std::min<double>(source_offset_, frames - 1);
        const size_t left = static_cast<size_t>(source);
        const size_t right = std::min(left + 1, frames - 1);
        const float fraction = static_cast<float>(source - left);
        output.push_back(mono[left] +
                         (mono[right] - mono[left]) * fraction);
        source_offset_ += step;
    }
    source_offset_ -= frames;
    return output;
}

void RuntimeAudioConverter::reset()
{
    source_offset_ = 0.0;
    source_rate_ = 0;
}

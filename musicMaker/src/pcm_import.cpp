#include "music_maker.h"

#include <sndfile.h>

#include <algorithm>
#include <cmath>
#include <numeric>

namespace musicmaker {

namespace {
void trimLeadingSilence(std::vector<float> &samples) {
    constexpr std::size_t Window = PcmSampleRate / 100;
    constexpr std::size_t PreRoll = PcmSampleRate / 50;
    if (samples.size() <= Window) {
        return;
    }
    std::size_t baselineSize =
        std::min<std::size_t>(samples.size(), PcmSampleRate / 5);
    double baseline = std::accumulate(samples.begin(),
                                      samples.begin() + baselineSize, 0.0) /
                      static_cast<double>(baselineSize);
    std::vector<float> levels;
    levels.reserve(baselineSize);
    for (std::size_t i = 0; i < baselineSize; i++) {
        levels.push_back(std::abs(samples[i] - static_cast<float>(baseline)));
    }
    std::nth_element(levels.begin(), levels.begin() + levels.size() / 2,
                     levels.end());
    float threshold = std::max(0.02f, levels[levels.size() / 2] * 4.0f);
    for (std::size_t start = 0; start + Window <= samples.size();
         start += Window / 4) {
        double energy = 0.0;
        for (std::size_t i = 0; i < Window; i++) {
            double centered = samples[start + i] - baseline;
            energy += centered * centered;
        }
        if (std::sqrt(energy / Window) >= threshold) {
            if (start > PreRoll) {
                samples.erase(samples.begin(),
                              samples.begin() + start - PreRoll);
            }
            return;
        }
    }
}
}

bool importPcmFile(const std::string &path, bool trimLeading,
                   std::vector<std::uint8_t> &sample, std::string &error) {
    SF_INFO info{};
    SNDFILE *file = sf_open(path.c_str(), SFM_READ, &info);
    if (!file) {
        error = sf_strerror(nullptr);
        return false;
    }
    if (info.frames <= 0 || info.channels <= 0 || info.samplerate <= 0) {
        sf_close(file);
        error = "The audio file has no usable samples";
        return false;
    }
    sf_count_t sourceLimit = std::min<sf_count_t>(
        info.frames, static_cast<sf_count_t>(info.samplerate) * 10 + 2);
    std::vector<float> interleaved(
        static_cast<std::size_t>(sourceLimit) * info.channels);
    sf_count_t frames = sf_readf_float(file, interleaved.data(), sourceLimit);
    sf_close(file);
    if (frames <= 0) {
        error = "Could not decode audio samples";
        return false;
    }
    std::vector<float> mono(static_cast<std::size_t>(frames));
    for (sf_count_t frame = 0; frame < frames; frame++) {
        double mixed = 0.0;
        for (int channel = 0; channel < info.channels; channel++) {
            mixed += interleaved[static_cast<std::size_t>(frame) *
                                     info.channels +
                                 channel];
        }
        mono[static_cast<std::size_t>(frame)] =
            static_cast<float>(mixed / info.channels);
    }
    std::size_t outputCount = static_cast<std::size_t>(std::ceil(
        static_cast<double>(frames) * PcmSampleRate / info.samplerate));
    std::vector<float> resampled(outputCount);
    double ratio = static_cast<double>(info.samplerate) / PcmSampleRate;
    for (std::size_t i = 0; i < outputCount; i++) {
        double position = i * ratio;
        std::size_t first = std::min<std::size_t>(
            static_cast<std::size_t>(position), mono.size() - 1);
        std::size_t second = std::min(first + 1, mono.size() - 1);
        float blend = static_cast<float>(position - first);
        resampled[i] = mono[first] * (1.0f - blend) + mono[second] * blend;
    }
    if (trimLeading) {
        trimLeadingSilence(resampled);
    }
    if (resampled.size() > MaxPcmSamples) {
        resampled.resize(MaxPcmSamples);
    }
    float peak = 0.0f;
    for (float value : resampled) {
        peak = std::max(peak, std::abs(value));
    }
    float gain = peak > 0.01f ? 0.95f / peak : 1.0f;
    std::size_t fade = std::min<std::size_t>(80, resampled.size() / 2);
    sample.resize(resampled.size());
    for (std::size_t i = 0; i < resampled.size(); i++) {
        float value = resampled[i] * gain;
        if (fade > 0 && i < fade) {
            value *= static_cast<float>(i) / fade;
        }
        if (fade > 0 && i + fade >= resampled.size()) {
            value *= static_cast<float>(resampled.size() - 1 - i) / fade;
        }
        int encoded = static_cast<int>(
            std::lround(std::clamp(value, -1.0f, 1.0f) * 127.0f + 128.0f));
        sample[i] = static_cast<std::uint8_t>(std::clamp(encoded, 0, 255));
    }
    return true;
}

}

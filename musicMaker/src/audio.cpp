#include "music_maker.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace musicmaker {

AudioEngine::AudioEngine() = default;

AudioEngine::~AudioEngine() { close(); }

void AudioEngine::close() {
    if (stream) {
        SDL_DestroyAudioStream(stream);
        stream = nullptr;
    }
}

bool AudioEngine::open(std::string &error) {
    SDL_AudioSpec spec{};
    spec.format = SDL_AUDIO_F32;
    spec.channels = 2;
    spec.freq = SampleRate;
    stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec,
                                       audioCallback, this);
    if (!stream) {
        error = SDL_GetError();
        return false;
    }
    if (!SDL_ResumeAudioStreamDevice(stream)) {
        error = SDL_GetError();
        return false;
    }
    return true;
}

void AudioEngine::setSong(const Song &newSong) {
    std::lock_guard lock(mutex);
    song = newSong;
    if (currentStep >= song.stepCount) {
        currentStep = 0;
        samplesIntoStep = 0.0;
        resetVoices();
    }
}

void AudioEngine::play(int startStep) {
    std::lock_guard lock(mutex);
    currentStep = std::clamp(startStep, 0, song.stepCount - 1);
    samplesIntoStep = 0.0;
    resetVoices();
    applyStep(currentStep);
    playing = true;
}

void AudioEngine::stop() {
    std::lock_guard lock(mutex);
    playing = false;
    currentStep = 0;
    samplesIntoStep = 0.0;
    resetVoices();
}

void AudioEngine::toggle(int startStep) {
    if (playing) {
        stop();
    } else {
        play(startStep);
    }
}

bool AudioEngine::isPlaying() const { return playing; }

int AudioEngine::playhead() const { return currentStep; }

void AudioEngine::preview(int channel, int note) {
    std::lock_guard lock(mutex);
    previewChannel = std::clamp(channel, 0, ChannelCount - 1);
    previewNote = std::clamp(note, 0, 127);
    previewSamples = previewChannel == 5 && !song.pcmSample.empty()
                         ? static_cast<int>(song.pcmSample.size()) *
                               SampleRate / PcmSampleRate
                         : SampleRate / 3;
    previewSamples =
        std::clamp(previewSamples, SampleRate / 10, SampleRate * 2);
    voices[previewChannel].note = previewNote;
    voices[previewChannel].velocity = 112;
    Step previewEvent{previewNote, 112};
    voices[previewChannel].frequency =
        eventFrequency(previewChannel, previewEvent);
    voices[previewChannel].gliding = false;
    voices[previewChannel].phase = 0.0;
    voices[previewChannel].lfsr = 0x4000;
    voices[previewChannel].samplePosition = 0.0;
}

void AudioEngine::audioCallback(void *userdata, SDL_AudioStream *audioStream,
                                int additionalAmount, int totalAmount) {
    (void)totalAmount;
    auto *engine = static_cast<AudioEngine *>(userdata);
    if (!engine || additionalAmount <= 0) {
        return;
    }
    int frames = additionalAmount / static_cast<int>(sizeof(float) * 2);
    if (frames <= 0) {
        return;
    }
    engine->buffer.resize(static_cast<std::size_t>(frames) * 2);
    engine->render(engine->buffer.data(), frames);
    SDL_PutAudioStreamData(
        audioStream, engine->buffer.data(),
        static_cast<int>(engine->buffer.size() * sizeof(float)));
}

void AudioEngine::render(float *output, int frames) {
    std::lock_guard lock(mutex);
    for (int frame = 0; frame < frames; frame++) {
        if (playing) {
            while (samplesIntoStep >= stepSamples(currentStep)) {
                samplesIntoStep -= stepSamples(currentStep);
                currentStep = (currentStep + 1) % song.stepCount;
                applyStep(currentStep);
            }
            updateTriplets();
            double progress =
                std::clamp(samplesIntoStep / stepSamples(currentStep), 0.0, 1.0);
            for (Voice &voice : voices) {
                if (voice.gliding) {
                    voice.frequency = voice.glideStart +
                                      (voice.glideTarget - voice.glideStart) *
                                          progress;
                }
            }
        }

        float left = 0.0f;
        float right = 0.0f;
        for (int channel = 0; channel < ChannelCount; channel++) {
            Voice &voice = voices[channel];
            bool previewing =
                !playing && channel == previewChannel && previewSamples > 0;
            if ((!playing && !previewing) || voice.note < 0) {
                continue;
            }
            float sample = voiceSample(channel, voice);
            float gain =
                song.channels[channel].muted
                    ? 0.0f
                    : static_cast<float>(song.channels[channel].volume) /
                          255.0f;
            gain *= static_cast<float>(voice.velocity) / 127.0f;
            gain *= 0.24f;
            float leftGain = gain;
            float rightGain = gain;
            int pan = song.channels[channel].pan;
            if (pan < 0) {
                rightGain *= static_cast<float>(100 + pan) / 100.0f;
            } else if (pan > 0) {
                leftGain *= static_cast<float>(100 - pan) / 100.0f;
            }
            left += sample * leftGain;
            right += sample * rightGain;
        }

        output[frame * 2] = std::clamp(left, -1.0f, 1.0f);
        output[frame * 2 + 1] = std::clamp(right, -1.0f, 1.0f);
        if (playing) {
            samplesIntoStep += 1.0;
            for (int channel = 0; channel < ChannelCount; channel++) {
                if (tripletPhases[channel] > 0) {
                    tripletSamples[channel] += 1.0;
                }
            }
        } else if (previewSamples > 0) {
            previewSamples--;
            if (previewSamples == 0 && previewChannel >= 0) {
                voices[previewChannel].note = Rest;
            }
        }
    }
}

void AudioEngine::applyStep(int step) {
    for (int channel = 0; channel < ChannelCount; channel++) {
        const TripletGroup &group = song.triplets[channel][step];
        if (group.active && group.duration > 0 &&
            step + group.duration <= song.stepCount) {
            tripletStarts[channel] = step;
            tripletPhases[channel] = 1;
            tripletSamples[channel] = 0.0;
            applyEvent(channel, group.events[0]);
            continue;
        }
        bool insideTriplet = false;
        for (int start = step - 1; start >= 0; start--) {
            const TripletGroup &previous = song.triplets[channel][start];
            if (previous.active && step < start + previous.duration) {
                insideTriplet = true;
                break;
            }
        }
        if (insideTriplet) {
            continue;
        }
        tripletPhases[channel] = 0;
        applyEvent(channel, song.pattern[channel][step]);
    }
}

void AudioEngine::applyEvent(int channel, const Step &event) {
    Voice &voice = voices[channel];
    if (event.note >= 0) {
        double target = eventFrequency(channel, event);
        bool canGlide = event.glide && voice.note >= 0;
        voice.glideStart = canGlide ? voice.frequency : target;
        voice.glideTarget = target;
        voice.frequency = voice.glideStart;
        voice.gliding = canGlide;
        voice.note = event.note;
        voice.velocity = event.velocity;
        if (!canGlide) {
            voice.phase = 0.0;
            voice.lfsr = 0x4000;
        }
        voice.samplePosition = 0.0;
    } else if (event.note == Stop || (event.note == Rest && channel != 5)) {
        voice.note = Rest;
        voice.velocity = 0;
        voice.gliding = false;
    }
}

void AudioEngine::updateTriplets() {
    for (int channel = 0; channel < ChannelCount; channel++) {
        int phase = tripletPhases[channel];
        if (phase <= 0 || phase >= 3) {
            continue;
        }
        int start = tripletStarts[channel];
        const TripletGroup &group = song.triplets[channel][start];
        double duration = 0.0;
        for (int step = start;
             step < start + group.duration && step < song.stepCount; step++) {
            duration += stepSamples(step);
        }
        if (phase == 1 && tripletSamples[channel] >= duration / 3.0) {
            applyEvent(channel, group.events[1]);
            tripletPhases[channel] = 2;
        }
        if (tripletPhases[channel] == 2 &&
            tripletSamples[channel] >= duration * 2.0 / 3.0) {
            applyEvent(channel, group.events[2]);
            tripletPhases[channel] = 3;
        }
    }
}

double AudioEngine::stepSamples(int step) const {
    return calculateStepFrames(song)[step] *
           (static_cast<double>(SampleRate) / 60.0);
}

float AudioEngine::voiceSample(int channel, Voice &voice) {
    double frequency = voice.frequency;
    if (channel == 4) {
        voice.phase += frequency * WavetableSize / SampleRate;
        voice.phase =
            std::fmod(voice.phase, static_cast<double>(WavetableSize));
        int index = static_cast<int>(voice.phase) % WavetableSize;
        return (static_cast<float>(song.wavetable[index]) - 128.0f) / 128.0f;
    }
    if (channel == 5) {
        if (song.pcmSample.empty()) {
            voice.note = Rest;
            return 0.0f;
        }
        int index = static_cast<int>(voice.samplePosition);
        if (index >= static_cast<int>(song.pcmSample.size())) {
            if (song.pcmLoop) {
                voice.samplePosition = 0.0;
                index = 0;
            } else {
                voice.note = Rest;
                return 0.0f;
            }
        }
        float sample =
            (static_cast<float>(song.pcmSample[index]) - 128.0f) / 128.0f;
        voice.samplePosition += voice.frequency / SampleRate;
        return sample;
    }
    voice.phase += frequency / SampleRate;
    voice.phase -= std::floor(voice.phase);
    if (channel == 0 || channel == 1) {
        static constexpr std::array<double, 4> duty{0.125, 0.25, 0.5, 0.75};
        return voice.phase < duty[song.channels[channel].parameter] ? 1.0f
                                                                    : -1.0f;
    }
    if (channel == 2) {
        return static_cast<float>(1.0 - 4.0 * std::abs(voice.phase - 0.5));
    }
    if (voice.phase < frequency / SampleRate) {
        std::uint16_t feedback =
            static_cast<std::uint16_t>((voice.lfsr ^ (voice.lfsr >> 1)) & 1u);
        voice.lfsr =
            static_cast<std::uint16_t>((voice.lfsr >> 1) | (feedback << 14));
        if (song.channels[channel].parameter == 1) {
            voice.lfsr = static_cast<std::uint16_t>((voice.lfsr & ~(1u << 6)) |
                                                    (feedback << 6));
        }
    }
    return (voice.lfsr & 1u) != 0 ? 1.0f : -1.0f;
}

PcmRecorder::PcmRecorder() = default;

PcmRecorder::~PcmRecorder() { cancel(); }

bool PcmRecorder::start(std::string &error) {
    cancel();
    samples.clear();
    SDL_AudioSpec spec{};
    spec.format = SDL_AUDIO_F32;
    spec.channels = 1;
    spec.freq = PcmSampleRate;
    stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_RECORDING,
                                       &spec, nullptr, nullptr);
    if (!stream) {
        error = SDL_GetError();
        return false;
    }
    if (!SDL_ResumeAudioStreamDevice(stream)) {
        error = SDL_GetError();
        SDL_DestroyAudioStream(stream);
        stream = nullptr;
        return false;
    }
    return true;
}

void PcmRecorder::poll() {
    if (!stream || samples.size() >= MaxPcmSamples) {
        return;
    }
    int available = SDL_GetAudioStreamAvailable(stream);
    if (available <= 0) {
        return;
    }
    int remaining =
        static_cast<int>((MaxPcmSamples - samples.size()) * sizeof(float));
    int bytes = std::min(available, remaining);
    bytes -= bytes % static_cast<int>(sizeof(float));
    if (bytes <= 0) {
        return;
    }
    std::vector<float> incoming(static_cast<std::size_t>(bytes) /
                                sizeof(float));
    int received = SDL_GetAudioStreamData(
        stream, incoming.data(),
        static_cast<int>(incoming.size() * sizeof(float)));
    if (received <= 0) {
        return;
    }
    incoming.resize(static_cast<std::size_t>(received) / sizeof(float));
    samples.insert(samples.end(), incoming.begin(), incoming.end());
}

std::vector<std::uint8_t> PcmRecorder::stop(bool trimLeadingSilence) {
    poll();
    if (stream) {
        SDL_PauseAudioStreamDevice(stream);
        SDL_DestroyAudioStream(stream);
        stream = nullptr;
    }
    if (samples.empty()) {
        return {};
    }
    if (trimLeadingSilence) {
        trimSilence(samples);
    }

    double mean = std::accumulate(samples.begin(), samples.end(), 0.0) /
                  static_cast<double>(samples.size());
    float peak = 0.0f;
    for (float sample : samples) {
        peak = std::max(peak, std::abs(sample - static_cast<float>(mean)));
    }
    float gain = peak > 0.01f ? 0.95f / peak : 1.0f;
    std::size_t fade = std::min<std::size_t>(80, samples.size() / 2);
    std::vector<std::uint8_t> encoded(samples.size());
    for (std::size_t i = 0; i < samples.size(); i++) {
        float value = (samples[i] - static_cast<float>(mean)) * gain;
        if (i < fade) {
            value *= static_cast<float>(i) / static_cast<float>(fade);
        }
        if (i + fade >= samples.size()) {
            value *= static_cast<float>(samples.size() - 1 - i) /
                     static_cast<float>(fade);
        }
        int byte = static_cast<int>(
            std::lround(std::clamp(value, -1.0f, 1.0f) * 127.0f + 128.0f));
        encoded[i] = static_cast<std::uint8_t>(std::clamp(byte, 0, 255));
    }
    samples.clear();
    return encoded;
}

void PcmRecorder::trimSilence(std::vector<float> &recording) {
    constexpr std::size_t Window = PcmSampleRate / 100;
    constexpr std::size_t PreRoll = PcmSampleRate / 50;
    if (recording.size() <= Window) {
        return;
    }

    std::size_t baselineSize =
        std::min<std::size_t>(recording.size(), PcmSampleRate / 5);
    double baseline = std::accumulate(recording.begin(),
                                      recording.begin() + baselineSize, 0.0) /
                      static_cast<double>(baselineSize);
    std::vector<float> levels;
    levels.reserve(baselineSize);
    for (std::size_t i = 0; i < baselineSize; i++) {
        levels.push_back(std::abs(recording[i] - static_cast<float>(baseline)));
    }
    std::nth_element(levels.begin(), levels.begin() + levels.size() / 2,
                     levels.end());
    float noiseFloor = levels[levels.size() / 2];
    float threshold = std::max(0.02f, noiseFloor * 4.0f);

    std::size_t onset = recording.size();
    for (std::size_t start = 0; start + Window <= recording.size();
         start += Window / 4) {
        double energy = 0.0;
        for (std::size_t i = 0; i < Window; i++) {
            double centered = recording[start + i] - baseline;
            energy += centered * centered;
        }
        float rms = static_cast<float>(std::sqrt(energy / Window));
        if (rms >= threshold) {
            onset = start;
            break;
        }
    }
    if (onset == recording.size() || onset <= PreRoll) {
        return;
    }
    recording.erase(recording.begin(), recording.begin() + onset - PreRoll);
}

void PcmRecorder::cancel() {
    if (stream) {
        SDL_DestroyAudioStream(stream);
        stream = nullptr;
    }
    samples.clear();
}

bool PcmRecorder::isRecording() const { return stream != nullptr; }

int PcmRecorder::sampleCount() const {
    return static_cast<int>(samples.size());
}

void AudioEngine::resetVoices() {
    voices = {};
    tripletStarts = {};
    tripletPhases = {};
    tripletSamples = {};
    for (Voice &voice : voices) {
        voice.note = Rest;
        voice.lfsr = 0x4000;
    }
}

}

#include "music_maker.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace musicmaker {

Song::Song() {
    channels[0] = {"Pulse 1", 210, -18, 2, false};
    channels[1] = {"Pulse 2", 190, 18, 1, false};
    channels[2] = {"Triangle", 220, 0, 0, false};
    channels[3] = {"Noise", 165, 0, 0, false};
    channels[4] = {"Wavetable", 190, -8, 0, false};
    channels[5] = {"PCM Sample", 220, 8, 0, false};
    for (int i = 0; i < WavetableSize; i++) {
        double phase = static_cast<double>(i) / WavetableSize;
        wavetable[i] = static_cast<std::uint8_t>(std::lround(
            std::sin(phase * 2.0 * std::acos(-1.0)) * 127.0 + 128.0));
    }
}

std::string noteName(int note) {
    if (note == Rest) {
        return "---";
    }
    if (note == Hold) {
        return "...";
    }
    if (note == Stop) {
        return "STP";
    }
    static constexpr std::array<const char *, 12> names{
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    int octave = note / 12 - 1;
    return std::string(names[static_cast<std::size_t>(note % 12)]) +
           std::to_string(octave);
}

double noteFrequency(int note) {
    return 440.0 * std::pow(2.0, (static_cast<double>(note) - 69.0) / 12.0);
}

double eventFrequency(int channel, const Step &event) {
    double base = channel == 5 ? PcmSampleRate : noteFrequency(event.note);
    return base * std::pow(2.0, static_cast<double>(event.tuning) / 1200.0);
}

std::array<int, MaxSteps> calculateStepFrames(const Song &song) {
    std::array<int, MaxSteps> frames{};
    double cumulative = 0.0;
    int emitted = 0;
    double base =
        3600.0 / (static_cast<double>(song.bpm) * song.stepsPerBeat);
    for (int step = 0; step < song.stepCount; step++) {
        double scale = 1.0;
        if (song.swing > 0) {
            scale += (step % 2 == 0 ? song.swing : -song.swing) / 100.0;
        }
        cumulative += base * scale;
        int target = static_cast<int>(std::lround(cumulative));
        frames[step] = std::max(1, target - emitted);
        emitted += frames[step];
    }
    return frames;
}

std::string exportSymbolPrefix(const std::string &path) {
    std::string source = std::filesystem::path(path).stem().string();
    std::string prefix;
    for (unsigned char value : source) {
        if (std::isalnum(value)) {
            prefix += static_cast<char>(std::toupper(value));
        } else if (prefix.empty() || prefix.back() != '_') {
            prefix += '_';
        }
    }
    while (!prefix.empty() && prefix.back() == '_') {
        prefix.pop_back();
    }
    if (prefix.empty()) {
        prefix = "SONG";
    }
    if (std::isdigit(static_cast<unsigned char>(prefix.front()))) {
        prefix.insert(prefix.begin(), '_');
    }
    return prefix;
}

bool saveSong(const Song &song, const std::string &path, std::string &error) {
    std::ofstream output(path);
    if (!output) {
        error = "Could not write " + path;
        return false;
    }

    output << "E16MUSIC 3\n";
    output << "title " << std::quoted(song.title) << "\n";
    output << "bpm " << song.bpm << "\n";
    output << "steps " << song.stepCount << "\n";
    output << "division " << song.stepsPerBeat << "\n";
    output << "swing " << song.swing << "\n";
    output << "pcm_loop " << static_cast<int>(song.pcmLoop) << "\n";
    output << "pcm_trim " << static_cast<int>(song.trimPcmSilence) << "\n";
    for (int channel = 0; channel < ChannelCount; channel++) {
        const ChannelSettings &settings = song.channels[channel];
        output << "channel " << channel << ' ' << settings.volume << ' '
               << settings.pan << ' ' << settings.parameter << ' '
               << static_cast<int>(settings.muted) << "\n";
    }
    output << "wavetable";
    for (std::uint8_t sample : song.wavetable) {
        output << ' ' << static_cast<int>(sample);
    }
    output << "\n";
    output << "pcmhex ";
    output << std::hex << std::setfill('0');
    if (song.pcmSample.empty()) {
        output << '-';
    } else {
        for (std::uint8_t sample : song.pcmSample) {
            output << std::setw(2) << static_cast<int>(sample);
        }
    }
    output << std::dec << "\n";
    for (int channel = 0; channel < ChannelCount; channel++) {
        for (int step = 0; step < song.stepCount; step++) {
            const Step &event = song.pattern[channel][step];
            if (event.note != Rest) {
                output << "event " << channel << ' ' << step << ' '
                       << event.note << ' ' << event.velocity << ' '
                       << event.tuning << ' ' << static_cast<int>(event.glide)
                       << "\n";
            }
        }
    }

    if (!output) {
        error = "Could not finish writing " + path;
        return false;
    }
    return true;
}

bool loadSong(Song &song, const std::string &path, std::string &error) {
    std::ifstream input(path);
    if (!input) {
        error = "Could not open " + path;
        return false;
    }

    std::string signature;
    int version = 0;
    input >> signature >> version;
    if (signature != "E16MUSIC" ||
        (version != 1 && version != 2 && version != 3)) {
        error = "This is not an E16 Music Maker project";
        return false;
    }

    Song loaded;
    std::string key;
    while (input >> key) {
        if (key == "title") {
            input >> std::quoted(loaded.title);
        } else if (key == "bpm") {
            input >> loaded.bpm;
        } else if (key == "steps") {
            input >> loaded.stepCount;
        } else if (key == "division") {
            input >> loaded.stepsPerBeat;
        } else if (key == "swing") {
            input >> loaded.swing;
        } else if (key == "pcm_loop") {
            int loop = 0;
            input >> loop;
            loaded.pcmLoop = loop != 0;
        } else if (key == "pcm_trim") {
            int trim = 0;
            input >> trim;
            loaded.trimPcmSilence = trim != 0;
        } else if (key == "channel") {
            int channel = 0;
            int muted = 0;
            ChannelSettings settings;
            input >> channel >> settings.volume >> settings.pan >>
                settings.parameter >> muted;
            if (channel < 0 || channel >= ChannelCount) {
                error = "Project contains an invalid channel";
                return false;
            }
            settings.name = loaded.channels[channel].name;
            settings.muted = muted != 0;
            loaded.channels[channel] = settings;
        } else if (key == "event") {
            int channel = 0;
            int step = 0;
            Step event;
            std::string values;
            std::getline(input, values);
            std::istringstream eventInput(values);
            eventInput >> channel >> step >> event.note >> event.velocity;
            int glide = 0;
            if (eventInput >> event.tuning) {
                eventInput >> glide;
                event.glide = glide != 0;
            }
            if (channel < 0 || channel >= ChannelCount || step < 0 ||
                step >= MaxSteps || event.note < Stop || event.note > 127 ||
                event.velocity < 1 || event.velocity > 127 ||
                event.tuning < -2400 || event.tuning > 2400) {
                error = "Project contains an invalid note event";
                return false;
            }
            loaded.pattern[channel][step] = event;
        } else if (key == "wavetable") {
            for (std::uint8_t &sample : loaded.wavetable) {
                int value = 0;
                input >> value;
                if (value < 0 || value > 255) {
                    error = "Project contains invalid wavetable data";
                    return false;
                }
                sample = static_cast<std::uint8_t>(value);
            }
        } else if (key == "pcmhex") {
            std::string encoded;
            input >> encoded;
            if (encoded == "-") {
                loaded.pcmSample.clear();
                continue;
            }
            if (encoded.size() % 2 != 0 || encoded.size() / 2 > MaxPcmSamples) {
                error = "Project contains invalid PCM data";
                return false;
            }
            if (!std::all_of(encoded.begin(), encoded.end(), [](char value) {
                    return std::isxdigit(static_cast<unsigned char>(value));
                })) {
                error = "Project contains invalid PCM data";
                return false;
            }
            loaded.pcmSample.clear();
            for (std::size_t i = 0; i < encoded.size(); i += 2) {
                try {
                    int value = std::stoi(encoded.substr(i, 2), nullptr, 16);
                    loaded.pcmSample.push_back(
                        static_cast<std::uint8_t>(value));
                } catch (...) {
                    error = "Project contains invalid PCM data";
                    return false;
                }
            }
        } else {
            std::string ignored;
            std::getline(input, ignored);
        }
        if (!input) {
            error = "Project is incomplete or malformed";
            return false;
        }
    }

    if (loaded.stepCount < StepsPerPage || loaded.stepCount > MaxSteps ||
        loaded.stepCount % StepsPerPage != 0) {
        error = "Project page count must be between 1 and 50";
        return false;
    }
    if (loaded.bpm < 30 || loaded.bpm > 300 || loaded.stepsPerBeat < 1 ||
        loaded.stepsPerBeat > 8 || loaded.swing < 0 || loaded.swing > 40) {
        error = "Project timing values are out of range";
        return false;
    }
    for (ChannelSettings &settings : loaded.channels) {
        settings.volume = std::clamp(settings.volume, 0, 255);
        settings.pan = std::clamp(settings.pan, -100, 100);
        settings.parameter = std::clamp(settings.parameter, 0, 3);
    }
    song = loaded;
    return true;
}

namespace {
int channelVolume(const ChannelSettings &settings, const Step &event) {
    int base = settings.muted ? 0 : settings.volume * event.velocity / 127;
    base = base * 24 / 100;
    int left = base;
    int right = base;
    if (settings.pan < 0) {
        right = right * (100 + settings.pan) / 100;
    } else if (settings.pan > 0) {
        left = left * (100 - settings.pan) / 100;
    }
    return std::clamp(left, 0, 255) | (std::clamp(right, 0, 255) << 8);
}

std::string hexadecimal(int value, int width) {
    std::ostringstream stream;
    stream << "0x" << std::uppercase << std::hex << std::setfill('0')
           << std::setw(width) << value;
    return stream.str();
}

void emitBytes(std::ostream &output, const std::string &label,
               const std::uint8_t *data, std::size_t size) {
    output << label << ":\n";
    for (std::size_t offset = 0; offset < size; offset += 16) {
        output << "    .byte ";
        std::size_t count = std::min<std::size_t>(16, size - offset);
        for (std::size_t i = 0; i < count; i++) {
            if (i != 0) {
                output << ", ";
            }
            output << static_cast<int>(data[offset + i]);
        }
        output << "\n";
    }
}

void replaceAll(std::string &value, const std::string &from,
                const std::string &to) {
    std::size_t position = 0;
    while ((position = value.find(from, position)) != std::string::npos) {
        value.replace(position, from.size(), to);
        position += to.size();
    }
}

int stateBaseForPrefix(const std::string &prefix) {
    std::uint32_t hash = 2166136261u;
    for (unsigned char value : prefix) {
        hash ^= value;
        hash *= 16777619u;
    }
    return 0x000100 + static_cast<int>(hash % 32u) * 8;
}
}

bool exportE16(const Song &song, const std::string &path, std::string &error) {
    std::ofstream file(path);
    if (!file) {
        error = "Could not write " + path;
        return false;
    }
    std::ostringstream output;
    std::string prefix = exportSymbolPrefix(path);

    output << ".const MUSIC_APU_CONTROL, 0xFF2000\n";
    output << ".const MUSIC_APU_MASTER_VOL, 0xFF2004\n";
    output << ".const MUSIC_CH_FREQ, 0x04\n";
    output << ".const MUSIC_CH_VOLUME, 0x06\n";
    output << ".const MUSIC_CH_PARAM, 0x0E\n";
    output << ".const MUSIC_CH_CONTROL, 0x00\n";
    output << ".const MUSIC_CH_LENGTH, 0x0A\n";
    output << ".const MUSIC_CH_BASE, 0x10\n";
    output << ".const MUSIC_WAVETABLE_RAM, 0x060000\n";
    output << ".const MUSIC_PCM_RAM, 0x060100\n";
    output << ".const MUSIC_WAVETABLE_LENGTH, " << WavetableSize << "\n";
    output << ".const MUSIC_PCM_LENGTH, " << song.pcmSample.size() << "\n";
    output << ".const MUSIC_PCM_RATE, " << PcmSampleRate << "\n";
    output << ".const MUSIC_STATE_BASE, "
           << hexadecimal(stateBaseForPrefix(prefix), 6) << "\n";
    output << ".const MUSIC_STEP_STATE, MUSIC_STATE_BASE\n";
    output << ".const MUSIC_WAIT_STATE, MUSIC_STATE_BASE + 2\n";
    output << ".const MUSIC_PLAYING_STATE, MUSIC_STATE_BASE + 4\n";
    output << ".const MUSIC_LOOP_STATE, MUSIC_STATE_BASE + 6\n";
    for (int channel = 0; channel < ChannelCount; channel++) {
        output << ".const MUSIC_APU_CH" << channel << "_BASE, "
               << hexadecimal(0xFF2100 + channel * 0x20, 6) << "\n";
    }
    output << "\n    bra music_module_end\n";
    output << "\nmusic_play:\n";
    output << "    push r0\n";
    output << "    mov r0, 1\n";
    output << "    storew @MUSIC_LOOP_STATE, r0\n";
    output << "    bra music_play_start\n";
    output << "\nmusic_play_once:\n";
    output << "    push r0\n";
    output << "    mov r0, 0\n";
    output << "    storew @MUSIC_LOOP_STATE, r0\n";
    output << "music_play_start:\n";
    output << "    call music_load_samples\n";
    output << "    mov r0, 0x0023\n";
    output << "    storew @MUSIC_APU_CONTROL, r0\n";
    output << "    mov r0, 0xFFFF\n";
    output << "    storew @MUSIC_APU_MASTER_VOL, r0\n";
    for (int channel = 0; channel < ChannelCount; channel++) {
        if (channel == 0 || channel == 1 || channel == 3) {
            output << "    mov r0, " << song.channels[channel].parameter
                   << "\n";
            output << "    storew @(MUSIC_APU_CH" << channel
                   << "_BASE + MUSIC_CH_PARAM), r0\n";
        }
    }
    output << "    mov r0, MUSIC_WAVETABLE_LENGTH\n";
    output << "    storew @(MUSIC_APU_CH4_BASE + MUSIC_CH_LENGTH), r0\n";
    output << "    mov r0, 0\n";
    output << "    storew @(MUSIC_APU_CH4_BASE + MUSIC_CH_BASE), r0\n";
    output << "    mov r0, 6\n";
    output << "    storeb @(MUSIC_APU_CH4_BASE + MUSIC_CH_BASE + 2), r0\n";
    output << "    mov r0, MUSIC_PCM_LENGTH\n";
    output << "    storew @(MUSIC_APU_CH5_BASE + MUSIC_CH_LENGTH), r0\n";
    output << "    mov r0, 0x0100\n";
    output << "    storew @(MUSIC_APU_CH5_BASE + MUSIC_CH_BASE), r0\n";
    output << "    mov r0, 6\n";
    output << "    storeb @(MUSIC_APU_CH5_BASE + MUSIC_CH_BASE + 2), r0\n";
    output << "    mov r0, 0\n";
    output << "    storew @MUSIC_STEP_STATE, r0\n";
    output << "    storew @MUSIC_WAIT_STATE, r0\n";
    output << "    mov r0, 1\n";
    output << "    storew @MUSIC_PLAYING_STATE, r0\n";
    output << "    call music_update\n";
    output << "    pop r0\n";
    output << "    ret\n";

    output << "\nmusic_load_samples:\n";
    output << "    push r0\n";
    output << "    push r2\n";
    output << "    mov r2, 0\n";
    output << "music_load_wavetable_loop:\n";
    output << "    loadb r0, @(music_wavetable_data + r2)\n";
    output << "    storeb @(MUSIC_WAVETABLE_RAM + r2), r0\n";
    output << "    inc r2\n";
    output << "    cmp r2, MUSIC_WAVETABLE_LENGTH\n";
    output << "    blt music_load_wavetable_loop\n";
    if (!song.pcmSample.empty()) {
        output << "    mov r2, 0\n";
        output << "music_load_pcm_loop:\n";
        output << "    loadb r0, @(music_pcm_data + r2)\n";
        output << "    storeb @(MUSIC_PCM_RAM + r2), r0\n";
        output << "    inc r2\n";
        output << "    cmp r2, MUSIC_PCM_LENGTH\n";
        output << "    blt music_load_pcm_loop\n";
    }
    output << "    pop r2\n";
    output << "    pop r0\n";
    output << "    ret\n";

    output << "\nmusic_stop:\n";
    output << "    push r0\n";
    output << "    mov r0, 0\n";
    output << "    storew @MUSIC_PLAYING_STATE, r0\n";
    for (int channel = 0; channel < ChannelCount; channel++) {
        output << "    storew @(MUSIC_APU_CH" << channel
               << "_BASE + MUSIC_CH_CONTROL), r0\n";
    }
    output << "    pop r0\n";
    output << "    ret\n";

    std::array<int, MaxSteps> frameCounts = calculateStepFrames(song);
    std::array<std::array<int, ChannelCount>, MaxSteps> glideStarts{};
    std::array<std::array<int, ChannelCount>, MaxSteps> glideTargets{};
    std::array<int, ChannelCount> previousFrequencies{};
    bool hasGlides = false;
    for (int step = 0; step < song.stepCount; step++) {
        for (int channel = 0; channel < ChannelCount; channel++) {
            const Step &event = song.pattern[channel][step];
            if (event.note >= 0) {
                int target = std::clamp(
                    static_cast<int>(std::lround(eventFrequency(channel, event))),
                    1, 65535);
                if (event.glide && previousFrequencies[channel] > 0 &&
                    previousFrequencies[channel] != target &&
                    frameCounts[step] > 1) {
                    glideStarts[step][channel] = previousFrequencies[channel];
                    glideTargets[step][channel] = target;
                    hasGlides = true;
                }
                previousFrequencies[channel] = target;
            } else if (event.note == Stop ||
                       (event.note == Rest && channel != 5)) {
                previousFrequencies[channel] = 0;
            }
        }
    }

    output << "\nmusic_update:\n";
    output << "    push r0\n";
    output << "    push r1\n";
    output << "    loadw r0, @MUSIC_PLAYING_STATE\n";
    output << "    cmp r0, 0\n";
    output << "    beq music_update_done\n";
    output << "    loadw r0, @MUSIC_WAIT_STATE\n";
    output << "    cmp r0, 0\n";
    output << "    beq music_update_advance\n";
    if (hasGlides) {
        output << "    call music_glide_update\n";
        output << "    loadw r0, @MUSIC_WAIT_STATE\n";
    }
    output << "    dec r0\n";
    output << "    storew @MUSIC_WAIT_STATE, r0\n";
    output << "    bra music_update_done\n";
    output << "music_update_advance:\n";
    output << "    loadw r1, @MUSIC_STEP_STATE\n";
    for (int step = 0; step < song.stepCount; step++) {
        output << "    cmp r1, " << step << "\n";
        output << "    beq music_step_" << step << "\n";
    }
    output << "    call music_stop\n";
    output << "    bra music_update_done\n";

    for (int step = 0; step < song.stepCount; step++) {
        output << "music_step_" << step << ":\n";
        for (int channel = 0; channel < ChannelCount; channel++) {
            const Step &event = song.pattern[channel][step];
            if (event.note >= 0) {
                int frequency = std::clamp(
                    static_cast<int>(std::lround(eventFrequency(channel, event))),
                    1, 65535);
                int initial = glideStarts[step][channel] > 0
                                  ? glideStarts[step][channel]
                                  : frequency;
                output << "    mov r0, " << initial << "\n";
                output << "    storew @(MUSIC_APU_CH" << channel
                       << "_BASE + MUSIC_CH_FREQ), r0\n";
                output << "    mov r0, "
                       << hexadecimal(
                              channelVolume(song.channels[channel], event), 4)
                       << "\n";
                output << "    storew @(MUSIC_APU_CH" << channel
                       << "_BASE + MUSIC_CH_VOLUME), r0\n";
                int control = channel == 5 && song.pcmLoop ? 0x0007 : 0x0003;
                output << "    mov r0, " << hexadecimal(control, 4) << "\n";
                output << "    storew @(MUSIC_APU_CH" << channel
                       << "_BASE + MUSIC_CH_CONTROL), r0\n";
            } else if (event.note == Stop ||
                       (event.note == Rest && channel != 5)) {
                output << "    mov r0, 0\n";
                output << "    storew @(MUSIC_APU_CH" << channel
                       << "_BASE + MUSIC_CH_CONTROL), r0\n";
            }
        }
        output << "    mov r0, " << frameCounts[step] - 1 << "\n";
        output << "    storew @MUSIC_WAIT_STATE, r0\n";
        if (step + 1 == song.stepCount) {
            output << "    loadw r0, @MUSIC_LOOP_STATE\n";
            output << "    cmp r0, 0\n";
            output << "    beq music_last_step_once\n";
            output << "    mov r0, 0\n";
            output << "    bra music_last_step_store\n";
            output << "music_last_step_once:\n";
            output << "    mov r0, " << song.stepCount << "\n";
            output << "music_last_step_store:\n";
        } else {
            output << "    mov r0, " << step + 1 << "\n";
        }
        output << "    storew @MUSIC_STEP_STATE, r0\n";
        output << "    bra music_update_done\n";
    }
    output << "music_update_done:\n";
    output << "    pop r1\n";
    output << "    pop r0\n";
    output << "    ret\n";
    if (hasGlides) {
        output << "\nmusic_glide_update:\n";
        output << "    loadw r0, @MUSIC_STEP_STATE\n";
        for (int step = 0; step < song.stepCount; step++) {
            bool stepGlides = std::any_of(
                glideStarts[step].begin(), glideStarts[step].end(),
                [](int value) { return value > 0; });
            if (!stepGlides) {
                continue;
            }
            int next = step + 1 == song.stepCount ? 0 : step + 1;
            output << "    cmp r0, " << next << "\n";
            output << "    beq music_glide_step_" << step << "\n";
            if (step + 1 == song.stepCount) {
                output << "    cmp r0, " << song.stepCount << "\n";
                output << "    beq music_glide_step_" << step << "\n";
            }
        }
        output << "    ret\n";
        for (int step = 0; step < song.stepCount; step++) {
            bool stepGlides = std::any_of(
                glideStarts[step].begin(), glideStarts[step].end(),
                [](int value) { return value > 0; });
            if (!stepGlides) {
                continue;
            }
            output << "music_glide_step_" << step << ":\n";
            output << "    loadw r0, @MUSIC_WAIT_STATE\n";
            for (int remaining = 1; remaining < frameCounts[step]; remaining++) {
                output << "    cmp r0, " << remaining << "\n";
                output << "    beq music_glide_step_" << step << "_frame_"
                       << remaining << "\n";
            }
            output << "    ret\n";
            for (int remaining = 1; remaining < frameCounts[step]; remaining++) {
                output << "music_glide_step_" << step << "_frame_" << remaining
                       << ":\n";
                double progress =
                    static_cast<double>(frameCounts[step] - remaining) /
                    frameCounts[step];
                for (int channel = 0; channel < ChannelCount; channel++) {
                    int start = glideStarts[step][channel];
                    if (start == 0) {
                        continue;
                    }
                    int target = glideTargets[step][channel];
                    int frequency = static_cast<int>(
                        std::lround(start + (target - start) * progress));
                    output << "    mov r1, " << frequency << "\n";
                    output << "    storew @(MUSIC_APU_CH" << channel
                           << "_BASE + MUSIC_CH_FREQ), r1\n";
                }
                output << "    ret\n";
            }
        }
    }
    output << "\n";
    emitBytes(output, "music_wavetable_data", song.wavetable.data(),
              song.wavetable.size());
    emitBytes(output, "music_pcm_data", song.pcmSample.data(),
              song.pcmSample.size());
    output << "music_module_end:\n";

    std::string module = output.str();
    replaceAll(module, "MUSIC_", prefix + "_");
    replaceAll(module, "music_", prefix + "_music_");
    file << module;
    if (!file) {
        error = "Could not finish writing " + path;
        return false;
    }
    return true;
}

}

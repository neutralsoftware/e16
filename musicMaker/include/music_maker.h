#ifndef E16_MUSIC_MAKER_H
#define E16_MUSIC_MAKER_H

#include <SDL3/SDL.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace musicmaker {

constexpr int ChannelCount = 6;
constexpr int StepsPerPage = 16;
constexpr int MaxPages = 50;
constexpr int MaxSteps = StepsPerPage * MaxPages;
constexpr int Rest = -1;
constexpr int Hold = -2;
constexpr int Stop = -3;
constexpr int SampleRate = 48000;
constexpr int PcmSampleRate = 8000;
constexpr int MaxPcmSamples = PcmSampleRate * 2;
constexpr int WavetableSize = 32;

struct Step {
    int note = Rest;
    int velocity = 112;
};

struct ChannelSettings {
    std::string name;
    int volume = 200;
    int pan = 0;
    int parameter = 2;
    bool muted = false;
};

struct Song {
    std::string title = "Untitled";
    int bpm = 120;
    int stepCount = 32;
    int stepsPerBeat = 4;
    int swing = 0;
    std::array<ChannelSettings, ChannelCount> channels;
    std::array<std::array<Step, MaxSteps>, ChannelCount> pattern;
    std::array<std::uint8_t, WavetableSize> wavetable;
    std::vector<std::uint8_t> pcmSample;
    bool pcmLoop = false;
    bool trimPcmSilence = true;

    Song();
};

std::string noteName(int note);
double noteFrequency(int note);
std::array<int, MaxSteps> calculateStepFrames(const Song &song);
std::string exportSymbolPrefix(const std::string &path);
bool saveSong(const Song &song, const std::string &path, std::string &error);
bool loadSong(Song &song, const std::string &path, std::string &error);
bool exportE16(const Song &song, const std::string &path, std::string &error);

class AudioEngine {
  public:
    AudioEngine();
    ~AudioEngine();

    bool open(std::string &error);
    void close();
    void setSong(const Song &song);
    void play();
    void stop();
    void toggle();
    bool isPlaying() const;
    int playhead() const;
    void preview(int channel, int note);

  private:
    struct Voice {
        double phase = 0.0;
        int note = Rest;
        int velocity = 0;
        std::uint16_t lfsr = 0x4000;
        double samplePosition = 0.0;
    };

    SDL_AudioStream *stream = nullptr;
    mutable std::mutex mutex;
    Song song;
    std::array<Voice, ChannelCount> voices;
    std::vector<float> buffer;
    std::atomic<bool> playing = false;
    std::atomic<int> currentStep = 0;
    double samplesIntoStep = 0.0;
    int previewChannel = -1;
    int previewNote = Rest;
    int previewSamples = 0;

    static void audioCallback(void *userdata, SDL_AudioStream *stream,
                              int additionalAmount, int totalAmount);
    void render(float *output, int frames);
    void applyStep(int step);
    double stepSamples(int step) const;
    float voiceSample(int channel, Voice &voice);
    void resetVoices();
};

class PcmRecorder {
  public:
    PcmRecorder();
    ~PcmRecorder();

    bool start(std::string &error);
    void poll();
    std::vector<std::uint8_t> stop(bool trimLeadingSilence);
    void cancel();
    bool isRecording() const;
    int sampleCount() const;

  private:
    SDL_AudioStream *stream = nullptr;
    std::vector<float> samples;

    static void trimSilence(std::vector<float> &samples);
};

class App {
  public:
    App();
    ~App();

    bool open(std::string &error);
    int run();

  private:
    enum class DialogAction { None, Open, Save, Export };

    struct Rect {
        float x;
        float y;
        float w;
        float h;
    };

    SDL_Window *window = nullptr;
    SDL_Renderer *renderer = nullptr;
    AudioEngine audio;
    PcmRecorder recorder;
    Song song;
    bool running = true;
    bool dirty = false;
    int selectedChannel = 0;
    int selectedStep = 0;
    int octave = 4;
    int page = 0;
    int selectionAnchorChannel = 0;
    int selectionAnchorStep = 0;
    int selectionEndChannel = 0;
    int selectionEndStep = 0;
    bool selectionActive = true;
    bool draggingSelection = false;
    std::string projectPath;
    std::string status = "Ready";
    Uint64 statusUntil = 0;
    std::mutex dialogMutex;
    DialogAction dialogAction = DialogAction::None;
    DialogAction pendingAction = DialogAction::None;
    std::string pendingPath;

    void processEvents();
    void update();
    void render();
    void handleKey(const SDL_KeyboardEvent &event);
    void handleMouse(const SDL_MouseButtonEvent &event);
    void handleMouseMotion(const SDL_MouseMotionEvent &event);
    void handleWheel(const SDL_MouseWheelEvent &event);
    void selectNote(int note);
    void transposeSelected(int amount);
    void selectCurrentCell();
    void extendSelectionTo(int channel, int step);
    bool cellIsSelected(int channel, int step) const;
    void copySelection(bool cut);
    void pasteSelection();
    void selectAllPattern();
    void clearSelection();
    void markChanged();
    void setStatus(const std::string &text, int milliseconds = 3500);
    bool confirmDiscard();
    void newSong();
    void openDialog();
    void saveProject(bool choosePath);
    void exportProject();
    void openProject(const std::string &path);
    void consumeDialogResult();
    void changeMaxPages(int direction);
    void changePage(int direction);
    void finishRecording();
    void editWavetable(float x, float y);
    void setWavetablePreset(int preset);
    void drawWavetableEditor();
    void drawPcmEditor();
    void drawRect(const Rect &rect, SDL_Color color, bool filled = true);
    void drawText(float x, float y, const std::string &text, SDL_Color color,
                  float scale = 1.0f);
    void drawButton(const Rect &rect, const std::string &label, bool active,
                    SDL_Color accent);
    bool contains(const Rect &rect, float x, float y) const;
    static void SDLCALL dialogCallback(void *userdata,
                                       const char *const *filelist, int filter);
};

}

#endif

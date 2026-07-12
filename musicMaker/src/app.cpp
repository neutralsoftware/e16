#include "music_maker.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <sstream>
#include <utility>

namespace musicmaker {

namespace {
constexpr int WindowWidth = 1280;
constexpr int WindowHeight = 900;
constexpr float GridX = 170.0f;
constexpr float GridY = 168.0f;
constexpr float GridWidth = 1080.0f;
constexpr float RowHeight = 58.0f;
constexpr int VisibleSteps = StepsPerPage;

constexpr SDL_Color Background{9, 12, 20, 255};
constexpr SDL_Color Surface{20, 25, 38, 255};
constexpr SDL_Color SurfaceRaised{31, 38, 55, 255};
constexpr SDL_Color Border{67, 79, 104, 255};
constexpr SDL_Color Text{226, 232, 240, 255};
constexpr SDL_Color Muted{139, 151, 170, 255};
constexpr SDL_Color Cyan{66, 211, 210, 255};
constexpr SDL_Color Pink{245, 111, 160, 255};
constexpr SDL_Color Yellow{244, 194, 96, 255};
constexpr SDL_Color Green{105, 219, 154, 255};
constexpr SDL_Color Red{244, 105, 116, 255};
constexpr SDL_Color Blue{104, 151, 245, 255};
constexpr SDL_Color Purple{181, 126, 240, 255};

constexpr std::array<SDL_Color, ChannelCount> ChannelColors{
    Cyan, Pink, Green, Yellow, Blue, Purple};
constexpr std::array<const char *, 12> PitchNames{
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

std::string withExtension(const std::string &path,
                          const std::string &extension) {
    std::filesystem::path result(path);
    if (result.extension().empty()) {
        result += extension;
    }
    return result.string();
}
}

App::App() = default;

App::~App() {
    recorder.cancel();
    audio.close();
    if (renderer) {
        SDL_DestroyRenderer(renderer);
    }
    if (window) {
        SDL_DestroyWindow(window);
    }
    SDL_Quit();
}

bool App::open(std::string &error) {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
        error = SDL_GetError();
        return false;
    }
    window = SDL_CreateWindow("E16 Music Maker", WindowWidth, WindowHeight,
                              SDL_WINDOW_RESIZABLE);
    if (!window) {
        error = SDL_GetError();
        return false;
    }
    SDL_SetWindowMinimumSize(window, 960, 675);
    renderer = SDL_CreateRenderer(window, nullptr);
    if (!renderer) {
        error = SDL_GetError();
        return false;
    }
    if (!SDL_SetRenderLogicalPresentation(renderer, WindowWidth, WindowHeight,
                                          SDL_LOGICAL_PRESENTATION_LETTERBOX)) {
        error = SDL_GetError();
        return false;
    }
    if (!audio.open(error)) {
        return false;
    }
    audio.setSong(song);
    return true;
}

int App::run() {
    while (running) {
        processEvents();
        update();
        render();
        SDL_Delay(1);
    }
    return 0;
}

void App::processEvents() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        SDL_ConvertEventToRenderCoordinates(renderer, &event);
        if (event.type == SDL_EVENT_QUIT) {
            if (confirmDiscard()) {
                running = false;
            }
        } else if (event.type == SDL_EVENT_KEY_DOWN) {
            handleKey(event.key);
        } else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
            handleMouse(event.button);
        } else if (event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
            draggingSelection = false;
        } else if (event.type == SDL_EVENT_MOUSE_MOTION) {
            handleMouseMotion(event.motion);
        } else if (event.type == SDL_EVENT_MOUSE_WHEEL) {
            handleWheel(event.wheel);
        } else if (event.type == SDL_EVENT_DROP_FILE && event.drop.data) {
            std::filesystem::path dropped(event.drop.data);
            std::string extension = dropped.extension().string();
            std::transform(extension.begin(), extension.end(), extension.begin(),
                           [](unsigned char value) { return std::tolower(value); });
            if (extension == ".mp3" || extension == ".wav" ||
                extension == ".flac" || extension == ".ogg") {
                std::vector<std::uint8_t> imported;
                std::string error;
                if (importPcmFile(event.drop.data, song.trimPcmSilence, imported,
                                  error)) {
                    song.pcmSample = std::move(imported);
                    selectedChannel = 5;
                    markChanged();
                    setStatus("Imported PCM audio from " + dropped.filename().string(),
                              6000);
                } else {
                    setStatus("Could not import audio: " + error, 6000);
                }
            } else if (confirmDiscard()) {
                openProject(event.drop.data);
            }
        }
    }
}

void App::update() {
    consumeDialogResult();
    if (recorder.isRecording()) {
        recorder.poll();
        if (recorder.sampleCount() >= MaxPcmSamples) {
            finishRecording();
        }
    }
    if (audio.isPlaying()) {
        int playhead = audio.playhead();
        if (playhead / VisibleSteps != page) {
            page = playhead / VisibleSteps;
        }
    }
    if (statusUntil != 0 && SDL_GetTicks() > statusUntil) {
        status = "Ready";
        statusUntil = 0;
    }
}

void App::render() {
    SDL_SetRenderDrawColor(renderer, Background.r, Background.g, Background.b,
                           Background.a);
    SDL_RenderClear(renderer);

    drawRect({0, 0, WindowWidth, 58}, SurfaceRaised, true);
    drawRect({0, 56, WindowWidth, 2}, Cyan, true);
    drawText(20, 16, "E16 MUSIC MAKER", Text, 1.6f);
    drawText(244, 20, song.title + (dirty ? "  *" : ""), Muted, 1.0f);
    drawText(1100, 20, "6-CHANNEL APU", Cyan, 1.0f);

    drawButton({20, 76, 62, 34}, "NEW", false, Cyan);
    drawButton({89, 76, 62, 34}, "OPEN", false, Cyan);
    drawButton({158, 76, 62, 34}, "SAVE", false, Cyan);
    drawButton({227, 76, 78, 34}, "EXPORT", false, Green);
    std::string playLabel = audio.isPlaying()
                                ? "STOP"
                                : "PLAY @" + std::to_string(selectedStep + 1);
    drawButton({326, 76, 82, 34}, playLabel,
               audio.isPlaying(), Pink);

    drawText(435, 88, "BPM " + std::to_string(song.bpm), Text);
    drawButton({500, 76, 30, 34}, "-", false, Cyan);
    drawButton({536, 76, 30, 34}, "+", false, Cyan);
    drawText(596, 88, "STEP 1/" + std::to_string(song.stepsPerBeat), Text);
    drawButton({686, 76, 30, 34}, "-", false, Cyan);
    drawButton({722, 76, 30, 34}, "+", false, Cyan);
    drawText(782, 88, "SWING " + std::to_string(song.swing) + "%", Text);
    drawButton({872, 76, 30, 34}, "-", false, Yellow);
    drawButton({908, 76, 30, 34}, "+", false, Yellow);
    drawText(958, 88,
             "MAXPAGES " + std::to_string(song.stepCount / StepsPerPage),
             Text);
    drawButton({1045, 76, 30, 34}, "-", false, Green);
    drawButton({1081, 76, 30, 34}, "+", false, Green);
    drawText(1125, 88, "PAGE " + std::to_string(page + 1), Text);
    drawButton({1174, 76, 30, 34}, "<", false, Cyan);
    drawButton({1210, 76, 30, 34}, ">", false, Cyan);

    drawText(20, 132, "CHANNEL", Muted);
    int firstStep = page * VisibleSteps;
    float cellWidth = GridWidth / VisibleSteps;
    for (int visible = 0; visible < VisibleSteps; visible++) {
        int step = firstStep + visible;
        if (step >= song.stepCount) {
            break;
        }
        SDL_Color numberColor = step % song.stepsPerBeat == 0 ? Text : Muted;
        drawText(GridX + visible * cellWidth + 5, 132, std::to_string(step + 1),
                 numberColor);
    }

    int playhead = audio.isPlaying() ? audio.playhead() : -1;
    for (int channel = 0; channel < ChannelCount; channel++) {
        float y = GridY + channel * RowHeight;
        bool channelSelected = channel == selectedChannel;
        drawRect({12, y, 145, RowHeight - 6},
                 channelSelected ? SurfaceRaised : Surface, true);
        drawRect({12, y, 5, RowHeight - 6}, ChannelColors[channel], true);
        drawText(28, y + 10, song.channels[channel].name,
                 song.channels[channel].muted ? Muted : Text);
        drawText(28, y + 31, "SELECT", Muted);
        drawButton({116, y + 25, 32, 22}, "M", song.channels[channel].muted,
                   Red);

        for (int visible = 0; visible < VisibleSteps; visible++) {
            int step = firstStep + visible;
            if (step >= song.stepCount) {
                break;
            }
            float x = GridX + visible * cellWidth;
            bool selected = channel == selectedChannel && step == selectedStep;
            bool rangeSelected = cellIsSelected(channel, step);
            bool playingCell = step == playhead;
            SDL_Color cellColor =
                (step % song.stepsPerBeat == 0) ? SurfaceRaised : Surface;
            if (playingCell) {
                cellColor = {45, 70, 72, 255};
            }
            if (selected) {
                cellColor = {52, 62, 86, 255};
            } else if (rangeSelected) {
                cellColor = {43, 52, 75, 255};
            }
            Rect cell{x + 2, y, cellWidth - 4, RowHeight - 6};
            drawRect(cell, cellColor, true);
            drawRect(cell,
                     selected        ? ChannelColors[channel]
                     : rangeSelected ? Cyan
                                     : Border,
                     false);
            const Step &event = song.pattern[channel][step];
            SDL_Color noteColor =
                event.note >= 0 ? ChannelColors[channel] : Muted;
            std::string cellText =
                channel == 5 && event.note >= 0 ? "SMP" : noteName(event.note);
            drawText(x + 9, y + 20, cellText, noteColor);
            if (event.note >= 0) {
                if (event.glide) {
                    drawText(x + cellWidth - 15, y + 7, "G", Purple);
                } else if (event.tuning != 0) {
                    drawText(x + cellWidth - 15, y + 7,
                             event.tuning > 0 ? "+" : "-", Blue);
                }
                float meter = (cellWidth - 14) * event.velocity / 127.0f;
                drawRect({x + 7, y + 43, meter, 3}, ChannelColors[channel],
                         true);
            }
        }
        for (int visible = 0; visible < VisibleSteps; visible++) {
            int step = firstStep + visible;
            const TripletGroup &group = song.triplets[channel][step];
            if (!group.active || group.duration < 1 ||
                step + group.duration > song.stepCount ||
                visible + group.duration > VisibleSteps) {
                continue;
            }
            float groupX = GridX + visible * cellWidth;
            float tripletWidth = cellWidth * group.duration / 3.0f;
            for (int slot = 0; slot < 3; slot++) {
                Rect cell{groupX + slot * tripletWidth + 2, y, tripletWidth - 4,
                          RowHeight - 6};
                bool selected = channel == selectedChannel &&
                                selectedTripletStart == step &&
                                selectedTripletSlot == slot;
                drawRect(cell,
                         selected ? SDL_Color{52, 62, 86, 255} : SurfaceRaised,
                         true);
                drawRect(cell, selected ? ChannelColors[channel] : Purple,
                         false);
                const Step &event = group.events[slot];
                std::string text = channel == 5 && event.note >= 0
                                       ? "SMP"
                                       : noteName(event.note);
                drawText(cell.x + 6, y + 20, text,
                         event.note >= 0 ? ChannelColors[channel] : Muted);
                drawText(cell.x + cell.w - 12, y + 6, "3", Purple);
            }
        }
    }

    const Step &selected = selectedEvent();
    std::string selectedName = selectedChannel == 5 && selected.note >= 0
                                   ? "SAMPLE"
                                   : noteName(selected.note);
    int selectedRows =
        std::abs(selectionEndChannel - selectionAnchorChannel) + 1;
    int selectedColumns = std::abs(selectionEndStep - selectionAnchorStep) + 1;
    drawText(20, 535,
             "CH " + std::to_string(selectedChannel + 1) + "  STEP " +
                 std::to_string(selectedStep + 1) + "  NOTE " + selectedName +
                 "  SELECT " + std::to_string(selectedRows) + "x" +
                 std::to_string(selectedColumns),
             Text, 1.15f);
    drawRect({12, 548, 928, 48}, Surface, true);
    drawText(20, 576, "OCTAVE " + std::to_string(octave), Muted);
    drawButton({98, 562, 32, 34}, "-", false, Cyan);
    drawButton({136, 562, 32, 34}, "+", false, Cyan);
    drawText(205, 576, "NOTE VOL " + std::to_string(selected.velocity), Muted);
    drawButton({302, 562, 32, 34}, "-", false, Pink);
    drawButton({340, 562, 32, 34}, "+", false, Pink);
    drawButton({405, 562, 74, 34}, selectedChannel == 5 ? "STOP" : "HOLD",
               selected.note == (selectedChannel == 5 ? Stop : Hold), Yellow);
    drawButton({486, 562, 74, 34}, selectedChannel == 5 ? "EMPTY" : "REST",
               selected.note == Rest, Red);
    std::string tuning = (selected.tuning >= 0 ? "+" : "") +
                         std::to_string(selected.tuning) + "c";
    drawText(590, 576, "FREQ " + tuning, Muted);
    drawButton({680, 562, 32, 34}, "-", false, Blue);
    drawButton({718, 562, 32, 34}, "+", false, Blue);
    drawButton({770, 562, 92, 34}, "GLISSANDO", selected.glide, Purple);
    drawButton({870, 562, 68, 34}, "TRIPLET", selectedTripletSlot >= 0, Purple);

    if (selectedChannel == 4) {
        drawWavetableEditor();
    } else if (selectedChannel == 5) {
        drawPcmEditor();
    } else {
        for (int pitch = 0; pitch < 12; pitch++) {
            Rect key{20.0f + pitch * 76.0f, 630.0f, 70.0f, 110.0f};
            bool sharp = PitchNames[pitch][1] == '#';
            SDL_Color keyColor =
                sharp ? SurfaceRaised : SDL_Color{214, 222, 233, 255};
            drawRect(key, keyColor, true);
            drawRect(key, sharp ? ChannelColors[selectedChannel] : Border,
                     false);
            drawText(key.x + 21, key.y + 46,
                     std::string(PitchNames[pitch]) + std::to_string(octave),
                     sharp ? Text : SDL_Color{24, 30, 45, 255});
        }
    }

    drawRect({960, 616, 290, 160}, Surface, true);
    drawText(978, 631, song.channels[selectedChannel].name, Text, 1.2f);
    drawText(978, 665,
             "VOLUME " + std::to_string(song.channels[selectedChannel].volume),
             Muted);
    drawButton({1090, 651, 32, 34}, "-", false, ChannelColors[selectedChannel]);
    drawButton({1128, 651, 32, 34}, "+", false, ChannelColors[selectedChannel]);
    drawText(978, 702,
             "PAN " + std::to_string(song.channels[selectedChannel].pan),
             Muted);
    drawButton({1090, 688, 32, 34}, "-", false, ChannelColors[selectedChannel]);
    drawButton({1128, 688, 32, 34}, "+", false, ChannelColors[selectedChannel]);
    std::string parameter =
        selectedChannel < 2
            ? "DUTY " + std::to_string(song.channels[selectedChannel].parameter)
        : selectedChannel == 3
            ? "NOISE " +
                  std::to_string(song.channels[selectedChannel].parameter)
        : selectedChannel == 4 ? "PREVIEW"
        : selectedChannel == 5 ? (song.pcmLoop ? "LOOP ON" : "ONE SHOT")
                               : "TRIANGLE";
    drawButton({1170, 651, 72, 71}, parameter,
               selectedChannel == 5 && song.pcmLoop,
               ChannelColors[selectedChannel]);

    drawRect({0, 800, WindowWidth, 100}, Surface, true);
    drawText(20, 819, status, status == "Ready" ? Muted : Green);
    drawText(20, 852,
             "Export API: call <PROJECT>_music_play or _music_play_once, then "
             "_music_update once per frame.",
             Cyan);
    drawText(
        20, 875,
        "Select a cell to choose the playback start. Per-note volume, tuning "
        "and glissando are copied with pattern selections.",
        Muted);

    SDL_RenderPresent(renderer);
}

void App::handleKey(const SDL_KeyboardEvent &event) {
    bool command = ((event.mod | SDL_GetModState()) & SDL_KMOD_GUI) != 0;
    bool octaveUp = event.scancode == SDL_SCANCODE_UP || event.key == SDLK_UP;
    bool octaveDown =
        event.scancode == SDL_SCANCODE_DOWN || event.key == SDLK_DOWN;
    if (command && octaveUp) {
        transposeSelected(12);
        return;
    }
    if (command && octaveDown) {
        transposeSelected(-12);
        return;
    }
    if (event.repeat) {
        return;
    }
    bool control = (event.mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0;
    bool shift = (event.mod & SDL_KMOD_SHIFT) != 0;
    if (control) {
        if (event.scancode == SDL_SCANCODE_C) {
            copySelection(false);
        } else if (event.scancode == SDL_SCANCODE_X) {
            copySelection(true);
        } else if (event.scancode == SDL_SCANCODE_V) {
            pasteSelection();
        } else if (event.scancode == SDL_SCANCODE_A) {
            selectAllPattern();
        } else if (event.scancode == SDL_SCANCODE_N) {
            newSong();
        } else if (event.scancode == SDL_SCANCODE_O) {
            openDialog();
        } else if (event.scancode == SDL_SCANCODE_S) {
            saveProject(shift);
        } else if (event.scancode == SDL_SCANCODE_E) {
            exportProject();
        } else if (event.scancode == SDL_SCANCODE_T) {
            makeTriplets();
        }
        return;
    }

    if (event.scancode == SDL_SCANCODE_SPACE) {
        audio.toggle(selectedStep);
        return;
    }
    if (event.scancode == SDL_SCANCODE_LEFT) {
        if (selectedTripletSlot > 0) {
            selectedTripletSlot--;
            return;
        }
        if (selectedTripletSlot == 0) {
            selectedStep =
                (selectedTripletStart + song.stepCount - 1) % song.stepCount;
            page = selectedStep / VisibleSteps;
            selectCurrentCell();
            return;
        }
        selectedStep = (selectedStep + song.stepCount - 1) % song.stepCount;
        page = selectedStep / VisibleSteps;
        if (shift) {
            extendSelectionTo(selectedChannel, selectedStep);
        } else {
            selectCurrentCell();
            int start = tripletStartFor(selectedChannel, selectedStep);
            if (start >= 0) {
                selectedTripletStart = start;
                selectedTripletSlot = 2;
                selectedStep = start;
                selectionAnchorStep = start;
                selectionEndStep =
                    start + song.triplets[selectedChannel][start].duration - 1;
            }
        }
        return;
    }
    if (event.scancode == SDL_SCANCODE_RIGHT) {
        if (selectedTripletSlot >= 0 && selectedTripletSlot < 2) {
            selectedTripletSlot++;
            return;
        }
        if (selectedTripletSlot == 2) {
            selectedStep = (selectedTripletStart +
                            song.triplets[selectedChannel][selectedTripletStart]
                                .duration) %
                           song.stepCount;
            page = selectedStep / VisibleSteps;
            selectCurrentCell();
            return;
        }
        selectedStep = (selectedStep + 1) % song.stepCount;
        page = selectedStep / VisibleSteps;
        if (shift) {
            extendSelectionTo(selectedChannel, selectedStep);
        } else {
            selectCurrentCell();
            if (song.triplets[selectedChannel][selectedStep].active) {
                selectedTripletStart = selectedStep;
                selectedTripletSlot = 0;
                selectionEndStep =
                    selectedStep +
                    song.triplets[selectedChannel][selectedStep].duration - 1;
            }
        }
        return;
    }
    if (event.scancode == SDL_SCANCODE_UP) {
        if (shift) {
            selectedChannel = std::max(0, selectedChannel - 1);
            extendSelectionTo(selectedChannel, selectedStep);
        } else {
            transposeSelected(1);
        }
        return;
    }
    if (event.scancode == SDL_SCANCODE_DOWN) {
        if (shift) {
            selectedChannel = std::min(ChannelCount - 1, selectedChannel + 1);
            extendSelectionTo(selectedChannel, selectedStep);
        } else {
            transposeSelected(-1);
        }
        return;
    }
    if (event.scancode == SDL_SCANCODE_PAGEUP) {
        changePage(1);
        return;
    }
    if (event.scancode == SDL_SCANCODE_PAGEDOWN) {
        changePage(-1);
        return;
    }
    if (event.scancode == SDL_SCANCODE_DELETE ||
        event.scancode == SDL_SCANCODE_BACKSPACE) {
        clearSelection();
        return;
    }
    if (event.scancode == SDL_SCANCODE_PERIOD) {
        selectedEvent().note = selectedChannel == 5 ? Stop : Hold;
        markChanged();
        return;
    }
    if (event.scancode == SDL_SCANCODE_RETURN && selectedChannel == 5) {
        selectNote(60);
        return;
    }
    if (event.scancode == SDL_SCANCODE_LEFTBRACKET) {
        Step &step = selectedEvent();
        step.velocity = std::max(1, step.velocity - 8);
        markChanged();
        return;
    }
    if (event.scancode == SDL_SCANCODE_RIGHTBRACKET) {
        Step &step = selectedEvent();
        step.velocity = std::min(127, step.velocity + 8);
        markChanged();
        return;
    }

    static const std::array<std::pair<SDL_Scancode, int>, 24> keys{
        {{SDL_SCANCODE_Z, 0},  {SDL_SCANCODE_S, 1},  {SDL_SCANCODE_X, 2},
         {SDL_SCANCODE_D, 3},  {SDL_SCANCODE_C, 4},  {SDL_SCANCODE_V, 5},
         {SDL_SCANCODE_G, 6},  {SDL_SCANCODE_B, 7},  {SDL_SCANCODE_H, 8},
         {SDL_SCANCODE_N, 9},  {SDL_SCANCODE_J, 10}, {SDL_SCANCODE_M, 11},
         {SDL_SCANCODE_Q, 12}, {SDL_SCANCODE_2, 13}, {SDL_SCANCODE_W, 14},
         {SDL_SCANCODE_3, 15}, {SDL_SCANCODE_E, 16}, {SDL_SCANCODE_R, 17},
         {SDL_SCANCODE_5, 18}, {SDL_SCANCODE_T, 19}, {SDL_SCANCODE_6, 20},
         {SDL_SCANCODE_Y, 21}, {SDL_SCANCODE_7, 22}, {SDL_SCANCODE_U, 23}}};
    for (const auto &[scancode, offset] : keys) {
        if (event.scancode == scancode) {
            selectNote((octave + 1) * 12 + offset);
            return;
        }
    }
}

void App::handleMouse(const SDL_MouseButtonEvent &event) {
    float x = event.x;
    float y = event.y;
    if (event.button == SDL_BUTTON_LEFT) {
        if (contains({20, 76, 62, 34}, x, y)) {
            newSong();
            return;
        }
        if (contains({89, 76, 62, 34}, x, y)) {
            openDialog();
            return;
        }
        if (contains({158, 76, 62, 34}, x, y)) {
            saveProject(false);
            return;
        }
        if (contains({227, 76, 78, 34}, x, y)) {
            exportProject();
            return;
        }
        if (contains({326, 76, 82, 34}, x, y)) {
            audio.toggle(selectedStep);
            return;
        }
        if (contains({500, 76, 30, 34}, x, y) ||
            contains({536, 76, 30, 34}, x, y)) {
            song.bpm = std::clamp(song.bpm + (x < 530 ? -5 : 5), 30, 300);
            markChanged();
            return;
        }
        if (contains({686, 76, 30, 34}, x, y) ||
            contains({722, 76, 30, 34}, x, y)) {
            song.stepsPerBeat =
                std::clamp(song.stepsPerBeat + (x < 716 ? -1 : 1), 1, 8);
            markChanged();
            return;
        }
        if (contains({872, 76, 30, 34}, x, y) ||
            contains({908, 76, 30, 34}, x, y)) {
            song.swing = std::clamp(song.swing + (x < 902 ? -2 : 2), 0, 40);
            markChanged();
            return;
        }
        if (contains({1045, 76, 30, 34}, x, y) ||
            contains({1081, 76, 30, 34}, x, y)) {
            changeMaxPages(x < 1075 ? -1 : 1);
            return;
        }
        if (contains({1174, 76, 30, 34}, x, y)) {
            changePage(-1);
            return;
        }
        if (contains({1210, 76, 30, 34}, x, y)) {
            changePage(1);
            return;
        }
    }

    for (int channel = 0; channel < ChannelCount; channel++) {
        float rowY = GridY + channel * RowHeight;
        if (contains({12, rowY, 145, RowHeight - 6}, x, y)) {
            if ((event.button == SDL_BUTTON_LEFT &&
                 contains({116, rowY + 25, 32, 22}, x, y)) ||
                event.button == SDL_BUTTON_RIGHT) {
                song.channels[channel].muted = !song.channels[channel].muted;
                markChanged();
            } else if (event.button == SDL_BUTTON_LEFT) {
                selectedChannel = channel;
                selectCurrentCell();
            }
            return;
        }
    }

    if (x >= GridX && x < GridX + GridWidth && y >= GridY &&
        y < GridY + ChannelCount * RowHeight) {
        int channel = static_cast<int>((y - GridY) / RowHeight);
        int visible =
            static_cast<int>((x - GridX) / (GridWidth / VisibleSteps));
        int step = page * VisibleSteps + visible;
        if (channel >= 0 && channel < ChannelCount && step >= 0 &&
            step < song.stepCount) {
            selectedChannel = channel;
            selectedStep = step;
            int tripletStart = tripletStartFor(channel, step);
            if ((event.button == SDL_BUTTON_LEFT ||
                 event.button == SDL_BUTTON_RIGHT) &&
                tripletStart >= 0) {
                float groupX = GridX + (tripletStart - page * VisibleSteps) *
                                           (GridWidth / VisibleSteps);
                int duration = song.triplets[channel][tripletStart].duration;
                float groupWidth = duration * GridWidth / VisibleSteps;
                selectedTripletStart = tripletStart;
                selectedTripletSlot = std::clamp(
                    static_cast<int>((x - groupX) / (groupWidth / 3.0f)), 0, 2);
                selectedStep = tripletStart;
                selectionAnchorChannel = channel;
                selectionEndChannel = channel;
                selectionAnchorStep = tripletStart;
                selectionEndStep = tripletStart + duration - 1;
                selectionActive = true;
                draggingSelection = false;
                if (event.button == SDL_BUTTON_RIGHT) {
                    selectedEvent().note = Rest;
                    markChanged();
                } else if (event.clicks >= 2) {
                    selectNote(channel == 5 ? 60 : (octave + 1) * 12);
                }
                return;
            }
            selectedTripletStart = -1;
            selectedTripletSlot = -1;
            if (event.button == SDL_BUTTON_RIGHT) {
                selectCurrentCell();
                song.pattern[channel][step].note = Rest;
                markChanged();
            } else if (event.button == SDL_BUTTON_LEFT) {
                bool extend = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
                if (!extend || !selectionActive) {
                    selectCurrentCell();
                } else {
                    extendSelectionTo(channel, step);
                }
                draggingSelection = true;
                if (event.clicks >= 2) {
                    selectNote(channel == 5 ? 60 : (octave + 1) * 12);
                }
            }
        }
        return;
    }

    if (event.button != SDL_BUTTON_LEFT) {
        return;
    }
    if (contains({98, 562, 32, 34}, x, y) ||
        contains({136, 562, 32, 34}, x, y)) {
        octave = std::clamp(octave + (x < 130 ? -1 : 1), 1, 7);
        return;
    }
    if (contains({302, 562, 32, 34}, x, y) ||
        contains({340, 562, 32, 34}, x, y)) {
        Step &step = selectedEvent();
        step.velocity = std::clamp(step.velocity + (x < 334 ? -8 : 8), 1, 127);
        markChanged();
        return;
    }
    if (contains({405, 562, 74, 34}, x, y)) {
        selectedEvent().note = selectedChannel == 5 ? Stop : Hold;
        markChanged();
        return;
    }
    if (contains({486, 562, 74, 34}, x, y)) {
        selectedEvent().note = Rest;
        markChanged();
        return;
    }
    if (contains({680, 562, 32, 34}, x, y) ||
        contains({718, 562, 32, 34}, x, y)) {
        Step &step = selectedEvent();
        step.tuning =
            std::clamp(step.tuning + (x < 712 ? -25 : 25), -2400, 2400);
        markChanged();
        return;
    }
    if (contains({770, 562, 92, 34}, x, y)) {
        Step &step = selectedEvent();
        step.glide = !step.glide;
        markChanged();
        return;
    }
    if (contains({870, 562, 68, 34}, x, y)) {
        makeTriplets();
        return;
    }
    if (selectedChannel == 4) {
        if (contains({20, 620, 920, 120}, x, y)) {
            editWavetable(x, y);
            return;
        }
        for (int preset = 0; preset < 5; preset++) {
            if (contains({20.0f + preset * 112.0f, 750, 104, 34}, x, y)) {
                setWavetablePreset(preset);
                return;
            }
        }
    } else if (selectedChannel == 5) {
        if (contains({20, 750, 104, 34}, x, y)) {
            if (recorder.isRecording()) {
                finishRecording();
            } else {
                std::string error;
                audio.stop();
                if (recorder.start(error)) {
                    setStatus("Recording PCM from the default microphone",
                              6000);
                } else {
                    setStatus("Could not start recording: " + error, 6000);
                }
            }
            return;
        }
        if (contains({132, 750, 104, 34}, x, y)) {
            audio.preview(5, 60);
            return;
        }
        if (contains({244, 750, 104, 34}, x, y)) {
            selectNote(60);
            return;
        }
        if (contains({356, 750, 104, 34}, x, y)) {
            song.pcmSample.clear();
            markChanged();
            setStatus("Cleared the PCM sample");
            return;
        }
        if (contains({468, 750, 104, 34}, x, y)) {
            song.trimPcmSilence = !song.trimPcmSilence;
            markChanged();
            setStatus(song.trimPcmSilence
                          ? "Automatic leading-silence trimming enabled"
                          : "Automatic leading-silence trimming disabled");
            return;
        }
        if (contains({580, 750, 104, 34}, x, y)) {
            importPcmDialog();
            return;
        }
    } else {
        for (int pitch = 0; pitch < 12; pitch++) {
            if (contains({20.0f + pitch * 76.0f, 630, 70, 110}, x, y)) {
                selectNote((octave + 1) * 12 + pitch);
                return;
            }
        }
    }
    if (contains({1090, 651, 32, 34}, x, y) ||
        contains({1128, 651, 32, 34}, x, y)) {
        ChannelSettings &settings = song.channels[selectedChannel];
        settings.volume =
            std::clamp(settings.volume + (x < 1122 ? -8 : 8), 0, 255);
        markChanged();
        return;
    }
    if (contains({1090, 688, 32, 34}, x, y) ||
        contains({1128, 688, 32, 34}, x, y)) {
        ChannelSettings &settings = song.channels[selectedChannel];
        settings.pan =
            std::clamp(settings.pan + (x < 1122 ? -10 : 10), -100, 100);
        markChanged();
        return;
    }
    if (contains({1170, 651, 72, 71}, x, y) && selectedChannel == 5) {
        song.pcmLoop = !song.pcmLoop;
        markChanged();
        return;
    }
    if (contains({1170, 651, 72, 71}, x, y) && selectedChannel == 4) {
        audio.preview(4, (octave + 1) * 12);
        return;
    }
    if (contains({1170, 651, 72, 71}, x, y) &&
        (selectedChannel < 2 || selectedChannel == 3)) {
        ChannelSettings &settings = song.channels[selectedChannel];
        int maximum = selectedChannel == 3 ? 1 : 3;
        settings.parameter = (settings.parameter + 1) % (maximum + 1);
        markChanged();
    }
}

void App::handleMouseMotion(const SDL_MouseMotionEvent &event) {
    if (draggingSelection && (event.state & SDL_BUTTON_LMASK) != 0 &&
        event.x >= GridX && event.x < GridX + GridWidth && event.y >= GridY &&
        event.y < GridY + ChannelCount * RowHeight) {
        int channel =
            std::clamp(static_cast<int>((event.y - GridY) / RowHeight), 0,
                       ChannelCount - 1);
        int visible = std::clamp(
            static_cast<int>((event.x - GridX) / (GridWidth / VisibleSteps)), 0,
            VisibleSteps - 1);
        int step = std::min(page * VisibleSteps + visible, song.stepCount - 1);
        selectedChannel = channel;
        selectedStep = step;
        extendSelectionTo(channel, step);
        return;
    }
    if (selectedChannel == 4 && (event.state & SDL_BUTTON_LMASK) != 0 &&
        contains({20, 620, 920, 120}, event.x, event.y)) {
        editWavetable(event.x, event.y);
    }
}

void App::handleWheel(const SDL_MouseWheelEvent &event) {
    if (event.y > 0) {
        transposeSelected(1);
    } else if (event.y < 0) {
        transposeSelected(-1);
    }
}

void App::selectNote(int note) {
    Step &step = selectedEvent();
    step.note = selectedChannel == 5 ? 60 : std::clamp(note, 0, 127);
    audio.preview(selectedChannel, step.note);
    markChanged();
    if (selectedTripletSlot >= 0) {
        if (selectedTripletSlot < 2) {
            selectedTripletSlot++;
            return;
        }
        selectedStep =
            selectedTripletStart +
            song.triplets[selectedChannel][selectedTripletStart].duration;
        selectedTripletStart = -1;
        selectedTripletSlot = -1;
    } else {
        selectedStep = (selectedStep + 1) % song.stepCount;
    }
    page = selectedStep / VisibleSteps;
    selectCurrentCell();
    if (song.triplets[selectedChannel][selectedStep].active) {
        selectedTripletStart = selectedStep;
        selectedTripletSlot = 0;
        selectionEndStep =
            selectedStep +
            song.triplets[selectedChannel][selectedStep].duration - 1;
    }
}

Step &App::selectedEvent() {
    if (selectedTripletStart >= 0 && selectedTripletSlot >= 0) {
        return song.triplets[selectedChannel][selectedTripletStart]
            .events[selectedTripletSlot];
    }
    return song.pattern[selectedChannel][selectedStep];
}

const Step &App::selectedEvent() const {
    if (selectedTripletStart >= 0 && selectedTripletSlot >= 0) {
        return song.triplets[selectedChannel][selectedTripletStart]
            .events[selectedTripletSlot];
    }
    return song.pattern[selectedChannel][selectedStep];
}

int App::tripletStartFor(int channel, int step) const {
    for (int start = step; start >= 0; start--) {
        const TripletGroup &group = song.triplets[channel][start];
        if (group.active && step < start + group.duration) {
            return start;
        }
    }
    return -1;
}

void App::makeTriplets() {
    if (!selectionActive || selectionAnchorChannel != selectionEndChannel) {
        setStatus("Select an area on one instrument for triplets", 5000);
        return;
    }
    int first = std::min(selectionAnchorStep, selectionEndStep);
    int last = std::max(selectionAnchorStep, selectionEndStep);
    int count = last - first + 1;
    if (count < 1) {
        setStatus("Select a duration for the triplet", 5000);
        return;
    }
    if (first / VisibleSteps != last / VisibleSteps) {
        setStatus("A triplet duration cannot cross a page boundary", 5000);
        return;
    }
    for (int step = 0; step <= last; step++) {
        TripletGroup &existing = song.triplets[selectedChannel][step];
        if (existing.active && step + existing.duration > first) {
            existing = TripletGroup{};
        }
    }
    TripletGroup &group = song.triplets[selectedChannel][first];
    group.active = true;
    group.duration = count;
    for (int slot = 0; slot < 3; slot++) {
        int source =
            first + static_cast<int>(std::lround(slot * (count - 1) / 2.0));
        group.events[slot] = song.pattern[selectedChannel][source];
    }
    for (int step = first; step <= last; step++) {
        song.pattern[selectedChannel][step] = Step{};
    }
    selectedStep = first;
    selectedTripletStart = first;
    selectedTripletSlot = 0;
    markChanged();
    setStatus("Converted selected instrument area to triplets");
}

void App::transposeSelected(int amount) {
    Step &step = selectedEvent();
    if (selectedChannel == 5) {
        if (step.note >= 0) {
            audio.preview(5, 60);
        }
        return;
    }
    if (step.note >= 0) {
        step.note = std::clamp(step.note + amount, 0, 127);
        audio.preview(selectedChannel, step.note);
        markChanged();
    }
}

void App::selectCurrentCell() {
    selectedTripletStart = -1;
    selectedTripletSlot = -1;
    selectionAnchorChannel = selectedChannel;
    selectionAnchorStep = selectedStep;
    selectionEndChannel = selectedChannel;
    selectionEndStep = selectedStep;
    selectionActive = true;
}

void App::extendSelectionTo(int channel, int step) {
    if (!selectionActive) {
        selectCurrentCell();
    }
    selectionEndChannel = std::clamp(channel, 0, ChannelCount - 1);
    selectionEndStep = std::clamp(step, 0, song.stepCount - 1);
}

bool App::cellIsSelected(int channel, int step) const {
    if (!selectionActive) {
        return false;
    }
    int firstChannel = std::min(selectionAnchorChannel, selectionEndChannel);
    int lastChannel = std::max(selectionAnchorChannel, selectionEndChannel);
    int firstStep = std::min(selectionAnchorStep, selectionEndStep);
    int lastStep = std::max(selectionAnchorStep, selectionEndStep);
    return channel >= firstChannel && channel <= lastChannel &&
           step >= firstStep && step <= lastStep;
}

void App::copySelection(bool cut) {
    if (!selectionActive) {
        selectCurrentCell();
    }
    int firstChannel = std::min(selectionAnchorChannel, selectionEndChannel);
    int lastChannel = std::max(selectionAnchorChannel, selectionEndChannel);
    int firstStep = std::min(selectionAnchorStep, selectionEndStep);
    int lastStep = std::max(selectionAnchorStep, selectionEndStep);
    int rows = lastChannel - firstChannel + 1;
    int columns = lastStep - firstStep + 1;

    std::ostringstream encoded;
    encoded << "E16PATTERN 4 " << rows << ' ' << columns;
    for (int channel = firstChannel; channel <= lastChannel; channel++) {
        for (int step = firstStep; step <= lastStep; step++) {
            const Step &event = song.pattern[channel][step];
            encoded << ' ' << event.note << ' ' << event.velocity << ' '
                    << event.tuning << ' ' << static_cast<int>(event.glide);
        }
    }
    int tripletCount = 0;
    for (int channel = firstChannel; channel <= lastChannel; channel++) {
        for (int step = firstStep; step <= lastStep; step++) {
            const TripletGroup &group = song.triplets[channel][step];
            if (group.active && step + group.duration - 1 <= lastStep) {
                tripletCount++;
            }
        }
    }
    encoded << ' ' << tripletCount;
    for (int channel = firstChannel; channel <= lastChannel; channel++) {
        for (int step = firstStep; step <= lastStep; step++) {
            const TripletGroup &group = song.triplets[channel][step];
            if (!group.active || step + group.duration - 1 > lastStep) {
                continue;
            }
            encoded << ' ' << channel - firstChannel << ' ' << step - firstStep
                    << ' ' << group.duration;
            for (const Step &event : group.events) {
                encoded << ' ' << event.note << ' ' << event.velocity << ' '
                        << event.tuning << ' ' << static_cast<int>(event.glide);
            }
        }
    }
    if (!SDL_SetClipboardText(encoded.str().c_str())) {
        setStatus("Could not copy pattern: " + std::string(SDL_GetError()),
                  6000);
        return;
    }
    if (cut) {
        for (int channel = firstChannel; channel <= lastChannel; channel++) {
            for (int step = firstStep; step <= lastStep; step++) {
                song.pattern[channel][step] = Step{};
            }
            for (int step = 0; step <= lastStep; step++) {
                TripletGroup &group = song.triplets[channel][step];
                if (group.active && step + group.duration > firstStep) {
                    group = TripletGroup{};
                }
            }
        }
        selectedTripletStart = -1;
        selectedTripletSlot = -1;
        markChanged();
    }
    setStatus(std::string(cut ? "Cut " : "Copied ") + std::to_string(rows) +
              " channel" + (rows == 1 ? "" : "s") + " x " +
              std::to_string(columns) + " step" + (columns == 1 ? "" : "s"));
}

void App::pasteSelection() {
    char *clipboard = SDL_GetClipboardText();
    if (!clipboard) {
        setStatus("Could not read the clipboard", 5000);
        return;
    }
    std::string encoded = clipboard;
    SDL_free(clipboard);
    std::istringstream input(encoded);
    std::string signature;
    int version = 0;
    int rows = 0;
    int columns = 0;
    input >> signature >> version >> rows >> columns;
    if (signature != "E16PATTERN" ||
        (version != 1 && version != 2 && version != 3 && version != 4) ||
        rows < 1 || rows > ChannelCount || columns < 1 || columns > MaxSteps) {
        setStatus("The clipboard does not contain E16 pattern data", 5000);
        return;
    }
    std::vector<Step> copied(static_cast<std::size_t>(rows * columns));
    for (Step &event : copied) {
        input >> event.note >> event.velocity;
        if (version >= 2) {
            int glide = 0;
            input >> event.tuning >> glide;
            event.glide = glide != 0;
        }
        if (!input || event.note < Stop || event.note > 127 ||
            event.velocity < 1 || event.velocity > 127 ||
            event.tuning < -2400 || event.tuning > 2400) {
            setStatus("The clipboard contains invalid E16 pattern data", 5000);
            return;
        }
    }
    struct CopiedTriplet {
        int row = 0;
        int column = 0;
        TripletGroup group;
    };
    std::vector<CopiedTriplet> copiedTriplets;
    if (version >= 3) {
        int count = 0;
        input >> count;
        if (count < 0 || count > rows * columns) {
            setStatus("The clipboard contains invalid triplet data", 5000);
            return;
        }
        copiedTriplets.resize(static_cast<std::size_t>(count));
        for (CopiedTriplet &triplet : copiedTriplets) {
            input >> triplet.row >> triplet.column;
            triplet.group.active = true;
            if (version >= 4) {
                input >> triplet.group.duration;
            } else {
                triplet.group.duration = 2;
            }
            for (Step &event : triplet.group.events) {
                int glide = 0;
                input >> event.note >> event.velocity >> event.tuning >> glide;
                event.glide = glide != 0;
                if (event.note < Stop || event.note > 127 ||
                    event.velocity < 1 || event.velocity > 127 ||
                    event.tuning < -2400 || event.tuning > 2400) {
                    setStatus("The clipboard contains invalid triplet data",
                              5000);
                    return;
                }
            }
            if (!input || triplet.row < 0 || triplet.row >= rows ||
                triplet.column < 0 || triplet.group.duration < 1 ||
                triplet.column + triplet.group.duration > columns) {
                setStatus("The clipboard contains invalid triplet data", 5000);
                return;
            }
        }
    }

    int pastedRows = std::min(rows, ChannelCount - selectedChannel);
    int pastedColumns = std::min(columns, song.stepCount - selectedStep);
    for (int row = 0; row < pastedRows; row++) {
        for (int step = 0; step < selectedStep + pastedColumns; step++) {
            TripletGroup &group = song.triplets[selectedChannel + row][step];
            if (group.active && step + group.duration > selectedStep) {
                group = TripletGroup{};
            }
        }
        for (int column = 0; column < pastedColumns; column++) {
            song.pattern[selectedChannel + row][selectedStep + column] =
                copied[static_cast<std::size_t>(row * columns + column)];
        }
    }
    for (const CopiedTriplet &triplet : copiedTriplets) {
        if (triplet.row < pastedRows &&
            triplet.column + triplet.group.duration <= pastedColumns) {
            song.triplets[selectedChannel + triplet.row]
                         [selectedStep + triplet.column] = triplet.group;
        }
    }
    selectionAnchorChannel = selectedChannel;
    selectionAnchorStep = selectedStep;
    selectionEndChannel = selectedChannel + pastedRows - 1;
    selectionEndStep = selectedStep + pastedColumns - 1;
    selectionActive = true;
    selectedTripletStart = -1;
    selectedTripletSlot = -1;
    markChanged();
    setStatus("Pasted " + std::to_string(pastedRows) + " channel" +
              (pastedRows == 1 ? "" : "s") + " x " +
              std::to_string(pastedColumns) + " step" +
              (pastedColumns == 1 ? "" : "s"));
}

void App::selectAllPattern() {
    selectedChannel = 0;
    selectedStep = 0;
    page = 0;
    selectCurrentCell();
    selectionAnchorChannel = 0;
    selectionAnchorStep = 0;
    selectionEndChannel = ChannelCount - 1;
    selectionEndStep = song.stepCount - 1;
    selectionActive = true;
    setStatus("Selected the complete pattern");
}

void App::clearSelection() {
    if (!selectionActive) {
        selectCurrentCell();
    }
    int firstChannel = std::min(selectionAnchorChannel, selectionEndChannel);
    int lastChannel = std::max(selectionAnchorChannel, selectionEndChannel);
    int firstStep = std::min(selectionAnchorStep, selectionEndStep);
    int lastStep = std::max(selectionAnchorStep, selectionEndStep);
    for (int channel = firstChannel; channel <= lastChannel; channel++) {
        for (int step = firstStep; step <= lastStep; step++) {
            song.pattern[channel][step] = Step{};
        }
        for (int step = 0; step <= lastStep; step++) {
            TripletGroup &group = song.triplets[channel][step];
            if (group.active && step + group.duration > firstStep) {
                group = TripletGroup{};
            }
        }
    }
    selectedTripletStart = -1;
    selectedTripletSlot = -1;
    markChanged();
    setStatus("Cleared the selected pattern cells");
}

void App::markChanged() {
    dirty = true;
    audio.setSong(song);
}

void App::setStatus(const std::string &text, int milliseconds) {
    status = text;
    statusUntil = SDL_GetTicks() + static_cast<Uint64>(milliseconds);
}

bool App::confirmDiscard() {
    if (!dirty) {
        return true;
    }
    static const SDL_MessageBoxButtonData buttons[]{
        {SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "Cancel"},
        {SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "Discard"}};
    SDL_MessageBoxData data{};
    data.flags = SDL_MESSAGEBOX_WARNING;
    data.window = window;
    data.title = "Unsaved song";
    data.message = "Discard the unsaved changes to this song?";
    data.numbuttons = 2;
    data.buttons = buttons;
    int choice = 0;
    return SDL_ShowMessageBox(&data, &choice) && choice == 1;
}

void App::newSong() {
    if (!confirmDiscard()) {
        return;
    }
    recorder.cancel();
    audio.stop();
    song = Song{};
    projectPath.clear();
    selectedChannel = 0;
    selectedStep = 0;
    page = 0;
    selectCurrentCell();
    dirty = false;
    audio.setSong(song);
    setStatus("Created a new song");
}

void App::openDialog() {
    if (!confirmDiscard()) {
        return;
    }
    recorder.cancel();
    {
        std::lock_guard lock(dialogMutex);
        if (dialogAction != DialogAction::None) {
            return;
        }
        dialogAction = DialogAction::Open;
    }
    static const SDL_DialogFileFilter filters[]{
        {"E16 Music Project", "e16music"}};
    SDL_ShowOpenFileDialog(dialogCallback, this, window, filters, 1, nullptr,
                           false);
}

void App::saveProject(bool choosePath) {
    if (recorder.isRecording()) {
        finishRecording();
    }
    if (!choosePath && !projectPath.empty()) {
        std::string error;
        if (saveSong(song, projectPath, error)) {
            dirty = false;
            setStatus("Saved " + projectPath);
        } else {
            setStatus(error);
        }
        return;
    }
    {
        std::lock_guard lock(dialogMutex);
        if (dialogAction != DialogAction::None) {
            return;
        }
        dialogAction = DialogAction::Save;
    }
    static const SDL_DialogFileFilter filters[]{
        {"E16 Music Project", "e16music"}};
    SDL_ShowSaveFileDialog(dialogCallback, this, window, filters, 1, nullptr);
}

void App::exportProject() {
    if (recorder.isRecording()) {
        finishRecording();
    }
    {
        std::lock_guard lock(dialogMutex);
        if (dialogAction != DialogAction::None) {
            return;
        }
        dialogAction = DialogAction::Export;
    }
    static const SDL_DialogFileFilter filters[]{{"E16 Assembly", "e16"}};
    SDL_ShowSaveFileDialog(dialogCallback, this, window, filters, 1, nullptr);
}

void App::importPcmDialog() {
    if (recorder.isRecording()) {
        recorder.cancel();
    }
    {
        std::lock_guard lock(dialogMutex);
        if (dialogAction != DialogAction::None) {
            return;
        }
        dialogAction = DialogAction::ImportPcm;
    }
    static const SDL_DialogFileFilter filters[]{
        {"Audio files", "mp3;wav;flac;ogg"}};
    SDL_ShowOpenFileDialog(dialogCallback, this, window, filters, 1, nullptr,
                           false);
}

void App::openProject(const std::string &path) {
    Song loaded;
    std::string error;
    if (!loadSong(loaded, path, error)) {
        setStatus(error, 6000);
        return;
    }
    recorder.cancel();
    audio.stop();
    song = loaded;
    projectPath = path;
    selectedChannel = 0;
    selectedStep = 0;
    page = 0;
    selectCurrentCell();
    dirty = false;
    audio.setSong(song);
    setStatus("Opened " + path);
}

void App::consumeDialogResult() {
    DialogAction action = DialogAction::None;
    std::string path;
    {
        std::lock_guard lock(dialogMutex);
        if (pendingAction == DialogAction::None) {
            return;
        }
        action = pendingAction;
        path = pendingPath;
        pendingAction = DialogAction::None;
        pendingPath.clear();
    }
    if (path.empty()) {
        return;
    }
    if (action == DialogAction::Open) {
        openProject(path);
        return;
    }
    std::string error;
    if (action == DialogAction::ImportPcm) {
        std::vector<std::uint8_t> imported;
        if (importPcmFile(path, song.trimPcmSilence, imported, error)) {
            song.pcmSample = std::move(imported);
            selectedChannel = 5;
            markChanged();
            setStatus("Imported " + std::filesystem::path(path).filename().string() +
                          " as 8 kHz PCM",
                      6000);
        } else {
            setStatus("Could not import audio: " + error, 6000);
        }
        return;
    }
    if (action == DialogAction::Save) {
        path = withExtension(path, ".e16music");
        if (song.title == "Untitled") {
            song.title = std::filesystem::path(path).stem().string();
            audio.setSong(song);
        }
        if (saveSong(song, path, error)) {
            projectPath = path;
            dirty = false;
            setStatus("Saved " + path);
        } else {
            setStatus(error, 6000);
        }
        return;
    }
    path = withExtension(path, ".e16");
    if (exportE16(song, path, error)) {
        std::string prefix = exportSymbolPrefix(path);
        setStatus("Exported " + prefix + " music play APIs to " + path,
                  6000);
    } else {
        setStatus(error, 6000);
    }
}

void App::changeMaxPages(int direction) {
    int pages = std::clamp(song.stepCount / StepsPerPage + direction, 1,
                           MaxPages);
    song.stepCount = pages * StepsPerPage;
    selectedStep = std::min(selectedStep, song.stepCount - 1);
    page = std::min(page, song.stepCount / VisibleSteps - 1);
    selectCurrentCell();
    markChanged();
}

void App::changePage(int direction) {
    int pageCount = song.stepCount / VisibleSteps;
    page = (page + direction + pageCount) % pageCount;
    selectedStep = page * VisibleSteps + selectedStep % VisibleSteps;
    selectCurrentCell();
}

void App::finishRecording() {
    std::vector<std::uint8_t> captured = recorder.stop(song.trimPcmSilence);
    if (captured.empty()) {
        setStatus("The recording was empty", 5000);
        return;
    }
    song.pcmSample = std::move(captured);
    markChanged();
    double seconds = static_cast<double>(song.pcmSample.size()) / PcmSampleRate;
    std::ostringstream message;
    message.precision(2);
    message << std::fixed << "Recorded " << seconds << " seconds of PCM";
    if (song.trimPcmSilence) {
        message << " with auto trim enabled";
    }
    setStatus(message.str(), 6000);
    audio.preview(5, 60);
}

void App::editWavetable(float x, float y) {
    int index =
        std::clamp(static_cast<int>((x - 20.0f) / 920.0f * WavetableSize), 0,
                   WavetableSize - 1);
    int value = static_cast<int>((1.0f - (y - 620.0f) / 120.0f) * 255.0f);
    song.wavetable[index] =
        static_cast<std::uint8_t>(std::clamp(value, 0, 255));
    markChanged();
}

void App::setWavetablePreset(int preset) {
    std::uint32_t random = 0x16E16u;
    for (int i = 0; i < WavetableSize; i++) {
        double phase = static_cast<double>(i) / WavetableSize;
        double value = 0.0;
        if (preset == 0) {
            value = std::sin(phase * 2.0 * std::acos(-1.0));
        } else if (preset == 1) {
            value = phase < 0.5 ? 1.0 : -1.0;
        } else if (preset == 2) {
            value = phase * 2.0 - 1.0;
        } else if (preset == 3) {
            value = 1.0 - 4.0 * std::abs(phase - 0.5);
        } else {
            random ^= random << 13;
            random ^= random >> 17;
            random ^= random << 5;
            value = static_cast<double>(random & 0xFFFFu) / 32767.5 - 1.0;
        }
        song.wavetable[i] = static_cast<std::uint8_t>(std::clamp(
            static_cast<int>(std::lround(value * 127.0 + 128.0)), 0, 255));
    }
    markChanged();
    audio.preview(4, (octave + 1) * 12);
}

void App::drawWavetableEditor() {
    Rect plot{20, 620, 920, 120};
    drawRect(plot, Surface, true);
    drawRect(plot, Border, false);
    drawText(30, 630, "DRAW THE 32-SAMPLE WAVEFORM", Muted);
    SDL_SetRenderDrawColor(renderer, Border.r, Border.g, Border.b, Border.a);
    SDL_RenderLine(renderer, plot.x, plot.y + plot.h / 2.0f, plot.x + plot.w,
                   plot.y + plot.h / 2.0f);
    float previousX = plot.x;
    float previousY = plot.y + plot.h -
                      static_cast<float>(song.wavetable[0]) / 255.0f * plot.h;
    for (int i = 0; i < WavetableSize; i++) {
        float x =
            plot.x + (static_cast<float>(i) + 0.5f) / WavetableSize * plot.w;
        float y = plot.y + plot.h -
                  static_cast<float>(song.wavetable[i]) / 255.0f * plot.h;
        SDL_SetRenderDrawColor(renderer, Blue.r, Blue.g, Blue.b, Blue.a);
        if (i != 0) {
            SDL_RenderLine(renderer, previousX, previousY, x, y);
        }
        drawRect({x - 3, y - 3, 6, 6}, Blue, true);
        previousX = x;
        previousY = y;
    }
    static constexpr std::array<const char *, 5> labels{"SINE", "SQUARE", "SAW",
                                                        "TRIANGLE", "RANDOM"};
    for (int preset = 0; preset < 5; preset++) {
        drawButton({20.0f + preset * 112.0f, 750, 104, 34}, labels[preset],
                   false, Blue);
    }
    drawText(600, 762, "Drag in the graph, then enter notes with the keyboard",
             Muted);
}

void App::drawPcmEditor() {
    Rect plot{20, 620, 920, 120};
    drawRect(plot, Surface, true);
    drawRect(plot, recorder.isRecording() ? Red : Border, false);
    std::size_t count = recorder.isRecording()
                            ? static_cast<std::size_t>(recorder.sampleCount())
                            : song.pcmSample.size();
    double seconds = static_cast<double>(count) / PcmSampleRate;
    std::ostringstream info;
    info.precision(2);
    info << std::fixed
         << (recorder.isRecording() ? "RECORDING  " : "PCM SAMPLE  ") << seconds
         << " s  /  " << count << " bytes  /  8 kHz mono";
    drawText(30, 630, info.str(), recorder.isRecording() ? Red : Muted);
    SDL_SetRenderDrawColor(renderer, Border.r, Border.g, Border.b, Border.a);
    SDL_RenderLine(renderer, plot.x, plot.y + plot.h / 2.0f, plot.x + plot.w,
                   plot.y + plot.h / 2.0f);
    if (!song.pcmSample.empty() && !recorder.isRecording()) {
        float previousX = plot.x;
        float previousY = plot.y + plot.h / 2.0f;
        int points = static_cast<int>(plot.w);
        for (int point = 0; point < points; point++) {
            std::size_t index = static_cast<std::size_t>(point) *
                                song.pcmSample.size() /
                                static_cast<std::size_t>(points);
            float value =
                (static_cast<float>(song.pcmSample[index]) - 128.0f) / 128.0f;
            float x = plot.x + point;
            float y = plot.y + plot.h / 2.0f - value * (plot.h * 0.42f);
            SDL_SetRenderDrawColor(renderer, Purple.r, Purple.g, Purple.b,
                                   Purple.a);
            SDL_RenderLine(renderer, previousX, previousY, x, y);
            previousX = x;
            previousY = y;
        }
    }
    drawButton({20, 750, 104, 34},
               recorder.isRecording() ? "STOP REC" : "RECORD",
               recorder.isRecording(), Red);
    drawButton({132, 750, 104, 34}, "PREVIEW", false, Purple);
    drawButton({244, 750, 104, 34}, "PLACE", false, Green);
    drawButton({356, 750, 104, 34}, "CLEAR", false, Red);
    drawButton({468, 750, 104, 34}, "AUTO TRIM", song.trimPcmSilence, Cyan);
    drawButton({580, 750, 104, 34}, "IMPORT", false, Blue);
    drawText(700, 762,
             song.pcmLoop ? "Looping sample; place STOP events to end it"
                          : "One-shot sample; empty steps do not cut it off",
             Muted);
}

void App::drawRect(const Rect &rect, SDL_Color color, bool filled) {
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    SDL_FRect sdlRect{rect.x, rect.y, rect.w, rect.h};
    if (filled) {
        SDL_RenderFillRect(renderer, &sdlRect);
    } else {
        SDL_RenderRect(renderer, &sdlRect);
    }
}

void App::drawText(float x, float y, const std::string &text, SDL_Color color,
                   float scale) {
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    if (scale == 1.0f) {
        SDL_RenderDebugText(renderer, x, y, text.c_str());
        return;
    }
    float oldX = 1.0f;
    float oldY = 1.0f;
    SDL_GetRenderScale(renderer, &oldX, &oldY);
    SDL_SetRenderScale(renderer, oldX * scale, oldY * scale);
    SDL_RenderDebugText(renderer, x / scale, y / scale, text.c_str());
    SDL_SetRenderScale(renderer, oldX, oldY);
}

void App::drawButton(const Rect &rect, const std::string &label, bool active,
                     SDL_Color accent) {
    drawRect(rect, active ? accent : SurfaceRaised, true);
    drawRect(rect, active ? accent : Border, false);
    float textX =
        rect.x + std::max(6.0f, (rect.w - label.size() * 8.0f) / 2.0f);
    float textY = rect.y + (rect.h - 8.0f) / 2.0f;
    drawText(textX, textY, label, active ? Background : Text);
}

bool App::contains(const Rect &rect, float x, float y) const {
    return x >= rect.x && y >= rect.y && x < rect.x + rect.w &&
           y < rect.y + rect.h;
}

void SDLCALL App::dialogCallback(void *userdata, const char *const *filelist,
                                 int filter) {
    (void)filter;
    auto *app = static_cast<App *>(userdata);
    if (!app) {
        return;
    }
    std::lock_guard lock(app->dialogMutex);
    app->pendingAction = app->dialogAction;
    app->dialogAction = DialogAction::None;
    if (filelist && filelist[0]) {
        app->pendingPath = filelist[0];
    }
}

}

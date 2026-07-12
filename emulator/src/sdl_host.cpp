#include "e16/sdl_host.h"

#include <algorithm>
#include <cstdlib>

namespace e16 {

namespace {
constexpr std::uint16_t ButtonRight = 1u << 0;
constexpr std::uint16_t ButtonLeft = 1u << 1;
constexpr std::uint16_t ButtonDown = 1u << 2;
constexpr std::uint16_t ButtonUp = 1u << 3;
constexpr std::uint16_t ButtonA = 1u << 4;
constexpr std::uint16_t ButtonB = 1u << 5;
constexpr std::uint16_t ButtonX = 1u << 6;
constexpr std::uint16_t ButtonY = 1u << 7;
constexpr std::uint16_t ButtonStart = 1u << 8;
constexpr std::uint16_t ButtonSelect = 1u << 9;

std::uint16_t keyboardPad0() {
    const bool *keys = SDL_GetKeyboardState(nullptr);
    std::uint16_t pad = 0;
    pad |= keys[SDL_SCANCODE_RIGHT] ? ButtonRight : 0;
    pad |= keys[SDL_SCANCODE_LEFT] ? ButtonLeft : 0;
    pad |= keys[SDL_SCANCODE_DOWN] ? ButtonDown : 0;
    pad |= keys[SDL_SCANCODE_UP] ? ButtonUp : 0;
    pad |= keys[SDL_SCANCODE_Z] ? ButtonA : 0;
    pad |= keys[SDL_SCANCODE_X] ? ButtonB : 0;
    pad |= keys[SDL_SCANCODE_A] ? ButtonX : 0;
    pad |= keys[SDL_SCANCODE_S] ? ButtonY : 0;
    pad |= keys[SDL_SCANCODE_RETURN] ? ButtonStart : 0;
    pad |= keys[SDL_SCANCODE_LSHIFT] ? ButtonSelect : 0;
    return pad;
}

std::uint16_t keyboardPad1() {
    const bool *keys = SDL_GetKeyboardState(nullptr);
    std::uint16_t pad = 0;
    pad |= keys[SDL_SCANCODE_L] ? ButtonRight : 0;
    pad |= keys[SDL_SCANCODE_J] ? ButtonLeft : 0;
    pad |= keys[SDL_SCANCODE_K] ? ButtonDown : 0;
    pad |= keys[SDL_SCANCODE_I] ? ButtonUp : 0;
    pad |= keys[SDL_SCANCODE_1] ? ButtonA : 0;
    pad |= keys[SDL_SCANCODE_2] ? ButtonB : 0;
    pad |= keys[SDL_SCANCODE_3] ? ButtonX : 0;
    pad |= keys[SDL_SCANCODE_4] ? ButtonY : 0;
    pad |= keys[SDL_SCANCODE_0] ? ButtonStart : 0;
    pad |= keys[SDL_SCANCODE_RSHIFT] ? ButtonSelect : 0;
    return pad;
}

bool escapePressed() {
    return SDL_GetKeyboardState(nullptr)[SDL_SCANCODE_ESCAPE];
}

bool exitPressed(SDL_Gamepad *gamepad) {
    return gamepad &&
           SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_BACK) &&
           SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_START);
}

std::uint16_t gamepadButtons(SDL_Gamepad *gamepad) {
    if (!gamepad) {
        return 0;
    }

    constexpr Sint16 AxisDeadZone = 18000;
    std::uint16_t pad = 0;

    const Sint16 leftX = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTX);
    const Sint16 leftY = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTY);

    const bool right = leftX > AxisDeadZone ||
                       SDL_GetGamepadButton(
                           gamepad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT);
    const bool left = leftX < -AxisDeadZone ||
                      SDL_GetGamepadButton(
                          gamepad, SDL_GAMEPAD_BUTTON_DPAD_LEFT);
    const bool down = leftY > AxisDeadZone ||
                      SDL_GetGamepadButton(
                          gamepad, SDL_GAMEPAD_BUTTON_DPAD_DOWN);
    const bool up = leftY < -AxisDeadZone ||
                    SDL_GetGamepadButton(gamepad,
                                         SDL_GAMEPAD_BUTTON_DPAD_UP);

    pad |= right && !left ? ButtonRight : 0;
    pad |= left && !right ? ButtonLeft : 0;
    pad |= down && !up ? ButtonDown : 0;
    pad |= up && !down ? ButtonUp : 0;
    pad |=
        SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_SOUTH) ? ButtonA : 0;
    pad |= SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_EAST) ? ButtonB : 0;
    pad |= SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_WEST) ? ButtonX : 0;
    pad |=
        SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_NORTH) ? ButtonY : 0;
    pad |= SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_START) ? ButtonStart
                                                                   : 0;
    pad |= SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_BACK) ? ButtonSelect
                                                                  : 0;

    return pad;
}
} // namespace

SdlHost::SdlHost() = default;

SdlHost::~SdlHost() {
    close();
    SDL_Quit();
}

bool SdlHost::open(int scale, Apu &apu, bool forceHeadless) {
    apuDevice = &apu;
    headless = forceHeadless;
    if (!SDL_Init(SDL_INIT_EVENTS | SDL_INIT_GAMEPAD)) {
        errorText = SDL_GetError();
        return false;
    }
    openAvailableGamepads();

    if (SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        SDL_AudioSpec spec{};
        spec.format = SDL_AUDIO_F32;
        spec.channels = 2;
        spec.freq = ApuSampleRate;
        audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
                                          &spec, audioCallback, this);
        if (audio && !SDL_ResumeAudioStreamDevice(audio)) {
            SDL_DestroyAudioStream(audio);
            audio = nullptr;
        }
    }

    if (headless) {
        return true;
    }
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
        headless = true;
        return true;
    }
    window = SDL_CreateWindow("Ember-16", ScreenWidth * scale,
                              ScreenHeight * scale, 0);
    if (!window) {
        headless = true;
        return true;
    }
    SDL_SetWindowMinimumSize(window, ScreenWidth * scale, ScreenHeight * scale);
    SDL_SetWindowMaximumSize(window, ScreenWidth * scale, ScreenHeight * scale);
    SDL_SetWindowFocusable(window, true);
    SDL_RaiseWindow(window);
    SDL_HideCursor();
    cursorHidden = true;
    renderer = SDL_CreateRenderer(window, nullptr);
    if (!renderer) {
        SDL_DestroyWindow(window);
        window = nullptr;
        headless = true;
        return true;
    }
    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                SDL_TEXTUREACCESS_STREAMING, ScreenWidth,
                                ScreenHeight);
    if (!texture) {
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        renderer = nullptr;
        window = nullptr;
        headless = true;
        return true;
    }
    SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
    return true;
}

bool SdlHost::poll(Memory &memory) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_EVENT_QUIT) {
            return false;
        }
        if (event.type == SDL_EVENT_GAMEPAD_ADDED) {
            openGamepad(event.gdevice.which);
        } else if (event.type == SDL_EVENT_GAMEPAD_REMOVED) {
            closeGamepad(event.gdevice.which);
        }
    }
    if (!headless && escapePressed()) {
        return false;
    }
    for (const GamepadSlot &slot : gamepads) {
        if (exitPressed(slot.gamepad)) {
            return false;
        }
    }
    memory.inputPad0 = (headless ? 0 : keyboardPad0()) | gamepadPad(0);
    memory.inputPad1 =
        (headless || !twoPlayerControls ? 0 : keyboardPad1()) | gamepadPad(1);
    return true;
}

void SdlHost::enableTwoPlayerControls() {
    if (twoPlayerControls) {
        return;
    }
    twoPlayerControls = true;
}

void SdlHost::present(const Flame &flame) {
    if (headless) {
        return;
    }
    const auto &fb = flame.framebuffer();
    SDL_UpdateTexture(texture, nullptr, fb.data(), ScreenWidth * 4);
    SDL_RenderClear(renderer);
    SDL_RenderTexture(renderer, texture, nullptr, nullptr);
    SDL_RenderPresent(renderer);
}

const std::string &SdlHost::error() const { return errorText; }

bool SdlHost::isHeadless() const { return headless; }

void SdlHost::close() {
    if (audio) {
        SDL_DestroyAudioStream(audio);
        audio = nullptr;
    }
    if (texture) {
        SDL_DestroyTexture(texture);
        texture = nullptr;
    }
    if (renderer) {
        SDL_DestroyRenderer(renderer);
        renderer = nullptr;
    }
    closeAllGamepads();
    if (window) {
        SDL_DestroyWindow(window);
        window = nullptr;
    }
    if (cursorHidden) {
        SDL_ShowCursor();
        cursorHidden = false;
    }
    apuDevice = nullptr;
}

void SdlHost::openAvailableGamepads() {
    int count = 0;
    SDL_JoystickID *ids = SDL_GetGamepads(&count);
    if (!ids) {
        return;
    }
    for (int i = 0; i < count; i++) {
        openGamepad(ids[i]);
    }
    SDL_free(ids);
}

void SdlHost::openGamepad(SDL_JoystickID instanceId) {
    auto existing = std::find_if(gamepads.begin(), gamepads.end(),
                                 [instanceId](const GamepadSlot &slot) {
                                     return slot.instanceId == instanceId;
                                 });
    if (existing != gamepads.end() || gamepads.size() >= 2 ||
        !SDL_IsGamepad(instanceId)) {
        return;
    }

    SDL_Gamepad *gamepad = SDL_OpenGamepad(instanceId);
    if (!gamepad) {
        return;
    }
    gamepads.push_back({instanceId, gamepad});
    if (gamepads.size() > 1) {
        enableTwoPlayerControls();
    }
}

void SdlHost::closeGamepad(SDL_JoystickID instanceId) {
    auto removed = std::remove_if(gamepads.begin(), gamepads.end(),
                                  [instanceId](GamepadSlot &slot) {
                                      if (slot.instanceId != instanceId) {
                                          return false;
                                      }
                                      SDL_CloseGamepad(slot.gamepad);
                                      return true;
                                  });
    gamepads.erase(removed, gamepads.end());
}

void SdlHost::closeAllGamepads() {
    for (GamepadSlot &slot : gamepads) {
        SDL_CloseGamepad(slot.gamepad);
    }
    gamepads.clear();
}

std::uint16_t SdlHost::gamepadPad(std::size_t player) const {
    if (player >= gamepads.size()) {
        return 0;
    }
    return gamepadButtons(gamepads[player].gamepad);
}

void SdlHost::audioCallback(void *userdata, SDL_AudioStream *stream,
                            int additionalAmount, int totalAmount) {
    (void)totalAmount;
    auto *host = static_cast<SdlHost *>(userdata);
    if (!host || !host->apuDevice || additionalAmount <= 0) {
        return;
    }
    int frames = additionalAmount / static_cast<int>(sizeof(float) * 2);
    if (frames <= 0) {
        return;
    }
    host->audioBuffer.resize(static_cast<std::size_t>(frames) * 2);
    host->apuDevice->mix(host->audioBuffer.data(), frames);
    SDL_PutAudioStreamData(
        stream, host->audioBuffer.data(),
        static_cast<int>(host->audioBuffer.size() * sizeof(float)));
}

} // namespace e16

#ifndef E16_SDL_HOST_H
#define E16_SDL_HOST_H

#include "e16/apu.h"
#include "e16/flame.h"
#include "e16/memory.h"

#include <cstdint>
#include <string>
#include <vector>

#if __has_include(<SDL3/SDL.h>)
#include <SDL3/SDL.h>
#else
#include <SDL.h>
#endif

namespace e16 {

class SdlHost {
  public:
    SdlHost();
    ~SdlHost();

    bool open(int scale, Apu &apu);
    bool poll(Memory &memory);
    void enableTwoPlayerControls();
    void showSaveRamNotice(const std::string &path) const;
    void present(const Flame &flame);
    const std::string &error() const;

  private:
    SDL_Window *window = nullptr;
    SDL_Renderer *renderer = nullptr;
    SDL_Texture *texture = nullptr;
    SDL_AudioStream *audio = nullptr;
    Apu *apuDevice = nullptr;
    std::string errorText;
    struct GamepadSlot {
        SDL_JoystickID instanceId = 0;
        SDL_Gamepad *gamepad = nullptr;
    };

    std::vector<float> audioBuffer;
    std::vector<GamepadSlot> gamepads;
    bool twoPlayerControls = false;
    bool controlsKeyDown = false;

    void close();
    void showControls() const;
    void openAvailableGamepads();
    void openGamepad(SDL_JoystickID instanceId);
    void closeGamepad(SDL_JoystickID instanceId);
    void closeAllGamepads();
    std::uint16_t gamepadPad(std::size_t player) const;
    static void audioCallback(void *userdata, SDL_AudioStream *stream,
                              int additionalAmount, int totalAmount);
};

} // namespace e16

#endif

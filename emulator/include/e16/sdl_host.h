#ifndef E16_SDL_HOST_H
#define E16_SDL_HOST_H

#include "e16/flame.h"
#include "e16/memory.h"

#include <cstdint>
#include <string>

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

    bool open(int scale);
    bool poll(Memory &memory);
    void present(const Flame &flame);
    const std::string &error() const;

  private:
    SDL_Window *window = nullptr;
    SDL_Renderer *renderer = nullptr;
    SDL_Texture *texture = nullptr;
    std::string errorText;

    void close();
};

}

#endif

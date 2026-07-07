#include "e16/sdl_host.h"

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

void setButton(std::uint16_t &pad, std::uint16_t bit, bool down) {
    if (down) {
        pad |= bit;
    } else {
        pad &= static_cast<std::uint16_t>(~bit);
    }
}
}

SdlHost::SdlHost() = default;

SdlHost::~SdlHost() {
    close();
    SDL_Quit();
}

bool SdlHost::open(int scale) {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        errorText = SDL_GetError();
        return false;
    }
    window = SDL_CreateWindow("Ember-16", ScreenWidth * scale,
                              ScreenHeight * scale, 0);
    if (!window) {
        errorText = SDL_GetError();
        return false;
    }
    renderer = SDL_CreateRenderer(window, nullptr);
    if (!renderer) {
        errorText = SDL_GetError();
        return false;
    }
    SDL_SetRenderLogicalPresentation(renderer, ScreenWidth, ScreenHeight,
                                     SDL_LOGICAL_PRESENTATION_LETTERBOX);
    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                SDL_TEXTUREACCESS_STREAMING, ScreenWidth,
                                ScreenHeight);
    if (!texture) {
        errorText = SDL_GetError();
        return false;
    }
    return true;
}

bool SdlHost::poll(Memory &memory) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_EVENT_QUIT) {
            return false;
        }
        if (event.type == SDL_EVENT_KEY_DOWN ||
            event.type == SDL_EVENT_KEY_UP) {
            bool down = event.type == SDL_EVENT_KEY_DOWN;
            switch (event.key.key) {
            case SDLK_RIGHT:
                setButton(memory.inputPad0, ButtonRight, down);
                break;
            case SDLK_LEFT:
                setButton(memory.inputPad0, ButtonLeft, down);
                break;
            case SDLK_DOWN:
                setButton(memory.inputPad0, ButtonDown, down);
                break;
            case SDLK_UP:
                setButton(memory.inputPad0, ButtonUp, down);
                break;
            case SDLK_Z:
                setButton(memory.inputPad0, ButtonA, down);
                break;
            case SDLK_X:
                setButton(memory.inputPad0, ButtonB, down);
                break;
            case SDLK_A:
                setButton(memory.inputPad0, ButtonX, down);
                break;
            case SDLK_S:
                setButton(memory.inputPad0, ButtonY, down);
                break;
            case SDLK_RETURN:
                setButton(memory.inputPad0, ButtonStart, down);
                break;
            case SDLK_RSHIFT:
            case SDLK_LSHIFT:
                setButton(memory.inputPad0, ButtonSelect, down);
                break;
            case SDLK_ESCAPE:
                if (down) {
                    return false;
                }
                break;
            default:
                break;
            }
        }
    }
    return true;
}

void SdlHost::present(const Flame &flame) {
    const auto &fb = flame.framebuffer();
    SDL_UpdateTexture(texture, nullptr, fb.data(), ScreenWidth * 4);
    SDL_RenderClear(renderer);
    SDL_RenderTexture(renderer, texture, nullptr, nullptr);
    SDL_RenderPresent(renderer);
}

const std::string &SdlHost::error() const {
    return errorText;
}

void SdlHost::close() {
    if (texture) {
        SDL_DestroyTexture(texture);
        texture = nullptr;
    }
    if (renderer) {
        SDL_DestroyRenderer(renderer);
        renderer = nullptr;
    }
    if (window) {
        SDL_DestroyWindow(window);
        window = nullptr;
    }
}

}

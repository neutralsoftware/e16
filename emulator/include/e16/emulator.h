#ifndef E16_EMULATOR_H
#define E16_EMULATOR_H

#include "e16/apu.h"
#include "e16/cpu.h"
#include "e16/debugger.h"
#include "e16/flame.h"
#include "e16/memory.h"
#include "e16/sdl_host.h"

#include <cstdint>
#include <string>

namespace e16 {

struct EmulatorOptions {
    std::string programPath;
    std::uint32_t loadAddress = DefaultLoadAddress;
    int scale = 4;
    bool debug = false;
    bool headless = false;
    bool windowed = false;
};

class Emulator {
  public:
    explicit Emulator(EmulatorOptions options);
    int run();

  private:
    EmulatorOptions options;
    Flame flame;
    Apu apu;
    Memory memory;
    Cpu cpu;
    Debugger debugger;
    SdlHost sdl;

    bool runFrame();
};

}

#endif

#include "e16/emulator.h"

#include "e16/disassembler.h"
#include "bios_data.h"

#include <chrono>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <thread>
#include <utility>

namespace e16 {

namespace {
std::string savePathFor(const std::string &programPath) {
    std::filesystem::path absolute =
        std::filesystem::absolute(programPath).lexically_normal();
    std::string identity = absolute.string();
    std::uint64_t hash = 14695981039346656037ull;
    for (unsigned char byte : identity) {
        hash ^= byte;
        hash *= 1099511628211ull;
    }
    std::string name = absolute.stem().string();
    for (char &character : name) {
        if (!std::isalnum(static_cast<unsigned char>(character)) &&
            character != '-' && character != '_') {
            character = '_';
        }
    }
    std::ostringstream filename;
    filename << name << '-' << std::hex << std::setw(16) << std::setfill('0')
             << hash << ".sav";
    const char *home = std::getenv("HOME");
    std::filesystem::path root = home ? home : ".";
    return (root / ".ember16" / "saves" / filename.str()).string();
}
}

Emulator::Emulator(EmulatorOptions options)
    : options(std::move(options)), flame(), apu(), memory(flame, apu),
      cpu(memory), debugger(cpu, memory, flame) {
    debugger.enabled = this->options.debug;
}

int Emulator::run() {
    static_assert(BiosData.size() <= BiosRomEnd - BiosRomBase + 1);
    memory.reset();
    flame.reset();
    apu.reset();
    memory.load(options.loadAddress, readFile(options.programPath));
    memory.load(BiosRomBase,
                std::vector<std::uint8_t>(BiosData.begin(), BiosData.end()));
    std::string savePath = savePathFor(options.programPath);
    memory.configureSaveRam(savePath);
    cpu.reset(BiosRomBase);

    if (!sdl.open(options.scale, apu)) {
        std::cerr << "SDL: " << sdl.error() << "\n";
        return 1;
    }

    bool running = true;
    auto frameTime = std::chrono::microseconds(1000000 / RefreshRate);

    while (running) {
        auto started = std::chrono::steady_clock::now();
        running = sdl.poll(memory);
        if (!running) {
            break;
        }

        if (debugger.enabled && debugger.shouldBreak()) {
            debugger.repl();
        }

        runFrame();
        if (memory.consumeSaveRamFirstWrite()) {
            sdl.showSaveRamNotice(savePath);
        }
        memory.flushSaveRam();
        if (memory.consumeInputPad1Read()) {
            sdl.enableTwoPlayerControls();
        }
        flame.renderFrame();
        apu.stepFrame();
        if (flame.consumeVblankInterrupt()) {
            cpu.requestInterrupt(1);
        } else if (apu.consumeInterrupt()) {
            cpu.requestInterrupt(2);
        } else {
            cpu.wake();
        }
        sdl.present(flame);

        if (cpu.halted()) {
            running = false;
        }

        auto elapsed = std::chrono::steady_clock::now() - started;
        if (elapsed < frameTime) {
            std::this_thread::sleep_for(frameTime - elapsed);
        }
    }

    memory.flushSaveRam();
    return 0;
}

void Emulator::runFrame() {
    for (int i = 0; i < InstructionsPerFrame; i++) {
        if (debugger.enabled && debugger.shouldBreak()) {
            break;
        }
        StopReason reason = cpu.step();
        if (reason == StopReason::Fault) {
            Disassembler disassembler(memory);
            DecodedInstruction decoded = disassembler.decode(cpu.faultAddress());
            std::cerr << "Runtime error at " << hex(cpu.faultAddress(), 6)
                      << ": " << cpu.fault() << "\n"
                      << decoded.text << "\n";
            if (debugger.enabled) {
                debugger.repl();
            }
            break;
        }
        if (reason == StopReason::Trap) {
            if (debugger.enabled) {
                debugger.repl();
                continue;
            }
            std::cerr << "Runtime trap at " << hex(mask24(cpu.state().pc - 1), 6)
                      << "\n";
            break;
        }
        if (reason == StopReason::Halted || reason == StopReason::Waiting) {
            break;
        }
    }
}

}

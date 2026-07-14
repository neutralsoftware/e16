#include "e16/emulator.h"

#include "bios_data.h"
#include "e16/disassembler.h"
#include "e16/log.h"

#include <cctype>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
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
    std::filesystem::path programPath =
        std::filesystem::absolute(options.programPath).lexically_normal();
    std::vector<std::uint8_t> program = readFile(programPath.string());
    if (program.empty()) {
        throw std::runtime_error("program is empty: " + programPath.string());
    }
    logInfo("program: " + programPath.string() +
            " bytes=" + std::to_string(program.size()) +
            " load-address=" + hex(options.loadAddress, 6));
    memory.reset();
    flame.reset();
    apu.reset();
    memory.load(options.loadAddress, program);
    memory.load(BiosRomBase,
                std::vector<std::uint8_t>(BiosData.begin(), BiosData.end()));
    std::string savePath = savePathFor(options.programPath);
    memory.configureSaveRam(savePath);
    cpu.reset(BiosRomBase);

    if (!sdl.open(options.scale, apu, options.headless, options.windowed,
                  options.muted)) {
        logError("SDL: " + sdl.error());
        return 1;
    }
    if (sdl.isHeadless()) {
        logInfo("running headless");
    }

    bool running = true;
    int result = 0;
    auto frameTime = std::chrono::microseconds(1000000 / RefreshRate);

    while (running) {
        auto started = std::chrono::steady_clock::now();
        running = sdl.poll(memory);
        if (!running) {
            logInfo("exit requested by window or controller input");
            break;
        }

        if (debugger.enabled && debugger.shouldBreak()) {
            debugger.repl();
        }

        if (!runFrame()) {
            result = 1;
            break;
        }
        if (memory.consumeSaveRamFirstWrite()) {
            logInfo("save RAM: " + savePath);
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
        if (!sdl.present(flame)) {
            logError("SDL: " + sdl.error());
            result = 1;
            break;
        }

        if (cpu.halted()) {
            logInfo("CPU halted normally");
            running = false;
        }

        auto elapsed = std::chrono::steady_clock::now() - started;
        if (elapsed < frameTime) {
            std::this_thread::sleep_for(frameTime - elapsed);
        }
    }

    memory.flushSaveRam();
    return result;
}

bool Emulator::runFrame() {
    for (int i = 0; i < InstructionsPerFrame; i++) {
        if (debugger.enabled && debugger.shouldBreak()) {
            break;
        }
        StopReason reason = cpu.step();
        if (reason == StopReason::Fault) {
            Disassembler disassembler(memory);
            DecodedInstruction decoded =
                disassembler.decode(cpu.faultAddress());
            logError("Runtime error at " + hex(cpu.faultAddress(), 6) + ": " +
                     cpu.fault() + " | " + decoded.text);
            if (debugger.enabled) {
                debugger.repl();
            }
            return false;
        }
        if (reason == StopReason::Trap) {
            if (debugger.enabled) {
                debugger.repl();
                continue;
            }
            logError("Runtime trap at " + hex(mask24(cpu.state().pc - 1), 6));
            return false;
        }
        if (reason == StopReason::Halted || reason == StopReason::Waiting) {
            break;
        }
    }
    return true;
}

}

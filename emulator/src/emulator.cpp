#include "e16/emulator.h"

#include "e16/disassembler.h"

#include <chrono>
#include <iostream>
#include <thread>
#include <utility>

namespace e16 {

Emulator::Emulator(EmulatorOptions options)
    : options(std::move(options)), flame(), apu(), memory(flame, apu),
      cpu(memory), debugger(cpu, memory, flame) {
    debugger.enabled = this->options.debug;
}

int Emulator::run() {
    memory.reset();
    flame.reset();
    apu.reset();
    memory.load(options.loadAddress, readFile(options.programPath));
    cpu.reset(options.loadAddress);

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

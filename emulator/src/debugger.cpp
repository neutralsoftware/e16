#include "e16/debugger.h"

#include <iomanip>
#include <iostream>
#include <sstream>
#include <cstdlib>
#include <vector>

namespace e16 {

Debugger::Debugger(Cpu &cpu, Memory &memory, Flame &flame)
    : cpu(cpu), memory(memory), flame(flame) {}

bool Debugger::shouldBreak() {
    if (!enabled) {
        return false;
    }
    if (!continueMode) {
        return true;
    }
    std::uint32_t pc = cpu.state().pc;
    if (ignoringCurrentBreakpoint && pc != ignoredBreakpoint) {
        ignoringCurrentBreakpoint = false;
    }
    if (breakpoints.contains(pc)) {
        if (ignoringCurrentBreakpoint && pc == ignoredBreakpoint) {
            return false;
        }
        continueMode = false;
        return true;
    }
    return false;
}

void Debugger::repl() {
    continueMode = false;
    std::string line;
    while (true) {
        std::cout << "e16:" << hex(cpu.state().pc, 6) << "> " << std::flush;
        if (!std::getline(std::cin, line)) {
            enabled = false;
            return;
        }
        if (command(line)) {
            return;
        }
    }
}

void Debugger::printHelp() const {
    std::cout << "commands:\n"
              << "  s [n]                 step n instructions\n"
              << "  c                     continue until breakpoint/trap/halt\n"
              << "  b <addr>              add breakpoint\n"
              << "  rb <addr>             remove breakpoint\n"
              << "  bl                    list breakpoints\n"
              << "  regs                  print registers\n"
              << "  mem <addr> [count]    dump memory\n"
              << "  dis <addr> [count]    dump raw instruction bytes\n"
              << "  set <reg> <value>     set register\n"
              << "  write <addr> <value>  write byte\n"
              << "  flame                 print VPU state\n"
              << "  q                     quit emulator\n";
}

void Debugger::printRegisters() const {
    const CpuState &s = cpu.state();
    for (int i = 0; i < 16; i++) {
        std::cout << "r" << std::dec << i << "=" << hex(s.r[i], 4)
                  << ((i % 4 == 3) ? "\n" : "  ");
    }
    std::cout << "pc=" << hex(s.pc, 6) << "  sp=" << hex(s.sp, 6)
              << "  fp=" << hex(s.fp, 6) << "  dp=" << hex(s.dp, 6)
              << "  ivt=" << hex(s.ivt, 6) << "  fl=" << hex(s.fl, 4)
              << "\n";
    std::cout << "flags:"
              << ((s.fl & FlagZ) ? " Z" : "")
              << ((s.fl & FlagN) ? " N" : "")
              << ((s.fl & FlagC) ? " C" : "")
              << ((s.fl & FlagV) ? " V" : "")
              << ((s.fl & FlagI) ? " I" : "") << "\n";
}

void Debugger::printMemory(std::uint32_t address, std::uint32_t count) const {
    for (std::uint32_t i = 0; i < count; i++) {
        if (i % 16 == 0) {
            std::cout << "\n" << hex(mask24(address + i), 6) << ": ";
        }
        std::cout << std::uppercase << std::hex << std::setw(2)
                  << std::setfill('0')
                  << static_cast<int>(memory.read8(address + i)) << " ";
    }
    std::cout << std::dec << "\n";
}

void Debugger::printDisasm(std::uint32_t address, std::uint32_t count) const {
    for (std::uint32_t i = 0; i < count; i++) {
        std::uint32_t pc = mask24(address + i);
        std::cout << hex(pc, 6) << "  " << hex(memory.read8(pc), 2) << "\n";
    }
}

void Debugger::printFlame() const {
    std::cout << "control=" << hex(flame.control(), 4)
              << " status=" << hex(flame.status(), 4)
              << " frame=" << flame.frameCounter()
              << " irq_enable=" << hex(flame.irqEnable(), 4)
              << " irq_status=" << hex(flame.irqStatus(), 4) << "\n";
    for (std::size_t i = 0; i < 4; i++) {
        BackgroundLayer layer = flame.layer(i);
        std::cout << "layer" << i << " control=" << hex(layer.control, 4)
                  << " tilemap=" << hex(layer.tilemapBase, 6)
                  << " tile=" << hex(layer.tileBase, 6)
                  << " scroll=(" << layer.scrollX << "," << layer.scrollY
                  << ") size=(" << layer.width << "," << layer.height
                  << ")\n";
    }
}

bool Debugger::command(const std::string &line) {
    std::istringstream in(line);
    std::string op;
    in >> op;
    if (op.empty()) {
        return false;
    }
    if (op == "h" || op == "help" || op == "?") {
        printHelp();
        return false;
    }
    if (op == "s" || op == "step") {
        std::uint32_t count = 1;
        in >> count;
        for (std::uint32_t i = 0; i < count; i++) {
            StopReason reason = cpu.step();
            if (reason != StopReason::None) {
                break;
            }
        }
        printRegisters();
        return false;
    }
    if (op == "c" || op == "continue") {
        continueMode = true;
        ignoredBreakpoint = cpu.state().pc;
        ignoringCurrentBreakpoint = breakpoints.contains(ignoredBreakpoint);
        return true;
    }
    if (op == "b") {
        std::string value;
        in >> value;
        breakpoints.insert(parseNumber(value));
        return false;
    }
    if (op == "rb") {
        std::string value;
        in >> value;
        breakpoints.erase(parseNumber(value));
        return false;
    }
    if (op == "bl") {
        for (std::uint32_t bp : breakpoints) {
            std::cout << hex(bp, 6) << "\n";
        }
        return false;
    }
    if (op == "regs") {
        printRegisters();
        return false;
    }
    if (op == "mem") {
        std::string addr;
        std::uint32_t count = 64;
        in >> addr >> count;
        printMemory(parseNumber(addr), count);
        return false;
    }
    if (op == "dis") {
        std::string addr;
        std::uint32_t count = 16;
        in >> addr >> count;
        printDisasm(parseNumber(addr), count);
        return false;
    }
    if (op == "set") {
        std::string reg;
        std::string value;
        in >> reg >> value;
        std::uint32_t parsed = parseNumber(value);
        CpuState &s = cpu.state();
        if (reg.rfind("r", 0) == 0) {
            int id = std::stoi(reg.substr(1));
            if (id >= 0 && id < 16) {
                s.r[static_cast<std::size_t>(id)] = low16(parsed);
            }
        } else if (reg == "pc") {
            s.pc = mask24(parsed);
        } else if (reg == "sp") {
            s.sp = mask24(parsed);
        } else if (reg == "fp") {
            s.fp = mask24(parsed);
        } else if (reg == "dp") {
            s.dp = mask24(parsed);
        } else if (reg == "ivt") {
            s.ivt = mask24(parsed);
        } else if (reg == "fl") {
            s.fl = low16(parsed) & 0x001F;
        }
        return false;
    }
    if (op == "write") {
        std::string addr;
        std::string value;
        in >> addr >> value;
        memory.write8(parseNumber(addr), static_cast<std::uint8_t>(parseNumber(value)));
        return false;
    }
    if (op == "flame") {
        printFlame();
        return false;
    }
    if (op == "q" || op == "quit" || op == "exit") {
        std::exit(0);
    }
    std::cout << "unknown command\n";
    return false;
}

}

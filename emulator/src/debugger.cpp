#include "e16/debugger.h"

#include <cstdlib>
#include <exception>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <vector>

namespace e16 {

Debugger::Debugger(Cpu &cpu, Memory &memory, Flame &flame)
    : cpu(cpu), memory(memory), flame(flame), disassembler(memory) {}

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
    std::cout << "\nEmber-16 monitor\n";
    printStatus();
    std::string line;
    while (true) {
        std::cout << "\ne16 " << hex(cpu.state().pc, 6) << " > " << std::flush;
        if (!std::getline(std::cin, line)) {
            enabled = false;
            return;
        }
        try {
            if (command(line)) {
                return;
            }
        } catch (const std::exception &error) {
            std::cout << "error: " << error.what() << "\n";
        }
    }
}

void Debugger::printHelp() const {
    std::cout
        << "commands:\n"
        << "  dis [addr] [count]    disassemble live memory\n"
        << "  s [n]                 step n instructions\n"
        << "  c                     continue until breakpoint/trap/halt\n"
        << "  regs                  print registers\n"
        << "  mem <addr> [count]    dump memory bytes\n"
        << "  b <addr>              add breakpoint\n"
        << "  rb <addr>             remove breakpoint\n"
        << "  bl                    list breakpoints\n"
        << "  set <reg> <value>     set register\n"
        << "  write <addr> <value>  write byte\n"
        << "  flame                 print VPU state\n"
        << "  status                print CPU status and next instruction\n"
        << "  q                     quit emulator\n";
}

void Debugger::printStatus() const {
    const CpuState &s = cpu.state();
    DecodedInstruction decoded = disassembler.decode(s.pc);
    std::cout << "pc=" << hex(s.pc, 6) << " sp=" << hex(s.sp, 6)
              << " fp=" << hex(s.fp, 6) << " dp=" << hex(s.dp, 6)
              << " fl=" << hex(s.fl, 4);
    if (cpu.halted()) {
        std::cout << " halted";
    } else if (cpu.waiting()) {
        std::cout << " waiting";
    }
    std::cout << "\nnext  " << hex(decoded.address, 6) << "  ";
    for (std::size_t i = 0; i < decoded.bytes.size(); i++) {
        std::cout << std::uppercase << std::hex << std::setw(2)
                  << std::setfill('0') << static_cast<int>(decoded.bytes[i])
                  << " ";
    }
    std::cout << std::dec << "  " << decoded.text << "\n";
}

void Debugger::printStop(StopReason reason) const {
    if (reason == StopReason::None) {
        return;
    }
    if (reason == StopReason::Fault) {
        DecodedInstruction decoded = disassembler.decode(cpu.faultAddress());
        std::cout << "runtime error at " << hex(cpu.faultAddress(), 6) << ": "
                  << cpu.fault() << "\n"
                  << decoded.text << "\n";
        return;
    }
    if (reason == StopReason::Trap) {
        std::cout << "trap\n";
        return;
    }
    if (reason == StopReason::Halted) {
        std::cout << "halted\n";
        return;
    }
    if (reason == StopReason::Waiting) {
        std::cout << "waiting for interrupt\n";
    }
}

void Debugger::printRegisters() const {
    const CpuState &s = cpu.state();
    for (int i = 0; i < 16; i++) {
        std::cout << "r" << std::dec << i << "=" << hex(s.r[i], 4)
                  << ((i % 4 == 3) ? "\n" : "  ");
    }
    std::cout << "pc=" << hex(s.pc, 6) << "  sp=" << hex(s.sp, 6)
              << "  fp=" << hex(s.fp, 6) << "  dp=" << hex(s.dp, 6)
              << "  ivt=" << hex(s.ivt, 6) << "  fl=" << hex(s.fl, 4) << "\n";
    std::cout << "flags:" << ((s.fl & FlagZ) ? " Z" : "")
              << ((s.fl & FlagN) ? " N" : "") << ((s.fl & FlagC) ? " C" : "")
              << ((s.fl & FlagV) ? " V" : "") << ((s.fl & FlagI) ? " I" : "")
              << "\n";
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
    std::uint32_t pc = address;
    for (std::uint32_t i = 0; i < count; i++) {
        DecodedInstruction decoded = disassembler.decode(pc);
        std::cout << (pc == cpu.state().pc ? "=> " : "   ")
                  << hex(decoded.address, 6) << "  ";
        for (std::size_t j = 0; j < 8; j++) {
            if (j < decoded.bytes.size()) {
                std::cout << std::uppercase << std::hex << std::setw(2)
                          << std::setfill('0')
                          << static_cast<int>(decoded.bytes[j]) << " ";
            } else {
                std::cout << "   ";
            }
        }
        std::cout << std::dec << decoded.text << "\n";
        pc = mask24(pc + decoded.size);
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
                  << " tile=" << hex(layer.tileBase, 6) << " scroll=("
                  << layer.scrollX << "," << layer.scrollY << ") size=("
                  << layer.width << "," << layer.height << ")\n";
    }
}

bool Debugger::command(const std::string &line) {
    std::istringstream in(line);
    std::string op;
    in >> op;
    if (op.empty()) {
        printStatus();
        return false;
    }
    if (op == "h" || op == "help" || op == "?") {
        printHelp();
        return false;
    }
    if (op == "clear" || op == "cls") {
        std::cout << "\x1B[2J\x1B[H";
        return false;
    }
    if (op == "status" || op == "st") {
        printStatus();
        return false;
    }
    if (op == "s" || op == "step") {
        std::uint32_t count = 1;
        in >> count;
        StopReason reason = StopReason::None;
        for (std::uint32_t i = 0; i < count; i++) {
            reason = cpu.step();
            if (reason != StopReason::None) {
                break;
            }
        }
        printStop(reason);
        printRegisters();
        printDisasm(cpu.state().pc, 6);
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
        std::uint32_t address = parseNumber(value);
        breakpoints.insert(address);
        std::cout << "breakpoint set at " << hex(address, 6) << "\n";
        return false;
    }
    if (op == "rb") {
        std::string value;
        in >> value;
        std::uint32_t address = parseNumber(value);
        breakpoints.erase(address);
        std::cout << "breakpoint removed at " << hex(address, 6) << "\n";
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
    if (op == "dis" || op == "u") {
        std::string addr;
        std::uint32_t start = cpu.state().pc;
        std::uint32_t count = 12;
        if (in >> addr) {
            if (addr != ".") {
                start = parseNumber(addr);
            }
            in >> count;
        }
        printDisasm(start, count);
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
        std::cout << reg << " = " << hex(parsed, reg == "fl" ? 4 : 6) << "\n";
        return false;
    }
    if (op == "write") {
        std::string addr;
        std::string value;
        in >> addr >> value;
        std::uint32_t address = parseNumber(addr);
        std::uint8_t byte = static_cast<std::uint8_t>(parseNumber(value));
        memory.write8(address, byte);
        std::cout << hex(address, 6) << " = " << hex(byte, 2) << "\n";
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

} // namespace e16

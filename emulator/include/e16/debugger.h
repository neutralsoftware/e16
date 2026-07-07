#ifndef E16_DEBUGGER_H
#define E16_DEBUGGER_H

#include "e16/cpu.h"
#include "e16/disassembler.h"
#include "e16/flame.h"
#include "e16/memory.h"

#include <cstdint>
#include <set>
#include <string>

namespace e16 {

class Debugger {
  public:
    Debugger(Cpu &cpu, Memory &memory, Flame &flame);

    bool enabled = false;
    bool shouldBreak();
    void repl();

  private:
    Cpu &cpu;
    Memory &memory;
    Flame &flame;
    Disassembler disassembler;
    std::set<std::uint32_t> breakpoints;
    bool continueMode = false;
    bool ignoringCurrentBreakpoint = false;
    std::uint32_t ignoredBreakpoint = 0;

    void printHelp() const;
    void printStatus() const;
    void printStop(StopReason reason) const;
    void printRegisters() const;
    void printMemory(std::uint32_t address, std::uint32_t count) const;
    void printDisasm(std::uint32_t address, std::uint32_t count) const;
    void printFlame() const;
    bool command(const std::string &line);
};

}

#endif

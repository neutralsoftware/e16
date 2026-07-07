#ifndef E16_CPU_H
#define E16_CPU_H

#include "e16/common.h"
#include "e16/memory.h"

#include <array>
#include <cstdint>
#include <string>

namespace e16 {

enum class StopReason {
    None,
    Halted,
    Waiting,
    Trap,
    Fault,
    Breakpoint
};

struct CpuState {
    std::array<std::uint16_t, 16> r{};
    std::uint32_t pc = DefaultLoadAddress;
    std::uint32_t sp = DefaultStackPointer;
    std::uint32_t fp = DefaultStackPointer;
    std::uint16_t fl = FlagI;
    std::uint32_t dp = 0;
    std::uint32_t ivt = 0;
};

class Cpu {
  public:
    explicit Cpu(Memory &memory);

    void reset(std::uint32_t pc = DefaultLoadAddress);
    StopReason step();
    void requestInterrupt(std::uint8_t number);
    void wake();
    bool halted() const;
    bool waiting() const;
    const std::string &fault() const;
    std::uint32_t faultAddress() const;
    CpuState &state();
    const CpuState &state() const;

  private:
    Memory &mem;
    CpuState s;
    bool haltState = false;
    bool waitState = false;
    bool interruptPending = false;
    std::uint8_t pendingInterrupt = 0;
    std::uint32_t instructionPc = DefaultLoadAddress;
    std::uint32_t faultPc = DefaultLoadAddress;
    std::string faultText;

    std::uint8_t fetch8();
    std::uint16_t fetch16();
    std::uint32_t fetch24();
    std::uint16_t readReg(std::uint8_t id) const;
    void writeReg(std::uint8_t id, std::uint16_t value);
    std::uint32_t readSpecial(std::uint8_t id) const;
    void writeSpecial(std::uint8_t id, std::uint32_t value);
    bool flag(std::uint16_t mask) const;
    void setFlag(std::uint16_t mask, bool enabled);
    void setNZ(std::uint16_t value);
    void setAddFlags(std::uint16_t a, std::uint16_t b, std::uint32_t result);
    void setSubFlags(std::uint16_t a, std::uint16_t b, std::uint32_t result);
    void push16(std::uint16_t value);
    void push24(std::uint32_t value);
    std::uint16_t pop16();
    std::uint32_t pop24();
    std::uint32_t memoryAddress(std::uint8_t mode, bool withRegister,
                                std::uint8_t &reg);
    StopReason interrupt(std::uint8_t number);
    StopReason fault(const std::string &message);
    bool branchCondition(std::uint8_t opcode) const;
    StopReason execBinary(std::uint8_t opcode);
    StopReason execUnary(std::uint8_t opcode);
};

}

#endif

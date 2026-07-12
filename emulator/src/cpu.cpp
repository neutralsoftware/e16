#include "e16/cpu.h"

#include <sstream>

namespace e16 {

namespace {
bool isBinary(std::uint8_t opcode) {
    return opcode == 0x01 || opcode == 0x02 || opcode == 0x03 ||
           opcode == 0x05 || opcode == 0x06 ||
           (opcode >= 0x20 && opcode <= 0x23) ||
           (opcode >= 0x27 && opcode <= 0x2B) ||
           (opcode >= 0x30 && opcode <= 0x32) ||
           (opcode >= 0x34 && opcode <= 0x37) ||
           (opcode >= 0x40 && opcode <= 0x42) || opcode == 0x50;
}

bool isUnary(std::uint8_t opcode) {
    return opcode == 0x04 || (opcode >= 0x24 && opcode <= 0x26) ||
           opcode == 0x33 || (opcode >= 0x43 && opcode <= 0x46) ||
           opcode == 0x74 || opcode == 0x75;
}
}

Cpu::Cpu(Memory &memory) : mem(memory) {}

void Cpu::reset(std::uint32_t pc) {
    s = CpuState{};
    s.pc = mask24(pc);
    s.sp = DefaultStackPointer;
    s.fp = DefaultStackPointer;
    s.fl = FlagI;
    haltState = false;
    waitState = false;
    interruptPending = false;
    pendingInterrupt = 0;
    instructionPc = s.pc;
    faultPc = s.pc;
    faultText.clear();
}

StopReason Cpu::step() {
    if (haltState) {
        return StopReason::Halted;
    }
    if (interruptPending && !flag(FlagI)) {
        return interrupt(pendingInterrupt);
    }
    if (waitState) {
        if (interruptPending) {
            waitState = false;
            if (!flag(FlagI)) {
                return interrupt(pendingInterrupt);
            }
        } else {
            return StopReason::Waiting;
        }
    }

    instructionPc = s.pc;
    std::uint8_t opcode = fetch8();

    if (opcode == 0x00) {
        return StopReason::None;
    }
    if (opcode >= 0x10 && opcode <= 0x17) {
        std::uint8_t mode = fetch8();
        std::uint8_t reg = 0;
        if (opcode == 0x17) {
            if (mode != 0x02) {
                return fault("addr expects absolute addressing mode");
            }
            reg = fetch8();
            std::uint32_t address = fetch24();
            s.dp = address & 0xFF0000;
            writeReg(reg, low16(address));
            return StopReason::None;
        }
        if (mode < 0x02 || mode > 0x07) {
            return fault("bad memory addressing mode " + hex(mode, 2));
        }
        std::uint32_t address = memoryAddress(mode, true, reg);
        if (opcode >= 0x10 && opcode <= 0x13) {
            if (opcode == 0x11) {
                writeReg(reg, mem.read8(address));
            } else if (opcode == 0x13) {
                writeReg(reg, static_cast<std::uint16_t>(
                                  static_cast<std::int16_t>(
                                      static_cast<std::int8_t>(mem.read8(address)))));
            } else {
                writeReg(reg, mem.read16(address));
            }
            return StopReason::None;
        }
        if (opcode == 0x14 || opcode == 0x16) {
            mem.write16(address, readReg(reg));
            return StopReason::None;
        }
        if (opcode == 0x15) {
            mem.write8(address, static_cast<std::uint8_t>(readReg(reg) & 0xFF));
            return StopReason::None;
        }
    }
    if (isBinary(opcode)) {
        return execBinary(opcode);
    }
    if (isUnary(opcode)) {
        return execUnary(opcode);
    }
    if (opcode >= 0x60 && opcode <= 0x6F) {
        std::int16_t relative = static_cast<std::int16_t>(fetch16());
        if (branchCondition(opcode)) {
            s.pc = mask24(static_cast<std::uint32_t>(
                static_cast<std::int32_t>(s.pc) + relative));
        }
        return StopReason::None;
    }
    if (opcode == 0x70) {
        std::uint32_t target = fetch24();
        push24(s.pc);
        s.pc = target;
        return StopReason::None;
    }
    if (opcode == 0x71) {
        s.pc = pop24();
        return StopReason::None;
    }
    if (opcode == 0x72) {
        std::uint16_t size = fetch16();
        push24(s.fp);
        s.fp = s.sp;
        s.sp = mask24(s.sp - size);
        return StopReason::None;
    }
    if (opcode == 0x73) {
        s.sp = s.fp;
        s.fp = pop24();
        return StopReason::None;
    }
    if (opcode == 0x76) {
        push16(s.fl);
        return StopReason::None;
    }
    if (opcode == 0x77) {
        s.fl = pop16() & 0x001F;
        return StopReason::None;
    }
    if (opcode == 0x78) {
        for (std::uint8_t i = 0; i < 16; i++) {
            push16(s.r[i]);
        }
        return StopReason::None;
    }
    if (opcode == 0x79) {
        for (int i = 15; i >= 0; i--) {
            s.r[static_cast<std::size_t>(i)] = pop16();
        }
        return StopReason::None;
    }
    if (opcode == 0x80) {
        return interrupt(fetch8());
    }
    if (opcode == 0x81) {
        s.fl = pop16() & 0x001F;
        s.pc = pop24();
        return StopReason::None;
    }
    if (opcode == 0x82) {
        setFlag(FlagI, false);
        return StopReason::None;
    }
    if (opcode == 0x83) {
        setFlag(FlagI, true);
        return StopReason::None;
    }
    if (opcode == 0x84) {
        waitState = true;
        return StopReason::Waiting;
    }
    if (opcode == 0x85) {
        haltState = true;
        return StopReason::Halted;
    }
    if (opcode == 0x86) {
        reset(BiosRomBase);
        return StopReason::None;
    }
    if (opcode == 0x87) {
        return StopReason::Trap;
    }
    if (opcode == 0x90) {
        std::uint8_t reg = fetch8();
        std::uint8_t special = fetch8();
        if (special < 0x10 || special > 0x15) {
            return fault("bad special register " + hex(special, 2));
        }
        writeReg(reg, low16(readSpecial(special)));
        return StopReason::None;
    }
    if (opcode == 0x91) {
        std::uint8_t mode = fetch8();
        std::uint8_t special = fetch8();
        if (special < 0x10 || special > 0x15) {
            return fault("bad special register " + hex(special, 2));
        }
        if (mode == 0x01) {
            writeSpecial(special, readReg(fetch8()));
            return StopReason::None;
        }
        if (mode == 0x00) {
            writeSpecial(special, fetch24());
            return StopReason::None;
        }
        return fault("bad set addressing mode");
    }
    if (opcode == 0xA0) {
        mem.requestDma();
        return StopReason::None;
    }

    std::ostringstream out;
    out << "unknown opcode " << hex(opcode, 2);
    return fault(out.str());
}

void Cpu::requestInterrupt(std::uint8_t number) {
    interruptPending = true;
    pendingInterrupt = number;
    waitState = false;
}

void Cpu::wake() {
    waitState = false;
}

bool Cpu::halted() const {
    return haltState;
}

bool Cpu::waiting() const {
    return waitState;
}

const std::string &Cpu::fault() const {
    return faultText;
}

std::uint32_t Cpu::faultAddress() const {
    return faultPc;
}

CpuState &Cpu::state() {
    return s;
}

const CpuState &Cpu::state() const {
    return s;
}

std::uint8_t Cpu::fetch8() {
    std::uint8_t value = mem.read8(s.pc);
    s.pc = mask24(s.pc + 1);
    return value;
}

std::uint16_t Cpu::fetch16() {
    std::uint16_t value = mem.read16(s.pc);
    s.pc = mask24(s.pc + 2);
    return value;
}

std::uint32_t Cpu::fetch24() {
    std::uint32_t value = mem.read24(s.pc);
    s.pc = mask24(s.pc + 3);
    return value;
}

std::uint16_t Cpu::readReg(std::uint8_t id) const {
    return s.r[id & 0x0F];
}

void Cpu::writeReg(std::uint8_t id, std::uint16_t value) {
    s.r[id & 0x0F] = value;
}

std::uint32_t Cpu::readSpecial(std::uint8_t id) const {
    switch (id) {
    case 0x10:
        return s.pc;
    case 0x11:
        return s.sp;
    case 0x12:
        return s.fp;
    case 0x13:
        return s.fl;
    case 0x14:
        return s.dp;
    case 0x15:
        return s.ivt;
    default:
        return 0;
    }
}

void Cpu::writeSpecial(std::uint8_t id, std::uint32_t value) {
    value = mask24(value);
    switch (id) {
    case 0x10:
        s.pc = value;
        break;
    case 0x11:
        s.sp = value;
        break;
    case 0x12:
        s.fp = value;
        break;
    case 0x13:
        s.fl = static_cast<std::uint16_t>(value & 0x001F);
        break;
    case 0x14:
        s.dp = value;
        break;
    case 0x15:
        s.ivt = value;
        break;
    default:
        break;
    }
}

bool Cpu::flag(std::uint16_t mask) const {
    return (s.fl & mask) != 0;
}

void Cpu::setFlag(std::uint16_t mask, bool enabled) {
    if (enabled) {
        s.fl |= mask;
    } else {
        s.fl &= static_cast<std::uint16_t>(~mask);
    }
}

void Cpu::setNZ(std::uint16_t value) {
    setFlag(FlagZ, value == 0);
    setFlag(FlagN, (value & 0x8000) != 0);
}

void Cpu::setAddFlags(std::uint16_t a, std::uint16_t b, std::uint32_t result) {
    std::uint16_t r = low16(result);
    setNZ(r);
    setFlag(FlagC, result > 0xFFFF);
    setFlag(FlagV, ((~(a ^ b) & (a ^ r)) & 0x8000) != 0);
}

void Cpu::setSubFlags(std::uint16_t a, std::uint16_t b, std::uint32_t result) {
    std::uint16_t r = low16(result);
    setNZ(r);
    setFlag(FlagC, a >= b);
    setFlag(FlagV, (((a ^ b) & (a ^ r)) & 0x8000) != 0);
}

void Cpu::push16(std::uint16_t value) {
    s.sp = mask24(s.sp - 2);
    mem.write16(s.sp, value);
}

void Cpu::push24(std::uint32_t value) {
    s.sp = mask24(s.sp - 3);
    mem.write24(s.sp, value);
}

std::uint16_t Cpu::pop16() {
    std::uint16_t value = mem.read16(s.sp);
    s.sp = mask24(s.sp + 2);
    return value;
}

std::uint32_t Cpu::pop24() {
    std::uint32_t value = mem.read24(s.sp);
    s.sp = mask24(s.sp + 3);
    return value;
}

std::uint32_t Cpu::memoryAddress(std::uint8_t mode, bool withRegister,
                                 std::uint8_t &reg) {
    if (mode == 0x02) {
        if (withRegister) {
            reg = fetch8();
        }
        return fetch24();
    }
    if (mode == 0x03) {
        std::uint8_t packed = fetch8();
        if (withRegister) {
            reg = packed >> 4;
            return mask24(s.dp + readReg(packed & 0x0F));
        }
        return mask24(s.dp + readReg(packed & 0x0F));
    }
    if (mode == 0x04) {
        if (withRegister) {
            reg = fetch8();
        }
        std::uint32_t base = fetch24();
        std::uint8_t index = fetch8();
        return mask24(base + readReg(index));
    }
    if (mode == 0x05) {
        if (withRegister) {
            reg = fetch8();
        }
        std::uint8_t baseReg = fetch8();
        std::int16_t offset = static_cast<std::int16_t>(fetch16());
        std::uint32_t base = baseReg <= 0x0F
                                 ? s.dp + readReg(baseReg)
                                 : readSpecial(baseReg);
        return mask24(base + offset);
    }
    if (mode == 0x06) {
        if (withRegister) {
            reg = fetch8();
        }
        return mask24(s.dp + fetch16());
    }
    if (mode == 0x07) {
        if (withRegister) {
            reg = fetch8();
        }
        return mask24(s.sp + fetch16());
    }
    return 0;
}

StopReason Cpu::interrupt(std::uint8_t number) {
    interruptPending = false;
    waitState = false;
    std::uint32_t vector = mem.read24(s.ivt + static_cast<std::uint32_t>(number) * 3);
    if (vector == 0) {
        return StopReason::None;
    }
    push24(s.pc);
    push16(s.fl);
    setFlag(FlagI, true);
    s.pc = vector;
    return StopReason::None;
}

StopReason Cpu::fault(const std::string &message) {
    faultPc = instructionPc;
    faultText = message;
    haltState = true;
    return StopReason::Fault;
}

bool Cpu::branchCondition(std::uint8_t opcode) const {
    switch (opcode) {
    case 0x60:
    case 0x61:
        return true;
    case 0x62:
        return flag(FlagZ);
    case 0x63:
        return !flag(FlagZ);
    case 0x64:
        return flag(FlagC);
    case 0x65:
        return !flag(FlagC);
    case 0x66:
        return flag(FlagN);
    case 0x67:
        return !flag(FlagN);
    case 0x68:
        return flag(FlagV);
    case 0x69:
        return !flag(FlagV);
    case 0x6A:
        return !flag(FlagZ) && flag(FlagN) == flag(FlagV);
    case 0x6B:
        return flag(FlagN) == flag(FlagV);
    case 0x6C:
        return flag(FlagN) != flag(FlagV);
    case 0x6D:
        return flag(FlagZ) || flag(FlagN) != flag(FlagV);
    case 0x6E:
        return flag(FlagC) && !flag(FlagZ);
    case 0x6F:
        return !flag(FlagC) || flag(FlagZ);
    default:
        return false;
    }
}

StopReason Cpu::execBinary(std::uint8_t opcode) {
    std::uint8_t selector = fetch8();
    std::uint8_t dst = 0;
    std::uint16_t source = 0;
    std::uint32_t memory = 0;
    bool memoryForm = false;

    if (selector == 0x00 && opcode != 0x05 && opcode != 0x06) {
        dst = fetch8();
        source = fetch16();
    } else if (opcode == 0x06 && selector >= 0x02 && selector <= 0x07) {
        memoryForm = true;
        memory = memoryAddress(selector, true, dst);
        source = mem.read16(memory);
    } else if (opcode != 0x06) {
        dst = selector >> 4;
        source = readReg(selector & 0x0F);
    } else if (selector >= 0x02 && selector <= 0x07) {
        memoryForm = true;
        memory = memoryAddress(selector, true, dst);
        source = mem.read16(memory);
    } else {
        return fault("bad binary operand");
    }

    std::uint16_t dest = readReg(dst);
    std::uint32_t result = dest;

    switch (opcode) {
    case 0x01:
    case 0x03:
        writeReg(dst, source);
        return StopReason::None;
    case 0x02:
        writeReg(dst, source & 0x00FF);
        return StopReason::None;
    case 0x05:
        writeReg(dst, source);
        writeReg(selector & 0x0F, dest);
        return StopReason::None;
    case 0x06:
        if (!memoryForm) {
            return fault("xchg expects a memory operand");
        }
        mem.write16(memory, dest);
        writeReg(dst, source);
        return StopReason::None;
    case 0x20:
        result = static_cast<std::uint32_t>(dest) + source;
        setAddFlags(dest, source, result);
        writeReg(dst, low16(result));
        return StopReason::None;
    case 0x21:
        result = static_cast<std::uint32_t>(dest) + source + (flag(FlagC) ? 1 : 0);
        setAddFlags(dest, static_cast<std::uint16_t>(source + (flag(FlagC) ? 1 : 0)),
                    result);
        writeReg(dst, low16(result));
        return StopReason::None;
    case 0x22:
        result = static_cast<std::uint32_t>(dest) - source;
        setSubFlags(dest, source, result);
        writeReg(dst, low16(result));
        return StopReason::None;
    case 0x23:
        source = static_cast<std::uint16_t>(source + (flag(FlagC) ? 0 : 1));
        result = static_cast<std::uint32_t>(dest) - source;
        setSubFlags(dest, source, result);
        writeReg(dst, low16(result));
        return StopReason::None;
    case 0x27:
        result = static_cast<std::uint32_t>(dest) * source;
        setNZ(low16(result));
        setFlag(FlagC, result > 0xFFFF);
        setFlag(FlagV, result > 0x7FFF);
        writeReg(dst, low16(result));
        return StopReason::None;
    case 0x28: {
        std::int32_t signedResult =
            static_cast<std::int16_t>(dest) * static_cast<std::int16_t>(source);
        writeReg(dst, low16(static_cast<std::uint32_t>(signedResult)));
        setNZ(readReg(dst));
        setFlag(FlagC, signedResult < -32768 || signedResult > 32767);
        setFlag(FlagV, signedResult < -32768 || signedResult > 32767);
        return StopReason::None;
    }
    case 0x29:
        if (source == 0) {
            return fault("division by zero");
        }
        writeReg(dst, static_cast<std::uint16_t>(dest / source));
        setNZ(readReg(dst));
        return StopReason::None;
    case 0x2A:
        if (source == 0) {
            return fault("division by zero");
        }
        writeReg(dst, static_cast<std::uint16_t>(static_cast<std::int16_t>(dest) /
                                                 static_cast<std::int16_t>(source)));
        setNZ(readReg(dst));
        return StopReason::None;
    case 0x2B:
        if (source == 0) {
            return fault("division by zero");
        }
        writeReg(dst, static_cast<std::uint16_t>(dest % source));
        setNZ(readReg(dst));
        return StopReason::None;
    case 0x30:
        writeReg(dst, dest & source);
        setNZ(readReg(dst));
        return StopReason::None;
    case 0x31:
        writeReg(dst, dest | source);
        setNZ(readReg(dst));
        return StopReason::None;
    case 0x32:
        writeReg(dst, dest ^ source);
        setNZ(readReg(dst));
        return StopReason::None;
    case 0x34:
        setNZ(dest & source);
        return StopReason::None;
    case 0x35:
        writeReg(dst, dest | source);
        setNZ(readReg(dst));
        return StopReason::None;
    case 0x36:
        writeReg(dst, dest & static_cast<std::uint16_t>(~source));
        setNZ(readReg(dst));
        return StopReason::None;
    case 0x37:
        writeReg(dst, dest ^ source);
        setNZ(readReg(dst));
        return StopReason::None;
    case 0x40: {
        int amount = source & 0x1F;
        result = amount >= 16 ? 0 : static_cast<std::uint32_t>(dest) << amount;
        setFlag(FlagC, amount != 0 && amount <= 16 &&
                           ((dest >> (16 - amount)) & 1u) != 0);
        writeReg(dst, low16(result));
        setNZ(readReg(dst));
        return StopReason::None;
    }
    case 0x41: {
        int amount = source & 0x1F;
        setFlag(FlagC, amount != 0 && amount <= 16 &&
                           ((dest >> (amount - 1)) & 1u) != 0);
        writeReg(dst, amount >= 16 ? 0 : static_cast<std::uint16_t>(dest >> amount));
        setNZ(readReg(dst));
        return StopReason::None;
    }
    case 0x42: {
        int amount = source & 0x1F;
        std::int16_t signedDest = static_cast<std::int16_t>(dest);
        setFlag(FlagC, amount != 0 && amount <= 16 &&
                           ((dest >> (amount - 1)) & 1u) != 0);
        writeReg(dst, amount >= 16
                          ? (signedDest < 0 ? 0xFFFF : 0)
                          : static_cast<std::uint16_t>(signedDest >> amount));
        setNZ(readReg(dst));
        return StopReason::None;
    }
    case 0x50:
        result = static_cast<std::uint32_t>(dest) - source;
        setSubFlags(dest, source, result);
        return StopReason::None;
    default:
        return fault("bad binary opcode");
    }
}

StopReason Cpu::execUnary(std::uint8_t opcode) {
    std::uint8_t selector = fetch8();
    std::uint16_t value = 0;
    std::uint32_t address = 0;
    std::uint8_t reg = 0;
    bool memoryForm = false;

    if (selector <= 0x0F) {
        reg = selector;
        value = readReg(reg);
    } else if (selector >= 0x02 && selector <= 0x07) {
        memoryForm = true;
        address = memoryAddress(selector, false, reg);
        value = mem.read16(address);
    } else {
        return fault("bad unary operand");
    }

    auto writeValue = [&](std::uint16_t out) {
        if (memoryForm) {
            mem.write16(address, out);
        } else {
            writeReg(reg, out);
        }
    };

    switch (opcode) {
    case 0x04:
        writeValue(0);
        setNZ(0);
        return StopReason::None;
    case 0x24: {
        std::uint16_t out = static_cast<std::uint16_t>(value + 1);
        writeValue(out);
        setNZ(out);
        setFlag(FlagV, value == 0x7FFF);
        return StopReason::None;
    }
    case 0x25: {
        std::uint16_t out = static_cast<std::uint16_t>(value - 1);
        writeValue(out);
        setNZ(out);
        setFlag(FlagV, value == 0x8000);
        return StopReason::None;
    }
    case 0x26: {
        std::uint16_t out = static_cast<std::uint16_t>(-static_cast<std::int16_t>(value));
        writeValue(out);
        setNZ(out);
        setFlag(FlagC, value != 0);
        setFlag(FlagV, value == 0x8000);
        return StopReason::None;
    }
    case 0x33:
        writeValue(static_cast<std::uint16_t>(~value));
        setNZ(static_cast<std::uint16_t>(~value));
        return StopReason::None;
    case 0x43: {
        std::uint16_t out = static_cast<std::uint16_t>((value << 1) | (value >> 15));
        writeValue(out);
        setFlag(FlagC, (value & 0x8000) != 0);
        setNZ(out);
        return StopReason::None;
    }
    case 0x44: {
        std::uint16_t out = static_cast<std::uint16_t>((value >> 1) | (value << 15));
        writeValue(out);
        setFlag(FlagC, (value & 0x0001) != 0);
        setNZ(out);
        return StopReason::None;
    }
    case 0x45: {
        bool oldCarry = flag(FlagC);
        std::uint16_t out = static_cast<std::uint16_t>((value << 1) |
                                                       (oldCarry ? 1 : 0));
        writeValue(out);
        setFlag(FlagC, (value & 0x8000) != 0);
        setNZ(out);
        return StopReason::None;
    }
    case 0x46: {
        bool oldCarry = flag(FlagC);
        std::uint16_t out = static_cast<std::uint16_t>((value >> 1) |
                                                       (oldCarry ? 0x8000 : 0));
        writeValue(out);
        setFlag(FlagC, (value & 0x0001) != 0);
        setNZ(out);
        return StopReason::None;
    }
    case 0x74:
        push16(value);
        return StopReason::None;
    case 0x75:
        if (memoryForm) {
            mem.write16(address, pop16());
        } else {
            writeReg(reg, pop16());
        }
        return StopReason::None;
    default:
        return fault("bad unary opcode");
    }
}

}

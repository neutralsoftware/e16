#include "e16/disassembler.h"

#include <iomanip>
#include <sstream>
#include <unordered_map>

namespace e16 {

namespace {

struct OpInfo {
    const char *name;
    int operands;
};

const std::unordered_map<std::uint8_t, OpInfo> &ops() {
    static const std::unordered_map<std::uint8_t, OpInfo> table = {
        {0x00, {"nop", 0}},     {0x01, {"mov", 2}},
        {0x02, {"movb", 2}},    {0x03, {"movw", 2}},
        {0x04, {"clr", 1}},     {0x05, {"swap", 2}},
        {0x06, {"xchg", 2}},    {0x10, {"load", 2}},
        {0x11, {"loadb", 2}},   {0x12, {"loadw", 2}},
        {0x13, {"loadsb", 2}},  {0x14, {"store", 2}},
        {0x15, {"storeb", 2}},  {0x16, {"storew", 2}},
        {0x17, {"addr", 2}},    {0x20, {"add", 2}},
        {0x21, {"addwc", 2}},   {0x22, {"sub", 2}},
        {0x23, {"subwc", 2}},   {0x24, {"inc", 1}},
        {0x25, {"dec", 1}},     {0x26, {"neg", 1}},
        {0x27, {"mul", 2}},     {0x28, {"muls", 2}},
        {0x29, {"div", 2}},     {0x2A, {"divs", 2}},
        {0x2B, {"mod", 2}},     {0x30, {"and", 2}},
        {0x31, {"or", 2}},      {0x32, {"xor", 2}},
        {0x33, {"not", 1}},     {0x34, {"test", 2}},
        {0x35, {"setb", 2}},    {0x36, {"clearb", 2}},
        {0x37, {"toggleb", 2}}, {0x40, {"shl", 2}},
        {0x41, {"shr", 2}},     {0x42, {"sar", 2}},
        {0x43, {"rol", 1}},     {0x44, {"ror", 1}},
        {0x45, {"rcl", 1}},     {0x46, {"rcr", 1}},
        {0x50, {"cmp", 2}},     {0x60, {"jmp", 1}},
        {0x61, {"bra", 1}},     {0x62, {"beq", 1}},
        {0x63, {"bne", 1}},     {0x64, {"bcs", 1}},
        {0x65, {"bcc", 1}},     {0x66, {"bmi", 1}},
        {0x67, {"bpl", 1}},     {0x68, {"bvs", 1}},
        {0x69, {"bvc", 1}},     {0x6A, {"bgt", 1}},
        {0x6B, {"bge", 1}},     {0x6C, {"blt", 1}},
        {0x6D, {"ble", 1}},     {0x6E, {"bhi", 1}},
        {0x6F, {"bls", 1}},     {0x70, {"call", 1}},
        {0x71, {"ret", 0}},     {0x72, {"enter", 1}},
        {0x73, {"leave", 0}},   {0x74, {"push", 1}},
        {0x75, {"pop", 1}},     {0x76, {"pushf", 0}},
        {0x77, {"popf", 0}},    {0x78, {"pusha", 0}},
        {0x79, {"popa", 0}},    {0x80, {"int", 1}},
        {0x81, {"iret", 0}},    {0x82, {"ei", 0}},
        {0x83, {"di", 0}},      {0x84, {"wait", 0}},
        {0x85, {"halt", 0}},    {0x86, {"reset", 0}},
        {0x87, {"trap", 0}},    {0x90, {"get", 2}},
        {0x91, {"set", 2}},     {0xA0, {"dma", 0}},
    };
    return table;
}

bool branch(std::uint8_t opcode) {
    return opcode >= 0x61 && opcode <= 0x6F;
}

bool noOperands(std::uint8_t opcode) {
    return opcode == 0x00 || opcode == 0x71 || opcode == 0x73 ||
           (opcode >= 0x76 && opcode <= 0x79) ||
           (opcode >= 0x81 && opcode <= 0x87) || opcode == 0xA0;
}

bool load(std::uint8_t opcode) {
    return opcode >= 0x10 && opcode <= 0x13;
}

bool store(std::uint8_t opcode) {
    return opcode >= 0x14 && opcode <= 0x16;
}

bool unary(std::uint8_t opcode) {
    return opcode == 0x04 || (opcode >= 0x24 && opcode <= 0x26) ||
           opcode == 0x33 || (opcode >= 0x43 && opcode <= 0x46) ||
           opcode == 0x74 || opcode == 0x75;
}

bool binary(std::uint8_t opcode) {
    return opcode == 0x01 || opcode == 0x02 || opcode == 0x03 ||
           opcode == 0x05 || opcode == 0x06 ||
           (opcode >= 0x20 && opcode <= 0x23) ||
           (opcode >= 0x27 && opcode <= 0x2B) ||
           (opcode >= 0x30 && opcode <= 0x32) ||
           (opcode >= 0x34 && opcode <= 0x37) ||
           (opcode >= 0x40 && opcode <= 0x42) || opcode == 0x50;
}

}

Disassembler::Disassembler(const Memory &memory) : memory(memory) {}

DecodedInstruction Disassembler::decode(std::uint32_t address) const {
    address = mask24(address);
    std::uint8_t opcode = read8(address);
    auto found = ops().find(opcode);
    if (found == ops().end()) {
        return {address, 1, bytes(address, 1), ".byte " + hex(opcode, 2),
                false};
    }

    const char *name = found->second.name;
    if (noOperands(opcode)) {
        return {address, 1, bytes(address, 1), name, true};
    }
    if (opcode == 0x60) {
        return {address, 4, bytes(address, 4),
                std::string(name) + " " + hex(read24(address + 1), 6), true};
    }
    if (branch(opcode)) {
        std::int16_t rel = readS16(address + 1);
        std::uint32_t target = mask24(address + 3 + rel);
        return {address, 3, bytes(address, 3),
                std::string(name) + " " + hex(target, 6), true};
    }
    if (opcode == 0x70) {
        return {address, 4, bytes(address, 4),
                std::string(name) + " " + hex(read24(address + 1), 6), true};
    }
    if (opcode == 0x72) {
        return {address, 3, bytes(address, 3),
                std::string(name) + " " + hex(read16(address + 1), 4), true};
    }
    if (opcode == 0x80) {
        return {address, 2, bytes(address, 2),
                std::string(name) + " " + hex(read8(address + 1), 2), true};
    }
    if (opcode == 0x90) {
        return {address, 3, bytes(address, 3),
                std::string(name) + " " + reg(read8(address + 1)) + ", " +
                    special(read8(address + 2)),
                true};
    }
    if (opcode == 0x91) {
        std::uint8_t mode = read8(address + 1);
        if (mode == 0x01) {
            return {address, 4, bytes(address, 4),
                    std::string(name) + " " + special(read8(address + 2)) +
                        ", " + reg(read8(address + 3)),
                    true};
        }
        if (mode == 0x00) {
            return {address, 6, bytes(address, 6),
                    std::string(name) + " " + special(read8(address + 2)) +
                        ", " + hex(read24(address + 3), 6),
                    true};
        }
        return {address, 2, bytes(address, 2),
                std::string(name) + " <bad set mode " + hex(mode, 2) + ">",
                false};
    }

    if (load(opcode) || store(opcode) || opcode == 0x17) {
        std::uint8_t mode = read8(address + 1);
        std::uint8_t regId = 0;
        std::uint8_t size = 1;
        std::string mem = memoryWithRegister(address + 2, mode, regId, size);
        if (opcode == 0x17) {
            return {address, 6, bytes(address, 6),
                    std::string(name) + " " + reg(read8(address + 2)) + ", " +
                        "@" + hex(read24(address + 3), 6),
                    true};
        }
        if (load(opcode)) {
            return {address, static_cast<std::uint8_t>(2 + size),
                    bytes(address, static_cast<std::uint8_t>(2 + size)),
                    std::string(name) + " " + reg(regId) + ", " + mem, true};
        }
        return {address, static_cast<std::uint8_t>(2 + size),
                bytes(address, static_cast<std::uint8_t>(2 + size)),
                std::string(name) + " " + mem + ", " + reg(regId), true};
    }

    if (unary(opcode)) {
        std::uint8_t selector = read8(address + 1);
        if (selector <= 0x0F) {
            return {address, 2, bytes(address, 2),
                    std::string(name) + " " + reg(selector), true};
        }
        if (selector >= 0x02 && selector <= 0x07) {
            std::uint8_t size = 1;
            std::string operand = unaryMemory(address + 2, selector, size);
            return {address, static_cast<std::uint8_t>(2 + size),
                    bytes(address, static_cast<std::uint8_t>(2 + size)),
                    std::string(name) + " " + operand, true};
        }
        return {address, 2, bytes(address, 2),
                std::string(name) + " <bad operand " + hex(selector, 2) + ">",
                false};
    }

    if (binary(opcode)) {
        std::uint8_t selector = read8(address + 1);
        if (selector == 0x00 && opcode != 0x05 && opcode != 0x06) {
            return {address, 5, bytes(address, 5),
                    std::string(name) + " " + reg(read8(address + 2)) + ", " +
                        hex(read16(address + 3), 4),
                    true};
        }
        if (opcode == 0x06 && selector >= 0x02 && selector <= 0x07) {
            std::uint8_t regId = 0;
            std::uint8_t size = 1;
            std::string mem =
                memoryWithRegister(address + 2, selector, regId, size);
            return {address, static_cast<std::uint8_t>(2 + size),
                    bytes(address, static_cast<std::uint8_t>(2 + size)),
                    std::string(name) + " " + reg(regId) + ", " + mem, true};
        }
        if (opcode != 0x06) {
            return {address, 2, bytes(address, 2),
                    std::string(name) + " " + reg(selector >> 4) + ", " +
                        reg(selector & 0x0F),
                    true};
        }
    }

    return {address, 1, bytes(address, 1),
            std::string(name) + " <undecoded>", false};
}

std::uint8_t Disassembler::read8(std::uint32_t address) const {
    return memory.read8(address);
}

std::uint16_t Disassembler::read16(std::uint32_t address) const {
    return memory.read16(address);
}

std::int16_t Disassembler::readS16(std::uint32_t address) const {
    return static_cast<std::int16_t>(read16(address));
}

std::uint32_t Disassembler::read24(std::uint32_t address) const {
    return memory.read24(address);
}

std::vector<std::uint8_t> Disassembler::bytes(std::uint32_t address,
                                              std::uint8_t count) const {
    std::vector<std::uint8_t> out;
    out.reserve(count);
    for (std::uint8_t i = 0; i < count; i++) {
        out.push_back(read8(address + i));
    }
    return out;
}

std::string Disassembler::reg(std::uint8_t id) const {
    return "r" + std::to_string(id & 0x0F);
}

std::string Disassembler::special(std::uint8_t id) const {
    switch (id) {
    case 0x10:
        return "pc";
    case 0x11:
        return "sp";
    case 0x12:
        return "fp";
    case 0x13:
        return "fl";
    case 0x14:
        return "dp";
    case 0x15:
        return "ivt";
    default:
        return "sr" + hex(id, 2);
    }
}

std::string Disassembler::byteList(
    const std::vector<std::uint8_t> &value) const {
    std::ostringstream out;
    for (std::size_t i = 0; i < value.size(); i++) {
        if (i != 0) {
            out << ' ';
        }
        out << std::uppercase << std::hex << std::setw(2) << std::setfill('0')
            << static_cast<int>(value[i]);
    }
    return out.str();
}

std::string Disassembler::memoryWithRegister(std::uint32_t address,
                                             std::uint8_t mode,
                                             std::uint8_t &regId,
                                             std::uint8_t &size) const {
    if (mode == 0x02) {
        regId = read8(address);
        size = 4;
        return "@" + hex(read24(address + 1), 6);
    }
    if (mode == 0x03) {
        std::uint8_t packed = read8(address);
        regId = packed >> 4;
        size = 1;
        return "@" + reg(packed & 0x0F);
    }
    if (mode == 0x04) {
        regId = read8(address);
        size = 5;
        return "@(" + hex(read24(address + 1), 6) + " + " +
               reg(read8(address + 4)) + ")";
    }
    if (mode == 0x05) {
        regId = read8(address);
        std::uint8_t base = read8(address + 1);
        std::int16_t offset = readS16(address + 2);
        size = 4;
        return "@(" + (base <= 0x0F ? reg(base) : special(base)) +
               (offset < 0 ? " - " : " + ") +
               hex(static_cast<std::uint16_t>(offset < 0 ? -offset : offset),
                   4) +
               ")";
    }
    if (mode == 0x06) {
        regId = read8(address);
        size = 3;
        return "dp:" + hex(read16(address + 1), 4);
    }
    if (mode == 0x07) {
        regId = read8(address);
        size = 3;
        return "#" + hex(read16(address + 1), 4);
    }
    size = 0;
    return "<bad memory mode " + hex(mode, 2) + ">";
}

std::string Disassembler::unaryMemory(std::uint32_t address, std::uint8_t mode,
                                      std::uint8_t &size) const {
    if (mode == 0x02) {
        size = 3;
        return "@" + hex(read24(address), 6);
    }
    if (mode == 0x03) {
        size = 1;
        return "@" + reg(read8(address));
    }
    if (mode == 0x04) {
        size = 4;
        return "@(" + hex(read24(address), 6) + " + " +
               reg(read8(address + 3)) + ")";
    }
    if (mode == 0x05) {
        std::uint8_t base = read8(address);
        std::int16_t offset = readS16(address + 1);
        size = 3;
        return "@(" + (base <= 0x0F ? reg(base) : special(base)) +
               (offset < 0 ? " - " : " + ") +
               hex(static_cast<std::uint16_t>(offset < 0 ? -offset : offset),
                   4) +
               ")";
    }
    if (mode == 0x06) {
        size = 2;
        return "dp:" + hex(read16(address), 4);
    }
    if (mode == 0x07) {
        size = 2;
        return "#" + hex(read16(address), 4);
    }
    size = 0;
    return "<bad memory mode " + hex(mode, 2) + ">";
}

}

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>
#include <unordered_map>
#include <vector>

namespace {
enum class Group {
    DataMovement,
    Memory,
    Arithmetic,
    Bitwise,
    Shift,
    Compare,
    Branch,
    CallStack,
    InterruptSystem,
    Special,
    Helper,
    Data
};

enum class Form {
    NoOperands,
    Branch,
    Call,
    Enter,
    Interrupt,
    Get,
    Set,
    Load,
    Store,
    Addr,
    GenericUnary,
    GenericBinary
};

struct OpSpec {
    std::uint8_t opcode;
    std::string name;
    int operandCount;
    Group group;
    Form form;
};

struct Options {
    std::string inputPath;
    std::uint32_t baseAddress = 0x200000;
    bool color = true;
    bool pager = true;
    bool plain = false;
    bool labels = true;
    bool bytes = true;
};

struct DecodedLine {
    std::uint32_t address = 0;
    std::vector<std::uint8_t> bytes;
    Group group = Group::Data;
    std::string mnemonic;
    std::vector<std::string> operands;
    std::string comment;
    std::optional<std::uint32_t> target;
    bool data = false;
};

struct Candidate {
    std::size_t size = 1;
    int score = 0;
    DecodedLine line;
};

struct RenderedProgram {
    std::vector<std::string> colored;
    std::vector<std::string> plain;
};

struct TerminalSize {
    int columns = 80;
    int rows = 24;
};

std::string trim(std::string value) {
    while (!value.empty() &&
           std::isspace(static_cast<unsigned char>(value.front()))) {
        value.erase(value.begin());
    }

    while (!value.empty() &&
           std::isspace(static_cast<unsigned char>(value.back()))) {
        value.pop_back();
    }

    return value;
}

bool startsWith(const std::string &value, const std::string &prefix) {
    return value.rfind(prefix, 0) == 0;
}

std::int64_t parseNumber(const std::string &text) {
    std::string number = trim(text);
    int sign = 1;

    if (number.empty()) {
        throw std::invalid_argument("empty number");
    }

    if (number.front() == '+' || number.front() == '-') {
        if (number.front() == '-') {
            sign = -1;
        }
        number.erase(number.begin());
    }

    if (number.empty()) {
        throw std::invalid_argument("empty number");
    }

    std::size_t pos = 0;
    std::int64_t parsed = 0;

    if (startsWith(number, "0x") || startsWith(number, "0X")) {
        parsed = std::stoll(number, &pos, 16);
    } else if (startsWith(number, "0b") || startsWith(number, "0B")) {
        parsed = std::stoll(number.substr(2), &pos, 2);
        pos += 2;
    } else if (startsWith(number, "0o") || startsWith(number, "0O")) {
        parsed = std::stoll(number.substr(2), &pos, 8);
        pos += 2;
    } else {
        parsed = std::stoll(number, &pos, 10);
    }

    if (pos != number.size()) {
        throw std::invalid_argument("bad number");
    }

    return parsed * sign;
}

std::uint32_t parseAddress(const std::string &value) {
    std::int64_t parsed = parseNumber(value);
    if (parsed < 0 || parsed > 0xFFFFFF) {
        throw std::runtime_error("address must fit in 24 bits");
    }
    return static_cast<std::uint32_t>(parsed);
}

std::string hexValue(std::uint64_t value, int width) {
    std::ostringstream out;
    out << "0x" << std::uppercase << std::hex << std::setw(width)
        << std::setfill('0') << value;
    return out.str();
}

std::string hexRaw(std::uint64_t value, int width) {
    std::ostringstream out;
    out << std::uppercase << std::hex << std::setw(width) << std::setfill('0')
        << value;
    return out.str();
}

std::string byteList(const std::vector<std::uint8_t> &bytes) {
    std::ostringstream out;
    for (std::size_t i = 0; i < bytes.size(); i++) {
        if (i != 0) {
            out << ' ';
        }
        out << std::uppercase << std::hex << std::setw(2) << std::setfill('0')
            << static_cast<int>(bytes[i]);
    }
    return out.str();
}

std::string signedOffset(int value) {
    if (value < 0) {
        return "- " + hexValue(static_cast<std::uint16_t>(-value), 4);
    }
    return "+ " + hexValue(static_cast<std::uint16_t>(value), 4);
}

bool canRead(const std::vector<std::uint8_t> &bytes, std::size_t offset,
             std::size_t count) {
    return offset <= bytes.size() && count <= bytes.size() - offset;
}

std::uint16_t read16(const std::vector<std::uint8_t> &bytes,
                     std::size_t offset) {
    return static_cast<std::uint16_t>(bytes[offset]) |
           static_cast<std::uint16_t>(bytes[offset + 1] << 8);
}

std::int16_t readSigned16(const std::vector<std::uint8_t> &bytes,
                          std::size_t offset) {
    return static_cast<std::int16_t>(read16(bytes, offset));
}

std::uint32_t read24(const std::vector<std::uint8_t> &bytes,
                     std::size_t offset) {
    return static_cast<std::uint32_t>(bytes[offset]) |
           (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
           (static_cast<std::uint32_t>(bytes[offset + 2]) << 16);
}

std::vector<std::uint8_t> sliceBytes(const std::vector<std::uint8_t> &bytes,
                                     std::size_t offset, std::size_t count) {
    return std::vector<std::uint8_t>(bytes.begin() + offset,
                                     bytes.begin() + offset + count);
}

std::string generalRegister(std::uint8_t id) {
    if (id <= 0x0F) {
        return "r" + std::to_string(static_cast<int>(id));
    }
    return "r?" + hexValue(id, 2);
}

std::string registerName(std::uint8_t id) {
    if (id <= 0x0F) {
        return generalRegister(id);
    }

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
        return "sr" + hexValue(id, 2);
    }
}

bool isNoOperand(const std::string &name) {
    static const std::array<std::string_view, 15> names = {
        "nop",  "ret", "leave", "pushf", "popf", "pusha",
        "popa", "iret", "ei",   "di",    "wait", "halt",
        "reset", "trap", "dma"};

    return std::find(names.begin(), names.end(), std::string_view{name}) !=
           names.end();
}

bool isBranch(const std::string &name) {
    static const std::array<std::string_view, 16> names = {
        "jmp", "bra", "beq", "bne", "bcs", "bcc", "bmi", "bpl",
        "bvs", "bvc", "bgt", "bge", "blt", "ble", "bhi", "bls"};

    return std::find(names.begin(), names.end(), std::string_view{name}) !=
           names.end();
}

bool isLoad(const std::string &name) {
    return name == "load" || name == "loadb" || name == "loadw" ||
           name == "loadsb";
}

bool isStore(const std::string &name) {
    return name == "store" || name == "storeb" || name == "storew";
}

Group groupFor(const std::string &name) {
    if (name == "nop" || name == "mov" || name == "movb" ||
        name == "movw" || name == "clr" || name == "swap" ||
        name == "xchg") {
        return Group::DataMovement;
    }
    if (isLoad(name) || isStore(name) || name == "addr") {
        return Group::Memory;
    }
    if (name == "add" || name == "addwc" || name == "sub" ||
        name == "subwc" || name == "inc" || name == "dec" ||
        name == "neg" || name == "mul" || name == "muls" ||
        name == "div" || name == "divs" || name == "mod") {
        return Group::Arithmetic;
    }
    if (name == "and" || name == "or" || name == "xor" ||
        name == "not" || name == "test" || name == "setb" ||
        name == "clearb" || name == "toggleb") {
        return Group::Bitwise;
    }
    if (name == "shl" || name == "shr" || name == "sar" ||
        name == "rol" || name == "ror" || name == "rcl" ||
        name == "rcr") {
        return Group::Shift;
    }
    if (name == "cmp") {
        return Group::Compare;
    }
    if (isBranch(name)) {
        return Group::Branch;
    }
    if (name == "call" || name == "ret" || name == "enter" ||
        name == "leave" || name == "push" || name == "pop" ||
        name == "pushf" || name == "popf" || name == "pusha" ||
        name == "popa") {
        return Group::CallStack;
    }
    if (name == "int" || name == "iret" || name == "ei" ||
        name == "di" || name == "wait" || name == "halt" ||
        name == "reset" || name == "trap") {
        return Group::InterruptSystem;
    }
    if (name == "get" || name == "set") {
        return Group::Special;
    }
    return Group::Helper;
}

Form formFor(const std::string &name, int operandCount) {
    if (isNoOperand(name)) {
        return Form::NoOperands;
    }
    if (isBranch(name)) {
        return Form::Branch;
    }
    if (name == "call") {
        return Form::Call;
    }
    if (name == "enter") {
        return Form::Enter;
    }
    if (name == "int") {
        return Form::Interrupt;
    }
    if (name == "get") {
        return Form::Get;
    }
    if (name == "set") {
        return Form::Set;
    }
    if (isLoad(name)) {
        return Form::Load;
    }
    if (isStore(name)) {
        return Form::Store;
    }
    if (name == "addr") {
        return Form::Addr;
    }
    return operandCount == 1 ? Form::GenericUnary : Form::GenericBinary;
}

std::unordered_map<int, OpSpec> makeSpecs() {
    std::unordered_map<int, OpSpec> specs;

    auto add = [&](std::uint8_t opcode, const std::string &name,
                   int operandCount) {
        specs[opcode] =
            OpSpec{opcode, name, operandCount, groupFor(name),
                   formFor(name, operandCount)};
    };

    add(0x00, "nop", 0);
    add(0x01, "mov", 2);
    add(0x02, "movb", 2);
    add(0x03, "movw", 2);
    add(0x04, "clr", 1);
    add(0x05, "swap", 2);
    add(0x06, "xchg", 2);
    add(0x10, "load", 2);
    add(0x11, "loadb", 2);
    add(0x12, "loadw", 2);
    add(0x13, "loadsb", 2);
    add(0x14, "store", 2);
    add(0x15, "storeb", 2);
    add(0x16, "storew", 2);
    add(0x17, "addr", 2);
    add(0x20, "add", 2);
    add(0x21, "addwc", 2);
    add(0x22, "sub", 2);
    add(0x23, "subwc", 2);
    add(0x24, "inc", 1);
    add(0x25, "dec", 1);
    add(0x26, "neg", 1);
    add(0x27, "mul", 2);
    add(0x28, "muls", 2);
    add(0x29, "div", 2);
    add(0x2A, "divs", 2);
    add(0x2B, "mod", 2);
    add(0x30, "and", 2);
    add(0x31, "or", 2);
    add(0x32, "xor", 2);
    add(0x33, "not", 1);
    add(0x34, "test", 2);
    add(0x35, "setb", 2);
    add(0x36, "clearb", 2);
    add(0x37, "toggleb", 2);
    add(0x40, "shl", 2);
    add(0x41, "shr", 2);
    add(0x42, "sar", 2);
    add(0x43, "rol", 1);
    add(0x44, "ror", 1);
    add(0x45, "rcl", 1);
    add(0x46, "rcr", 1);
    add(0x50, "cmp", 2);
    add(0x60, "jmp", 1);
    add(0x61, "bra", 1);
    add(0x62, "beq", 1);
    add(0x63, "bne", 1);
    add(0x64, "bcs", 1);
    add(0x65, "bcc", 1);
    add(0x66, "bmi", 1);
    add(0x67, "bpl", 1);
    add(0x68, "bvs", 1);
    add(0x69, "bvc", 1);
    add(0x6A, "bgt", 1);
    add(0x6B, "bge", 1);
    add(0x6C, "blt", 1);
    add(0x6D, "ble", 1);
    add(0x6E, "bhi", 1);
    add(0x6F, "bls", 1);
    add(0x70, "call", 1);
    add(0x71, "ret", 0);
    add(0x72, "enter", 1);
    add(0x73, "leave", 0);
    add(0x74, "push", 1);
    add(0x75, "pop", 1);
    add(0x76, "pushf", 0);
    add(0x77, "popf", 0);
    add(0x78, "pusha", 0);
    add(0x79, "popa", 0);
    add(0x80, "int", 1);
    add(0x81, "iret", 0);
    add(0x82, "ei", 0);
    add(0x83, "di", 0);
    add(0x84, "wait", 0);
    add(0x85, "halt", 0);
    add(0x86, "reset", 0);
    add(0x87, "trap", 0);
    add(0x90, "get", 2);
    add(0x91, "set", 2);
    add(0xA0, "dma", 0);

    return specs;
}

DecodedLine makeInstruction(const OpSpec &spec, std::uint32_t address,
                            const std::vector<std::uint8_t> &bytes,
                            std::vector<std::string> operands,
                            std::string comment = {}) {
    DecodedLine line;
    line.address = address;
    line.bytes = bytes;
    line.group = spec.group;
    line.mnemonic = spec.name;
    line.operands = std::move(operands);
    line.comment = std::move(comment);
    return line;
}

DecodedLine makeDataByte(std::uint32_t address, std::uint8_t value,
                         std::string comment) {
    DecodedLine line;
    line.address = address;
    line.bytes = {value};
    line.group = Group::Data;
    line.mnemonic = ".byte";
    line.operands = {hexValue(value, 2)};
    line.comment = std::move(comment);
    line.data = true;
    return line;
}

std::string memoryAbsolute(std::uint32_t address) {
    return "@" + hexValue(address, 6);
}

std::string memoryRegisterIndirect(std::uint8_t reg) {
    return "@" + generalRegister(reg);
}

std::string memoryIndexed(std::uint32_t address, std::uint8_t reg) {
    return "@(" + hexValue(address, 6) + " + " + generalRegister(reg) + ")";
}

std::string memoryBaseOffset(std::uint8_t base, std::int16_t offset) {
    return "@(" + registerName(base) + " " + signedOffset(offset) + ")";
}

std::string memoryDirectPage(std::uint16_t offset) {
    return "dp:" + hexValue(offset, 4);
}

std::string memoryStack(std::uint16_t offset) {
    return "#" + hexValue(offset, 4);
}

std::optional<Candidate> decodeMemoryWithRegister(
    const std::vector<std::uint8_t> &bytes, std::size_t offset,
    std::uint32_t address, const OpSpec &spec, int score) {
    if (!canRead(bytes, offset, 2)) {
        return std::nullopt;
    }

    std::uint8_t mode = bytes[offset + 1];

    auto make = [&](std::size_t size, std::uint8_t reg,
                    const std::string &memory) {
        std::vector<std::string> operands;
        if (spec.form == Form::Store) {
            operands = {memory, generalRegister(reg)};
        } else {
            operands = {generalRegister(reg), memory};
        }

        return Candidate{size, score,
                         makeInstruction(spec, address,
                                         sliceBytes(bytes, offset, size),
                                         operands)};
    };

    switch (mode) {
    case 0x02:
        if (!canRead(bytes, offset, 6)) {
            return std::nullopt;
        }
        return make(6, bytes[offset + 2], memoryAbsolute(read24(bytes, offset + 3)));
    case 0x03:
        if (!canRead(bytes, offset, 3)) {
            return std::nullopt;
        }
        return make(3, bytes[offset + 2] >> 4,
                    memoryRegisterIndirect(bytes[offset + 2] & 0x0F));
    case 0x04:
        if (!canRead(bytes, offset, 7)) {
            return std::nullopt;
        }
        return make(7, bytes[offset + 2],
                    memoryIndexed(read24(bytes, offset + 3), bytes[offset + 6]));
    case 0x05:
        if (!canRead(bytes, offset, 6)) {
            return std::nullopt;
        }
        return make(6, bytes[offset + 2],
                    memoryBaseOffset(bytes[offset + 3],
                                     readSigned16(bytes, offset + 4)));
    case 0x06:
        if (!canRead(bytes, offset, 5)) {
            return std::nullopt;
        }
        return make(5, bytes[offset + 2], memoryDirectPage(read16(bytes, offset + 3)));
    case 0x07:
        if (!canRead(bytes, offset, 5)) {
            return std::nullopt;
        }
        return make(5, bytes[offset + 2], memoryStack(read16(bytes, offset + 3)));
    default:
        return std::nullopt;
    }
}

std::optional<Candidate> decodeUnaryMemory(
    const std::vector<std::uint8_t> &bytes, std::size_t offset,
    std::uint32_t address, const OpSpec &spec, int score) {
    if (!canRead(bytes, offset, 2)) {
        return std::nullopt;
    }

    std::uint8_t mode = bytes[offset + 1];

    auto make = [&](std::size_t size, const std::string &operand) {
        return Candidate{size, score,
                         makeInstruction(spec, address,
                                         sliceBytes(bytes, offset, size),
                                         {operand})};
    };

    switch (mode) {
    case 0x02:
        if (!canRead(bytes, offset, 5)) {
            return std::nullopt;
        }
        return make(5, memoryAbsolute(read24(bytes, offset + 2)));
    case 0x03:
        if (!canRead(bytes, offset, 3)) {
            return std::nullopt;
        }
        return make(3, memoryRegisterIndirect(bytes[offset + 2]));
    case 0x04:
        if (!canRead(bytes, offset, 6)) {
            return std::nullopt;
        }
        return make(6, memoryIndexed(read24(bytes, offset + 2), bytes[offset + 5]));
    case 0x05:
        if (!canRead(bytes, offset, 5)) {
            return std::nullopt;
        }
        return make(5, memoryBaseOffset(bytes[offset + 2],
                                        readSigned16(bytes, offset + 3)));
    case 0x06:
        if (!canRead(bytes, offset, 4)) {
            return std::nullopt;
        }
        return make(4, memoryDirectPage(read16(bytes, offset + 2)));
    case 0x07:
        if (!canRead(bytes, offset, 4)) {
            return std::nullopt;
        }
        return make(4, memoryStack(read16(bytes, offset + 2)));
    default:
        return std::nullopt;
    }
}

std::vector<Candidate> candidatesAt(
    const std::vector<std::uint8_t> &bytes, std::size_t offset,
    std::uint32_t baseAddress, const std::unordered_map<int, OpSpec> &specs) {
    std::vector<Candidate> candidates;
    std::uint8_t opcode = bytes[offset];
    std::uint32_t address =
        static_cast<std::uint32_t>((baseAddress + offset) & 0xFFFFFF);
    auto found = specs.find(opcode);

    if (found == specs.end()) {
        candidates.push_back(
            Candidate{1, -20, makeDataByte(address, opcode, "unknown opcode")});
        return candidates;
    }

    const OpSpec &spec = found->second;
    candidates.push_back(
        Candidate{1, -50, makeDataByte(address, opcode, "undecoded " + spec.name)});

    switch (spec.form) {
    case Form::NoOperands:
        candidates.push_back(
            Candidate{1, 100, makeInstruction(spec, address, {opcode}, {})});
        break;
    case Form::Branch:
        if (canRead(bytes, offset, 3)) {
            std::int16_t relative = readSigned16(bytes, offset + 1);
            std::uint32_t target = static_cast<std::uint32_t>(
                (static_cast<std::int64_t>(address) + 3 + relative) & 0xFFFFFF);
            DecodedLine line = makeInstruction(
                spec, address, sliceBytes(bytes, offset, 3), {hexValue(target, 6)},
                "rel " + std::to_string(relative));
            line.target = target;
            candidates.push_back(Candidate{3, 105, line});
        }
        break;
    case Form::Call:
        if (canRead(bytes, offset, 4)) {
            std::uint32_t target = read24(bytes, offset + 1);
            DecodedLine line = makeInstruction(
                spec, address, sliceBytes(bytes, offset, 4), {hexValue(target, 6)});
            line.target = target;
            candidates.push_back(Candidate{4, 100, line});
        }
        break;
    case Form::Enter:
        if (canRead(bytes, offset, 3)) {
            candidates.push_back(Candidate{
                3, 100,
                makeInstruction(spec, address, sliceBytes(bytes, offset, 3),
                                {hexValue(read16(bytes, offset + 1), 4)})});
        }
        break;
    case Form::Interrupt:
        if (canRead(bytes, offset, 2)) {
            candidates.push_back(Candidate{
                2, 100,
                makeInstruction(spec, address, sliceBytes(bytes, offset, 2),
                                {hexValue(bytes[offset + 1], 2)})});
        }
        break;
    case Form::Get:
        if (canRead(bytes, offset, 3)) {
            candidates.push_back(Candidate{
                3, 100,
                makeInstruction(spec, address, sliceBytes(bytes, offset, 3),
                                {generalRegister(bytes[offset + 1]),
                                 registerName(bytes[offset + 2])})});
        }
        break;
    case Form::Set:
        if (canRead(bytes, offset, 2)) {
            std::uint8_t mode = bytes[offset + 1];
            if (mode == 0x01 && canRead(bytes, offset, 4)) {
                candidates.push_back(Candidate{
                    4, 100,
                    makeInstruction(spec, address, sliceBytes(bytes, offset, 4),
                                    {registerName(bytes[offset + 2]),
                                     generalRegister(bytes[offset + 3])})});
            } else if (mode == 0x00 && canRead(bytes, offset, 6)) {
                candidates.push_back(Candidate{
                    6, 100,
                    makeInstruction(spec, address, sliceBytes(bytes, offset, 6),
                                    {registerName(bytes[offset + 2]),
                                     hexValue(read24(bytes, offset + 3), 6)})});
            }
        }
        break;
    case Form::Load:
    case Form::Store:
        if (auto decoded =
                decodeMemoryWithRegister(bytes, offset, address, spec, 105)) {
            candidates.push_back(*decoded);
        }
        break;
    case Form::Addr:
        if (canRead(bytes, offset, 6) && bytes[offset + 1] == 0x02) {
            candidates.push_back(Candidate{
                6, 105,
                makeInstruction(spec, address, sliceBytes(bytes, offset, 6),
                                {generalRegister(bytes[offset + 2]),
                                 memoryAbsolute(read24(bytes, offset + 3))})});
        }
        break;
    case Form::GenericUnary:
        if (canRead(bytes, offset, 2) && bytes[offset + 1] <= 0x0F) {
            candidates.push_back(Candidate{
                2, 82,
                makeInstruction(spec, address, sliceBytes(bytes, offset, 2),
                                {generalRegister(bytes[offset + 1])})});
        }
        if (canRead(bytes, offset, 4) && bytes[offset + 1] == 0x00) {
            candidates.push_back(Candidate{
                4, spec.name == "push" ? 74 : 38,
                makeInstruction(spec, address, sliceBytes(bytes, offset, 4),
                                {hexValue(read16(bytes, offset + 2), 4)})});
        }
        if (auto decoded =
                decodeUnaryMemory(bytes, offset, address, spec,
                                  spec.name == "push" || spec.name == "pop" ? 48
                                                                             : 70)) {
            candidates.push_back(*decoded);
        }
        break;
    case Form::GenericBinary:
        if (canRead(bytes, offset, 2)) {
            std::uint8_t packed = bytes[offset + 1];
            candidates.push_back(Candidate{
                2, spec.name == "swap" ? 88 : 58,
                makeInstruction(spec, address, sliceBytes(bytes, offset, 2),
                                {generalRegister(packed >> 4),
                                 generalRegister(packed & 0x0F)})});
        }
        if (canRead(bytes, offset, 5) && bytes[offset + 1] == 0x00) {
            candidates.push_back(Candidate{
                5, 90,
                makeInstruction(spec, address, sliceBytes(bytes, offset, 5),
                                {generalRegister(bytes[offset + 2]),
                                 hexValue(read16(bytes, offset + 3), 4)})});
        }
        if (auto decoded =
                decodeMemoryWithRegister(bytes, offset, address, spec,
                                         spec.name == "xchg" ? 86 : 68)) {
            candidates.push_back(*decoded);
        }
        break;
    }

    return candidates;
}

std::vector<DecodedLine> decodeProgram(
    const std::vector<std::uint8_t> &bytes, std::uint32_t baseAddress) {
    const auto specs = makeSpecs();
    std::vector<DecodedLine> lines;

    for (std::size_t offset = 0; offset < bytes.size();) {
        std::vector<Candidate> candidates =
            candidatesAt(bytes, offset, baseAddress, specs);
        Candidate best;
        bool found = false;

        for (const Candidate &candidate : candidates) {
            if (candidate.size == 0 || offset + candidate.size > bytes.size()) {
                continue;
            }

            bool better = !found || candidate.score > best.score;
            bool tie = found && candidate.score == best.score &&
                       candidate.size > best.size;

            if (better || tie) {
                best = candidate;
                found = true;
            }
        }

        if (!found) {
            best = Candidate{
                1, -100,
                makeDataByte(static_cast<std::uint32_t>((baseAddress + offset) &
                                                        0xFFFFFF),
                             bytes[offset], "decode error")};
        }

        lines.push_back(best.line);
        offset += best.size;
    }

    return lines;
}

std::map<std::uint32_t, std::string>
makeLabels(const std::vector<DecodedLine> &lines, std::uint32_t baseAddress,
           std::size_t byteCount) {
    std::map<std::uint32_t, std::string> labels;
    std::uint32_t endAddress =
        static_cast<std::uint32_t>((baseAddress + byteCount) & 0xFFFFFF);

    for (const DecodedLine &line : lines) {
        if (!line.target.has_value()) {
            continue;
        }

        std::uint32_t target = *line.target;
        bool inRange = baseAddress <= endAddress
                           ? target >= baseAddress && target < endAddress
                           : target >= baseAddress || target < endAddress;

        if (inRange) {
            labels[target] = "loc_" + hexRaw(target, 6);
        }
    }

    return labels;
}

namespace Ansi {
const std::string reset = "\x1b[0m";
const std::string bold = "\x1b[1m";
const std::string dim = "\x1b[2m";
const std::string inverse = "\x1b[7m";
const std::string gray = "\x1b[90m";
const std::string red = "\x1b[91m";
const std::string green = "\x1b[92m";
const std::string yellow = "\x1b[93m";
const std::string blue = "\x1b[94m";
const std::string magenta = "\x1b[95m";
const std::string cyan = "\x1b[96m";
const std::string white = "\x1b[97m";
}

std::string colorize(const std::string &text, const std::string &color,
                     bool enabled) {
    if (!enabled || text.empty()) {
        return text;
    }
    return color + text + Ansi::reset;
}

std::string groupColor(Group group) {
    switch (group) {
    case Group::DataMovement:
        return Ansi::cyan;
    case Group::Memory:
        return Ansi::blue;
    case Group::Arithmetic:
        return Ansi::yellow;
    case Group::Bitwise:
        return Ansi::magenta;
    case Group::Shift:
        return Ansi::green;
    case Group::Compare:
        return Ansi::white;
    case Group::Branch:
        return Ansi::red;
    case Group::CallStack:
        return Ansi::cyan;
    case Group::InterruptSystem:
        return Ansi::red;
    case Group::Special:
        return Ansi::green;
    case Group::Helper:
        return Ansi::magenta;
    case Group::Data:
        return Ansi::gray;
    }
    return Ansi::white;
}

std::size_t visibleLength(const std::string &text) {
    std::size_t length = 0;
    for (std::size_t i = 0; i < text.size();) {
        if (text[i] == '\x1b' && i + 1 < text.size() && text[i + 1] == '[') {
            i += 2;
            while (i < text.size() &&
                   !std::isalpha(static_cast<unsigned char>(text[i]))) {
                i++;
            }
            if (i < text.size()) {
                i++;
            }
            continue;
        }
        length++;
        i++;
    }
    return length;
}

std::string stripAnsi(const std::string &text) {
    std::string out;
    for (std::size_t i = 0; i < text.size();) {
        if (text[i] == '\x1b' && i + 1 < text.size() && text[i + 1] == '[') {
            i += 2;
            while (i < text.size() &&
                   !std::isalpha(static_cast<unsigned char>(text[i]))) {
                i++;
            }
            if (i < text.size()) {
                i++;
            }
            continue;
        }
        out += text[i++];
    }
    return out;
}

std::string lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](char c) {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    });
    return value;
}

std::string padRight(const std::string &text, std::size_t width) {
    std::size_t length = visibleLength(text);
    if (length >= width) {
        return text;
    }
    return text + std::string(width - length, ' ');
}

std::string fitAnsi(const std::string &text, int width) {
    if (width <= 0) {
        return {};
    }

    if (visibleLength(text) <= static_cast<std::size_t>(width)) {
        return text + std::string(static_cast<std::size_t>(width) -
                                      visibleLength(text),
                                  ' ');
    }

    std::string out;
    int visible = 0;
    int limit = std::max(0, width - 1);

    for (std::size_t i = 0; i < text.size() && visible < limit;) {
        if (text[i] == '\x1b' && i + 1 < text.size() && text[i + 1] == '[') {
            std::size_t start = i;
            i += 2;
            while (i < text.size() &&
                   !std::isalpha(static_cast<unsigned char>(text[i]))) {
                i++;
            }
            if (i < text.size()) {
                i++;
            }
            out += text.substr(start, i - start);
            continue;
        }
        out += text[i++];
        visible++;
    }

    out += Ansi::reset;
    out += "~";
    return out;
}

std::string highlightOperand(const std::string &operand, bool color) {
    if (!color) {
        return operand;
    }

    std::string out;
    for (std::size_t i = 0; i < operand.size();) {
        char c = operand[i];

        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            std::size_t start = i;
            while (i < operand.size() &&
                   (std::isalnum(static_cast<unsigned char>(operand[i])) ||
                    operand[i] == '_')) {
                i++;
            }
            std::string token = operand.substr(start, i - start);
            bool reg = token == "pc" || token == "sp" || token == "fp" ||
                       token == "fl" || token == "dp" || token == "ivt";
            if (!reg && token.size() >= 2 && token[0] == 'r' &&
                std::isdigit(static_cast<unsigned char>(token[1]))) {
                reg = true;
            }
            out += colorize(token, reg ? Ansi::green : Ansi::magenta, true);
            continue;
        }

        if (std::isdigit(static_cast<unsigned char>(c))) {
            std::size_t start = i;
            if (i + 1 < operand.size() && operand[i] == '0' &&
                (operand[i + 1] == 'x' || operand[i + 1] == 'X')) {
                i += 2;
                while (i < operand.size() &&
                       std::isxdigit(static_cast<unsigned char>(operand[i]))) {
                    i++;
                }
            } else {
                while (i < operand.size() &&
                       std::isdigit(static_cast<unsigned char>(operand[i]))) {
                    i++;
                }
            }
            out += colorize(operand.substr(start, i - start), Ansi::yellow, true);
            continue;
        }

        if (std::ispunct(static_cast<unsigned char>(c))) {
            out += colorize(std::string(1, c), Ansi::gray, true);
            i++;
            continue;
        }

        out += c;
        i++;
    }

    return out;
}

std::string joinOperands(const std::vector<std::string> &operands, bool color) {
    std::string out;
    for (std::size_t i = 0; i < operands.size(); i++) {
        if (i != 0) {
            out += colorize(", ", Ansi::gray, color);
        }
        out += highlightOperand(operands[i], color);
    }
    return out;
}

std::string renderLabel(const std::string &label, bool color) {
    return std::string(11, ' ') + colorize(label + ":", Ansi::bold + Ansi::magenta,
                                           color);
}

std::string renderLine(const DecodedLine &line,
                       const std::map<std::uint32_t, std::string> &labels,
                       bool color, bool showBytes) {
    std::vector<std::string> operands = line.operands;
    if (line.target.has_value()) {
        auto found = labels.find(*line.target);
        if (found != labels.end() && !operands.empty()) {
            operands[0] = found->second;
        }
    }

    std::string address =
        colorize(hexValue(line.address, 6), Ansi::bold + Ansi::gray, color);
    std::string bytes =
        showBytes ? colorize(padRight(byteList(line.bytes), 22), Ansi::dim, color)
                  : "";
    std::string mnemonic =
        colorize(padRight(line.mnemonic, 8), Ansi::bold + groupColor(line.group),
                 color);
    std::string assembly = mnemonic + joinOperands(operands, color);
    std::string result = address + "  ";

    if (showBytes) {
        result += bytes + "  ";
    }

    result += assembly;

    if (!line.comment.empty()) {
        result += "  " + colorize("; " + line.comment, Ansi::dim, color);
    }

    return result;
}

RenderedProgram renderProgram(const std::vector<DecodedLine> &decoded,
                              const std::string &path,
                              std::uint32_t baseAddress, std::size_t byteCount,
                              bool color, bool showBytes, bool labelsEnabled) {
    std::map<std::uint32_t, std::string> labels =
        labelsEnabled ? makeLabels(decoded, baseAddress, byteCount)
                      : std::map<std::uint32_t, std::string>{};

    RenderedProgram rendered;
    auto add = [&](const std::string &line) {
        rendered.colored.push_back(line);
        rendered.plain.push_back(stripAnsi(line));
    };

    add(colorize("E16 Disassembly", Ansi::bold + Ansi::cyan, color) + "  " +
        colorize(path, Ansi::dim, color) + "  " +
        colorize(std::to_string(byteCount) + " bytes", Ansi::dim, color) +
        "  base " + colorize(hexValue(baseAddress, 6), Ansi::yellow, color));
    add("");
    add(colorize("address     " +
                     std::string(showBytes ? "bytes                   " : "") +
                     "instruction",
                 Ansi::bold + Ansi::gray, color));

    for (const DecodedLine &line : decoded) {
        auto found = labels.find(line.address);
        if (found != labels.end()) {
            add(renderLabel(found->second, color));
        }
        add(renderLine(line, labels, color, showBytes));
    }

    return rendered;
}

std::vector<std::uint8_t> readBinaryFile(const std::string &path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("could not open " + path);
    }

    input.seekg(0, std::ios::end);
    std::streamoff size = input.tellg();
    input.seekg(0, std::ios::beg);

    if (size < 0) {
        throw std::runtime_error("could not read " + path);
    }

    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    if (!bytes.empty()) {
        input.read(reinterpret_cast<char *>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()));
    }

    return bytes;
}

void printUsage() {
    std::cout
        << "Usage: e16dis [--base address] [--plain] [--no-color] "
           "[--no-pager] [--no-labels] [--no-bytes] file.bin\n";
}

Options parseOptions(int argc, char **argv) {
    Options options;

    for (int i = 1; i < argc; i++) {
        std::string argument = argv[i];

        if (argument == "-h" || argument == "--help") {
            printUsage();
            std::exit(0);
        }

        if (argument == "-b" || argument == "--base") {
            if (i + 1 >= argc) {
                throw std::runtime_error(argument + " expects an address");
            }
            options.baseAddress = parseAddress(argv[++i]);
            continue;
        }

        if (argument == "--plain") {
            options.plain = true;
            options.color = false;
            options.pager = false;
            continue;
        }

        if (argument == "--no-color") {
            options.color = false;
            continue;
        }

        if (argument == "--color") {
            options.color = true;
            continue;
        }

        if (argument == "--no-pager") {
            options.pager = false;
            continue;
        }

        if (argument == "--pager") {
            options.pager = true;
            continue;
        }

        if (argument == "--no-labels") {
            options.labels = false;
            continue;
        }

        if (argument == "--no-bytes") {
            options.bytes = false;
            continue;
        }

        if (!argument.empty() && argument.front() == '-') {
            throw std::runtime_error("unknown option " + argument);
        }

        if (!options.inputPath.empty()) {
            throw std::runtime_error("only one input file can be provided");
        }

        options.inputPath = argument;
    }

    if (options.inputPath.empty()) {
        printUsage();
        std::exit(1);
    }

    return options;
}

TerminalSize terminalSize() {
    winsize size{};
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) == 0 && size.ws_col != 0 &&
        size.ws_row != 0) {
        return TerminalSize{static_cast<int>(size.ws_col),
                            static_cast<int>(size.ws_row)};
    }
    return {};
}

class TerminalSession {
  public:
    TerminalSession() {
        if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) {
            return;
        }

        if (tcgetattr(STDIN_FILENO, &original) != 0) {
            return;
        }

        termios raw = original;
        raw.c_lflag &= static_cast<tcflag_t>(~(ICANON | ECHO));
        raw.c_iflag &= static_cast<tcflag_t>(~(IXON | ICRNL));
        raw.c_cc[VMIN] = 1;
        raw.c_cc[VTIME] = 0;

        if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) != 0) {
            return;
        }

        active = true;
        std::cout << "\x1b[?1049h\x1b[?25l";
        std::cout.flush();
    }

    ~TerminalSession() {
        if (!active) {
            return;
        }
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &original);
        std::cout << "\x1b[?25h\x1b[?1049l";
        std::cout.flush();
    }

    bool isActive() const {
        return active;
    }

  private:
    termios original{};
    bool active = false;
};

bool readByteWithTimeout(char &c, int milliseconds) {
    fd_set set;
    FD_ZERO(&set);
    FD_SET(STDIN_FILENO, &set);

    timeval timeout{};
    timeout.tv_sec = milliseconds / 1000;
    timeout.tv_usec = (milliseconds % 1000) * 1000;

    int ready = select(STDIN_FILENO + 1, &set, nullptr, nullptr, &timeout);
    if (ready <= 0) {
        return false;
    }

    return read(STDIN_FILENO, &c, 1) == 1;
}

std::string readKey() {
    char c = '\0';
    if (read(STDIN_FILENO, &c, 1) != 1) {
        return {};
    }

    if (c != '\x1b') {
        if (c == '\r' || c == '\n') {
            return "enter";
        }
        if (c == 127 || c == '\b') {
            return "backspace";
        }
        return std::string(1, c);
    }

    char first = '\0';
    if (!readByteWithTimeout(first, 30)) {
        return "escape";
    }

    if (first != '[') {
        return "escape";
    }

    char second = '\0';
    if (!readByteWithTimeout(second, 30)) {
        return "escape";
    }

    switch (second) {
    case 'A':
        return "up";
    case 'B':
        return "down";
    case 'H':
        return "home";
    case 'F':
        return "end";
    case '5': {
        char ignored = '\0';
        readByteWithTimeout(ignored, 30);
        return "pageup";
    }
    case '6': {
        char ignored = '\0';
        readByteWithTimeout(ignored, 30);
        return "pagedown";
    }
    default:
        return "escape";
    }
}

class Pager {
  public:
    Pager(std::vector<std::string> coloredLines,
          std::vector<std::string> plainLines, std::string title)
        : colored(std::move(coloredLines)), plain(std::move(plainLines)),
          title(std::move(title)) {}

    void run() {
        TerminalSession session;
        if (!session.isActive()) {
            for (const std::string &line : plain) {
                std::cout << line << '\n';
            }
            return;
        }

        while (running) {
            draw();
            handleKey(readKey());
        }
    }

  private:
    std::vector<std::string> colored;
    std::vector<std::string> plain;
    std::string title;
    std::size_t top = 0;
    bool running = true;
    bool help = false;
    std::string search;
    std::string message;

    std::size_t maxTop(int viewportRows) const {
        if (colored.size() <= static_cast<std::size_t>(viewportRows)) {
            return 0;
        }
        return colored.size() - static_cast<std::size_t>(viewportRows);
    }

    void clampTop(int viewportRows) {
        top = std::min(top, maxTop(viewportRows));
    }

    void draw() {
        TerminalSize size = terminalSize();
        int viewportRows = std::max(1, size.rows - 2);
        clampTop(viewportRows);

        std::cout << "\x1b[H\x1b[2J";
        std::string header = " " + title + "  " + std::to_string(colored.size()) +
                             " lines ";
        std::cout << Ansi::inverse << fitAnsi(header, size.columns) << Ansi::reset;

        if (help) {
            drawHelp(size, viewportRows);
        } else {
            for (int row = 0; row < viewportRows; row++) {
                std::cout << "\x1b[" << (row + 2) << ";1H";
                std::size_t index = top + static_cast<std::size_t>(row);
                if (index < colored.size()) {
                    std::cout << fitAnsi(colored[index], size.columns);
                } else {
                    std::cout << std::string(size.columns, ' ');
                }
            }
        }

        drawStatus(size);
        std::cout.flush();
    }

    void drawHelp(const TerminalSize &size, int viewportRows) {
        std::vector<std::string> lines = {
            "E16 disassembler pager",
            "",
            "q or Esc      quit",
            "j/down        scroll down",
            "k/up          scroll up",
            "space/PgDn    next page",
            "b/PgUp        previous page",
            "g             first line",
            "G             last line",
            "/             search",
            "n             next search match",
            "N             previous search match",
            "h             toggle this help"};

        for (int row = 0; row < viewportRows; row++) {
            std::cout << "\x1b[" << (row + 2) << ";1H";
            if (row < static_cast<int>(lines.size())) {
                std::string line = row == 0 ? Ansi::bold + lines[row] + Ansi::reset
                                            : lines[row];
                std::cout << fitAnsi(line, size.columns);
            } else {
                std::cout << std::string(size.columns, ' ');
            }
        }
    }

    void drawStatus(const TerminalSize &size) {
        int viewportRows = std::max(1, size.rows - 2);
        std::size_t percent = colored.empty()
                                  ? 100
                                  : std::min<std::size_t>(
                                        100, ((top + viewportRows) * 100) /
                                                 std::max<std::size_t>(1,
                                                                      colored.size()));
        std::string status = message.empty()
                                 ? " q quit | arrows/jk scroll | space/b page | / search | h help | " +
                                       std::to_string(percent) + "%"
                                 : " " + message;
        std::cout << "\x1b[" << size.rows << ";1H" << Ansi::inverse
                  << fitAnsi(status, size.columns) << Ansi::reset;
        message.clear();
    }

    void handleKey(const std::string &key) {
        TerminalSize size = terminalSize();
        int viewportRows = std::max(1, size.rows - 2);
        std::size_t page = static_cast<std::size_t>(viewportRows);

        if (key == "q" || key == "escape") {
            running = false;
        } else if (key == "down" || key == "j" || key == "\x0e") {
            top = std::min(top + 1, maxTop(viewportRows));
        } else if (key == "up" || key == "k" || key == "\x10") {
            top = top == 0 ? 0 : top - 1;
        } else if (key == "pagedown" || key == " ") {
            top = std::min(top + page, maxTop(viewportRows));
        } else if (key == "pageup" || key == "b") {
            top = page > top ? 0 : top - page;
        } else if (key == "g" || key == "home") {
            top = 0;
        } else if (key == "G" || key == "end") {
            top = maxTop(viewportRows);
        } else if (key == "h") {
            help = !help;
        } else if (key == "/") {
            promptSearch();
        } else if (key == "n") {
            findNext(false);
        } else if (key == "N") {
            findNext(true);
        }
    }

    void promptSearch() {
        TerminalSize size = terminalSize();
        std::string query;

        while (true) {
            std::cout << "\x1b[" << size.rows << ";1H" << Ansi::inverse
                      << fitAnsi("/" + query, size.columns) << Ansi::reset;
            std::cout.flush();

            std::string key = readKey();
            if (key == "escape") {
                message = "search cancelled";
                return;
            }
            if (key == "enter") {
                search = query;
                findNext(false);
                return;
            }
            if (key == "backspace") {
                if (!query.empty()) {
                    query.pop_back();
                }
                continue;
            }
            if (key.size() == 1 &&
                std::isprint(static_cast<unsigned char>(key.front()))) {
                query += key.front();
            }
        }
    }

    void findNext(bool backwards) {
        if (search.empty()) {
            message = "no search";
            return;
        }

        std::string needle = lowercase(search);
        if (colored.empty()) {
            message = "empty";
            return;
        }

        std::size_t count = colored.size();
        for (std::size_t step = 1; step <= count; step++) {
            std::size_t index = backwards ? (top + count - step) % count
                                          : (top + step) % count;
            if (lowercase(plain[index]).find(needle) != std::string::npos) {
                top = index;
                message = "match " + std::to_string(index + 1) + "/" +
                          std::to_string(count);
                return;
            }
        }

        message = "not found: " + search;
    }
};

void printLines(const std::vector<std::string> &lines) {
    for (const std::string &line : lines) {
        std::cout << line << '\n';
    }
}
}

int main(int argc, char **argv) {
    try {
        Options options = parseOptions(argc, argv);
        std::vector<std::uint8_t> bytes = readBinaryFile(options.inputPath);
        std::vector<DecodedLine> decoded =
            decodeProgram(bytes, options.baseAddress);

        bool useColor = options.color && isatty(STDOUT_FILENO);
        RenderedProgram rendered =
            renderProgram(decoded, options.inputPath, options.baseAddress,
                          bytes.size(), useColor, options.bytes, options.labels);

        bool usePager = options.pager && isatty(STDIN_FILENO) &&
                        isatty(STDOUT_FILENO) && !options.plain;

        if (usePager) {
            Pager pager(rendered.colored, rendered.plain,
                        "E16 Disassembly - " + options.inputPath);
            pager.run();
        } else {
            printLines(useColor ? rendered.colored : rendered.plain);
        }
    } catch (const std::exception &error) {
        std::cerr << "e16dis: " << error.what() << '\n';
        return 1;
    }

    return 0;
}

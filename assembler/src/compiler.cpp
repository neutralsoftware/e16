#include "compiler.h"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>

namespace {
[[noreturn]] void fail(std::size_t line, const std::string &message) {
    throw std::runtime_error("line " + std::to_string(line) + ": " + message);
}

std::string decodeString(const std::string &literal, std::size_t line);

bool isStringLiteral(const std::string &value) {
    return value.length() >= 2 &&
           ((value.front() == '"' && value.back() == '"') ||
            (value.front() == '\'' && value.back() == '\''));
}

std::string unquotePath(const std::string &value, std::size_t line) {
    if (!isStringLiteral(value)) {
        fail(line, "Expected a file path string literal.");
    }
    return decodeString(value, line);
}

std::filesystem::path resolveDataPath(const Directive &directive) {
    std::filesystem::path path(unquotePath(directive.arguments[0],
                                           directive.line));
    if (path.is_absolute()) {
        return path;
    }
    if (!directive.sourcePath.empty()) {
        std::filesystem::path source(directive.sourcePath);
        if (source.has_parent_path()) {
            return source.parent_path() / path;
        }
    }
    return std::filesystem::current_path() / path;
}

std::vector<std::uint8_t> readBinaryFile(const std::filesystem::path &path,
                                         std::size_t line) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        fail(line, "Could not open binary file " + path.string() + ".");
    }
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(input),
                                     std::istreambuf_iterator<char>());
}

std::string decodeString(const std::string &literal, std::size_t line) {
    if (!isStringLiteral(literal)) {
        fail(line, "Expected a string literal.");
    }

    std::string decoded;
    for (size_t i = 1; i + 1 < literal.length(); i++) {
        char c = literal[i];
        if (c != '\\') {
            decoded += c;
            continue;
        }

        if (i + 2 >= literal.length()) {
            fail(line, "Bad escape sequence in string literal.");
        }

        i++;
        switch (literal[i]) {
        case '0':
            decoded += '\0';
            break;
        case 'n':
            decoded += '\n';
            break;
        case 'r':
            decoded += '\r';
            break;
        case 't':
            decoded += '\t';
            break;
        case '\\':
            decoded += '\\';
            break;
        case '"':
            decoded += '"';
            break;
        case '\'':
            decoded += '\'';
            break;
        default:
            fail(line, "Unknown escape sequence in string literal.");
        }
    }

    return decoded;
}

bool isGeneralRegister(const std::string &value) {
    if (!value.starts_with("r") || value.length() < 2 || value.length() > 3) {
        return false;
    }

    for (size_t i = 1; i < value.length(); i++) {
        if (!std::isdigit(static_cast<unsigned char>(value[i]))) {
            return false;
        }
    }

    int id = std::stoi(value.substr(1));
    return id >= 0 && id < 16;
}

bool isSpecialRegister(const std::string &value) {
    return value == "pc" || value == "sp" || value == "fp" || value == "fl" ||
           value == "dp" || value == "ivt";
}

std::uint8_t generalRegisterId(const std::string &value, std::size_t line) {
    if (!isGeneralRegister(value)) {
        fail(line, "Expected a general register, got " + value + ".");
    }

    return static_cast<std::uint8_t>(std::stoi(value.substr(1)));
}

std::uint8_t registerId(const std::string &value, std::size_t line) {
    if (isGeneralRegister(value)) {
        return generalRegisterId(value, line);
    }

    if (value == "pc") {
        return 0x10;
    }
    if (value == "sp") {
        return 0x11;
    }
    if (value == "fp") {
        return 0x12;
    }
    if (value == "fl") {
        return 0x13;
    }
    if (value == "dp") {
        return 0x14;
    }
    if (value == "ivt") {
        return 0x15;
    }

    fail(line, "Expected a register, got " + value + ".");
}

std::uint8_t packedRegisters(const std::string &left, const std::string &right,
                             std::size_t line) {
    return static_cast<std::uint8_t>((generalRegisterId(left, line) << 4) |
                                     generalRegisterId(right, line));
}

std::uint8_t modeId(AddressingMode mode, std::size_t line) {
    switch (mode) {
    case AddressingMode::Immediate:
        return 0x0;
    case AddressingMode::Register:
        return 0x1;
    case AddressingMode::Absolute:
        return 0x2;
    case AddressingMode::RegisterIndirect:
        return 0x3;
    case AddressingMode::Indexed:
        return 0x4;
    case AddressingMode::BasePlusOffset:
        return 0x5;
    case AddressingMode::DirectPageOffset:
        return 0x6;
    case AddressingMode::StackRelative:
        return 0x7;
    case AddressingMode::Label:
        fail(line, "Labels must be resolved before writing a mode byte.");
    }
    fail(line, "Unknown addressing mode.");
}

bool isLoad(const std::string &opcode) {
    return opcode == "load" || opcode == "loadb" || opcode == "loadw" ||
           opcode == "loadsb";
}

bool isStore(const std::string &opcode) {
    return opcode == "store" || opcode == "storeb" || opcode == "storew";
}

bool isBranch(const std::string &opcode) {
    return opcode == "jmp" || opcode == "bra" || opcode == "beq" ||
           opcode == "bne" || opcode == "bcs" || opcode == "bcc" ||
           opcode == "bmi" || opcode == "bpl" || opcode == "bvs" ||
           opcode == "bvc" || opcode == "bgt" || opcode == "bge" ||
           opcode == "blt" || opcode == "ble" || opcode == "bhi" ||
           opcode == "bls";
}

bool isNoOperandOpcode(const std::string &opcode) {
    return opcode == "nop" || opcode == "ret" || opcode == "leave" ||
           opcode == "pushf" || opcode == "popf" || opcode == "pusha" ||
           opcode == "popa" || opcode == "iret" || opcode == "ei" ||
           opcode == "di" || opcode == "wait" || opcode == "halt" ||
           opcode == "reset" || opcode == "trap" || opcode == "dma";
}

bool isMemoryMode(AddressingMode mode) {
    return mode == AddressingMode::Absolute ||
           mode == AddressingMode::RegisterIndirect ||
           mode == AddressingMode::Indexed ||
           mode == AddressingMode::BasePlusOffset ||
           mode == AddressingMode::DirectPageOffset ||
           mode == AddressingMode::StackRelative;
}

std::uint8_t opcodeFor(const std::string &opcode, std::size_t line) {
    for (const auto &definition : Dictionary::all()) {
        if (definition->name == opcode) {
            return static_cast<std::uint8_t>(definition->opcode);
        }
    }

    fail(line, "Unknown opcode " + opcode + ".");
}

std::size_t memoryOperandSize(AddressingMode mode, std::size_t line,
                              bool hasRegister) {
    switch (mode) {
    case AddressingMode::Absolute:
        return hasRegister ? 6 : 5;
    case AddressingMode::RegisterIndirect:
        return hasRegister ? 3 : 3;
    case AddressingMode::Indexed:
        return hasRegister ? 7 : 6;
    case AddressingMode::BasePlusOffset:
        return hasRegister ? 6 : 5;
    case AddressingMode::DirectPageOffset:
    case AddressingMode::StackRelative:
        return hasRegister ? 5 : 4;
    default:
        fail(line, "Expected a memory addressing mode.");
    }
}

void emit8(std::vector<std::uint8_t> &bytes, std::uint8_t value) {
    bytes.push_back(value);
}

void emit16(std::vector<std::uint8_t> &bytes, int value) {
    std::uint16_t encoded = static_cast<std::uint16_t>(value);
    bytes.push_back(static_cast<std::uint8_t>(encoded & 0xFF));
    bytes.push_back(static_cast<std::uint8_t>((encoded >> 8) & 0xFF));
}

void emit24(std::vector<std::uint8_t> &bytes, std::uint32_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value & 0xFF));
    bytes.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
    bytes.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFF));
}

int checkedSigned16(int value, std::size_t line, const std::string &what) {
    if (value < -32768 || value > 32767) {
        fail(line, what + " does not fit in signed 16 bits.");
    }
    return value;
}

int checkedWord(int value, std::size_t line, const std::string &what) {
    if (value < -32768 || value > 0xFFFF) {
        fail(line, what + " does not fit in 16 bits.");
    }
    return value;
}

std::uint8_t checkedByte(int value, std::size_t line, const std::string &what) {
    if (value < 0 || value > 0xFF) {
        fail(line, what + " does not fit in one byte.");
    }
    return static_cast<std::uint8_t>(value);
}

std::uint32_t checkedAddress(int value, std::size_t line,
                             const std::string &what) {
    if (value < 0 || value > 0xFFFFFF) {
        fail(line, what + " does not fit in 24 bits.");
    }
    return static_cast<std::uint32_t>(value);
}
}

Compiler::Compiler(Parser &parser, std::uint32_t baseAddress)
    : parser(parser), baseAddress(baseAddress) {}

std::vector<std::uint8_t> Compiler::compile() {
    collectConstants();
    layout();

    std::vector<std::uint8_t> bytes;
    for (const auto &expression : parser.expressions) {
        if (expression->type == ExpressionType::Directive) {
            emitDirective(*dynamic_cast<Directive *>(expression.get()), bytes);
        } else if (expression->type == ExpressionType::Instruction) {
            emitInstruction(*dynamic_cast<Instruction *>(expression.get()),
                            bytes);
        }
    }

    return bytes;
}

void Compiler::writeBinary(const std::string &path) {
    std::vector<std::uint8_t> bytes = compile();
    std::ofstream output(path, std::ios::binary);

    if (!output) {
        throw std::runtime_error("Could not open output file " + path + ".");
    }

    output.write(reinterpret_cast<const char *>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
}

void Compiler::collectConstants() {
    constants.clear();

    for (const auto &expression : parser.expressions) {
        if (expression->type != ExpressionType::Directive) {
            continue;
        }

        const auto *directive = dynamic_cast<Directive *>(expression.get());
        if (directive->name != ".const" && directive->name != ".constant") {
            continue;
        }

        if (directive->arguments.size() != 2) {
            fail(directive->line,
                 "A constant directive must have exactly two arguments.");
        }

        if (constants.contains(directive->arguments[0])) {
            fail(directive->line, "Constant " + directive->arguments[0] +
                                      " is already "
                                      "defined.");
        }

        try {
            constants[directive->arguments[0]] =
                utils::parseNumber(directive->arguments[1]);
        } catch (...) {
            fail(directive->line, "Constant " + directive->arguments[0] +
                                      " must resolve to a number.");
        }
    }
}

void Compiler::layout() {
    symbols.clear();
    addresses.clear();
    sizes.clear();

    std::uint32_t pc = baseAddress;

    for (const auto &expression : parser.expressions) {
        addresses[expression.get()] = pc;

        if (expression->type == ExpressionType::Label) {
            const auto *label = dynamic_cast<Label *>(expression.get());
            if (symbols.contains(label->name)) {
                fail(label->line,
                     "Label " + label->name + " is already defined.");
            }
            symbols[label->name] = pc;
            sizes[expression.get()] = 0;
            continue;
        }

        if (expression->type == ExpressionType::Directive) {
            const auto *directive = dynamic_cast<Directive *>(expression.get());
            std::size_t size = directiveSize(*directive);
            sizes[expression.get()] = size;
            if (size > 0x1000000 - pc) {
                fail(directive->line,
                     "Output would exceed the 24-bit address space.");
            }
            pc += static_cast<std::uint32_t>(size);
            continue;
        }

        if (expression->type == ExpressionType::Instruction) {
            const auto *instruction =
                dynamic_cast<Instruction *>(expression.get());
            std::size_t size = instructionSize(*instruction);
            sizes[expression.get()] = size;
            if (size > 0x1000000 - pc) {
                fail(instruction->line,
                     "Output would exceed the 24-bit address space.");
            }
            pc += static_cast<std::uint32_t>(size);
        }
    }
}

std::size_t Compiler::directiveSize(const Directive &directive) const {
    if (directive.name == ".const" || directive.name == ".constant" ||
        directive.name == ".include") {
        return 0;
    }

    auto resolve = [&](const std::string &value) -> int {
        if (constants.contains(value)) {
            return constants.at(value);
        }
        try {
            return utils::parseNumber(value);
        } catch (...) {
            fail(directive.line, "Could not resolve " + value + ".");
        }
    };

    if (directive.name == ".string") {
        std::size_t size = 0;
        for (const std::string &argument : directive.arguments) {
            size += decodeString(argument, directive.line).size();
        }
        return size;
    }

    if (directive.name == ".data" || directive.name == ".byte") {
        std::size_t size = 0;
        for (const std::string &argument : directive.arguments) {
            if (isStringLiteral(argument)) {
                size += decodeString(argument, directive.line).size();
            } else {
                size++;
            }
        }
        return size;
    }

    if (directive.name == ".word") {
        return directive.arguments.size() * 2;
    }

    if (directive.name == ".addr24") {
        return directive.arguments.size() * 3;
    }

    if (directive.name == ".bin") {
        if (directive.arguments.empty() || directive.arguments.size() > 3) {
            fail(directive.line,
                 ".bin expects a file path and optional offset/count.");
        }
        std::vector<std::uint8_t> data =
            readBinaryFile(resolveDataPath(directive), directive.line);
        std::size_t offset = 0;
        if (directive.arguments.size() >= 2) {
            int parsed = resolve(directive.arguments[1]);
            if (parsed < 0) {
                fail(directive.line, ".bin offset cannot be negative.");
            }
            offset = static_cast<std::size_t>(parsed);
        }
        if (offset > data.size()) {
            fail(directive.line, ".bin offset is past end of file.");
        }
        if (directive.arguments.size() == 3) {
            int parsed = resolve(directive.arguments[2]);
            if (parsed < 0) {
                fail(directive.line, ".bin count cannot be negative.");
            }
            std::size_t count = static_cast<std::size_t>(parsed);
            if (count > data.size() - offset) {
                fail(directive.line, ".bin range exceeds file size.");
            }
            return count;
        }
        return data.size() - offset;
    }

    fail(directive.line, "Directive " + directive.name + " cannot emit data.");
}

std::size_t Compiler::instructionSize(const Instruction &instruction) const {
    const auto &ops = instruction.parsedOperands;

    if (isNoOperandOpcode(instruction.opcode)) {
        if (!ops.empty()) {
            fail(instruction.line, "Instruction " + instruction.opcode +
                                       " does not accept operands.");
        }
        return 1;
    }

    if (isBranch(instruction.opcode)) {
        if (ops.size() != 1) {
            fail(instruction.line,
                 "Branch instruction expects exactly one operand.");
        }
        return 3;
    }

    if (instruction.opcode == "call") {
        if (ops.size() != 1) {
            fail(instruction.line, "call expects exactly one operand.");
        }
        return 4;
    }

    if (instruction.opcode == "int") {
        if (ops.size() != 1) {
            fail(instruction.line, "int expects exactly one operand.");
        }
        return 2;
    }

    if (instruction.opcode == "enter") {
        if (ops.size() != 1) {
            fail(instruction.line, "enter expects exactly one operand.");
        }
        return 3;
    }

    if (instruction.opcode == "get") {
        return 3;
    }

    if (instruction.opcode == "set") {
        if (ops.size() != 2) {
            fail(instruction.line, "set expects exactly two operands.");
        }
        if (ops[1].mode == AddressingMode::Immediate ||
            ops[1].mode == AddressingMode::Label) {
            return 6;
        }
        if (ops[1].mode == AddressingMode::Register) {
            return 4;
        }
        fail(instruction.line, "set source must be a register or immediate.");
    }

    if (isLoad(instruction.opcode) || instruction.opcode == "addr") {
        if (ops.size() != 2 || ops[0].mode != AddressingMode::Register) {
            fail(instruction.line,
                 instruction.opcode +
                     " expects a destination register and one source operand.");
        }

        if (instruction.opcode == "addr") {
            if (ops[1].mode == AddressingMode::Absolute ||
                ops[1].mode == AddressingMode::Label) {
                return 6;
            }
            fail(instruction.line, "addr expects a label or absolute address.");
        }

        if (!isMemoryMode(ops[1].mode)) {
            fail(instruction.line,
                 instruction.opcode + " source must be a memory operand.");
        }

        return memoryOperandSize(ops[1].mode, instruction.line, true);
    }

    if (isStore(instruction.opcode)) {
        if (ops.size() != 2 || ops[1].mode != AddressingMode::Register ||
            !isMemoryMode(ops[0].mode)) {
            fail(instruction.line,
                 instruction.opcode +
                     " expects a memory destination and source register.");
        }

        return memoryOperandSize(ops[0].mode, instruction.line, true);
    }

    if (ops.size() == 1) {
        if (ops[0].mode == AddressingMode::Register) {
            return 2;
        }

        if (ops[0].mode == AddressingMode::Immediate) {
            return 4;
        }

        if (isMemoryMode(ops[0].mode)) {
            return memoryOperandSize(ops[0].mode, instruction.line, false);
        }

        fail(instruction.line, "Instruction " + instruction.opcode +
                                   " cannot use this operand form.");
    }

    if (ops.size() == 2) {
        if (ops[0].mode == AddressingMode::Register &&
            ops[1].mode == AddressingMode::Register) {
            return 2;
        }

        if (ops[0].mode == AddressingMode::Register &&
            ops[1].mode == AddressingMode::Immediate) {
            return 5;
        }

        if (ops[0].mode == AddressingMode::Register &&
            isMemoryMode(ops[1].mode)) {
            return memoryOperandSize(ops[1].mode, instruction.line, true);
        }

        if (isMemoryMode(ops[0].mode) &&
            ops[1].mode == AddressingMode::Register) {
            return memoryOperandSize(ops[0].mode, instruction.line, true);
        }

        fail(instruction.line, "Instruction " + instruction.opcode +
                                   " cannot use this operand form.");
    }

    fail(instruction.line,
         "Instruction " + instruction.opcode + " has unsupported operands.");
}

void Compiler::emitDirective(const Directive &directive,
                             std::vector<std::uint8_t> &bytes) const {
    auto resolve = [&](const std::string &value) -> int {
        if (constants.contains(value)) {
            return constants.at(value);
        }
        if (symbols.contains(value)) {
            return static_cast<int>(symbols.at(value));
        }
        try {
            return utils::parseNumber(value);
        } catch (...) {
            fail(directive.line, "Could not resolve " + value + ".");
        }
    };

    if (directive.name == ".const" || directive.name == ".constant" ||
        directive.name == ".include") {
        return;
    }

    if (directive.name == ".string") {
        for (const std::string &argument : directive.arguments) {
            std::string decoded = decodeString(argument, directive.line);
            bytes.insert(bytes.end(), decoded.begin(), decoded.end());
        }
        return;
    }

    if (directive.name == ".data" || directive.name == ".byte") {
        for (const std::string &argument : directive.arguments) {
            if (isStringLiteral(argument)) {
                std::string decoded = decodeString(argument, directive.line);
                bytes.insert(bytes.end(), decoded.begin(), decoded.end());
            } else {
                emit8(bytes,
                      checkedByte(resolve(argument), directive.line, argument));
            }
        }
        return;
    }

    if (directive.name == ".word") {
        for (const std::string &argument : directive.arguments) {
            emit16(bytes,
                   checkedWord(resolve(argument), directive.line, argument));
        }
        return;
    }

    if (directive.name == ".addr24") {
        for (const std::string &argument : directive.arguments) {
            emit24(bytes,
                   checkedAddress(resolve(argument), directive.line, argument));
        }
        return;
    }

    if (directive.name == ".bin") {
        if (directive.arguments.empty() || directive.arguments.size() > 3) {
            fail(directive.line,
                 ".bin expects a file path and optional offset/count.");
        }
        std::vector<std::uint8_t> data =
            readBinaryFile(resolveDataPath(directive), directive.line);
        std::size_t offset = 0;
        if (directive.arguments.size() >= 2) {
            int parsed = resolve(directive.arguments[1]);
            if (parsed < 0) {
                fail(directive.line, ".bin offset cannot be negative.");
            }
            offset = static_cast<std::size_t>(parsed);
        }
        if (offset > data.size()) {
            fail(directive.line, ".bin offset is past end of file.");
        }
        std::size_t count = data.size() - offset;
        if (directive.arguments.size() == 3) {
            int parsed = resolve(directive.arguments[2]);
            if (parsed < 0) {
                fail(directive.line, ".bin count cannot be negative.");
            }
            count = static_cast<std::size_t>(parsed);
            if (count > data.size() - offset) {
                fail(directive.line, ".bin range exceeds file size.");
            }
        }
        bytes.insert(bytes.end(), data.begin() + static_cast<std::ptrdiff_t>(offset),
                     data.begin() + static_cast<std::ptrdiff_t>(offset + count));
        return;
    }

    fail(directive.line, "Directive " + directive.name + " cannot emit data.");
}

void Compiler::emitInstruction(const Instruction &instruction,
                               std::vector<std::uint8_t> &bytes) const {
    auto resolve = [&](const InstructionOperand &operand) -> int {
        if (operand.mode == AddressingMode::Immediate ||
            operand.mode == AddressingMode::Absolute ||
            operand.mode == AddressingMode::Indexed ||
            operand.mode == AddressingMode::Label) {
            if (constants.contains(operand.base)) {
                return constants.at(operand.base);
            }
            if (symbols.contains(operand.base)) {
                return static_cast<int>(symbols.at(operand.base));
            }
            try {
                return utils::parseNumber(operand.base);
            } catch (...) {
                fail(instruction.line,
                     "Could not resolve symbol " + operand.base + ".");
            }
        }

        fail(instruction.line, "Operand does not resolve to a number.");
    };

    auto emitMemoryWithRegister = [&](const InstructionOperand &memory,
                                      const InstructionOperand &reg) {
        emit8(bytes, modeId(memory.mode, instruction.line));

        if (memory.mode == AddressingMode::Absolute) {
            emit8(bytes, generalRegisterId(reg.base, instruction.line));
            emit24(bytes, checkedAddress(resolve(memory), instruction.line,
                                         memory.base));
            return;
        }

        if (memory.mode == AddressingMode::RegisterIndirect) {
            emit8(bytes,
                  packedRegisters(reg.base, memory.base, instruction.line));
            return;
        }

        if (memory.mode == AddressingMode::Indexed) {
            emit8(bytes, generalRegisterId(reg.base, instruction.line));
            emit24(bytes, checkedAddress(resolve(memory), instruction.line,
                                         memory.base));
            emit8(bytes, generalRegisterId(memory.offsetReg, instruction.line));
            return;
        }

        if (memory.mode == AddressingMode::BasePlusOffset) {
            emit8(bytes, generalRegisterId(reg.base, instruction.line));
            emit8(bytes, registerId(memory.base, instruction.line));
            emit16(bytes,
                   checkedSigned16(memory.offset, instruction.line, "Offset"));
            return;
        }

        if (memory.mode == AddressingMode::DirectPageOffset ||
            memory.mode == AddressingMode::StackRelative) {
            emit8(bytes, generalRegisterId(reg.base, instruction.line));
            emit16(bytes,
                   checkedWord(memory.offset, instruction.line, "Offset"));
            return;
        }

        fail(instruction.line, "Expected a memory operand.");
    };

    auto emitUnaryMemory = [&](const InstructionOperand &operand) {
        emit8(bytes, modeId(operand.mode, instruction.line));

        if (operand.mode == AddressingMode::Absolute) {
            emit24(bytes, checkedAddress(resolve(operand), instruction.line,
                                         operand.base));
            return;
        }

        if (operand.mode == AddressingMode::RegisterIndirect) {
            emit8(bytes, generalRegisterId(operand.base, instruction.line));
            return;
        }

        if (operand.mode == AddressingMode::Indexed) {
            emit24(bytes, checkedAddress(resolve(operand), instruction.line,
                                         operand.base));
            emit8(bytes,
                  generalRegisterId(operand.offsetReg, instruction.line));
            return;
        }

        if (operand.mode == AddressingMode::BasePlusOffset) {
            emit8(bytes, registerId(operand.base, instruction.line));
            emit16(bytes,
                   checkedSigned16(operand.offset, instruction.line, "Offset"));
            return;
        }

        if (operand.mode == AddressingMode::DirectPageOffset ||
            operand.mode == AddressingMode::StackRelative) {
            emit16(bytes,
                   checkedWord(operand.offset, instruction.line, "Offset"));
            return;
        }

        fail(instruction.line, "Expected a memory operand.");
    };

    std::uint8_t opcode = opcodeFor(instruction.opcode, instruction.line);
    const auto &ops = instruction.parsedOperands;
    emit8(bytes, opcode);

    if (isNoOperandOpcode(instruction.opcode)) {
        return;
    }

    if (isBranch(instruction.opcode)) {
        int target = resolve(ops[0]);
        int current = static_cast<int>(addresses.at(&instruction));
        int size = static_cast<int>(sizes.at(&instruction));
        int relative = target - (current + size);
        emit16(bytes,
               checkedSigned16(relative, instruction.line, "Branch offset"));
        return;
    }

    if (instruction.opcode == "call") {
        emit24(bytes,
               checkedAddress(resolve(ops[0]), instruction.line, ops[0].base));
        return;
    }

    if (instruction.opcode == "int") {
        emit8(bytes,
              checkedByte(resolve(ops[0]), instruction.line, ops[0].base));
        return;
    }

    if (instruction.opcode == "enter") {
        emit16(bytes,
               checkedWord(resolve(ops[0]), instruction.line, ops[0].base));
        return;
    }

    if (instruction.opcode == "get") {
        emit8(bytes, generalRegisterId(ops[0].base, instruction.line));
        emit8(bytes, registerId(ops[1].base, instruction.line));
        return;
    }

    if (instruction.opcode == "set") {
        emit8(bytes, modeId(ops[1].mode, instruction.line));
        emit8(bytes, registerId(ops[0].base, instruction.line));
        if (ops[1].mode == AddressingMode::Register) {
            emit8(bytes, generalRegisterId(ops[1].base, instruction.line));
            return;
        }
        emit24(bytes,
               checkedAddress(resolve(ops[1]), instruction.line, ops[1].base));
        return;
    }

    if (isLoad(instruction.opcode)) {
        emitMemoryWithRegister(ops[1], ops[0]);
        return;
    }

    if (isStore(instruction.opcode)) {
        emitMemoryWithRegister(ops[0], ops[1]);
        return;
    }

    if (instruction.opcode == "addr") {
        emit8(bytes, modeId(AddressingMode::Absolute, instruction.line));
        emit8(bytes, generalRegisterId(ops[0].base, instruction.line));
        emit24(bytes,
               checkedAddress(resolve(ops[1]), instruction.line, ops[1].base));
        return;
    }

    if (ops.size() == 1) {
        if (ops[0].mode == AddressingMode::Register) {
            emit8(bytes, generalRegisterId(ops[0].base, instruction.line));
            return;
        }

        if (ops[0].mode == AddressingMode::Immediate) {
            emit8(bytes, modeId(AddressingMode::Immediate, instruction.line));
            emit16(bytes,
                   checkedWord(resolve(ops[0]), instruction.line, ops[0].base));
            return;
        }

        if (isMemoryMode(ops[0].mode)) {
            emitUnaryMemory(ops[0]);
            return;
        }
    }

    if (ops.size() == 2) {
        if (ops[0].mode == AddressingMode::Register &&
            ops[1].mode == AddressingMode::Register) {
            emit8(bytes,
                  packedRegisters(ops[0].base, ops[1].base, instruction.line));
            return;
        }

        if (ops[0].mode == AddressingMode::Register &&
            ops[1].mode == AddressingMode::Immediate) {
            emit8(bytes, modeId(AddressingMode::Immediate, instruction.line));
            emit8(bytes, generalRegisterId(ops[0].base, instruction.line));
            emit16(bytes,
                   checkedWord(resolve(ops[1]), instruction.line, ops[1].base));
            return;
        }

        if (ops[0].mode == AddressingMode::Register &&
            isMemoryMode(ops[1].mode)) {
            emitMemoryWithRegister(ops[1], ops[0]);
            return;
        }

        if (isMemoryMode(ops[0].mode) &&
            ops[1].mode == AddressingMode::Register) {
            emitMemoryWithRegister(ops[0], ops[1]);
            return;
        }
    }

    fail(instruction.line,
         "Instruction " + instruction.opcode + " cannot be encoded.");
}

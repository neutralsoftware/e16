#include <parser.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace {
bool isSpecialRegister(const std::string &val) {
    return val == "pc" || val == "sp" || val == "fp" || val == "fl" ||
           val == "dp" || val == "ivt";
}

bool isGeneralRegister(const std::string &val) {
    if (!val.starts_with("r") || val.length() < 2 || val.length() > 3) {
        return false;
    }

    for (size_t i = 1; i < val.length(); i++) {
        if (!std::isdigit(static_cast<unsigned char>(val[i]))) {
            return false;
        }
    }

    int registerNum = std::stoi(val.substr(1));
    return registerNum >= 0 && registerNum < 16;
}

bool isIdentifier(const std::string &val) {
    if (val.empty()) {
        return false;
    }

    unsigned char first = static_cast<unsigned char>(val[0]);
    if (!std::isalpha(first) && val[0] != '_' && val[0] != '.') {
        return false;
    }

    for (char c : val) {
        unsigned char ch = static_cast<unsigned char>(c);
        if (!std::isalnum(ch) && c != '_' && c != '.') {
            return false;
        }
    }

    return true;
}

size_t findOffsetOperator(const std::string &val) {
    for (size_t i = 1; i < val.size(); i++) {
        if (val[i] == '+' || val[i] == '-') {
            return i;
        }
    }

    return std::string::npos;
}

std::string unquote(const std::string &val) {
    std::string trimmed = utils::trim(val);
    if (trimmed.length() >= 2 &&
        ((trimmed.front() == '"' && trimmed.back() == '"') ||
         (trimmed.front() == '\'' && trimmed.back() == '\''))) {
        return trimmed.substr(1, trimmed.length() - 2);
    }

    return trimmed;
}

std::filesystem::path includeBase(const std::string &path) {
    if (path.empty()) {
        return std::filesystem::current_path();
    }

    std::filesystem::path source(path);
    if (source.has_parent_path()) {
        return source.parent_path();
    }

    return std::filesystem::current_path();
}

std::string readTextFile(const std::filesystem::path &path) {
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("Could not open include file " +
                                 path.string() + ".");
    }

    std::string contents;
    std::string line;
    while (std::getline(file, line)) {
        contents += line;
        contents += '\n';
    }

    return contents;
}

std::unordered_map<std::string, int>
collectParserConstants(const std::vector<std::unique_ptr<Expression>> &expressions) {
    std::unordered_map<std::string, int> constants;

    for (const auto &expression : expressions) {
        if (expression->type != ExpressionType::Directive) {
            continue;
        }

        const auto *directive = dynamic_cast<Directive *>(expression.get());
        if (directive->name != ".const" && directive->name != ".constant") {
            continue;
        }

        if (directive->arguments.size() != 2) {
            continue;
        }

        try {
            constants[directive->arguments[0]] =
                utils::parseNumber(directive->arguments[1]);
        } catch (...) {
        }
    }

    return constants;
}

bool parseConstTerm(const std::string &term,
                    const std::unordered_map<std::string, int> &constants,
                    int &value) {
    std::string trimmed = utils::trim(term);
    if (trimmed.empty()) {
        return false;
    }

    if (constants.contains(trimmed)) {
        value = constants.at(trimmed);
        return true;
    }

    try {
        value = utils::parseNumber(trimmed);
        return true;
    } catch (...) {
        return false;
    }
}

bool resolveConstExpression(const std::string &text,
                            const std::unordered_map<std::string, int> &constants,
                            int &value) {
    std::string expression = utils::trim(text);
    if (expression.empty()) {
        return false;
    }

    int total = 0;
    int sign = 1;
    std::string current;
    bool sawTerm = false;

    for (size_t i = 0; i <= expression.size(); i++) {
        bool atEnd = i == expression.size();
        char c = atEnd ? '\0' : expression[i];
        bool split = atEnd || c == '+' || c == '-';

        if (!split || (current.empty() && (c == '+' || c == '-'))) {
            current += c;
            continue;
        }

        int term = 0;
        if (!parseConstTerm(current, constants, term)) {
            return false;
        }

        total += sign * term;
        sawTerm = true;
        current.clear();
        sign = c == '-' ? -1 : 1;
    }

    if (!sawTerm) {
        return false;
    }

    value = total;
    return true;
}

std::string normalizeNumber(int value) {
    return std::to_string(value);
}
}

std::vector<std::string> utils::split(std::string str, char separator) {
    std::vector<std::string> parts;

    size_t start = 0;
    while (true) {
        size_t end = str.find(separator, start);

        if (end == std::string::npos) {
            parts.emplace_back(str.substr(start));
            break;
        }

        parts.emplace_back(str.substr(start, end - start));
        start = end + 1;
    }

    return parts;
}

std::vector<std::string> utils::splitArguments(const std::string &str) {
    std::vector<std::string> parts;
    std::string current;
    bool inString = false;
    bool escaped = false;
    char quote = '\0';
    int parenDepth = 0;

    for (char c : str) {
        if (escaped) {
            current += c;
            escaped = false;
            continue;
        }

        if (inString && c == '\\') {
            current += c;
            escaped = true;
            continue;
        }

        if ((c == '"' || c == '\'') && !inString) {
            inString = true;
            quote = c;
            current += c;
            continue;
        }

        if (inString && c == quote) {
            inString = false;
            current += c;
            continue;
        }

        if (!inString && c == '(') {
            parenDepth++;
        } else if (!inString && c == ')' && parenDepth > 0) {
            parenDepth--;
        }

        if (!inString && parenDepth == 0 && c == ',') {
            std::string argument = trim(current);
            if (!argument.empty()) {
                parts.push_back(argument);
            }
            current.clear();
            continue;
        }

        current += c;
    }

    std::string argument = trim(current);
    if (!argument.empty()) {
        parts.push_back(argument);
    }

    return parts;
}

std::string utils::trim(std::string str) {
    while (!str.empty() &&
           std::isspace(static_cast<unsigned char>(str.front())))
        str = str.substr(1);

    while (!str.empty() && std::isspace(static_cast<unsigned char>(str.back())))
        str = str.substr(0, str.length() - 1);

    return str;
}

std::string utils::stripComment(const std::string &str) {
    bool inString = false;
    bool escaped = false;
    char quote = '\0';

    for (size_t i = 0; i < str.length(); i++) {
        char c = str[i];

        if (escaped) {
            escaped = false;
            continue;
        }

        if (inString && c == '\\') {
            escaped = true;
            continue;
        }

        if ((c == '"' || c == '\'') && !inString) {
            inString = true;
            quote = c;
            continue;
        }

        if (inString && c == quote) {
            inString = false;
            continue;
        }

        if (!inString && c == ';') {
            return str.substr(0, i);
        }
    }

    return str;
}

int utils::parseNumber(const std::string &s) {
    size_t pos = 0;
    int value;
    int sign = 1;
    std::string number = trim(s);

    if (number.empty()) {
        throw std::invalid_argument("Invalid numeric literal");
    }

    if (number[0] == '+' || number[0] == '-') {
        if (number[0] == '-') {
            sign = -1;
        }
        number = number.substr(1);
    }

    if (number.empty()) {
        throw std::invalid_argument("Invalid numeric literal");
    }

    if (number.starts_with("0x") || number.starts_with("0X")) {
        value = std::stoi(number, &pos, 16);
    } else if (number.starts_with("0b") || number.starts_with("0B")) {
        value = std::stoi(number.substr(2), &pos, 2);
        pos += 2;
    } else if (number.starts_with("0o") || number.starts_with("0O")) {
        value = std::stoi(number.substr(2), &pos, 8);
        pos += 2;
    } else {
        value = std::stoi(number, &pos, 10);
    }

    if (pos != number.size()) {
        throw std::invalid_argument("Invalid numeric literal");
    }

    return value * sign;
}

Parser::Parser(const std::string &input, const std::string &sourcePath)
    : input(input), sourcePath(sourcePath) {
    Dictionary::registerAllInstructions();
}

void Parser::fail(std::size_t line, const std::string &message) const {
    throw std::runtime_error("line " + std::to_string(line) + ": " + message);
}

void Parser::parse() {
    expressions.clear();
    std::vector<std::string> includeStack;
    parseSource(input, sourcePath, includeStack);
}

void Parser::parseSource(const std::string &source, const std::string &path,
                         std::vector<std::string> &includeStack) {
    const std::vector<std::string> lines = utils::split(source, '\n');
    for (size_t lineIndex = 0; lineIndex < lines.size(); lineIndex++) {
        std::size_t lineNumber = lineIndex + 1;
        std::string trimmedLine =
            utils::trim(utils::stripComment(lines[lineIndex]));
        if (trimmedLine.empty()) {
            continue;
        }
        if (trimmedLine[trimmedLine.length() - 1] == ':') {
            expressions.push_back(std::make_unique<Label>(
                trimmedLine.substr(0, trimmedLine.length() - 1), lineNumber));
        } else if (trimmedLine[0] == '.') {
            std::string name;

            size_t i = 0;

            while (i < trimmedLine.length() &&
                   !std::isblank(static_cast<unsigned char>(trimmedLine[i]))) {
                name += trimmedLine[i];
                i++;
            }

            while (i < trimmedLine.length() &&
                   std::isblank(static_cast<unsigned char>(trimmedLine[i]))) {
                i++;
            }

            std::vector<std::string> arguments =
                utils::splitArguments(trimmedLine.substr(i));

            if (name == ".include") {
                if (arguments.size() != 1) {
                    fail(lineNumber,
                         ".include expects exactly one file path argument.");
                }

                std::filesystem::path includePath = unquote(arguments[0]);
                if (includePath.is_relative()) {
                    includePath = includeBase(path) / includePath;
                }

                includePath = std::filesystem::weakly_canonical(includePath);
                std::string includeKey = includePath.string();
                if (std::find(includeStack.begin(), includeStack.end(),
                              includeKey) != includeStack.end()) {
                    fail(lineNumber,
                         "Recursive include detected for " + includeKey + ".");
                }

                includeStack.push_back(includeKey);
                parseSource(readTextFile(includePath), includeKey, includeStack);
                includeStack.pop_back();
                continue;
            }

            expressions.push_back(std::make_unique<Directive>(
                name, arguments, std::string(trimmedLine), lineNumber, path));
        } else {
            std::string opcode;

            size_t i = 0;

            while (i < trimmedLine.length() &&
                   !std::isblank(static_cast<unsigned char>(trimmedLine[i]))) {
                opcode += trimmedLine[i];
                i++;
            }

            while (i < trimmedLine.length() &&
                   std::isblank(static_cast<unsigned char>(trimmedLine[i]))) {
                i++;
            }

            std::vector<std::string> operands =
                utils::splitArguments(trimmedLine.substr(i));

            expressions.push_back(std::make_unique<Instruction>(
                opcode, operands, std::string(trimmedLine), lineNumber));
        }
    }
}

void Parser::verifyIntegrity() {
    for (auto &expression : expressions) {
        if (expression->type == ExpressionType::Directive) {
            auto directive = dynamic_cast<Directive *>(expression.get());
            if (directive->name == ".const" || directive->name == ".constant" ||
                directive->name == ".string" || directive->name == ".data" ||
                directive->name == ".byte" || directive->name == ".word" ||
                directive->name == ".addr24" || directive->name == ".include" ||
                directive->name == ".bin") {
                continue;
            }
            fail(directive->line,
                 "Directive " + directive->name + " is not a valid directive.");
        }
        if (expression->type == ExpressionType::Instruction) {
            auto instruction = dynamic_cast<Instruction *>(expression.get());
            bool foundMatch = false;
            for (auto &validInstruction : Dictionary::all()) {
                if (instruction->opcode == validInstruction->name &&
                    instruction->operands.size() ==
                        validInstruction->argumentCount) {
                    foundMatch = true;

                    for (auto argument : instruction->operands) {
                        if (argument.starts_with("0") ||
                            std::isdigit(
                                static_cast<unsigned char>(argument[0]))) {
                            try {
                                int val = utils::parseNumber(argument);
                                (void)val;
                            } catch (...) {
                                fail(instruction->line,
                                     "String '" + argument +
                                         "' was detected as a number but "
                                         "cannot be "
                                         "parsed as one.");
                            }
                        }

                        if (argument.starts_with("r") &&
                            argument.length() > 1 &&
                            std::isdigit(
                                static_cast<unsigned char>(argument[1])) &&
                            !isGeneralRegister(argument)) {
                            fail(instruction->line,
                                 "Register " + argument +
                                     " is invalid in the E16 architecture.");
                        }
                    }

                    verifySpecialRegisters(instruction);
                    continue;
                }
                if (instruction->opcode == validInstruction->name &&
                    instruction->operands.size() !=
                        validInstruction->argumentCount) {
                    fail(instruction->line,
                         "Instruction " + instruction->opcode + " expected " +
                             std::to_string(validInstruction->argumentCount) +
                             " arguments but instead " +
                             std::to_string(instruction->operands.size()) +
                             " were provided.");
                }
            }
            if (!foundMatch) {
                fail(instruction->line, "Instruction " + instruction->opcode +
                                            " is not a valid instruction.");
            }
        }
    }
}

void Parser::printExpressions() {
    std::cout << "Parsing has " << expressions.size() << " expressions. "
              << std::endl;
    for (auto &expression : expressions) {
        if (expression->type == ExpressionType::Label) {
            auto label = dynamic_cast<Label *>(expression.get());
            std::cout << "Label: " << label->name << std::endl;
        } else if (expression->type == ExpressionType::Directive) {
            auto directive = dynamic_cast<Directive *>(expression.get());
            std::cout << "Directive: " << directive->name << std::endl;
            for (auto argument : directive->arguments) {
                std::cout << "    " << argument << std::endl;
            }
        } else if (expression->type == ExpressionType::Instruction) {
            auto instruction = dynamic_cast<Instruction *>(expression.get());
            std::cout << "Instruction: " << instruction->opcode << std::endl;
            if (instruction->parsedOperands.empty()) {
                for (auto operand : instruction->operands) {
                    std::cout << "    " << operand << std::endl;
                }
            } else {
                for (auto operand : instruction->parsedOperands) {
                    std::cout << "    "
                              << "Addressing Mode: "
                              << addressingModeToString(operand.mode)
                              << ", Base: " << operand.base;

                    if (operand.offset != 0) {
                        std::cout << ", Offset: "
                                  << std::to_string(operand.offset);
                    }

                    if (!operand.offsetReg.empty()) {
                        std::cout << ", Offset Register: " << operand.offsetReg;
                    }
                    std::cout << std::endl;
                }
            }
        }
    }
}

void Parser::verifySpecialRegisters(Instruction *instruction) {
    if (instruction->opcode == "get") {
        if (!isGeneralRegister(instruction->operands[0]) ||
            !isSpecialRegister(instruction->operands[1])) {
            fail(instruction->line,
                 "get expects a general register followed by a special "
                 "register.");
        }
        return;
    }

    if (instruction->opcode == "set") {
        if (!isSpecialRegister(instruction->operands[0])) {
            fail(instruction->line,
                 "set expects a special register as its first argument.");
        }
        return;
    }

    for (auto argument : instruction->operands) {
        if (isSpecialRegister(argument)) {
            if (instruction->opcode == "mov") {
                fail(instruction->line,
                     "For readability, try to address special registers like " +
                         argument + " with 'get' and 'set'");
            }
        }
    }
}

void Parser::parseAddressingModes() {
    const auto constants = collectParserConstants(expressions);

    for (auto &expression : expressions) {
        if (expression->type == ExpressionType::Instruction) {
            auto *instruction = dynamic_cast<Instruction *>(expression.get());
            instruction->parsedOperands.clear();
            for (std::string operand : instruction->operands) {
                int resolvedExpression = 0;
                if (resolveConstExpression(operand, constants,
                                           resolvedExpression)) {
                    instruction->parsedOperands.push_back(InstructionOperand(
                        AddressingMode::Immediate,
                        normalizeNumber(resolvedExpression)));
                    continue;
                }

                try {
                    int val = utils::parseNumber(operand);
                    (void)val;
                    instruction->parsedOperands.push_back(
                        InstructionOperand(AddressingMode::Immediate, operand));
                    continue;
                } catch (...) {
                }

                if (isRegister(operand)) {
                    instruction->parsedOperands.push_back(
                        InstructionOperand(AddressingMode::Register, operand));
                    continue;
                }

                if (operand.starts_with("dp:")) {
                    try {
                        int val = utils::parseNumber(operand.substr(3));
                        instruction->parsedOperands.push_back(
                            InstructionOperand(AddressingMode::DirectPageOffset,
                                               "dp", val));
                        continue;
                    } catch (...) {
                        fail(instruction->line,
                             "Badly formulated direct page offset mode "
                             "argument " +
                                 operand + ".");
                    }
                }

                if (operand.starts_with("#")) {
                    try {
                        int val = utils::parseNumber(operand.substr(1));
                        instruction->parsedOperands.push_back(
                            InstructionOperand(AddressingMode::StackRelative,
                                               "sp", val));
                        continue;
                    } catch (...) {
                        fail(instruction->line,
                             "Badly formulated stack-relative "
                             "offset mode argument " +
                                 operand + ".");
                    }
                }

                if (operand[0] == '@') {
                    if (operand.length() > 2 && operand[1] == '(' &&
                        operand.back() == ')') {
                        std::string inner = utils::trim(
                            operand.substr(2, operand.length() - 3));
                        int resolvedAddress = 0;
                        if (resolveConstExpression(inner, constants,
                                                   resolvedAddress)) {
                            instruction->parsedOperands.push_back(
                                InstructionOperand(
                                    AddressingMode::Absolute,
                                    normalizeNumber(resolvedAddress)));
                            continue;
                        }
                    }

                    try {
                        int val = utils::parseNumber(operand.substr(1));
                        (void)val;
                        instruction->parsedOperands.push_back(
                            InstructionOperand(AddressingMode::Absolute,
                                               operand.substr(1)));
                        continue;
                    } catch (...) {
                    }

                    if (isRegister(operand.substr(1))) {
                        instruction->parsedOperands.push_back(
                            InstructionOperand(AddressingMode::RegisterIndirect,
                                               operand.substr(1)));
                        continue;
                    }

                    if (operand.length() > 2 && operand[1] == '(' &&
                        operand.back() == ')') {
                        std::string inner = utils::trim(
                            operand.substr(2, operand.length() - 3));
                        size_t signPos = findOffsetOperator(inner);

                        if (signPos == std::string::npos) {
                            fail(instruction->line,
                                 "Badly formulated mode for argument " +
                                     operand);
                        }

                        std::string left =
                            utils::trim(inner.substr(0, signPos));
                        char sign;
                        sign = inner[signPos];
                        std::string right =
                            utils::trim(inner.substr(signPos + 1));

                        if (left.empty() || right.empty()) {
                            fail(instruction->line,
                                 "Badly formulated mode for argument " +
                                     operand);
                        }

                        if (isRegister(right) && sign == '+') {
                            instruction->parsedOperands.push_back(
                                InstructionOperand(AddressingMode::Indexed,
                                                   left, 0, right));
                            continue;
                        }

                        if (isRegister(left)) {
                            try {
                                int val = utils::parseNumber(
                                    (sign == '-' ? "-" : "") + right);
                                instruction->parsedOperands.push_back(
                                    InstructionOperand(
                                        AddressingMode::BasePlusOffset, left,
                                        val));
                                continue;
                            } catch (...) {
                                fail(instruction->line,
                                     "The offsetting mode for argument " +
                                         operand + " is badly formulated");
                            }
                        }

                        fail(instruction->line,
                             "Badly formulated mode for argument " + operand);
                    }

                    if (isIdentifier(operand.substr(1))) {
                        instruction->parsedOperands.push_back(
                            InstructionOperand(AddressingMode::Absolute,
                                               operand.substr(1)));
                        continue;
                    }
                }

                bool foundLabelOrConstant = false;
                for (auto &candidate : expressions) {
                    if (foundLabelOrConstant) {
                        break;
                    }
                    if (candidate->type == ExpressionType::Label) {
                        Label *label = dynamic_cast<Label *>(candidate.get());
                        if (label->name == operand) {
                            instruction->parsedOperands.push_back(
                                InstructionOperand(AddressingMode::Label,
                                                   operand));
                            foundLabelOrConstant = true;
                        }
                    } else if (candidate->type == ExpressionType::Directive) {
                        Directive *directive =
                            dynamic_cast<Directive *>(candidate.get());
                        if (directive->name == ".const" ||
                            directive->name == ".constant") {
                            if (directive->arguments.size() != 2) {
                                fail(directive->line,
                                     "A constant directive must have exactly "
                                     "two arguments.");
                            }
                            std::string name = directive->arguments[0];
                            std::string substitution = directive->arguments[1];
                            if (name == operand) {
                                try {
                                    int val = utils::parseNumber(substitution);
                                    (void)val;
                                    instruction->parsedOperands.push_back(
                                        InstructionOperand(
                                            AddressingMode::Immediate,
                                            substitution));
                                    foundLabelOrConstant = true;
                                } catch (...) {
                                    fail(instruction->line,
                                         "The offsetting mode for argument " +
                                             operand + " is badly formulated");
                                }
                            }
                        }
                    }
                }

                if (!foundLabelOrConstant) {
                    fail(instruction->line,
                         "The addressing mode for argument " + operand +
                             " is not correctly formulated.");
                }
            }
        }
    }
}

bool Parser::isRegister(std::string val) {
    return isGeneralRegister(val) || isSpecialRegister(val);
}

std::string Parser::addressingModeToString(AddressingMode mode) {
    switch (mode) {
    case AddressingMode::Immediate:
        return "Immediate";
    case AddressingMode::Register:
        return "Register";
    case AddressingMode::Absolute:
        return "Absolute";
    case AddressingMode::BasePlusOffset:
        return "Base + Offset";
    case AddressingMode::DirectPageOffset:
        return "Direct Page Offset";
    case AddressingMode::Indexed:
        return "Indexed";
    case AddressingMode::RegisterIndirect:
        return "Register Indirect";
    case AddressingMode::StackRelative:
        return "Stack Relative";
    case AddressingMode::Label:
        return "Label";
    }
    return "Unknown";
}

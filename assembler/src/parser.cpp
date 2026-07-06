#include <parser.h>

#include <iostream>
#include <string>
#include <vector>

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

std::string utils::trim(std::string str) {
    while (!str.empty() &&
           std::isspace(static_cast<unsigned char>(str.front())))
        str = str.substr(1);

    while (!str.empty() && std::isspace(static_cast<unsigned char>(str.back())))
        str = str.substr(0, str.length() - 1);

    return str;
}

int utils::parseNumber(const std::string &s) {
    size_t pos = 0;
    int value;

    if (s.starts_with("0x")) {
        value = std::stoi(s, &pos, 16);
    } else if (s.starts_with("0b")) {
        value = std::stoi(s.substr(2), &pos, 2);
        pos += 2;
    } else if (s.starts_with("0o")) {
        value = std::stoi(s.substr(2), &pos, 8);
        pos += 2;
    } else {
        value = std::stoi(s, &pos, 10);
    }

    if (pos != s.size()) {
        throw std::invalid_argument("Invalid numeric literal");
    }

    return value;
}

Parser::Parser(const std::string &input) : input(input) {
    Dictionary::registerAllInstructions();
}

void Parser::parse() {
    for (const std::vector<std::string> lines = utils::split(input, '\n');
         std::string line : lines) {
        std::string trimmedLine = utils::trim(line);
        if (trimmedLine.empty()) {
            continue;
        }
        if (trimmedLine[trimmedLine.length() - 1] == ':') {
            // Label
            expressions.push_back(std::make_unique<Label>(
                trimmedLine.substr(0, trimmedLine.length() - 1)));
        } else if (trimmedLine[0] == '.') {
            std::string name;
            std::vector<std::string> arguments;

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

            while (i < trimmedLine.length()) {
                std::string argument;

                while (i < trimmedLine.length() && trimmedLine[i] != ',') {
                    argument += trimmedLine[i];
                    i++;
                }

                argument = std::string(utils::trim(argument));

                if (!argument.empty()) {
                    arguments.push_back(argument);
                }

                if (i < trimmedLine.length() && trimmedLine[i] == ',') {
                    i++;
                }

                while (
                    i < trimmedLine.length() &&
                    std::isblank(static_cast<unsigned char>(trimmedLine[i]))) {
                    i++;
                }
            }

            expressions.push_back(std::make_unique<Directive>(
                name, arguments, std::string(trimmedLine)));
        } else {
            // Instructions
            std::string opcode;
            std::vector<std::string> operands;

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

            while (i < trimmedLine.length()) {
                std::string argument;

                while (i < trimmedLine.length() && trimmedLine[i] != ',') {
                    argument += trimmedLine[i];
                    i++;
                }

                argument = std::string(utils::trim(argument));

                if (!argument.empty()) {
                    operands.push_back(argument);
                }

                if (i < trimmedLine.length() && trimmedLine[i] == ',') {
                    i++;
                }

                while (
                    i < trimmedLine.length() &&
                    std::isblank(static_cast<unsigned char>(trimmedLine[i]))) {
                    i++;
                }
            }

            expressions.push_back(std::make_unique<Instruction>(
                opcode, operands, std::string(trimmedLine)));
        }
    }
}

void Parser::verifyIntegrity() {
    for (auto &expression : expressions) {
        if (expression->type == ExpressionType::Directive) {
            auto directive = dynamic_cast<Directive *>(expression.get());
            if (directive->name == ".const" || directive->name == ".constant") {
                continue;
            }
            throw std::runtime_error("Directive " + directive->name +
                                     " is not a valid directive.");
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
                            isnumber(argument[0])) {
                            try {
                                int val = utils::parseNumber(argument);
                                (void)val;
                            } catch (...) {
                                throw std::runtime_error(
                                    "String '" + argument +
                                    "' was detected as a number but cannot be "
                                    "parsed as one.");
                            }
                        }

                        if (argument.starts_with("r")) {
                            if (argument.length() > 3) {
                                throw std::runtime_error(
                                    "The E16 can just address "
                                    "registers from r0 to r15.");
                            }
                            int registerNum = std::stoi(argument.substr(1));
                            if (registerNum > 15) {
                                throw std::runtime_error(
                                    "Register " + argument +
                                    " is invalid in the E16 architecture.");
                            }
                        }
                    }

                    verifySpecialRegisters(instruction);
                    continue;
                }
                if (instruction->opcode == validInstruction->name &&
                    instruction->operands.size() !=
                        validInstruction->argumentCount) {
                    throw std::runtime_error(
                        "Instruction " + instruction->opcode + " expected " +
                        std::to_string(validInstruction->argumentCount) +
                        " arguments but instead " +
                        std::to_string(instruction->operands.size()) +
                        " were provided.");
                }
            }
            if (!foundMatch) {
                throw std::runtime_error("Instruction " + instruction->opcode +
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
            for (auto operand : instruction->operands) {
                std::cout << "    " << operand << std::endl;
            }
        }
    }
}

void Parser::verifySpecialRegisters(Instruction *instruction) {
    for (auto argument : instruction->operands) {
        if (argument == "pc" || argument == "sp" || argument == "fp" ||
            argument == "fl" || argument == "dp" || argument == "ivt") {
            if (instruction->opcode == "mov") {
                throw std::runtime_error(
                    "For readability, try to address special registers like " +
                    argument + " with 'get' and 'set'");
            }
        } else if (instruction->opcode == "get" ||
                   instruction->opcode == "set") {
            throw std::runtime_error(
                "Only special registers can be modified with 'get' or 'set'");
        }
    }
}
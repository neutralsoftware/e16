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
        str.substr(0, str.length() - 1);

    return str;
}

Parser::Parser(const std::string &input) : input(input) {}

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

void Parser::verifyIntegrity() {}

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
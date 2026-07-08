//
// parser.h
// As part of the assembler project
// Created by Max Van den Eynde in 2026
// --------------------------------------------------
// Description: Parser for the assembler for the E16
// Copyright (c) 2026 Max Van den Eynde
//

#ifndef PARSER_H
#define PARSER_H

#include <cstddef>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

enum class ExpressionType { Instruction, Label, Directive };

class Expression {
  public:
    Expression(ExpressionType type, const std::string &contents,
               std::size_t line)
        : type(type), content(contents), line(line) {}
    ExpressionType type;
    std::string content;
    std::size_t line;

    virtual ~Expression() = default;
};

enum class AddressingMode {
    Immediate,
    Register,
    Absolute,
    RegisterIndirect,
    Indexed,
    BasePlusOffset,
    DirectPageOffset,
    StackRelative,
    Label
};

struct InstructionOperand {
    AddressingMode mode;
    std::string base;
    int offset = 0;
    std::string offsetReg;
};

class Instruction : public Expression {
  public:
    Instruction(const std::string &opcode,
                const std::vector<std::string> &operands,
                const std::string &contents, std::size_t line)
        : Expression(ExpressionType::Instruction, contents, line),
          opcode(opcode), operands(operands) {}

    std::string opcode;
    std::vector<std::string> operands;
    std::vector<InstructionOperand> parsedOperands;
};

class Label : public Expression {
  public:
    explicit Label(const std::string &name, std::size_t line)
        : Expression(ExpressionType::Label, name + ":", line), name(name) {}

    std::string name;
};

class Directive : public Expression {
  public:
    Directive(const std::string &name,
              const std::vector<std::string> &arguments,
              const std::string &contents, std::size_t line,
              const std::string &sourcePath = "")
        : Expression(ExpressionType::Directive, contents, line), name(name),
          arguments(arguments), sourcePath(sourcePath) {}

    std::string name;
    std::vector<std::string> arguments;
    std::string sourcePath;
};

class Parser {
  public:
    Parser(const std::string &input, const std::string &sourcePath = "");
    std::string input;
    std::string sourcePath;
    std::vector<std::unique_ptr<Expression>> expressions;

    void parse();
    void verifyIntegrity();
    void printExpressions();
    void verifySpecialRegisters(Instruction *instruction);
    void parseAddressingModes();

    static bool isRegister(std::string val);
    static std::string addressingModeToString(AddressingMode mode);

  private:
    struct MacroDefinition {
        std::vector<std::string> parameters;
        std::vector<std::string> body;
        std::size_t line = 0;
    };

    std::unordered_map<std::string, MacroDefinition> macros;
    int macroExpansionDepth = 0;

    void parseSource(const std::string &source, const std::string &path,
                     std::vector<std::string> &includeStack);
    [[noreturn]] void fail(std::size_t line, const std::string &message) const;
};

struct InstructionDefinition {
    std::string name;
    int opcode;
    int argumentCount;
};

class Dictionary {
  public:
    static void addInstruction(std::unique_ptr<InstructionDefinition> def) {
        instructions.push_back(std::move(def));
    }

    static const std::vector<std::unique_ptr<InstructionDefinition>> &all() {
        return instructions;
    }

    static void registerAllInstructions();

  private:
    static inline std::vector<std::unique_ptr<InstructionDefinition>>
        instructions;
};

namespace utils {
std::vector<std::string> split(std::string str, char separator);
std::vector<std::string> splitArguments(const std::string &str);
std::string trim(std::string str);
std::string stripComment(const std::string &str);
int parseNumber(const std::string &s);
bool evaluateExpression(
    const std::string &text,
    const std::unordered_map<std::string, int> &symbols,
    int &value);
} // namespace utils

#endif

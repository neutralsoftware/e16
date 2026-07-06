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

#include <string>
#include <vector>

enum class ExpressionType { Instruction, Label, Directive };

class Expression {
  public:
    Expression(ExpressionType type, const std::string &contents)
        : type(type), content(contents) {}
    ExpressionType type;
    std::string content;

    virtual ~Expression() = default;
};

class Instruction : public Expression {
  public:
    Instruction(const std::string &opcode,
                const std::vector<std::string> &operands,
                const std::string &contents)
        : Expression(ExpressionType::Instruction, contents), opcode(opcode),
          operands(operands) {}

    std::string opcode;
    std::vector<std::string> operands;
};

class Label : public Expression {
  public:
    explicit Label(const std::string &name)
        : Expression(ExpressionType::Label, name + ":"), name(name) {}

    std::string name;
};

class Directive : public Expression {
  public:
    Directive(const std::string &name,
              const std::vector<std::string> &arguments,
              const std::string &contents)
        : Expression(ExpressionType::Directive, contents), name(name),
          arguments(arguments) {}

    std::string name;
    std::vector<std::string> arguments;
};

class Parser {
  public:
    Parser(const std::string &input);
    std::string input;
    std::vector<std::unique_ptr<Expression>> expressions;

    void parse();
    void verifyIntegrity();
    void printExpressions();
};

struct InstructionDefinition {
    std::string name;
    int opcode;
    int argumentCount;
};

class Dictionary {
  public:
};

namespace utils {
std::vector<std::string> split(std::string str, char separator);
std::string trim(std::string str);
} // namespace utils

#endif

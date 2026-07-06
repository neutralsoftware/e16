/*
 * compiler.h
 * As part of the E16Assembler project
 * Created by Max Van den Eynde in 2026
 * --------------------------------------
 * Description: Final compiler
 * Copyright (c) 2026 Max Van den Eynde
 */

#ifndef E16ASSEMBLER_COMPILER_H
#define E16ASSEMBLER_COMPILER_H

#include "parser.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

class Compiler {
  public:
    Compiler(Parser &parser, std::uint32_t baseAddress);

    std::vector<std::uint8_t> compile();
    void writeBinary(const std::string &path);

  private:
    Parser &parser;
    std::uint32_t baseAddress;
    std::unordered_map<std::string, std::uint32_t> symbols;
    std::unordered_map<std::string, int> constants;
    std::unordered_map<const Expression *, std::uint32_t> addresses;
    std::unordered_map<const Expression *, std::size_t> sizes;

    void collectConstants();
    void layout();
    std::size_t directiveSize(const Directive &directive) const;
    std::size_t instructionSize(const Instruction &instruction) const;
    void emitDirective(const Directive &directive,
                       std::vector<std::uint8_t> &bytes) const;
    void emitInstruction(const Instruction &instruction,
                         std::vector<std::uint8_t> &bytes) const;
};

#endif // E16ASSEMBLER_COMPILER_H

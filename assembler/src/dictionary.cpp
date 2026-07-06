/*
 * dictionary.cpp
 * As part of the E16Assembler project
 * Created by Max Van den Eynde in 2026
 * --------------------------------------
 * Description: The dictionary of instructions
 * Copyright (c) 2026 Max Van den Eynde
 */

#include "parser.h"

void Dictionary::registerAllInstructions() {
    // Data movem ent
    addInstruction(std::make_unique<InstructionDefinition>("nop", 0x0, 0));
    addInstruction(std::make_unique<InstructionDefinition>("mov", 0x1, 2));
    addInstruction(std::make_unique<InstructionDefinition>("movb", 0x2, 2));
    addInstruction(std::make_unique<InstructionDefinition>("movw", 0x3, 2));
    addInstruction(std::make_unique<InstructionDefinition>("clr", 0x4, 1));
    addInstruction(std::make_unique<InstructionDefinition>("swap", 0x5, 2));
    addInstruction(std::make_unique<InstructionDefinition>("xchg", 0x6, 2));

    // Memory access
    addInstruction(std::make_unique<InstructionDefinition>("load", 0x10, 2));
    addInstruction(std::make_unique<InstructionDefinition>("loadb", 0x11, 2));
    addInstruction(std::make_unique<InstructionDefinition>("loadw", 0x12, 2));
    addInstruction(std::make_unique<InstructionDefinition>("loadsb", 0x13, 2));
    addInstruction(std::make_unique<InstructionDefinition>("store", 0x14, 2));
    addInstruction(std::make_unique<InstructionDefinition>("storeb", 0x15, 2));
    addInstruction(std::make_unique<InstructionDefinition>("storew", 0x16, 2));
    addInstruction(std::make_unique<InstructionDefinition>("addr", 0x17, 2));

    // Arithmetic
    addInstruction(std::make_unique<InstructionDefinition>("add", 0x20, 2));
    addInstruction(std::make_unique<InstructionDefinition>("addwc", 0x21, 2));
    addInstruction(std::make_unique<InstructionDefinition>("sub", 0x22, 2));
    addInstruction(std::make_unique<InstructionDefinition>("subwc", 0x23, 2));
    addInstruction(std::make_unique<InstructionDefinition>("inc", 0x24, 1));
    addInstruction(std::make_unique<InstructionDefinition>("dec", 0x25, 1));
    addInstruction(std::make_unique<InstructionDefinition>("neg", 0x26, 1));
    addInstruction(std::make_unique<InstructionDefinition>("mul", 0x27, 2));
    addInstruction(std::make_unique<InstructionDefinition>("muls", 0x28, 2));
    addInstruction(std::make_unique<InstructionDefinition>("div", 0x29, 2));
    addInstruction(std::make_unique<InstructionDefinition>("divs", 0x2A, 2));
    addInstruction(std::make_unique<InstructionDefinition>("mod", 0x2B, 2));

    // Bitwise
    addInstruction(std::make_unique<InstructionDefinition>("and", 0x30, 2));
    addInstruction(std::make_unique<InstructionDefinition>("or", 0x31, 2));
    addInstruction(std::make_unique<InstructionDefinition>("xor", 0x32, 2));
    addInstruction(std::make_unique<InstructionDefinition>("not", 0x33, 1));
    addInstruction(std::make_unique<InstructionDefinition>("test", 0x34, 2));
    addInstruction(std::make_unique<InstructionDefinition>("setb", 0x35, 2));
    addInstruction(std::make_unique<InstructionDefinition>("clearb", 0x36, 2));
    addInstruction(std::make_unique<InstructionDefinition>("toggleb", 0x37, 2));

    // Shifts and rotates
    addInstruction(std::make_unique<InstructionDefinition>("shl", 0x40, 2));
    addInstruction(std::make_unique<InstructionDefinition>("shr", 0x41, 2));
    addInstruction(std::make_unique<InstructionDefinition>("sar", 0x42, 2));
    addInstruction(std::make_unique<InstructionDefinition>("rol", 0x43, 1));
    addInstruction(std::make_unique<InstructionDefinition>("ror", 0x44, 1));
    addInstruction(std::make_unique<InstructionDefinition>("rcl", 0x45, 1));
    addInstruction(std::make_unique<InstructionDefinition>("rcr", 0x46, 1));

    // Comparison
    addInstruction(std::make_unique<InstructionDefinition>("cmp", 0x50, 2));

    // Branches and jumps
    addInstruction(std::make_unique<InstructionDefinition>("jmp", 0x60, 1));
    addInstruction(std::make_unique<InstructionDefinition>("bra", 0x61, 1));
    addInstruction(std::make_unique<InstructionDefinition>("beq", 0x62, 1));
    addInstruction(std::make_unique<InstructionDefinition>("bne", 0x63, 1));
    addInstruction(std::make_unique<InstructionDefinition>("bcs", 0x64, 1));
    addInstruction(std::make_unique<InstructionDefinition>("bcc", 0x65, 1));
    addInstruction(std::make_unique<InstructionDefinition>("bmi", 0x66, 1));
    addInstruction(std::make_unique<InstructionDefinition>("bpl", 0x67, 1));
    addInstruction(std::make_unique<InstructionDefinition>("bvs", 0x68, 1));
    addInstruction(std::make_unique<InstructionDefinition>("bvc", 0x69, 1));
    addInstruction(std::make_unique<InstructionDefinition>("bgt", 0x6A, 1));
    addInstruction(std::make_unique<InstructionDefinition>("bge", 0x6B, 1));
    addInstruction(std::make_unique<InstructionDefinition>("blt", 0x6C, 1));
    addInstruction(std::make_unique<InstructionDefinition>("ble", 0x6D, 1));
    addInstruction(std::make_unique<InstructionDefinition>("bhi", 0x6E, 1));
    addInstruction(std::make_unique<InstructionDefinition>("bls", 0x6F, 1));

    // Calls and stack
    addInstruction(std::make_unique<InstructionDefinition>("call", 0x70, 1));
    addInstruction(std::make_unique<InstructionDefinition>("ret", 0x71, 0));
    addInstruction(std::make_unique<InstructionDefinition>("enter", 0x72, 1));
    addInstruction(std::make_unique<InstructionDefinition>("leave", 0x73, 0));
    addInstruction(std::make_unique<InstructionDefinition>("push", 0x74, 1));
    addInstruction(std::make_unique<InstructionDefinition>("pop", 0x74, 1));
    addInstruction(std::make_unique<InstructionDefinition>("pushf", 0x75, 0));
    addInstruction(std::make_unique<InstructionDefinition>("popf", 0x76, 0));
    addInstruction(std::make_unique<InstructionDefinition>("pusha", 0x77, 0));
    addInstruction(std::make_unique<InstructionDefinition>("popa", 0x78, 0));

    // Interrupts & System
    addInstruction(std::make_unique<InstructionDefinition>("int", 0x80, 1));
    addInstruction(std::make_unique<InstructionDefinition>("iret", 0x81, 0));
    addInstruction(std::make_unique<InstructionDefinition>("ei", 0x82, 0));
    addInstruction(std::make_unique<InstructionDefinition>("di", 0x83, 0));
    addInstruction(std::make_unique<InstructionDefinition>("wait", 0x84, 0));
    addInstruction(std::make_unique<InstructionDefinition>("halt", 0x85, 0));
    addInstruction(std::make_unique<InstructionDefinition>("reset", 0x86, 0));
    addInstruction(std::make_unique<InstructionDefinition>("trap", 0x87, 0));

    // Special registers
    addInstruction(std::make_unique<InstructionDefinition>("get", 0x90, 2));
    addInstruction(std::make_unique<InstructionDefinition>("set", 0x91, 2));

    // Helpers
    addInstruction(std::make_unique<InstructionDefinition>("dma", 0xA0, 0));
}

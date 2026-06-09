/*
 * disasm.h - Public AVR disassembly formatter.
 * Produces human-readable assembly for one- and two-word instructions.
 */
#pragma once

#include <cstdint>
#include <string>

// Disassemble a single instruction.  `pc` is the byte address of `instr`
// in flash, used to compute branch/jump targets.
// `extra` is the second word for 32-bit instructions (0 otherwise).
std::string disassemble(uint16_t instr, uint16_t extra, uint16_t pc);

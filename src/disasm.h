#pragma once
// Disassembly formatter for the ATmega328P emulator TUI.
// Takes raw instruction words and produces human-readable assembly strings
// like "ADD R16, R17" or "RJMP -4".

#include <cstdint>
#include <string>

// Disassemble a single instruction.  `pc` is the byte address of `instr`
// in flash, used to compute branch/jump targets.
// `extra` is the second word for 32-bit instructions (0 otherwise).
std::string disassemble(uint16_t instr, uint16_t extra, uint16_t pc);

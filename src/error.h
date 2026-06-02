#pragma once

#include <cstdint>

// Report emulator failures to stderr with consistent formatting.
// All messages are prefixed with "error:" and written to stderr.

void emuError(const char* message);
void emuErrorFile(const char* path, const char* message);
void emuErrorPc(uint16_t pc, const char* message);
void emuErrorPcInstr(uint16_t pc, uint16_t instruction, const char* message);
void emuErrorPcAddr(uint16_t pc, uint16_t addr, const char* access, const char* detail);

// Set before each instruction so memory/stack helpers can cite the faulting PC.
void emuSetFaultPc(uint16_t pc);
uint16_t emuFaultPc();

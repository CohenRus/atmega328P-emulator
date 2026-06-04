/*
 * error.cpp — Implementation of emulator error-reporting helpers.
 *
 * Uses a file-scope global to hold the faulting PC, set before every
 * instruction so that any helper (memory, stack, etc.) can produce an
 * error message that pinpoints the originating instruction.
 *
 * Design: all output goes to stderr. The format helpers below avoid
 * repeating the "error:" prefix and PC/instruction/address suffixes.
 */

#include "error.h"

#include <cstdio>
#include <iostream>

namespace {

// The PC of the currently executing instruction, or 0xFFFF if unset.
uint16_t g_faultPc = 0xFFFF;

// Write the standard "error:" prefix to stderr.
void writePrefix() {
  std::cerr << "error: ";
}

// Write " at PC 0xXXXX" to stderr.
// @param pc — program counter word address
void writePc(uint16_t pc) {
  std::fprintf(stderr, " at PC 0x%04X", pc);
}

// Write " (instruction 0xXXXX)" to stderr.
// @param instruction — 16-bit opcode
void writeInstr(uint16_t instruction) {
  std::fprintf(stderr, " (instruction 0x%04X)", instruction);
}

// Write " (address 0xXXXX)" to stderr.
// @param addr — data-space byte address
void writeAddr(uint16_t addr) {
  std::fprintf(stderr, " (address 0x%04X)", addr);
}

}  // namespace

// Record the PC at which the current instruction started executing.
// Called by the executor before dispatching each instruction.
void emuSetFaultPc(uint16_t pc) {
  g_faultPc = pc;
}

// Return the currently-set fault PC.
uint16_t emuFaultPc() {
  return g_faultPc;
}

// Generic error — just the message, no extra context.
void emuError(const char* message) {
  writePrefix();
  std::cerr << message << '\n';
}

// Error associated with a filesystem path (ELF loading failures, etc.).
// Omits the path if it is NULL or empty.
void emuErrorFile(const char* path, const char* message) {
  writePrefix();
  std::cerr << message;
  if (path && path[0] != '\0') {
    std::cerr << " ('" << path << "')";
  }
  std::cerr << '\n';
}

// Error with a program counter.
void emuErrorPc(uint16_t pc, const char* message) {
  writePrefix();
  std::cerr << message;
  writePc(pc);
  std::cerr << '\n';
}

// Error with PC and the instruction word that faulted.
// Useful when the instruction itself is unknown or illegal.
void emuErrorPcInstr(uint16_t pc, uint16_t instruction, const char* message) {
  writePrefix();
  std::cerr << message;
  writePc(pc);
  writeInstr(instruction);
  std::cerr << '\n';
}

// Error with PC, faulting data-space address, access type, and detail.
// Used by memory/stack access violations within an instruction.
void emuErrorPcAddr(uint16_t pc, uint16_t addr, const char* access, const char* detail) {
  writePrefix();
  std::cerr << detail;
  writePc(pc);
  std::cerr << " during " << access;
  writeAddr(addr);
  std::cerr << '\n';
}

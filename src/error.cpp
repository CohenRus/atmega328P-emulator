#include "error.h"

#include <cstdio>
#include <iostream>

namespace {

uint16_t g_faultPc = 0xFFFF;

void writePrefix() {
  std::cerr << "error: ";
}

void writePc(uint16_t pc) {
  std::fprintf(stderr, " at PC 0x%04X", pc);
}

void writeInstr(uint16_t instruction) {
  std::fprintf(stderr, " (instruction 0x%04X)", instruction);
}

void writeAddr(uint16_t addr) {
  std::fprintf(stderr, " (address 0x%04X)", addr);
}

}  // namespace

void emuSetFaultPc(uint16_t pc) {
  g_faultPc = pc;
}

uint16_t emuFaultPc() {
  return g_faultPc;
}

void emuError(const char* message) {
  writePrefix();
  std::cerr << message << '\n';
}

void emuErrorFile(const char* path, const char* message) {
  writePrefix();
  std::cerr << message;
  if (path && path[0] != '\0') {
    std::cerr << " ('" << path << "')";
  }
  std::cerr << '\n';
}

void emuErrorPc(uint16_t pc, const char* message) {
  writePrefix();
  std::cerr << message;
  writePc(pc);
  std::cerr << '\n';
}

void emuErrorPcInstr(uint16_t pc, uint16_t instruction, const char* message) {
  writePrefix();
  std::cerr << message;
  writePc(pc);
  writeInstr(instruction);
  std::cerr << '\n';
}

void emuErrorPcAddr(uint16_t pc, uint16_t addr, const char* access, const char* detail) {
  writePrefix();
  std::cerr << detail;
  writePc(pc);
  std::cerr << " during " << access;
  writeAddr(addr);
  std::cerr << '\n';
}

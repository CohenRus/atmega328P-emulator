/*
 * error.cpp — Implementation of emulator error-reporting helpers.
 *
 * Uses a file-scope global to hold the faulting PC, set before every
 * instruction so that any helper (memory, stack, etc.) can produce an
 * error message that pinpoints the originating instruction.
 *
 * Design: all output goes to stderr. The format helpers below avoid
 * repeating the "error:" prefix and PC/instruction/address suffixes.
 *
 * Each error function also captures its message into g_emu_last_error
 * so the TUI can display the last error without parsing stderr.
 */

#include "error.h"
#include <cstdio>
#include <mutex>

// Last error captured for UI consumption. Protected by g_error_mutex.
static std::mutex g_error_mutex;
static char g_emu_last_error[256] = "";

namespace {
// The PC of the currently executing instruction, or 0xFFFF if unset.
uint16_t g_faultPc = 0xFFFF;
// The raw 16-bit instruction word at g_faultPc, or 0xFFFF if unset.
uint16_t g_faultInstr = 0xFFFF;
}  // namespace


// Record the PC at which the current instruction started executing.
void emuSetFaultPc(uint16_t pc) {
  g_faultPc = pc;
}
uint16_t emuFaultPc() {
  return g_faultPc;
}
void emuSetFaultInstr(uint16_t instr) {
  g_faultInstr = instr;
}
uint16_t emuFaultInstr() {
  return g_faultInstr;
}
void emuError(const char* message) {
  std::lock_guard<std::mutex> lock(g_error_mutex);
  std::snprintf(g_emu_last_error, sizeof(g_emu_last_error), "%s", message);
  std::fprintf(stderr, "error: %s\n", message);
}
void emuErrorFile(const char* path, const char* message) {
  std::lock_guard<std::mutex> lock(g_error_mutex);
  if (path && path[0] != '\0') {
    std::snprintf(g_emu_last_error, sizeof(g_emu_last_error), "%s ('%s')", message, path);
    std::fprintf(stderr, "error: %s ('%s')\n", message, path);
  } else {
    std::snprintf(g_emu_last_error, sizeof(g_emu_last_error), "%s", message);
    std::fprintf(stderr, "error: %s\n", message);
  }
}
void emuErrorPc(uint16_t pc, const char* message) {
  std::lock_guard<std::mutex> lock(g_error_mutex);
  std::snprintf(g_emu_last_error, sizeof(g_emu_last_error), "%s at PC 0x%04X", message, pc);
  std::fprintf(stderr, "error: %s at PC 0x%04X\n", message, pc);
}
void emuErrorPcInstr(uint16_t pc, uint16_t instruction, const char* message) {
  std::lock_guard<std::mutex> lock(g_error_mutex);
  std::snprintf(g_emu_last_error, sizeof(g_emu_last_error), "%s at PC 0x%04X (0x%04X)", message, pc, instruction);
  std::fprintf(stderr, "error: %s at PC 0x%04X (instruction 0x%04X)\n", message, pc, instruction);
}
void emuErrorPcAddr(uint16_t pc, uint16_t addr, const char* access, const char* detail) {
  std::lock_guard<std::mutex> lock(g_error_mutex);
  std::snprintf(g_emu_last_error, sizeof(g_emu_last_error),
                "%s at PC 0x%04X (instr 0x%04X) addr 0x%04X (%s)",
                detail, pc, g_faultInstr, addr, access);
  std::fprintf(stderr, "error: %s at PC 0x%04X (instr 0x%04X) during %s (address 0x%04X)\n",
               detail, pc, g_faultInstr, access, addr);
}
void emuGetLastError(char* buf, size_t bufsize) {
  if (bufsize == 0) return;
  std::lock_guard<std::mutex> lock(g_error_mutex);
  std::snprintf(buf, bufsize, "%s", g_emu_last_error);
}

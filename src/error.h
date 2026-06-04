/*
 * error.h — Consistent error reporting for the ATmega328P emulator.
 *
 * All error output is prefixed with "error:" and written to stderr.
 * Variants attach optional context: file path, program counter,
 * instruction word, or faulting address.
 *
 * A thread-local fault-PC can be set before each instruction so that
 * memory/stack helpers can report the exact instruction that triggered
 * a fault without threading the PC through every call.
 */
#pragma once

#include <cstdint>

// Report a generic error message to stderr.
// @param message — error description (null terminated)
void emuError(const char* message);

// Report an error that relates to a specific file (e.g. ELF loading).
// @param path    — path to the file (may be NULL or empty)
// @param message — error description
void emuErrorFile(const char* path, const char* message);

// Report an error with the program counter at which it occurred.
// @param pc      — program counter value (word address)
// @param message — error description
void emuErrorPc(uint16_t pc, const char* message);

// Report an error with both program counter and the instruction word.
// @param pc          — program counter value
// @param instruction — full 16-bit opcode
// @param message     — error description
void emuErrorPcInstr(uint16_t pc, uint16_t instruction, const char* message);

// Report an error with PC, faulting address, access type, and detail.
// @param pc     — program counter value
// @param addr   — the data-space address that was accessed
// @param access — "read" or "write"
// @param detail — human-readable description of the fault
void emuErrorPcAddr(uint16_t pc, uint16_t addr, const char* access, const char* detail);

// Set the faulting PC so that memory/stack helpers can cite it in errors.
// @param pc — program counter at which the current instruction started
void emuSetFaultPc(uint16_t pc);

// Return the faulting PC stored by emuSetFaultPc.
// @return program counter (defaults to 0xFFFF if never set)
uint16_t emuFaultPc();

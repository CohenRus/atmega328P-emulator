/*
 * memory.h - Unified ATmega328P data-space and stack access interface.
 * Routes register, I/O, peripheral, SRAM, and flash operations through the
 * emulator's bounds-checked memory model.
 */
#pragma once

#include <cstdint>
#include "state.h"
#include "uart.h"

// ===========================================================================
// Memory helpers for the ATmega328P emulator.
//
// All helpers take AvrState& as the first parameter.  None allocate, none
// throw.  Every data-space access routes through readDataByte / writeDataByte
// so the unified address map lives in exactly one place.
// ===========================================================================

// ── SREG Flags ───────────────────────────────────────────────────────────────

// Bit indices into state.sreg (matches ATmega328P datasheet §6.3)
enum class SregBit : uint8_t {
    C = 0,  // Carry
    Z = 1,  // Zero
    N = 2,  // Negative
    V = 3,  // Two's complement overflow
    S = 4,  // Sign (N xor V)
    H = 5,  // Half Carry
    T = 6,  // Bit copy storage
    I = 7,  // Global interrupt enable
};

void  setFlag(AvrState& state, SregBit bit);
void  clearFlag(AvrState& state, SregBit bit);
bool  getFlag(const AvrState& state, SregBit bit);

// ── GP Register Access ───────────────────────────────────────────────────────

uint8_t  readReg(const AvrState& state, uint8_t reg);
void     writeReg(AvrState& state, uint8_t reg, uint8_t value);

// Word ops: reg must be even (R0, R2, …, R30).
// Low byte = r[reg], high byte = r[reg + 1].
uint16_t readRegWord(const AvrState& state, uint8_t reg);
void     writeRegWord(AvrState& state, uint8_t reg, uint16_t value);

// ── Data Space (unified: registers + I/O + SRAM) ─────────────────────────────
//
// Data-space address map (ATmega328P):
//   0x0000 – 0x001F   →  state.r[0..31]          (32 GP registers)
//   0x0020 – 0x005F   →  state.sram[0x20..0x5F]   (64 I/O registers)
//      EXCEPT 0x005D (SPL), 0x005E (SPH), 0x005F (SREG) which proxy to
//      state.sp and state.sreg struct fields.
//   0x0060 – 0x00FF   →  state.sram[0x60..0xFF]   (160 extended I/O)
//   0x0100 – 0x08FF   →  state.sram[0x100..0x8FF] (2048 bytes SRAM)

uint8_t readDataByte(const AvrState& state, uint16_t addr);
void    writeDataByte(AvrState& state, uint16_t addr, uint8_t value);

bool memoryFaultPending();
void memoryClearFault();
void memorySignalFault();

// ── I/O Register Bit Helpers ─────────────────────────────────────────────────
//
// Operate on I/O register address (0x00–0x3F), which maps to data-space
// 0x20 + ioAddr.  Used by SBI / CBI / SBIC / SBIS.

bool  getIOBit(const AvrState& state, uint8_t ioAddr, uint8_t bit);
void  setIOBit(AvrState& state, uint8_t ioAddr, uint8_t bit);
void  clearIOBit(AvrState& state, uint8_t ioAddr, uint8_t bit);

// ── Stack ────────────────────────────────────────────────────────────────────
//
// PUSH writes at SP, then decrements it. POP increments SP, then reads.
// Stack lives at data-space addresses 0x0100-0x08FF.

void     pushByte(AvrState& state, uint8_t value);
uint8_t  popByte(AvrState& state);

// Low byte pushed first (higher address), high byte second (lower address).
void     pushWord(AvrState& state, uint16_t value);
uint16_t popWord(AvrState& state);

// ── Program Memory (Flash) ───────────────────────────────────────────────────
//
// Read a 16-bit little-endian word from flash at the given byte address.
// addr must be even and within [0, AVR_FLASH_SIZE).

uint16_t readFlashWord(const AvrState& state, uint32_t byteAddr);

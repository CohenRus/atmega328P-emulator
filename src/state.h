/*
 * state.h - Complete mutable state of one emulated ATmega328P.
 * Stores CPU registers, timing counters, and the flash, SRAM, and EEPROM
 * backing arrays used by the core and peripherals.
 */
#pragma once
#include <cstdint>

#define AVR_FLASH_SIZE  32768
#define AVR_SRAM_SIZE   2304  // data-space addrs 0x20..0x8FF (2048 B SRAM + 256 B I/O)
#define AVR_EEPROM_SIZE 1024
// Program memory is word-addressed on real hardware; our PC is a byte address
// (word_addr × 2).  cycle_count tracks total elapsed cycles for peripheral timing.
struct AvrState {
  // CPU state
  uint8_t r[32]; // 32 general purpose registers R0-R31
  uint16_t pc; // program counter (byte address into flash)
  uint8_t sreg; // status register
  uint16_t sp; // stack pointer
  uint64_t cycle_count; // total elapsed CPU cycles (for peripheral timing)
  uint8_t  extra_cycles; // extra cycles for branch taken / skip (resets to 0 each instruction)

  // Device memory
  uint8_t flash[AVR_FLASH_SIZE];
  uint8_t sram[AVR_SRAM_SIZE];
  uint8_t eeprom[AVR_EEPROM_SIZE];
};

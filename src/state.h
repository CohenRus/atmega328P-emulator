// Defines AvrState — the complete runtime state of the emulated ATmega328P,
// Keeps track of general-purpose registers, program counter, stack pointer, status register,
// and all three memory regions (flash, SRAM, EEPROM).
#pragma once
#include <cstdint>

#define AVR_FLASH_SIZE  32768
#define AVR_SRAM_SIZE   2048
#define AVR_EEPROM_SIZE 1024

struct AvrState {
  // registers
  uint8_t r[32]; // 32 general purpose registers R0-R31
  uint16_t pc; // program counter 
  uint8_t sreg; // status register
  uint16_t sp; // stack pointer

  // memory
  uint8_t flash[AVR_FLASH_SIZE];
  uint8_t sram[AVR_SRAM_SIZE];
  uint8_t eeprom[AVR_EEPROM_SIZE];
};


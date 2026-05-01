#include "executor.h"
#include "state.h"
#include "decoder.h"

// run the program
bool execute(AvrState& state) {
  // setup initial state
  clearState(state);

  bool running = true;
  uint16_t instruction; 
  while (running) {
    if (state.pc > AVR_FLASH_SIZE) {
      std::cout << "end of memory reached" << std::endl;
      return false;
    }
    // read instruction from flash
    // each instruction is two bytes, stored low byte high byte
    instruction = state.flash[state.pc] | state.flash[state.pc + 1] << 8;

    // update program counter
    state.pc += 2;

    // decode instruction
    Opcode op = decodeInstruction(instruction);
    std::cout << op.mnemonic << std::endl;

    // read second word for 32-bit instructions before executing
    uint16_t extra = 0;
    if (op.words == 2) {
      extra = state.flash[state.pc] | state.flash[state.pc + 1] << 8;
      state.pc += 2;
    }

    // run the instruction
  }
  return true;
}

// do any inital setup of memory state
bool clearState(AvrState& state) {
  
  // registers
  for(uint8_t i = 0; i < 32; ++i) state.r[i] = 0;
  
  state.pc = 0;
  state.sreg = 0;
  state.sp = 0;

  // memory
  for(int i = 0; i < AVR_SRAM_SIZE; ++i) state.sram[i] = 0;
  for(int i = 0; i < AVR_EEPROM_SIZE; ++i) state.eeprom[i] = 0;
}

#include "decoder.h"
#include <stdexcept>

Opcode decodeInstruction(uint16_t& instruction) {
  for (Opcode opcode : OPCODE_TABLE) {
    uint16_t op = instruction & opcode.mask;
    if (op == opcode.code) {
      return opcode;
    }
  }
  throw std::runtime_error("unknown opcode");
}


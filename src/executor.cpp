#include "executor.h"
#include "memory.h"
#include "error.h"

// ---------------------------------------------------------------------------
// SREG bit indices (matches ATmega328P datasheet)
// ---------------------------------------------------------------------------
#define SREG_C  0
#define SREG_Z  1
#define SREG_N  2
#define SREG_V  3
#define SREG_S  4
#define SREG_H  5
#define SREG_T  6
#define SREG_I  7

static void skipNextInstruction(AvrState& state, const char* context);

// ---------------------------------------------------------------------------
// Execute loop
// ---------------------------------------------------------------------------
bool executeProgram(AvrState& state) {
  clearState(state);
  uartInit();

  bool running = true;
  uint16_t instruction;
  while (running) {
    uartPoll();
    if (state.pc + 1 >= AVR_FLASH_SIZE) {
      emuErrorPc(state.pc, "program counter out of flash bounds");
      return false;
    }
    const uint16_t instrPc = state.pc;
    emuSetFaultPc(instrPc);

    // each instruction is two bytes, stored low byte then high byte
    instruction = state.flash[state.pc] | state.flash[state.pc + 1] << 8;
    state.pc += 2;

    Opcode op;
    if (!decodeInstruction(instruction, op)) {
      emuErrorPcInstr(instrPc, instruction, "unknown opcode (not in decoder table)");
      return false;
    }

    // read second word for 32-bit instructions before executing
    uint16_t extra = 0;
    if (op.words == 2) {
      if (state.pc + 1 >= AVR_FLASH_SIZE) {
        emuErrorPc(state.pc, "32-bit instruction extends past end of flash");
        return false;
      }
      extra = state.flash[state.pc] | state.flash[state.pc + 1] << 8;
      state.pc += 2;
    }

    memoryClearFault();
    if (!executeInstruction(state, op, instruction, extra)) {
      return false;
    }
    if (memoryFaultPending()) {
      return false;
    }
  }
  return true;
}

// do any inital setup of memory state
bool clearState(AvrState& state) {
  // registers
  for (uint8_t i = 0; i < 32; ++i) state.r[i] = 0;
  state.pc   = 0;       // fallback; loadFirmware() overwrites with ELF entry point
  state.sreg = 0;
  state.sp   = 0x08FF;  // RAMEND for ATmega328P

  // memory
  for (int i = 0; i < AVR_SRAM_SIZE;   ++i) state.sram[i]   = 0;
  for (int i = 0; i < AVR_EEPROM_SIZE; ++i) state.eeprom[i] = 0;
  return true;
}

// Decode operands once based on op.fmt, then call the matching execute fn.
// Aliases (LSL→ADD, ROL→ADC, CLR→EOR, TST→AND, etc.) share the same decode
// path and just call the canonical implementation.
bool executeInstruction(AvrState& state, Opcode& op, uint16_t instr, uint16_t secondWord) {
  switch (op.op) {

    // --- Arithmetic -------------------------------------------------------

    case AvrOp::ADC:    executeADC(state,    decodeRdRr(instr));           break;
    case AvrOp::ADD:    executeADD(state,    decodeRdRr(instr));           break;
    case AvrOp::ADIW:   executeADIW(state,   decodeRd06K6(instr));         break;
    case AvrOp::ASR:    executeASR(state,    decodeRd(instr));             break;
    case AvrOp::DEC:    executeDEC(state,    decodeRd(instr));             break;
    case AvrOp::INC:    executeINC(state,    decodeRd(instr));             break;
    case AvrOp::MUL:    executeMUL(state,    decodeRdRr(instr));           break;
    case AvrOp::MULS:   executeMULS(state,   decodeRd06Rr06(instr));       break;
    case AvrOp::MULSU:  executeMULSU(state,  decodeRdRrMpy(instr));        break;
    case AvrOp::FMUL:   executeFMUL(state,   decodeRdRrMpy(instr));        break;
    case AvrOp::FMULS:  executeFMULS(state,  decodeRdRrMpy(instr));        break;
    case AvrOp::FMULSU: executeFMULSU(state, decodeRdRrMpy(instr));        break;
    case AvrOp::NEG:    executeNEG(state,    decodeRd(instr));             break;
    case AvrOp::SBIW:   executeSBIW(state,   decodeRd06K6(instr));         break;
    case AvrOp::SBC:    executeSBC(state,    decodeRdRr(instr));           break;
    case AvrOp::SBCI:   executeSBCI(state,   decodeRdK8(instr));           break;
    case AvrOp::SUB:    executeSUB(state,    decodeRdRr(instr));           break;
    case AvrOp::SUBI:   executeSUBI(state,   decodeRdK8(instr));           break;

    // --- Logic ------------------------------------------------------------

    case AvrOp::AND:    executeAND(state,    decodeRdRr(instr));           break;
    case AvrOp::ANDI:   executeANDI(state,   decodeRdK8(instr));           break;
    case AvrOp::CBR:    executeANDI(state,   decodeRdK8(instr));           break; // CBR = ANDI ~K
    case AvrOp::CLR:    executeEOR(state,    decodeRdRr(instr));           break; // CLR = EOR Rd,Rd
    case AvrOp::COM:    executeCOM(state,    decodeRd(instr));             break;
    case AvrOp::EOR:    executeEOR(state,    decodeRdRr(instr));           break;
    case AvrOp::OR:     executeOR(state,     decodeRdRr(instr));           break;
    case AvrOp::ORI:    executeORI(state,    decodeRdK8(instr));           break;
    case AvrOp::SBR:    executeORI(state,    decodeRdK8(instr));           break; // SBR = ORI
    case AvrOp::SER:    executeSER(state,    decodeRdK8(instr));           break;
    case AvrOp::SWAP:   executeSWAP(state,   decodeRd(instr));             break;

    // --- Shift / Rotate ---------------------------------------------------

    case AvrOp::LSL:    executeADD(state,    decodeRdRr(instr));           break; // LSL = ADD Rd,Rd
    case AvrOp::LSR:    executeLSR(state,    decodeRd(instr));             break;
    case AvrOp::ROL:    executeADC(state,    decodeRdRr(instr));           break; // ROL = ADC Rd,Rd
    case AvrOp::ROR:    executeROR(state,    decodeRd(instr));             break;

    // --- Compare ----------------------------------------------------------

    case AvrOp::CP:     executeCP(state,     decodeRdRr(instr));           break;
    case AvrOp::CPC:    executeCPC(state,    decodeRdRr(instr));           break;
    case AvrOp::CPI:    executeCPI(state,    decodeRdK8(instr));           break;
    case AvrOp::CPSE:   executeCPSE(state,   decodeRdRr(instr));           break;
    case AvrOp::TST:    executeAND(state,    decodeRdRr(instr));           break; // TST = AND Rd,Rd

    // --- Data Transfer ----------------------------------------------------

    case AvrOp::MOV:    executeMOV(state,    decodeRdRr(instr));           break;
    case AvrOp::MOVW:   executeMOVW(state,   decodeRd06Rr06(instr));       break;
    case AvrOp::LDI:    executeLDI(state,    decodeRdK8(instr));           break;
    case AvrOp::LD_X:   executeLD_X(state,   decodeLdSt(instr));           break;

    case AvrOp::LD_Y:
      if (op.fmt == AvrFmt::LDD_family) executeLDD_Y(state, decodeLdd(instr));
      else                              executeLD_Y(state,   decodeLdSt(instr));
      break;

    case AvrOp::LD_Z:
      if (op.fmt == AvrFmt::LDD_family) executeLDD_Z(state, decodeLdd(instr));
      else                              executeLD_Z(state,   decodeLdSt(instr));
      break;

    case AvrOp::LDS:    executeLDS(state,    decodeLdsSts(instr, secondWord)); break;

    // LPM with NONE fmt implies R0 and Z with no post-increment
    case AvrOp::LPM:
      if (op.fmt == AvrFmt::NONE) executeLPM(state, OpsRd{0}, 0);
      else                        executeLPM(state, decodeRd(instr), instr & 0x01);
      break;

    case AvrOp::IN:     executeIN(state,     decodeRdIO(instr));           break;
    case AvrOp::OUT:    executeOUT(state,    decodeIORr(instr));           break;
    case AvrOp::POP:    executePOP(state,    decodeRd(instr));             break;
    case AvrOp::PUSH:   executePUSH(state,   decodeRd(instr));             break;
    case AvrOp::ST_X:   executeST_X(state,   decodeLdSt(instr));           break;

    case AvrOp::ST_Y:
      if (op.fmt == AvrFmt::LDD_family) executeSTD_Y(state, decodeLdd(instr));
      else                              executeST_Y(state,   decodeLdSt(instr));
      break;

    case AvrOp::ST_Z:
      if (op.fmt == AvrFmt::LDD_family) executeSTD_Z(state, decodeLdd(instr));
      else                              executeST_Z(state,   decodeLdSt(instr));
      break;

    case AvrOp::STS:    executeSTS(state,    decodeLdsSts(instr, secondWord)); break;

    // --- Branch / Jump / Call ---------------------------------------------

    // Named branch aliases all encode the SREG bit in instr[2:0], so decodeK7
    // extracts the right value even for BRCC/BRNE/etc.
    case AvrOp::BRBC:
    case AvrOp::BRCC: case AvrOp::BRGE: case AvrOp::BRHC: case AvrOp::BRID:
    case AvrOp::BRNE: case AvrOp::BRPL: case AvrOp::BRSH: case AvrOp::BRTC:
    case AvrOp::BRVC:
      executeBRBC(state, decodeK7(instr)); break;

    case AvrOp::BRBS:
    case AvrOp::BRCS: case AvrOp::BREQ: case AvrOp::BRHS: case AvrOp::BRIE:
    case AvrOp::BRLO: case AvrOp::BRLT: case AvrOp::BRMI: case AvrOp::BRTS:
    case AvrOp::BRVS:
      executeBRBS(state, decodeK7(instr)); break;

    case AvrOp::RJMP:   executeRJMP(state,   decodeK02(instr));            break;
    case AvrOp::JMP:    executeJMP(state,    decodeK22(instr, secondWord)); break;
    case AvrOp::IJMP:   executeIJMP(state);                                break;
    case AvrOp::RCALL:  executeRCALL(state,  decodeK02(instr));            break;
    case AvrOp::CALL:   executeCALL(state,   decodeK22(instr, secondWord)); break;
    case AvrOp::ICALL:  executeICALL(state);                               break;
    case AvrOp::RET:    executeRET(state);                                 break;
    case AvrOp::RETI:   executeRETI(state);                                break;

    // --- Skip -------------------------------------------------------------

    case AvrOp::SBIC:   executeSBIC(state,   decodeIOB(instr));            break;
    case AvrOp::SBIS:   executeSBIS(state,   decodeIOB(instr));            break;
    case AvrOp::SBRC:   executeSBRC(state,   decodeRrB(instr));            break;
    case AvrOp::SBRS:   executeSBRS(state,   decodeRrB(instr));            break;

    // --- Bit manipulation -------------------------------------------------

    case AvrOp::BSET:   executeBSET(state,   decodeBOnly(instr));          break;
    case AvrOp::BCLR:   executeBCLR(state,   decodeBOnly(instr));          break;
    case AvrOp::BLD:    executeBLD(state,    decodeRdB(instr));            break;
    case AvrOp::BST:    executeBST(state,    decodeRdB(instr));            break;
    case AvrOp::CBI:    executeCBI(state,    decodeIOB(instr));            break;
    case AvrOp::SBI:    executeSBI(state,    decodeIOB(instr));            break;

    // SREG flag set/clear aliases — bit index is still encoded in instr[6:4]
    case AvrOp::SEC: case AvrOp::SEZ: case AvrOp::SEN: case AvrOp::SEV:
    case AvrOp::SES: case AvrOp::SEH: case AvrOp::SET: case AvrOp::SEI:
      executeBSET(state, decodeBOnly(instr)); break;

    case AvrOp::CLC: case AvrOp::CLZ: case AvrOp::CLN: case AvrOp::CLV:
    case AvrOp::CLS: case AvrOp::CLH: case AvrOp::CLT: case AvrOp::CLI:
      executeBCLR(state, decodeBOnly(instr)); break;

    // --- MCU control ------------------------------------------------------

    case AvrOp::NOP:    executeNOP(state);    break;
    case AvrOp::SLEEP:  executeSLEEP(state);  break;
    case AvrOp::WDR:    executeWDR(state);    break;
    case AvrOp::BREAK:  executeBREAK(state);  break;
    case AvrOp::SPM_E:  executeSPM(state);    break;

    default:
      emuErrorPcInstr(emuFaultPc(), instr,
                      "unimplemented opcode in executor (decoded but not handled)");
      return false;
  }
  return true;
}

// ===========================================================================
// Arithmetic instructions
// ===========================================================================

void executeADC(AvrState& state, OpsRdRr ops) {
    uint8_t rd = state.r[ops.d];
    uint8_t rr = state.r[ops.r];
    uint8_t ci = (state.sreg >> SREG_C) & 1;
    uint16_t result16 = (uint16_t)rd + (uint16_t)rr + ci;
    uint8_t result8 = (uint8_t)result16;

    bool h = ((rd & 0x0F) + (rr & 0x0F) + ci) > 0x0F;
    bool v = ((rd ^ result8) & (rr ^ result8) & 0x80) != 0;
    bool n = (result8 & 0x80) != 0;
    bool z = result8 == 0;
    bool c = result16 > 0xFF;
    bool s = n ^ v;

    state.r[ops.d] = result8;
    state.sreg = (state.sreg & 0xC0) | (s << SREG_S) | (v << SREG_V)
               | (n << SREG_N) | (z << SREG_Z) | (c << SREG_C) | (h << SREG_H);
}

void executeADD(AvrState& state, OpsRdRr ops) {
    uint8_t rd = state.r[ops.d];
    uint8_t rr = state.r[ops.r];
    uint16_t result16 = (uint16_t)rd + (uint16_t)rr;
    uint8_t result8 = (uint8_t)result16;

    bool h = ((rd & 0x0F) + (rr & 0x0F)) > 0x0F;
    bool v = ((rd ^ result8) & (rr ^ result8) & 0x80) != 0;
    bool n = (result8 & 0x80) != 0;
    bool z = result8 == 0;
    bool c = result16 > 0xFF;
    bool s = n ^ v;

    state.r[ops.d] = result8;
    state.sreg = (state.sreg & 0xC0) | (s << SREG_S) | (v << SREG_V)
               | (n << SREG_N) | (z << SREG_Z) | (c << SREG_C) | (h << SREG_H);
}

void executeADIW(AvrState& state, OpsRd06K6 ops) {
    uint8_t  lo = state.r[ops.d];
    uint8_t  hi = state.r[ops.d + 1];
    uint16_t word = ((uint16_t)hi << 8) | lo;
    uint16_t result16 = word + ops.k;

    bool v = (hi & 0x80) && !(result16 & 0x8000);
    bool n = (result16 & 0x8000) != 0;
    bool z = result16 == 0;
    bool c = !(result16 & 0x8000) && (hi & 0x80);
    bool s = n ^ v;

    state.r[ops.d]     = (uint8_t)(result16 & 0xFF);
    state.r[ops.d + 1] = (uint8_t)(result16 >> 8);
    state.sreg = (state.sreg & 0xE0) | (s << SREG_S) | (v << SREG_V)
               | (n << SREG_N) | (z << SREG_Z) | (c << SREG_C);
}

void executeASR(AvrState& state, OpsRd ops) {
    uint8_t rd = state.r[ops.d];
    bool c = rd & 0x01;
    uint8_t result8 = (rd >> 1) | (rd & 0x80);
    bool n = (result8 & 0x80) != 0;
    bool z = result8 == 0;
    bool v = n ^ c;
    bool s = n ^ v;

    state.r[ops.d] = result8;
    state.sreg = (state.sreg & 0xE0) | (s << SREG_S) | (v << SREG_V)
               | (n << SREG_N) | (z << SREG_Z) | (c << SREG_C);
}

void executeDEC(AvrState& state, OpsRd ops) {
    uint8_t rd = state.r[ops.d];
    uint8_t result8 = rd - 1;
    bool v = rd == 0x80;
    bool n = (result8 & 0x80) != 0;
    bool z = result8 == 0;
    bool s = n ^ v;

    state.r[ops.d] = result8;
    state.sreg = (state.sreg & 0xE1) | (s << SREG_S) | (v << SREG_V)
               | (n << SREG_N) | (z << SREG_Z);
}

void executeINC(AvrState& state, OpsRd ops) {
    uint8_t rd = state.r[ops.d];
    uint8_t result8 = rd + 1;
    bool v = rd == 0x7F;
    bool n = (result8 & 0x80) != 0;
    bool z = result8 == 0;
    bool s = n ^ v;

    state.r[ops.d] = result8;
    state.sreg = (state.sreg & 0xE1) | (s << SREG_S) | (v << SREG_V)
               | (n << SREG_N) | (z << SREG_Z);
}

void executeMUL(AvrState& state, OpsRdRr ops) {
    uint16_t result16 = (uint16_t)state.r[ops.d] * (uint16_t)state.r[ops.r];
    state.r[0] = (uint8_t)(result16 & 0xFF);
    state.r[1] = (uint8_t)(result16 >> 8);
    bool c = (result16 & 0x8000) != 0;
    bool z = result16 == 0;
    state.sreg = (state.sreg & 0xFC) | (z << SREG_Z) | (c << SREG_C);
}

void executeMULS(AvrState& state, OpsRd06Rr06 ops) {
    int8_t rd = (int8_t)state.r[ops.d + 16];
    int8_t rr = (int8_t)state.r[ops.r + 16];
    int16_t result16 = (int16_t)rd * (int16_t)rr;
    state.r[0] = (uint8_t)((uint16_t)result16 & 0xFF);
    state.r[1] = (uint8_t)(((uint16_t)result16 >> 8) & 0xFF);
    bool c = (result16 & 0x8000) != 0;
    bool z = result16 == 0;
    state.sreg = (state.sreg & 0xFC) | (z << SREG_Z) | (c << SREG_C);
}

void executeMULSU(AvrState& state, OpsRdRrMpy ops) {
    int8_t  rd = (int8_t)state.r[ops.d];
    uint8_t rr = state.r[ops.r];
    int16_t result16 = (int16_t)rd * (int16_t)((uint16_t)rr);
    state.r[0] = (uint8_t)((uint16_t)result16 & 0xFF);
    state.r[1] = (uint8_t)(((uint16_t)result16 >> 8) & 0xFF);
    bool c = (result16 & 0x8000) != 0;
    bool z = result16 == 0;
    state.sreg = (state.sreg & 0xFC) | (z << SREG_Z) | (c << SREG_C);
}

void executeFMUL(AvrState& state, OpsRdRrMpy ops) {
    uint16_t product = (uint16_t)state.r[ops.d] * (uint16_t)state.r[ops.r];
    uint16_t result16 = product << 1;
    state.r[0] = (uint8_t)(result16 & 0xFF);
    state.r[1] = (uint8_t)(result16 >> 8);
    bool c = (result16 & 0x8000) != 0;
    bool z = result16 == 0;
    state.sreg = (state.sreg & 0xFC) | (z << SREG_Z) | (c << SREG_C);
}

void executeFMULS(AvrState& state, OpsRdRrMpy ops) {
    int8_t rd = (int8_t)state.r[ops.d];
    int8_t rr = (int8_t)state.r[ops.r];
    int16_t product = (int16_t)rd * (int16_t)rr;
    uint16_t result16 = (uint16_t)(product << 1);
    state.r[0] = (uint8_t)(result16 & 0xFF);
    state.r[1] = (uint8_t)(result16 >> 8);
    bool c = (result16 & 0x8000) != 0;
    bool z = result16 == 0;
    state.sreg = (state.sreg & 0xFC) | (z << SREG_Z) | (c << SREG_C);
}

void executeFMULSU(AvrState& state, OpsRdRrMpy ops) {
    int8_t  rd = (int8_t)state.r[ops.d];
    uint8_t rr = state.r[ops.r];
    int16_t product = (int16_t)rd * (int16_t)((uint16_t)rr);
    uint16_t result16 = (uint16_t)(product << 1);
    state.r[0] = (uint8_t)(result16 & 0xFF);
    state.r[1] = (uint8_t)(result16 >> 8);
    bool c = (result16 & 0x8000) != 0;
    bool z = result16 == 0;
    state.sreg = (state.sreg & 0xFC) | (z << SREG_Z) | (c << SREG_C);
}

void executeNEG(AvrState& state, OpsRd ops) {
    uint8_t rd = state.r[ops.d];
    uint8_t result8 = (uint8_t)(-(int8_t)rd);
    bool h = ((result8 & 0x08) != 0) || ((rd & 0x08) != 0);
    bool v = rd == 0x80;
    bool n = (result8 & 0x80) != 0;
    bool z = result8 == 0;
    bool c = result8 != 0x00;
    bool s = n ^ v;

    state.r[ops.d] = result8;
    state.sreg = (state.sreg & 0xC0) | (s << SREG_S) | (v << SREG_V)
               | (n << SREG_N) | (z << SREG_Z) | (c << SREG_C) | (h << SREG_H);
}

void executeSBIW(AvrState& state, OpsRd06K6 ops) {
    uint8_t  lo = state.r[ops.d];
    uint8_t  hi = state.r[ops.d + 1];
    uint16_t word = ((uint16_t)hi << 8) | lo;
    uint16_t result16 = word - ops.k;

    bool v = (hi & 0x80) && (result16 & 0x8000);
    bool n = (result16 & 0x8000) != 0;
    bool z = result16 == 0;
    bool c = (result16 & 0x8000) && (hi & 0x80);
    bool s = n ^ v;

    state.r[ops.d]     = (uint8_t)(result16 & 0xFF);
    state.r[ops.d + 1] = (uint8_t)(result16 >> 8);
    state.sreg = (state.sreg & 0xE0) | (s << SREG_S) | (v << SREG_V)
               | (n << SREG_N) | (z << SREG_Z) | (c << SREG_C);
}

void executeSBC(AvrState& state, OpsRdRr ops) {
    uint8_t rd = state.r[ops.d];
    uint8_t rr = state.r[ops.r];
    uint8_t ci = (state.sreg >> SREG_C) & 1;
    uint16_t result16 = (uint16_t)rd - (uint16_t)rr - ci;
    uint8_t result8 = (uint8_t)result16;

    bool h = ((rd & 0x0F) < ((rr & 0x0F) + ci));
    bool v = ((rd ^ rr) & (rd ^ result8) & 0x80) != 0;
    bool n = (result8 & 0x80) != 0;
    bool z = result8 == 0;
    bool c = result16 > 0xFF;
    bool s = n ^ v;

    state.r[ops.d] = result8;
    state.sreg = (state.sreg & 0xC0) | (s << SREG_S) | (v << SREG_V)
               | (n << SREG_N) | (z << SREG_Z) | (c << SREG_C) | (h << SREG_H);
}

void executeSBCI(AvrState& state, OpsRdK8 ops) {
    uint8_t rd = state.r[ops.d];
    uint8_t ci = (state.sreg >> SREG_C) & 1;
    uint16_t result16 = (uint16_t)rd - (uint16_t)ops.k - ci;
    uint8_t result8 = (uint8_t)result16;

    bool h = ((rd & 0x0F) < ((ops.k & 0x0F) + ci));
    bool v = ((rd ^ ops.k) & (rd ^ result8) & 0x80) != 0;
    bool n = (result8 & 0x80) != 0;
    bool z = result8 == 0;
    bool c = result16 > 0xFF;
    bool s = n ^ v;

    state.r[ops.d] = result8;
    state.sreg = (state.sreg & 0xC0) | (s << SREG_S) | (v << SREG_V)
               | (n << SREG_N) | (z << SREG_Z) | (c << SREG_C) | (h << SREG_H);
}

void executeSUB(AvrState& state, OpsRdRr ops) {
    uint8_t rd = state.r[ops.d];
    uint8_t rr = state.r[ops.r];
    uint16_t result16 = (uint16_t)rd - (uint16_t)rr;
    uint8_t result8 = (uint8_t)result16;

    bool h = (rd & 0x0F) < (rr & 0x0F);
    bool v = ((rd ^ rr) & (rd ^ result8) & 0x80) != 0;
    bool n = (result8 & 0x80) != 0;
    bool z = result8 == 0;
    bool c = result16 > 0xFF;
    bool s = n ^ v;

    state.r[ops.d] = result8;
    state.sreg = (state.sreg & 0xC0) | (s << SREG_S) | (v << SREG_V)
               | (n << SREG_N) | (z << SREG_Z) | (c << SREG_C) | (h << SREG_H);
}

void executeSUBI(AvrState& state, OpsRdK8 ops) {
    uint8_t rd = state.r[ops.d];
    uint16_t result16 = (uint16_t)rd - (uint16_t)ops.k;
    uint8_t result8 = (uint8_t)result16;

    bool h = (rd & 0x0F) < (ops.k & 0x0F);
    bool v = ((rd ^ ops.k) & (rd ^ result8) & 0x80) != 0;
    bool n = (result8 & 0x80) != 0;
    bool z = result8 == 0;
    bool c = result16 > 0xFF;
    bool s = n ^ v;

    state.r[ops.d] = result8;
    state.sreg = (state.sreg & 0xC0) | (s << SREG_S) | (v << SREG_V)
               | (n << SREG_N) | (z << SREG_Z) | (c << SREG_C) | (h << SREG_H);
}

// ===========================================================================
// Logic instructions
// ===========================================================================

void executeAND(AvrState& state, OpsRdRr ops) {
    uint8_t result8 = state.r[ops.d] & state.r[ops.r];
    bool n = (result8 & 0x80) != 0;
    bool z = result8 == 0;
    // V=0, S=N xor V = N
    state.r[ops.d] = result8;
    state.sreg = (state.sreg & 0xE4) | (n << SREG_S) | (n << SREG_N) | (z << SREG_Z);
}

void executeANDI(AvrState& state, OpsRdK8 ops) {
    uint8_t result8 = state.r[ops.d] & ops.k;
    bool n = (result8 & 0x80) != 0;
    bool z = result8 == 0;
    state.r[ops.d] = result8;
    state.sreg = (state.sreg & 0xE4) | (n << SREG_S) | (n << SREG_N) | (z << SREG_Z);
}

void executeCOM(AvrState& state, OpsRd ops) {
    uint8_t result8 = 0xFF - state.r[ops.d];
    bool n = (result8 & 0x80) != 0;
    bool z = result8 == 0;
    state.r[ops.d] = result8;
    // V=0, C=1, S=N
    state.sreg = (state.sreg & 0xE0) | (n << SREG_S) | (n << SREG_N)
               | (z << SREG_Z) | (1 << SREG_C);
}

void executeEOR(AvrState& state, OpsRdRr ops) {
    uint8_t result8 = state.r[ops.d] ^ state.r[ops.r];
    bool n = (result8 & 0x80) != 0;
    bool z = result8 == 0;
    state.r[ops.d] = result8;
    state.sreg = (state.sreg & 0xE4) | (n << SREG_S) | (n << SREG_N) | (z << SREG_Z);
}

void executeOR(AvrState& state, OpsRdRr ops) {
    uint8_t result8 = state.r[ops.d] | state.r[ops.r];
    bool n = (result8 & 0x80) != 0;
    bool z = result8 == 0;
    state.r[ops.d] = result8;
    state.sreg = (state.sreg & 0xE4) | (n << SREG_S) | (n << SREG_N) | (z << SREG_Z);
}

void executeORI(AvrState& state, OpsRdK8 ops) {
    uint8_t result8 = state.r[ops.d] | ops.k;
    bool n = (result8 & 0x80) != 0;
    bool z = result8 == 0;
    state.r[ops.d] = result8;
    state.sreg = (state.sreg & 0xE4) | (n << SREG_S) | (n << SREG_N) | (z << SREG_Z);
}

void executeSER(AvrState& state, OpsRdK8 ops) {
    state.r[ops.d] = 0xFF;
    // No SREG flags affected (consistent with LDI behavior)
}

void executeSWAP(AvrState& state, OpsRd ops) {
    uint8_t rd = state.r[ops.d];
    state.r[ops.d] = (rd << 4) | (rd >> 4);
    // No SREG flags affected
}

// ===========================================================================
// Shift / Rotate instructions
// ===========================================================================

void executeLSR(AvrState& state, OpsRd ops) {
    uint8_t rd = state.r[ops.d];
    bool c = rd & 0x01;
    uint8_t result8 = rd >> 1;
    bool n = false;
    bool z = result8 == 0;
    bool v = n ^ c;
    bool s = n ^ v;

    state.r[ops.d] = result8;
    state.sreg = (state.sreg & 0xE0) | (s << SREG_S) | (v << SREG_V)
               | (z << SREG_Z) | (c << SREG_C);
}

void executeROR(AvrState& state, OpsRd ops) {
    uint8_t rd = state.r[ops.d];
    bool oldC = (state.sreg >> SREG_C) & 1;
    bool newC = rd & 0x01;
    uint8_t result8 = (rd >> 1) | (oldC ? 0x80 : 0x00);
    bool n = (result8 & 0x80) != 0;
    bool z = result8 == 0;
    bool v = n ^ newC;
    bool s = n ^ v;

    state.r[ops.d] = result8;
    state.sreg = (state.sreg & 0xE0) | (s << SREG_S) | (v << SREG_V)
               | (n << SREG_N) | (z << SREG_Z) | (newC << SREG_C);
}

// ===========================================================================
// Compare instructions
// ===========================================================================

void executeCP(AvrState& state, OpsRdRr ops) {
    uint8_t rd = state.r[ops.d];
    uint8_t rr = state.r[ops.r];
    uint16_t result16 = (uint16_t)rd - (uint16_t)rr;
    uint8_t result8 = (uint8_t)result16;

    bool h = (rd & 0x0F) < (rr & 0x0F);
    bool v = ((rd ^ rr) & (rd ^ result8) & 0x80) != 0;
    bool n = (result8 & 0x80) != 0;
    bool z = result8 == 0;
    bool c = result16 > 0xFF;
    bool s = n ^ v;

    state.sreg = (state.sreg & 0xC0) | (s << SREG_S) | (v << SREG_V)
               | (n << SREG_N) | (z << SREG_Z) | (c << SREG_C) | (h << SREG_H);
}

void executeCPC(AvrState& state, OpsRdRr ops) {
    uint8_t rd = state.r[ops.d];
    uint8_t rr = state.r[ops.r];
    uint8_t ci = (state.sreg >> SREG_C) & 1;
    uint16_t result16 = (uint16_t)rd - (uint16_t)rr - ci;
    uint8_t result8 = (uint8_t)result16;

    bool h = ((rd & 0x0F) < ((rr & 0x0F) + ci));
    bool v = ((rd ^ rr) & (rd ^ result8) & 0x80) != 0;
    bool n = (result8 & 0x80) != 0;
    // Z: previous value preserved if result is zero; cleared otherwise
    bool prevZ = (state.sreg >> SREG_Z) & 1;
    bool z = (result8 == 0) ? prevZ : false;
    bool c = result16 > 0xFF;
    bool s = n ^ v;

    state.sreg = (state.sreg & 0xC0) | (s << SREG_S) | (v << SREG_V)
               | (n << SREG_N) | (z << SREG_Z) | (c << SREG_C) | (h << SREG_H);
}

void executeCPI(AvrState& state, OpsRdK8 ops) {
    uint8_t rd = state.r[ops.d];
    uint16_t result16 = (uint16_t)rd - (uint16_t)ops.k;
    uint8_t result8 = (uint8_t)result16;

    bool h = (rd & 0x0F) < (ops.k & 0x0F);
    bool v = ((rd ^ ops.k) & (rd ^ result8) & 0x80) != 0;
    bool n = (result8 & 0x80) != 0;
    bool z = result8 == 0;
    bool c = result16 > 0xFF;
    bool s = n ^ v;

    state.sreg = (state.sreg & 0xC0) | (s << SREG_S) | (v << SREG_V)
               | (n << SREG_N) | (z << SREG_Z) | (c << SREG_C) | (h << SREG_H);
}

void executeCPSE(AvrState& state, OpsRdRr ops) {
    if (state.r[ops.d] == state.r[ops.r]) {
        skipNextInstruction(state, "CPSE skip: unknown next instruction");
    }
}

// ===========================================================================
// Data Transfer instructions
// ===========================================================================

void executeMOV(AvrState& state, OpsRdRr ops) {
    state.r[ops.d] = state.r[ops.r];
}

void executeMOVW(AvrState& state, OpsRd06Rr06 ops) {
    uint8_t d = ops.d * 2;
    uint8_t r = ops.r * 2;
    state.r[d]     = state.r[r];
    state.r[d + 1] = state.r[r + 1];
}

void executeLDI(AvrState& state, OpsRdK8 ops) {
    state.r[ops.d] = ops.k;
}

void executeLD_X(AvrState& state, OpsLdSt ops) {
    uint16_t X = readRegWord(state, 26);

    if (ops.mode == 2) {  // Pre-decrement: LD Rd, -X
        X--;
        writeRegWord(state, 26, X);
    }

    state.r[ops.d] = readDataByte(state, X);

    if (ops.mode == 1) {  // Post-increment: LD Rd, X+
        X++;
        writeRegWord(state, 26, X);
    }
}

void executeLD_Y(AvrState& state, OpsLdSt ops) {
    uint16_t Y = readRegWord(state, 28);

    if (ops.mode == 2) {  // Pre-decrement
        Y--;
        writeRegWord(state, 28, Y);
    }

    state.r[ops.d] = readDataByte(state, Y);

    if (ops.mode == 1) {  // Post-increment
        Y++;
        writeRegWord(state, 28, Y);
    }
}

void executeLDD_Y(AvrState& state, OpsLdd ops) {
    uint16_t Y = readRegWord(state, 28);
    state.r[ops.d] = readDataByte(state, Y + ops.q);
}

void executeLD_Z(AvrState& state, OpsLdSt ops) {
    uint16_t Z = readRegWord(state, 30);

    if (ops.mode == 2) {  // Pre-decrement
        Z--;
        writeRegWord(state, 30, Z);
    }

    state.r[ops.d] = readDataByte(state, Z);

    if (ops.mode == 1) {  // Post-increment
        Z++;
        writeRegWord(state, 30, Z);
    }
}

void executeLDD_Z(AvrState& state, OpsLdd ops) {
    uint16_t Z = readRegWord(state, 30);
    state.r[ops.d] = readDataByte(state, Z + ops.q);
}

void executeLDS(AvrState& state, OpsLdsSts ops) {
    state.r[ops.d] = readDataByte(state, ops.addr);
}

void executeLPM(AvrState& state, OpsRd ops, uint8_t mode) {
    uint16_t Z = readRegWord(state, 30);
    if (Z >= AVR_FLASH_SIZE) {
      emuErrorPcAddr(emuFaultPc(), Z, "LPM", "flash read out of bounds");
      memorySignalFault();
      return;
    }
    state.r[ops.d] = state.flash[Z];
    if (mode == 1) {
        Z++;
        writeRegWord(state, 30, Z);
    }
}

void executeIN(AvrState& state, OpsRdIO ops) {
    state.r[ops.d] = readDataByte(state, ops.a + 0x20);
}

void executeOUT(AvrState& state, OpsIORr ops) {
    writeDataByte(state, ops.a + 0x20, state.r[ops.r]);
}

void executePOP(AvrState& state, OpsRd ops) {
    state.r[ops.d] = popByte(state);
}

void executePUSH(AvrState& state, OpsRd ops) {
    pushByte(state, state.r[ops.d]);
}

void executeST_X(AvrState& state, OpsLdSt ops) {
    uint16_t X = readRegWord(state, 26);

    if (ops.mode == 2) {  // ST -X, Rr
        X--;
        writeRegWord(state, 26, X);
    }

    writeDataByte(state, X, state.r[ops.d]);

    if (ops.mode == 1) {  // ST X+, Rr
        X++;
        writeRegWord(state, 26, X);
    }
}

void executeST_Y(AvrState& state, OpsLdSt ops) {
    uint16_t Y = readRegWord(state, 28);

    if (ops.mode == 2) {
        Y--;
        writeRegWord(state, 28, Y);
    }

    writeDataByte(state, Y, state.r[ops.d]);

    if (ops.mode == 1) {
        Y++;
        writeRegWord(state, 28, Y);
    }
}

void executeSTD_Y(AvrState& state, OpsLdd ops) {
    uint16_t Y = readRegWord(state, 28);
    writeDataByte(state, Y + ops.q, state.r[ops.d]);
}

void executeST_Z(AvrState& state, OpsLdSt ops) {
    uint16_t Z = readRegWord(state, 30);

    if (ops.mode == 2) {
        Z--;
        writeRegWord(state, 30, Z);
    }

    writeDataByte(state, Z, state.r[ops.d]);

    if (ops.mode == 1) {
        Z++;
        writeRegWord(state, 30, Z);
    }
}

void executeSTD_Z(AvrState& state, OpsLdd ops) {
    uint16_t Z = readRegWord(state, 30);
    writeDataByte(state, Z + ops.q, state.r[ops.d]);
}

void executeSTS(AvrState& state, OpsLdsSts ops) {
    writeDataByte(state, ops.addr, state.r[ops.d]);
}

// ===========================================================================
// Branch / Jump / Call instructions
// ===========================================================================

void executeBRBC(AvrState& state, OpsK7 ops) {
    if (!((state.sreg >> ops.s) & 1)) {
        // k is a 7-bit signed offset in 16-bit words; PC already points past the branch
        state.pc += static_cast<int16_t>(ops.k) * 2;
    }
}

void executeBRBS(AvrState& state, OpsK7 ops) {
    if ((state.sreg >> ops.s) & 1) {
        state.pc += static_cast<int16_t>(ops.k) * 2;
    }
}

void executeRJMP(AvrState& state, OpsK02 ops) {
    // k is a 12-bit signed offset in 16-bit words; PC already points past RJMP
    state.pc += static_cast<int16_t>(ops.k) * 2;
}

void executeJMP(AvrState& state, OpsK22 ops) {
    state.pc = (uint16_t)(ops.k * 2);  // k in words, PC in bytes
}

void executeIJMP(AvrState& state) {
    state.pc = readRegWord(state, 30) * 2;  // Z is a word address; PC is in bytes
}

void executeRCALL(AvrState& state, OpsK02 ops) {
    pushWord(state, state.pc);
    state.pc += static_cast<int16_t>(ops.k) * 2;
}

void executeCALL(AvrState& state, OpsK22 ops) {
    pushWord(state, state.pc);
    state.pc = (uint16_t)(ops.k * 2);
}

void executeICALL(AvrState& state) {
    pushWord(state, state.pc);
    state.pc = readRegWord(state, 30) * 2;  // Z is a word address; PC is in bytes
}

void executeRET(AvrState& state) {
    state.pc = popWord(state);
}

void executeRETI(AvrState& state) {
    state.pc = popWord(state);
    setFlag(state, SregBit::I);
}

// ===========================================================================
// Skip instructions
// ===========================================================================

static void skipNextInstruction(AvrState& state, const char* context) {
    if (state.pc + 1 >= AVR_FLASH_SIZE) {
      emuErrorPc(state.pc, context);
      memorySignalFault();
      return;
    }
    uint16_t nextInstr = state.flash[state.pc] | (state.flash[state.pc + 1] << 8);
    Opcode nextOp;
    if (!decodeInstruction(nextInstr, nextOp)) {
      emuErrorPcInstr(state.pc, nextInstr, context);
      memorySignalFault();
      return;
    }
    state.pc += nextOp.words * 2;
}

void executeSBIC(AvrState& state, OpsIOB ops) {
    if (!getIOBit(state, ops.a, ops.b)) {
        skipNextInstruction(state, "SBIC skip: unknown next instruction");
    }
}

void executeSBIS(AvrState& state, OpsIOB ops) {
    if (getIOBit(state, ops.a, ops.b)) {
        skipNextInstruction(state, "SBIS skip: unknown next instruction");
    }
}

void executeSBRC(AvrState& state, OpsRrB ops) {
    if (!((state.r[ops.r] >> ops.b) & 1)) {
        skipNextInstruction(state, "SBRC skip: unknown next instruction");
    }
}

void executeSBRS(AvrState& state, OpsRrB ops) {
    if ((state.r[ops.r] >> ops.b) & 1) {
        skipNextInstruction(state, "SBRS skip: unknown next instruction");
    }
}

// ===========================================================================
// Bit manipulation instructions
// ===========================================================================

void executeBSET(AvrState& state, OpsBOnly ops) {
    setFlag(state, static_cast<SregBit>(ops.b));
}

void executeBCLR(AvrState& state, OpsBOnly ops) {
    clearFlag(state, static_cast<SregBit>(ops.b));
}

void executeBLD(AvrState& state, OpsRdB ops) {
    if (getFlag(state, SregBit::T))
        state.r[ops.d] |= (1 << ops.b);
    else
        state.r[ops.d] &= ~(1 << ops.b);
}

void executeBST(AvrState& state, OpsRdB ops) {
    if ((state.r[ops.d] >> ops.b) & 1)
        setFlag(state, SregBit::T);
    else
        clearFlag(state, SregBit::T);
}

void executeCBI(AvrState& state, OpsIOB ops) {
    clearIOBit(state, ops.a, ops.b);
}

void executeSBI(AvrState& state, OpsIOB ops) {
    setIOBit(state, ops.a, ops.b);
}

// ===========================================================================
// MCU control instructions
// ===========================================================================

void executeNOP(AvrState& state) {
    (void)state;  // No operation
}

void executeSLEEP(AvrState& state) {
    (void)state;  // For emulator: treat as NOP (full peripheral integration out of scope)
}

void executeWDR(AvrState& state) {
    (void)state;  // Reset watchdog timer — for emulator, treat as NOP
}

void executeBREAK(AvrState& state) {
    (void)state;  // Without debug system enabled: acts as NOP
}

void executeSPM(AvrState& state) {
    uint16_t Z = readRegWord(state, 30);
    if (Z + 1 >= AVR_FLASH_SIZE) return;
    state.flash[Z]     = state.r[0];
    state.flash[Z + 1] = state.r[1];
    // Note: Full SPM with SPMCSR and page buffer is deferred for bootloader support.
    // This minimal implementation writes R1:R0 directly to flash at Z.
}

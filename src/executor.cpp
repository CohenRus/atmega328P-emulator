#include "executor.h"

bool executeProgram(AvrState& state) {
  clearState(state);

  bool running = true;
  uint16_t instruction;
  while (running) {
    if (state.pc > AVR_FLASH_SIZE) {
      std::cout << "end of memory reached" << std::endl;
      return false;
    }
    // each instruction is two bytes, stored low byte then high byte
    instruction = state.flash[state.pc] | state.flash[state.pc + 1] << 8;
    state.pc += 2;

    Opcode op = decodeInstruction(instruction);
    std::cout << op.mnemonic << std::endl;

    // read second word for 32-bit instructions before executing
    uint16_t extra = 0;
    if (op.words == 2) {
      extra = state.flash[state.pc] | state.flash[state.pc + 1] << 8;
      state.pc += 2;
    }

    if (!executeInstruction(state, op, instruction, extra))
      return false;
  }
  return true;
}

// do any inital setup of memory state
bool clearState(AvrState& state) {
  // registers
  for (uint8_t i = 0; i < 32; ++i) state.r[i] = 0;
  state.pc   = 0;
  state.sreg = 0;
  state.sp   = 0;

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
      std::cerr << "unknown opcode" << std::endl;
      return false;
  }
  return true;
}

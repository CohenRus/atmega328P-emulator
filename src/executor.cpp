// ===========================================================================
// executor.cpp — ATmega328P instruction execution engine.
//
// Executes decoded AVR instructions against the emulator's runtime state.
// Contains the main fetch-decode-execute loop, instruction implementations
// with full SREG flag computation, and peripheral timing integration
// (Timer0 tick, UART poll, interrupt servicing, wall-clock sync).
//
// Instruction aliases (LSL→ADD, ROL→ADC, TST→AND, CLR→EOR, CBR→ANDI, SBR→ORI)
// dispatch to the canonical implementation — only one code path per operation.
// ===========================================================================

#include "executor.h"
#include "error.h"
#include "interrupt.h"
#include "memory.h"
#include "timer0.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>
// ATmega328P nominal clock: 16 MHz
#define AVR_CPU_HZ 16000000ULL

// Cooperative stop flag.  Set to true from any thread to request the
// execution loop to exit cleanly at the next iteration boundary.
std::atomic<bool> g_emu_stop(false);
// ---------------------------------------------------------------------------
// SREG bit indices (matches ATmega328P datasheet §6.3)
// ---------------------------------------------------------------------------
#define SREG_C  0  // Carry
#define SREG_Z  1  // Zero
#define SREG_N  2  // Negative
#define SREG_V  3  // Two's complement overflow
#define SREG_S  4  // Sign flag (N ⊕ V)
#define SREG_H  5  // Half carry
#define SREG_T  6  // Transfer bit (used by BLD/BST)
#define SREG_I  7  // Global interrupt enable

static void skipNextInstruction(AvrState& state, const char* context);

// ===========================================================================
// Main execution loop
// ===========================================================================

// Runs the fetch-decode-execute cycle indefinitely.  Each iteration:
//   1. Polls UART for incoming bytes (non-blocking).
//   2. Checks peripheral flags (Timer0 overflow/compare-match, UART RX/TX/UDRE)
//      and raises pending interrupt vectors.
//   3. Services any pending interrupts via the interrupt controller (may dispatch
//      and skip instruction fetch if an interrupt fires).
//   4. Fetches the next 16-bit instruction word from flash at state.pc.
//   5. Decodes it; reads a second word if it's a 32-bit instruction.
//   6. Clears memory fault, executes the instruction, checks for faults.
//   7. Advances cycle count and peripheral timing (Timer0).
//   8. Synchronizes with wall-clock to match real-time execution speed.
// @param state — the complete emulator runtime state (modified in place)
// @return false on fatal error (bad PC, unknown opcode, memory fault);
//         true on clean exit (g_emu_stop set)
bool executeProgram(AvrState& state) {
  // State (registers, PC, SP, SRAM) is already initialized by the caller.
  // Only peripheral state needs resetting here.
  uartInit();
  timer0SetState(&state);
  timer0Reset();
  interruptSetState(&state);
  interruptReset();

  // Wall-clock anchor for real-time synchronization.
  auto wall_start = std::chrono::steady_clock::now();
  uint16_t instruction;

  while (true) {
    // ── Cooperative stop check ──
    if (g_emu_stop.load(std::memory_order_relaxed)) {
        return true;  // clean exit requested by TUI
    }

    uartPoll();
    // After each instruction, check for peripheral interrupt conditions
    // and raise the corresponding interrupt vectors.  These fire at the
    // top of the next instruction cycle so the PC pushed on dispatch
    // points to the next instruction that would have executed.
    if (timer0OverflowPending()) {
        interruptRaise(InterruptVector::TIMER0_OVF);
    }
    if (timer0CompAPending()) {
        interruptRaise(InterruptVector::TIMER0_COMPA);
    }
    if (timer0CompBPending()) {
        interruptRaise(InterruptVector::TIMER0_COMPB);
    }

    // ── Service pending interrupts ──
    if (interruptService()) {
        // Hardware auto-clears the TIFR flag on dispatch.
        // Acknowledge all Timer0 flags — the one that fired will be cleared;
        // others are unaffected by the &= ~mask.
        timer0AckOverflow();
        timer0AckCompA();
        timer0AckCompB();
        continue;
    }
    // ── Fetch ──
    if (state.pc + 1 >= AVR_FLASH_SIZE) {
      emuErrorPc(state.pc, "program counter out of flash bounds");
      return false;
    }
    const uint16_t instrPc = state.pc;
    emuSetFaultPc(instrPc);
    instruction = state.flash[state.pc] | (state.flash[state.pc + 1] << 8);
    emuSetFaultInstr(instruction);
    state.pc += 2;
    Opcode op;
    if (!decodeInstruction(instruction, op)) {
      emuErrorPcInstr(instrPc, instruction, "unknown opcode (not in decoder table)");
      return false;
    }
    // ── Fetch second word for 32-bit instructions ──
    uint16_t extra = 0;
    if (op.words == 2) {
      if (state.pc + 1 >= AVR_FLASH_SIZE) {
        emuErrorPc(state.pc, "32-bit instruction extends past end of flash");
        return false;
      }
      extra = state.flash[state.pc] | (state.flash[state.pc + 1] << 8);
      state.pc += 2;
    }

    // ── Execute ──
    memoryClearFault();
    state.extra_cycles = 0;
    if (!executeInstruction(state, op, instruction, extra)) {
      return false;
    }
    if (memoryFaultPending()) {
      return false;
    }

    // ── Advance peripheral timing ──
    uint16_t total_cycles = op.cycles_min + state.extra_cycles;
    state.cycle_count += total_cycles;
    timer0Tick(total_cycles);

    // ── Wall-clock synchronization ──
    // Sleep only when more than 100 µs ahead to balance precision vs syscall cost.
    {
        auto target_wall = wall_start + std::chrono::microseconds(
            state.cycle_count * 1000000ULL / AVR_CPU_HZ);
        auto now = std::chrono::steady_clock::now();
        auto ahead = target_wall - now;
        if (ahead > std::chrono::microseconds(100)) {
            std::this_thread::sleep_until(target_wall);
        }
    }
  }
}

// ===========================================================================
// State initialization
// ===========================================================================

// Resets the emulator to power-on state.
// All 32 GP registers → 0, PC → 0, SREG → 0,
// SP → 0x08FF (top of SRAM, as per ATmega328P reset),
// all SRAM and EEPROM bytes → 0.
//
// @param state — runtime state to clear
// @return always true
bool clearState(AvrState& state) {
  // Zero all general-purpose registers R0–R31.
  for (uint8_t i = 0; i < 32; ++i) state.r[i] = 0;
  state.pc   = 0;
  state.sreg = 0;
  // SP initializes to RAMEND (0x08FF for ATmega328P).
  state.sp   = 0x08FF;
  state.cycle_count = 0;
  state.extra_cycles = 0;
  // SRAM is NOT zeroed here — firmware .data/.bss is loaded by
  // loadFirmware and/or the CRT startup code.  Zeroing it would
  // wipe initialized globals.
  for (int i = 0; i < AVR_EEPROM_SIZE; ++i) state.eeprom[i] = 0;
  return true;
}

// ===========================================================================
// Instruction dispatch
//
// Decodes operands from the raw instruction word(s) based on op.fmt,
// then calls the matching executor function.  Instruction aliases
// (LSL→ADD, ROL→ADC, CLR→EOR, TST→AND, CBR→ANDI, SBR→ORI) share
// the same decode path and dispatch to the canonical implementation —
// only one code path exists per operation.
//
// @param state      — emulator runtime state
// @param op         — decoded opcode entry (op, fmt, words, cycles)
// @param instr      — raw 16-bit instruction word
// @param secondWord — second 16-bit word for 32-bit instructions, or 0
// @return true on success, false on unimplemented opcode
// ===========================================================================
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
    case AvrOp::CBR:    executeANDI(state,   decodeRdK8(instr));           break; // CBR = ANDI with ~K
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
      // LDD_family format: LD Rd, Y+q (displacement); LD_family: LD Rd, Y / Y+ / -Y
      if (op.fmt == AvrFmt::LDD_family) executeLDD_Y(state, decodeLdd(instr));
      else                              executeLD_Y(state,   decodeLdSt(instr));
      break;

    case AvrOp::LD_Z:
      if (op.fmt == AvrFmt::LDD_family) executeLDD_Z(state, decodeLdd(instr));
      else                              executeLD_Z(state,   decodeLdSt(instr));
      break;

    case AvrOp::LDS:    executeLDS(state,    decodeLdsSts(instr, secondWord)); break;

    // LPM with NONE fmt is the implicit form (LPM R0, Z — no register operand).
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

    // Named branch aliases all encode the SREG bit index in instr[2:0],
    // so decodeK7 extracts the correct bit for BRCC/BRNE/etc.
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

    // SREG flag set/clear aliases — bit index is encoded in instr[6:4].
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

// ADC — Add with Carry
// Adds two registers and the Carry flag.  Rd ← Rd + Rr + C.
// Used for multi-byte addition chains (ADD for LSB, ADC for subsequent bytes).
//
// SREG affected: H (half-carry from bit 3), V (two's complement overflow),
//     N (result negative), Z (result zero), C (carry from bit 7),
//     S (N ⊕ V).  T and I are preserved.
//
// @param ops.d — destination register (Rd); also the first source operand
// @param ops.r — source register (Rr); the second source operand
void executeADC(AvrState& state, OpsRdRr ops) {
    uint8_t rd = state.r[ops.d];
    uint8_t rr = state.r[ops.r];
    uint8_t ci = (state.sreg >> SREG_C) & 1;        // carry-in (0 or 1)
    uint16_t result16 = (uint16_t)rd + (uint16_t)rr + ci;  // 9-bit sum
    uint8_t result8 = (uint8_t)result16;

    // H: half-carry — set when low nibble sum exceeds 0x0F.
    bool h = ((rd & 0x0F) + (rr & 0x0F) + ci) > 0x0F;
    // V: overflow — sign of operands differs from sign of result.
    bool v = ((rd ^ result8) & (rr ^ result8) & 0x80) != 0;
    bool n = (result8 & 0x80) != 0;
    bool z = result8 == 0;
    bool c = result16 > 0xFF;                         // carry-out from bit 7
    bool s = n ^ v;

    state.r[ops.d] = result8;
    // Preserve T (bit 6) and I (bit 7); update H, V, N, Z, C, S.
    state.sreg = (state.sreg & 0xC0) | (s << SREG_S) | (v << SREG_V)
               | (n << SREG_N) | (z << SREG_Z) | (c << SREG_C) | (h << SREG_H);
}

// ADD — Add without Carry
// Adds two registers without the Carry flag.  Rd ← Rd + Rr.
// Also used as LSL (Logical Shift Left): LSL Rd,N = ADD Rd,Rd repeated N times.
//
// SREG affected: H, V, N, Z, C, S.  T and I are preserved.
//
// @param ops.d — destination register (also first source)
// @param ops.r — source register (second source)
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

// ADIW — Add Immediate to Word
// Adds a 6-bit immediate (0–63) to a register pair (R25:R24, R27:R26,
// R29:R28, or R31:R30).  Rd+1:Rd ← Rd+1:Rd + K.
// Used for pointer arithmetic on X/Y/Z and the high register pair.
//
// SREG affected: V, N, Z, C, S.  H is NOT affected.  T and I are preserved.
//
// @param ops.d — even register index (24, 26, 28, or 30)
// @param ops.k — 6-bit unsigned immediate (0–63)
void executeADIW(AvrState& state, OpsRd06K6 ops) {
    uint8_t  lo = state.r[ops.d];                    // low byte of register pair
    uint8_t  hi = state.r[ops.d + 1];                // high byte
    uint16_t word = ((uint16_t)hi << 8) | lo;
    uint16_t result16 = word + ops.k;

    // V: set if the high byte's sign bit changed from 1 to 0 (positive overflow).
    bool v = (hi & 0x80) && !(result16 & 0x8000);
    bool n = (result16 & 0x8000) != 0;
    bool z = result16 == 0;
    // C: set if there was no carry out of bit 15 (i.e., result MSB is 0 but source MSB was 1).
    bool c = !(result16 & 0x8000) && (hi & 0x80);
    bool s = n ^ v;

    state.r[ops.d]     = (uint8_t)(result16 & 0xFF);
    state.r[ops.d + 1] = (uint8_t)(result16 >> 8);
    // Preserve H, T, I.
    state.sreg = (state.sreg & 0xE0) | (s << SREG_S) | (v << SREG_V)
               | (n << SREG_N) | (z << SREG_Z) | (c << SREG_C);
}

// ASR — Arithmetic Shift Right
// Shifts a register right by one bit.  Bit 7 (sign) is held constant.
// Rd ← (Rd >> 1) | (Rd & 0x80).  Effectively Rd ← Rd / 2 (signed).
//
// SREG affected: V (N ⊕ C), N (result sign), Z (result zero), C (bit shifted out),
//     S (N ⊕ V).  H, T, I are preserved.
//
// @param ops.d — register to shift
void executeASR(AvrState& state, OpsRd ops) {
    uint8_t rd = state.r[ops.d];
    bool c = rd & 0x01;                              // bit shifted out → Carry
    uint8_t result8 = (rd >> 1) | (rd & 0x80);       // preserve sign bit (bit 7)
    bool n = (result8 & 0x80) != 0;
    bool z = result8 == 0;
    bool v = n ^ c;
    bool s = n ^ v;

    state.r[ops.d] = result8;
    // Preserve H, T, I.
    state.sreg = (state.sreg & 0xE0) | (s << SREG_S) | (v << SREG_V)
               | (n << SREG_N) | (z << SREG_Z) | (c << SREG_C);
}

// DEC — Decrement
// Decrements a register by one.  Rd ← Rd - 1.
// C is NOT affected (unlike SUB).  V set only on overflow from 0x80 → 0x7F.
//
// SREG affected: V, N, Z, S.  C, H, T, I are preserved.
//
// @param ops.d — register to decrement
void executeDEC(AvrState& state, OpsRd ops) {
    uint8_t rd = state.r[ops.d];
    uint8_t result8 = rd - 1;
    // V: overflow only when going from 0x80 (-128) to 0x7F (+127).
    bool v = rd == 0x80;
    bool n = (result8 & 0x80) != 0;
    bool z = result8 == 0;
    bool s = n ^ v;

    state.r[ops.d] = result8;
    // Preserve C, H, T, I.
    state.sreg = (state.sreg & 0xE1) | (s << SREG_S) | (v << SREG_V)
               | (n << SREG_N) | (z << SREG_Z);
}

// INC — Increment
// Increments a register by one.  Rd ← Rd + 1.
// C is NOT affected (unlike ADD).  V set only on overflow from 0x7F → 0x80.
//
// SREG affected: V, N, Z, S.  C, H, T, I are preserved.
//
// @param ops.d — register to increment
void executeINC(AvrState& state, OpsRd ops) {
    uint8_t rd = state.r[ops.d];
    uint8_t result8 = rd + 1;
    // V: overflow only when going from 0x7F (+127) to 0x80 (-128).
    bool v = rd == 0x7F;
    bool n = (result8 & 0x80) != 0;
    bool z = result8 == 0;
    bool s = n ^ v;

    state.r[ops.d] = result8;
    state.sreg = (state.sreg & 0xE1) | (s << SREG_S) | (v << SREG_V)
               | (n << SREG_N) | (z << SREG_Z);
}

// MUL — Multiply Unsigned
// Multiplies two unsigned 8-bit registers.  Result is stored in R1:R0
// (R1 = high byte, R0 = low byte).  Rd × Rr → R1:R0.
//
// SREG affected: Z (result zero), C (result bit 15).  H, V, N, S, T, I are preserved.
//
// @param ops.d — multiplicand register
// @param ops.r — multiplier register
void executeMUL(AvrState& state, OpsRdRr ops) {
    uint16_t result16 = (uint16_t)state.r[ops.d] * (uint16_t)state.r[ops.r];
    state.r[0] = (uint8_t)(result16 & 0xFF);         // R0 ← low byte
    state.r[1] = (uint8_t)(result16 >> 8);           // R1 ← high byte
    bool c = (result16 & 0x8000) != 0;               // C ← bit 15 of result
    bool z = result16 == 0;
    // Preserve H, V, N, S, T, I; update only Z and C.
    state.sreg = (state.sreg & 0xFC) | (z << SREG_Z) | (c << SREG_C);
}

// MULS — Multiply Signed
// Multiplies two signed 8-bit registers from R16–R31.  Result in R1:R0.
// Rd (signed) × Rr (signed) → R1:R0.
//
// SREG affected: Z, C (result bit 15).  All others preserved.
//
// @param ops.d — register index offset from R16 (0–15, maps to R16–R31)
// @param ops.r — register index offset from R16 (0–15, maps to R16–R31)
void executeMULS(AvrState& state, OpsRd06Rr06 ops) {
    int8_t rd = (int8_t)state.r[ops.d + 16];         // sign-extend to signed
    int8_t rr = (int8_t)state.r[ops.r + 16];
    int16_t result16 = (int16_t)rd * (int16_t)rr;
    state.r[0] = (uint8_t)((uint16_t)result16 & 0xFF);
    state.r[1] = (uint8_t)(((uint16_t)result16 >> 8) & 0xFF);
    bool c = (result16 & 0x8000) != 0;
    bool z = result16 == 0;
    state.sreg = (state.sreg & 0xFC) | (z << SREG_Z) | (c << SREG_C);
}

// MULSU — Multiply Signed with Unsigned
// Multiplies a signed register (R16–R23) with an unsigned register (R16–R23).
// Rd (signed) × Rr (unsigned) → R1:R0.
//
// SREG affected: Z, C.  All others preserved.
//
// @param ops.d — signed multiplicand (R16–R23)
// @param ops.r — unsigned multiplier (R16–R23)
void executeMULSU(AvrState& state, OpsRdRrMpy ops) {
    int8_t  rd = (int8_t)state.r[ops.d];
    uint8_t rr = state.r[ops.r];
    // Multiply as signed × unsigned by zero-extending the unsigned operand.
    int16_t result16 = (int16_t)rd * (int16_t)((uint16_t)rr);
    state.r[0] = (uint8_t)((uint16_t)result16 & 0xFF);
    state.r[1] = (uint8_t)(((uint16_t)result16 >> 8) & 0xFF);
    bool c = (result16 & 0x8000) != 0;
    bool z = result16 == 0;
    state.sreg = (state.sreg & 0xFC) | (z << SREG_Z) | (c << SREG_C);
}

// FMUL — Fractional Multiply Unsigned
// Multiplies two unsigned 1.7 fixed-point registers and left-shifts by 1.
// Result: (Rd × Rr) << 1 → R1:R0.  Used for fixed-point DSP.
//
// SREG affected: Z, C.  All others preserved.
//
// @param ops.d — unsigned operand (R16–R23)
// @param ops.r — unsigned operand (R16–R23)
void executeFMUL(AvrState& state, OpsRdRrMpy ops) {
    uint16_t product = (uint16_t)state.r[ops.d] * (uint16_t)state.r[ops.r];
    uint16_t result16 = product << 1;               // fractional shift
    state.r[0] = (uint8_t)(result16 & 0xFF);
    state.r[1] = (uint8_t)(result16 >> 8);
    bool c = (result16 & 0x8000) != 0;
    bool z = result16 == 0;
    state.sreg = (state.sreg & 0xFC) | (z << SREG_Z) | (c << SREG_C);
}

// FMULS — Fractional Multiply Signed
// Multiplies two signed 1.7 fixed-point registers and left-shifts by 1.
// Result: (Rd × Rr) << 1 → R1:R0.
//
// SREG affected: Z, C.  All others preserved.
//
// @param ops.d — signed operand (R16–R23)
// @param ops.r — signed operand (R16–R23)
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

// FMULSU — Fractional Multiply Signed with Unsigned
// Multiplies a signed 1.7 fixed-point register with an unsigned one,
// left-shifts by 1.  Result: (Rd × Rr) << 1 → R1:R0.
//
// SREG affected: Z, C.  All others preserved.
//
// @param ops.d — signed operand (R16–R23)
// @param ops.r — unsigned operand (R16–R23)
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

// NEG — Two's Complement
// Replaces a register with its two's complement.  Rd ← 0x00 - Rd.
// Equivalent to Rd ← -Rd (signed) or Rd ← ~Rd + 1.
//
// SREG affected: H (borrow from bit 3), V (overflow: Rd == 0x80),
//     N, Z, C (set if result != 0; i.e., cleared only when Rd == 0x00),
//     S (N ⊕ V).  T and I are preserved.
//
// @param ops.d — register to negate
void executeNEG(AvrState& state, OpsRd ops) {
    uint8_t rd = state.r[ops.d];
    uint8_t result8 = (uint8_t)(-(int8_t)rd);        // two's complement via signed cast
    // H: set if there was a borrow from bit 3.
    bool h = ((result8 & 0x08) != 0) || ((rd & 0x08) != 0);
    // V: overflow only for 0x80 (which has no positive counterpart).
    bool v = rd == 0x80;
    bool n = (result8 & 0x80) != 0;
    bool z = result8 == 0;
    // C: set for all non-zero results (cleared only for NEG 0x00).
    bool c = result8 != 0x00;
    bool s = n ^ v;

    state.r[ops.d] = result8;
    state.sreg = (state.sreg & 0xC0) | (s << SREG_S) | (v << SREG_V)
               | (n << SREG_N) | (z << SREG_Z) | (c << SREG_C) | (h << SREG_H);
}

// SBIW — Subtract Immediate from Word
// Subtracts a 6-bit immediate (0–63) from a register pair (R25:R24, R27:R26,
// R29:R28, or R31:R30).  Rd+1:Rd ← Rd+1:Rd - K.
//
// SREG affected: V, N, Z, C, S.  H is NOT affected.  T and I are preserved.
//
// @param ops.d — even register index (24, 26, 28, or 30)
// @param ops.k — 6-bit unsigned immediate (0–63)
void executeSBIW(AvrState& state, OpsRd06K6 ops) {
    uint8_t  lo = state.r[ops.d];
    uint8_t  hi = state.r[ops.d + 1];
    uint16_t word = ((uint16_t)hi << 8) | lo;
    uint16_t result16 = word - ops.k;

    // V: set if sign bit changed from 0 to 1 (underflow to negative).
    bool v = (hi & 0x80) && (result16 & 0x8000);
    bool n = (result16 & 0x8000) != 0;
    bool z = result16 == 0;
    // C: set if there was a borrow (result MSB became 1 when source MSB was 0).
    bool c = (result16 & 0x8000) && (hi & 0x80);
    bool s = n ^ v;

    state.r[ops.d]     = (uint8_t)(result16 & 0xFF);
    state.r[ops.d + 1] = (uint8_t)(result16 >> 8);
    state.sreg = (state.sreg & 0xE0) | (s << SREG_S) | (v << SREG_V)
               | (n << SREG_N) | (z << SREG_Z) | (c << SREG_C);
}

// SBC — Subtract with Carry
// Subtracts a register and the Carry flag from a register.  Rd ← Rd - Rr - C.
// Used for multi-byte subtraction chains (SUB for LSB, SBC for subsequent bytes).
//
// SREG affected: H, V, N, Z, C, S.  T and I are preserved.
//
// @param ops.d — destination register (minuend)
// @param ops.r — source register (subtrahend)
void executeSBC(AvrState& state, OpsRdRr ops) {
    uint8_t rd = state.r[ops.d];
    uint8_t rr = state.r[ops.r];
    uint8_t ci = (state.sreg >> SREG_C) & 1;        // carry-in (borrow)
    uint16_t result16 = (uint16_t)rd - (uint16_t)rr - ci;
    uint8_t result8 = (uint8_t)result16;

    // H: half-carry borrow — set when low nibble borrows from bit 4.
    bool h = ((rd & 0x0F) < ((rr & 0x0F) + ci));
    // V: two's complement overflow on subtraction.
    bool v = ((rd ^ rr) & (rd ^ result8) & 0x80) != 0;
    bool n = (result8 & 0x80) != 0;
    bool z = result8 == 0;
    // C: set when result underflows (borrow from bit 7).
    bool c = result16 > 0xFF;
    bool s = n ^ v;

    state.r[ops.d] = result8;
    state.sreg = (state.sreg & 0xC0) | (s << SREG_S) | (v << SREG_V)
               | (n << SREG_N) | (z << SREG_Z) | (c << SREG_C) | (h << SREG_H);
}

// SBCI — Subtract Immediate with Carry
// Subtracts an 8-bit constant and the Carry flag from a register.
// Rd ← Rd - K - C.  Rd must be R16–R31.
//
// SREG affected: H, V, N, Z, C, S.  T and I are preserved.
//
// @param ops.d — destination register (R16–R31)
// @param ops.k — 8-bit unsigned immediate
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

// SUB — Subtract without Carry
// Subtracts a register from a register.  Rd ← Rd - Rr.
//
// SREG affected: H, V, N, Z, C, S.  T and I are preserved.
//
// @param ops.d — destination register (minuend)
// @param ops.r — source register (subtrahend)
void executeSUB(AvrState& state, OpsRdRr ops) {
    uint8_t rd = state.r[ops.d];
    uint8_t rr = state.r[ops.r];
    uint16_t result16 = (uint16_t)rd - (uint16_t)rr;
    uint8_t result8 = (uint8_t)result16;

    bool h = (rd & 0x0F) < (rr & 0x0F);              // borrow from low nibble
    bool v = ((rd ^ rr) & (rd ^ result8) & 0x80) != 0;
    bool n = (result8 & 0x80) != 0;
    bool z = result8 == 0;
    bool c = result16 > 0xFF;                         // borrow from bit 7
    bool s = n ^ v;

    state.r[ops.d] = result8;
    state.sreg = (state.sreg & 0xC0) | (s << SREG_S) | (v << SREG_V)
               | (n << SREG_N) | (z << SREG_Z) | (c << SREG_C) | (h << SREG_H);
}

// SUBI — Subtract Immediate
// Subtracts an 8-bit constant from a register (R16–R31).  Rd ← Rd - K.
//
// SREG affected: H, V, N, Z, C, S.  T and I are preserved.
//
// @param ops.d — destination register (R16–R31)
// @param ops.k — 8-bit unsigned immediate
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

// AND — Logical AND
// Performs bitwise AND between two registers.  Rd ← Rd & Rr.
// Also used as TST (Test): TST Rd = AND Rd,Rd → sets flags without modifying Rd.
//
// SREG affected: V (cleared), N (bit 7 of result), Z (result zero),
//     S (N ⊕ V = N, since V=0).  C, H, T, I are preserved.
//
// @param ops.d — destination register (also first source)
// @param ops.r — second source register
void executeAND(AvrState& state, OpsRdRr ops) {
    uint8_t result8 = state.r[ops.d] & state.r[ops.r];
    bool n = (result8 & 0x80) != 0;
    bool z = result8 == 0;
    // V is always cleared by logical instructions.
    state.r[ops.d] = result8;
    state.sreg = (state.sreg & 0xE4) | (n << SREG_S) | (n << SREG_N) | (z << SREG_Z);
}

// ANDI — Logical AND with Immediate
// Performs bitwise AND between a register (R16–R31) and an 8-bit constant.
// Rd ← Rd & K.  Also used as CBR (Clear Bits in Register): CBR Rd,K = ANDI Rd,~K.
//
// SREG affected: V (cleared), N, Z, S.  C, H, T, I are preserved.
//
// @param ops.d — destination register (R16–R31)
// @param ops.k — 8-bit immediate mask
void executeANDI(AvrState& state, OpsRdK8 ops) {
    uint8_t result8 = state.r[ops.d] & ops.k;
    bool n = (result8 & 0x80) != 0;
    bool z = result8 == 0;
    state.r[ops.d] = result8;
    state.sreg = (state.sreg & 0xE4) | (n << SREG_S) | (n << SREG_N) | (z << SREG_Z);
}

// COM — One's Complement
// Inverts all bits of a register.  Rd ← 0xFF - Rd (equivalent to Rd ← ~Rd).
//
// SREG affected: V (cleared), C (set), N, Z, S (N ⊕ V = N).
//     H is handled by Rd_Rr format but V/C dominate the mask.  T and I are preserved.
//
// @param ops.d — register to complement
void executeCOM(AvrState& state, OpsRd ops) {
    uint8_t result8 = 0xFF - state.r[ops.d];          // one's complement
    bool n = (result8 & 0x80) != 0;
    bool z = result8 == 0;
    state.r[ops.d] = result8;
    // V is cleared (0); C is set (1) by COM.
    state.sreg = (state.sreg & 0xE0) | (n << SREG_S) | (n << SREG_N)
               | (z << SREG_Z) | (1 << SREG_C);
}

// EOR — Exclusive OR
// Performs bitwise XOR between two registers.  Rd ← Rd ⊕ Rr.
// Also used as CLR (Clear Register): CLR Rd = EOR Rd,Rd → Rd ← 0.
//
// SREG affected: V (cleared), N, Z, S.  C, H, T, I are preserved.
//
// @param ops.d — destination register (also first source)
// @param ops.r — second source register
void executeEOR(AvrState& state, OpsRdRr ops) {
    uint8_t result8 = state.r[ops.d] ^ state.r[ops.r];
    bool n = (result8 & 0x80) != 0;
    bool z = result8 == 0;
    state.r[ops.d] = result8;
    state.sreg = (state.sreg & 0xE4) | (n << SREG_S) | (n << SREG_N) | (z << SREG_Z);
}

// OR — Logical OR
// Performs bitwise OR between two registers.  Rd ← Rd | Rr.
//
// SREG affected: V (cleared), N, Z, S.  C, H, T, I are preserved.
//
// @param ops.d — destination register (also first source)
// @param ops.r — second source register
void executeOR(AvrState& state, OpsRdRr ops) {
    uint8_t result8 = state.r[ops.d] | state.r[ops.r];
    bool n = (result8 & 0x80) != 0;
    bool z = result8 == 0;
    state.r[ops.d] = result8;
    state.sreg = (state.sreg & 0xE4) | (n << SREG_S) | (n << SREG_N) | (z << SREG_Z);
}

// ORI — Logical OR with Immediate
// Performs bitwise OR between a register (R16–R31) and an 8-bit constant.
// Rd ← Rd | K.  Also used as SBR (Set Bits in Register): SBR Rd,K = ORI Rd,K.
//
// SREG affected: V (cleared), N, Z, S.  C, H, T, I are preserved.
//
// @param ops.d — destination register (R16–R31)
// @param ops.k — 8-bit immediate mask
void executeORI(AvrState& state, OpsRdK8 ops) {
    uint8_t result8 = state.r[ops.d] | ops.k;
    bool n = (result8 & 0x80) != 0;
    bool z = result8 == 0;
    state.r[ops.d] = result8;
    state.sreg = (state.sreg & 0xE4) | (n << SREG_S) | (n << SREG_N) | (z << SREG_Z);
}

// SER — Set all Bits in Register
// Sets all bits in a register (R16–R31).  Rd ← 0xFF.
// Encoded as LDI Rd, 0xFF but with a distinct mnemonic.
//
// SREG affected: None.  All flags are preserved.
//
// @param ops.d — destination register (R16–R31)
// @param ops.k — unused (always 0xFF at the instruction encoding level)
void executeSER(AvrState& state, OpsRdK8 ops) {
    state.r[ops.d] = 0xFF;
    // No SREG flags affected.
}

// SWAP — Swap Nibbles
// Swaps the high and low nibbles of a register.  Rd ← (Rd << 4) | (Rd >> 4).
//
// SREG affected: None.  All flags are preserved.
//
// @param ops.d — register to swap
void executeSWAP(AvrState& state, OpsRd ops) {
    uint8_t rd = state.r[ops.d];
    state.r[ops.d] = (rd << 4) | (rd >> 4);
    // No SREG flags affected.
}

// ===========================================================================
// Shift / Rotate instructions
// ===========================================================================

// Note: LSL (Logical Shift Left) dispatches to executeADD (ADD Rd,Rd).
//       ROL (Rotate Left through Carry) dispatches to executeADC (ADC Rd,Rd).

// LSR — Logical Shift Right
// Shifts a register right by one bit.  Bit 0 is shifted into C.
// Bit 7 is cleared.  Rd ← Rd >> 1.
//
// SREG affected: V (N ⊕ C), N (cleared — bit 7 always 0), Z, C (bit shifted out),
//     S (N ⊕ V).  H, T, I are preserved.
//
// @param ops.d — register to shift
void executeLSR(AvrState& state, OpsRd ops) {
    uint8_t rd = state.r[ops.d];
    bool c = rd & 0x01;                              // bit shifted out → Carry
    uint8_t result8 = rd >> 1;                       // bit 7 → 0
    bool n = false;                                   // bit 7 is always 0 after LSR
    bool z = result8 == 0;
    bool v = n ^ c;
    bool s = n ^ v;

    state.r[ops.d] = result8;
    state.sreg = (state.sreg & 0xE0) | (s << SREG_S) | (v << SREG_V)
               | (z << SREG_Z) | (c << SREG_C);
}

// ROR — Rotate Right through Carry
// Shifts a register right by one bit.  The old Carry flag is shifted into bit 7,
// and bit 0 is shifted into the new Carry flag.
// Rd ← (Rd >> 1) | (oldC << 7).
//
// SREG affected: V (N ⊕ C after rotation), N (bit 7 of result), Z, C (bit shifted out),
//     S (N ⊕ V).  H, T, I are preserved.
//
// @param ops.d — register to rotate
void executeROR(AvrState& state, OpsRd ops) {
    uint8_t rd = state.r[ops.d];
    bool oldC = (state.sreg >> SREG_C) & 1;          // previous Carry → new bit 7
    bool newC = rd & 0x01;                           // bit 0 → new Carry
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
//
// Compare instructions perform subtraction without storing the result.
// They update only the SREG flags — Rd is NOT modified.
// ===========================================================================

// CP — Compare
// Compares two registers by subtraction: Rd - Rr.  Sets flags; Rd is unchanged.
//
// SREG affected: H (borrow from bit 3), V, N, Z, C, S.  T and I are preserved.
//
// @param ops.d — first operand register (Rd)
// @param ops.r — second operand register (Rr)
void executeCP(AvrState& state, OpsRdRr ops) {
    uint8_t rd = state.r[ops.d];
    uint8_t rr = state.r[ops.r];
    uint16_t result16 = (uint16_t)rd - (uint16_t)rr;  // compute difference
    uint8_t result8 = (uint8_t)result16;

    bool h = (rd & 0x0F) < (rr & 0x0F);              // borrow from bit 3
    bool v = ((rd ^ rr) & (rd ^ result8) & 0x80) != 0;
    bool n = (result8 & 0x80) != 0;
    bool z = result8 == 0;
    bool c = result16 > 0xFF;
    bool s = n ^ v;

    // Rd is NOT written — only flags are updated.
    state.sreg = (state.sreg & 0xC0) | (s << SREG_S) | (v << SREG_V)
               | (n << SREG_N) | (z << SREG_Z) | (c << SREG_C) | (h << SREG_H);
}

// CPC — Compare with Carry
// Compares two registers with carry: Rd - Rr - C.  Sets flags; Rd is unchanged.
// Used with CP for multi-byte comparisons.
//
// SREG affected: H, V, N, C, S.  Z: previous Z is preserved if result is zero;
//     cleared otherwise.  T and I are preserved.
//
// @param ops.d — first operand register (Rd)
// @param ops.r — second operand register (Rr)
void executeCPC(AvrState& state, OpsRdRr ops) {
    uint8_t rd = state.r[ops.d];
    uint8_t rr = state.r[ops.r];
    uint8_t ci = (state.sreg >> SREG_C) & 1;
    uint16_t result16 = (uint16_t)rd - (uint16_t)rr - ci;
    uint8_t result8 = (uint8_t)result16;

    bool h = ((rd & 0x0F) < ((rr & 0x0F) + ci));
    bool v = ((rd ^ rr) & (rd ^ result8) & 0x80) != 0;
    bool n = (result8 & 0x80) != 0;
    // Z: ANDed with previous Z — stays set only if both bytes were equal.
    bool prevZ = (state.sreg >> SREG_Z) & 1;
    bool z = (result8 == 0) ? prevZ : false;
    bool c = result16 > 0xFF;
    bool s = n ^ v;

    state.sreg = (state.sreg & 0xC0) | (s << SREG_S) | (v << SREG_V)
               | (n << SREG_N) | (z << SREG_Z) | (c << SREG_C) | (h << SREG_H);
}

// CPI — Compare with Immediate
// Compares a register (R16–R31) with an 8-bit constant: Rd - K.
// Sets flags; Rd is unchanged.
//
// SREG affected: H, V, N, Z, C, S.  T and I are preserved.
//
// @param ops.d — register to compare (R16–R31)
// @param ops.k — 8-bit constant
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

// CPSE — Compare Skip if Equal
// Compares two registers.  If they are equal, the next instruction is skipped.
// Skipping handles both 16-bit and 32-bit instructions correctly.
//
// SREG affected: None.  All flags are preserved.
//
// @param ops.d — first register
// @param ops.r — second register to compare against
void executeCPSE(AvrState& state, OpsRdRr ops) {
    if (state.r[ops.d] == state.r[ops.r]) {
        skipNextInstruction(state, "CPSE skip: unknown next instruction");
        state.extra_cycles = 1;  // skip costs 1 extra cycle
    }
}

// ===========================================================================
// Data Transfer instructions
// ===========================================================================

// MOV — Copy Register
// Copies the contents of one register to another.  Rd ← Rr.
//
// SREG affected: None.  All flags are preserved.
//
// @param ops.d — destination register
// @param ops.r — source register
void executeMOV(AvrState& state, OpsRdRr ops) {
    state.r[ops.d] = state.r[ops.r];
}

// MOVW — Copy Register Word
// Copies an even/odd register pair.  Rd+1:Rd ← Rr+1:Rr.
// Both d and r must be even (R0, R2, …, R30).
//
// SREG affected: None.
//
// @param ops.d — destination even register index (0–15, maps to R0,R2,…,R30)
// @param ops.r — source even register index (0–15, maps to R0,R2,…,R30)
void executeMOVW(AvrState& state, OpsRd06Rr06 ops) {
    // ops fields are raw nibbles (0–15); multiply by 2 for actual register number.
    uint8_t d = ops.d * 2;
    uint8_t r = ops.r * 2;
    state.r[d]     = state.r[r];                     // copy low byte
    state.r[d + 1] = state.r[r + 1];                 // copy high byte
}

// LDI — Load Immediate
// Loads an 8-bit constant into a register (R16–R31).  Rd ← K.
//
// SREG affected: None.
//
// @param ops.d — destination register (R16–R31)
// @param ops.k — 8-bit immediate value
void executeLDI(AvrState& state, OpsRdK8 ops) {
    state.r[ops.d] = ops.k;
}

// LD_X — Load Indirect from Data Space using X
// Loads one byte from the data space address pointed to by X (R27:R26).
// Supports post-increment (LD Rd, X+) and pre-decrement (LD Rd, -X).
// Mode: 0 = no change, 1 = post-increment, 2 = pre-decrement.
//
// SREG affected: None.
//
// @param ops.d — destination register
// @param ops.mode — 0 (base), 1 (post-increment), 2 (pre-decrement)
void executeLD_X(AvrState& state, OpsLdSt ops) {
    uint16_t X = readRegWord(state, 26);             // X = R27:R26

    if (ops.mode == 2) {  // Pre-decrement: --X, then load
        X--;
        writeRegWord(state, 26, X);
    }

    state.r[ops.d] = readDataByte(state, X);

    if (ops.mode == 1) {  // Post-increment: load, then X++
        X++;
        writeRegWord(state, 26, X);
    }
}

// LD_Y — Load Indirect from Data Space using Y
// Loads one byte from the data space address pointed to by Y (R29:R28).
// Supports post-increment (LD Rd, Y+) and pre-decrement (LD Rd, -Y).
// Mode: 0 = no change, 1 = post-increment, 2 = pre-decrement.
//
// SREG affected: None.
//
// @param ops.d — destination register
// @param ops.mode — 0 (base), 1 (post-increment), 2 (pre-decrement)
void executeLD_Y(AvrState& state, OpsLdSt ops) {
    uint16_t Y = readRegWord(state, 28);             // Y = R29:R28

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

// LDD_Y — Load Indirect with Displacement from Y
// Loads one byte from data space at address Y + q (R29:R28 + 6-bit displacement).
// Does NOT modify Y.  Rd ← [Y + q].
//
// SREG affected: None.
//
// @param ops.d — destination register
// @param ops.q — 6-bit unsigned displacement (0–63)
void executeLDD_Y(AvrState& state, OpsLdd ops) {
    uint16_t Y = readRegWord(state, 28);
    state.r[ops.d] = readDataByte(state, Y + ops.q);
}

// LD_Z — Load Indirect from Data Space using Z
// Loads one byte from the data space address pointed to by Z (R31:R30).
// Supports post-increment (LD Rd, Z+) and pre-decrement (LD Rd, -Z).
// Mode: 0 = no change, 1 = post-increment, 2 = pre-decrement.
//
// SREG affected: None.
//
// @param ops.d — destination register
// @param ops.mode — 0 (base), 1 (post-increment), 2 (pre-decrement)
void executeLD_Z(AvrState& state, OpsLdSt ops) {
    uint16_t Z = readRegWord(state, 30);             // Z = R31:R30

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

// LDD_Z — Load Indirect with Displacement from Z
// Loads one byte from data space at address Z + q (R31:R30 + 6-bit displacement).
// Does NOT modify Z.  Rd ← [Z + q].
//
// SREG affected: None.
//
// @param ops.d — destination register
// @param ops.q — 6-bit unsigned displacement (0–63)
void executeLDD_Z(AvrState& state, OpsLdd ops) {
    uint16_t Z = readRegWord(state, 30);
    state.r[ops.d] = readDataByte(state, Z + ops.q);
}

// LDS — Load Direct from Data Space (32-bit instruction)
// Loads one byte from a 16-bit data space address.  Rd ← [addr].
// The address occupies the second instruction word.
//
// SREG affected: None.
//
// @param ops.d   — destination register
// @param ops.addr — 16-bit data space address
void executeLDS(AvrState& state, OpsLdsSts ops) {
    state.r[ops.d] = readDataByte(state, ops.addr);
}

// LPM — Load Program Memory
// Loads one byte from program memory (flash) at the word address in Z (R31:R30).
// Supports post-increment (LPM Rd, Z+).  When mode == 0, Z is unchanged.
//
// SREG affected: None.
//
// @param ops.d  — destination register (0 for implicit LPM R0,Z form)
// @param mode   — 0 (no post-inc), 1 (post-increment Z)
void executeLPM(AvrState& state, OpsRd ops, uint8_t mode) {
    uint16_t Z = readRegWord(state, 30);
    if (Z >= AVR_FLASH_SIZE) {
      emuErrorPcAddr(emuFaultPc(), Z, "LPM", "flash read out of bounds");
      memorySignalFault();
      return;
    }
    state.r[ops.d] = state.flash[Z];                // read byte from flash
    if (mode == 1) {                                // LPM Rd, Z+
        Z++;
        writeRegWord(state, 30, Z);
    }
}

// IN — Load an I/O Location to Register
// Reads one byte from an I/O register and stores it in a GP register.
// The I/O address (0x00–0x3F) maps to data space 0x20 + ioAddr.
// Rd ← I/O[A].
//
// SREG affected: None.
//
// @param ops.d — destination register
// @param ops.a — 6-bit I/O address (0x00–0x3F)
void executeIN(AvrState& state, OpsRdIO ops) {
    state.r[ops.d] = readDataByte(state, ops.a + 0x20);  // I/O → data space
}

// OUT — Store Register to I/O Location
// Writes a GP register to an I/O register.
// The I/O address (0x00–0x3F) maps to data space 0x20 + ioAddr.
// I/O[A] ← Rr.
//
// SREG affected: None.
//
// @param ops.a — 6-bit I/O address (0x00–0x3F)
// @param ops.r — source register
void executeOUT(AvrState& state, OpsIORr ops) {
    writeDataByte(state, ops.a + 0x20, state.r[ops.r]);
}

// POP — Pop Register from Stack
// Pops one byte from the stack into a register.  Rd ← [SP]; SP++.
//
// SREG affected: None.
//
// @param ops.d — destination register
void executePOP(AvrState& state, OpsRd ops) {
    state.r[ops.d] = popByte(state);
}

// PUSH — Push Register on Stack
// Pushes a register onto the stack.  SP--; [SP] ← Rr.
//
// SREG affected: None.
//
// @param ops.d — source register (note: named 'd' by decoder but it's the source)
void executePUSH(AvrState& state, OpsRd ops) {
    pushByte(state, state.r[ops.d]);
}

// ST_X — Store Indirect to Data Space using X
// Stores one byte to the data space address pointed to by X (R27:R26).
// Supports post-increment (ST X+, Rr) and pre-decrement (ST -X, Rr).
// Mode: 0 = no change, 1 = post-increment, 2 = pre-decrement.
//
// SREG affected: None.
//
// @param ops.d   — source register (value to store)
// @param ops.mode — 0 (base), 1 (post-increment), 2 (pre-decrement)
void executeST_X(AvrState& state, OpsLdSt ops) {
    uint16_t X = readRegWord(state, 26);

    if (ops.mode == 2) {  // Pre-decrement: --X, then store
        X--;
        writeRegWord(state, 26, X);
    }

    writeDataByte(state, X, state.r[ops.d]);

    if (ops.mode == 1) {  // Post-increment: store, then X++
        X++;
        writeRegWord(state, 26, X);
    }
}

// ST_Y — Store Indirect to Data Space using Y
// Stores one byte to data space at Y (R29:R28).
// Supports post-increment (ST Y+, Rr) and pre-decrement (ST -Y, Rr).
// Mode: 0 = no change, 1 = post-increment, 2 = pre-decrement.
//
// SREG affected: None.
//
// @param ops.d   — source register
// @param ops.mode — 0 (base), 1 (post-increment), 2 (pre-decrement)
void executeST_Y(AvrState& state, OpsLdSt ops) {
    uint16_t Y = readRegWord(state, 28);

    if (ops.mode == 2) {  // Pre-decrement
        Y--;
        writeRegWord(state, 28, Y);
    }

    writeDataByte(state, Y, state.r[ops.d]);

    if (ops.mode == 1) {  // Post-increment
        Y++;
        writeRegWord(state, 28, Y);
    }
}

// STD_Y — Store Indirect with Displacement to Y
// Stores one byte to data space at Y + q (R29:R28 + 6-bit displacement).
// Does NOT modify Y.  [Y + q] ← Rr.
//
// SREG affected: None.
//
// @param ops.d — source register
// @param ops.q — 6-bit unsigned displacement (0–63)
void executeSTD_Y(AvrState& state, OpsLdd ops) {
    uint16_t Y = readRegWord(state, 28);
    writeDataByte(state, Y + ops.q, state.r[ops.d]);
}

// ST_Z — Store Indirect to Data Space using Z
// Stores one byte to data space at Z (R31:R30).
// Supports post-increment (ST Z+, Rr) and pre-decrement (ST -Z, Rr).
// Mode: 0 = no change, 1 = post-increment, 2 = pre-decrement.
//
// SREG affected: None.
//
// @param ops.d   — source register
// @param ops.mode — 0 (base), 1 (post-increment), 2 (pre-decrement)
void executeST_Z(AvrState& state, OpsLdSt ops) {
    uint16_t Z = readRegWord(state, 30);

    if (ops.mode == 2) {  // Pre-decrement
        Z--;
        writeRegWord(state, 30, Z);
    }

    writeDataByte(state, Z, state.r[ops.d]);

    if (ops.mode == 1) {  // Post-increment
        Z++;
        writeRegWord(state, 30, Z);
    }
}

// STD_Z — Store Indirect with Displacement to Z
// Stores one byte to data space at Z + q (R31:R30 + 6-bit displacement).
// Does NOT modify Z.  [Z + q] ← Rr.
//
// SREG affected: None.
//
// @param ops.d — source register
// @param ops.q — 6-bit unsigned displacement (0–63)
void executeSTD_Z(AvrState& state, OpsLdd ops) {
    uint16_t Z = readRegWord(state, 30);
    writeDataByte(state, Z + ops.q, state.r[ops.d]);
}

// STS — Store Direct to Data Space (32-bit instruction)
// Stores one byte to a 16-bit data space address.  [addr] ← Rr.
// The address occupies the second instruction word.
//
// SREG affected: None.
//
// @param ops.d   — source register
// @param ops.addr — 16-bit data space address
void executeSTS(AvrState& state, OpsLdsSts ops) {
    writeDataByte(state, ops.addr, state.r[ops.d]);
}

// ===========================================================================
// Branch / Jump / Call instructions
// ===========================================================================

// BRBC — Branch if Bit in SREG is Cleared
// Tests a specific SREG bit.  If the bit is 0, adds a signed 7-bit offset
// (in words) to PC.  The offset k is relative to the instruction AFTER the branch.
//
// SREG affected: None.  Conditionally modifies PC only.
//
void executeBRBC(AvrState& state, OpsK7 ops) {
    if (!((state.sreg >> ops.s) & 1)) {
        // PC already points past the branch instruction; add signed offset in bytes.
        state.pc += static_cast<int16_t>(ops.k) * 2;
        state.extra_cycles = 1;  // branch taken costs 1 extra cycle
    }
}

// BRBS — Branch if Bit in SREG is Set
// Tests a specific SREG bit.  If the bit is 1, adds a signed 7-bit offset
// (in words) to PC.
//
// SREG affected: None.
//
// @param ops.s — SREG bit index (0–7) to test
void executeBRBS(AvrState& state, OpsK7 ops) {
    if ((state.sreg >> ops.s) & 1) {
        state.pc += static_cast<int16_t>(ops.k) * 2;
        state.extra_cycles = 1;  // branch taken costs 1 extra cycle
    }
}

// RJMP — Relative Jump
// Unconditional relative jump.  PC ← PC + k + 1 (where k is a 12-bit signed
// word offset, and PC already points past the RJMP instruction).
//
// SREG affected: None.
//
// @param ops.k — 12-bit signed word offset (-2048 to +2047 words)
void executeRJMP(AvrState& state, OpsK02 ops) {
    // PC already points past RJMP; multiply by 2 → byte offset.
    state.pc += static_cast<int16_t>(ops.k) * 2;
}

// JMP — Jump (32-bit instruction)
// Unconditional absolute jump to a 22-bit word address.
// PC ← k * 2 (k in words, PC in bytes).  Can reach entire 4M word flash.
//
// SREG affected: None.
//
// @param ops.k — 22-bit word address
void executeJMP(AvrState& state, OpsK22 ops) {
    state.pc = (uint16_t)(ops.k * 2);
}
// IJMP — Indirect Jump
//
// SREG affected: None.
void executeIJMP(AvrState& state) {
    // Z holds a word address; PC uses byte addresses.
    state.pc = readRegWord(state, 30) * 2;
}

// RCALL — Relative Call to Subroutine
// Pushes the return address (current PC) onto the stack, then adds a 12-bit
// signed word offset to PC.  PUSH(PC); PC ← PC + k (words).
//
// SREG affected: None.
//
// @param ops.k — 12-bit signed word offset (-2048 to +2047 words)
void executeRCALL(AvrState& state, OpsK02 ops) {
    pushWord(state, state.pc);                       // save return address
    state.pc += static_cast<int16_t>(ops.k) * 2;
}

// CALL — Long Call to Subroutine (32-bit instruction)
// Pushes the return address onto the stack, then jumps to a 22-bit word address.
// PUSH(PC); PC ← k * 2.
//
// SREG affected: None.
//
// @param ops.k — 22-bit word address
void executeCALL(AvrState& state, OpsK22 ops) {
    pushWord(state, state.pc);
    state.pc = (uint16_t)(ops.k * 2);
}
//
// SREG affected: None.
void executeICALL(AvrState& state) {
    pushWord(state, state.pc);
    state.pc = readRegWord(state, 30) * 2;
}

// RET — Return from Subroutine
// Pops the return address from the stack into PC.  PC ← POP().
// Used at the end of subroutines called by RCALL/CALL/ICALL.
//
// SREG affected: None.
void executeRET(AvrState& state) {
    state.pc = popWord(state);
}

// RETI — Return from Interrupt
// Pops the return address from the stack into PC and sets the Global Interrupt
// Enable flag (I in SREG).  PC ← POP(); I ← 1.
// Used at the end of interrupt service routines.
//
// SREG affected: I (set).  All other flags are preserved.
void executeRETI(AvrState& state) {
    state.pc = popWord(state);
    setFlag(state, SregBit::I);                      // re-enable interrupts
}

// ===========================================================================
// Skip instructions
//
// Skip instructions conditionally skip the next instruction by advancing PC
// past it without executing it.  The skipped instruction may be 16 or 32 bits.
// ===========================================================================

// skipNextInstruction — internal helper for all skip instructions.
// Advances PC past the next instruction (accounting for 32-bit instructions).
//
// @param state   — emulator state; PC is advanced past the next instruction
// @param context — error message prefix for decoder failures
static void skipNextInstruction(AvrState& state, const char* context) {
    if (state.pc + 1 >= AVR_FLASH_SIZE) {
      emuErrorPc(state.pc, context);
      memorySignalFault();
      return;
    }
    // Decode the next instruction to determine its size (1 or 2 words).
    uint16_t nextInstr = state.flash[state.pc] | (state.flash[state.pc + 1] << 8);
    Opcode nextOp;
    if (!decodeInstruction(nextInstr, nextOp)) {
      emuErrorPcInstr(state.pc, nextInstr, context);
      memorySignalFault();
      return;
    }
    state.pc += nextOp.words * 2;                    // skip 2 or 4 bytes
}

// SBIC — Skip if Bit in I/O Register is Cleared
// Tests a bit in an I/O register (0x00–0x1F).  If the bit is 0, skips
// the next instruction.
//
// SREG affected: None.  Conditionally modifies PC.
//
// @param ops.a — 5-bit I/O address (0x00–0x1F)
// @param ops.b — bit position (0–7)
void executeSBIC(AvrState& state, OpsIOB ops) {
    if (!getIOBit(state, ops.a, ops.b)) {
        skipNextInstruction(state, "SBIC skip: unknown next instruction");
        state.extra_cycles = 1;
    }
}

// SBIS — Skip if Bit in I/O Register is Set
// Tests a bit in an I/O register (0x00–0x1F).  If the bit is 1, skips
// the next instruction.
//
// SREG affected: None.
//
// @param ops.a — 5-bit I/O address (0x00–0x1F)
// @param ops.b — bit position (0–7)
void executeSBIS(AvrState& state, OpsIOB ops) {
    if (getIOBit(state, ops.a, ops.b)) {
        skipNextInstruction(state, "SBIS skip: unknown next instruction");
        state.extra_cycles = 1;
    }
}

// SBRC — Skip if Bit in Register is Cleared
// Tests a bit in a GP register.  If the bit is 0, skips the next instruction.
//
// SREG affected: None.
//
// @param ops.r — register to test
// @param ops.b — bit position (0–7)
void executeSBRC(AvrState& state, OpsRrB ops) {
    if (!((state.r[ops.r] >> ops.b) & 1)) {
        skipNextInstruction(state, "SBRC skip: unknown next instruction");
        state.extra_cycles = 1;
    }
}

// SBRS — Skip if Bit in Register is Set
// Tests a bit in a GP register.  If the bit is 1, skips the next instruction.
//
// SREG affected: None.
//
// @param ops.r — register to test
// @param ops.b — bit position (0–7)
void executeSBRS(AvrState& state, OpsRrB ops) {
    if ((state.r[ops.r] >> ops.b) & 1) {
        skipNextInstruction(state, "SBRS skip: unknown next instruction");
        state.extra_cycles = 1;
    }
}

// ===========================================================================
// Bit manipulation instructions
// ===========================================================================

// BSET — Bit Set in SREG
// Sets a single bit in the Status Register.  SREG[s] ← 1.
// Also used by named aliases: SEC, SEZ, SEN, SEV, SES, SEH, SET, SEI.
//
// SREG affected: the specified bit.  All others are preserved.
//
// @param ops.b — SREG bit index (0–7) to set
void executeBSET(AvrState& state, OpsBOnly ops) {
    setFlag(state, static_cast<SregBit>(ops.b));
}

// BCLR — Bit Clear in SREG
// Clears a single bit in the Status Register.  SREG[s] ← 0.
// Also used by named aliases: CLC, CLZ, CLN, CLV, CLS, CLH, CLT, CLI.
//
// SREG affected: the specified bit.  All others are preserved.
//
// @param ops.b — SREG bit index (0–7) to clear
void executeBCLR(AvrState& state, OpsBOnly ops) {
    clearFlag(state, static_cast<SregBit>(ops.b));
}

// BLD — Bit Load from T Flag to Register
// Copies the T flag in SREG to a specific bit in a register.
// Rd[b] ← T.
//
// SREG affected: None.
//
// @param ops.d — destination register
// @param ops.b — bit position (0–7) in the register
void executeBLD(AvrState& state, OpsRdB ops) {
    if (getFlag(state, SregBit::T))
        state.r[ops.d] |= (1 << ops.b);              // set the bit
    else
        state.r[ops.d] &= ~(1 << ops.b);             // clear the bit
}

// BST — Bit Store from Register to T Flag
// Copies a specific bit from a register to the T flag in SREG.
// T ← Rd[b].
//
// SREG affected: T.  All other flags are preserved.
//
// @param ops.d — source register
// @param ops.b — bit position (0–7) to copy
void executeBST(AvrState& state, OpsRdB ops) {
    if ((state.r[ops.d] >> ops.b) & 1)
        setFlag(state, SregBit::T);
    else
        clearFlag(state, SregBit::T);
}

// CBI — Clear Bit in I/O Register
// Clears a single bit in an I/O register (0x00–0x1F).  I/O[a].b ← 0.
//
// SREG affected: None.
//
// @param ops.a — 5-bit I/O address (0x00–0x1F)
// @param ops.b — bit position (0–7) to clear
void executeCBI(AvrState& state, OpsIOB ops) {
    clearIOBit(state, ops.a, ops.b);
}

// SBI — Set Bit in I/O Register
// Sets a single bit in an I/O register (0x00–0x1F).  I/O[a].b ← 1.
//
// SREG affected: None.
//
// @param ops.a — 5-bit I/O address (0x00–0x1F)
// @param ops.b — bit position (0–7) to set
void executeSBI(AvrState& state, OpsIOB ops) {
    setIOBit(state, ops.a, ops.b);
}

// ===========================================================================
// MCU control instructions
// ===========================================================================

// NOP — No Operation
// Does nothing.  Consumes one cycle.
//
// SREG affected: None.
void executeNOP(AvrState& state) {
    (void)state;
}

// SLEEP — Sleep
// Puts the MCU into sleep mode as configured by the SMCR register.
// In the emulator, treated as a NOP — full peripheral integration is out of scope.
//
// SREG affected: None.
void executeSLEEP(AvrState& state) {
    (void)state;
}

// WDR — Watchdog Reset
// Resets the Watchdog Timer.  In the emulator, treated as a NOP —
// the watchdog is not emulated.
//
// SREG affected: None.
void executeWDR(AvrState& state) {
    (void)state;
}

// BREAK — Break
// Used by the On-Chip Debug system.  Without a debugger, acts as NOP.
//
// SREG affected: None.
void executeBREAK(AvrState& state) {
    (void)state;
}

// SPM — Store Program Memory
// Writes R1:R0 to the program memory (flash) at the word address in Z.
// Full SPM with SPMCSR and page buffer is deferred for bootloader support.
// This minimal implementation writes R1:R0 directly to flash[Z:Z+1].
//
// SREG affected: None.
void executeSPM(AvrState& state) {
    uint16_t Z = readRegWord(state, 30);
    if (Z + 1 >= AVR_FLASH_SIZE) return;             // bounds check
    state.flash[Z]     = state.r[0];                 // write low byte (R0)
    state.flash[Z + 1] = state.r[1];                 // write high byte (R1)
}

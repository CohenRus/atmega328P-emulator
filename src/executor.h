// ===========================================================================
// executor.h — ATmega328P instruction execution interface.
//
// Declares the main execution loop (executeProgram), state initialization,
// operand-decoded instruction dispatch, and the full set of per-instruction
// executor functions.  Each executor function receives pre-decoded operands
// from the decoder layer — no instruction re-parses bit fields.
//
// Instruction aliases (LSL→ADD, ROL→ADC, TST→AND, CLR→EOR, CBR→ANDI,
// SBR→ORI) dispatch to the canonical implementation so only one code path
// exists per operation.
// ===========================================================================

#pragma once
#include <atomic>
#include <cstdint>
#include "decoder.h"
#include "state.h"
// ===========================================================================
// Top-level entry points
// ===========================================================================

// Runs the fetch-decode-execute loop.  Initializes all peripherals, then
// loops indefinitely: poll UART, service interrupts, fetch/decode/execute
// each instruction, advance Timer0, and sync with wall-clock.
// Returns true on clean exit (g_emu_stop set), false on fatal error.
//
// @param state — emulator runtime state (modified in place each cycle)
bool executeProgram(AvrState& state);

// Resets the emulator to power-on state: all GP registers → 0, PC → 0,
// SREG → 0, SP → RAMEND (0x08FF), all SRAM and EEPROM → 0.
//
// @param state — runtime state to clear
// @return always true
bool clearState(AvrState& state);

// Set to true from any thread to request the execution loop to exit
// at the next iteration boundary.  executeProgram checks this at the
// top of each cycle and returns true (clean exit) when set.
extern std::atomic<bool> g_emu_stop;

// Set to true from the TUI (Space key) to pause execution.
// The loop spin-waits while this is true and g_emu_stop is false.
extern std::atomic<bool> g_emu_pause;

// Decodes operands from the raw instruction word(s) based on op.fmt,
// then calls the matching instruction executor.  Aliased opcodes
// dispatch to the canonical implementation.
// dispatch to the canonical implementation.
//
// @param state      — emulator runtime state
// @param op         — decoded opcode entry (op, fmt, words, cycles)
// @param instr      — raw 16-bit instruction word
// @param secondWord — second 16-bit word for 32-bit instructions, or 0
// @return true on success, false on unimplemented opcode
bool executeInstruction(AvrState& state, Opcode& op, uint16_t instr, uint16_t secondWord = 0);

// ===========================================================================
// Arithmetic instructions — detailed docs in executor.cpp
// ===========================================================================

void executeADC(AvrState& state, OpsRdRr ops);        // Add with Carry
void executeADD(AvrState& state, OpsRdRr ops);        // Add without Carry (also LSL)
void executeADIW(AvrState& state, OpsRd06K6 ops);     // Add Immediate to Word
void executeASR(AvrState& state, OpsRd ops);          // Arithmetic Shift Right
void executeDEC(AvrState& state, OpsRd ops);          // Decrement
void executeINC(AvrState& state, OpsRd ops);          // Increment
void executeMUL(AvrState& state, OpsRdRr ops);        // Multiply Unsigned
void executeMULS(AvrState& state, OpsRd06Rr06 ops);   // Multiply Signed (raw nibbles; +16 applied inside)
void executeMULSU(AvrState& state, OpsRdRrMpy ops);   // Multiply Signed with Unsigned
void executeFMUL(AvrState& state, OpsRdRrMpy ops);    // Fractional Multiply Unsigned
void executeFMULS(AvrState& state, OpsRdRrMpy ops);   // Fractional Multiply Signed
void executeFMULSU(AvrState& state, OpsRdRrMpy ops);  // Fractional Multiply Signed with Unsigned
void executeNEG(AvrState& state, OpsRd ops);          // Two's Complement
void executeSBIW(AvrState& state, OpsRd06K6 ops);     // Subtract Immediate from Word
void executeSBC(AvrState& state, OpsRdRr ops);        // Subtract with Carry
void executeSBCI(AvrState& state, OpsRdK8 ops);       // Subtract Immediate with Carry
void executeSUB(AvrState& state, OpsRdRr ops);        // Subtract without Carry
void executeSUBI(AvrState& state, OpsRdK8 ops);       // Subtract Immediate

// ===========================================================================
// Logic instructions
// ===========================================================================

void executeAND(AvrState& state, OpsRdRr ops);        // Logical AND (also TST)
void executeANDI(AvrState& state, OpsRdK8 ops);       // Logical AND with Immediate (also CBR)
void executeCOM(AvrState& state, OpsRd ops);          // One's Complement
void executeEOR(AvrState& state, OpsRdRr ops);        // Exclusive OR (also CLR)
void executeOR(AvrState& state, OpsRdRr ops);         // Logical OR
void executeORI(AvrState& state, OpsRdK8 ops);        // Logical OR with Immediate (also SBR)
void executeSER(AvrState& state, OpsRdK8 ops);        // Set all Bits in Register
void executeSWAP(AvrState& state, OpsRd ops);         // Swap Nibbles

// ===========================================================================
// Shift / Rotate instructions
// LSL dispatches to executeADD (ADD Rd,Rd).
// ROL dispatches to executeADC (ADC Rd,Rd).
// ===========================================================================

void executeLSR(AvrState& state, OpsRd ops);          // Logical Shift Right
void executeROR(AvrState& state, OpsRd ops);          // Rotate Right through Carry

// ===========================================================================
// Compare instructions — perform subtraction without storing the result
// ===========================================================================

void executeCP(AvrState& state, OpsRdRr ops);         // Compare
void executeCPC(AvrState& state, OpsRdRr ops);        // Compare with Carry
void executeCPI(AvrState& state, OpsRdK8 ops);        // Compare with Immediate
void executeCPSE(AvrState& state, OpsRdRr ops);       // Compare Skip if Equal

// ===========================================================================
// Data Transfer instructions
// ===========================================================================

void executeMOV(AvrState& state, OpsRdRr ops);        // Copy Register
void executeMOVW(AvrState& state, OpsRd06Rr06 ops);   // Copy Register Word (raw nibbles; ×2 applied inside)
void executeLDI(AvrState& state, OpsRdK8 ops);        // Load Immediate
void executeLD_X(AvrState& state, OpsLdSt ops);       // Load Indirect from X
void executeLD_Y(AvrState& state, OpsLdSt ops);       // Load Indirect from Y
void executeLDD_Y(AvrState& state, OpsLdd ops);       // Load Indirect with Displacement from Y
void executeLD_Z(AvrState& state, OpsLdSt ops);       // Load Indirect from Z
void executeLDD_Z(AvrState& state, OpsLdd ops);       // Load Indirect with Displacement from Z
void executeLDS(AvrState& state, OpsLdsSts ops);      // Load Direct from Data Space (32-bit)

// Load Program Memory.
// @param mode — 0 = Z (no post-increment), 1 = Z+
// d = 0 is implied when dispatched from the LPM R0,Z form
void executeLPM(AvrState& state, OpsRd ops, uint8_t mode);

void executeIN(AvrState& state, OpsRdIO ops);         // Load I/O Location to Register
void executeOUT(AvrState& state, OpsIORr ops);        // Store Register to I/O Location
void executePOP(AvrState& state, OpsRd ops);          // Pop Register from Stack
void executePUSH(AvrState& state, OpsRd ops);         // Push Register on Stack
void executeST_X(AvrState& state, OpsLdSt ops);       // Store Indirect to X
void executeST_Y(AvrState& state, OpsLdSt ops);       // Store Indirect to Y
void executeSTD_Y(AvrState& state, OpsLdd ops);       // Store Indirect with Displacement to Y
void executeST_Z(AvrState& state, OpsLdSt ops);       // Store Indirect to Z
void executeSTD_Z(AvrState& state, OpsLdd ops);       // Store Indirect with Displacement to Z
void executeSTS(AvrState& state, OpsLdsSts ops);      // Store Direct to Data Space (32-bit)

// ===========================================================================
// Branch / Jump / Call instructions
// ===========================================================================

void executeBRBC(AvrState& state, OpsK7 ops);         // Branch if Bit in SREG is Cleared
void executeBRBS(AvrState& state, OpsK7 ops);         // Branch if Bit in SREG is Set
void executeRJMP(AvrState& state, OpsK02 ops);        // Relative Jump
void executeJMP(AvrState& state, OpsK22 ops);         // Jump (32-bit)
void executeIJMP(AvrState& state);                    // Indirect Jump (via Z)
void executeRCALL(AvrState& state, OpsK02 ops);       // Relative Call
void executeCALL(AvrState& state, OpsK22 ops);        // Long Call (32-bit)
void executeICALL(AvrState& state);                   // Indirect Call (via Z)
void executeRET(AvrState& state);                     // Return from Subroutine
void executeRETI(AvrState& state);                    // Return from Interrupt

// ===========================================================================
// Skip instructions — conditionally skip the next instruction
// ===========================================================================

void executeSBIC(AvrState& state, OpsIOB ops);        // Skip if Bit in I/O Register is Cleared
void executeSBIS(AvrState& state, OpsIOB ops);        // Skip if Bit in I/O Register is Set
void executeSBRC(AvrState& state, OpsRrB ops);        // Skip if Bit in Register is Cleared
void executeSBRS(AvrState& state, OpsRrB ops);        // Skip if Bit in Register is Set

// ===========================================================================
// Bit manipulation instructions
// ===========================================================================

void executeBSET(AvrState& state, OpsBOnly ops);      // Bit Set in SREG
void executeBCLR(AvrState& state, OpsBOnly ops);      // Bit Clear in SREG
void executeBLD(AvrState& state, OpsRdB ops);         // Bit Load from T Flag to Register
void executeBST(AvrState& state, OpsRdB ops);         // Bit Store from Register to T Flag
void executeCBI(AvrState& state, OpsIOB ops);         // Clear Bit in I/O Register
void executeSBI(AvrState& state, OpsIOB ops);         // Set Bit in I/O Register

// ===========================================================================
// MCU control instructions
// ===========================================================================

void executeNOP(AvrState& state);                     // No Operation
void executeSLEEP(AvrState& state);                   // Sleep (treated as NOP)
void executeWDR(AvrState& state);                     // Watchdog Reset (treated as NOP)
void executeBREAK(AvrState& state);                   // Break (acts as NOP without debugger)
void executeSPM(AvrState& state);                     // Store Program Memory

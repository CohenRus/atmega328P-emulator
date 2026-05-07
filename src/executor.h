#pragma once

#include <cstdint>
#include <iostream>
#include "state.h"
#include "decoder.h"

bool executeProgram(AvrState& state);
bool clearState(AvrState& state);
bool executeInstruction(AvrState& state, Opcode& op, uint16_t instr, uint16_t secondWord = 0);

// ---------------------------------------------------------------------------
// Individual instruction executors.
// Each takes the decoded operand struct that matches its AvrFmt so no
// instruction re-parses bit fields.
// ---------------------------------------------------------------------------

// Arithmetic
void executeADC(AvrState& state, OpsRdRr ops);
void executeADD(AvrState& state, OpsRdRr ops);
void executeADIW(AvrState& state, OpsRd06K6 ops);
void executeASR(AvrState& state, OpsRd ops);
void executeDEC(AvrState& state, OpsRd ops);
void executeINC(AvrState& state, OpsRd ops);
void executeMUL(AvrState& state, OpsRdRr ops);
void executeMULS(AvrState& state, OpsRd06Rr06 ops);  // raw nibbles; +16 applied inside
void executeMULSU(AvrState& state, OpsRdRrMpy ops);
void executeFMUL(AvrState& state, OpsRdRrMpy ops);
void executeFMULS(AvrState& state, OpsRdRrMpy ops);
void executeFMULSU(AvrState& state, OpsRdRrMpy ops);
void executeNEG(AvrState& state, OpsRd ops);
void executeSBIW(AvrState& state, OpsRd06K6 ops);
void executeSBC(AvrState& state, OpsRdRr ops);
void executeSBCI(AvrState& state, OpsRdK8 ops);
void executeSUB(AvrState& state, OpsRdRr ops);
void executeSUBI(AvrState& state, OpsRdK8 ops);

// Logic
void executeAND(AvrState& state, OpsRdRr ops);
void executeANDI(AvrState& state, OpsRdK8 ops);
void executeCOM(AvrState& state, OpsRd ops);
void executeEOR(AvrState& state, OpsRdRr ops);
void executeOR(AvrState& state, OpsRdRr ops);
void executeORI(AvrState& state, OpsRdK8 ops);
void executeSER(AvrState& state, OpsRdK8 ops);
void executeSWAP(AvrState& state, OpsRd ops);

// Shift / Rotate  (LSL/ROL/CLR/TST dispatch to ADD/ADC/EOR/AND respectively)
void executeLSR(AvrState& state, OpsRd ops);
void executeROR(AvrState& state, OpsRd ops);

// Compare
void executeCP(AvrState& state, OpsRdRr ops);
void executeCPC(AvrState& state, OpsRdRr ops);
void executeCPI(AvrState& state, OpsRdK8 ops);
void executeCPSE(AvrState& state, OpsRdRr ops);

// Data Transfer
void executeMOV(AvrState& state, OpsRdRr ops);
void executeMOVW(AvrState& state, OpsRd06Rr06 ops);  // raw nibbles; ×2 applied inside
void executeLDI(AvrState& state, OpsRdK8 ops);
void executeLD_X(AvrState& state, OpsLdSt ops);
void executeLD_Y(AvrState& state, OpsLdSt ops);
void executeLDD_Y(AvrState& state, OpsLdd ops);
void executeLD_Z(AvrState& state, OpsLdSt ops);
void executeLDD_Z(AvrState& state, OpsLdd ops);
void executeLDS(AvrState& state, OpsLdsSts ops);
// mode: 0 = Z (no inc), 1 = Z+; d = 0 is implied when called from LPM R0,Z
void executeLPM(AvrState& state, OpsRd ops, uint8_t mode);
void executeIN(AvrState& state, OpsRdIO ops);
void executeOUT(AvrState& state, OpsIORr ops);
void executePOP(AvrState& state, OpsRd ops);
void executePUSH(AvrState& state, OpsRd ops);
void executeST_X(AvrState& state, OpsLdSt ops);
void executeST_Y(AvrState& state, OpsLdSt ops);
void executeSTD_Y(AvrState& state, OpsLdd ops);
void executeST_Z(AvrState& state, OpsLdSt ops);
void executeSTD_Z(AvrState& state, OpsLdd ops);
void executeSTS(AvrState& state, OpsLdsSts ops);

// Branch / Jump / Call
void executeBRBC(AvrState& state, OpsK7 ops);
void executeBRBS(AvrState& state, OpsK7 ops);
void executeRJMP(AvrState& state, OpsK02 ops);
void executeJMP(AvrState& state, OpsK22 ops);
void executeIJMP(AvrState& state);
void executeRCALL(AvrState& state, OpsK02 ops);
void executeCALL(AvrState& state, OpsK22 ops);
void executeICALL(AvrState& state);
void executeRET(AvrState& state);
void executeRETI(AvrState& state);

// Skip
void executeSBIC(AvrState& state, OpsIOB ops);
void executeSBIS(AvrState& state, OpsIOB ops);
void executeSBRC(AvrState& state, OpsRrB ops);
void executeSBRS(AvrState& state, OpsRrB ops);

// Bit manipulation
void executeBSET(AvrState& state, OpsBOnly ops);
void executeBCLR(AvrState& state, OpsBOnly ops);
void executeBLD(AvrState& state, OpsRdB ops);
void executeBST(AvrState& state, OpsRdB ops);
void executeCBI(AvrState& state, OpsIOB ops);
void executeSBI(AvrState& state, OpsIOB ops);

// MCU control
void executeNOP(AvrState& state);
void executeSLEEP(AvrState& state);
void executeWDR(AvrState& state);
void executeBREAK(AvrState& state);
void executeSPM(AvrState& state);

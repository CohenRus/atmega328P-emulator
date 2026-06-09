/*
 * test_instructions.cpp - Unit tests for AVR instruction execution.
 * Exercises register results, SREG behavior, stack semantics, and edge cases
 * by calling decoded instruction handlers directly.
 */

#include "catch_amalgamated.hpp"
#include "decoder.h"
#include "state.h"
#include "memory.h"
#include "executor.h"
#include <cstring>

// Helper: zero and init a new AvrState with stack at top of SRAM
static AvrState freshState() {
    AvrState s{};
    clearState(s);
    s.sp = 0x08FF;
    return s;
}

// ===========================================================================
// Rd_Rr format (call executors directly with operands)
// ===========================================================================

TEST_CASE("ADD R5,R10", "[instructions][arithmetic][Rd_Rr]") {
    auto s = freshState();
    s.r[5] = 10;
    s.r[10] = 20;
    OpsRdRr ops = {5, 10};
    executeADD(s, ops);
    REQUIRE(s.r[5] == 30);
    REQUIRE(s.r[10] == 20);
    REQUIRE_FALSE(getFlag(s, SregBit::Z));
    REQUIRE_FALSE(getFlag(s, SregBit::C));
    REQUIRE_FALSE(getFlag(s, SregBit::N));
    REQUIRE_FALSE(getFlag(s, SregBit::V));
}

TEST_CASE("ADD zero+zero → zero flag", "[instructions][arithmetic][Rd_Rr]") {
    auto s = freshState();
    OpsRdRr ops = {5, 10};
    executeADD(s, ops); // R5=0, R10=0
    REQUIRE(s.r[5] == 0);
    REQUIRE(getFlag(s, SregBit::Z));
}

TEST_CASE("ADD overflow 200+100", "[instructions][arithmetic][Rd_Rr]") {
    auto s = freshState();
    s.r[5] = 200;
    s.r[10] = 100;
    OpsRdRr ops = {5, 10};
    executeADD(s, ops);
    REQUIRE(s.r[5] == 44);
    REQUIRE(getFlag(s, SregBit::C));
    REQUIRE_FALSE(getFlag(s, SregBit::N));
    REQUIRE_FALSE(getFlag(s, SregBit::Z));
}

TEST_CASE("ADC with carry in", "[instructions][arithmetic][Rd_Rr]") {
    auto s = freshState();
    s.r[3] = 5;
    s.r[7] = 3;
    setFlag(s, SregBit::C);
    OpsRdRr ops = {3, 7};
    executeADC(s, ops);
    REQUIRE(s.r[3] == 9);
    REQUIRE_FALSE(getFlag(s, SregBit::C));
}

TEST_CASE("ADC preserves cleared Z across a zero high byte",
          "[instructions][arithmetic][regression]") {
    auto s = freshState();
    clearFlag(s, SregBit::Z);
    executeADC(s, {3, 7});
    REQUIRE(s.r[3] == 0);
    REQUIRE_FALSE(getFlag(s, SregBit::Z));
}

TEST_CASE("SUB 30-10", "[instructions][arithmetic][Rd_Rr]") {
    auto s = freshState();
    s.r[5] = 30;
    s.r[10] = 10;
    OpsRdRr ops = {5, 10};
    executeSUB(s, ops);
    REQUIRE(s.r[5] == 20);
    REQUIRE_FALSE(getFlag(s, SregBit::C));
}

TEST_CASE("SUB 5-10 → borrow", "[instructions][arithmetic][Rd_Rr]") {
    auto s = freshState();
    s.r[5] = 5;
    s.r[10] = 10;
    OpsRdRr ops = {5, 10};
    executeSUB(s, ops);
    REQUIRE(s.r[5] == 251);
    REQUIRE(getFlag(s, SregBit::C));
    REQUIRE(getFlag(s, SregBit::N));
}

TEST_CASE("SBC with borrow in", "[instructions][arithmetic][Rd_Rr]") {
    auto s = freshState();
    s.r[3] = 10;
    s.r[7] = 3;
    setFlag(s, SregBit::C);
    OpsRdRr ops = {3, 7};
    executeSBC(s, ops);
    REQUIRE(s.r[3] == 6);
    REQUIRE_FALSE(getFlag(s, SregBit::C));
}

TEST_CASE("SBC preserves cleared Z across a zero high byte",
          "[instructions][arithmetic][regression]") {
    auto s = freshState();
    clearFlag(s, SregBit::Z);
    executeSBC(s, {3, 7});
    REQUIRE(s.r[3] == 0);
    REQUIRE_FALSE(getFlag(s, SregBit::Z));
}

TEST_CASE("AND 0xF0 & 0x0F", "[instructions][logic][Rd_Rr]") {
    auto s = freshState();
    s.r[5] = 0xF0;
    s.r[10] = 0x0F;
    OpsRdRr ops = {5, 10};
    executeAND(s, ops);
    REQUIRE(s.r[5] == 0x00);
    REQUIRE(getFlag(s, SregBit::Z));
}

TEST_CASE("OR 0xF0 | 0x0F", "[instructions][logic][Rd_Rr]") {
    auto s = freshState();
    s.r[5] = 0xF0;
    s.r[10] = 0x0F;
    OpsRdRr ops = {5, 10};
    executeOR(s, ops);
    REQUIRE(s.r[5] == 0xFF);
    REQUIRE(getFlag(s, SregBit::N));
}

TEST_CASE("EOR Rd,Rd → 0 (CLR)", "[instructions][logic][Rd_Rr]") {
    auto s = freshState();
    s.r[5] = 0xAA;
    OpsRdRr ops = {5, 5};
    executeEOR(s, ops);
    REQUIRE(s.r[5] == 0);
    REQUIRE(getFlag(s, SregBit::Z));
}

TEST_CASE("MOV R5←R10", "[instructions][datatransfer][Rd_Rr]") {
    auto s = freshState();
    s.r[10] = 0x42;
    OpsRdRr ops = {5, 10};
    executeMOV(s, ops);
    REQUIRE(s.r[5] == 0x42);
}

TEST_CASE("CP equal registers", "[instructions][compare][Rd_Rr]") {
    auto s = freshState();
    s.r[5] = 10;
    s.r[10] = 10;
    OpsRdRr ops = {5, 10};
    executeCP(s, ops);
    REQUIRE(s.r[5] == 10); // unchanged
    REQUIRE(getFlag(s, SregBit::Z));
}

TEST_CASE("CPSE skip — registers equal", "[instructions][skip][Rd_Rr]") {
    auto s = freshState();
    s.r[0] = 42;
    s.r[1] = 42;
    uint16_t origPc = s.pc;
    OpsRdRr ops = {0, 1};
    executeCPSE(s, ops);
    // skipNextInstruction advances PC by 2 (1-word NOP at address 0)
    REQUIRE(s.pc == origPc + 2);
}

TEST_CASE("CPSE no skip — registers differ", "[instructions][skip][Rd_Rr]") {
    auto s = freshState();
    s.r[0] = 42;
    s.r[1] = 99;
    uint16_t origPc = s.pc;
    OpsRdRr ops = {0, 1};
    executeCPSE(s, ops);
    REQUIRE(s.pc == origPc);
}

// ===========================================================================
// Rd_K8 format
// ===========================================================================

TEST_CASE("LDI R20,0x55", "[instructions][datatransfer][Rd_K8]") {
    auto s = freshState();
    OpsRdK8 ops = {20, 0x55};
    executeLDI(s, ops);
    REQUIRE(s.r[20] == 0x55);
}

TEST_CASE("ANDI R16,0x0F", "[instructions][logic][Rd_K8]") {
    auto s = freshState();
    s.r[16] = 0xFF;
    OpsRdK8 ops = {16, 0x0F};
    executeANDI(s, ops);
    REQUIRE(s.r[16] == 0x0F);
    REQUIRE_FALSE(getFlag(s, SregBit::Z));
}

TEST_CASE("ORI R16,0x20", "[instructions][logic][Rd_K8]") {
    auto s = freshState();
    s.r[16] = 0x10;
    OpsRdK8 ops = {16, 0x20};
    executeORI(s, ops);
    REQUIRE(s.r[16] == 0x30);
}

TEST_CASE("SUBI R20,20 → 30", "[instructions][arithmetic][Rd_K8]") {
    auto s = freshState();
    s.r[20] = 50;
    OpsRdK8 ops = {20, 20};
    executeSUBI(s, ops);
    REQUIRE(s.r[20] == 30);
    REQUIRE_FALSE(getFlag(s, SregBit::C));
}

TEST_CASE("SBCI with carry in", "[instructions][arithmetic][Rd_K8]") {
    auto s = freshState();
    s.r[20] = 10;
    setFlag(s, SregBit::C);
    OpsRdK8 ops = {20, 3};
    executeSBCI(s, ops);
    REQUIRE(s.r[20] == 6);
}

TEST_CASE("SBCI preserves cleared Z across a zero high byte",
          "[instructions][arithmetic][regression]") {
    auto s = freshState();
    clearFlag(s, SregBit::Z);
    executeSBCI(s, {20, 0});
    REQUIRE(s.r[20] == 0);
    REQUIRE_FALSE(getFlag(s, SregBit::Z));
}

TEST_CASE("CPI equal → Z flag", "[instructions][compare][Rd_K8]") {
    auto s = freshState();
    s.r[20] = 42;
    OpsRdK8 ops = {20, 42};
    executeCPI(s, ops);
    REQUIRE(getFlag(s, SregBit::Z));
}

TEST_CASE("SER → 0xFF", "[instructions][logic][Rd_K8]") {
    auto s = freshState();
    OpsRdK8 ops = {16, 0xFF};
    executeSER(s, ops);
    REQUIRE(s.r[16] == 0xFF);
}

// ===========================================================================
// Rd_only format
// ===========================================================================

TEST_CASE("INC R3 → 1", "[instructions][arithmetic][Rd_only]") {
    auto s = freshState();
    OpsRd ops = {3};
    executeINC(s, ops);
    REQUIRE(s.r[3] == 1);
    REQUIRE_FALSE(getFlag(s, SregBit::Z));
}

TEST_CASE("INC 0xFF → 0x00 overflow", "[instructions][arithmetic][Rd_only]") {
    auto s = freshState();
    s.r[3] = 0xFF;
    OpsRd ops = {3};
    executeINC(s, ops);
    REQUIRE(s.r[3] == 0x00);
    REQUIRE(getFlag(s, SregBit::Z));
}

TEST_CASE("DEC 1 → 0", "[instructions][arithmetic][Rd_only]") {
    auto s = freshState();
    s.r[5] = 1;
    OpsRd ops = {5};
    executeDEC(s, ops);
    REQUIRE(s.r[5] == 0);
    REQUIRE(getFlag(s, SregBit::Z));
}

TEST_CASE("DEC 0 → 0xFF underflow", "[instructions][arithmetic][Rd_only]") {
    auto s = freshState();
    OpsRd ops = {5};
    executeDEC(s, ops);
    REQUIRE(s.r[5] == 0xFF);
    REQUIRE(getFlag(s, SregBit::N));
}

TEST_CASE("COM 0x0F → 0xF0", "[instructions][logic][Rd_only]") {
    auto s = freshState();
    s.r[5] = 0x0F;
    OpsRd ops = {5};
    executeCOM(s, ops);
    REQUIRE(s.r[5] == 0xF0);
    REQUIRE(getFlag(s, SregBit::N));
    REQUIRE(getFlag(s, SregBit::C));
}

TEST_CASE("NEG 0 → 0", "[instructions][arithmetic][Rd_only]") {
    auto s = freshState();
    OpsRd ops = {5};
    executeNEG(s, ops);
    REQUIRE(s.r[5] == 0);
    REQUIRE(getFlag(s, SregBit::Z));
}

TEST_CASE("NEG 1 → 0xFF", "[instructions][arithmetic][Rd_only]") {
    auto s = freshState();
    s.r[5] = 1;
    OpsRd ops = {5};
    executeNEG(s, ops);
    REQUIRE(s.r[5] == 0xFF);
    REQUIRE(getFlag(s, SregBit::C));
    REQUIRE_FALSE(getFlag(s, SregBit::Z));
}

TEST_CASE("ASR 0x80 → 0xC0 (sign extend)", "[instructions][shift][Rd_only]") {
    auto s = freshState();
    s.r[5] = 0x80;
    OpsRd ops = {5};
    executeASR(s, ops);
    REQUIRE(s.r[5] == 0xC0);
    REQUIRE(getFlag(s, SregBit::N));
}

TEST_CASE("LSR 0x02 → 0x01", "[instructions][shift][Rd_only]") {
    auto s = freshState();
    s.r[5] = 0x02;
    OpsRd ops = {5};
    executeLSR(s, ops);
    REQUIRE(s.r[5] == 0x01);
    REQUIRE_FALSE(getFlag(s, SregBit::C));
}

TEST_CASE("LSR 0x03 → 0x01 carry out", "[instructions][shift][Rd_only]") {
    auto s = freshState();
    s.r[5] = 0x03;
    OpsRd ops = {5};
    executeLSR(s, ops);
    REQUIRE(s.r[5] == 0x01);
    REQUIRE(getFlag(s, SregBit::C));
}

TEST_CASE("ROR with carry in", "[instructions][shift][Rd_only]") {
    auto s = freshState();
    s.r[5] = 0x02;
    setFlag(s, SregBit::C);
    OpsRd ops = {5};
    executeROR(s, ops);
    REQUIRE(s.r[5] == 0x81);
}

TEST_CASE("SWAP 0xAB → 0xBA", "[instructions][logic][Rd_only]") {
    auto s = freshState();
    s.r[5] = 0xAB;
    OpsRd ops = {5};
    executeSWAP(s, ops);
    REQUIRE(s.r[5] == 0xBA);
}

// ===========================================================================
// PUSH / POP stack operations
// ===========================================================================

TEST_CASE("PUSH then POP restores register", "[instructions][stack]") {
    auto s = freshState();
    s.r[5] = 0x42;
    uint16_t origSp = s.sp;
    OpsRd ops = {5};
    executePUSH(s, ops);
    REQUIRE(s.sp == origSp - 1);
    REQUIRE(readDataByte(s, origSp) == 0x42);
    s.r[5] = 0x00;
    executePOP(s, ops);
    REQUIRE(s.r[5] == 0x42);
    REQUIRE(s.sp == origSp);
}

TEST_CASE("pushWord/popWord round-trip 0xABCD", "[instructions][stack]") {
    auto s = freshState();
    pushWord(s, 0xABCD);
    // pushWord pushes low byte (0xCD) first, then high byte (0xAB)
    // So: [SP+1]=0xAB, [SP+2]=0xCD after both post-decrements.
    REQUIRE(readDataByte(s, s.sp + 1) == 0xAB);
    REQUIRE(readDataByte(s, s.sp + 2) == 0xCD);
    uint16_t w = popWord(s);
    REQUIRE(w == 0xABCD);
}

TEST_CASE("compiler-style stack frame restores saved pointer registers",
          "[instructions][stack][regression]") {
    auto s = freshState();
    const uint16_t originalSp = s.sp;
    s.r[28] = 0x34;
    s.r[29] = 0x12;

    // Equivalent to avr-gcc's PUSH r28; PUSH r29; IN Y,SP prologue.
    pushByte(s, s.r[28]);
    pushByte(s, s.r[29]);
    writeRegWord(s, 28, s.sp);

    // avr-libc epilogues restore the saved pair from Y+1 and Y+2.
    uint8_t savedHigh = readDataByte(s, readRegWord(s, 28) + 1);
    uint8_t savedLow = readDataByte(s, readRegWord(s, 28) + 2);
    s.r[29] = savedHigh;
    s.r[28] = savedLow;
    s.sp = originalSp;

    REQUIRE(readRegWord(s, 28) == 0x1234);
}

// ===========================================================================
// ADIW / SBIW
// ===========================================================================

TEST_CASE("ADIW 0x00FF + 1 → 0x0100", "[instructions][arithmetic]") {
    auto s = freshState();
    writeRegWord(s, 24, 0x00FF);
    OpsRd06K6 ops = {24, 1};
    executeADIW(s, ops);
    REQUIRE(readRegWord(s, 24) == 0x0100);
}

TEST_CASE("SBIW 0x0100 - 1 → 0x00FF", "[instructions][arithmetic]") {
    auto s = freshState();
    writeRegWord(s, 24, 0x0100);
    OpsRd06K6 ops = {24, 1};
    executeSBIW(s, ops);
    REQUIRE(readRegWord(s, 24) == 0x00FF);
}

TEST_CASE("ADIW sets signed overflow without carry at 0x7FFF + 1",
          "[instructions][arithmetic][regression]") {
    auto s = freshState();
    writeRegWord(s, 24, 0x7FFF);
    executeADIW(s, {24, 1});
    REQUIRE(readRegWord(s, 24) == 0x8000);
    REQUIRE(getFlag(s, SregBit::V));
    REQUIRE(getFlag(s, SregBit::N));
    REQUIRE_FALSE(getFlag(s, SregBit::C));
    REQUIRE_FALSE(getFlag(s, SregBit::S));
}

TEST_CASE("ADIW sets carry without signed overflow at 0xFFFF + 1",
          "[instructions][arithmetic][regression]") {
    auto s = freshState();
    writeRegWord(s, 24, 0xFFFF);
    executeADIW(s, {24, 1});
    REQUIRE(readRegWord(s, 24) == 0x0000);
    REQUIRE_FALSE(getFlag(s, SregBit::V));
    REQUIRE(getFlag(s, SregBit::Z));
    REQUIRE(getFlag(s, SregBit::C));
}

TEST_CASE("SBIW sets borrow without signed overflow at 0x0000 - 1",
          "[instructions][arithmetic][regression]") {
    auto s = freshState();
    writeRegWord(s, 24, 0x0000);
    executeSBIW(s, {24, 1});
    REQUIRE(readRegWord(s, 24) == 0xFFFF);
    REQUIRE_FALSE(getFlag(s, SregBit::V));
    REQUIRE(getFlag(s, SregBit::N));
    REQUIRE(getFlag(s, SregBit::C));
}

TEST_CASE("SBIW sets signed overflow without borrow at 0x8000 - 1",
          "[instructions][arithmetic][regression]") {
    auto s = freshState();
    writeRegWord(s, 24, 0x8000);
    executeSBIW(s, {24, 1});
    REQUIRE(readRegWord(s, 24) == 0x7FFF);
    REQUIRE(getFlag(s, SregBit::V));
    REQUIRE_FALSE(getFlag(s, SregBit::N));
    REQUIRE_FALSE(getFlag(s, SregBit::C));
    REQUIRE(getFlag(s, SregBit::S));
}

// ===========================================================================
// MOVW
// ===========================================================================

TEST_CASE("MOVW copies word register pair", "[instructions][datatransfer]") {
    auto s = freshState();
    writeRegWord(s, 4, 0xABCD);   // R5:R4
    OpsRd06Rr06 ops = {0, 2};     // raw nibbles: d→R0:R1, r→R4:R5
    executeMOVW(s, ops);
    REQUIRE(readRegWord(s, 0) == 0xABCD);
}

// ===========================================================================
// MUL
// ===========================================================================

TEST_CASE("MUL 10*20 = 200", "[instructions][arithmetic]") {
    auto s = freshState();
    s.r[5] = 10;
    s.r[10] = 20;
    OpsRdRr ops = {5, 10};
    executeMUL(s, ops);
    REQUIRE(readRegWord(s, 0) == 200);
}

TEST_CASE("MUL 0xFF*0xFF = 0xFE01", "[instructions][arithmetic]") {
    auto s = freshState();
    s.r[5] = 0xFF;
    s.r[10] = 0xFF;
    OpsRdRr ops = {5, 10};
    executeMUL(s, ops);
    REQUIRE(readRegWord(s, 0) == 0xFE01);
    REQUIRE(getFlag(s, SregBit::C));
}

// ===========================================================================
// Branch instructions
// ===========================================================================

TEST_CASE("BREQ taken (Z=1)", "[instructions][branch]") {
    auto s = freshState();
    setFlag(s, SregBit::Z);
    uint16_t origPc = s.pc;
    OpsK7 ops = {5, 1}; // k=5 words forward, s=1 (Z flag)
    executeBRBS(s, ops);
    REQUIRE(s.pc == origPc + 10);
}

TEST_CASE("BREQ not taken (Z=0)", "[instructions][branch]") {
    auto s = freshState();
    clearFlag(s, SregBit::Z);
    uint16_t origPc = s.pc;
    OpsK7 ops = {5, 1};
    executeBRBS(s, ops);
    REQUIRE(s.pc == origPc);
}

TEST_CASE("BRNE taken (Z=0)", "[instructions][branch]") {
    auto s = freshState();
    clearFlag(s, SregBit::Z);
    uint16_t origPc = s.pc;
    OpsK7 ops = {5, 1}; // BRBC with s=1, Z cleared → branch
    executeBRBC(s, ops);
    REQUIRE(s.pc == origPc + 10);
}

TEST_CASE("BRBS backward branch", "[instructions][branch]") {
    auto s = freshState();
    s.pc = 0x100;
    setFlag(s, SregBit::Z);
    OpsK7 ops = {-4, 1}; // k=-4 words (-8 bytes)
    executeBRBS(s, ops);
    REQUIRE(s.pc == 0x100 - 8);
}

// ===========================================================================
// RJMP / RCALL
// ===========================================================================

TEST_CASE("RJMP forward 10 words", "[instructions][jump]") {
    auto s = freshState();
    uint16_t origPc = s.pc;
    OpsK02 ops = {10};
    executeRJMP(s, ops);
    REQUIRE(s.pc == origPc + 20);
}

TEST_CASE("RCALL pushes return address", "[instructions][call]") {
    auto s = freshState();
    uint16_t origPc = s.pc;
    OpsK02 ops = {10};
    executeRCALL(s, ops);
    REQUIRE(s.pc == origPc + 20);
    REQUIRE(s.sp == 0x08FD); // pushed 2 bytes
}

// ===========================================================================
// JMP / CALL (32-bit)
// ===========================================================================

TEST_CASE("JMP sets PC to word*2", "[instructions][jump]") {
    auto s = freshState();
    OpsK22 ops = {0x0100}; // word addr, byte target = 0x0200
    executeJMP(s, ops);
    REQUIRE(s.pc == 0x0200);
}

TEST_CASE("CALL pushes return and jumps", "[instructions][call]") {
    auto s = freshState();
    uint16_t origSp = s.sp;
    OpsK22 ops = {0x0100};
    executeCALL(s, ops);
    REQUIRE(s.pc == 0x0200);
    REQUIRE(s.sp == origSp - 2); // pushed 2 bytes
}

// ===========================================================================
// IJMP / ICALL (word → byte conversion)
// ===========================================================================

TEST_CASE("IJMP converts word address to byte", "[instructions][jump]") {
    auto s = freshState();
    writeRegWord(s, 30, 0x0050); // Z = word address
    executeIJMP(s);
    REQUIRE(s.pc == 0x00A0); // word * 2 = byte
}

TEST_CASE("ICALL pushes return and jumps via Z", "[instructions][call]") {
    auto s = freshState();
    writeRegWord(s, 30, 0x0050);
    executeICALL(s);
    REQUIRE(s.pc == 0x00A0);
    REQUIRE(s.sp == 0x08FD);
}

// ===========================================================================
// RET / RETI
// ===========================================================================

TEST_CASE("RET restores PC from stack", "[instructions][call]") {
    auto s = freshState();
    // After pushWord, SP is below both bytes: high at SP+1, low at SP+2.
    s.sp = 0x08FD;
    writeDataByte(s, 0x08FE, 0x12);
    writeDataByte(s, 0x08FF, 0x34);
    executeRET(s);
    REQUIRE(s.pc == 0x1234);
    REQUIRE(s.sp == 0x08FF);
}

TEST_CASE("RETI restores PC and sets I", "[instructions][call]") {
    auto s = freshState();
    s.sp = 0x08FD;
    writeDataByte(s, 0x08FE, 0x56);
    writeDataByte(s, 0x08FF, 0x78);
    executeRETI(s);
    REQUIRE(s.pc == 0x5678);
    REQUIRE(getFlag(s, SregBit::I));
}

// ===========================================================================
// LD/ST X
// ===========================================================================

TEST_CASE("LD X loads byte from SRAM", "[instructions][datatransfer]") {
    auto s = freshState();
    writeRegWord(s, 26, 0x0300);
    s.sram[0x0300] = 0xAB;
    OpsLdSt ops = {5, 0}; // d=R5, mode=0 (base)
    executeLD_X(s, ops);
    REQUIRE(s.r[5] == 0xAB);
}

TEST_CASE("LD X+ post-increments by 1", "[instructions][datatransfer]") {
    auto s = freshState();
    writeRegWord(s, 26, 0x0300);
    s.sram[0x0300] = 0xCD;
    OpsLdSt ops = {5, 1}; // mode=1 (post-inc)
    executeLD_X(s, ops);
    REQUIRE(s.r[5] == 0xCD);
    REQUIRE(readRegWord(s, 26) == 0x0301); // byte increment
}

TEST_CASE("LD -X pre-decrements by 1", "[instructions][datatransfer]") {
    auto s = freshState();
    writeRegWord(s, 26, 0x0302);
    s.sram[0x0301] = 0xEF;
    OpsLdSt ops = {5, 2}; // mode=2 (pre-dec)
    executeLD_X(s, ops);
    REQUIRE(s.r[5] == 0xEF);
    REQUIRE(readRegWord(s, 26) == 0x0301);
}

TEST_CASE("ST X stores byte to SRAM", "[instructions][datatransfer]") {
    auto s = freshState();
    writeRegWord(s, 26, 0x0300);
    s.r[5] = 0x99;
    OpsLdSt ops = {5, 0};
    executeST_X(s, ops);
    REQUIRE(s.sram[0x0300] == 0x99);
}

// ===========================================================================
// LPM
// ===========================================================================

TEST_CASE("LPM Rd,Z loads from flash at byte address", "[instructions][datatransfer]") {
    auto s = freshState();
    writeRegWord(s, 30, 0x0050);
    s.flash[0x0050] = 0x37;
    OpsRd ops = {5};
    executeLPM(s, ops, 0);
    REQUIRE(s.r[5] == 0x37);
}

TEST_CASE("LPM Rd,Z+ post-increments Z by 1", "[instructions][datatransfer]") {
    auto s = freshState();
    writeRegWord(s, 30, 0x0050);
    s.flash[0x0050] = 0x42;
    OpsRd ops = {5};
    executeLPM(s, ops, 1);
    REQUIRE(s.r[5] == 0x42);
    REQUIRE(readRegWord(s, 30) == 0x0051);
}

TEST_CASE("LPM R0,Z (implied register)", "[instructions][datatransfer]") {
    auto s = freshState();
    writeRegWord(s, 30, 0x0050);
    s.flash[0x0050] = 0x77;
    OpsRd ops = {0};
    executeLPM(s, ops, 0);
    REQUIRE(s.r[0] == 0x77);
}

// ===========================================================================
// IN / OUT
// ===========================================================================

TEST_CASE("IN reads from I/O address", "[instructions][datatransfer]") {
    auto s = freshState();
    s.sram[0x25] = 0xCC; // I/O 0x05 → data-space 0x25
    OpsRdIO ops = {5, 0x05};
    executeIN(s, ops);
    REQUIRE(s.r[5] == 0xCC);
}

TEST_CASE("OUT writes to I/O address", "[instructions][datatransfer]") {
    auto s = freshState();
    s.r[10] = 0xDD;
    OpsIORr ops = {0x05, 10};
    executeOUT(s, ops);
    REQUIRE(s.sram[0x25] == 0xDD);
}

// ===========================================================================
// LDS / STS
// ===========================================================================

TEST_CASE("LDS loads from 16-bit SRAM addr", "[instructions][datatransfer]") {
    auto s = freshState();
    s.sram[0x01FF] = 0x88;
    OpsLdsSts ops = {5, 0x01FF};
    executeLDS(s, ops);
    REQUIRE(s.r[5] == 0x88);
}

TEST_CASE("STS stores to 16-bit SRAM addr", "[instructions][datatransfer]") {
    auto s = freshState();
    s.r[5] = 0x99;
    OpsLdsSts ops = {5, 0x01FF};
    executeSTS(s, ops);
    REQUIRE(s.sram[0x01FF] == 0x99);
}

// ===========================================================================
// SREG flag set/clear
// ===========================================================================

TEST_CASE("BSET sets SREG bit", "[instructions][bit]") {
    auto s = freshState();
    OpsBOnly ops = {0}; // Carry
    executeBSET(s, ops);
    REQUIRE(getFlag(s, SregBit::C));
}

TEST_CASE("BCLR clears SREG bit", "[instructions][bit]") {
    auto s = freshState();
    setFlag(s, SregBit::Z);
    OpsBOnly ops = {1}; // Zero
    executeBCLR(s, ops);
    REQUIRE_FALSE(getFlag(s, SregBit::Z));
}

// ===========================================================================
// BLD / BST
// ===========================================================================

TEST_CASE("BST stores register bit 3 → T", "[instructions][bit]") {
    auto s = freshState();
    s.r[5] = 0x08;
    OpsRdB ops = {5, 3};
    executeBST(s, ops);
    REQUIRE(getFlag(s, SregBit::T));
}

TEST_CASE("BLD loads T flag into register bit 3", "[instructions][bit]") {
    auto s = freshState();
    setFlag(s, SregBit::T);
    OpsRdB ops = {5, 3};
    executeBLD(s, ops);
    REQUIRE(s.r[5] == 0x08);
}

// ===========================================================================
// SBRC / SBRS
// ===========================================================================

TEST_CASE("SBRC skips when bit is clear", "[instructions][skip]") {
    auto s = freshState();
    s.r[5] = 0xF7; // bit 3 = 0
    uint16_t origPc = s.pc;
    OpsRrB ops = {5, 3};
    executeSBRC(s, ops);
    // skipNextInstruction advances PC by nextOp.words*2 = 2 (NOP at PC=0)
    REQUIRE(s.pc == origPc + 2);
}

TEST_CASE("SBRC does not skip when bit is set", "[instructions][skip]") {
    auto s = freshState();
    s.r[5] = 0x08; // bit 3 = 1
    uint16_t origPc = s.pc;
    OpsRrB ops = {5, 3};
    executeSBRC(s, ops);
    REQUIRE(s.pc == origPc);
}

TEST_CASE("SBRS skips when bit is set", "[instructions][skip]") {
    auto s = freshState();
    s.r[5] = 0x08;
    uint16_t origPc = s.pc;
    OpsRrB ops = {5, 3};
    executeSBRS(s, ops);
    REQUIRE(s.pc == origPc + 2);
}

// ===========================================================================
// SBI / CBI
// ===========================================================================

TEST_CASE("SBI sets I/O register bit", "[instructions][bit]") {
    auto s = freshState();
    OpsIOB ops = {0x05, 3};
    executeSBI(s, ops);
    REQUIRE(s.sram[0x25] == 0x08);
}

TEST_CASE("CBI clears I/O register bit", "[instructions][bit]") {
    auto s = freshState();
    s.sram[0x25] = 0xFF;
    OpsIOB ops = {0x05, 3};
    executeCBI(s, ops);
    REQUIRE(s.sram[0x25] == 0xF7);
}

// ===========================================================================
// SBIC / SBIS (skip when I/O bit condition met)
// ===========================================================================

TEST_CASE("SBIC skips when I/O bit is clear", "[instructions][skip]") {
    auto s = freshState();
    s.sram[0x25] = 0xF7; // bit 3 = 0
    uint16_t origPc = s.pc;
    OpsIOB ops = {0x05, 3};
    executeSBIC(s, ops);
    REQUIRE(s.pc == origPc + 2);
}

TEST_CASE("SBIS skips when I/O bit is set", "[instructions][skip]") {
    auto s = freshState();
    s.sram[0x25] = 0x08; // bit 3 = 1
    uint16_t origPc = s.pc;
    OpsIOB ops = {0x05, 3};
    executeSBIS(s, ops);
    REQUIRE(s.pc == origPc + 2);
}

// ===========================================================================
// MCU control
// ===========================================================================

TEST_CASE("NOP does nothing", "[instructions][mcu]") {
    auto s = freshState();
    uint16_t origPc = s.pc;
    uint8_t origSreg = s.sreg;
    executeNOP(s);
    REQUIRE(s.pc == origPc);
    REQUIRE(s.sreg == origSreg);
}

TEST_CASE("SLEEP/WDR/BREAK/SPM are no-op (no crash)", "[instructions][mcu]") {
    auto s = freshState();
    executeSLEEP(s);
    executeWDR(s);
    executeBREAK(s);
    executeSPM(s);
    // Verify no crash — just reaching here is success
    REQUIRE(true);
}

// ===========================================================================
// LDD / STD (indirect with displacement)
// ===========================================================================

TEST_CASE("LDD Y+5 loads from Y+5", "[instructions][datatransfer]") {
    auto s = freshState();
    writeRegWord(s, 28, 0x0200); // Y = 0x0200
    s.sram[0x0205] = 0xAB;
    OpsLdd ops = {5, 5}; // d=R5, q=5
    executeLDD_Y(s, ops);
    REQUIRE(s.r[5] == 0xAB);
}

TEST_CASE("STD Y+3 stores to Y+3", "[instructions][datatransfer]") {
    auto s = freshState();
    writeRegWord(s, 28, 0x0200);
    s.r[5] = 0xCD;
    OpsLdd ops = {5, 3};
    executeSTD_Y(s, ops);
    REQUIRE(s.sram[0x0203] == 0xCD);
}

TEST_CASE("LDD Z+16 loads from Z+16", "[instructions][datatransfer]") {
    auto s = freshState();
    writeRegWord(s, 30, 0x0100);
    s.sram[0x0110] = 0xEF;
    OpsLdd ops = {5, 16};
    executeLDD_Z(s, ops);
    REQUIRE(s.r[5] == 0xEF);
}

TEST_CASE("STD Z+10 stores to Z+10", "[instructions][datatransfer]") {
    auto s = freshState();
    writeRegWord(s, 30, 0x0100);
    s.r[5] = 0x12;
    OpsLdd ops = {5, 10};
    executeSTD_Z(s, ops);
    REQUIRE(s.sram[0x010A] == 0x12);
}

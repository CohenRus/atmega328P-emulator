// decoding hex opcodes
#pragma once
#include <cstdint>

// all the instructions
enum class AvrOp {
    ADC,    // Add with Carry
    ADD,    // Add without Carry
    ADIW,   // Add Immediate to Word
    AND,    // Logical AND
    ANDI,   // Logical AND with Immediate
    ASR,    // Arithmetic Shift Right
    BCLR,   // Bit Clear in SREG
    BLD,    // Bit Load from T Bit in SREG to Bit in Register
    BRBC,   // Branch if Bit in SREG is Cleared
    BRBS,   // Branch if Bit in SREG is Set
    BRCC,   // Branch if Carry Cleared
    BRCS,   // Branch if Carry Set
    BREAK,  // Break
    BREQ,   // Branch if Equal
    BRGE,   // Branch if Greater or Equal (Signed)
    BRHC,   // Branch if Half Carry Flag is Cleared
    BRHS,   // Branch if Half Carry Flag is Set
    BRID,   // Branch if Global Interrupt is Disabled
    BRIE,   // Branch if Global Interrupt is Enabled
    BRLO,   // Branch if Lower (Unsigned)
    BRLT,   // Branch if Less Than (Signed)
    BRMI,   // Branch if Minus
    BRNE,   // Branch if Not Equal
    BRPL,   // Branch if Plus
    BRSH,   // Branch if Same or Higher (Unsigned)
    BRTC,   // Branch if T Bit is Cleared
    BRTS,   // Branch if T Bit is Set
    BRVC,   // Branch if Overflow Cleared
    BRVS,   // Branch if Overflow Set
    BSET,   // Bit Set in SREG
    BST,    // Bit Store from Bit in Register to T Bit in SREG
    CALL,   // Long Call to a Subroutine
    CBI,    // Clear Bit in I/O Register
    CBR,    // Clear Bits in Register
    CLC,    // Clear Carry Flag
    CLH,    // Clear Half Carry Flag
    CLI,    // Clear Global Interrupt Enable Bit
    CLN,    // Clear Negative Flag
    CLR,    // Clear Register
    CLS,    // Clear Sign Flag
    CLT,    // Clear T Bit
    CLV,    // Clear Overflow Flag
    CLZ,    // Clear Zero Flag
    COM,    // One's Complement
    CP,     // Compare
    CPC,    // Compare with Carry
    CPI,    // Compare with Immediate
    CPSE,   // Compare Skip if Equal
    DEC,    // Decrement
    DES,    // Data Encryption Standard
    EOR,    // Exclusive OR
    FMUL,   // Fractional Multiply Unsigned
    FMULS,  // Fractional Multiply Signed
    FMULSU, // Fractional Multiply Signed with Unsigned
    ICALL,  // Indirect Call to Subroutine
    IJMP,   // Indirect Jump
    IN,     // Load an I/O Location to Register
    INC,    // Increment
    JMP,    // Jump
    LAC,    // Load and Clear
    LAS,    // Load and Set
    LAT,    // Load and Toggle
    LD_X,   // Load Indirect from Data Space using X
    LD_Y,   // Load Indirect from Data Space using Y (LDD)
    LD_Z,   // Load Indirect from Data Space using Z (LDD)
    LDI,    // Load Immediate
    LDS,    // Load Direct from Data Space
    LDS_RC, // Load Direct from Data Space (AVRrc)
    LPM,    // Load Program Memory
    LSL,    // Logical Shift Left
    LSR,    // Logical Shift Right
    MOV,    // Copy Register
    MOVW,   // Copy Register Word
    MUL,    // Multiply Unsigned
    MULS,   // Multiply Signed
    MULSU,  // Multiply Signed with Unsigned
    NEG,    // Two's Complement
    NOP,    // No Operation
    OR,     // Logical OR
    ORI,    // Logical OR with Immediate
    OUT,    // Store Register to I/O Location
    POP,    // Pop Register from Stack
    PUSH,   // Push Register on Stack
    RCALL,  // Relative Call to Subroutine
    RET,    // Return from Subroutine
    RETI,   // Return from Interrupt
    RJMP,   // Relative Jump
    ROL,    // Rotate Left through Carry
    ROR,    // Rotate Right through Carry
    SBC,    // Subtract with Carry
    SBCI,   // Subtract Immediate with Carry
    SBI,    // Set Bit in I/O Register
    SBIC,   // Skip if Bit in I/O Register is Cleared
    SBIS,   // Skip if Bit in I/O Register is Set
    SBIW,   // Subtract Immediate from Word
    SBR,    // Set Bits in Register
    SBRC,   // Skip if Bit in Register is Cleared
    SBRS,   // Skip if Bit in Register is Set
    SEC,    // Set Carry Flag
    SEH,    // Set Half Carry Flag
    SEI,    // Set Global Interrupt Enable Bit
    SEN,    // Set Negative Flag
    SER,    // Set all Bits in Register
    SES,    // Set Sign Flag
    SET,    // Set T Bit
    SEV,    // Set Overflow Flag
    SEZ,    // Set Zero Flag
    SLEEP,  // Sleep
    SPM_E,  // Store Program Memory (AVRe)
    SPM_XM, // Store Program Memory (AVRxm, AVRxt)
    ST_X,   // Store Indirect to Data Space using Index X
    ST_Y,   // Store Indirect to Data Space using Index Y (STD)
    ST_Z,   // Store Indirect to Data Space using Index Z (STD)
    STS,    // Store Direct to Data Space
    STS_RC, // Store Direct to Data Space (AVRrc)
    SUB,    // Subtract Without Carry
    SUBI,   // Subtract Immediate
    SWAP,   // Swap Nibbles
    TST,    // Test for Zero or Minus
    WDR,    // Watchdog Reset
    XCH,    // Exchange
};

// Operand encoding types
enum class AvrFmt {
    NONE,       // NOP, RET, RETI, SLEEP, WDR, BREAK
    Rd_Rr,      // 0000 00rd dddd rrrr  — ADD, ADC, AND, CP, EOR, MOV, OR, SBC, SUB
    Rd_K8,      // 0000 KKKK dddd KKKK  — LDI, ANDI, ORI, SUBI, SBCI, CPI  (d: 16..31)
    Rd_only,    // 0000 000d dddd xxxx  — INC, DEC, COM, NEG, PUSH, POP, etc.
    Rd_Rr_mpy,  // 0000 0000 0ddd 0rrr  — MULSU, FMUL, FMULS, FMULSU  (d,r: 16..23)
    Rd06_K6,    // 0000 0000 KKdd KKKK  — ADIW, SBIW  (dd: {24,26,28,30})
    Rd06_Rr06,  // 0000 0000 dddd rrrr  — MOVW  (d,r even, /2)
    b_only,     // 0000 0000 0bbb 0000  — BSET/BCLR
    Rd_b,       // 0000 000d dddd 0bbb  — BLD/BST
    Rr_b,       // 0000 000r rrrr 0bbb  — SBRC/SBRS
    IO_b,       // 0000 0000 AAAA Abbb  — SBI/CBI/SBIC/SBIS
    Rd_IO,      // 0000 0AAd dddd AAAA  — IN
    IO_Rr,      // 0000 0AAr rrrr AAAA  — OUT
    k7,         // 0000 0kkk kkkk ksss  — BRBC/BRBS
    k02,        // 0000 kkkk kkkk kkkk  — RJMP
    k02_call,   // 0000 kkkk kkkk kkkk  — RCALL
    k22,        // 0000 000k kkkk 000k kkkk kkkk kkkk kkkk  — JMP (32-bit)
    k22_call,   // 0000 000k kkkk 000k kkkk kkkk kkkk kkkk  — CALL (32-bit)
    LD_family,  // various LD/ST modes (X/Y/Z with pre/post inc/dec)
    LDD_family, // LDD/STD Rd, Y+q / Z+q
    LDS_STS,    // 32-bit: 0000 00xd dddd 0000 kkkk kkkk kkkk kkkk
};

struct OpcodeEntry {
    uint16_t    mask;       // bits to AND before comparing
    uint16_t    pattern;    // what the result must equal
    AvrOp       op;
    AvrFmt      fmt;
    uint8_t     words;      // 1 = 16-bit, 2 = 32-bit instruction
    uint8_t     cycles_min; // base cycles (branching may add 1)
    const char* mnemonic;   // for disassembly / debugging
};

static const OpcodeEntry OPCODE_TABLE[] = {
    // mask   pattern   op           fmt        words  cyc  mnemonic
    { 0xFC00, 0x1C00, AvrOp::ADC,  AvrFmt::Rd_Rr, 1, 1, "ADC"  },
    { 0xFC00, 0x0C00, AvrOp::ADD,  AvrFmt::Rd_Rr, 1, 1, "ADD"  },
};

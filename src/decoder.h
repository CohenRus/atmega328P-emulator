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
    EOR,    // Exclusive OR
    FMUL,   // Fractional Multiply Unsigned
    FMULS,  // Fractional Multiply Signed
    FMULSU, // Fractional Multiply Signed with Unsigned
    ICALL,  // Indirect Call to Subroutine
    IJMP,   // Indirect Jump
    IN,     // Load an I/O Location to Register
    INC,    // Increment
    JMP,    // Jump
    LD_X,   // Load Indirect from Data Space using X
    LD_Y,   // Load Indirect from Data Space using Y (LDD)
    LD_Z,   // Load Indirect from Data Space using Z (LDD)
    LDI,    // Load Immediate
    LDS,    // Load Direct from Data Space
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
    ST_X,   // Store Indirect to Data Space using Index X
    ST_Y,   // Store Indirect to Data Space using Index Y (STD)
    ST_Z,   // Store Indirect to Data Space using Index Z (STD)
    STS,    // Store Direct to Data Space
    SUB,    // Subtract Without Carry
    SUBI,   // Subtract Immediate
    SWAP,   // Swap Nibbles
    TST,    // Test for Zero or Minus
    WDR,    // Watchdog Reset
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

// opcode table entry
struct Opcode {
    uint16_t    mask;       // bits to AND before comparing
    uint16_t    code;    // what the result must equal
    AvrOp       op;
    AvrFmt      fmt;
    uint8_t     words;      // 1 = 16-bit, 2 = 32-bit instruction
    uint8_t     cycles_min; // base cycles (branching may add 1)
    const char* mnemonic;   // for disassembly / debugging
};

// every opcode with its mask, pattern, op, fmt, words, cycles, and mnemonic
static const Opcode OPCODE_TABLE[] = {
    // mask   code   op                    fmt              words  cyc  mnemonic
 
    // -----------------------------------------------------------------------
    // Arithmetic
    // -----------------------------------------------------------------------
    { 0xFC00, 0x1C00, AvrOp::ADC,    AvrFmt::Rd_Rr,     1, 1, "ADC"    },  // 0001 11rd dddd rrrr
    { 0xFC00, 0x0C00, AvrOp::ADD,    AvrFmt::Rd_Rr,     1, 1, "ADD"    },  // 0000 11rd dddd rrrr
    { 0xFF00, 0x9600, AvrOp::ADIW,   AvrFmt::Rd06_K6,   1, 2, "ADIW"   },  // 1001 0110 KKdd KKKK
    { 0xFE0F, 0x9405, AvrOp::ASR,    AvrFmt::Rd_only,   1, 1, "ASR"    },  // 1001 010d dddd 0101
    { 0xFE0F, 0x940A, AvrOp::DEC,    AvrFmt::Rd_only,   1, 1, "DEC"    },  // 1001 010d dddd 1010
    { 0xFE0F, 0x9403, AvrOp::INC,    AvrFmt::Rd_only,   1, 1, "INC"    },  // 1001 010d dddd 0011
    { 0xFC00, 0x9C00, AvrOp::MUL,    AvrFmt::Rd_Rr,     1, 2, "MUL"    },  // 1001 11rd dddd rrrr
    { 0xFF00, 0x0200, AvrOp::MULS,   AvrFmt::Rd06_Rr06, 1, 2, "MULS"   },  // 0000 0010 dddd rrrr
    { 0xFF88, 0x0300, AvrOp::MULSU,  AvrFmt::Rd_Rr_mpy, 1, 2, "MULSU"  },  // 0000 0011 0ddd 0rrr
    { 0xFF88, 0x0308, AvrOp::FMUL,   AvrFmt::Rd_Rr_mpy, 1, 2, "FMUL"   },  // 0000 0011 0ddd 1rrr
    { 0xFF88, 0x0380, AvrOp::FMULS,  AvrFmt::Rd_Rr_mpy, 1, 2, "FMULS"  },  // 0000 0011 1ddd 0rrr
    { 0xFF88, 0x0388, AvrOp::FMULSU, AvrFmt::Rd_Rr_mpy, 1, 2, "FMULSU" },  // 0000 0011 1ddd 1rrr
    { 0xFE0F, 0x9401, AvrOp::NEG,    AvrFmt::Rd_only,   1, 1, "NEG"    },  // 1001 010d dddd 0001
    { 0xFF00, 0x9700, AvrOp::SBIW,   AvrFmt::Rd06_K6,   1, 2, "SBIW"   },  // 1001 0111 KKdd KKKK
    { 0xFC00, 0x0800, AvrOp::SBC,    AvrFmt::Rd_Rr,     1, 1, "SBC"    },  // 0000 10rd dddd rrrr
    { 0xF000, 0x4000, AvrOp::SBCI,   AvrFmt::Rd_K8,     1, 1, "SBCI"   },  // 0100 KKKK dddd KKKK
    { 0xFC00, 0x1800, AvrOp::SUB,    AvrFmt::Rd_Rr,     1, 1, "SUB"    },  // 0001 10rd dddd rrrr
    { 0xF000, 0x5000, AvrOp::SUBI,   AvrFmt::Rd_K8,     1, 1, "SUBI"   },  // 0101 KKKK dddd KKKK
 
    // -----------------------------------------------------------------------
    // Logic
    // -----------------------------------------------------------------------
    { 0xFC00, 0x2000, AvrOp::AND,    AvrFmt::Rd_Rr,     1, 1, "AND"    },  // 0010 00rd dddd rrrr
    { 0xF000, 0x7000, AvrOp::ANDI,   AvrFmt::Rd_K8,     1, 1, "ANDI"   },  // 0111 KKKK dddd KKKK
    { 0xF000, 0x7000, AvrOp::CBR,    AvrFmt::Rd_K8,     1, 1, "CBR"    },  // alias: ANDI with ~K
    { 0xFC00, 0x2400, AvrOp::CLR,    AvrFmt::Rd_Rr,     1, 1, "CLR"    },  // alias: EOR Rd,Rd
    { 0xFE0F, 0x9400, AvrOp::COM,    AvrFmt::Rd_only,   1, 1, "COM"    },  // 1001 010d dddd 0000
    { 0xFC00, 0x2400, AvrOp::EOR,    AvrFmt::Rd_Rr,     1, 1, "EOR"    },  // 0010 01rd dddd rrrr
    { 0xFC00, 0x2800, AvrOp::OR,     AvrFmt::Rd_Rr,     1, 1, "OR"     },  // 0010 10rd dddd rrrr
    { 0xF000, 0x6000, AvrOp::ORI,    AvrFmt::Rd_K8,     1, 1, "ORI"    },  // 0110 KKKK dddd KKKK
    { 0xF000, 0x6000, AvrOp::SBR,    AvrFmt::Rd_K8,     1, 1, "SBR"    },  // alias: ORI
    { 0xFF0F, 0xEF0F, AvrOp::SER,    AvrFmt::Rd_K8,     1, 1, "SER"    },  // 1110 1111 dddd 1111
    { 0xFE0F, 0x9402, AvrOp::SWAP,   AvrFmt::Rd_only,   1, 1, "SWAP"   },  // 1001 010d dddd 0010
 
    // -----------------------------------------------------------------------
    // Shift / Rotate
    // -----------------------------------------------------------------------
    { 0xFC00, 0x0C00, AvrOp::LSL,    AvrFmt::Rd_Rr,     1, 1, "LSL"    },  // alias: ADD Rd,Rd
    { 0xFE0F, 0x9406, AvrOp::LSR,    AvrFmt::Rd_only,   1, 1, "LSR"    },  // 1001 010d dddd 0110
    { 0xFC00, 0x1C00, AvrOp::ROL,    AvrFmt::Rd_Rr,     1, 1, "ROL"    },  // alias: ADC Rd,Rd
    { 0xFE0F, 0x9407, AvrOp::ROR,    AvrFmt::Rd_only,   1, 1, "ROR"    },  // 1001 010d dddd 0111
 
    // -----------------------------------------------------------------------
    // Compare
    // -----------------------------------------------------------------------
    { 0xFC00, 0x1400, AvrOp::CP,     AvrFmt::Rd_Rr,     1, 1, "CP"     },  // 0001 01rd dddd rrrr
    { 0xFC00, 0x0400, AvrOp::CPC,    AvrFmt::Rd_Rr,     1, 1, "CPC"    },  // 0000 01rd dddd rrrr
    { 0xF000, 0x3000, AvrOp::CPI,    AvrFmt::Rd_K8,     1, 1, "CPI"    },  // 0011 KKKK dddd KKKK
    { 0xFC00, 0x1000, AvrOp::CPSE,   AvrFmt::Rd_Rr,     1, 1, "CPSE"   },  // 0001 00rd dddd rrrr
    { 0xFC00, 0x2000, AvrOp::TST,    AvrFmt::Rd_Rr,     1, 1, "TST"    },  // alias: AND Rd,Rd
 
    // -----------------------------------------------------------------------
    // Data Transfer
    // -----------------------------------------------------------------------
    { 0xFC00, 0x2C00, AvrOp::MOV,    AvrFmt::Rd_Rr,     1, 1, "MOV"    },  // 0010 11rd dddd rrrr
    { 0xFF00, 0x0100, AvrOp::MOVW,   AvrFmt::Rd06_Rr06, 1, 1, "MOVW"   },  // 0000 0001 dddd rrrr
    { 0xF000, 0xE000, AvrOp::LDI,    AvrFmt::Rd_K8,     1, 1, "LDI"    },  // 1110 KKKK dddd KKKK
 
    // LD X: (i) ..1100  (ii) ..1101  (iii) ..1110  — mask covers all three
    { 0xFE0C, 0x900C, AvrOp::LD_X,   AvrFmt::LD_family, 1, 2, "LD X"   },  // 1001 000d dddd 11xx
 
    // LD Y: (i) 1000..1000  — mask for base; (ii)..1001 (iii)..1010 (iv)LDD q handled by decoder
    { 0xFE0F, 0x8008, AvrOp::LD_Y,   AvrFmt::LD_family, 1, 2, "LD Y"   },  // 1000 000d dddd 1000
    { 0xFE0F, 0x9009, AvrOp::LD_Y,   AvrFmt::LD_family, 1, 2, "LD Y+"  },  // 1001 000d dddd 1001
    { 0xFE0F, 0x900A, AvrOp::LD_Y,   AvrFmt::LD_family, 1, 2, "LD -Y"  },  // 1001 000d dddd 1010
    { 0xD208, 0x8008, AvrOp::LD_Y,   AvrFmt::LDD_family,1, 2, "LDD Y+q"},  // 10q0 qq0d dddd 1qqq
 
    // LD Z: (i) 1000..0000  (ii)..0001 (iii)..0010 (iv)LDD q
    { 0xFE0F, 0x8000, AvrOp::LD_Z,   AvrFmt::LD_family, 1, 2, "LD Z"   },  // 1000 000d dddd 0000
    { 0xFE0F, 0x9001, AvrOp::LD_Z,   AvrFmt::LD_family, 1, 2, "LD Z+"  },  // 1001 000d dddd 0001
    { 0xFE0F, 0x9002, AvrOp::LD_Z,   AvrFmt::LD_family, 1, 2, "LD -Z"  },  // 1001 000d dddd 0010
    { 0xD208, 0x8000, AvrOp::LD_Z,   AvrFmt::LDD_family,1, 2, "LDD Z+q"},  // 10q0 qq0d dddd 0qqq
 
    // LDS 32-bit (AVRe/AVRxm/AVRxt)
    { 0xFE0F, 0x9000, AvrOp::LDS,    AvrFmt::LDS_STS,   2, 2, "LDS"    },  // 1001 000d dddd 0000 + k16
 
    // LPM: (i) R0 implied  (ii) Rd,Z  (iii) Rd,Z+
    { 0xFFFF, 0x95C8, AvrOp::LPM,    AvrFmt::NONE,      1, 3, "LPM"    },  // 1001 0101 1100 1000
    { 0xFE0F, 0x9004, AvrOp::LPM,    AvrFmt::Rd_only,   1, 3, "LPM Rd,Z"  },  // 1001 000d dddd 0100
    { 0xFE0F, 0x9005, AvrOp::LPM,    AvrFmt::Rd_only,   1, 3, "LPM Rd,Z+" },  // 1001 000d dddd 0101
 
    { 0xF800, 0xB000, AvrOp::IN,     AvrFmt::Rd_IO,     1, 1, "IN"     },  // 1011 0AAd dddd AAAA
    { 0xF800, 0xB800, AvrOp::OUT,    AvrFmt::IO_Rr,     1, 1, "OUT"    },  // 1011 1AAr rrrr AAAA
    { 0xFE0F, 0x900F, AvrOp::POP,    AvrFmt::Rd_only,   1, 2, "POP"    },  // 1001 000d dddd 1111
    { 0xFE0F, 0x920F, AvrOp::PUSH,   AvrFmt::Rd_only,   1, 2, "PUSH"   },  // 1001 001d dddd 1111
 
    // ST X: (i) ..1100  (ii) ..1101  (iii) ..1110
    { 0xFE0C, 0x920C, AvrOp::ST_X,   AvrFmt::LD_family, 1, 2, "ST X"   },  // 1001 001r rrrr 11xx
 
    // ST Y: (i) 1000..1000  (ii)..1001 (iii)..1010 (iv)STD q
    { 0xFE0F, 0x8208, AvrOp::ST_Y,   AvrFmt::LD_family, 1, 2, "ST Y"   },  // 1000 001r rrrr 1000
    { 0xFE0F, 0x9209, AvrOp::ST_Y,   AvrFmt::LD_family, 1, 2, "ST Y+"  },  // 1001 001r rrrr 1001
    { 0xFE0F, 0x920A, AvrOp::ST_Y,   AvrFmt::LD_family, 1, 2, "ST -Y"  },  // 1001 001r rrrr 1010
    { 0xD208, 0x8208, AvrOp::ST_Y,   AvrFmt::LDD_family,1, 2, "STD Y+q"},  // 10q0 qq1r rrrr 1qqq
 
    // ST Z: (i) 1000..0000  (ii)..0001 (iii)..0010 (iv)STD q
    { 0xFE0F, 0x8200, AvrOp::ST_Z,   AvrFmt::LD_family, 1, 2, "ST Z"   },  // 1000 001r rrrr 0000
    { 0xFE0F, 0x9201, AvrOp::ST_Z,   AvrFmt::LD_family, 1, 2, "ST Z+"  },  // 1001 001r rrrr 0001
    { 0xFE0F, 0x9202, AvrOp::ST_Z,   AvrFmt::LD_family, 1, 2, "ST -Z"  },  // 1001 001r rrrr 0010
    { 0xD208, 0x8200, AvrOp::ST_Z,   AvrFmt::LDD_family,1, 2, "STD Z+q"},  // 10q0 qq1r rrrr 0qqq
 
    // STS 32-bit (AVRe/AVRxm/AVRxt)
    { 0xFE0F, 0x9200, AvrOp::STS,    AvrFmt::LDS_STS,   2, 2, "STS"    },  // 1001 001d dddd 0000 + k16
 
    // -----------------------------------------------------------------------
    // Branch / Jump / Call
    // -----------------------------------------------------------------------
    { 0xFC00, 0xF400, AvrOp::BRBC,   AvrFmt::k7,        1, 1, "BRBC"   },  // 1111 01kk kkkk ksss
    { 0xFC00, 0xF000, AvrOp::BRBS,   AvrFmt::k7,        1, 1, "BRBS"   },  // 1111 00kk kkkk ksss
    { 0xFC07, 0xF400, AvrOp::BRCC,   AvrFmt::k7,        1, 1, "BRCC"   },  // 1111 01kk kkkk k000
    { 0xFC07, 0xF000, AvrOp::BRCS,   AvrFmt::k7,        1, 1, "BRCS"   },  // 1111 00kk kkkk k000
    { 0xFC07, 0xF001, AvrOp::BREQ,   AvrFmt::k7,        1, 1, "BREQ"   },  // 1111 00kk kkkk k001
    { 0xFC07, 0xF404, AvrOp::BRGE,   AvrFmt::k7,        1, 1, "BRGE"   },  // 1111 01kk kkkk k100
    { 0xFC07, 0xF405, AvrOp::BRHC,   AvrFmt::k7,        1, 1, "BRHC"   },  // 1111 01kk kkkk k101
    { 0xFC07, 0xF005, AvrOp::BRHS,   AvrFmt::k7,        1, 1, "BRHS"   },  // 1111 00kk kkkk k101
    { 0xFC07, 0xF407, AvrOp::BRID,   AvrFmt::k7,        1, 1, "BRID"   },  // 1111 01kk kkkk k111
    { 0xFC07, 0xF007, AvrOp::BRIE,   AvrFmt::k7,        1, 1, "BRIE"   },  // 1111 00kk kkkk k111
    { 0xFC07, 0xF000, AvrOp::BRLO,   AvrFmt::k7,        1, 1, "BRLO"   },  // alias: BRCS
    { 0xFC07, 0xF004, AvrOp::BRLT,   AvrFmt::k7,        1, 1, "BRLT"   },  // 1111 00kk kkkk k100
    { 0xFC07, 0xF002, AvrOp::BRMI,   AvrFmt::k7,        1, 1, "BRMI"   },  // 1111 00kk kkkk k010
    { 0xFC07, 0xF401, AvrOp::BRNE,   AvrFmt::k7,        1, 1, "BRNE"   },  // 1111 01kk kkkk k001
    { 0xFC07, 0xF402, AvrOp::BRPL,   AvrFmt::k7,        1, 1, "BRPL"   },  // 1111 01kk kkkk k010
    { 0xFC07, 0xF400, AvrOp::BRSH,   AvrFmt::k7,        1, 1, "BRSH"   },  // alias: BRCC
    { 0xFC07, 0xF406, AvrOp::BRTC,   AvrFmt::k7,        1, 1, "BRTC"   },  // 1111 01kk kkkk k110
    { 0xFC07, 0xF006, AvrOp::BRTS,   AvrFmt::k7,        1, 1, "BRTS"   },  // 1111 00kk kkkk k110
    { 0xFC07, 0xF403, AvrOp::BRVC,   AvrFmt::k7,        1, 1, "BRVC"   },  // 1111 01kk kkkk k011
    { 0xFC07, 0xF003, AvrOp::BRVS,   AvrFmt::k7,        1, 1, "BRVS"   },  // 1111 00kk kkkk k011
    { 0xF000, 0xC000, AvrOp::RJMP,   AvrFmt::k02,       1, 2, "RJMP"   },  // 1100 kkkk kkkk kkkk
    { 0xFE0E, 0x940C, AvrOp::JMP,    AvrFmt::k22,       2, 3, "JMP"    },  // 1001 010k kkkk 110k + k16
    { 0xFFFF, 0x9409, AvrOp::IJMP,   AvrFmt::NONE,      1, 2, "IJMP"   },  // 1001 0100 0000 1001
    { 0xF000, 0xD000, AvrOp::RCALL,  AvrFmt::k02_call,  1, 3, "RCALL"  },  // 1101 kkkk kkkk kkkk
    { 0xFE0E, 0x940E, AvrOp::CALL,   AvrFmt::k22_call,  2, 4, "CALL"   },  // 1001 010k kkkk 111k + k16
    { 0xFFFF, 0x9509, AvrOp::ICALL,  AvrFmt::NONE,      1, 3, "ICALL"  },  // 1001 0101 0000 1001
    { 0xFFFF, 0x9508, AvrOp::RET,    AvrFmt::NONE,      1, 4, "RET"    },  // 1001 0101 0000 1000
    { 0xFFFF, 0x9518, AvrOp::RETI,   AvrFmt::NONE,      1, 4, "RETI"   },  // 1001 0101 0001 1000
 
    // -----------------------------------------------------------------------
    // Skip instructions
    // -----------------------------------------------------------------------
    { 0xFF00, 0x9900, AvrOp::SBIC,   AvrFmt::IO_b,      1, 1, "SBIC"   },  // 1001 1001 AAAA Abbb
    { 0xFF00, 0x9B00, AvrOp::SBIS,   AvrFmt::IO_b,      1, 1, "SBIS"   },  // 1001 1011 AAAA Abbb
    { 0xFE08, 0xFC00, AvrOp::SBRC,   AvrFmt::Rr_b,      1, 1, "SBRC"   },  // 1111 110r rrrr 0bbb
    { 0xFE08, 0xFE00, AvrOp::SBRS,   AvrFmt::Rr_b,      1, 1, "SBRS"   },  // 1111 111r rrrr 0bbb
 
    // -----------------------------------------------------------------------
    // Bit manipulation
    // -----------------------------------------------------------------------
    { 0xFF8F, 0x9408, AvrOp::BSET,   AvrFmt::b_only,    1, 1, "BSET"   },  // 1001 0100 0sss 1000
    { 0xFF8F, 0x9488, AvrOp::BCLR,   AvrFmt::b_only,    1, 1, "BCLR"   },  // 1001 0100 1sss 1000
    { 0xFE08, 0xF800, AvrOp::BLD,    AvrFmt::Rd_b,      1, 1, "BLD"    },  // 1111 100d dddd 0bbb
    { 0xFE08, 0xFA00, AvrOp::BST,    AvrFmt::Rd_b,      1, 1, "BST"    },  // 1111 101d dddd 0bbb
    { 0xFF00, 0x9800, AvrOp::CBI,    AvrFmt::IO_b,      1, 2, "CBI"    },  // 1001 1000 AAAA Abbb
    { 0xFF00, 0x9A00, AvrOp::SBI,    AvrFmt::IO_b,      1, 2, "SBI"    },  // 1001 1010 AAAA Abbb
 
    // SREG flag set/clear aliases (all encoded as BSET/BCLR with fixed sss)
    { 0xFFFF, 0x9408, AvrOp::BSET,   AvrFmt::NONE,      1, 1, "SEC"    },  // sss=000  (SEC = BSET 0)
    { 0xFFFF, 0x9428, AvrOp::BSET,   AvrFmt::NONE,      1, 1, "SEN"    },  // sss=010  (SEN = BSET 2)  -- kept as named ops below
    { 0xFFFF, 0x9408, AvrOp::SEC,    AvrFmt::NONE,      1, 1, "SEC"    },  // 1001 0100 0000 1000
    { 0xFFFF, 0x9418, AvrOp::SEZ,    AvrFmt::NONE,      1, 1, "SEZ"    },  // 1001 0100 0001 1000
    { 0xFFFF, 0x9428, AvrOp::SEN,    AvrFmt::NONE,      1, 1, "SEN"    },  // 1001 0100 0010 1000
    { 0xFFFF, 0x9438, AvrOp::SEV,    AvrFmt::NONE,      1, 1, "SEV"    },  // 1001 0100 0011 1000
    { 0xFFFF, 0x9448, AvrOp::SES,    AvrFmt::NONE,      1, 1, "SES"    },  // 1001 0100 0100 1000
    { 0xFFFF, 0x9458, AvrOp::SEH,    AvrFmt::NONE,      1, 1, "SEH"    },  // 1001 0100 0101 1000
    { 0xFFFF, 0x9468, AvrOp::SET,    AvrFmt::NONE,      1, 1, "SET"    },  // 1001 0100 0110 1000
    { 0xFFFF, 0x9478, AvrOp::SEI,    AvrFmt::NONE,      1, 1, "SEI"    },  // 1001 0100 0111 1000
    { 0xFFFF, 0x9488, AvrOp::CLC,    AvrFmt::NONE,      1, 1, "CLC"    },  // 1001 0100 1000 1000
    { 0xFFFF, 0x9498, AvrOp::CLZ,    AvrFmt::NONE,      1, 1, "CLZ"    },  // 1001 0100 1001 1000
    { 0xFFFF, 0x94A8, AvrOp::CLN,    AvrFmt::NONE,      1, 1, "CLN"    },  // 1001 0100 1010 1000
    { 0xFFFF, 0x94B8, AvrOp::CLV,    AvrFmt::NONE,      1, 1, "CLV"    },  // 1001 0100 1011 1000
    { 0xFFFF, 0x94C8, AvrOp::CLS,    AvrFmt::NONE,      1, 1, "CLS"    },  // 1001 0100 1100 1000
    { 0xFFFF, 0x94D8, AvrOp::CLH,    AvrFmt::NONE,      1, 1, "CLH"    },  // 1001 0100 1101 1000
    { 0xFFFF, 0x94E8, AvrOp::CLT,    AvrFmt::NONE,      1, 1, "CLT"    },  // 1001 0100 1110 1000
    { 0xFFFF, 0x94F8, AvrOp::CLI,    AvrFmt::NONE,      1, 1, "CLI"    },  // 1001 0100 1111 1000
 
    // -----------------------------------------------------------------------
    // MCU control
    // -----------------------------------------------------------------------
    { 0xFFFF, 0x0000, AvrOp::NOP,    AvrFmt::NONE,      1, 1, "NOP"    },  // 0000 0000 0000 0000
    { 0xFFFF, 0x9588, AvrOp::SLEEP,  AvrFmt::NONE,      1, 1, "SLEEP"  },  // 1001 0101 1000 1000
    { 0xFFFF, 0x95A8, AvrOp::WDR,    AvrFmt::NONE,      1, 1, "WDR"    },  // 1001 0101 1010 1000
    { 0xFFFF, 0x9598, AvrOp::BREAK,  AvrFmt::NONE,      1, 1, "BREAK"  },  // 1001 0101 1001 1000
    // SPM (AVRe): 1001 0101 1110 1000
    { 0xFFFF, 0x95E8, AvrOp::SPM_E,  AvrFmt::NONE,      1, 1, "SPM"    },  // 1001 0101 1110 1000
};

Opcode decodeInstruction(uint16_t& instruction);

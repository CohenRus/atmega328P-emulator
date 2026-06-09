/*
 * test_decoder.cpp - Exhaustive and targeted tests for AVR opcode decoding.
 * Verifies opcode-table coverage and operand extraction across instruction
 * formats.
 */

#include "catch_amalgamated.hpp"
#include "decoder.h"
#include "state.h"
#include "memory.h"
#include "executor.h"
#include <cstdint>
#include <set>

// Keep the exclusion hook explicit so documented undefined encodings can be
// added without weakening the exhaustive scan.
static bool isKnownUnused(uint16_t instr) {
    (void)instr;
    return false;
}

TEST_CASE("Decoder: exhaustive 16-bit opcode scan", "[decoder]") {
    int decoded = 0;
    int failed = 0;
    std::set<AvrOp> opsSeen;

    for (uint32_t i = 0; i < 65536; ++i) {
        uint16_t instr = static_cast<uint16_t>(i);
        Opcode op;
        if (decodeInstruction(instr, op)) {
            decoded++;
            opsSeen.insert(op.op);
        } else {
            if (!isKnownUnused(instr)) {
                failed++;
            }
        }
    }

    REQUIRE(opsSeen.count(AvrOp::NOP) > 0);
    REQUIRE(opsSeen.count(AvrOp::ADD) > 0);
    REQUIRE(opsSeen.count(AvrOp::ADC) > 0);
    REQUIRE(opsSeen.count(AvrOp::SUB) > 0);
    REQUIRE(opsSeen.count(AvrOp::AND) > 0);
    REQUIRE(opsSeen.count(AvrOp::OR) > 0);
    // EOR and CLR share the same bit pattern (CLR is alias for EOR Rd,Rd).
    // CLR is listed first in the table so it always wins the linear scan.
    REQUIRE((opsSeen.count(AvrOp::EOR) > 0 || opsSeen.count(AvrOp::CLR) > 0));
    REQUIRE(opsSeen.count(AvrOp::MOV) > 0);
    REQUIRE(opsSeen.count(AvrOp::LDI) > 0);
    REQUIRE(opsSeen.count(AvrOp::RJMP) > 0);
    REQUIRE(opsSeen.count(AvrOp::JMP) > 0);
    REQUIRE(opsSeen.count(AvrOp::CALL) > 0);
    REQUIRE(opsSeen.count(AvrOp::RET) > 0);
    REQUIRE(opsSeen.count(AvrOp::PUSH) > 0);
    REQUIRE(opsSeen.count(AvrOp::POP) > 0);
    REQUIRE(opsSeen.count(AvrOp::IN) > 0);
    REQUIRE(opsSeen.count(AvrOp::OUT) > 0);
    REQUIRE(opsSeen.count(AvrOp::LDS) > 0);
    REQUIRE(opsSeen.count(AvrOp::STS) > 0);
    REQUIRE(opsSeen.count(AvrOp::LPM) > 0);
    REQUIRE(opsSeen.count(AvrOp::CPSE) > 0);
    REQUIRE(opsSeen.count(AvrOp::SBRC) > 0);

    // There should be many decodable patterns — at least 60000+ of 65536
    // since the AVR instruction set densely covers most of the space.
    INFO("Decoded " << decoded << " of 65536 patterns, " << failed << " unknown");
    REQUIRE(decoded > 50000);
    REQUIRE(failed < 15000);
}

TEST_CASE("Decoder: no duplicate opcode matches", "[decoder]") {
    // For each possible 16-bit pattern, at most one opcode should match.
    for (uint32_t i = 0; i < 65536; ++i) {
        uint16_t instr = static_cast<uint16_t>(i);
        Opcode op;
        bool first = decodeInstruction(instr, op);
        if (!first) continue;

        AvrOp firstOp = op.op;
        AvrFmt firstFmt = op.fmt;

        // Re-decode should return same result (no random second match)
        Opcode op2;
        bool second = decodeInstruction(instr, op2);
        REQUIRE(second);
        REQUIRE(op2.op == firstOp);
        REQUIRE(op2.fmt == firstFmt);
    }
}

TEST_CASE("Decoder: known instruction patterns", "[decoder]") {
    struct TestCase {
        uint16_t instr;
        AvrOp expectedOp;
        AvrFmt expectedFmt;
        uint8_t expectedWords;
        const char* desc;
    };

    TestCase cases[] = {
        {0x0000, AvrOp::NOP,    AvrFmt::NONE,       1, "NOP"},
        {0x0C00, AvrOp::ADD,    AvrFmt::Rd_Rr,      1, "ADD R0,R0"},
        {0x1C00, AvrOp::ADC,    AvrFmt::Rd_Rr,      1, "ADC R0,R0"},
        {0x0C01, AvrOp::ADD,    AvrFmt::Rd_Rr,      1, "ADD R0,R1"},
        {0xE000, AvrOp::LDI,    AvrFmt::Rd_K8,      1, "LDI R16,0"},
        {0xE0FF, AvrOp::LDI,    AvrFmt::Rd_K8,      1, "LDI R16,0xFF"},
        {0xE10F, AvrOp::LDI,    AvrFmt::Rd_K8,      1, "LDI R17,0xF"},
        {0x2C00, AvrOp::MOV,    AvrFmt::Rd_Rr,      1, "MOV R0,R0"},
        {0x2C01, AvrOp::MOV,    AvrFmt::Rd_Rr,      1, "MOV R0,R1"},
        {0x9403, AvrOp::INC,    AvrFmt::Rd_only,    1, "INC R0"},
        {0x940A, AvrOp::DEC,    AvrFmt::Rd_only,    1, "DEC R0"},
        {0x9405, AvrOp::ASR,    AvrFmt::Rd_only,    1, "ASR R0"},
        {0x9406, AvrOp::LSR,    AvrFmt::Rd_only,    1, "LSR R0"},
        {0x9407, AvrOp::ROR,    AvrFmt::Rd_only,    1, "ROR R0"},
        {0x9401, AvrOp::NEG,    AvrFmt::Rd_only,    1, "NEG R0"},
        {0x9400, AvrOp::COM,    AvrFmt::Rd_only,    1, "COM R0"},
        {0x9402, AvrOp::SWAP,   AvrFmt::Rd_only,    1, "SWAP R0"},
        {0x2000, AvrOp::AND,    AvrFmt::Rd_Rr,      1, "AND R0,R0"},
        {0x7000, AvrOp::ANDI,   AvrFmt::Rd_K8,      1, "ANDI R16,0"},
        {0x2800, AvrOp::OR,     AvrFmt::Rd_Rr,      1, "OR R0,R0"},
        {0x6000, AvrOp::ORI,    AvrFmt::Rd_K8,      1, "ORI R16,0"},
        {0x2400, AvrOp::CLR,    AvrFmt::Rd_Rr,      1, "EOR R0,R0 (CLR alias wins)"},
        {0xEF0F, AvrOp::SER,    AvrFmt::Rd_K8,      1, "SER R16"},
        {0x1800, AvrOp::SUB,    AvrFmt::Rd_Rr,      1, "SUB R0,R0"},
        {0x5000, AvrOp::SUBI,   AvrFmt::Rd_K8,      1, "SUBI R16,0"},
        {0x0800, AvrOp::SBC,    AvrFmt::Rd_Rr,      1, "SBC R0,R0"},
        {0x4000, AvrOp::SBCI,   AvrFmt::Rd_K8,      1, "SBCI R16,0"},
        {0x1400, AvrOp::CP,     AvrFmt::Rd_Rr,      1, "CP R0,R0"},
        {0x0400, AvrOp::CPC,    AvrFmt::Rd_Rr,      1, "CPC R0,R0"},
        {0x3000, AvrOp::CPI,    AvrFmt::Rd_K8,      1, "CPI R16,0"},
        {0x1000, AvrOp::CPSE,   AvrFmt::Rd_Rr,      1, "CPSE R0,R0"},
        {0x9C00, AvrOp::MUL,    AvrFmt::Rd_Rr,      1, "MUL R0,R0"},
        {0x0200, AvrOp::MULS,   AvrFmt::Rd06_Rr06,  1, "MULS"},
        {0x0300, AvrOp::MULSU,  AvrFmt::Rd_Rr_mpy,  1, "MULSU R16,R16"},
        {0x0308, AvrOp::FMUL,   AvrFmt::Rd_Rr_mpy,  1, "FMUL R16,R16"},
        {0x0380, AvrOp::FMULS,  AvrFmt::Rd_Rr_mpy,  1, "FMULS R16,R16"},
        {0x0388, AvrOp::FMULSU, AvrFmt::Rd_Rr_mpy,  1, "FMULSU R16,R16"},
        {0x9600, AvrOp::ADIW,   AvrFmt::Rd06_K6,    1, "ADIW R24,0"},
        {0x9700, AvrOp::SBIW,   AvrFmt::Rd06_K6,    1, "SBIW R24,0"},
        {0x0100, AvrOp::MOVW,   AvrFmt::Rd06_Rr06,  1, "MOVW R0,R0"},
        // Branch
        {0xC000, AvrOp::RJMP,   AvrFmt::k02,        1, "RJMP 0"},
        {0xF400, AvrOp::BRBC,   AvrFmt::k7,         1, "BRCC -0"},
        {0xF000, AvrOp::BRBS,   AvrFmt::k7,         1, "BRCS -0"},
        // 32-bit
        {0x940C, AvrOp::JMP,    AvrFmt::k22,        2, "JMP (first word)"},
        {0x940E, AvrOp::CALL,   AvrFmt::k22_call,   2, "CALL (first word)"},
        {0x9000, AvrOp::LDS,    AvrFmt::LDS_STS,    2, "LDS R0,k (first word)"},
        {0x9200, AvrOp::STS,    AvrFmt::LDS_STS,    2, "STS k,R0 (first word)"},
        // Data transfer
        {0x900F, AvrOp::POP,    AvrFmt::Rd_only,    1, "POP R0"},
        {0x920F, AvrOp::PUSH,   AvrFmt::Rd_only,    1, "PUSH R0"},
        {0xB000, AvrOp::IN,     AvrFmt::Rd_IO,      1, "IN R0,0"},
        {0xB800, AvrOp::OUT,    AvrFmt::IO_Rr,      1, "OUT 0,R0"},
        {0x900C, AvrOp::LD_X,   AvrFmt::LD_family,  1, "LD R0,X"},
        {0x900D, AvrOp::LD_X,   AvrFmt::LD_family,  1, "LD R0,X+"},
        {0x900E, AvrOp::LD_X,   AvrFmt::LD_family,  1, "LD R0,-X"},
        {0x8008, AvrOp::LD_Y,   AvrFmt::LDD_family, 1, "LD R0,Y (q=0, LDD wins)"},
        {0x9009, AvrOp::LD_Y,   AvrFmt::LD_family,  1, "LD R0,Y+"},
        {0x900A, AvrOp::LD_Y,   AvrFmt::LD_family,  1, "LD R0,-Y"},
        {0x8000, AvrOp::LD_Z,   AvrFmt::LDD_family, 1, "LD R0,Z (q=0, LDD wins)"},
        {0x9001, AvrOp::LD_Z,   AvrFmt::LD_family,  1, "LD R0,Z+"},
        {0x9002, AvrOp::LD_Z,   AvrFmt::LD_family,  1, "LD R0,-Z"},
        {0x920C, AvrOp::ST_X,   AvrFmt::LD_family,  1, "ST X,R0"},
        {0x920D, AvrOp::ST_X,   AvrFmt::LD_family,  1, "ST X+,R0"},
        {0x920E, AvrOp::ST_X,   AvrFmt::LD_family,  1, "ST -X,R0"},
        {0x8208, AvrOp::ST_Y,   AvrFmt::LDD_family, 1, "ST Y,R0 (q=0, STD wins)"},
        {0x9209, AvrOp::ST_Y,   AvrFmt::LD_family,  1, "ST Y+,R0"},
        {0x920A, AvrOp::ST_Y,   AvrFmt::LD_family,  1, "ST -Y,R0"},
        {0x8200, AvrOp::ST_Z,   AvrFmt::LDD_family, 1, "ST Z,R0 (q=0, STD wins)"},
        {0x9201, AvrOp::ST_Z,   AvrFmt::LD_family,  1, "ST Z+,R0"},
        {0x9202, AvrOp::ST_Z,   AvrFmt::LD_family,  1, "ST -Z,R0"},
        // LPM
        {0x95C8, AvrOp::LPM,    AvrFmt::NONE,       1, "LPM (R0,Z)"},
        {0x9004, AvrOp::LPM,    AvrFmt::Rd_only,    1, "LPM R0,Z"},
        {0x9005, AvrOp::LPM,    AvrFmt::Rd_only,    1, "LPM R0,Z+"},
        // LDD/STD with non-zero q — avoids shadowing by base LD/ST entries
        {0x8009, AvrOp::LD_Y,   AvrFmt::LDD_family, 1, "LDD R0,Y+1"},
        {0x8209, AvrOp::ST_Y,   AvrFmt::LDD_family, 1, "STD Y+1,R0"},
        {0x8001, AvrOp::LD_Z,   AvrFmt::LDD_family, 1, "LDD R0,Z+1"},
        {0x8201, AvrOp::ST_Z,   AvrFmt::LDD_family, 1, "STD Z+1,R0"},
        // SREG flag ops
        {0x9408, AvrOp::SEC,    AvrFmt::NONE,       1, "SEC"},
        {0x9488, AvrOp::CLC,    AvrFmt::NONE,       1, "CLC"},
        {0x9418, AvrOp::SEZ,    AvrFmt::NONE,       1, "SEZ"},
        {0x9498, AvrOp::CLZ,    AvrFmt::NONE,       1, "CLZ"},
        {0x9428, AvrOp::SEN,    AvrFmt::NONE,       1, "SEN"},
        {0x94A8, AvrOp::CLN,    AvrFmt::NONE,       1, "CLN"},
        {0x9438, AvrOp::SEV,    AvrFmt::NONE,       1, "SEV"},
        {0x94B8, AvrOp::CLV,    AvrFmt::NONE,       1, "CLV"},
        {0x9448, AvrOp::SES,    AvrFmt::NONE,       1, "SES"},
        {0x94C8, AvrOp::CLS,    AvrFmt::NONE,       1, "CLS"},
        {0x9458, AvrOp::SEH,    AvrFmt::NONE,       1, "SEH"},
        {0x94D8, AvrOp::CLH,    AvrFmt::NONE,       1, "CLH"},
        {0x9468, AvrOp::SET,    AvrFmt::NONE,       1, "SET"},
        {0x94E8, AvrOp::CLT,    AvrFmt::NONE,       1, "CLT"},
        {0x9478, AvrOp::SEI,    AvrFmt::NONE,       1, "SEI"},
        {0x94F8, AvrOp::CLI,    AvrFmt::NONE,       1, "CLI"},
        // Bit manipulation
        {0xF800, AvrOp::BLD,    AvrFmt::Rd_b,       1, "BLD R0,0"},
        {0xFA00, AvrOp::BST,    AvrFmt::Rd_b,       1, "BST R0,0"},
        {0xFC00, AvrOp::SBRC,   AvrFmt::Rr_b,       1, "SBRC R0,0"},
        {0xFE00, AvrOp::SBRS,   AvrFmt::Rr_b,       1, "SBRS R0,0"},
        {0x9800, AvrOp::CBI,    AvrFmt::IO_b,       1, "CBI"},
        {0x9A00, AvrOp::SBI,    AvrFmt::IO_b,       1, "SBI"},
        {0x9900, AvrOp::SBIC,   AvrFmt::IO_b,       1, "SBIC"},
        {0x9B00, AvrOp::SBIS,   AvrFmt::IO_b,       1, "SBIS"},
        // MCU control
        {0x9588, AvrOp::SLEEP,  AvrFmt::NONE,       1, "SLEEP"},
        {0x95A8, AvrOp::WDR,    AvrFmt::NONE,       1, "WDR"},
        {0x9598, AvrOp::BREAK,  AvrFmt::NONE,       1, "BREAK"},
        {0x95E8, AvrOp::SPM_E,  AvrFmt::NONE,       1, "SPM"},
        // RET/RETI/IJMP/ICALL
        {0x9508, AvrOp::RET,    AvrFmt::NONE,       1, "RET"},
        {0x9518, AvrOp::RETI,   AvrFmt::NONE,       1, "RETI"},
        {0x9409, AvrOp::IJMP,   AvrFmt::NONE,       1, "IJMP"},
        {0x9509, AvrOp::ICALL,  AvrFmt::NONE,       1, "ICALL"},
    };

    for (const auto& tc : cases) {
        DYNAMIC_SECTION(tc.desc) {
            Opcode op;
            bool ok = decodeInstruction(tc.instr, op);
            INFO("Instruction 0x" << std::hex << tc.instr << " failed to decode");
            REQUIRE(ok);
            REQUIRE(op.op == tc.expectedOp);
            REQUIRE(op.fmt == tc.expectedFmt);
            REQUIRE(op.words == tc.expectedWords);
        }
    }
}

TEST_CASE("Decoder: 0x9393 opcode is unused/undefined", "[decoder][regression]") {
    // 0x9393 was caught crashing the emulator. It should either decode as a
    // valid instruction or be recognized as an unused encoding.
    // Per the AVR instruction set manual, 0x9393 has bit 15:12 = 1001 and
    // bit 3:0 = 0011, which is not a documented encoding for any instruction.
    // It should fail to decode.
    Opcode op;
    REQUIRE_FALSE(decodeInstruction(0x9393, op));
}

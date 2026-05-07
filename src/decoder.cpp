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

// ---------------------------------------------------------------------------
// Operand decoders — one per AvrFmt.
// Each function extracts only the fields defined by that format's bit layout.
// ---------------------------------------------------------------------------

// Rd_Rr:  xxxx xxrd dddd rrrr
OpsRdRr decodeRdRr(uint16_t instr) {
    uint8_t d = (instr >> 4) & 0x1F;
    uint8_t r = ((instr >> 5) & 0x10) | (instr & 0x0F);
    return {d, r};
}

// Rd_K8:  xxxx KKKK dddd KKKK  (d: 16..31)
OpsRdK8 decodeRdK8(uint16_t instr) {
    uint8_t d = ((instr >> 4) & 0x0F) + 16;
    uint8_t k = ((instr >> 4) & 0xF0) | (instr & 0x0F);
    return {d, k};
}

// Rd_only:  xxxx xxxd dddd xxxx
OpsRd decodeRd(uint16_t instr) {
    return {(uint8_t)((instr >> 4) & 0x1F)};
}

// Rd_Rr_mpy:  xxxx xxxx 0ddd 0rrr  (d,r: 16..23)
OpsRdRrMpy decodeRdRrMpy(uint16_t instr) {
    uint8_t d = ((instr >> 4) & 0x07) + 16;
    uint8_t r = (instr & 0x07) + 16;
    return {d, r};
}

// Rd06_K6:  xxxx xxxx KKdd KKKK  (dd encodes {24,26,28,30}, K is 6-bit)
OpsRd06K6 decodeRd06K6(uint16_t instr) {
    uint8_t d = 24 + (((instr >> 4) & 0x03) << 1);
    uint8_t k = ((instr >> 2) & 0x30) | (instr & 0x0F);
    return {d, k};
}

// Rd06_Rr06:  xxxx xxxx dddd rrrr
// Returns raw nibbles — callers apply their own offset (MOVW ×2, MULS +16).
OpsRd06Rr06 decodeRd06Rr06(uint16_t instr) {
    return {(uint8_t)((instr >> 4) & 0x0F), (uint8_t)(instr & 0x0F)};
}

// b_only:  xxxx xxxx xbbb xxxx  (BSET/BCLR — bit index in [6:4])
OpsBOnly decodeBOnly(uint16_t instr) {
    return {(uint8_t)((instr >> 4) & 0x07)};
}

// Rd_b:  xxxx xxxd dddd xbbb
OpsRdB decodeRdB(uint16_t instr) {
    return {(uint8_t)((instr >> 4) & 0x1F), (uint8_t)(instr & 0x07)};
}

// Rr_b:  xxxx xxxr rrrr xbbb  (same bit layout as Rd_b)
OpsRrB decodeRrB(uint16_t instr) {
    return {(uint8_t)((instr >> 4) & 0x1F), (uint8_t)(instr & 0x07)};
}

// IO_b:  xxxx xxxx AAAA Abbb
OpsIOB decodeIOB(uint16_t instr) {
    return {(uint8_t)((instr >> 3) & 0x1F), (uint8_t)(instr & 0x07)};
}

// Rd_IO:  xxxx xAAd dddd AAAA  (IN)
OpsRdIO decodeRdIO(uint16_t instr) {
    uint8_t d = (instr >> 4) & 0x1F;
    uint8_t a = ((instr >> 5) & 0x30) | (instr & 0x0F);
    return {d, a};
}

// IO_Rr:  xxxx xAAr rrrr AAAA  (OUT — same bit layout as Rd_IO)
OpsIORr decodeIORr(uint16_t instr) {
    uint8_t r = (instr >> 4) & 0x1F;
    uint8_t a = ((instr >> 5) & 0x30) | (instr & 0x0F);
    return {a, r};
}

// k7:  xxxx xxkk kkkk ksss  (7-bit signed offset, 3-bit SREG bit)
OpsK7 decodeK7(uint16_t instr) {
    uint8_t raw = (instr >> 3) & 0x7F;
    // sign-extend bit 6 into a signed byte
    int8_t k = (raw & 0x40) ? (int8_t)(raw | 0x80) : (int8_t)raw;
    uint8_t s = instr & 0x07;
    return {k, s};
}

// k02:  xxxx kkkk kkkk kkkk  (12-bit signed — RJMP / RCALL)
OpsK02 decodeK02(uint16_t instr) {
    uint16_t raw = instr & 0x0FFF;
    int16_t k = (raw & 0x0800) ? (int16_t)(raw | 0xF000) : (int16_t)raw;
    return {k};
}

// k22:  xxxx xxxk kkkk 110k  +  16-bit secondWord  (JMP / CALL)
// bits[8:4] of word1 → k[21:17], bit[0] of word1 → k[16], word2 → k[15:0]
OpsK22 decodeK22(uint16_t instr, uint16_t secondWord) {
    uint32_t k = ((uint32_t)((instr >> 4) & 0x1F) << 17)
               | ((uint32_t)(instr & 0x01)         << 16)
               | secondWord;
    return {k};
}

// LD_family:  xxxx xxxd dddd xxmm  (mode = bits[1:0]: 0=base, 1=post-inc, 2=pre-dec)
OpsLdSt decodeLdSt(uint16_t instr) {
    return {(uint8_t)((instr >> 4) & 0x1F), (uint8_t)(instr & 0x03)};
}

// LDD_family:  10q0 qq0d dddd yqqq  (q = 6-bit displacement)
OpsLdd decodeLdd(uint16_t instr) {
    uint8_t d = (instr >> 4) & 0x1F;
    uint8_t q = ((instr >> 8) & 0x20) | ((instr >> 7) & 0x18) | (instr & 0x07);
    return {d, q};
}

// LDS_STS:  32-bit — first word carries Rd, second word is the 16-bit address
OpsLdsSts decodeLdsSts(uint16_t instr, uint16_t secondWord) {
    return {(uint8_t)((instr >> 4) & 0x1F), secondWord};
}

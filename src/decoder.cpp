/*
 * decoder.cpp — Implementation of the AVR instruction decoder.
 *
 * decodeInstruction() uses an immutable lookup table built from the opcode table.
 * Each operand decoder unpacks one AvrFmt's bit layout.
 *
 * All decoders assume the instruction has already been matched and the
 * AvrFmt tag has been set — they do not perform their own validation.
 */

#include "decoder.h"

#include <array>

namespace {

struct CachedOpcode {
  Opcode opcode{};
  bool valid = false;
};

const std::array<CachedOpcode, 65536>& opcodeCache() {
  static const auto cache = [] {
    std::array<CachedOpcode, 65536> entries{};
    for (uint32_t instruction = 0; instruction < entries.size(); ++instruction) {
      for (const Opcode& opcode : OPCODE_TABLE) {
        if ((instruction & opcode.mask) == opcode.code) {
          entries[instruction] = {opcode, true};
          break;
        }
      }
    }
    return entries;
  }();
  return cache;
}

} // namespace

// Look up a raw 16-bit instruction word in the precomputed decode table.
// @param instruction — the 16-bit opcode to decode
// @param out         — [out] set to the matched Opcode on success
// @return true if a matching entry was found
bool decodeInstruction(uint16_t instruction, Opcode& out) {
  const CachedOpcode& cached = opcodeCache()[instruction];
  if (cached.valid) out = cached.opcode;
  return cached.valid;
}

// ---------------------------------------------------------------------------
// Operand decoders — one per AvrFmt.
// Each function extracts only the fields defined by that format's bit layout.
// ---------------------------------------------------------------------------

// Rd_Rr:  xxxx xxrd dddd rrrr
// Bit[9] is the high-order bit of the source register (bit 4 of r),
// while the low 4 bits of r are in bits[3:0].
// The destination register occupies bits[8:4].
// @param instr — 16-bit instruction word
// @return d (bits[8:4]), r = bit[9]<<4 | bits[3:0]
OpsRdRr decodeRdRr(uint16_t instr) {
    // Destination: 5 bits at [8:4].
    uint8_t d = (instr >> 4) & 0x1F;
    // Source: high bit from [9], low 4 bits from [3:0].
    uint8_t r = ((instr >> 5) & 0x10) | (instr & 0x0F);
    return {d, r};
}

// Rd_K8:  xxxx KKKK dddd KKKK  (d: 16..31)
// The upper nibble of K occupies bits[11:8], the lower nibble in bits[3:0].
// The register d is encoded in bits[7:4] as a value 0..15, then offset by 16.
// @param instr — 16-bit instruction word
// @return d (range 16..31), k = bits[11:8]<<4 | bits[3:0]
OpsRdK8 decodeRdK8(uint16_t instr) {
    // Register: 4 bits at [7:4], add 16 to map to R16..R31.
    uint8_t d = ((instr >> 4) & 0x0F) + 16;
    // Immediate: upper nibble at [11:8], lower nibble at [3:0].
    uint8_t k = ((instr >> 4) & 0xF0) | (instr & 0x0F);
    return {d, k};
}

// Rd_only:  xxxx xxxd dddd xxxx
// The destination register is in bits[7:4] (5 bits, range 0..31).
// @param instr — 16-bit instruction word
// @return d (0..31)
OpsRd decodeRd(uint16_t instr) {
    return {(uint8_t)((instr >> 4) & 0x1F)};
}

// Rd_Rr_mpy:  xxxx xxxx 0ddd 0rrr  (d,r: 16..23)
// Both registers use 3-bit encodings (range 0..7), offset by 16.
// The high bit of each nibble is always 0, restricting to R16..R23.
// @param instr — 16-bit instruction word
// @return d and r, both in range 16..23
OpsRdRrMpy decodeRdRrMpy(uint16_t instr) {
    // d: 3 bits at [6:4], offset by 16.
    uint8_t d = ((instr >> 4) & 0x07) + 16;
    // r: 3 bits at [3:0] (bit 3 is always 0, so effectively bits[2:0]).
    uint8_t r = (instr & 0x07) + 16;
    return {d, r};
}

// Rd06_K6:  xxxx xxxx KKdd KKKK  (dd encodes {24,26,28,30}, K is 6-bit)
// The 2-bit dd field (bits[5:4]) selects one of four word registers:
//   00 → R24, 01 → R26 (X), 10 → R28 (Y), 11 → R30 (Z).
// The 6-bit immediate K is split: upper 2 bits at [7:6] (shifted from [5:4]),
// lower 4 bits at [3:0].
// @param instr — 16-bit instruction word
// @return d in {24,26,28,30}, k 6-bit unsigned immediate
OpsRd06K6 decodeRd06K6(uint16_t instr) {
    // Register pair selector: dd = bits[5:4] → multiply by 2, add 24.
    uint8_t d = 24 + (((instr >> 4) & 0x03) << 1);
    // Immediate: bits[7:6] shifted down to [5:4] (becomes 0x30 mask when <<2),
    // combined with bits[3:0].
    uint8_t k = ((instr >> 2) & 0x30) | (instr & 0x0F);
    return {d, k};
}

// Rd06_Rr06:  xxxx xxxx dddd rrrr
// Returns raw nibbles — callers apply their own offset (MOVW ×2, MULS +16).
// Both fields are 4 bits wide.
// @param instr — 16-bit instruction word
// @return raw d nibble in [7:4], raw r nibble in [3:0]
OpsRd06Rr06 decodeRd06Rr06(uint16_t instr) {
    return {(uint8_t)((instr >> 4) & 0x0F), (uint8_t)(instr & 0x0F)};
}

// b_only:  xxxx xxxx xbbb xxxx  (BSET/BCLR — bit index in [6:4])
// The SREG bit index (0..7) occupies bits[6:4].
// @param instr — 16-bit instruction word
// @return b SREG bit index (0..7)
OpsBOnly decodeBOnly(uint16_t instr) {
    return {(uint8_t)((instr >> 4) & 0x07)};
}

// Rd_b:  xxxx xxxd dddd xbbb
// Register d in bits[8:4] (5 bits), bit index b in bits[2:0] (3 bits).
// @param instr — 16-bit instruction word
// @return d register (0..31), b bit index (0..7)
OpsRdB decodeRdB(uint16_t instr) {
    return {(uint8_t)((instr >> 4) & 0x1F), (uint8_t)(instr & 0x07)};
}

// Rr_b:  xxxx xxxr rrrr xbbb  (same bit layout as Rd_b)
// @param instr — 16-bit instruction word
// @return r register (0..31), b bit index (0..7)
OpsRrB decodeRrB(uint16_t instr) {
    return {(uint8_t)((instr >> 4) & 0x1F), (uint8_t)(instr & 0x07)};
}

// IO_b:  xxxx xxxx AAAA Abbb
// The I/O address is split: upper 4 bits at [7:4], lowest bit at [3].
// The bit index is in bits[2:0].
// This handles the 5-bit I/O space (0..31) used by SBI/CBI/SBIC/SBIS.
// @param instr — 16-bit instruction word
// @return a I/O address (0..31), b bit index (0..7)
OpsIOB decodeIOB(uint16_t instr) {
    // I/O address: upper nibble at [7:4], bit [3] is the lsb.
    uint8_t a = ((instr >> 3) & 0x1F);
    // Bit index: bits[2:0].
    uint8_t b = (instr & 0x07);
    return {a, b};
}

// Rd_IO:  xxxx xAAd dddd AAAA  (IN)
// The I/O address is 6 bits: upper 2 bits at [9:8], lower 4 at [3:0].
// The destination register is in bits[8:4].
// @param instr — 16-bit instruction word
// @return d register (0..31), a I/O address (0..63)
OpsRdIO decodeRdIO(uint16_t instr) {
    // Destination register: bits[8:4].
    uint8_t d = (instr >> 4) & 0x1F;
    // I/O address: bits[9:8] shifted to [5:4], combined with bits[3:0].
    uint8_t a = ((instr >> 5) & 0x30) | (instr & 0x0F);
    return {d, a};
}

// IO_Rr:  xxxx xAAr rrrr AAAA  (OUT — same bit layout as Rd_IO)
// The I/O address is 6 bits: upper 2 bits at [9:8], lower 4 at [3:0].
// The source register is in bits[8:4].
// @param instr — 16-bit instruction word
// @return a I/O address (0..63), r source register (0..31)
OpsIORr decodeIORr(uint16_t instr) {
    // Source register: bits[8:4].
    uint8_t r = (instr >> 4) & 0x1F;
    // I/O address: bits[9:8] → [5:4], combined with bits[3:0].
    uint8_t a = ((instr >> 5) & 0x30) | (instr & 0x0F);
    return {a, r};
}

// k7:  xxxx xxkk kkkk ksss  (7-bit signed word offset, 3-bit SREG bit)
// The 7-bit offset occupies bits[9:3]. It is sign-extended from bit 6
// (the high bit of the raw 7-bit field, i.e. bit 9 of the instruction).
// The SREG bit selector is in bits[2:0].
// @param instr — 16-bit instruction word
// @return k sign-extended 7-bit offset (word units), s SREG bit (0..7)
OpsK7 decodeK7(uint16_t instr) {
    // Raw offset: 7 bits at [9:3], shifted to [6:0].
    uint8_t raw = (instr >> 3) & 0x7F;
    // Sign-extend bit 6 into a signed byte.
    // If bit 6 is set, OR with 0x80 to set the upper bits of the int8_t.
    int8_t k = (raw & 0x40) ? (int8_t)(raw | 0x80) : (int8_t)raw;
    // SREG bit number: low 3 bits.
    uint8_t s = instr & 0x07;
    return {k, s};
}

// k02:  xxxx kkkk kkkk kkkk  (12-bit signed word offset — RJMP / RCALL)
// The 12-bit offset is in bits[11:0]. It is sign-extended from bit 11
// into a 16-bit signed integer.
// @param instr — 16-bit instruction word
// @return k sign-extended 12-bit offset (word units)
OpsK02 decodeK02(uint16_t instr) {
    // Raw offset: 12 bits at [11:0].
    uint16_t raw = instr & 0x0FFF;
    // Sign-extend: if bit 11 is set, set bits [15:12] to 1.
    int16_t k = (raw & 0x0800) ? (int16_t)(raw | 0xF000) : (int16_t)raw;
    return {k};
}

// k22:  xxxx xxxk kkkk 110k  +  16-bit secondWord  (JMP / CALL)
// The 22-bit absolute address is assembled from three pieces:
//   - word1 bits[8:4]  → k[21:17]  (5 bits)
//   - word1 bit[0]     → k[16]     (1 bit)
//   - word2            → k[15:0]   (16 bits)
// @param instr      — first 16-bit instruction word
// @param secondWord — second 16-bit instruction word
// @return k 22-bit absolute word address
OpsK22 decodeK22(uint16_t instr, uint16_t secondWord) {
    uint32_t k = ((uint32_t)((instr >> 4) & 0x3F) << 16)  // bits[21:16] from word1[9:4]
               | secondWord;                                // bits[15:0] from word2
    return {k};
}

// LD_family:  xxxx xxxd dddd xxmm  (mode = bits[1:0]: 0=base, 1=post-inc, 2=pre-dec)
// The destination/source register occupies bits[8:4].
// The addressing mode is the low 2 bits of the instruction.
// @param instr — 16-bit instruction word
// @return d register (0..31), mode 0=base, 1=post-inc, 2=pre-dec
OpsLdSt decodeLdSt(uint16_t instr) {
    return {(uint8_t)((instr >> 4) & 0x1F), (uint8_t)(instr & 0x03)};
}

// LDD_family:  10q0 qq0d dddd yqqq  (q = 6-bit displacement)
// The 6-bit displacement q is scattered across three bit fields:
//   - bit[13]    → q[5]  (shifted from bit 8 of the high byte)
//   - bits[12:11] → q[4:3] (shifted from bits 7:6)
//   - bits[2:0]  → q[2:0]
// The destination register is in bits[8:4].
// The 'y' bit (bit 3) distinguishes Y+q (y=1) from Z+q (y=0);
// it is used by the LD/ST execute functions, not by the decoder.
// @param instr — 16-bit instruction word
// @return d register (0..31), q 6-bit unsigned displacement
OpsLdd decodeLdd(uint16_t instr) {
    uint8_t d = (instr >> 4) & 0x1F;
    // Reassemble the 6-bit displacement from its scattered fields.
    // q[5]   ← bit 13  (shifted by 8 places to bit 5, masked with 0x20)
    // q[4:3] ← bits[11:10] (shifted by 7 places to bits 4:3, masked with 0x18)
    // q[2:0] ← bits[2:0]
    uint8_t q = ((instr >> 8) & 0x20) | ((instr >> 7) & 0x18) | (instr & 0x07);
    return {d, q};
}

// LDS_STS:  32-bit — first word carries Rd, second word is the 16-bit address.
// The destination/source register is in word1 bits[8:4].
// The second word is the data-space address, passed through unchanged.
// @param instr      — first 16-bit instruction word
// @param secondWord — 16-bit data-space address
// @return d register (0..31), addr 16-bit data-space address
OpsLdsSts decodeLdsSts(uint16_t instr, uint16_t secondWord) {
    return {(uint8_t)((instr >> 4) & 0x1F), secondWord};
}

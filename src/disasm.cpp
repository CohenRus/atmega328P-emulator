#include "disasm.h"
#include "decoder.h"

#include <cstdio>

// Helper: format a hex address
static std::string hex16(uint16_t v) {
  char buf[16];
  snprintf(buf, sizeof(buf), "0x%04X", v);
  return buf;
}

static std::string hex8(uint8_t v) {
  char buf[8];
  snprintf(buf, sizeof(buf), "0x%02X", v);
  return buf;
}

static std::string hex6(uint8_t v) {
  char buf[8];
  snprintf(buf, sizeof(buf), "0x%02X", v);
  return buf;
}

// Sign-extend a 7-bit value at bit 6 → int16_t, convert to word offset string
static std::string fmtK7(uint16_t instr) {
  auto ops = decodeK7(instr);
  char buf[32];
  snprintf(buf, sizeof(buf), "%+d", (int)ops.k);
  return buf;
}

// Sign-extend a 12-bit value, convert to word offset string
static std::string fmtK02(uint16_t instr) {
  auto ops = decodeK02(instr);
  char buf[32];
  snprintf(buf, sizeof(buf), "%+d", (int)ops.k);
  return buf;
}

// Format a 22-bit absolute address as byte address (×2)
static std::string fmtK22(uint16_t instr, uint16_t extra) {
  auto ops = decodeK22(instr, extra);
  return hex16((uint16_t)(ops.k * 2));
}

// Format mode suffix for LD/ST family
static const char* ldstMode(uint8_t mode) {
  switch (mode) {
    case 0: return "";
    case 1: return "+";
    case 2: return "-";
  }
  return "";
}

// Format the LDD displacement as e.g. "+5"
static std::string fmtLddQ(uint16_t instr) {
  auto ops = decodeLdd(instr);
  char buf[16];
  snprintf(buf, sizeof(buf), "+%u", ops.q);
  return buf;
}

// Compute PC-relative branch target address for display
static std::string fmtBranchTarget(uint16_t pc, int16_t offset_words) {
  // PC at time of branch already points past the instruction (pc += 2).
  // The branch offset is relative to that post-increment PC.
  // But for display purposes, we show the absolute target.
  uint16_t target = (uint16_t)(pc + 2 + offset_words * 2);
  return hex16(target);
}

std::string disassemble(uint16_t instr, uint16_t extra, uint16_t pc) {
  Opcode op;
  if (!decodeInstruction(instr, op)) {
    char buf[32];
    snprintf(buf, sizeof(buf), "???  0x%04X", instr);
    return buf;
  }

  char buf[128];

  switch (op.fmt) {
    case AvrFmt::NONE:
      return op.mnemonic;

    case AvrFmt::Rd_Rr: {
      auto ops = decodeRdRr(instr);
      // Handle aliases: CLR, TST, LSL, ROL
      if (op.op == AvrOp::CLR) {
        snprintf(buf, sizeof(buf), "CLR R%u", ops.d);
      } else if (op.op == AvrOp::TST) {
        snprintf(buf, sizeof(buf), "TST R%u", ops.d);
      } else if (op.op == AvrOp::LSL) {
        snprintf(buf, sizeof(buf), "LSL R%u", ops.d);
      } else if (op.op == AvrOp::ROL) {
        snprintf(buf, sizeof(buf), "ROL R%u", ops.d);
      } else {
        snprintf(buf, sizeof(buf), "%s R%u, R%u", op.mnemonic, ops.d, ops.r);
      }
      return buf;
    }

    case AvrFmt::Rd_K8: {
      auto ops = decodeRdK8(instr);
      if (op.op == AvrOp::CBR) {
        snprintf(buf, sizeof(buf), "CBR R%u, %s", ops.d, hex8((uint8_t)(~ops.k & 0xFF)).c_str());
      } else if (op.op == AvrOp::SBR) {
        snprintf(buf, sizeof(buf), "SBR R%u, %s", ops.d, hex8(ops.k).c_str());
      } else if (op.op == AvrOp::SER) {
        snprintf(buf, sizeof(buf), "SER R%u", ops.d);
      } else {
        snprintf(buf, sizeof(buf), "%s R%u, %s", op.mnemonic, ops.d, hex8(ops.k).c_str());
      }
      return buf;
    }

    case AvrFmt::Rd_only: {
      auto ops = decodeRd(instr);
      if (op.op == AvrOp::LPM) {
        // The opcode table has different mnemonics for the three LPM forms
        snprintf(buf, sizeof(buf), "%s", op.mnemonic);
      } else {
        snprintf(buf, sizeof(buf), "%s R%u", op.mnemonic, ops.d);
      }
      return buf;
    }

    case AvrFmt::Rd_Rr_mpy: {
      auto ops = decodeRdRrMpy(instr);
      snprintf(buf, sizeof(buf), "%s R%u, R%u", op.mnemonic, ops.d, ops.r);
      return buf;
    }

    case AvrFmt::Rd06_K6: {
      auto ops = decodeRd06K6(instr);
      snprintf(buf, sizeof(buf), "%s R%u, %u", op.mnemonic, ops.d, ops.k);
      return buf;
    }

    case AvrFmt::Rd06_Rr06: {
      auto ops = decodeRd06Rr06(instr);
      if (op.op == AvrOp::MOVW) {
        snprintf(buf, sizeof(buf), "MOVW R%u, R%u", ops.d * 2, ops.r * 2);
      } else {
        // MULS
        snprintf(buf, sizeof(buf), "MULS R%u, R%u", ops.d + 16, ops.r + 16);
      }
      return buf;
    }

    case AvrFmt::b_only: {
      auto ops = decodeBOnly(instr);
      // Check for named alias
      static const char* setNames[8] = {"SEC","SEZ","SEN","SEV","SES","SEH","SET","SEI"};
      static const char* clrNames[8] = {"CLC","CLZ","CLN","CLV","CLS","CLH","CLT","CLI"};
      if (op.op == AvrOp::BSET) {
        snprintf(buf, sizeof(buf), "%s", setNames[ops.b]);
      } else {
        snprintf(buf, sizeof(buf), "%s", clrNames[ops.b]);
      }
      return buf;
    }

    case AvrFmt::Rd_b: {
      auto ops = decodeRdB(instr);
      snprintf(buf, sizeof(buf), "%s R%u, %u", op.mnemonic, ops.d, ops.b);
      return buf;
    }

    case AvrFmt::Rr_b: {
      auto ops = decodeRrB(instr);
      snprintf(buf, sizeof(buf), "%s R%u, %u", op.mnemonic, ops.r, ops.b);
      return buf;
    }

    case AvrFmt::IO_b: {
      auto ops = decodeIOB(instr);
      snprintf(buf, sizeof(buf), "%s %s, %u", op.mnemonic, hex6(ops.a).c_str(), ops.b);
      return buf;
    }

    case AvrFmt::Rd_IO: {
      auto ops = decodeRdIO(instr);
      snprintf(buf, sizeof(buf), "%s R%u, %s", op.mnemonic, ops.d, hex6(ops.a).c_str());
      return buf;
    }

    case AvrFmt::IO_Rr: {
      auto ops = decodeIORr(instr);
      snprintf(buf, sizeof(buf), "%s %s, R%u", op.mnemonic, hex6(ops.a).c_str(), ops.r);
      return buf;
    }

    case AvrFmt::k7: {
      auto ops = decodeK7(instr);
      // BRBC/BRBS with named aliases
      static const char* brbsNames[8] = {
        "BRCS","BREQ","BRMI","BRVS","BRLT","BRHS","BRTS","BRIE"
      };
      static const char* brbcNames[8] = {
        "BRCC","BRNE","BRPL","BRVC","BRGE","BRHC","BRTC","BRID"
      };
      const char* name = (op.op == AvrOp::BRBS) ? brbsNames[ops.s] : brbcNames[ops.s];
      snprintf(buf, sizeof(buf), "%s %s  ; → %s", name, fmtK7(instr).c_str(),
               fmtBranchTarget(pc, ops.k).c_str());
      return buf;
    }

    case AvrFmt::k02: {
      auto ops = decodeK02(instr);
      int16_t target = (int16_t)(pc + 2 + ops.k * 2);
      char tbuf[8];
      snprintf(tbuf, sizeof(tbuf), "0x%04X", (uint16_t)target);
      snprintf(buf, sizeof(buf), "%s %s  ; → %s", op.mnemonic,
               fmtK02(instr).c_str(), tbuf);
      return buf;
    }

    case AvrFmt::k02_call: {
      auto ops = decodeK02(instr); // same layout as k02
      int16_t target = (int16_t)(pc + 2 + ops.k * 2);
      char tbuf[8];
      snprintf(tbuf, sizeof(tbuf), "0x%04X", (uint16_t)target);
      snprintf(buf, sizeof(buf), "RCALL %s  ; → %s", fmtK02(instr).c_str(), tbuf);
      return buf;
    }

    case AvrFmt::k22: {
      snprintf(buf, sizeof(buf), "JMP %s", fmtK22(instr, extra).c_str());
      return buf;
    }

    case AvrFmt::k22_call: {
      snprintf(buf, sizeof(buf), "CALL %s", fmtK22(instr, extra).c_str());
      return buf;
    }

    case AvrFmt::LD_family: {
      auto ops = decodeLdSt(instr);
      const char* mode = ldstMode(ops.mode);
      // Determine X/Y/Z from the opcode
      char reg = '?';
      switch (op.op) {
        case AvrOp::LD_X: case AvrOp::ST_X: reg = 'X'; break;
        case AvrOp::LD_Y: case AvrOp::ST_Y: reg = 'Y'; break;
        case AvrOp::LD_Z: case AvrOp::ST_Z: reg = 'Z'; break;
        default: break;
      }
      bool isPop = (op.op == AvrOp::POP);
      if (isPop) {
        snprintf(buf, sizeof(buf), "POP R%u", ops.d);
      } else {
        // Check if it's a post-inc/pre-dec from mnemonic
        const char* mn = op.mnemonic;
        bool hasSuffix = (mn[3] == '+' || mn[3] == '-');
        if (hasSuffix) {
          snprintf(buf, sizeof(buf), "%s R%u, %c%s", mn, ops.d, reg, mode);
        } else if (mode[0] == '+') {
          snprintf(buf, sizeof(buf), "%s R%u, %c+", mn, ops.d, reg);
        } else if (mode[0] == '-') {
          snprintf(buf, sizeof(buf), "%s R%u, -%c", mn, ops.d, reg);
        } else {
          snprintf(buf, sizeof(buf), "%s R%u, %c", mn, ops.d, reg);
        }
      }
      return buf;
    }

    case AvrFmt::LDD_family: {
      auto ops = decodeLdd(instr);
      char reg = (op.op == AvrOp::LD_Y || op.op == AvrOp::ST_Y) ? 'Y' : 'Z';
      snprintf(buf, sizeof(buf), "%s R%u, %c%s", op.mnemonic, ops.d, reg,
               fmtLddQ(instr).c_str());
      return buf;
    }

    case AvrFmt::LDS_STS: {
      auto ops = decodeLdsSts(instr, extra);
      snprintf(buf, sizeof(buf), "%s R%u, %s", op.mnemonic, ops.d, hex16(ops.addr).c_str());
      return buf;
    }
  }

  return op.mnemonic; // fallback
}

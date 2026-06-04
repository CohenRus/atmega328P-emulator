#include "memory.h"
#include "error.h"
#include "timer0.h"

namespace {

constexpr uint16_t DATA_SPACE_END = 0x08FF;
constexpr uint16_t STACK_BOTTOM   = 0x0100;

bool g_memoryFault = false;

void faultDataAddr(uint16_t addr, const char* access) {
  g_memoryFault = true;
  emuErrorPcAddr(emuFaultPc(), addr, access, "invalid data-space access");
}

void faultStack(const char* detail) {
  g_memoryFault = true;
  emuErrorPc(emuFaultPc(), detail);
}

bool dataAddrOk(uint16_t addr) {
  return addr <= DATA_SPACE_END;
}

}  // namespace

bool memoryFaultPending() {
  return g_memoryFault;
}

void memoryClearFault() {
  g_memoryFault = false;
}

void memorySignalFault() {
  g_memoryFault = true;
}

// ===========================================================================
// SREG Flags
// ===========================================================================

void setFlag(AvrState& state, SregBit bit) {
    state.sreg |= (1 << static_cast<uint8_t>(bit));
}

void clearFlag(AvrState& state, SregBit bit) {
    state.sreg &= ~(1 << static_cast<uint8_t>(bit));
}

bool getFlag(const AvrState& state, SregBit bit) {
    return (state.sreg >> static_cast<uint8_t>(bit)) & 1;
}

// ===========================================================================
// GP Register Access
// ===========================================================================

uint8_t readReg(const AvrState& state, uint8_t reg) {
    return state.r[reg];
}

void writeReg(AvrState& state, uint8_t reg, uint8_t value) {
    state.r[reg] = value;
}

uint16_t readRegWord(const AvrState& state, uint8_t reg) {
    return (uint16_t)state.r[reg] | ((uint16_t)state.r[reg + 1] << 8);
}

void writeRegWord(AvrState& state, uint8_t reg, uint16_t value) {
    state.r[reg]     = (uint8_t)(value & 0xFF);
    state.r[reg + 1] = (uint8_t)(value >> 8);
}

// ===========================================================================
// Data Space (unified address map)
// ===========================================================================

uint8_t readDataByte(const AvrState& state, uint16_t addr) {
    // Register file: 0x0000 – 0x001F
    if (addr < 0x20) {
        return state.r[addr];
    }

    // Three I/O registers backed by AvrState struct fields (not sram[])
    if (addr == 0x5D) {        // SPL
        return (uint8_t)(state.sp & 0xFF);
    }
    if (addr == 0x5E) {        // SPH
        return (uint8_t)(state.sp >> 8);
    }
    if (addr == 0x5F) {        // SREG
        return state.sreg;
    }

    // Timer0 register range (data-space 0x35, 0x44–0x48, 0x6E)
    {
        uint8_t val = 0;
        if (timer0Read(addr - 0x20, &val)) return val;
    }

    // UART register range (data-space 0xC0–0xC6 for ATmega328P)
    if (addr >= 0xC0 && addr <= 0xC6) {
        return uartRead(addr - 0xC0);
    }

    // UART register range (data-space 0x29–0x2D for legacy ATmega168)
     if (addr >= 0x29 && addr <= 0x2D) {
         return uartRead(addr - 0x20);
     }

    // I/O, extended I/O, and SRAM — all in sram[] indexed by data-space addr
    if (!dataAddrOk(addr)) {
      faultDataAddr(addr, "read");
      return 0;
    }
    return state.sram[addr];
}

void writeDataByte(AvrState& state, uint16_t addr, uint8_t value) {
    // Register file: 0x0000 – 0x001F
    if (addr < 0x20) {
        state.r[addr] = value;
        return;
    }

    // Three I/O registers backed by AvrState struct fields
    if (addr == 0x5D) {        // SPL
        state.sp = (state.sp & 0xFF00) | value;
        return;
    }
    if (addr == 0x5E) {        // SPH
        state.sp = (state.sp & 0x00FF) | ((uint16_t)value << 8);
        return;
    }
    if (addr == 0x5F) {        // SREG
        state.sreg = value;
        return;
    }

    // Timer0 register range (data-space 0x35, 0x44–0x48, 0x6E)
    if (timer0Write(addr - 0x20, value)) return;

    // UART register range (data-space 0xC0–0xC6 for ATmega328P)
    if (addr >= 0xC0 && addr <= 0xC6) {
        uartWrite(addr - 0xC0, value);
        return;
    }

    // UART register range (data-space 0x29–0x2D for legacy ATmega168)
    if (addr >= 0x29 && addr <= 0x2D) {
        uartWrite(addr - 0x20, value);
        return;
    }

    // I/O, extended I/O, and SRAM
    if (!dataAddrOk(addr)) {
      faultDataAddr(addr, "write");
      return;
    }
    state.sram[addr] = value;
}

// ===========================================================================
// I/O Register Bit Helpers
// ===========================================================================

bool getIOBit(const AvrState& state, uint8_t ioAddr, uint8_t bit) {
    return (readDataByte(state, 0x20 + ioAddr) >> bit) & 1;
}

void setIOBit(AvrState& state, uint8_t ioAddr, uint8_t bit) {
    uint8_t val = readDataByte(state, 0x20 + ioAddr);
    writeDataByte(state, 0x20 + ioAddr, val | (1 << bit));
}

void clearIOBit(AvrState& state, uint8_t ioAddr, uint8_t bit) {
    uint8_t val = readDataByte(state, 0x20 + ioAddr);
    writeDataByte(state, 0x20 + ioAddr, val & ~(1 << bit));
}

// ===========================================================================
// Stack
// ===========================================================================

void pushByte(AvrState& state, uint8_t value) {
    if (state.sp <= STACK_BOTTOM) {
      faultStack("stack overflow (SP would drop below 0x0100)");
      return;
    }
    state.sp--;
    writeDataByte(state, state.sp, value);
}

uint8_t popByte(AvrState& state) {
    if (state.sp > DATA_SPACE_END) {
      faultStack("stack underflow (SP above RAMEND 0x08FF)");
      return 0;
    }
    uint8_t value = readDataByte(state, state.sp);
    state.sp++;
    return value;
}

void pushWord(AvrState& state, uint16_t value) {
    // Low byte first (higher stack address), high byte second (lower address)
    pushByte(state, (uint8_t)(value & 0xFF));
    pushByte(state, (uint8_t)(value >> 8));
}

uint16_t popWord(AvrState& state) {
    // High byte first (lower stack address), then low byte
    uint8_t hi = popByte(state);
    uint8_t lo = popByte(state);
    return ((uint16_t)hi << 8) | lo;
}

// ===========================================================================
// Program Memory (Flash)
// ===========================================================================

uint16_t readFlashWord(const AvrState& state, uint32_t byteAddr) {
    return (uint16_t)state.flash[byteAddr] | ((uint16_t)state.flash[byteAddr + 1] << 8);
}

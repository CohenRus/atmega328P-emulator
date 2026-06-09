// ===========================================================================
// memory.cpp — ATmega328P data-space access layer.
//
// Implements the unified data-space address map: register file (0x0000–0x001F),
// I/O registers (0x0020–0x005F with SPL/SPH/SREG proxies), extended I/O
// (0x0060–0x00FF), and SRAM (0x0100–0x08FF).  All byte reads/writes route
// through readDataByte / writeDataByte so peripheral routing and fault
// detection live in exactly one place.
//
// Key design decisions:
// - SPL (0x5D), SPH (0x5E), SREG (0x5F) are proxied to state.sp / state.sreg
//   fields rather than stored in sram[], so the stack pointer and status
//   register can be manipulated directly by instructions.
// - Timer0 and UART registers are routed to their respective module handlers
//   at the data-space level; I/O address offsets (0x20 for Timer0, 0xC0 for
//   UART on ATmega328P) are translated here so callers see a unified map.
// - Stack overflow/underflow is detected with bounds checks against
//   STACK_BOTTOM (0x0100) and DATA_SPACE_END (0x08FF).
// ===========================================================================
#include "memory.h"
#include "error.h"
#include "state.h"
#include "timer0.h"

// Top of the data-space address range (ATmega328P: 0x08FF = RAMEND).
constexpr uint16_t DATA_SPACE_END = 0x08FF;

// Lowest valid stack address — SP must stay above this to avoid corrupting
// the I/O register region.
constexpr uint16_t STACK_BOTTOM   = 0x0100;

// Global fault flag used by the executor to halt emulation on bad accesses.
bool g_memoryFault = false;

// Log an out-of-bounds data-space access and raise the fault flag.
// @param addr   — data-space address that was accessed
// @param access — "read" or "write", for the error message
void faultDataAddr(uint16_t addr, const char* access) {
  g_memoryFault = true;
  emuErrorPcAddr(emuFaultPc(), addr, access, "invalid data-space access");
}
static void dumpFaultContext(const AvrState& state) {
  std::fprintf(stderr, "debug: fault context — PC=0x%04X SP=0x%04X X=0x%02X%02X Y=0x%02X%02X Z=0x%02X%02X\n",
               state.pc, state.sp,
               state.r[27], state.r[26],
               state.r[29], state.r[28],
               state.r[31], state.r[30]);
  std::fprintf(stderr, "debug: registers — ");
  for (int i = 0; i < 32; i++) std::fprintf(stderr, "r%02d=0x%02X ", i, state.r[i]);
  std::fprintf(stderr, "\nsreg=0x%02X\n", state.sreg);
}

// Log a stack-related fault (overflow or underflow) and raise the fault flag.
// @param detail — human-readable description of the fault
void faultStack(const char* detail) {
  g_memoryFault = true;
  emuErrorPc(emuFaultPc(), detail);
}

// True when addr is within the valid data-space range [0, 0x08FF].
// Addresses above DATA_SPACE_END trigger a fault in readDataByte/writeDataByte.
bool dataAddrOk(uint16_t addr) {
  return addr <= DATA_SPACE_END;
}

// Returns true if a memory fault has been raised (bad address or stack error).
// The executor checks this after every instruction to halt on faults.
// @return whether a fault is currently pending
bool memoryFaultPending() {
  return g_memoryFault;
}

// Clear a pending memory fault so execution can resume after the fault handler.
void memoryClearFault() {
  g_memoryFault = false;
}

// Manually raise the memory fault flag (e.g. from an unhandled peripheral error).
void memorySignalFault() {
  g_memoryFault = true;
}
// Set a single bit in the status register.
// @param state — emulator state
// @param bit   — which SREG bit to set (SregBit enum)
void setFlag(AvrState& state, SregBit bit) {
    state.sreg |= (1 << static_cast<uint8_t>(bit));
}

// Clear a single bit in the status register.
// @param state — emulator state
// @param bit   — which SREG bit to clear
void clearFlag(AvrState& state, SregBit bit) {
    state.sreg &= ~(1 << static_cast<uint8_t>(bit));
}

// Read a single bit from the status register.
// @param state — emulator state (const — does not modify)
// @param bit   — which SREG bit to read
// @return current value of the bit (true = 1, false = 0)
bool getFlag(const AvrState& state, SregBit bit) {
    return (state.sreg >> static_cast<uint8_t>(bit)) & 1;
}


// ===========================================================================
// GP Register Access
// ===========================================================================

// Read a single general-purpose register R0–R31.
// @param state — emulator state
// @param reg   — register index (0–31)
// @return register value
uint8_t readReg(const AvrState& state, uint8_t reg) {
    return state.r[reg];
}

// Write a single general-purpose register R0–R31.
// @param state — emulator state (mutable)
// @param reg   — register index (0–31)
// @param value — byte to store
void writeReg(AvrState& state, uint8_t reg, uint8_t value) {
    state.r[reg] = value;
}

// Read a 16-bit word from a register pair.  reg must be even (R0:R1, R2:R3, …).
// Little-endian: low byte = r[reg], high byte = r[reg + 1].
// @param state — emulator state
// @param reg   — even register index (0, 2, …, 30)
// @return 16-bit value formed from r[reg] | (r[reg+1] << 8)
uint16_t readRegWord(const AvrState& state, uint8_t reg) {
    return (uint16_t)state.r[reg] | ((uint16_t)state.r[reg + 1] << 8);
}

// Write a 16-bit word to a register pair.  reg must be even.
// Low byte goes to r[reg], high byte to r[reg + 1].
// @param state — emulator state (mutable)
// @param reg   — even register index
// @param value — 16-bit value to store
void writeRegWord(AvrState& state, uint8_t reg, uint16_t value) {
    state.r[reg]     = (uint8_t)(value & 0xFF);
    state.r[reg + 1] = (uint8_t)(value >> 8);
}

// ===========================================================================
// Data Space (unified address map)
// ===========================================================================

// Read a single byte from the unified data-space address map.
//
// Address routing order (first match wins):
//   1. 0x0000–0x001F → register file (state.r[])
//   2. 0x005D (SPL), 0x005E (SPH), 0x005F (SREG) → struct field proxies
//   3. 0x0035, 0x0044–0x0048, 0x006E → Timer0 peripheral (via timer0Read)
//   4. 0x00C0–0x00C6 → UART peripheral ATmega328P mapping (data-space - 0xC0)
//   5. 0x0029–0x002D → UART peripheral legacy ATmega168 mapping (data-space - 0x20)
//   6. 0x0020–0x08FF → SRAM array (state.sram[addr])
//
// @param state — emulator state (const)
// @param addr  — data-space address (0x0000–0x08FF)
// @return byte value at addr, or 0 if out of bounds (fault raised)
uint8_t readDataByte(const AvrState& state, uint16_t addr) {
    // Register file: 0x0000 – 0x001F — direct access to GP registers
    if (addr < 0x20) {
        return state.r[addr];
    }

    // Three I/O registers backed by AvrState struct fields (not sram[]).
    // These are proxied so instructions manipulating SP/SREG directly
    // (e.g. PUSH, POP, SEI, CLI) see consistent state without sram[] mirroring.
    if (addr == 0x5D) {        // SPL — low byte of stack pointer
        return (uint8_t)(state.sp & 0xFF);
    }
    if (addr == 0x5E) {        // SPH — high byte of stack pointer
        return (uint8_t)(state.sp >> 8);
    }
    if (addr == 0x5F) {        // SREG — status register
        return state.sreg;
    }

    // Timer0 register range (data-space 0x35, 0x44–0x48, 0x6E).
    // timer0Read expects an I/O address (0x00–0x3F), but we pass data-space minus 0x20,
    // which works because all Timer0 registers fall within the I/O range.
    {
        uint8_t val = 0;
        if (timer0Read(addr - 0x20, &val)) return val;
    }

    // UART register range (data-space 0xC0–0xC6 for ATmega328P extended I/O).
    // uartRead expects an I/O address relative to 0xC0.
    if (addr >= 0xC0 && addr <= 0xC6) {
        return uartRead(addr - 0xC0);
    }

    // UART register range (data-space 0x29–0x2D for legacy ATmega168 mapping).
    // Falls within the standard I/O space; pass I/O address (addr - 0x20).
     if (addr >= 0x29 && addr <= 0x2D) {
         return uartRead(addr - 0x20);
     }

    // I/O, extended I/O, and SRAM — all stored contiguously in state.sram[].
    // sram[] is indexed directly by data-space address (0x0020–0x08FF).
    if (!dataAddrOk(addr)) {
      dumpFaultContext(state);
      faultDataAddr(addr, "read");
      return 0;
    }
    return state.sram[addr];
}

// Write a single byte into the unified data-space address map.
// Address routing mirrors readDataByte — see that function for the full map.
//
// Key differences from reads:
// - SPL write: (state.sp & 0xFF00) | value — only low byte is affected
// - SPH write: (state.sp & 0x00FF) | (value << 8) — only high byte affected
// - Timer0/UART writes are routed to their handlers which mutate peripheral state
//
// @param state — emulator state (mutable)
// @param addr  — data-space address (0x0000–0x08FF)
// @param value — byte to store
void writeDataByte(AvrState& state, uint16_t addr, uint8_t value) {
    // Register file: 0x0000 – 0x001F — direct write to GP registers
    if (addr < 0x20) {
        state.r[addr] = value;
        return;
    }

    // Three I/O registers backed by AvrState struct fields.
    // SPL/SPH writes only affect the targeted byte of the 16-bit SP.
    if (addr == 0x5D) {        // SPL — replace low byte of SP
        state.sp = (state.sp & 0xFF00) | value;
        return;
    }
    if (addr == 0x5E) {        // SPH — replace high byte of SP
        state.sp = (state.sp & 0x00FF) | ((uint16_t)value << 8);
        return;
    }
    if (addr == 0x5F) {        // SREG — full status register replacement
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

    // I/O, extended I/O, and SRAM (contiguous sram[] array)
    if (!dataAddrOk(addr)) {
      dumpFaultContext(state);
      faultDataAddr(addr, "write");
      return;
    }
    state.sram[addr] = value;
}


// ===========================================================================
// I/O Register Bit Helpers
// ===========================================================================

// Read a single bit from an I/O register.
// Used by SBIC / SBIS instructions to skip on bit state.
// @param state  — emulator state
// @param ioAddr — I/O address (0x00–0x3F), mapped to data-space 0x20 + ioAddr
// @param bit    — bit position (0–7)
// @return current state of the bit
bool getIOBit(const AvrState& state, uint8_t ioAddr, uint8_t bit) {
    return (readDataByte(state, 0x20 + ioAddr) >> bit) & 1;
}

// Set a single bit in an I/O register (read-modify-write).
// Used by SBI instruction.
// @param state  — emulator state (mutable)
// @param ioAddr — I/O address (0x00–0x3F)
// @param bit    — bit position to set
void setIOBit(AvrState& state, uint8_t ioAddr, uint8_t bit) {
    uint8_t val = readDataByte(state, 0x20 + ioAddr);
    writeDataByte(state, 0x20 + ioAddr, val | (1 << bit));
}
// Clear a single bit in an I/O register (read-modify-write).
// Used by CBI instruction.
// @param state  — emulator state (mutable)
// @param ioAddr — I/O address (0x00–0x3F)
// @param bit    — bit position to clear
void clearIOBit(AvrState& state, uint8_t ioAddr, uint8_t bit) {
    uint8_t val = readDataByte(state, 0x20 + ioAddr);
    writeDataByte(state, 0x20 + ioAddr, val & ~(1 << bit));
}

// ===========================================================================
// Stack
// ===========================================================================

// Push a byte onto the hardware stack.
// The value is written at SP, then SP is decremented (stack grows downward).
// Detects overflow if SP would fall into the I/O register region (< 0x0100).
// @param state — emulator state (mutable)
// @param value — byte to push
void pushByte(AvrState& state, uint8_t value) {
    if (state.sp < STACK_BOTTOM || state.sp > DATA_SPACE_END) {
      faultStack("stack overflow (SP would drop below 0x0100)");
      return;
    }
    writeDataByte(state, state.sp, value);
    state.sp--;
}

// Pop a byte from the hardware stack.
// SP is incremented first, then the value is read.
// Detects underflow if SP would go above DATA_SPACE_END (0x08FF).
// @param state — emulator state (mutable)
// @return byte popped from stack, or 0 on underflow (fault raised)
uint8_t popByte(AvrState& state) {
    if (state.sp >= DATA_SPACE_END) {
      faultStack("stack underflow (SP above RAMEND 0x08FF)");
      return 0;
    }
    state.sp++;
    return readDataByte(state, state.sp);
}

// Push a 16-bit word onto the stack.
// Low byte is pushed first (at the higher address), then the high byte.
// This matches the ATmega328P little-endian byte order.
// @param state — emulator state (mutable)
// @param value — 16-bit word to push
void pushWord(AvrState& state, uint16_t value) {
    // Low byte first (higher address), high byte second (lower address).
    pushByte(state, (uint8_t)(value & 0xFF));
    pushByte(state, (uint8_t)(value >> 8));
}

// Pop a 16-bit word from the stack.
// High byte is popped first (from the lower address), then the low byte.
// @param state — emulator state (mutable)
// @return 16-bit word assembled as (hi << 8) | lo
uint16_t popWord(AvrState& state) {
    // High byte first (lower stack address), then low byte
    uint8_t hi = popByte(state);
    uint8_t lo = popByte(state);
    return ((uint16_t)hi << 8) | lo;
}


// ===========================================================================
// Program Memory (Flash)
// ===========================================================================

// Read a 16-bit little-endian word from flash memory at the given byte address.
// Used by LPM / ELPM instructions to load data from program space.
// @param state    — emulator state (const)
// @param byteAddr — byte address in flash (must be even within [0, AVR_FLASH_SIZE))
// @return 16-bit word: flash[byteAddr] | (flash[byteAddr + 1] << 8)
uint16_t readFlashWord(const AvrState& state, uint32_t byteAddr) {
    return (uint16_t)state.flash[byteAddr] | ((uint16_t)state.flash[byteAddr + 1] << 8);
}

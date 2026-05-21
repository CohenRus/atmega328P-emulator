# Memory Helpers for ATmega328P Emulator — Planning Document

## 1. Problem & Outcome

**Current state:** `src/executor.cpp` contains ~50 empty executor function stubs. Each instruction executor will need to read/write GP registers, set/clear SREG flags, access the data address space (registers + I/O + SRAM as a unified map), push/pop the stack, and read program memory. Without helpers, every executor must duplicate address-decoding logic, SREG bit-twiddling, and bounds checking inline.

**Desired outcome:** A single `memory.h` / `memory.cpp` module that exports clean, narrowly-scoped helper functions so each `executeX()` function body is ~3–10 lines of arithmetic/branch logic calling helpers for all state access. The helpers encapsulate the ATmega328P's unified address-space model, register-pair word semantics, SREG flag layout, and stack mechanics.

**Success signals:**
- Every executor stub can be filled in by calling only helpers from `memory.h` + direct C++ arithmetic — no inline `state.sreg |= (1 << 3)` or manual address-range `if/else` chains in executor code.
- Helpers have clear, grep-friendly names that match AVR datasheet terminology (e.g. `setFlag`, `readDataByte`, `writeDataByte`, `pushByte`).
- The module is ~150–250 lines total (header + impl), not a sprawling abstraction layer.

## 2. Scope

**In scope:**
- Flag helpers: read/set/clear individual SREG bits (I, T, H, S, V, N, Z, C).
- Register access: read/write a GP register byte (`r[d]`) and word register pairs (`r[d]`, `r[d+1]` for even `d`).
- Unified data-space read/write: given a 16-bit address, route to GP registers (0x00–0x1F), I/O registers (0x20–0x5F via IN/OUT mapping; 0x20–0xFF for LD/ST mapping), or SRAM (0x0100–0x08FF). Include bounds-checking.
- Stack operations: `pushByte` (decrement SP, write SRAM), `popByte` (read SRAM, increment SP), `pushWord` / `popWord` for CALL/RET/RCALL/RETI.
- Program memory read: `readFlashWord` (16-bit word at byte address) for LPM.
- I/O register bit helpers: `getIOBit`, `setIOBit`, `clearIOBit` for SBI/CBI/SBIC/SBIS.

**Out of scope (explicit):**
- EEPROM read/write — accessed via a multi-step I/O register dance (EEAR, EEDR, EECR), not simple helpers. Deferred until EEPROM-aware instructions are implemented.
- SPM (self-programming of flash) — complex, rarely used. Deferred.
- Peripheral emulation (UART, timers, GPIO ports) — these will be separate modules that hook into I/O register reads/writes.
- Any threading or interrupt infrastructure.
- Performance optimization (caching, hot-path inlining decisions) — correctness first.

## 3. Requirements

### Product expectations
- Executor functions for arithmetic (ADD, SUB, ADC, SBC, etc.), logic (AND, OR, EOR), data transfer (MOV, LDI, LD*, ST*, IN, OUT, PUSH, POP), branching (RJMP, JMP, BR*, CALL, RET), and bit ops (SBI, CBI, BLD, BST) must be implementable using only helpers + plain C++ operators.
- Helpers must match the ATmega328P datasheet's address-space layout exactly.

### Engineering constraints
- **Language:** C++20, headers use `#pragma once`, match existing codebase conventions.
- **Interface:** Free functions in `memory.h`, implemented in `memory.cpp`. All take `AvrState&` as first parameter. No classes, no templates (keep it simple).
- **No heap allocation:** All helpers operate on `AvrState` arrays in place.
- **No exceptions:** Return `bool` for success/failure (or `void` when the operation is infallible). Bounds violations return `false`.

## 4. Scale & Constraints

N/A — single-threaded emulator, one instruction at a time, no server component. Each helper is called once per emulated instruction. ATmega328P runs at ~16 MHz max; emulator throughput is irrelevant at this stage.

## 5. Data Modeling

The key model is the unified data address space. The existing `AvrState` already holds the physical arrays; the helpers encode the mapping:

```
Data Space Address → Physical Storage
─────────────────────────────────────────
0x0000 – 0x001F    →  state.r[0..31]       (32 GP registers)
0x0020 – 0x005F    →  state.sram[0x20..0x5F] (64 I/O registers)
0x0060 – 0x00FF    →  state.sram[0x60..0xFF] (160 extended I/O)
0x0100 – 0x08FF    →  state.sram[0x100..0x8FF] (2048 bytes SRAM)
0x0900+             →  unmapped (out of bounds for ATmega328P)
```

SREG flag bit positions (`state.sreg`):
```
Bit 7 (0x80): I — Global Interrupt Enable
Bit 6 (0x40): T — Bit Copy Storage
Bit 5 (0x20): H — Half Carry
Bit 4 (0x10): S — Sign (N ⊕ V)
Bit 3 (0x08): V — Overflow
Bit 2 (0x04): N — Negative
Bit 1 (0x02): Z — Zero
Bit 0 (0x01): C — Carry
```

Register pairs for word operations: register `d` must be even (R0, R2, …, R30). `r[d]` is the low byte, `r[d+1]` is the high byte. Word value = `r[d] | (r[d+1] << 8)`.

Stack: `state.sp` points to the next free byte. On push, SP is decremented first, then the byte is written. On pop, the byte is read, then SP is incremented. SP initializes to RAMEND (0x08FF for ATmega328P). Stack lives in SRAM.

Flash: `state.flash[]` is a byte array. Each 16-bit instruction word occupies two consecutive bytes (little-endian: low byte first). LPM addresses are byte addresses; `Z` register holds the byte address. Addresses beyond `AVR_FLASH_SIZE` are out of bounds.

Special I/O register routing: three I/O registers in the 0x20–0x5F range are backed by `AvrState` struct fields rather than `state.sram[]`, because the emulator manipulates them directly during execution:

| Data addr | I/O addr | Register | Backed by |
|---|---|---|---|
| 0x5D | 0x3D | SPL | low byte of `state.sp` |
| 0x5E | 0x3E | SPH | high byte of `state.sp` |
| 0x5F | 0x3F | SREG | `state.sreg` |

`readDataByte`/`writeDataByte` must route these three addresses to the struct fields. All other I/O addresses (0x20–0x5C, 0x60–0xFF) read/write `state.sram[]` directly.

## 6. Approach

A single C++ module (`memory.h` + `memory.cpp`) providing ~20 free functions organized into five groups:

1. **Flag ops** — 3 functions for SREG bit get/set/clear.
2. **Register ops** — byte read/write + word read/write (register pairs).
3. **Data space ops** — `readDataByte(addr)`, `writeDataByte(addr, value)` that route through the unified map, plus I/O bit helpers.
4. **Stack ops** — `pushByte`, `popByte`, `pushWord`, `popWord` for CALL/RCALL/RET.
5. **Flash ops** — `readFlashWord(byteAddr)` for LPM.

The unified `readDataByte` / `writeDataByte` functions contain a single `if/else if/else` chain on the address. Three I/O registers have dedicated backing in `AvrState` and route to struct fields instead of `state.sram[]`:

- Data addr **0x5D** (I/O 0x3D, SPL) → low byte of `state.sp`
- Data addr **0x5E** (I/O 0x3E, SPH) → high byte of `state.sp`
- Data addr **0x5F** (I/O 0x3F, SREG) → `state.sreg`

All other I/O registers (0x20–0x5C, 0x60–0xFF) route to `state.sram[]`. I/O bit helpers (`getIOBit`, `setIOBit`, `clearIOBit`) call `readDataByte`/`writeDataByte` on the I/O register address, then mask the target bit.

## 7. Implementation Details

### File / module plan

| File | Action | Responsibility | Key exports |
|---|---|---|---|
| `src/memory.h` | Modify (currently empty) | Declare all helper signatures | `setFlag()`, `getFlag()`, `readReg()`, `writeReg()`, `readRegWord()`, `writeRegWord()`, `readDataByte()`, `writeDataByte()`, `getIOBit()`, `setIOBit()`, `clearIOBit()`, `pushByte()`, `popByte()`, `pushWord()`, `popWord()`, `readFlashWord()` |
| `src/memory.cpp` | Modify (currently empty) | Implement all helpers | (same symbols) |

No new files needed. No changes to `state.h` needed — the existing `AvrState` struct is complete for this scope.

### Key interfaces & signatures

```cpp
// memory.h

#pragma once
#include <cstdint>
#include "state.h"

// ── SREG Flags ───────────────────────────────────────────────────────────

// Bit indices into state.sreg (for use with the generic helpers below)
enum class SregBit : uint8_t {
    C = 0,  // Carry
    Z = 1,  // Zero
    N = 2,  // Negative
    V = 3,  // Two's complement overflow
    S = 4,  // Sign (N xor V)
    H = 5,  // Half Carry
    T = 6,  // Bit copy storage
    I = 7,  // Global interrupt enable
};

void setFlag(AvrState& state, SregBit bit);
void clearFlag(AvrState& state, SregBit bit);
bool getFlag(const AvrState& state, SregBit bit);

// ── GP Register Access ───────────────────────────────────────────────────

uint8_t  readReg(const AvrState& state, uint8_t reg);
void     writeReg(AvrState& state, uint8_t reg, uint8_t value);

// Word ops: reg must be even (R0..R30); returns reg as low byte, reg+1 as high
uint16_t readRegWord(const AvrState& state, uint8_t reg);
void     writeRegWord(AvrState& state, uint8_t reg, uint16_t value);

// ── Data Space (unified: registers + I/O + SRAM) ─────────────────────────

uint8_t readDataByte(const AvrState& state, uint16_t addr);
void    writeDataByte(AvrState& state, uint16_t addr, uint8_t value);

// ── I/O Register Bit Helpers ─────────────────────────────────────────────

bool    getIOBit(const AvrState& state, uint8_t ioAddr, uint8_t bit);
void    setIOBit(AvrState& state, uint8_t ioAddr, uint8_t bit);
void    clearIOBit(AvrState& state, uint8_t ioAddr, uint8_t bit);

// ── Stack ────────────────────────────────────────────────────────────────

void pushByte(AvrState& state, uint8_t value);
uint8_t popByte(AvrState& state);

// Push/pop a 16-bit word (low byte first, high byte second)
void     pushWord(AvrState& state, uint16_t value);
uint16_t popWord(AvrState& state);

// ── Program Memory (Flash) ───────────────────────────────────────────────

// Read a 16-bit word from flash at the given byte address (addr must be even)
uint16_t readFlashWord(const AvrState& state, uint32_t byteAddr);
```

### Data flow at call-site level

Here's how executor functions use the helpers — a concrete example for `executeLDI` and `executeST_Z`:

```
executeLDI(state, ops)               // LDI Rd, K  →  Rd = K
  → writeReg(state, ops.d, ops.k)    // writes to state.r[ops.d]

executeST_Z(state, ops)              // ST Z, Rr  →  *(Z) = Rr
  → uint16_t z = readRegWord(state, 30)         // Z = R31:R30
  → uint8_t val = readReg(state, ops.d)          // value to store
  → switch (ops.mode):
      case 0: writeDataByte(state, z, val)       // base: no change to Z
      case 1: writeDataByte(state, z, val)       // post-inc
               writeRegWord(state, 30, z + 1)
      case 2: writeRegWord(state, 30, z - 1)     // pre-dec
               writeDataByte(state, z - 1, val)
```

```
executeCALL(state, ops)              // CALL k22: push PC, jump to k
  → pushWord(state, state.pc)        // push return address
  → state.pc = ops.k                 // jump to target
```

### Implementation order

1. **`readDataByte` / `writeDataByte`** — the unified address-space router. Everything else builds on these (except flag ops and flash reads, which are independent).
2. **Flag ops** — `setFlag`, `clearFlag`, `getFlag` — independent, used by ~30 instructions.
3. **Register byte ops** — `readReg`, `writeReg` — trivially wrap `state.r[]`.
4. **Register word ops** — `readRegWord`, `writeRegWord` — for MOVW, ADIW, SBIW, LD/ST with X/Y/Z.
5. **I/O bit helpers** — `getIOBit`, `setIOBit`, `clearIOBit` — for SBI/CBI/SBIC/SBIS.
6. **Stack ops** — `pushByte`, `popByte`, `pushWord`, `popWord` — for PUSH, POP, CALL, RCALL, RET, RETI.
7. **Flash read** — `readFlashWord` — for LPM.

### Configuration & environment

None. This is a pure C++ module with no external config.

### New dependencies

None. Standard library only (`<cstdint>`).

### Migration / data changes

None. `AvrState` remains unchanged.

## 8. Proportionality

**Simplest approach:** Free functions on `AvrState&` — no classes, no vtable, no templates. The unified `readDataByte`/`writeDataByte` is one `if/else` chain (~15 lines). Each flag helper is a one-liner. Stack helpers are 3–5 lines each.

**What we are NOT building (and why):**
- No address-space abstraction class with virtual dispatch — overkill for a single emulated chip.
- No separate I/O register file — the I/O registers live in `state.sram[]` per the unified address space; no need for a parallel array.
- No EEPROM helpers — those require a state machine (wait for EEPE bit to clear), not a simple read/write call.

**Future pressure points:** The I/O register space (0x20–0xFF) will eventually need peripheral-specific side effects (writing to UART data register should transmit, reading from timer counter should reflect elapsed cycles). When that happens, `readDataByte`/`writeDataByte` will need a hook or callback mechanism for I/O addresses. This plan deliberately keeps it simple (raw SRAM reads/writes for all I/O addresses) since peripherals don't exist yet. The hook point is obvious: the `else if (addr >= 0x20 && addr <= 0xFF)` branch in `readDataByte`/`writeDataByte`.

## 9. Decisions (pre-implementation)

- **`SregBit` as an enum class rather than raw `uint8_t`:** Provides type safety and self-documenting calls (`setFlag(state, SregBit::C)` vs `state.sreg |= 0x01`). No runtime cost — compiles to the same bit-twiddling.
- **`readDataByte` returns `uint8_t` by value, not through a bool+reference out-param:** Cleaner at call sites; bounds violations are currently impossible given the emulator only generates valid addresses. If future instructions can generate bad addresses, we add an error return.
- **Stack grows downward, SP points to next free byte:** Matches AVR hardware init (SP = RAMEND after reset, first push writes to RAMEND). The `pushByte` helper decrements SP first.
- **Register word ops take even register number:** Callers (MOVW, ADIW, SBIW, X/Y/Z register pairs) naturally provide even register indices. The helper asserts or clamps if needed.
- **Flash read takes byte address, returns little-endian word:** Matches how `executeProgram()` already reads instructions (`state.flash[pc] | state.flash[pc+1] << 8`).

## 10. Risks & Open Questions

- **[Risk] I/O register side-effects:** When peripherals are added, many I/O registers will need read/write hooks. The current plan's direct-SRAM approach will need refactoring at that point. **Mitigation:** The `readDataByte`/`writeDataByte` routing is already a single chokepoint; adding hooks there is straightforward.
- **[Risk] SP initialization:** `clearState()` currently sets `state.sp = 0`. Per datasheet §6.5, the initial SP value equals the last address of internal SRAM — RAMEND = 0x08FF for ATmega328P. The stack helpers assume SP is valid; a mismatched init will cause underflow on first push (SP decrements from 0 to 0xFFFF). **Mitigation:** Fix `clearState` to set `sp = 0x08FF` (AVR_SRAM_SIZE + 0x100 - 1).
- **[Question] Should `readDataByte`/`writeDataByte` bounds-check and signal errors, or trust the emulator?** Trusting the emulator is simpler and faster. But a bounds-check on debug builds catches bugs. **Owner:** implementer's judgment — suggest a debug-only assert for now.

## 11. Test Plan

| Layer | Coverage | Method |
|---|---|---|
| Unit — flag helpers | Set, clear, get each SREG bit; verify other bits untouched | Manual test harness or inline in `main.cpp` |
| Unit — data space routing | Read/write at addresses 0x00, 0x1F, 0x20, 0x5F, 0x60, 0xFF, 0x100, 0x8FF; verify correct physical array touched | Same |
| Unit — stack ops | Push then pop; verify byte order; verify SP movement | Same |
| Unit — register word ops | Write word to R26:R27 (X), read back, verify endianness | Same |
| Integration | Pick 3–4 executor stubs (LDI, ADD, ST_Z, CALL), fill them in using helpers, run against the blink.elf test sketch | Run emulator with `./build/native/emulator tests/sketch/build/arduino.avr.uno/sketch.ino.elf` |

The project has no formal test framework. The pragmatic approach: implement helpers, fill in a few executor stubs, and run the existing blink.elf test to validate end-to-end.

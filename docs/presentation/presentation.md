---
marp: true
theme: uncover
class:
  - lead
  - invert
paginate: true
---
<style>
section { font-size: 1.3em; }
code   { font-size: 0.9em; }
table  { font-size: 0.9em; }
</style>

<!-- _class: lead invert -->

## **ATmega328P Emulator**
Building an AVR Emulator in C++20

Cohen Russell

---

<!-- _class: default -->

### What?
An emulator is software that pretends to be hardware — it reads real compiled firmware and executes it instruction-by-instruction, cycle-by-cycle.

- **Target chip:** ATmega328P — the microcontroller inside every Arduino Uno R3
- **16 MHz**, 8-bit AVR Harvard architecture
- **32 KB** flash, **2 KB** SRAM, **131** distinct instructions

---

### Why?

Part of a larger project to create a full online arduino IDE complete with editor, simulator and debugger.
This emulator will be at the core of the simulator and debugger.

---

### System Overview

```
┌──────────────┐     ┌────────────┐     ┌───────────────┐
│  ELF binary  │ ─>  │  Loader    │ ─>  │  Flash (32KB) │
│ (.text/.data)│     │ loader.cpp │     │  state.flash[]│
└──────────────┘     └────────────┘     └───────┬───────┘
                                                │
                   ┌────────────────────────────┘
                   ▼
     ┌──────────────────────────────┐
     │       Execute Loop           │
     │  executor.cpp                │
     │  fetch → decode → execute    │
     └──────┬───────────────┬───────┘
            │               │
            ▼               ▼
    ┌──────────┐    ┌──────────────┐
    │  Memory  │    │  Peripherals │
    │ SRAM/IO  │    │ Timer0, UART │
    └──────────┘    └──────────────┘
```

---

### AVR Core State

The emulator holds the entire CPU in one struct

| Component | Size | What it holds |
|---|---|---|
| **32 GP registers** (R0–R31) | 32 × 8-bit | Working data |
| **Program Counter** | 16-bit | Byte address into flash |
| **Stack Pointer** | 16-bit | Grows downward from top of SRAM |
| **SREG** | 8-bit | Status register flags |
| **Flash** | 32 KB | Program memory  |
| **SRAM** | 2304 B | Data space |
| **EEPROM** | 1 KB | Non-volatile storage |
| **Cycle count** | 64-bit | Total elapsed CPU cycles |

---

### Unified Memory Map

The ATmega328P uses a **single 16-bit address space** for everything — registers, I/O, and SRAM all share the same bus.

```
Data Space (0x0000 → 0x08FF):
┌──────────────────┐ 
│  32 GP Registers │ 0x0000 
│                  │  R0–R31 live here, not in a separate register file
├──────────────────┤ 
│  64 I/O Registers│ 0x0020 
│                  │  Timer0, UART, PORTB/D, etc. — IN/OUT instructions
├──────────────────┤ 
│ 160 Ext I/O Regs │ 0x0060
│                  │  Additional peripheral registers (LD/ST access)
├──────────────────┤ 
│                  │ 0x0100
│  2048 B SRAM     │  Stack + .data + .bss + heap
│                  │
└──────────────────┘ 
```

---

### Instruction Pipeline

- Five-stage loop: fetch → decode → extract operands → execute → advance PC
- Each iteration also: polls UART, services interrupts, advances Timer0, syncs wall-clock

| Step | What happens |
|---|---|
| **1. Fetch** | Read 16-bit word from `flash[pc]` |
| **2. Decode** | Walk opcode table: `(instr & mask) == code` |
| **3. Extract** | Call format-specific operand decoder (17 formats) |
| **4. Execute** | Dispatch via `switch(op.op)` — 76 instruction bodies |
| **5. Advance** | `pc += words × 2` or set by branch/jump/call |

---

### Decoding: Mask & Match

- Every AVR instruction is a 16-bit word (a few are 32-bit)
- Decoding: mask the instruction word, compare against known patterns
- **Table ordering matters:** narrower masks must appear first
  - `LDD Y+q` (mask `0xD208`) before `LD Y` (mask `0xFE0F`)
  - Otherwise `LDD` with displacement zero incorrectly matches `LD`
- Linear scan: 131 entries, fixed stride, no branches in the mask test

---

### SREG Flags

- Every arithmetic/logic instruction computes up to 6 flags from its result
- Getting flags wrong → branches go the wrong way

| Flag | Name | Set when… |
|---|---|---|
| **C** | Carry | Bit 7 produced a carry/borrow |
| **Z** | Zero | Result == 0 |
| **N** | Negative | Bit 7 of result == 1 |
| **V** | Overflow | Two's complement overflow (sign change on same-sign operands) |
| **S** | Sign | Always N ⊕ V — correct sign even after overflow |
| **H** | Half-carry | Carry from bit 3 → bit 4 (enables multi-byte arithmetic) |

---

### Challenge: Making `delay()` Work

- Arduino's `delay(ms)` busy-loops reading a `millis()` counter
- Loop speed = CPU clock speed → same `delay(1000)` runs vastly faster on a laptop than real hardware
- Solution: pace the emulator to match the chip's actual 16 MHz clock

### Wall-Clock Synchronization

- Track `cycle_count` across all instructions, pace against real time
- 16 MHz → each cycle = 62.5 ns
- After every instruction: compute expected wall-clock position, sleep if ahead

- **No busy-waiting** — `sleep_until()` between instructions when ahead of schedule
- **100µs sleep threshold** — avoids syscall overhead dominating short bursts
- Trades a few µs of drift for orders of magnitude less CPU usage
- Result: `delay(1000)` takes 1 second ± negligible drift, not 1 millisecond

---

<!-- _class: lead -->

# Questions?

### Repository
`github.com/cohenrus/atmega328p-emulator`

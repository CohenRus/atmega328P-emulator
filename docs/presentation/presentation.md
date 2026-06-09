---
marp: true
theme: uncover
paginate: true
---
<style>
section { font-size: 1.3em; text-align: left; }
code   { font-size: 0.9em; }
table  { font-size: 0.9em; }
</style>

## **ATmega328P Emulator**
Building an AVR Emulator in C++20


Repository:
`github.com/cohenrus/atmega328p-emulator`

---

### What?
An emulator is software that pretends to be hardware.

Target chip: ATmega328P 
- Microcontroller inside the Arduino Uno R3
- 16 MHz, 8-bit AVR Harvard architecture
- 32 KB flash, 2 KB SRAM, 131 distinct instructions

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
     │       executor.cpp           │
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

| Component | What it holds |
|---|---|
| **32 registers** | Working data |
| **Program Counter** | Byte address into flash |
| **Stack Pointer**| Grows downward from top of SRAM |
| **SREG**| Status register flags |
| **Flash** | Program memory |
| **SRAM** | Data space |
| **EEPROM** | Non-volatile storage |
| **Cycle count** | Total elapsed CPU cycles |

---

### Unified Memory Map

The ATmega328P uses a single 16-bit address space for everything — registers, I/O, and SRAM all share the same bus.

```
Data Space (0x0000 → 0x08FF):
┌──────────────────┐
│  32 GP Registers │ 0x0000
│                  │  R0–R31
├──────────────────┤
│  64 I/O Registers│ 0x0020
│                  │  Timer0, UART, PORTB/D, etc. — IN/OUT instructions
│ 160 Ext I/O Regs │ 0x0060
│                  │  Additional peripheral registers (LD/ST access)
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
- Linear scan: 131 entries, fixed stride, no branches in the mask test

---

### SREG Flags

- Every arithmetic/logic instruction computes up to 6 flags from its result

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

---
### What's Not Yet Implemented

**Major peripherals:**

- **GPIO** — PORTB/C/D registers exist in the memory map but pin I/O is not wired; no `digitalWrite`, no `pinMode`
- **ADC** — analog-to-digital converter not started
- **SPI, I2C/TWI** — serial protocols not implemented
- **PWM pin output** — Timer0 WGM modes compute flags correctly but don't drive output pins
  
---

# Questions?

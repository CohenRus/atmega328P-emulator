---
marp: true
theme: uncover
class:
  - lead
  - invert
paginate: true
---
<style>
section { font-size: 1.5em; }
code   { font-size: 1.2em; }
table  { font-size: 1.2em; }
</style>

<!-- _class: lead invert -->

# **ATmega328P Emulator**
### Building an AVR Emulator in C++20

**Cohen Russell**

---

<!-- _class: default -->

### What is This Project?

A cycle-compatible, instruction-complete emulator for the ATmega328P — the microcontroller at the heart of the Arduino Uno R3.

- **131 instructions** — all arithmetic, logic, branch, bit-manipulation, and data-transfer ops
- **Peripheral emulation** — Timer0 (prescaler, CTC, overflow interrupts), UART I/O
- **Real-time sync** — wall-clock pacing so `delay(1000)` actually takes one second

---

### System Overview

```
┌──────────────┐     ┌────────────┐     ┌───────────────┐
│  ELF binary  │ ──▶ │  Loader    │ ──▶ │  Flash (32KB) │
│ (.text/.data)│     │ loader.cpp │     │  state.flash[] │
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

### Instruction Pipeline

| Step | What happens |
|---|---|
| **1. Fetch** | Read 16-bit word from `flash[pc]` (little-endian) |
| **2. Decode** | Walk opcode table: `(instr & mask) == code` |
| **3. Extract** | Call format-specific operand decoder (17 formats) |
| **4. Execute** | Dispatch via `switch(op.op)` — 76 instruction bodies |
| **5. Advance** | `pc += words * 2` or set by branch/jump/call |

**Each iteration also:** polls UART, services interrupts, advances Timer0, syncs wall-clock.

---

### Decoding: Opcode Table + Operand Extractors

```cpp
// 131 entries — linear search through a mask/match table
static const Opcode OPCODE_TABLE[] = {
    { 0xFC00, 0x1C00, AvrOp::ADC, AvrFmt::Rd_Rr, 1, 1, "ADC" },
    { 0xFC00, 0x0C00, AvrOp::ADD, AvrFmt::Rd_Rr, 1, 1, "ADD" },
    // ...
};
```

**Operand extraction** — 17 format-specific decoders:

```
ADD Rd, Rr    →  0000 11rd dddd rrrr
                 d = (instr >> 4) & 0x1F
                 r = ((instr >> 5) & 0x10) | (instr & 0x0F)

LDI Rd, K     →  1110 KKKK dddd KKKK
                 d = 16 + ((instr >> 4) & 0x0F)
                 k = ((instr >> 4) & 0xF0) | (instr & 0x0F)
```

---

### Challenge: Making `delay()` Work

**The problem:** Arduino's `delay(ms)` busy-loops reading a `millis()` counter.
On real hardware, Timer0 overflows every ~1ms and an ISR increments `timer0_millis`.

**But in the emulator** — compiled firmware links `__bad_interrupt` as the Timer0
overflow handler (an infinite loop). There is no real ISR to update the counter.

---

### Solution: Emulate the ISR from Outside

**Two-tier approach:**

1. **Loader** scans the ELF symbol table at startup for `timer0_millis`, records its SRAM address

2. **Executor** checks at init: "does the TIMER0_OVF vector point to `__bad_interrupt`?"
   If yes → **bypass the ISR entirely** and directly increment the counter in SRAM

```cpp
// In the main loop, on each overflow:
uint32_t m = read_sram(timer0_millis_addr);  // 4-byte counter
m++;
write_sram(timer0_millis_addr, m);           // firmware reads this
```

The emulator becomes the ISR.

---

### Wall-Clock Synchronization

Track a `cycle_count` and pace execution against real time:

```cpp
auto target = wall_start + μs(cycle_count * 1'000'000 / 16'000'000);
auto ahead   = target - steady_clock::now();

if (ahead > 100μs) {          // threshold avoids syscall jitter
    this_thread::sleep_until(target);
}
```

- **No busy-waiting** — sleeps between instructions when ahead of schedule
- **100µs threshold** — trades a few µs of drift for orders of magnitude less CPU
- Result: `delay(1000)` takes 1 second ± negligible drift, not 1 millisecond

---

### Key Stats

| Metric | Value |
|---|---|
| Instructions implemented | 131 / 131 |
| Opcode formats | 17 |
| Source lines (src/) | ~2,500 |
| Test assertions | ~130 |
| Flash size | 32 KB |
| SRAM | 2 KB |
| EEPROM | 1 KB |

---

### Testing Strategy

- **Catch2** unit test framework
- **Instruction-level tests** — verify every SREG flag for arithmetic, logic, branch ops
- **Peripheral tests** — Timer0 prescaler (/1, /8, /64, /256, /1024), CTC mode, overflow flags
- **Integration tests** — real Arduino sketches compiled with `arduino-cli`, run through the emulator

---

<!-- _class: lead -->

# Questions?

### Repository
`github.com/cohenrus/atmega328p-emulator`

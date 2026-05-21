# AVR Instruction Executor Implementation Plan

> **Source of truth:** Atmel AVR Instruction Set Manual DS40002198B (02/2021)
> **Target:** ATmega328P emulator (AVRe/AVRxm profile)
> **Current state:** 76 executor functions stubbed out in `src/executor.cpp` — all bodies are `{}`

---

## 1. Problem & Outcome

**Current state:** `src/executor.cpp` contains 76 instruction executor function stubs that are all empty. The decoder and dispatch infrastructure is fully wired — `decodeInstruction()` correctly identifies every opcode and `executeInstruction()` routes to the right function with the right operand struct — but the emulator does no actual work because every executor body is `{ }`.

**Desired outcome:** Every AVR instruction produces the correct architectural side effects: register writes, SREG flag updates, memory reads/writes, pointer register adjustments, PC modifications, and stack operations. The emulator can boot and run real ATmega328P firmware compiled from Arduino sketches.

**Success signals:**
- All 76 executor functions contain complete implementations matching the DS40002198 spec
- SREG flags (I, T, H, S, V, N, Z, C) are set/cleared correctly per the Boolean formulas in the datasheet
- Alias instructions (CLR→EOR, TST→AND, LSL→ADD, ROL→ADC, CBR→ANDI~K, etc.) produce identical results to their canonical forms
- Test firmware (`tests/sketch/sketch.ino`) runs correctly through the emulator

---

## 2. Scope

**In scope:**
- All 76 individual instruction executor functions listed in [executor.cpp:180-380](src/executor.cpp:180)
- Full SREG flag computation (I, T, H, S, V, N, Z, C) per the Boolean formulas in §6 of DS40002198
- Address-space routing: register file (R0-R31), I/O space (0x00-0x3F + 0x20-0x5F alias), SRAM (0x0100+), program memory (flash)
- Pointer register operations (X=R27:R26, Y=R29:R28, Z=R31:R30) with pre-decrement, post-increment, and displacement
- Stack Pointer post-decrement on CALL/RCALL/ICALL/PUSH; pre-increment on RET/RETI/POP
- Branch offset as signed 7-bit two's complement applied to PC

**Out of scope (explicit):**
- Extended instructions requiring >16-bit PC: EICALL, EIJMP, ELPM, LAC, LAS, LAT, DES — these are not available on ATmega328P (AVRe)
- AVRrc (reduced core) variants — the emulator targets AVRe/AVRxm
- AVRxt-specific instructions (SPM #2 variant, etc.)
- Cycle-count timing — correctly implementing cycle counts is a separate task; this plan focuses on architectural correctness
- Interrupt controller integration beyond the I flag behavior in RETI (set I) and SEI/CLI
- UART, timer, GPIO peripheral emulation

---

## 3. Requirements

### Product expectations

| # | Requirement | Acceptance criteria |
|---|---|---|
| R1 | Arithmetic instructions (ADD, ADC, SUB, SBC, etc.) produce correct 8-bit results with full SREG flag computation | Compare against AVR simulator trace for all edge cases (overflow, carry, zero, half-carry) |
| R2 | Multiply instructions (MUL, MULS, MULSU, FMUL, FMULS, FMULSU) write 16-bit results to R1:R0 | Verify product split across register pair |
| R3 | ADIW/SBIW operate only on register pairs {24,26,28,30} and affect S,V,N,Z,C correctly | Invalid register pairs are rejected at decode time |
| R4 | Load/store instructions (LD/ST/LDS/STS/LPM/IN/OUT) route to correct address space | SRAM reads return values previously written; flash reads match ELF load |
| R5 | Pointer post-increment/pre-decrement update X/Y/Z correctly | LD Rd, X+ increments X by 1; LD Rd, -X decrements X before loading |
| R6 | Branch instructions (BRBC/BRBS and 16 named aliases) test the correct SREG bit and apply signed 7-bit offset | BREQ branches when Z=1; BRNE when Z=0; BRGE when S=0; etc. |
| R7 | Skip instructions (CPSE/SBIC/SBIS/SBRC/SBRS) skip 1 or 2 words depending on the skipped instruction's size | CPSE with 32-bit instruction following skips 4 bytes |
| R8 | Stack operations (PUSH/POP/CALL/RCALL/ICALL/RET/RETI) use SP correctly with byte storage | PUSH stores low-byte-first; CALL pushes 2-byte return address |
| R9 | Bit manipulation (BSET/BCLR/BLD/BST/CBI/SBI) operate on correct bit positions | BSET 7 enables interrupts; BCLR 0 clears carry |
| R10 | MCU control instructions (NOP/SLEEP/WDR/BREAK/SPM) produce correct architectural side effects | NOP is a true no-op; BREAK acts as NOP when debug is disabled |
| R11 | Alias instructions produce identical results to canonical forms | CLR Rd ≡ EOR Rd,Rd; TST Rd ≡ AND Rd,Rd; LSL Rd ≡ ADD Rd,Rd |
| R12 | No undefined behavior — all 256 possible opcode bit patterns either produce defined results or are rejected at decode | Unknown opcodes return false from executeInstruction |

### Engineering constraints

- **Language:** C++20, no exceptions, no RTTI
- **Dependencies:** None beyond standard library
- **SREG access:** Use helper inline functions or macros for setting SREG flags
- **Arithmetic:** Use `uint16_t` for intermediate results to capture carry/borrow; cast back to `uint8_t` for register writes
- **Memory helpers:** Use `readSRAM(state, addr)`, `writeSRAM(state, addr, value)` helpers

---

## 4. Scale & Constraints

| Dimension | Expected | Peak | Source/Assumption |
|---|---|---|---|
| QPS / throughput | N/A — single-threaded emulation | N/A | CLI tool, no server |
| Data volume | 32 KB flash, 2 KB SRAM, 1 KB EEPROM | Fixed | ATmega328P datasheet |
| Concurrent users | 1 | 1 | Single-user emulator |
| Latency target | Instruction-at-a-time correctness | N/A | Not cycle-accurate timing |
| Growth rate | N/A | N/A | Static target device |

---

## 5. Data Modeling

### Key types

```cpp
// From state.h — the emulator's architectural state
struct AvrState {
    uint8_t  r[32];        // R0-R31 general-purpose registers
    uint16_t pc;           // Program Counter (byte address)
    uint8_t  sreg;         // Status Register (I-T-H-S-V-N-Z-C)
    uint16_t sp;           // Stack Pointer
    uint8_t  flash[32768]; // Program memory
    uint8_t  sram[2048];   // Data SRAM
    uint8_t  eeprom[1024]; // EEPROM
};
```

---

### 7.2 Logic Instructions

#### 7.2.1 AND — Logical AND

- **PDF ref:** §6.4 (page 27)
- **Function:** `executeAND(AvrState& state, OpsRdRr ops)`
- **Operation:** `Rd ← Rd ∧ Rr`
- **Alias for:** TST Rd (when d == r)

**SREG flags:** S, V(0), N, Z

```cpp
void executeAND(AvrState& state, OpsRdRr ops) {
    uint8_t result8 = state.r[ops.d] & state.r[ops.r];
    bool n = (result8 & 0x80) != 0;
    bool z = result8 == 0;

    state.r[ops.d] = result8;
    state.sreg = (state.sreg & 0xE4) | (n << SREG_S) | (n << SREG_N) | (z << SREG_Z);
    // V=0 (bit 3 cleared), S=N xor V = N
}
```

---

#### 7.2.2 ANDI — Logical AND with Immediate

- **PDF ref:** §6.5 (page 28)
- **Function:** `executeANDI(AvrState& state, OpsRdK8 ops)`
- **Operation:** `Rd ← Rd ∧ K`
- **Also serves:** CBR (with ~K applied at decode)

```cpp
void executeANDI(AvrState& state, OpsRdK8 ops) {
    uint8_t result8 = state.r[ops.d] & ops.K;
    bool n = (result8 & 0x80) != 0;
    bool z = result8 == 0;

    state.r[ops.d] = result8;
    state.sreg = (state.sreg & 0xE4) | (n << SREG_S) | (n << SREG_N) | (z << SREG_Z);
}
```

---

#### 7.2.3 COM — One's Complement

- **PDF ref:** §6.44 (page 64)
- **Function:** `executeCOM(AvrState& state, OpsRd ops)`
- **Operation:** `Rd ← 0xFF - Rd`

**SREG flags:** S, V(0), N, Z, C(1)

```cpp
void executeCOM(AvrState& state, OpsRd ops) {
    uint8_t result8 = 0xFF - state.r[ops.d];
    bool n = (result8 & 0x80) != 0;
    bool z = result8 == 0;

    state.r[ops.d] = result8;
    state.sreg = (state.sreg & 0xE0) | (n << SREG_S) | (n << SREG_N)
               | (z << SREG_Z) | (1 << SREG_C);
}
```

---

#### 7.2.4 EOR — Exclusive OR

- **PDF ref:** §6.54 (page 75)
- **Function:** `executeEOR(AvrState& state, OpsRdRr ops)`
- **Operation:** `Rd ← Rd ⊕ Rr`
- **Alias for:** CLR Rd (when d == r)

```cpp
void executeEOR(AvrState& state, OpsRdRr ops) {
    uint8_t result8 = state.r[ops.d] ^ state.r[ops.r];
    bool n = (result8 & 0x80) != 0;
    bool z = result8 == 0;

    state.r[ops.d] = result8;
    state.sreg = (state.sreg & 0xE4) | (n << SREG_S) | (n << SREG_N) | (z << SREG_Z);
}
```

---

#### 7.2.5 OR — Logical OR

- **PDF ref:** §6.82 (page 105)
- **Function:** `executeOR(AvrState& state, OpsRdRr ops)`
- **Operation:** `Rd ← Rd ∨ Rr`

```cpp
void executeOR(AvrState& state, OpsRdRr ops) {
    uint8_t result8 = state.r[ops.d] | state.r[ops.r];
    bool n = (result8 & 0x80) != 0;
    bool z = result8 == 0;

    state.r[ops.d] = result8;
    state.sreg = (state.sreg & 0xE4) | (n << SREG_S) | (n << SREG_N) | (z << SREG_Z);
}
```

---

#### 7.2.6 ORI — Logical OR with Immediate

- **PDF ref:** §6.83 (page 106)
- **Function:** `executeORI(AvrState& state, OpsRdK8 ops)`
- **Operation:** `Rd ← Rd ∨ K`
- **Also serves:** SBR (same opcode)

```cpp
void executeORI(AvrState& state, OpsRdK8 ops) {
    uint8_t result8 = state.r[ops.d] | ops.K;
    bool n = (result8 & 0x80) != 0;
    bool z = result8 == 0;

    state.r[ops.d] = result8;
    state.sreg = (state.sreg & 0xE4) | (n << SREG_S) | (n << SREG_N) | (z << SREG_Z);
}
```

---

#### 7.2.7 SER — Set all Bits in Register

- **PDF ref:** §6.106 (page 128)
- **Function:** `executeSER(AvrState& state, OpsRdK8 ops)`
- **Operation:** `Rd ← 0xFF`
- **Note:** Encoded as LDI Rd,0xFF for 16 ≤ d ≤ 31. SER does not affect SREG.

```cpp
void executeSER(AvrState& state, OpsRdK8 ops) {
    state.r[ops.d] = 0xFF;
}
```

---

#### 7.2.8 SWAP — Swap Nibbles

- **PDF ref:** §6.121 (page 145)
- **Function:** `executeSWAP(AvrState& state, OpsRd ops)`
- **Operation:** `Rd(7:4) ↔ Rd(3:0)`

**SREG flags:** None affected

```cpp
void executeSWAP(AvrState& state, OpsRd ops) {
    uint8_t rd = state.r[ops.d];
    state.r[ops.d] = (rd << 4) | (rd >> 4);
}
```

---

### 7.3 Shift / Rotate Instructions

#### 7.3.1 LSR — Logical Shift Right

- **PDF ref:** §6.74 (page 97)
- **Function:** `executeLSR(AvrState& state, OpsRd ops)`
- **Operation:** Shift all bits right; bit 7 ← 0; bit 0 → C

**SREG flags:** S, V, N, Z, C

```cpp
void executeLSR(AvrState& state, OpsRd ops) {
    uint8_t rd = state.r[ops.d];
    bool c = rd & 0x01;
    uint8_t result8 = rd >> 1;
    bool n = false;  // MSB always 0
    bool z = result8 == 0;
    bool v = n ^ c;
    bool s = n ^ v;

    state.r[ops.d] = result8;
    state.sreg = (state.sreg & 0xE0) | (s << SREG_S) | (v << SREG_V)
               | (z << SREG_Z) | (c << SREG_C);
}
```

---

#### 7.3.2 ROR — Rotate Right through Carry

- **PDF ref:** §6.92 (page 115)
- **Function:** `executeROR(AvrState& state, OpsRd ops)`
- **Operation:** Shift all bits right; C → bit 7; bit 0 → C

**SREG flags:** S, V, N, Z, C

```cpp
void executeROR(AvrState& state, OpsRd ops) {
    uint8_t rd = state.r[ops.d];
    bool oldC = (state.sreg >> SREG_C) & 1;
    bool newC = rd & 0x01;
    uint8_t result8 = (rd >> 1) | (oldC ? 0x80 : 0x00);
    bool n = (result8 & 0x80) != 0;
    bool z = result8 == 0;
    bool v = n ^ newC;
    bool s = n ^ v;

    state.r[ops.d] = result8;
    state.sreg = (state.sreg & 0xE0) | (s << SREG_S) | (v << SREG_V)
               | (n << SREG_N) | (z << SREG_Z) | (newC << SREG_C);
}
```

**Note:** LSL and ROL are handled as aliases by the dispatch table — LSL dispatches to `executeADD` with `d==r`, and ROL dispatches to `executeADC` with `d==r`. No separate `executeLSL` or `executeROL` bodies are needed.

---

### 7.4 Compare Instructions

#### 7.4.1 CP — Compare

- **PDF ref:** §6.45 (page 65)
- **Function:** `executeCP(AvrState& state, OpsRdRr ops)`
- **Operation:** `Rd - Rr` (result NOT stored)

**SREG flags:** H, S, V, N, Z, C

```cpp
void executeCP(AvrState& state, OpsRdRr ops) {
    uint8_t rd = state.r[ops.d];
    uint8_t rr = state.r[ops.r];
    uint16_t result16 = (uint16_t)rd - (uint16_t)rr;
    uint8_t result8 = (uint8_t)result16;

    bool h = (rd & 0x0F) < (rr & 0x0F);
    bool v = ((rd ^ rr) & (rd ^ result8) & 0x80) != 0;
    bool n = (result8 & 0x80) != 0;
    bool z = result8 == 0;
    bool c = result16 > 0xFF;
    bool s = n ^ v;

    state.sreg = (state.sreg & 0xC0) | (s << SREG_S) | (v << SREG_V)
               | (n << SREG_N) | (z << SREG_Z) | (c << SREG_C) | (h << SREG_H);
}
```

---

#### 7.4.2 CPC — Compare with Carry

- **PDF ref:** §6.46 (page 66)
- **Function:** `executeCPC(AvrState& state, OpsRdRr ops)`
- **Operation:** `Rd - Rr - C` (result NOT stored)
- **Note:** Z flag preserved if result=0; cleared otherwise (multi-byte compare support)

```cpp
void executeCPC(AvrState& state, OpsRdRr ops) {
    uint8_t rd = state.r[ops.d];
    uint8_t rr = state.r[ops.r];
    uint8_t ci = (state.sreg >> SREG_C) & 1;
    uint16_t result16 = (uint16_t)rd - (uint16_t)rr - ci;
    uint8_t result8 = (uint8_t)result16;

    bool h = ((rd & 0x0F) < ((rr & 0x0F) + ci));
    bool v = ((rd ^ rr) & (rd ^ result8) & 0x80) != 0;
    bool n = (result8 & 0x80) != 0;
    bool z = (result8 == 0) ? ((state.sreg >> SREG_Z) & 1) : false;
    bool c = result16 > 0xFF;
    bool s = n ^ v;

    state.sreg = (state.sreg & 0xC0) | (s << SREG_S) | (v << SREG_V)
               | (n << SREG_N) | (z << SREG_Z) | (c << SREG_C) | (h << SREG_H);
}
```

---

#### 7.4.3 CPI — Compare with Immediate

- **PDF ref:** §6.47 (page 67)
- **Function:** `executeCPI(AvrState& state, OpsRdK8 ops)`
- **Operation:** `Rd - K` (result NOT stored)
- **Constraints:** 16 ≤ d ≤ 31

```cpp
void executeCPI(AvrState& state, OpsRdK8 ops) {
    uint8_t rd = state.r[ops.d];
    uint16_t result16 = (uint16_t)rd - (uint16_t)ops.K;
    uint8_t result8 = (uint8_t)result16;

    bool h = (rd & 0x0F) < (ops.K & 0x0F);
    bool v = ((rd ^ ops.K) & (rd ^ result8) & 0x80) != 0;
    bool n = (result8 & 0x80) != 0;
    bool z = result8 == 0;
    bool c = result16 > 0xFF;
    bool s = n ^ v;

    state.sreg = (state.sreg & 0xC0) | (s << SREG_S) | (v << SREG_V)
               | (n << SREG_N) | (z << SREG_Z) | (c << SREG_C) | (h << SREG_H);
}
```

---

#### 7.4.4 CPSE — Compare Skip if Equal

- **PDF ref:** §6.48 (page 68)
- **Function:** `executeCPSE(AvrState& state, OpsRdRr ops)`
- **Operation:** If Rd == Rr, skip next instruction (advance PC by next instruction's size)

**SREG flags:** None affected

```cpp
void executeCPSE(AvrState& state, OpsRdRr ops) {
    if (state.r[ops.d] == state.r[ops.r]) {
        uint16_t nextInstr = state.flash[state.pc] | (state.flash[state.pc + 1] << 8);
        Opcode nextOp = decodeInstruction(nextInstr);
        state.pc += nextOp.words * 2;
    }
}
```

---

### 7.5 Data Transfer Instructions

#### 7.5.1 MOV — Copy Register

- **PDF ref:** §6.75 (page 98)
- **Function:** `executeMOV(AvrState& state, OpsRdRr ops)`
- **Operation:** `Rd ← Rr`

```cpp
void executeMOV(AvrState& state, OpsRdRr ops) {
    state.r[ops.d] = state.r[ops.r];
}
```

---

#### 7.5.2 MOVW — Copy Register Word

- **PDF ref:** §6.76 (page 99)
- **Function:** `executeMOVW(AvrState& state, OpsRd06Rr06 ops)`
- **Operation:** `R[d+1]:Rd ← R[r+1]:Rr`
- **Note:** Raw nibbles from decode; multiply by 2 for even register indices

```cpp
void executeMOVW(AvrState& state, OpsRd06Rr06 ops) {
    uint8_t d = ops.d * 2;
    uint8_t r = ops.r * 2;
    state.r[d]     = state.r[r];
    state.r[d + 1] = state.r[r + 1];
}
```

---

#### 7.5.3 LDI — Load Immediate

- **PDF ref:** §6.69 (page 92)
- **Function:** `executeLDI(AvrState& state, OpsRdK8 ops)`
- **Operation:** `Rd ← K`
- **Constraints:** 16 ≤ d ≤ 31

```cpp
void executeLDI(AvrState& state, OpsRdK8 ops) {
    state.r[ops.d] = ops.K;
}
```

---

#### 7.5.4 LD_X — Load Indirect using X

- **PDF ref:** §6.66 (page 87)
- **Function:** `executeLD_X(AvrState& state, OpsLdSt ops)`
- **Operation:** `Rd ← DS(X)` with optional post-increment (mode=1) or pre-decrement (mode=2)

```cpp
void executeLD_X(AvrState& state, OpsLdSt ops) {
    uint16_t X = ((uint16_t)state.r[27] << 8) | state.r[26];

    if (ops.mode == 2) {  // Pre-decrement: LD Rd, -X
        X--;
        state.r[26] = (uint8_t)(X & 0xFF);
        state.r[27] = (uint8_t)(X >> 8);
    }

    state.r[ops.d] = readSRAM(state, X);

    if (ops.mode == 1) {  // Post-increment: LD Rd, X+
        X++;
        state.r[26] = (uint8_t)(X & 0xFF);
        state.r[27] = (uint8_t)(X >> 8);
    }
}
```

---

#### 7.5.5 LD_Y — Load Indirect using Y

- **PDF ref:** §6.67 (page 89)
- **Function:** `executeLD_Y(AvrState& state, OpsLdSt ops)`

```cpp
void executeLD_Y(AvrState& state, OpsLdSt ops) {
    uint16_t Y = ((uint16_t)state.r[29] << 8) | state.r[28];

    if (ops.mode == 2) {  // Pre-decrement
        Y--;
        state.r[28] = (uint8_t)(Y & 0xFF);
        state.r[29] = (uint8_t)(Y >> 8);
    }

    state.r[ops.d] = readSRAM(state, Y);

    if (ops.mode == 1) {  // Post-increment
        Y++;
        state.r[28] = (uint8_t)(Y & 0xFF);
        state.r[29] = (uint8_t)(Y >> 8);
    }
}
```

---

#### 7.5.6 LDD_Y — Load Indirect with Displacement using Y

- **PDF ref:** §6.67(iv) (page 90)
- **Function:** `executeLDD_Y(AvrState& state, OpsLdd ops)`
- **Operation:** `Rd ← DS(Y + q)` — Y unchanged

```cpp
void executeLDD_Y(AvrState& state, OpsLdd ops) {
    uint16_t Y = ((uint16_t)state.r[29] << 8) | state.r[28];
    state.r[ops.d] = readSRAM(state, Y + ops.q);
}
```

---

#### 7.5.7 LD_Z — Load Indirect using Z

- **PDF ref:** §6.68 (page 90)
- **Function:** `executeLD_Z(AvrState& state, OpsLdSt ops)`

```cpp
void executeLD_Z(AvrState& state, OpsLdSt ops) {
    uint16_t Z = ((uint16_t)state.r[31] << 8) | state.r[30];

    if (ops.mode == 2) {  // Pre-decrement
        Z--;
        state.r[30] = (uint8_t)(Z & 0xFF);
        state.r[31] = (uint8_t)(Z >> 8);
    }

    state.r[ops.d] = readSRAM(state, Z);

    if (ops.mode == 1) {  // Post-increment
        Z++;
        state.r[30] = (uint8_t)(Z & 0xFF);
        state.r[31] = (uint8_t)(Z >> 8);
    }
}
```

---

#### 7.5.8 LDD_Z — Load Indirect with Displacement using Z

- **PDF ref:** §6.68(iv) (page 91)
- **Function:** `executeLDD_Z(AvrState& state, OpsLdd ops)`
- **Operation:** `Rd ← DS(Z + q)`

```cpp
void executeLDD_Z(AvrState& state, OpsLdd ops) {
    uint16_t Z = ((uint16_t)state.r[31] << 8) | state.r[30];
    state.r[ops.d] = readSRAM(state, Z + ops.q);
}
```

---

#### 7.5.9 LDS — Load Direct from Data Space

- **PDF ref:** §6.70 (page 93)
- **Function:** `executeLDS(AvrState& state, OpsLdsSts ops)`
- **Operation:** `Rd ← DS(k)` — 16-bit address k from second word

```cpp
void executeLDS(AvrState& state, OpsLdsSts ops) {
    state.r[ops.d] = readSRAM(state, ops.k);
}
```

---

#### 7.5.10 LPM — Load Program Memory

- **PDF ref:** §6.72 (page 95)
- **Function:** `executeLPM(AvrState& state, OpsRd ops, uint8_t mode)`
- **Operation:** `Rd ← PS(Z)` — load byte from flash at Z
- **Modes:** mode=0 (Z unchanged), mode=1 (Z post-incremented)

```cpp
void executeLPM(AvrState& state, OpsRd ops, uint8_t mode) {
    uint16_t Z = ((uint16_t)state.r[31] << 8) | state.r[30];
    state.r[ops.d] = state.flash[Z];
    if (mode == 1) {
        Z++;
        state.r[30] = (uint8_t)(Z & 0xFF);
        state.r[31] = (uint8_t)(Z >> 8);
    }
}
```

---

#### 7.5.11 IN — Load I/O Location to Register

- **PDF ref:** §6.60 (page 82)
- **Function:** `executeIN(AvrState& state, OpsRdIO ops)`
- **Operation:** `Rd ← I/O(A)` — A is 6-bit I/O address (0–63)
- **Note:** I/O space 0x00–0x3F maps to SRAM 0x20–0x5F

```cpp
void executeIN(AvrState& state, OpsRdIO ops) {
    state.r[ops.d] = state.sram[ops.A + 0x20];
}
```

---

#### 7.5.12 OUT — Store Register to I/O Location

- **PDF ref:** §6.84 (page 107)
- **Function:** `executeOUT(AvrState& state, OpsIORr ops)`
- **Operation:** `I/O(A) ← Rr`

```cpp
void executeOUT(AvrState& state, OpsIORr ops) {
    state.sram[ops.A + 0x20] = state.r[ops.r];
}
```

---

#### 7.5.13 POP — Pop Register from Stack

- **PDF ref:** §6.85 (page 108)
- **Function:** `executePOP(AvrState& state, OpsRd ops)`
- **Operation:** `Rd ← STACK` — SP post-incremented

```cpp
void executePOP(AvrState& state, OpsRd ops) {
    state.r[ops.d] = state.sram[state.sp];
    state.sp++;
}
```

---

#### 7.5.14 PUSH — Push Register on Stack

- **PDF ref:** §6.86 (page 109)
- **Function:** `executePUSH(AvrState& state, OpsRd ops)`
- **Operation:** `STACK ← Rr` — SP pre-decremented

```cpp
void executePUSH(AvrState& state, OpsRd ops) {
    state.sp--;
    state.sram[state.sp] = state.r[ops.d];
}
```

---

#### 7.5.15 ST_X — Store Indirect using X

- **PDF ref:** §6.114 (page 136)
- **Function:** `executeST_X(AvrState& state, OpsLdSt ops)`
- **Operation:** `DS(X) ← Rr` with optional post-increment (mode=1) or pre-decrement (mode=2)
- **Note:** ops.d is the source register (Rr) for ST-family instructions

```cpp
void executeST_X(AvrState& state, OpsLdSt ops) {
    uint16_t X = ((uint16_t)state.r[27] << 8) | state.r[26];

    if (ops.mode == 2) {  // ST -X, Rr
        X--;
        state.r[26] = (uint8_t)(X & 0xFF);
        state.r[27] = (uint8_t)(X >> 8);
    }

    writeSRAM(state, X, state.r[ops.d]);

    if (ops.mode == 1) {  // ST X+, Rr
        X++;
        state.r[26] = (uint8_t)(X & 0xFF);
        state.r[27] = (uint8_t)(X >> 8);
    }
}
```

---

#### 7.5.16 ST_Y — Store Indirect using Y

- **PDF ref:** §6.115 (page 138)
- **Function:** `executeST_Y(AvrState& state, OpsLdSt ops)`

```cpp
void executeST_Y(AvrState& state, OpsLdSt ops) {
    uint16_t Y = ((uint16_t)state.r[29] << 8) | state.r[28];

    if (ops.mode == 2) {
        Y--;
        state.r[28] = (uint8_t)(Y & 0xFF);
        state.r[29] = (uint8_t)(Y >> 8);
    }

    writeSRAM(state, Y, state.r[ops.d]);

    if (ops.mode == 1) {
        Y++;
        state.r[28] = (uint8_t)(Y & 0xFF);
        state.r[29] = (uint8_t)(Y >> 8);
    }
}
```

---

#### 7.5.17 STD_Y — Store Indirect with Displacement using Y

- **PDF ref:** §6.115(iv) (page 139)
- **Function:** `executeSTD_Y(AvrState& state, OpsLdd ops)`
- **Operation:** `DS(Y + q) ← Rr`

```cpp
void executeSTD_Y(AvrState& state, OpsLdd ops) {
    uint16_t Y = ((uint16_t)state.r[29] << 8) | state.r[28];
    writeSRAM(state, Y + ops.q, state.r[ops.d]);
}
```

---

#### 7.5.18 ST_Z — Store Indirect using Z

- **PDF ref:** §6.116 (page 140)
- **Function:** `executeST_Z(AvrState& state, OpsLdSt ops)`

```cpp
void executeST_Z(AvrState& state, OpsLdSt ops) {
    uint16_t Z = ((uint16_t)state.r[31] << 8) | state.r[30];

    if (ops.mode == 2) {
        Z--;
        state.r[30] = (uint8_t)(Z & 0xFF);
        state.r[31] = (uint8_t)(Z >> 8);
    }

    writeSRAM(state, Z, state.r[ops.d]);

    if (ops.mode == 1) {
        Z++;
        state.r[30] = (uint8_t)(Z & 0xFF);
        state.r[31] = (uint8_t)(Z >> 8);
    }
}
```

---

#### 7.5.19 STD_Z — Store Indirect with Displacement using Z

- **PDF ref:** §6.116(iv) (page 141)
- **Function:** `executeSTD_Z(AvrState& state, OpsLdd ops)`

```cpp
void executeSTD_Z(AvrState& state, OpsLdd ops) {
    uint16_t Z = ((uint16_t)state.r[31] << 8) | state.r[30];
    writeSRAM(state, Z + ops.q, state.r[ops.d]);
}
```

---

#### 7.5.20 STS — Store Direct to Data Space

- **PDF ref:** §6.117 (page 141)
- **Function:** `executeSTS(AvrState& state, OpsLdsSts ops)`
- **Operation:** `DS(k) ← Rr`

```cpp
void executeSTS(AvrState& state, OpsLdsSts ops) {
    writeSRAM(state, ops.k, state.r[ops.d]);
}
```

---

### 7.6 Branch / Jump / Call Instructions

#### 7.6.1 BRBC — Branch if Bit in SREG is Cleared

- **PDF ref:** §6.9 (page 32)
- **Function:** `executeBRBC(AvrState& state, OpsK7 ops)`
- **Operation:** `If SREG(s) == 0 then PC ← PC + k + 1`
- **Note:** All named branch-on-clear aliases (BRCC, BRNE, BRGE, BRHC, BRID, BRPL, BRSH, BRTC, BRVC) route here. k is signed 7-bit.

```cpp
void executeBRBC(AvrState& state, OpsK7 ops) {
    if (!((state.sreg >> ops.s) & 1)) {
        int16_t offset = ops.k;
        if (offset & 0x40) offset |= 0xFF80;  // sign-extend 7-bit
        state.pc += offset;  // PC is already past instruction; k is relative
    }
}
```

---

#### 7.6.2 BRBS — Branch if Bit in SREG is Set

- **PDF ref:** §6.10 (page 33)
- **Function:** `executeBRBS(AvrState& state, OpsK7 ops)`
- **Operation:** `If SREG(s) == 1 then PC ← PC + k + 1`
- **Note:** All named branch-on-set aliases (BRCS, BREQ, BRHS, BRIE, BRLO, BRLT, BRMI, BRTS, BRVS) route here.

```cpp
void executeBRBS(AvrState& state, OpsK7 ops) {
    if ((state.sreg >> ops.s) & 1) {
        int16_t offset = ops.k;
        if (offset & 0x40) offset |= 0xFF80;
        state.pc += offset;
    }
}
```

---

#### 7.6.3 RJMP — Relative Jump

- **PDF ref:** §6.90 (page 113)
- **Function:** `executeRJMP(AvrState& state, OpsK02 ops)`
- **Operation:** `PC ← PC + k + 1`
- **Note:** k is signed 12-bit offset (-2048 to +2047)

```cpp
void executeRJMP(AvrState& state, OpsK02 ops) {
    int16_t offset = ops.k;
    if (offset & 0x0800) offset |= 0xF000;  // sign-extend 12-bit
    state.pc += offset;
}
```

---

#### 7.6.4 JMP — Jump

- **PDF ref:** §6.62 (page 84)
- **Function:** `executeJMP(AvrState& state, OpsK22 ops)`
- **Operation:** `PC ← k` (22-bit absolute word address)

```cpp
void executeJMP(AvrState& state, OpsK22 ops) {
    state.pc = (uint16_t)(ops.k * 2);  // k in words, PC in bytes
}
```

---

#### 7.6.5 IJMP — Indirect Jump

- **PDF ref:** §6.59 (page 81)
- **Function:** `executeIJMP(AvrState& state)`
- **Operation:** `PC ← Z`

```cpp
void executeIJMP(AvrState& state) {
    state.pc = ((uint16_t)state.r[31] << 8) | state.r[30];
}
```

---

#### 7.6.6 RCALL — Relative Call

- **PDF ref:** §6.87 (page 110)
- **Function:** `executeRCALL(AvrState& state, OpsK02 ops)`
- **Operation:** Push return address, then `PC ← PC + k + 1`

```cpp
void executeRCALL(AvrState& state, OpsK02 ops) {
    // Push return address (PC already past this instruction)
    state.sp--;
    state.sram[state.sp] = (uint8_t)(state.pc & 0xFF);
    state.sp--;
    state.sram[state.sp] = (uint8_t)((state.pc >> 8) & 0xFF);

    // Branch
    int16_t offset = ops.k;
    if (offset & 0x0800) offset |= 0xF000;
    state.pc += offset;
}
```

---

#### 7.6.7 CALL — Long Call

- **PDF ref:** §6.32 (page 54)
- **Function:** `executeCALL(AvrState& state, OpsK22 ops)`
- **Operation:** Push return address, then `PC ← k`

```cpp
void executeCALL(AvrState& state, OpsK22 ops) {
    // Push 2-byte return address
    state.sp--;
    state.sram[state.sp] = (uint8_t)(state.pc & 0xFF);
    state.sp--;
    state.sram[state.sp] = (uint8_t)((state.pc >> 8) & 0xFF);

    state.pc = (uint16_t)(ops.k * 2);
}
```

---

#### 7.6.8 ICALL — Indirect Call

- **PDF ref:** §6.58 (page 80)
- **Function:** `executeICALL(AvrState& state)`
- **Operation:** Push return address, then `PC ← Z`

```cpp
void executeICALL(AvrState& state) {
    state.sp--;
    state.sram[state.sp] = (uint8_t)(state.pc & 0xFF);
    state.sp--;
    state.sram[state.sp] = (uint8_t)((state.pc >> 8) & 0xFF);

    state.pc = ((uint16_t)state.r[31] << 8) | state.r[30];
}
```

---

#### 7.6.9 RET — Return from Subroutine

- **PDF ref:** §6.88 (page 111)
- **Function:** `executeRET(AvrState& state)`
- **Operation:** Pop return address from stack → PC

```cpp
void executeRET(AvrState& state) {
    uint8_t hi = state.sram[state.sp];
    state.sp++;
    uint8_t lo = state.sram[state.sp];
    state.sp++;
    state.pc = ((uint16_t)hi << 8) | lo;
}
```

---

#### 7.6.10 RETI — Return from Interrupt

- **PDF ref:** §6.89 (page 112)
- **Function:** `executeRETI(AvrState& state)`
- **Operation:** Pop return address → PC; set I flag

```cpp
void executeRETI(AvrState& state) {
    uint8_t hi = state.sram[state.sp];
    state.sp++;
    uint8_t lo = state.sram[state.sp];
    state.sp++;
    state.pc = ((uint16_t)hi << 8) | lo;
    state.sreg |= (1 << SREG_I);
}
```

---

### 7.7 Skip Instructions

#### 7.7.1 SBIC — Skip if Bit in I/O Register is Cleared

- **PDF ref:** §6.96 (page 119)
- **Function:** `executeSBIC(AvrState& state, OpsIOB ops)`
- **Operation:** If I/O(A,b) == 0, skip next instruction

```cpp
void executeSBIC(AvrState& state, OpsIOB ops) {
    uint8_t ioVal = state.sram[ops.A + 0x20];
    if (!((ioVal >> ops.b) & 1)) {
        uint16_t nextInstr = state.flash[state.pc] | (state.flash[state.pc + 1] << 8);
        Opcode nextOp = decodeInstruction(nextInstr);
        state.pc += nextOp.words * 2;
    }
}
```

---

#### 7.7.2 SBIS — Skip if Bit in I/O Register is Set

- **PDF ref:** §6.97 (page 120)
- **Function:** `executeSBIS(AvrState& state, OpsIOB ops)`

```cpp
void executeSBIS(AvrState& state, OpsIOB ops) {
    uint8_t ioVal = state.sram[ops.A + 0x20];
    if ((ioVal >> ops.b) & 1) {
        uint16_t nextInstr = state.flash[state.pc] | (state.flash[state.pc + 1] << 8);
        Opcode nextOp = decodeInstruction(nextInstr);
        state.pc += nextOp.words * 2;
    }
}
```

---

#### 7.7.3 SBRC — Skip if Bit in Register is Cleared

- **PDF ref:** §6.100 (page 123)
- **Function:** `executeSBRC(AvrState& state, OpsRrB ops)`

```cpp
void executeSBRC(AvrState& state, OpsRrB ops) {
    if (!((state.r[ops.r] >> ops.b) & 1)) {
        uint16_t nextInstr = state.flash[state.pc] | (state.flash[state.pc + 1] << 8);
        Opcode nextOp = decodeInstruction(nextInstr);
        state.pc += nextOp.words * 2;
    }
}
```

---

#### 7.7.4 SBRS — Skip if Bit in Register is Set

- **PDF ref:** §6.101 (page 124)
- **Function:** `executeSBRS(AvrState& state, OpsRrB ops)`

```cpp
void executeSBRS(AvrState& state, OpsRrB ops) {
    if ((state.r[ops.r] >> ops.b) & 1) {
        uint16_t nextInstr = state.flash[state.pc] | (state.flash[state.pc + 1] << 8);
        Opcode nextOp = decodeInstruction(nextInstr);
        state.pc += nextOp.words * 2;
    }
}
```

---

### 7.8 Bit Manipulation Instructions

#### 7.8.1 BSET — Bit Set in SREG

- **PDF ref:** §6.30 (page 52)
- **Function:** `executeBSET(AvrState& state, OpsBOnly ops)`
- **Operation:** `SREG(s) ← 1`
- **Also serves:** SEC, SEZ, SEN, SEV, SES, SEH, SET, SEI

```cpp
void executeBSET(AvrState& state, OpsBOnly ops) {
    state.sreg |= (1 << ops.s);
}
```

---

#### 7.8.2 BCLR — Bit Clear in SREG

- **PDF ref:** §6.7 (page 30)
- **Function:** `executeBCLR(AvrState& state, OpsBOnly ops)`
- **Operation:** `SREG(s) ← 0`
- **Also serves:** CLC, CLZ, CLN, CLV, CLS, CLH, CLT, CLI

```cpp
void executeBCLR(AvrState& state, OpsBOnly ops) {
    state.sreg &= ~(1 << ops.s);
}
```

---

#### 7.8.3 BLD — Bit Load from T to Register

- **PDF ref:** §6.8 (page 31)
- **Function:** `executeBLD(AvrState& state, OpsRdB ops)`
- **Operation:** `Rd(b) ← T`

```cpp
void executeBLD(AvrState& state, OpsRdB ops) {
    bool t = (state.sreg >> SREG_T) & 1;
    if (t)
        state.r[ops.d] |= (1 << ops.b);
    else
        state.r[ops.d] &= ~(1 << ops.b);
}
```

---

#### 7.8.4 BST — Bit Store from Register to T

- **PDF ref:** §6.31 (page 53)
- **Function:** `executeBST(AvrState& state, OpsRdB ops)`
- **Operation:** `T ← Rd(b)`

```cpp
void executeBST(AvrState& state, OpsRdB ops) {
    if ((state.r[ops.d] >> ops.b) & 1)
        state.sreg |= (1 << SREG_T);
    else
        state.sreg &= ~(1 << SREG_T);
}
```

---

#### 7.8.5 CBI — Clear Bit in I/O Register

- **PDF ref:** §6.33 (page 55)
- **Function:** `executeCBI(AvrState& state, OpsIOB ops)`
- **Operation:** `I/O(A,b) ← 0`
- **Constraints:** 0 ≤ A ≤ 31

```cpp
void executeCBI(AvrState& state, OpsIOB ops) {
    state.sram[ops.A + 0x20] &= ~(1 << ops.b);
}
```

---

#### 7.8.6 SBI — Set Bit in I/O Register

- **PDF ref:** §6.95 (page 118)
- **Function:** `executeSBI(AvrState& state, OpsIOB ops)`
- **Operation:** `I/O(A,b) ← 1`
- **Constraints:** 0 ≤ A ≤ 31

```cpp
void executeSBI(AvrState& state, OpsIOB ops) {
    state.sram[ops.A + 0x20] |= (1 << ops.b);
}
```

---

### 7.9 MCU Control Instructions

#### 7.9.1 NOP — No Operation

- **PDF ref:** §6.81 (page 104)
- **Function:** `executeNOP(AvrState& state)`

```cpp
void executeNOP(AvrState& state) {
    // No operation
}
```

---

#### 7.9.2 SLEEP — Sleep

- **PDF ref:** §6.111 (page 132)
- **Function:** `executeSLEEP(AvrState& state)`

```cpp
void executeSLEEP(AvrState& state) {
    // For emulator: treat as NOP (full peripheral integration out of scope)
}
```

---

#### 7.9.3 WDR — Watchdog Reset

- **PDF ref:** §6.123 (page 147)
- **Function:** `executeWDR(AvrState& state)`

```cpp
void executeWDR(AvrState& state) {
    // Reset watchdog timer — for emulator, treat as NOP
}
```

---

#### 7.9.4 BREAK — Break

- **PDF ref:** §6.13 (page 36)
- **Function:** `executeBREAK(AvrState& state)`

```cpp
void executeBREAK(AvrState& state) {
    // Without debug system enabled: acts as NOP
}
```

---

#### 7.9.5 SPM — Store Program Memory

- **PDF ref:** §6.112 (page 133)
- **Function:** `executeSPM(AvrState& state)`
- **Operation:** Store R1:R0 to program memory at Z pointer (self-programming)
- **Note:** Full SPM requires SPMCSR register and page buffer; minimal implementation writes flash directly.

```cpp
void executeSPM(AvrState& state) {
    uint16_t Z = ((uint16_t)state.r[31] << 8) | state.r[30];
    state.flash[Z]     = state.r[0];
    state.flash[Z + 1] = state.r[1];
    // Full SPM with SPMCSR and page buffer is deferred for bootloader support
}
```

---

## 8. Implementation Order

| Step | What | Depends on |
|---|---|---|
| 1 | SREG bit-flag macros (`#define SREG_C 0` etc.) | Nothing |
| 2 | `readSRAM()` / `writeSRAM()` memory helpers | Address space map |
| 3 | Simple no-flag instructions: MOV, MOVW, LDI, NOP, SER, SWAP, IN, OUT, PUSH, POP | Steps 1-2 |
| 4 | Bit manipulation: BSET, BCLR, BLD, BST, CBI, SBI | Step 1 |
| 5 | Branch/Jump/Call: BRBC, BRBS, RJMP, JMP, IJMP, RCALL, CALL, ICALL, RET, RETI | Steps 1-2 |
| 6 | Logic instructions: AND, ANDI, COM, EOR, OR, ORI | Step 1 |
| 7 | Shift/Rotate: ASR, LSR, ROR | Step 1 |
| 8 | Compare: CP, CPC, CPI, CPSE | Step 1 |
| 9 | Simple arithmetic: DEC, INC | Step 1 |
| 10 | Add/Subtract: ADD, ADC, SUB, SBC, SUBI, SBCI | Step 1 |
| 11 | Word arithmetic: ADIW, SBIW | Step 1 |
| 12 | Multiply: MUL, MULS, MULSU, FMUL, FMULS, FMULSU | Step 1 |
| 13 | Load/Store indirect: LD_X, LD_Y, LD_Z, ST_X, ST_Y, ST_Z | Steps 1-2 |
| 14 | Load/Store with displacement: LDD_Y, LDD_Z, STD_Y, STD_Z | Steps 1-2, 13 |
| 15 | Direct load/store: LDS, STS | Steps 1-2 |
| 16 | Program memory: LPM, SPM | Steps 1-2 |
| 17 | Skip: CPSE, SBIC, SBIS, SBRC, SBRS | Steps 1, 13 |
| 18 | MCU control: SLEEP, WDR, BREAK | Nothing |
| 19 | NEG | Step 1 |

---

## 9. Special Considerations

### 9.1 Alias Instruction Handling

Aliases share a single executor function — no duplicate code:

| Alias | Canonical | Dispatch |
|---|---|---|
| LSL Rd | ADD Rd, Rd | `executeADD(state, decodeRdRr(instr))` |
| ROL Rd | ADC Rd, Rd | `executeADC(state, decodeRdRr(instr))` |
| CLR Rd | EOR Rd, Rd | `executeEOR(state, decodeRdRr(instr))` |
| TST Rd | AND Rd, Rd | `executeAND(state, decodeRdRr(instr))` |
| CBR Rd,K | ANDI Rd,~K | `executeANDI(state, decodeRdK8(instr))` |
| SBR Rd,K | ORI Rd,K | `executeORI(state, decodeRdK8(instr))` |
| SEC/SEZ/etc. | BSET s | `executeBSET(state, decodeBOnly(instr))` |
| CLC/CLZ/etc. | BCLR s | `executeBCLR(state, decodeBOnly(instr))` |
| BRCC/BREQ/etc. | BRBC s,k | `executeBRBC(state, decodeK7(instr))` |
| BRCS/BRNE/etc. | BRBS s,k | `executeBRBS(state, decodeK7(instr))` |

### 9.2 Branch Offset Semantics

The fetch loop in `executeProgram()` increments `state.pc` by 2 (or 4) *before* calling `executeInstruction`. The datasheet formula `PC ← PC + k + 1` uses PC as the instruction address. Since our PC is already at `instruction_address + 2`, the actual adjustment is `state.pc += k`.

### 9.3 Half-Carry Computation

- **Addition:** `((rd & 0x0F) + (rr & 0x0F) + carry_in) > 0x0F`
- **Subtraction:** `(rd & 0x0F) < ((rr & 0x0F) + carry_in)`
- **NEG:** `((result8 & 0x08) != 0) || ((rd & 0x08) != 0)`

### 9.4 Overflow Computation

- **Addition (ADD/ADC/INC/ADIW):** `((rd ^ result8) & (rr ^ result8) & 0x80) != 0`
- **Subtraction (SUB/SBC/CP/CPC/CPI/DEC/SBIW):** `((rd ^ rr) & (rd ^ result8) & 0x80) != 0`
- **DEC special:** `rd == 0x80`
- **INC special:** `rd == 0x7F`
- **NEG special:** `rd == 0x80`

### 9.5 CPC Zero Flag

In CPC, the Z flag preserves its previous value if the result is zero, cleared otherwise:

```cpp
bool z = (result8 == 0) ? ((state.sreg >> SREG_Z) & 1) : false;
```

### 9.6 Fix: executeSBI Declaration

The executor.h declares `void executeSBI(...)` but [executor.cpp:375](src/executor.cpp:375) has only a forward declaration with no body:
```cpp
void executeSBI(AvrState& state, OpsIOB ops);
```
This must be replaced with a full function body as part of this plan.

---

## 10. Risks & Open Questions

| # | Type | Description | Mitigation |
|---|---|---|---|
| R1 | Risk | Branch offset may be off-by-1 due to fetch-loop PC management | Validate with BRNE loop test first |
| R2 | Risk | CPSE/skip instructions require `decodeInstruction()` — coupling executor to decoder | Already coupled via include; acceptable |
| R3 | Risk | SPM is simplified; full bootloader needs SPMCSR and page buffer | Document limitation |
| R4 | Risk | I/O register mapping (0x20 offset) assumes ATmega328P layout | Acceptable for this target |
| R5 | Bug | `executeSBI` has a forward declaration in .cpp with no body — linker error | Fix as part of this plan |
| R6 | Dep | Memory helpers (`readSRAM`, `writeSRAM`) are planned but not yet implemented | Implement first per `docs/memory-helpers-plan.md` |

---

## 11. Test Plan

| Layer | Coverage | Method |
|---|---|---|
| Unit | Each arithmetic instruction with known input/output and SREG flags | Hand-crafted test vectors in test firmware |
| Unit | Edge cases: overflow/zero/carry/half-carry at boundary values (0x00, 0x7F, 0x80, 0xFF) | Test firmware with assertions |
| Integration | Test firmware (`tests/sketch/sketch.ino`) runs end-to-end | Compile with arduino-cli, load .elf, execute |
| Integration | Multi-instruction sequences: 16-bit arithmetic, loop counters, stack save/restore | Hand-written test sketches |
| Manual | Bootloader/memory-intensive scenarios (SPM, LPM) | Hand-written test sketches |
| Skip | Interrupt handling (full controller not implemented) | RETI sets I flag; SEI/CLI via BSET/BCLR |
| Skip | Peripheral interaction (UART, timer, GPIO) | Out of scope |

---

## 12. File Changes Summary

| File | Action | Changes |
|---|---|---|
| `src/executor.cpp` | Modify | Fill all 76 function bodies; add SREG macros near top; fix `executeSBI` |
| `src/executor.h` | Verify | All 76 signatures match; `executeSBI` declared correctly |
| `src/state.h` | No change | Already has `AvrState` with all fields |
| `src/decoder.h` | No change | Decode infrastructure is complete |
| `docs/memory-helpers-plan.md` | Prerequisite | Implement `readSRAM()`/`writeSRAM()` before load/store executors |

---

## Appendix A: SREG Bit Definitions

```cpp
// Add near the top of executor.cpp
#define SREG_C  0
#define SREG_Z  1
#define SREG_N  2
#define SREG_V  3
#define SREG_S  4
#define SREG_H  5
#define SREG_T  6
#define SREG_I  7
```

## Appendix B: Memory Address Decoding Helpers

```cpp
uint8_t readSRAM(AvrState& state, uint16_t addr) {
    if (addr < 0x20)        return state.r[addr];         // Register file
    else if (addr < 0x100)  return state.sram[addr];      // I/O + Extended I/O
    else                    return state.sram[addr];      // SRAM
}

void writeSRAM(AvrState& state, uint16_t addr, uint8_t value) {
    if (addr < 0x20)        state.r[addr] = value;
    else if (addr < 0x100)  state.sram[addr] = value;
    else                    state.sram[addr] = value;
}
```

---

*Plan generated from AVR Instruction Set Manual DS40002198B, sections 6.1–6.123. Covers all 76 executor functions in `src/executor.cpp`.*
### SREG bit layout

| Bit | 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0 |
|---|---|---|---|---|---|---|---|---|
| Flag | I | T | H | S | V | N | Z | C |

```cpp
// SREG bit indices
#define SREG_C  0
#define SREG_Z  1
#define SREG_N  2
#define SREG_V  3
#define SREG_S  4
#define SREG_H  5
#define SREG_T  6
#define SREG_I  7
```

### Pointer register composition

```
X = R27:R26  (XH:XL)
Y = R29:R28  (YH:YL)
Z = R31:R30  (ZH:ZL)
```

---

## 6. Approach

### Architecture overview

The emulator follows a fetch-decode-execute loop. Each 16-bit (or 32-bit) instruction word is decoded into an `Opcode` struct by `decodeInstruction()` in `decoder.cpp`. The dispatch function `executeInstruction()` in `executor.cpp` extracts operands via format-specific decode helpers (e.g., `decodeRdRr()`, `decodeRdK8()`) and calls the corresponding `execute*()` function with the decoded operand struct.

Each `execute*()` function:
1. Reads source operands from `state.r[]`, `state.sram[]`, `state.flash[]`, or the operand struct itself
2. Performs the computation (addition, shift, bitwise op, comparison, etc.)
3. Writes the result to the destination register or memory
4. Updates SREG flags per the Boolean formulas in the datasheet
5. Updates PC/SP/pointer registers as specified

### SREG flag update strategy

For most arithmetic/logic instructions, SREG flags are computed from intermediate values *before* truncation to 8 bits. The general pattern:

```cpp
uint8_t rd = state.r[ops.d];
uint8_t rr = state.r[ops.r];
uint16_t result16 = (uint16_t)rd + (uint16_t)rr;  // for ADD
uint8_t result8 = (uint8_t)result16;

// Half-carry: was there a carry from bit 3 to bit 4?
bool h = ((rd & 0x0F) + (rr & 0x0F)) > 0x0F;
// Carry: was there a carry from bit 7?
bool c = result16 > 0xFF;
// Overflow: both operands have same sign but result has different sign
bool v = ((rd ^ result8) & (rr ^ result8) & 0x80) != 0;
// Negative: MSB of result
bool n = (result8 & 0x80) != 0;
// Zero: all bits of result are 0
bool z = result8 == 0;
// Sign: N XOR V
bool s = n ^ v;
```

### Address space routing

Data-space addresses are decoded based on the ATmega328P memory map:
- 0x0000–0x001F: Register file (R0–R31)
- 0x0020–0x005F: I/O registers (64 bytes)
- 0x0060–0x00FF: Extended I/O (160 bytes)
- 0x0100–0x08FF: Internal SRAM (2048 bytes)

Program memory (flash) is addressed at byte granularity.

---

## 7. Implementation Details

Below, every instruction executor function is specified with PDF reference, description, operation, operand struct, SREG flags, implementation pseudocode, and notes.

---

### 7.1 Arithmetic Instructions

#### 7.1.1 ADC — Add with Carry

- **PDF ref:** §6.1 (page 24)
- **Function:** `executeADC(AvrState& state, OpsRdRr ops)`
- **Operation:** `Rd ← Rd + Rr + C`
- **Operands:** `0 ≤ d ≤ 31, 0 ≤ r ≤ 31`

**SREG flags:** H, S, V, N, Z, C — all updated

| Flag | Formula |
|---|---|
| H | Carry from bit 3: `((Rd & 0x0F) + (Rr & 0x0F) + C_in) > 0x0F` |
| S | N ⊕ V |
| V | Two's complement overflow: `((Rd ^ R) & (Rr ^ R) & 0x80) != 0` |
| N | R₇ (MSB of result) |
| Z | Set if result = 0x00 |
| C | Carry from bit 7: `result16 > 0xFF` |

```cpp
void executeADC(AvrState& state, OpsRdRr ops) {
    uint8_t rd = state.r[ops.d];
    uint8_t rr = state.r[ops.r];
    uint8_t ci = (state.sreg >> SREG_C) & 1;
    uint16_t result16 = (uint16_t)rd + (uint16_t)rr + ci;
    uint8_t result8 = (uint8_t)result16;

    bool h = ((rd & 0x0F) + (rr & 0x0F) + ci) > 0x0F;
    bool v = ((rd ^ result8) & (rr ^ result8) & 0x80) != 0;
    bool n = (result8 & 0x80) != 0;
    bool z = result8 == 0;
    bool c = result16 > 0xFF;
    bool s = n ^ v;

    state.r[ops.d] = result8;
    state.sreg = (state.sreg & 0xC0) | (s << SREG_S) | (v << SREG_V)
               | (n << SREG_N) | (z << SREG_Z) | (c << SREG_C) | (h << SREG_H);
}
```

---

#### 7.1.2 ADD — Add without Carry

- **PDF ref:** §6.2 (page 25)
- **Function:** `executeADD(AvrState& state, OpsRdRr ops)`
- **Operation:** `Rd ← Rd + Rr`
- **Alias for:** LSL Rd (when d == r)

**SREG flags:** H, S, V, N, Z, C (same as ADC but without C-in)

```cpp
void executeADD(AvrState& state, OpsRdRr ops) {
    uint8_t rd = state.r[ops.d];
    uint8_t rr = state.r[ops.r];
    uint16_t result16 = (uint16_t)rd + (uint16_t)rr;
    uint8_t result8 = (uint8_t)result16;

    bool h = ((rd & 0x0F) + (rr & 0x0F)) > 0x0F;
    bool v = ((rd ^ result8) & (rr ^ result8) & 0x80) != 0;
    bool n = (result8 & 0x80) != 0;
    bool z = result8 == 0;
    bool c = result16 > 0xFF;
    bool s = n ^ v;

    state.r[ops.d] = result8;
    state.sreg = (state.sreg & 0xC0) | (s << SREG_S) | (v << SREG_V)
               | (n << SREG_N) | (z << SREG_Z) | (c << SREG_C) | (h << SREG_H);
}
```

**Note:** When called as LSL alias, `ops.d == ops.r`, so `rd == rr`, and the operation `Rd ← Rd + Rd` performs a logical shift left (bit 0 cleared, bit 7 → C).

---

#### 7.1.3 ADIW — Add Immediate to Word

- **PDF ref:** §6.3 (page 26)
- **Function:** `executeADIW(AvrState& state, OpsRd06K6 ops)`
- **Operation:** `R[d+1]:Rd ← R[d+1]:Rd + K`
- **Constraints:** d ∈ {24, 26, 28, 30}, 0 ≤ K ≤ 63

**SREG flags:** S, V, N, Z, C

```cpp
void executeADIW(AvrState& state, OpsRd06K6 ops) {
    uint8_t  lo = state.r[ops.d];
    uint8_t  hi = state.r[ops.d + 1];
    uint16_t word = ((uint16_t)hi << 8) | lo;
    uint16_t result16 = word + ops.K;

    bool v = (hi & 0x80) && !(result16 & 0x8000);
    bool n = (result16 & 0x8000) != 0;
    bool z = result16 == 0;
    bool c = !(result16 & 0x8000) && (hi & 0x80);
    bool s = n ^ v;

    state.r[ops.d]     = (uint8_t)(result16 & 0xFF);
    state.r[ops.d + 1] = (uint8_t)(result16 >> 8);
    state.sreg = (state.sreg & 0xE0) | (s << SREG_S) | (v << SREG_V)
               | (n << SREG_N) | (z << SREG_Z) | (c << SREG_C);
}
```

---

#### 7.1.4 ASR — Arithmetic Shift Right

- **PDF ref:** §6.6 (page 29)
- **Function:** `executeASR(AvrState& state, OpsRd ops)`
- **Operation:** Shift all bits right; bit 7 held constant; bit 0 → C

**SREG flags:** S, V, N, Z, C

```cpp
void executeASR(AvrState& state, OpsRd ops) {
    uint8_t rd = state.r[ops.d];
    bool c = rd & 0x01;
    uint8_t result8 = (rd >> 1) | (rd & 0x80);
    bool n = (result8 & 0x80) != 0;
    bool z = result8 == 0;
    bool v = n ^ c;
    bool s = n ^ v;

    state.r[ops.d] = result8;
    state.sreg = (state.sreg & 0xE0) | (s << SREG_S) | (v << SREG_V)
               | (n << SREG_N) | (z << SREG_Z) | (c << SREG_C);
}
```

---

#### 7.1.5 DEC — Decrement

- **PDF ref:** §6.49 (page 69)
- **Function:** `executeDEC(AvrState& state, OpsRd ops)`
- **Operation:** `Rd ← Rd - 1`
- **Note:** C flag is NOT affected

**SREG flags:** S, V, N, Z (C, H unchanged)

```cpp
void executeDEC(AvrState& state, OpsRd ops) {
    uint8_t rd = state.r[ops.d];
    uint8_t result8 = rd - 1;
    bool v = rd == 0x80;
    bool n = (result8 & 0x80) != 0;
    bool z = result8 == 0;
    bool s = n ^ v;

    state.r[ops.d] = result8;
    state.sreg = (state.sreg & 0xE1) | (s << SREG_S) | (v << SREG_V)
               | (n << SREG_N) | (z << SREG_Z);
}
```

---

#### 7.1.6 INC — Increment

- **PDF ref:** §6.61 (page 83)
- **Function:** `executeINC(AvrState& state, OpsRd ops)`
- **Operation:** `Rd ← Rd + 1`
- **Note:** C flag is NOT affected

**SREG flags:** S, V, N, Z (C, H unchanged)

```cpp
void executeINC(AvrState& state, OpsRd ops) {
    uint8_t rd = state.r[ops.d];
    uint8_t result8 = rd + 1;
    bool v = rd == 0x7F;
    bool n = (result8 & 0x80) != 0;
    bool z = result8 == 0;
    bool s = n ^ v;

    state.r[ops.d] = result8;
    state.sreg = (state.sreg & 0xE1) | (s << SREG_S) | (v << SREG_V)
               | (n << SREG_N) | (z << SREG_Z);
}
```

---

#### 7.1.7 MUL — Multiply Unsigned

- **PDF ref:** §6.77 (page 100)
- **Function:** `executeMUL(AvrState& state, OpsRdRr ops)`
- **Operation:** `R1:R0 ← Rd × Rr` (unsigned 8×8→16)

**SREG flags:** Z, C

```cpp
void executeMUL(AvrState& state, OpsRdRr ops) {
    uint16_t result16 = (uint16_t)state.r[ops.d] * (uint16_t)state.r[ops.r];
    state.r[0] = (uint8_t)(result16 & 0xFF);
    state.r[1] = (uint8_t)(result16 >> 8);
    bool c = (result16 & 0x8000) != 0;
    bool z = result16 == 0;
    state.sreg = (state.sreg & 0xFC) | (z << SREG_Z) | (c << SREG_C);
}
```

---

#### 7.1.8 MULS — Multiply Signed

- **PDF ref:** §6.78 (page 101)
- **Function:** `executeMULS(AvrState& state, OpsRd06Rr06 ops)`
- **Operation:** `R1:R0 ← Rd × Rr` (signed 8×8→16)
- **Note:** Raw nibbles from decode; +16 applied inside

```cpp
void executeMULS(AvrState& state, OpsRd06Rr06 ops) {
    int8_t rd = (int8_t)state.r[ops.d + 16];
    int8_t rr = (int8_t)state.r[ops.r + 16];
    int16_t result16 = (int16_t)rd * (int16_t)rr;
    state.r[0] = (uint8_t)((uint16_t)result16 & 0xFF);
    state.r[1] = (uint8_t)(((uint16_t)result16 >> 8) & 0xFF);
    bool c = (result16 & 0x8000) != 0;
    bool z = result16 == 0;
    state.sreg = (state.sreg & 0xFC) | (z << SREG_Z) | (c << SREG_C);
}
```

---

#### 7.1.9 MULSU — Multiply Signed with Unsigned

- **PDF ref:** §6.79 (page 102)
- **Function:** `executeMULSU(AvrState& state, OpsRdRrMpy ops)`
- **Operation:** `R1:R0 ← Rd(signed) × Rr(unsigned)`

```cpp
void executeMULSU(AvrState& state, OpsRdRrMpy ops) {
    int8_t  rd = (int8_t)state.r[ops.d + 16];
    uint8_t rr = state.r[ops.r + 16];
    int16_t result16 = (int16_t)rd * (int16_t)((uint16_t)rr);
    state.r[0] = (uint8_t)((uint16_t)result16 & 0xFF);
    state.r[1] = (uint8_t)(((uint16_t)result16 >> 8) & 0xFF);
    bool c = (result16 & 0x8000) != 0;
    bool z = result16 == 0;
    state.sreg = (state.sreg & 0xFC) | (z << SREG_Z) | (c << SREG_C);
}
```

---

#### 7.1.10 FMUL — Fractional Multiply Unsigned

- **PDF ref:** §6.55 (page 76)
- **Function:** `executeFMUL(AvrState& state, OpsRdRrMpy ops)`
- **Operation:** `R1:R0 ← (Rd × Rr) << 1`

```cpp
void executeFMUL(AvrState& state, OpsRdRrMpy ops) {
    uint16_t product = (uint16_t)state.r[ops.d + 16] * (uint16_t)state.r[ops.r + 16];
    uint16_t result16 = product << 1;
    state.r[0] = (uint8_t)(result16 & 0xFF);
    state.r[1] = (uint8_t)(result16 >> 8);
    bool c = (result16 & 0x8000) != 0;
    bool z = result16 == 0;
    state.sreg = (state.sreg & 0xFC) | (z << SREG_Z) | (c << SREG_C);
}
```

---

#### 7.1.11 FMULS — Fractional Multiply Signed

- **PDF ref:** §6.56 (page 77)
- **Function:** `executeFMULS(AvrState& state, OpsRdRrMpy ops)`
- **Operation:** `R1:R0 ← (Rd × Rr) << 1` (signed)

```cpp
void executeFMULS(AvrState& state, OpsRdRrMpy ops) {
    int8_t rd = (int8_t)state.r[ops.d + 16];
    int8_t rr = (int8_t)state.r[ops.r + 16];
    int16_t product = (int16_t)rd * (int16_t)rr;
    uint16_t result16 = (uint16_t)(product << 1);
    state.r[0] = (uint8_t)(result16 & 0xFF);
    state.r[1] = (uint8_t)(result16 >> 8);
    bool c = (result16 & 0x8000) != 0;
    bool z = result16 == 0;
    state.sreg = (state.sreg & 0xFC) | (z << SREG_Z) | (c << SREG_C);
}
```

---

#### 7.1.12 FMULSU — Fractional Multiply Signed with Unsigned

- **PDF ref:** §6.57 (page 79)
- **Function:** `executeFMULSU(AvrState& state, OpsRdRrMpy ops)`
- **Operation:** `R1:R0 ← (Rd(signed) × Rr(unsigned)) << 1`

```cpp
void executeFMULSU(AvrState& state, OpsRdRrMpy ops) {
    int8_t  rd = (int8_t)state.r[ops.d + 16];
    uint8_t rr = state.r[ops.r + 16];
    int16_t product = (int16_t)rd * (int16_t)((uint16_t)rr);
    uint16_t result16 = (uint16_t)(product << 1);
    state.r[0] = (uint8_t)(result16 & 0xFF);
    state.r[1] = (uint8_t)(result16 >> 8);
    bool c = (result16 & 0x8000) != 0;
    bool z = result16 == 0;
    state.sreg = (state.sreg & 0xFC) | (z << SREG_Z) | (c << SREG_C);
}
```

---

#### 7.1.13 NEG — Two's Complement

- **PDF ref:** §6.80 (page 103)
- **Function:** `executeNEG(AvrState& state, OpsRd ops)`
- **Operation:** `Rd ← 0x00 - Rd`

**SREG flags:** H, S, V, N, Z, C

```cpp
void executeNEG(AvrState& state, OpsRd ops) {
    uint8_t rd = state.r[ops.d];
    uint8_t result8 = (uint8_t)(-(int8_t)rd);
    bool h = ((result8 & 0x08) != 0) || ((rd & 0x08) != 0);
    bool v = rd == 0x80;
    bool n = (result8 & 0x80) != 0;
    bool z = result8 == 0;
    bool c = result8 != 0x00;
    bool s = n ^ v;

    state.r[ops.d] = result8;
    state.sreg = (state.sreg & 0xC0) | (s << SREG_S) | (v << SREG_V)
               | (n << SREG_N) | (z << SREG_Z) | (c << SREG_C) | (h << SREG_H);
}
```

---

#### 7.1.14 SBIW — Subtract Immediate from Word

- **PDF ref:** §6.98 (page 121)
- **Function:** `executeSBIW(AvrState& state, OpsRd06K6 ops)`
- **Operation:** `R[d+1]:Rd ← R[d+1]:Rd - K`
- **Constraints:** d ∈ {24, 26, 28, 30}, 0 ≤ K ≤ 63

```cpp
void executeSBIW(AvrState& state, OpsRd06K6 ops) {
    uint8_t  lo = state.r[ops.d];
    uint8_t  hi = state.r[ops.d + 1];
    uint16_t word = ((uint16_t)hi << 8) | lo;
    uint16_t result16 = word - ops.K;

    bool v = (hi & 0x80) && (result16 & 0x8000);
    bool n = (result16 & 0x8000) != 0;
    bool z = result16 == 0;
    bool c = (result16 & 0x8000) && (hi & 0x80);
    bool s = n ^ v;

    state.r[ops.d]     = (uint8_t)(result16 & 0xFF);
    state.r[ops.d + 1] = (uint8_t)(result16 >> 8);
    state.sreg = (state.sreg & 0xE0) | (s << SREG_S) | (v << SREG_V)
               | (n << SREG_N) | (z << SREG_Z) | (c << SREG_C);
}
```

---

#### 7.1.15 SBC — Subtract with Carry

- **PDF ref:** §6.93 (page 116)
- **Function:** `executeSBC(AvrState& state, OpsRdRr ops)`
- **Operation:** `Rd ← Rd - Rr - C`

```cpp
void executeSBC(AvrState& state, OpsRdRr ops) {
    uint8_t rd = state.r[ops.d];
    uint8_t rr = state.r[ops.r];
    uint8_t ci = (state.sreg >> SREG_C) & 1;
    uint16_t result16 = (uint16_t)rd - (uint16_t)rr - ci;
    uint8_t result8 = (uint8_t)result16;

    bool h = ((rd & 0x0F) < ((rr & 0x0F) + ci));
    bool v = ((rd ^ rr) & (rd ^ result8) & 0x80) != 0;
    bool n = (result8 & 0x80) != 0;
    bool z = result8 == 0;
    bool c = result16 > 0xFF;
    bool s = n ^ v;

    state.r[ops.d] = result8;
    state.sreg = (state.sreg & 0xC0) | (s << SREG_S) | (v << SREG_V)
               | (n << SREG_N) | (z << SREG_Z) | (c << SREG_C) | (h << SREG_H);
}
```

---

#### 7.1.16 SBCI — Subtract Immediate with Carry

- **PDF ref:** §6.94 (page 117)
- **Function:** `executeSBCI(AvrState& state, OpsRdK8 ops)`
- **Operation:** `Rd ← Rd - K - C`
- **Constraints:** 16 ≤ d ≤ 31

```cpp
void executeSBCI(AvrState& state, OpsRdK8 ops) {
    uint8_t rd = state.r[ops.d];
    uint8_t ci = (state.sreg >> SREG_C) & 1;
    uint16_t result16 = (uint16_t)rd - (uint16_t)ops.K - ci;
    uint8_t result8 = (uint8_t)result16;

    bool h = ((rd & 0x0F) < ((ops.K & 0x0F) + ci));
    bool v = ((rd ^ ops.K) & (rd ^ result8) & 0x80) != 0;
    bool n = (result8 & 0x80) != 0;
    bool z = result8 == 0;
    bool c = result16 > 0xFF;
    bool s = n ^ v;

    state.r[ops.d] = result8;
    state.sreg = (state.sreg & 0xC0) | (s << SREG_S) | (v << SREG_V)
               | (n << SREG_N) | (z << SREG_Z) | (c << SREG_C) | (h << SREG_H);
}
```

---

#### 7.1.17 SUB — Subtract without Carry

- **PDF ref:** §6.119 (page 143)
- **Function:** `executeSUB(AvrState& state, OpsRdRr ops)`
- **Operation:** `Rd ← Rd - Rr`

```cpp
void executeSUB(AvrState& state, OpsRdRr ops) {
    uint8_t rd = state.r[ops.d];
    uint8_t rr = state.r[ops.r];
    uint16_t result16 = (uint16_t)rd - (uint16_t)rr;
    uint8_t result8 = (uint8_t)result16;

    bool h = (rd & 0x0F) < (rr & 0x0F);
    bool v = ((rd ^ rr) & (rd ^ result8) & 0x80) != 0;
    bool n = (result8 & 0x80) != 0;
    bool z = result8 == 0;
    bool c = result16 > 0xFF;
    bool s = n ^ v;

    state.r[ops.d] = result8;
    state.sreg = (state.sreg & 0xC0) | (s << SREG_S) | (v << SREG_V)
               | (n << SREG_N) | (z << SREG_Z) | (c << SREG_C) | (h << SREG_H);
}
```

---

#### 7.1.18 SUBI — Subtract Immediate

- **PDF ref:** §6.120 (page 144)
- **Function:** `executeSUBI(AvrState& state, OpsRdK8 ops)`
- **Operation:** `Rd ← Rd - K`
- **Constraints:** 16 ≤ d ≤ 31

```cpp
void executeSUBI(AvrState& state, OpsRdK8 ops) {
    uint8_t rd = state.r[ops.d];
    uint16_t result16 = (uint16_t)rd - (uint16_t)ops.K;
    uint8_t result8 = (uint8_t)result16;

    bool h = (rd & 0x0F) < (ops.K & 0x0F);
    bool v = ((rd ^ ops.K) & (rd ^ result8) & 0x80) != 0;
    bool n = (result8 & 0x80) != 0;
    bool z = result8 == 0;
    bool c = result16 > 0xFF;
    bool s = n ^ v;

    state.r[ops.d] = result8;
    state.sreg = (state.sreg & 0xC0) | (s << SREG_S) | (v << SREG_V)
               | (n << SREG_N) | (z << SREG_Z) | (c << SREG_C) | (h << SREG_H);
}
```

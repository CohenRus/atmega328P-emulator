# ATmega328P Emulator v1 Implementation Plan

**Date:** 2026-06-01
**Status:** Draft

---

## Executive Summary

v1 is the first release capable of running real Arduino Uno sketches end-to-end. Today the emulator decodes and executes all 131 AVR instructions, parses ELF binaries, maps the unified ATmega328P data space (GP regs → I/O → ext I/O → SRAM), and drives a polled UART (TX→stdout, RX←stdin). What's missing: correctness under real-world compiler output, peripherals needed for `delay()`/`millis()`/`digitalWrite()`/`analogRead()`, execution control tooling, tests, CI, and a WASM build target. This plan closes every gap in priority order, with concrete file paths, structs, and acceptance criteria at each phase.

After v1, a user can:
- Compile an Arduino sketch with `arduino-cli` and run it through the emulator
- See `Serial.print()` output on stdout
- Use `delay()` and `millis()` via Timer0
- Toggle pins via PORT/DDR registers
- Trace execution with `--trace`
- Run the emulator in a browser via WASM
- Trust that every instruction passes automated tests

---

## Phase Structure

Each phase declares: goal, files affected, concrete steps, acceptance criteria, dependencies, and approximate scope. Phases are sequenced — each depends on all prior phases.

---

## Phase 1: Fix Decoder & Test Foundation

### Goal
Eliminate all unknown-opcode crashes against real avr-gcc output, and establish a unit-test harness with initial instruction tests.

### Dependencies
None.

### Files Affected

| File | Action |
|---|---|
| `src/decoder.h` | Fix mask gaps in `OPCODE_TABLE[]` |
| `src/decoder.cpp` | Possibly add second-chance decode logic for ambiguous masks |
| `tests/catch2/catch_amalgamated.hpp` | **Create** — drop in Catch2 v3 single-header |
| `tests/CMakeLists.txt` | **Create** — test runner target |
| `tests/test_instructions.cpp` | **Create** — representative-per-format tests |
| `tests/test_decoder.cpp` | **Create** — exhaustive decoder mask validation |
| `CMakeLists.txt` | Add `add_subdirectory(tests)` with conditional |
| `src/executor.cpp:43` | Remove `std::cout << op.mnemonic << std::endl;` debug line |

### Steps

#### 1.1 Decoder Mask Audit

The known crash: opcode `0x9393` at PC `0x01E7` from avr-gcc compiled `Serial.print()` firmware. The current `OPCODE_TABLE[]` in `src/decoder.h` has 58 entries covering 131 enumerated opcodes (many via aliases). The audit process:

1. **Cross-reference the OPCODE_TABLE against the AVR Instruction Set Manual (DS40002198) §6.** Every AVR instruction encoding in the manual has explicit bit patterns. Map each to a `{mask, code, op, fmt}` entry.

2. **Investigate `0x9393` specifically.** Binary: `1001 0011 1001 0011`. Bits [15:12] = `1001` (prefix for LD/ST/MCU-control/bit-manip). Bits [13:12] = `00` (LD/ST group). Bits [9:4] = `100111` (Rd = 19, or Rr = 19). Bits [3:0] = `0011`. 
   - Check against LDS (mask `0xFE0F`, code `0x9000`): `0x9393 & 0xFE0F = 0x9203 ≠ 0x9000`.
   - Check against STS (mask `0xFE0F`, code `0x9200`): `0x9393 & 0xFE0F = 0x9203 ≠ 0x9200`.
   - Check against LD_Z variants: no match.
   - Check against ST_Z variants: no match.
   - Check against LDD/STD q variants: mask `0xD208`; `0x9393 & 0xD208 = 0x9200`; codes are `0x8000`/`0x8008`/`0x8200`/`0x8208` — none match.
   - Check against IN/OUT: `0xF800` masks → bits[15:11] = `10010` vs `10110`/`10111` — no match.
   - **Resolution:** `0x9393` likely corresponds to an encoding not yet in the table: `LDD Rd, Z+q` where q is encoded differently, or a variant the mask is too tight for. Specifically, the LDD Z+q entry has mask `0xD208` and code `0x8000` — but `0x9393` has bit[9]=1 and bit[3]=1 which both fail the mask. The fix is either a wider mask or a new entry. **Required action: step through the AVR manual's LDD/STD encoding table character-by-character, ensuring every valid q/d combination is covered.**

3. **Systematic approach:** Write `tests/test_decoder.cpp` with a fuzzing loop that iterates all 65536 possible 16-bit opcode patterns and checks that each either decodes successfully or is a documented "unused" encoding. Known-undecodable patterns (e.g., `0xFFFF` which is not a valid instruction) should be explicitly listed. Run this against real `.elf` firmware — every opcode in the firmware binary must decode.

4. **Duplicate entry cleanup:** Remove the redundant `{0xFFFF, 0x9408, AvrOp::BSET, …, "SEC"}` and `{0xFFFF, 0x9428, AvrOp::BSET, …, "SEN"}` entries at lines ~340-341 — they shadow the typed `AvrOp::SEC`/`AvrOp::SEN` entries below them.

#### 1.2 Test Framework Setup

1. Download Catch2 v3 amalgamated header and commit to `tests/catch2/catch_amalgamated.hpp`.

2. Create `tests/CMakeLists.txt`:
```cmake
cmake_minimum_required(VERSION 3.20)

# Catch2 header-only, no compilation needed
add_library(Catch2 INTERFACE)
target_include_directories(Catch2 INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/catch2)

add_executable(emulator_test
    test_decoder.cpp
    test_instructions.cpp
    ../src/decoder.cpp
    ../src/error.cpp
    ../src/memory.cpp
    ../src/uart.cpp
    ../src/executor.cpp
    ../src/loader.cpp
)

target_include_directories(emulator_test PRIVATE ../src)
target_link_libraries(emulator_test PRIVATE Catch2)
target_compile_options(emulator_test PRIVATE -Wall -Wextra -Wpedantic)

# Let CTest discover the test executable
include(CTest)
add_test(NAME emulator_test COMMAND emulator_test)
```

3. Edit root `CMakeLists.txt`: add `if(NOT EMSCRIPTEN) add_subdirectory(tests) endif()` before the closing line.

#### 1.3 Initial Instruction Tests

Create `tests/test_instructions.cpp` with one test case per `AvrFmt` group:
- `Rd_Rr`: `ADD R5, R10` — verify result and SREG Z/C/N/V/H flags
- `Rd_K8`: `LDI R20, 0x55` — verify register write, no flag changes
- `Rd_only`: `INC R3` — verify result and SREG (Z,N,V)
- `Rd06_K6`: `ADIW R24, 10` — verify word result and SREG
- `Rd06_Rr06`: `MOVW R4, R6` — verify register pair copy
- `k7`: `BREQ +10` (Z=1) — verify branch taken; `BREQ +10` (Z=0) — verify not taken
- `k02`: `RJMP 42` — verify PC set correctly
- `k22`: `JMP 0x2000` — 32-bit instruction, verify PC
- `LD_family`: `LD R5, X+` — verify load and pointer increment
- `LDD_family`: `LDD R5, Y+3` — verify load at offset
- `LDS_STS`: `LDS R5, 0x01FF` — 32-bit, verify load from SRAM
- `IO/Rr_b`: `SBRC R4, 3` — verify skip behavior
- `Rd_IO`/`IO_Rr`: `IN R5, 0x05`, `OUT 0x05, R5` — verify I/O roundtrip
- `b_only`: `BSET 3` — verify SREG.V set; `BCLR 3` — verify cleared

Each test:
1. Creates a fresh `AvrState` and calls `clearState()`
2. Pre-conditions registers/SREG/Memory as needed
3. Calls the executor function directly (not through `executeInstruction`) — tests the execution logic, not the decoder
4. Asserts register values, SREG flags, and PC changes

#### 1.4 Remove Debug Print

`src/executor.cpp:43` has `std::cout << op.mnemonic << std::endl;` in the execute loop. Remove it — this is a debugging artifact that clutters output. Trace functionality will replace it in Phase 6.

### Acceptance Criteria
1. `cmake -B build/native && cmake --build build/native` builds both `emulator` and `emulator_test`.
2. `./build/native/tests/emulator_test` passes all tests (≥15 test cases).
3. `0x9393` correctly decodes and executes when encountered in real firmware.
4. `test_decoder.cpp` iterates all 2^16 opcode patterns; every pattern from real `.elf` firmware decodes successfully.

---

## Phase 2: Complete Instruction Test Coverage

### Goal
Every of the 131 instructions has at least one dedicated test case with edge-case coverage (wrap, zero, carry chain, branch taken/not-taken, skip word-size handling).

### Dependencies
Phase 1.

### Files Affected

| File | Action |
|---|---|
| `tests/test_arithmetic.cpp` | **Create** — ADD, ADC, ADIW, SUB, SBC, SBCI, SUBI, INC, DEC, MUL, MULS, MULSU, NEG, ASR, SBIW, FMUL, FMULS, FMULSU |
| `tests/test_logic.cpp` | **Create** — AND, ANDI, OR, ORI, EOR, COM, SER, SWAP |
| `tests/test_datatransfer.cpp` | **Create** — MOV, MOVW, LDI, LD_*/ST_*/LDS/STS/LPM/IN/OUT/PUSH/POP |
| `tests/test_branch.cpp` | **Create** — all BRBC/BRBS variants, RJMP, JMP, IJMP, RCALL, CALL, ICALL, RET, RETI |
| `tests/test_bit.cpp` | **Create** — BSET, BCLR, BLD, BST, CBI, SBI, SREG-named aliases |
| `tests/test_skip.cpp` | **Create** — CPSE, SBRC, SBRS, SBIC, SBIS with 16-bit and 32-bit following instructions |
| `tests/test_mcu.cpp` | **Create** — NOP, SLEEP, WDR, BREAK, SPM |
| `tests/test_memory.cpp` | **Create** — data space address routing, SREG proxy, SP proxy, stack wrap, I/O bit helpers |
| `tests/test_regression.cpp` | **Create** — loads `.elf` files from `tests/firmware/`, runs to completion |
| `tests/firmware/` | **Create** directory — firmware binaries for regression |
| `tests/CMakeLists.txt` | Add new test sources |

### Steps

#### 2.1 Per-Category Test Files

Each test file follows the same pattern:
- `#include "catch_amalgamated.hpp"` + relevant executor/decoder headers
- `TEST_CASE("Category::InstructionName behavior")` sections
- Each instruction gets at minimum: normal case + edge case

Example edge cases:
- `ADD R0, R0` → R0*=2, verify C,N,Z,V,H flags
- `ADC R0, R0` with C=1 → result = R0*2+1
- `MUL 0xFF, 0xFF` → R1:R0 = 0xFE01
- `SBIW R24, 0` → Z flag set, no change
- `INC R0` on `0xFF` → wraps to `0x00`, Z=1
- `DEC R0` on `0x00` → wraps to `0xFF`, N=1, Z=0
- `LPM R0, Z` with Z pointing to known flash byte
- `PUSH`/`POP` round-trip: push byte, pop back, verify unchanged
- `CPSE` with equal registers, 2-word skip: PC advances 4 extra bytes

#### 2.2 Memory Tests

`tests/test_memory.cpp`:
- `readDataByte` at addresses: `0x00` (R0), `0x1F` (R31), `0x20` (I/O), `0x5D` (SPL proxy), `0x5E` (SPH proxy), `0x5F` (SREG proxy)
- `writeDataByte` at same addresses, verify via `readDataByte`
- Write to `0x5D` (SPL) → verify `state.sp` low byte updated
- `pushByte` + `popByte`: verify LIFO order, SP movement
- `pushWord` + `popWord`: verify little-endian byte order

### Acceptance Criteria
1. `./build/native/tests/emulator_test` passes >100 test cases.
2. Every `AvrOp` enum value appears in at least one `TEST_CASE` by name.

---

## Phase 3: Timer0 Peripheral (8-bit)

### Goal
Implement Timer0 with prescaler, overflow interrupt flag, and compare-match registers. This enables `delay()`, `millis()`, and `micros()` in Arduino sketches.

### Dependencies
Phase 2 (test infrastructure). Phase 4 (interrupt controller — Timer0 overflow must raise interrupts).

**Note:** Phase 3 and Phase 4 are co-dependent. Timer0 needs the interrupt controller to fire overflow interrupts. Implement Timer0 register model and overflow flag first, then wire to the interrupt controller in Phase 4.

### Files Affected

| File | Action |
|---|---|
| `src/timer0.h` | **Create** |
| `src/timer0.cpp` | **Create** |
| `src/memory.cpp` | Add Timer0 register dispatch in `readDataByte`/`writeDataByte` |
| `src/executor.cpp` | Call `timer0Tick(cycles)` after each instruction |
| `tests/test_timer0.cpp` | **Create** |
| `tests/CMakeLists.txt` | Add `test_timer0.cpp` |

### Register Map

| Data Addr | I/O Addr | Register | Description |
|---|---|---|---|
| `0x46` | `0x26` | `TCCR0A` | Timer/Counter Control Register A |
| `0x45` | `0x25` | `TCCR0B` | Timer/Counter Control Register B |
| `0x44` | `0x24` | `TCNT0` | Timer/Counter Register (8-bit) |
| `0x47` | `0x27` | `OCR0A` | Output Compare Register A |
| `0x48` | `0x28` | `OCR0B` | Output Compare Register B |
| `0x6E` | `0x4E` | `TIMSK0` | Interrupt Mask Register (ext I/O) |
| `0x35` | `0x15` | `TIFR0` | Interrupt Flag Register |

Prescaler divider lookup (TCCR0B CS02:CS00): `{0, 1, 8, 64, 256, 1024}`. For v1: Normal mode and CTC mode.

### Interface

```cpp
void timer0Tick(uint8_t cycles);
bool timer0Read(uint8_t ioAddr, uint8_t* out);
bool timer0Write(uint8_t ioAddr, uint8_t value);
void timer0SetState(AvrState* state);
void timer0Reset();
```

Internal state uses a `prescaler_accum` (uint16_t) that accumulates instruction cycles. When `prescaler_accum >= prescaler_div`, subtract the divider and advance TCNT0 by 1.

### Acceptance Criteria
1. TCNT0 increments at correct prescaler rate (256 cycles at /64 → TCNT0 advances by 4)
2. Normal mode: TCNT0 overflows 0xFF→0x00, TOV0 (TIFR0 bit 0) set
3. CTC mode: TCNT0 resets to 0 on match with OCR0A
4. A sketch using `delay(1000)` does not hang — Timer0 overflow flag eventually sets

---

## Phase 4: Interrupt Controller

### Goal
Implement the ATmega328P interrupt vector table, priority dispatch, I-bit gating, and nested interrupt prevention.

### Dependencies
Phase 2. Phase 3 provides the first interrupt source (Timer0 overflow).

### Files Affected

| File | Action |
|---|---|
| `src/interrupt.h` | **Create** |
| `src/interrupt.cpp` | **Create** |
| `src/executor.cpp` | Call `interruptCheck()` after each instruction |
| `tests/test_interrupts.cpp` | **Create** |

### Interface

```cpp
enum class InterruptVector : uint8_t { RESET = 1, …, TIMER0_OVF = 17, …, SPM_READY = 26 };
void interruptRaise(InterruptVector vec);
bool interruptCheck();  // returns true if dispatched
```

Dispatch logic: find highest-priority pending interrupt, push PC, clear I bit, jump to vector address. For v1, only TIMER0_OVF is wired from a peripheral. Wire Timer0 overflow to `interruptRaise(InterruptVector::TIMER0_OVF)`.

### Acceptance Criteria
1. Interrupt dispatches to correct vector, pushes PC, clears I
2. No dispatch when I=0
3. Highest priority (lowest vector number) fires first
4. RETI restores PC and sets I
5. Timer0 overflow sketch calls the TIMER0_OVF ISR

---

## Phase 5: GPIO Pin Model

### Goal
Model PORTB, PORTC, PORTD registers with DDR direction control and PIN register semantics.

### Dependencies
Phase 2. Independent of Phases 3-4.

### Files Affected

| File | Action |
|---|---|
| `src/gpio.h` | **Create** |
| `src/gpio.cpp` | **Create** |
| `src/memory.cpp` | Add GPIO dispatch |
| `src/main.cpp` | Add `--gpio-trace` flag |
| `tests/test_gpio.cpp` | **Create** |

### Pin Model

Three 8-bit ports (B, C, D), each with DDR, PORT, PIN. Internal state per port:
```cpp
struct GpioPort { uint8_t ddr, port, pin; };
```

Read semantics for PIN: if DDR=output, returns PORT; if DDR=input, returns external `pin` state (with pull-up when PORT=1). All `pin` fields initialize to 0.

### Acceptance Criteria
1. Write to DDRB sets direction; read returns written value
2. Write to PORTB when DDRB=0xFF → PINB reflects PORTB
3. Blink sketch toggling PORTB bit 5 (Arduino pin 13) sets/clears correct bit

---

## Phase 6: Execution Control & Tooling

### Goal
Add `--max-cycles`, `--trace`, and `--dump-state` CLI flags.

### Dependencies
Phase 2. Independent of Phases 3-5.

### Files Affected

| File | Action |
|---|---|
| `src/main.cpp` | Argument parsing for `--max-cycles`, `--trace`, `--dump-state` |
| `src/executor.h` | Add `AvrRunConfig` struct; change `executeProgram` signature |
| `src/executor.cpp` | Cycle counting, trace output, max-cycles halt |
| `tests/test_execution_control.cpp` | **Create** |

### Key Changes

`AvrRunConfig`:
```cpp
struct AvrRunConfig {
    uint32_t max_cycles = 0;
    bool     trace      = false;
    bool     dump_state = false;
};
```

Cycle tracking: add `uint8_t cycles` field to `AvrState`. Each executor writes actual cycle count (including +1 for taken branches/skips). Execute loop accumulates.

Trace format: `[PC=0x01E7] LDI  R20, 0x20   (R20: 0x00→0x20)`. Only show changed registers.

Dump-state on halt prints full register file, SREG, SP, PC, and SRAM[0x100..0x11F].

### Acceptance Criteria
1. `--max-cycles 100 --trace` prints exactly 100 traced instructions and exits 0
2. `--max-cycles 100 --dump-state` prints state dump on exit
3. No flags → silent execution

---

## Phase 7: ADC & Watchdog Peripherals

### Goal
Add ADC (configurable fixed-value reads) and Watchdog Timer (reset-on-timeout).

### Dependencies
Phase 2. Phase 4 for WDT interrupt vector.

### Files Affected

| File | Action |
|---|---|
| `src/adc.h` / `src/adc.cpp` | **Create** — ADMUX, ADCSRA, ADCH/ADCL |
| `src/wdt.h` / `src/wdt.cpp` | **Create** — WDTCSR, timeout logic |
| `src/memory.cpp` | Add ADC/WDT dispatch |
| `src/executor.cpp` | Call `wdtTick(cycles)` |
| `src/main.cpp` | Add `--adc-channel N=value` |
| `tests/test_adc.cpp` / `tests/test_wdt.cpp` | **Create** |

### ADC Simplification
Conversions complete instantly. When ADSC is set, load configured channel value into ADCH:ADCL and set ADIF. Default: `0x0200` mid-scale. Override via CLI.

### WDT Model
Timeout in cycles: `{16K, 32K, 64K, 128K, 256K, 512K, 1M, 2M}`. On timeout: set MCUSR WDRF, reset PC to 0, clear peripheral state. `WDR` resets counter.

### Acceptance Criteria
1. ADC returns configured values per channel
2. WDT timeout resets PC to 0 and sets MCUSR WDRF
3. `WDR` resets counter; WDE=0 disables

---

## Phase 8: WASM Target

### Goal
Build the emulator for WebAssembly with Emscripten embind bindings.

### Dependencies
Phases 1-7.

### Files Affected

| File | Action |
|---|---|
| `wasm/bindings.cpp` | **Create** — Embind bindings |
| `wasm/build.sh` | **Create** |
| `CMakeLists.txt` | Ensure includes for Emscripten headers |

### Exposed API
```cpp
class Emulator {
    bool loadFirmware(std::string data);
    bool step();
    int  run(int maxCycles);
    int  readReg(int r);
    int  readIOReg(int addr);
    void writeIOReg(int addr, int value);
    void setUartTxCallback(val cb);
};
```

**Note:** ELF loader needs an in-memory buffer overload (`loadFirmwareFromBuffer`) for WASM.

### Acceptance Criteria
1. `wasm/build.sh` produces `build/wasm/emulator.js` and `build/wasm/emulator.wasm`
2. HTML page loads module, steps through instructions, prints UART output

---

## Phase 9: CI & Regression Suite

### Goal
GitHub Actions CI: build on macOS/Linux, run tests, build WASM, run regression firmware.

### Dependencies
All prior phases.

### Files Affected

| File | Action |
|---|---|
| `.github/workflows/ci.yml` | **Create** |
| `tests/firmware/*.elf` | **Create** — committed regression binaries |
| `tests/firmware/compile.sh` | **Create** — documentation of firmware build |
| `README.md` | Add CI badge |

### CI Jobs
- `build-native`: ubuntu + macos, cmake build + run `emulator_test`
- `build-wasm`: ubuntu with emsdk, cmake wasm build
- `regression`: run all `tests/firmware/*.elf` through emulator with `--max-cycles 1000000`

### Regression Firmware
- `blink.ino`: toggles pin 13 every 500ms (exercises Timer0)
- `serial_echo.ino`: Serial read/write round-trip
- `math_test.ino`: all arithmetic/logic ops, exits with known state
- `branch_test.ino`: all 16 branch conditions, taken and not-taken

### Acceptance Criteria
1. CI passes on push: all three jobs green
2. Regression firmware runs without unknown-opcode crashes
3. Test suite passes on both Ubuntu and macOS

---

## Dependency Graph

```
Phase 1 (Decoder Fix + Tests)
  └── Phase 2 (Complete Test Coverage)
        ├── Phase 3 (Timer0)
        │     └── Phase 4 (Interrupts)
        │           └── Phase 7 (ADC + WDT)
        ├── Phase 5 (GPIO)
        ├── Phase 6 (Execution Control)
        └── Phase 8 (WASM)
              └── Phase 9 (CI + Regression)
```

Phases 3/5/6 are parallelizable after Phase 2. Phases 4 and 7 are sequential after Phase 3.

---

## Completion Criteria

- [ ] `0x9393` and any other mask gaps fixed; all AVR opcodes decode
- [ ] Every ATmega328P instruction has ≥1 unit test with edge cases
- [ ] Timer0 with prescaler, overflow, CTC mode → `delay()` and `millis()` work
- [ ] Interrupt controller dispatches TIMER0_OVF correctly
- [ ] GPIO pin model → PORTB bit 5 toggles like Arduino pin 13
- [ ] `--trace` prints per-instruction disassembly with register changes
- [ ] `--max-cycles N` halts cleanly
- [ ] `--dump-state` prints full architectural state
- [ ] ADC returns configurable values; WDT resets on timeout
- [ ] WASM target builds and exposes JS API
- [ ] CI passes on push across macOS and Linux
- [ ] Regression firmware `.elf` files run without errors

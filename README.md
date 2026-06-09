<!-- Project overview, build instructions, architecture notes, and status. -->
# ATmega328P Emulator

A cycle-accurate, instruction-complete emulator for the Microchip ATmega328P
microcontroller (the chip inside Arduino Uno R3), written in C++20.

Decodes and executes AVR ELF firmware images produced by `arduino-cli`,
`avr-gcc`, or any standard AVR toolchain.  Includes an interactive terminal
UI (TUI) with live register inspection, disassembly, and peripheral views.

## Project Status

| Area              | Status                     | Notes |
|-------------------|----------------------------|-------|
| Instruction set   | Complete (131 opcodes)     | All ATmega328P instructions decoded and executed. |
| ELF loading       | Done                       | Validates magic, parses headers, loads .text into flash. |
| Unified data space| Done                       | GP registers, I/O, extended I/O, SRAM in one address map. |
| SREG flags        | Done                       | Bit-level set/clear/get for all 8 flags. |
| Stack             | Done                       | push/pop byte and word through data-space SP. |
| UART (polled)     | Done                       | TUI mode (buffered TX/RX) and headless mode (stdin/stdout). |
| Timer0            | Done                       | Prescaler (/1, /8, /64, /256, /1024), CTC, overflow + compare-match interrupts. `delay()` and `millis()` work. |
| Interrupts        | Partial                    | TIMER0_OVF, TIMER0_COMPA, TIMER0_COMPB dispatched. USART and external interrupts not yet wired. |
| Cycle timing      | Done                       | Per-opcode cycle tracking + wall-clock sync (16 MHz nominal). `delay(1000)` takes 1 real second. |
| Unit tests        | Done (119 tests, 192k assertions) | Instruction-level SREG verification, loader validation, Timer0 prescaler/CTC tests. Catch2 framework. |
| TUI               | Done                       | Tabbed interface: Serial, Registers, Disassembly. Pause, speed toggle, change highlighting. |
| ADC               | Missing                    | No analog-to-digital converter peripheral. |
| GPIO pin model    | Missing                    | PORTB/C/D registers exist but no pin-level tracking. |
| EEPROM writes     | Missing                    | `SPM` writes R1:R0 to flash; full page-buffer SPM is deferred. |
| Watchdog          | Missing                    | `WDR` is a no-op. |
| WASM build        | Broken                     | `wasm/bindings.cpp` does not exist. |
| CI                | Missing                    | No GitHub Actions or continuous integration. |

## Quick Start

### Prerequisites

- **CMake ≥ 3.20**
- A C++20 compiler (GCC ≥ 10, Clang ≥ 10, Apple Clang ≥ 14)
- `arduino-cli` to compile `.ino` sketches to `.elf`

### Build

```bash
cmake -B build/native
cmake --build build/native
```

The binary is written to `build/native/emulator`.

### Run

```bash
# Direct path to a .elf file
./build/native/emulator <firmware.elf>

# No argument — fuzzy-finds .elf files with fzf
./build/native/emulator

# Compile and run a test sketch
arduino-cli compile --fqbn arduino:avr:uno --export-binaries tests/sketch/
./build/native/emulator tests/sketch/build/arduino.avr.uno/sketch.ino.elf
```

### Compiling a Sketch

```bash
arduino-cli compile --fqbn arduino:avr:uno --export-binaries <path-to-sketch-directory>
```

The `.elf` file lands in a `build/` subdirectory under the sketch directory.

## TUI

The emulator opens a fullscreen terminal UI with 3 tabs:

| Tab | Content |
|-----|---------|
| **Serial** | UART TX output (scrolling) + RX input prompt. Type and press Enter to send bytes to the emulated UART. |
| **Registers** | All 32 GP registers in a 4-column grid, SREG flags as colored indicators (green=set), plus PC, SP, and cycle count. Changed registers flash yellow. |
| **Disasm** | Live disassembly around the current PC. The current instruction is highlighted in yellow. |

### Keyboard Controls

| Key | Action |
|-----|--------|
| `Tab` / `Shift+Tab` | Cycle forward/backward through tabs |
| `Space` | Pause / resume emulation |
| `t` | Toggle between slow (~5 fps, readable) and fast (real-time) view updates |
| `Enter` | Send typed input to UART (Serial tab only) |
| `Esc` | Stop emulation; press again to exit |

### Pause & Speed Toggle

Pressing `Space` freezes execution — registers, disassembly, and SREG flags hold
their current values for inspection.  Press `Space` again to resume.

Pressing `t` toggles the register and disassembly views between:
- **slow** — state snapshots every ~200ms.  Readable at full execution speed.
- **fast** — updates every frame.  Useful for watching rapid state changes.

## Testing

```bash
cd build/native/tests && ./emulator_test
```

119 test cases, 192k assertions. Covers instruction-level SREG flag verification
for arithmetic, logic, branch, and data-transfer ops, ELF loader validation,
plus Timer0 prescaler and CTC mode behavior.

## Project Layout

```
.
├── CMakeLists.txt              # Build system
├── README.md
├── REASONIX.md                 # Architecture & conventions
├── LICENSE
├── cmake/
│   └── Emscripten.cmake        # Emscripten toolchain (WASM, experimental)
├── src/
│   ├── main.cpp                # Entry point + TUI (FTXUI)
│   ├── state.h                 # AvrState: registers, PC, SP, SREG, flash/SRAM/EEPROM
│   ├── decoder.h / .cpp        # Opcode table, decodeInstruction(), operand extractors
│   ├── executor.h / .cpp       # Execute loop, all 131 instruction implementations
│   ├── disasm.h / .cpp         # Disassembly formatter (all 17 operand formats)
│   ├── loader.h / .cpp         # ELF parser and flash loader
│   ├── memory.h / .cpp         # Unified data-space read/write, SREG helpers, stack
│   ├── uart.h / .cpp           # UART peripheral (TUI + headless modes)
│   ├── timer0.h / .cpp         # Timer0 prescaler, CTC, overflow/compare interrupts
│   ├── interrupt.h / .cpp      # Interrupt vector table and dispatch
│   ├── error.h / .cpp          # Structured error reporting
├── tests/
│   ├── catch2/                 # Catch2 test framework (amalgamated)
│   ├── test_decoder.cpp        # Opcode table validation
│   ├── test_instructions.cpp   # Per-instruction SREG flag tests
│   ├── test_timer0.cpp         # Timer0 prescaler, CTC, overflow tests
│   └── sketch/                 # Arduino test sketches (.ino → .elf)
├── docs/
│   ├── v1-plan.md
│   ├── uart-plan.md
│   ├── instruction-implementation.md
│   └── memory-helpers-plan.md
└── wasm/                       # WASM bindings (not yet implemented)
```

## Architecture

### Instruction Pipeline

1. **Fetch** — read 16-bit word from `flash[pc]` (little-endian).
2. **Decode** — linear scan of 131-entry opcode table: `(instr & mask) == code`.  For 32-bit instructions (JMP, CALL, LDS, STS), a second word is fetched.
3. **Operand Extract** — format-specific decoder (17 formats: Rd_Rr, Rd_K8, k7, LDD_family, etc.).
4. **Execute** — `switch(op.op)` dispatches to the per-opcode function. SREG flags updated atomically per instruction.
5. **PC Advance** — control-flow instructions set `state.pc`; sequential instructions advance `pc += words * 2`.
6. **Peripheral Tick** — cycle count accumulated, Timer0 advanced, wall-clock sync applied.

### Unified Data Space

All reads and writes route through `readDataByte()` / `writeDataByte()`:

| Address Range   | Region         | Backing Store            |
|-----------------|----------------|--------------------------|
| 0x0000 – 0x001F | GP Registers   | `state.r[addr]`          |
| 0x0020 – 0x005F | I/O Registers  | `state.sram[addr]`       |
| 0x005D – 0x005F | SPL/SPH/SREG   | `state.sp` / `state.sreg`|
| 0x0060 – 0x00FF | Ext I/O        | `state.sram[addr]`       |
| 0x0100 – 0x08FF | SRAM           | `state.sram[addr]`       |

### Timer0 & `delay()` / `millis()`

Arduino's `delay(ms)` busy-loops reading a `millis()` counter incremented by the
firmware's Timer0 overflow ISR. The emulator advances Timer0 from instruction
cycle counts, raises `TIMER0_OVF`, and executes the compiled ISR through the
normal interrupt controller.

### Wall-Clock Sync

```cpp
auto target = wall_start + μs(cycle_count * 50'000 / 16'000'000);
if (target - now > 100μs) sleep_until(target);
```

Sleeps between instructions when ahead of schedule.  ~100µs threshold balances
precision against syscall overhead.

## Resources

- [ATmega328P Datasheet](https://ww1.microchip.com/downloads/en/DeviceDoc/Atmel-7810-Automotive-Microcontrollers-ATmega328P_Datasheet.pdf)
- [AVR Instruction Set Manual](https://ww1.microchip.com/downloads/en/DeviceDoc/AVR-InstructionSet-Manual-DS40002198.pdf)
- [FTXUI](https://github.com/ArthurSonzogni/ftxui) — terminal UI library

# ATmega328P Emulator

A cycle-aware, instruction-complete emulator for the Microchip ATmega328P microcontroller (the chip inside Arduino Uno R3), written in C++20 with zero third-party dependencies.

Decodes and executes AVR ELF firmware images produced by `arduino-cli`, `avr-gcc`, or any standard AVR toolchain.

## Project Status (pre-v1)

| Area              | Status  | Notes |
|-------------------|---------|-------|
| Instruction set   | Complete (131 opcodes + aliases) | All documented ATmega328P instructions decoded and executed. See `src/decoder.h`. |
| ELF loading       | Done    | Validates magic, parses program headers, loads text segment into flash. |
| Unified data space| Done    | Transparent mapping: GP registers (0x00–0x1F), I/O (0x20–0x5F), extended I/O (0x60–0xFF), SRAM (0x100–0x8FF). |
| SREG flags        | Done    | Bit-level set/clear/get for all 8 SREG flags. |
| Stack             | Done    | push/pop byte and word through data-space SP. |
| UART (polled)     | Done    | TX → stdout, RX ← stdin (non-blocking, POSIX + Windows). |
| Timers             | Missing | No Timer0/1/2. `delay()`, `millis()`, PWM, tone() will not work. |
| Interrupts         | Missing | No vector table dispatch. `sei()`/`cli()` work but nothing triggers interrupts. |
| ADC               | Missing | No analog-to-digital converter peripheral. |
| GPIO pin model    | Missing | PORTB/C/D registers exist in data space but no pin-level tracking. |
| EEPROM writes     | Missing | `SPM` is a no-op. |
| Watchdog          | Missing | `WDR` is a no-op. |
| Cycle timing      | Partial | Opcode table carries `cycles_min` but no cycle counter or timing simulation. |
| Instruction trace | Missing | No `--trace` or per-instruction disassembly output. |
| Unit tests        | Missing | No automated tests for any instruction. |
| WASM build        | Broken  | `wasm/bindings.cpp` does not exist. |
| CI                | Missing | No GitHub Actions or continuous integration. |

**Before declaring v1**, all "Missing" items in the table above must be addressed.

## Quick Start

### Prerequisites

- **CMake ≥ 3.20**
- A C++20 compiler (GCC ≥ 10, Clang ≥ 10, Apple Clang ≥ 14)
- arduino-cli to compile `.ino` sketches to `.elf`

### Build

```bash
# Configure (run once, or after changes to CMakeLists.txt)
cmake -B build/native

# Build
cmake --build build/native
```
The binary is written to `build/native/emulator`

### Run
from project root
```bash
# Basic usage
./build/native/emulator <firmware.elf>

# Compile and run a test sketch
arduino-cli compile --fqbn arduino:avr:uno --export-binaries tests/sketch/
./build/native/emulator tests/sketch/build/arduino.avr.uno/sketch.ino.elf
```

### Compiling a Sketch

```bash
arduino-cli compile --fqbn arduino:avr:uno --export-binaries <path-to-sketch-directory>
```

The `.elf` file will be placed in a `build/` subdirectory under the sketch directory.

## Project Layout

```
.
├── CMakeLists.txt              # Build system (native + WASM targets)
├── README.md
├── REASONIX.md                 # Architecture & conventions reference
├── LICENSE
├── cmake/
│   └── Emscripten.cmake        # Emscripten toolchain file
├── src/
│   ├── main.cpp                # Desktop CLI entry point
│   ├── state.h                 # AvrState: registers, PC, SP, SREG, flash/SRAM/EEPROM
│   ├── decoder.h               # AvrOp enum (all opcodes), Opcode table, operand structs
│   ├── decoder.cpp             # decodeInstruction() + operand extractors
│   ├── executor.h              # Execute function declarations
│   ├── executor.cpp            # Execute loop + all instruction implementations
│   ├── loader.h                # ELF header structs, loadFirmware()
│   ├── loader.cpp              # ELF parser and flash loader
│   ├── memory.h                # Data-space access, SREG helpers, stack, I/O bit ops
│   ├── memory.cpp              # Unified read/write dispatch
│   ├── uart.h                  # UART peripheral API
│   ├── uart.cpp                # UART emulation (TX stdout, RX stdin)
│   ├── error.h                 # Structured error reporting
│   └── error.cpp               # Error formatters
├── wasm/
│   └── (empty)                 # WASM bindings entry point (not yet created)
├── tests/
│   └── sketch/
│       └── sketch.ino          # Minimal test firmware (Serial.print loop)
└── docs/
```

## Architecture

### Instruction Decode → Execute Pipeline

1. **Fetch**: `readFlashWord(state, state.pc)` reads the next 16-bit instruction from flash.
2. **Decode**: `decodeInstruction(instr)` walks the opcode table, matching `(instr & mask) == code`. Returns `Opcode` with a resolved `AvrOp`, `AvrFmt`, `words`, `cycles_min`, and mnemonic. For 32-bit instructions (JMP, CALL, LDS, STS), the second word is fetched and decoded separately.
3. **Operand Extract**: The matching `AvrFmt` determines which decode helper to call (e.g., `decodeRdRr` for `AvrFmt::Rd_Rr`), producing a POD operand struct.
4. **Execute**: The `AvrOp` cases in `executeInstruction()` dispatch to the per-opcode execution function, passing the pre-decoded operands.
5. **PC Advance**: Control flow is handled inside each execution function (branches/jumps/calls set `state.pc`; sequential instructions advance `pc += words * 2` in the main loop).

### Unified Data Space

All data-space reads and writes go through `readDataByte()` / `writeDataByte()` in `memory.cpp`, which map address ranges to the appropriate backing store:

| Address Range   | Region         | Backing Store               |
|-----------------|----------------|-----------------------------|
| 0x0000 – 0x001F | GP Registers   | `state.r[addr]`             |
| 0x0020 – 0x005F | I/O Registers  | `state.sram[addr]`          |
| 0x005D – 0x005F | SPL/SPH/SREG   | `state.sp` / `state.sreg`   |
| 0x0060 – 0x00FF | Ext I/O        | `state.sram[addr]`          |
| 0x0100 – 0x08FF | SRAM           | `state.sram[addr]`          |

I/O register writes at addresses 0x09–0x0D (UART range) are additionally routed through `uartWrite()` for peripheral emulation.

### UART

Polled, no interrupts. On every `OUT` to UCSRB (I/O 0x0A) or `OUT` to UCSRA data-ready bits, the UART checks if the firmware has written the Data Register (UDR, I/O 0x0C). If so, the byte is written to `stdout`. On every read of UCSRA, the UART polls `stdin` (non-blocking, `read()` with `O_NONBLOCK` on POSIX, `_kbhit()` on Windows) and sets the RXC flag if a byte is available. The byte is returned from the subsequent UDR read.

## Building for WASM (Experimental)

```bash
# Ensure EMSDK is set
export EMSDK=/path/to/emsdk
source "$EMSDK/emsdk_env.sh"

cmake -B build/wasm -DCMAKE_TOOLCHAIN_FILE=cmake/Emscripten.cmake
cmake --build build/wasm
```

**Note**: `wasm/bindings.cpp` has not been created. Native target is the primary development target for now.

## Resources

- [ATmega328P Datasheet](https://ww1.microchip.com/downloads/en/DeviceDoc/Atmel-7810-Automotive-Microcontrollers-ATmega328P_Datasheet.pdf)
- [AVR Instruction Set Manual](https://ww1.microchip.com/downloads/en/DeviceDoc/AVR-InstructionSet-Manual-DS40002198.pdf)
- [ELF Format Reference](https://gist.github.com/x0nu11byt3/bcb35c3de461e5fb66173071a2379779)

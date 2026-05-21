# UART Module for ATmega328P Emulator — Planning Document

## 1. Problem & Outcome

**Current state:** `src/uart.h` and `src/uart.cpp` are empty (0 bytes). The UART I/O registers (UDR, UCSRA, UCSRB, UCSRC, UBRRL/H) are not handled specially — reads return zero (the cleared-SRAM default) and writes silently land in `state.sram[]`. Firmware that tries to transmit via UART gets no output; firmware that polls for received data sees an eternally-empty buffer. The emulator cannot run any Arduino sketch that uses `Serial.begin()` / `Serial.print()` / `Serial.read()`.

**Desired outcome:** A `uart.h` / `uart.cpp` module that intercepts reads and writes to the ATmega328P UART register range. When firmware writes a byte to UDR (the transmit data register), the emulator writes that byte to `stdout`. When firmware reads from UDR or polls the RXC (receive complete) flag in UCSRA, the emulator checks `stdin` for available input and surfaces it as if a byte had arrived over the wire. A minimal Arduino sketch (`Serial.begin(9600); Serial.println("hello");`) produces visible console output when run through the emulator.

**Success signals:**
- `Serial.print("hello world\n")` from a compiled Arduino sketch emits that exact string to stdout when the emulator runs.
- `Serial.read()` returns bytes typed into the emulator's stdin (one byte at a time, polling-style).
- `Serial.available()` returns a non-zero count when stdin has data pending.
- No regressions: all existing executor tests / build continue to pass.
- The UART module is ~100–150 lines total, not a sprawling peripheral simulator.

## 2. Scope

**In scope:**
- UART register file for ATmega328P USART0: UCSRA, UCSRB, UCSRC, UBRRL, UBRRH, UDR (6 registers at I/O addresses 0x0B–0x0D plus 0x09–0x0A).
- TX path: byte written to UDR → `std::cout.put()` immediately (no baud-rate delay).
- RX path: `std::cin` polled on every read of UCSRA or UDR; RXC flag set when a byte is available; data byte returned on UDR read.
- UDRE (data register empty) flag always set — the emulated UART is infinitely fast at transmitting.
- Integration hook in `src/memory.cpp`: `readDataByte` / `writeDataByte` delegate UART-range addresses (data-space 0x29–0x2D) to `uartRead()` / `uartWrite()`.
- `uartInit()` called once at emulator startup to put stdin in non-blocking mode (POSIX `fcntl` / Windows `_setmode` as appropriate).
- `uartPoll()` called once per emulated instruction cycle (or lazily on UART register access) to check stdin and buffer available data.

**Out of scope (explicit):**
- Interrupt-driven UART (RXCIE, TXCIE, UDRIE) — requires full interrupt controller integration.
- Baud-rate simulation / timing — TX is instant, RX is always-ready. AVR firmware configures baud rate registers, but the emulator ignores them.
- Hardware flow control (CTS/RTS), parity errors, framing errors, overrun errors — all error flags hardwired to 0.
- Multi-byte buffered TX/RX rings — single-byte interface only.
- WASM build — stdin/stdout don't exist in a browser. The WASM target will need a separate I/O channel (callback-based or SharedArrayBuffer). Deferred to a follow-up.
- Second USART (ATmega328P has only one).

## 3. Requirements

### Product expectations

| # | Requirement | Acceptance criteria |
|---|---|---|
| R1 | Writing a byte to UDR (I/O 0x0C) outputs it to stdout | Run emulator with blink-serial.elf; `Serial.write('A')` prints `A` |
| R2 | Reading UCSRA (I/O 0x0B) returns correct flag bits: RXC=bit7, UDRE=bit5 | `Serial.available()` returns non-zero when stdin has data |
| R3 | Reading UDR (I/O 0x0C) returns the oldest buffered stdin byte and clears RXC | `Serial.read()` returns typed characters in order |
| R4 | UDRE flag (bit 5 of UCSRA) is always 1 | Firmware never spins waiting for TX buffer to empty |
| R5 | UCSRB/UCSRC/UBRRL/UBRRH are readable/writable but have no side effects | Firmware can configure baud rate without error; values stored but ignored |
| R6 | Non-blocking stdin: reads return immediately with 0 bytes if no input | Emulator doesn't hang waiting for keyboard input |
| R7 | UART registers outside the 0x29–0x2D data-space range are not affected | Existing I/O register behavior (SPL/SPH/SREG proxying, generic SRAM) is preserved |

### Engineering constraints

- **Language:** C++20, matching existing codebase conventions (`#pragma once`, PascalCase enums, camelCase functions).
- **No heap allocation:** The UART module uses a single statically-sized internal byte buffer (1 byte is sufficient for polled single-byte interface).
- **Platform:** POSIX (`<termios.h>`, `<fcntl.h>`) for native build; `#ifdef _WIN32` path for Windows (`<conio.h>` `_kbhit()` / `_getch()`).
- **Header-only integration:** `uart.h` exposes `uartInit()`, `uartPoll()`, `uartRead(uint8_t ioAddr)`, `uartWrite(uint8_t ioAddr, uint8_t value)`. `memory.cpp` calls these from `readDataByte`/`writeDataByte`.
- **No exceptions:** All functions return `bool` or `void`.

## 4. Scale & Constraints

| Dimension | Expected | Peak | Source/Assumption |
|---|---|---|---|
| QPS / throughput | N/A — single-threaded emulation | N/A | CLI tool, no server |
| Data volume | ~1 byte per UART access | Same | Polled I/O, single-byte buffer |
| Concurrent users | 1 | 1 | Single-user emulator |
| Latency target | Instant TX, sub-instruction RX | N/A | No timing simulation |
| Growth rate | N/A | N/A | Static target device |

## 5. Data Modeling

### UART register map (ATmega328P)

| I/O addr | Data-space addr | Register | Bits | Our handling |
|---|---|---|---|---|
| 0x09 | 0x29 | UBRRL | baud rate low byte | Read/write stored in `state.sram[0x29]`, ignored |
| 0x0A | 0x2A | UCSRB | RXCIE,TXCIE,UDRIE,RXEN,TXEN,UCSZ2,RXB8,TXB8 | Read/write, ignored except we could use RXEN/TXEN as enable flags |
| 0x0A | 0x2A | UBRRH (alternate access) | — | Not modeled; ATmega328P uses UCSRC shared-address scheme; simplified |
| 0x0B | 0x2B | UCSRA | RXC,TXC,UDRE,FE,DOR,PE,U2X,MPCM | RXC (bit 7) set when RX byte available; UDRE (bit 5) always 1; TXC (bit 6) always 1; others 0 |
| 0x0C | 0x2C | UDR | TX/RX data | Write → stdout; Read → buffered stdin byte |
| 0x0D | 0x2D | UCSRC | UMSEL,UPM,USBS,UCSZ,URSEL | Read/write, ignored |

### Internal state (in `uart.cpp`, file-static)

```cpp
// Non-blocking stdin state
static bool stdinReady = false;   // Set by uartPoll() when a byte is available
static char rxBuffer = 0;         // The buffered stdin byte
static bool initialized = false;  // uartInit() guard
```

### Key invariants
- `stdinReady` is true iff `rxBuffer` holds a valid byte that hasn't been consumed.
- Reading UDR when `stdinReady` is true returns `rxBuffer` and clears `stdinReady`.
- Reading UDR when `stdinReady` is false returns the last value (or 0x00) — matches hardware behavior of reading an empty data register.
- TX is fire-and-forget: write to UDR always succeeds immediately, UDRE is always 1.

## 6. Approach

### Architecture overview

The UART module is a small C++ file (`uart.cpp`) with four public functions (`uartInit`, `uartPoll`, `uartRead`, `uartWrite`) and private file-static state. `memory.cpp`'s `readDataByte` and `writeDataByte` check whether the data-space address falls in the UART range (0x29–0x2D) and delegate to `uartRead`/`uartWrite` before falling through to the generic `state.sram[]` path. `uartInit()` is called once at the top of `executeProgram()` to put stdin in non-blocking mode. `uartPoll()` is called at the start of each instruction cycle to check if stdin has a byte ready.

### Flow: TX (firmware sends a byte)

```
1. Firmware executes OUT 0x0C, R16  (write byte to UDR)
2. executeOUT() → writeDataByte(state, 0x2C, value)
3. writeDataByte sees addr 0x2C in UART range → calls uartWrite(0x0C, value)
4. uartWrite() sees ioAddr == 0x0C → std::cout.put(value) → flushes stdout
5. Return; byte transmitted.
```

### Flow: RX (firmware polls for a byte)

```
1. Firmware executes IN R16, 0x0B  (read UCSRA to check RXC)
2. executeIN() → readDataByte(state, 0x2B)
3. readDataByte sees addr 0x2B in UART range → calls uartRead(0x0B)
4. uartRead() for UCSRA:
   - Checks file-static stdinReady flag
   - If stdinReady: returns 0xA0 (RXC=1, UDRE=1, TXC=1)
   - If not: returns 0x60 (UDRE=1, TXC=1, RXC=0)
5. Firmware sees RXC=1, executes IN R16, 0x0C (read UDR)
6. readDataByte calls uartRead(0x0C)
7. uartRead() for UDR: returns rxBuffer, clears stdinReady
```

### Flow: stdin polling

```
1. uartPoll() called at top of executeProgram loop
2. If stdinReady is already true, return (byte already buffered)
3. Use platform-specific non-blocking read:
   - POSIX: read(STDIN_FILENO, &rxBuffer, 1) with O_NONBLOCK
   - Windows: _kbhit() then _getch()
4. If a byte was read, set stdinReady = true
```

### ASCII diagram

```
  ┌──────────────┐     ┌──────────────┐     ┌──────────┐
  │  executor    │     │  memory.cpp  │     │ uart.cpp │
  │  IN/OUT      │────▶│ readDataByte │────▶│ uartRead │──▶ stdin
  │  instructions│     │ writeDataByte│     │ uartWrite│──▶ stdout
  └──────────────┘     └──────────────┘     └──────────┘
```

## 7. Implementation Details

### File / module plan

| File | Action | Responsibility | Key exports |
|---|---|---|---|
| `src/uart.h` | Fill (currently empty) | Declare UART public API | `uartInit()`, `uartPoll()`, `uartRead()`, `uartWrite()` |
| `src/uart.cpp` | Fill (currently empty) | Implement UART logic + platform non-blocking I/O | (same symbols) |
| `src/memory.cpp` | Modify | Add UART range delegation in `readDataByte` / `writeDataByte` | — |
| `src/memory.h` | Modify | Add `#include "uart.h"` (or forward-declare) so `readDataByte`/`writeDataByte` can call uart functions | — |
| `src/executor.cpp` | Modify | Call `uartInit()` in `executeProgram()`; call `uartPoll()` once per instruction | — |

### Key interfaces & signatures

```cpp
// ── src/uart.h ─────────────────────────────────────────────────────────

#pragma once
#include <cstdint>

// Call once at emulator startup. Puts stdin in non-blocking mode.
// Returns true on success, false if platform setup fails.
bool uartInit();

// Call once per emulated instruction cycle. Checks stdin for a pending
// byte and buffers it internally if one is available.
void uartPoll();

// Read the value that should be returned when firmware reads from an
// I/O register in the UART range (0x09–0x0D).  ioAddr is the I/O address
// (0x00–0x3F), NOT the data-space address.
uint8_t uartRead(uint8_t ioAddr);

// Handle a write by firmware to an I/O register in the UART range.
// ioAddr is the I/O address (0x00–0x3F), NOT the data-space address.
void uartWrite(uint8_t ioAddr, uint8_t value);
```

### Hook placement in `memory.cpp`

The UART registers occupy data-space addresses 0x29–0x2D. In `readDataByte`, add BEFORE the final `return state.sram[addr]`:

```cpp
// In readDataByte, after the SREG/SPL/SPH checks:
if (addr >= 0x29 && addr <= 0x2D) {
    return uartRead(addr - 0x20);  // data-space → I/O address
}
```

In `writeDataByte`, same position:

```cpp
if (addr >= 0x29 && addr <= 0x2D) {
    uartWrite(addr - 0x20, value);
    return;  // do NOT fall through to state.sram[] write
}
```

This means UART register values are NOT stored in `state.sram[]` — they're fully managed by `uart.cpp`. This is the same pattern used for SPL/SPH/SREG (which return before hitting the sram array). The generic I/O registers (0x20–0x28, 0x2E–0x5C, etc.) continue to be stored in `state.sram[]`.

### Data flow at call-site level

```
executeProgram()                          [executor.cpp]
  ├─ uartInit()                           // once, before main loop
  └─ loop:
       ├─ uartPoll()                      // check stdin for new bytes
       ├─ decodeInstruction()
       └─ executeInstruction()
            └─ executeOUT(state, ops)      // e.g. OUT 0x0C, R16
                 └─ writeDataByte(state, 0x2C, value)   [memory.cpp]
                      └─ uartWrite(0x0C, value)          [uart.cpp]
                           └─ std::cout.put(value)
```

### Implementation order

1. **`src/uart.h`** — declare the four public functions. No dependencies.
2. **`src/uart.cpp`** — implement `uartInit` (non-blocking stdin setup), `uartPoll` (platform-specific `_kbhit` / `read`), `uartRead` (return UCSRA flags or UDR byte), `uartWrite` (stdout on UDR write, ignore others). No dependencies beyond standard library and `uart.h`.
3. **`src/memory.cpp`** — add UART range delegation (5 lines in `readDataByte`, 5 lines in `writeDataByte`). Depends on `uart.h`.
4. **`src/memory.h`** — add `#include "uart.h"`. Depends on step 1.
5. **`src/executor.cpp`** — call `uartInit()` at top of `executeProgram()`; call `uartPoll()` at top of the instruction loop. Depends on `uart.h`.

### Configuration & environment

None. No env vars, no config files. Platform detection is via standard preprocessor macros (`__linux__`, `__APPLE__`, `_WIN32`).

### New dependencies

None. Standard C++ library only (`<cstdint>`, `<iostream>`). Platform headers (`<unistd.h>`, `<fcntl.h>` on POSIX; `<conio.h>` on Windows) are system headers, not third-party packages.

### Migration / data changes

None. No new state fields needed in `AvrState`. The UART internal state is file-static in `uart.cpp`.

## 8. Proportionality

**Simplest approach:** A single-byte RX buffer with non-blocking stdin poll. TX is `std::cout.put()`. No baud-rate simulation, no interrupt support, no multi-byte FIFOs. The UART register values (baud rate, control/status bits beyond RXC/UDRE/TXC) are readable/writable but ignored. This is roughly 100 lines of C++.

**What we are NOT building (and why):**
- No interrupt controller integration — requires a full interrupt subsystem (vector table, priority, I-bit handling, RETI behavior). Deferred until interrupts are needed.
- No baud-rate-accurate timing — the emulator doesn't track cycle counts yet. TX is instant; RX is always-ready. This is sufficient for polled `Serial.print()` / `Serial.read()` loops.
- No ring buffers / FIFOs — single-byte interface matches the simplest polling pattern Arduino sketches use.
- No WASM I/O — stdin/stdout don't exist in a browser. WASM will need callback hooks; that's a separate design problem.

**Future pressure points:**
- When cycle-accurate timing is added, TX should buffer bytes and "transmit" them at the configured baud rate (1 bit per (F_CPU / baud) cycles).
- When interrupt support is added, writing to UDR should trigger TXC/RXC interrupts, and the interrupt flag bits in UCSRA/UCSRB become meaningful.
- When WASM support is needed, `uartRead`/`uartWrite` should become configurable callbacks rather than hardcoded `stdin`/`stdout`.

## 9. Decisions (pre-implementation)

- **UART registers NOT stored in `state.sram[]`:** Decision: UART registers are fully managed by `uart.cpp` and the data-space addresses 0x29–0x2D proxy to `uartRead`/`uartWrite`. Rationale: this is the same pattern as SPL/SPH/SREG and avoids synchronizing two copies of state.
- **Non-blocking stdin via platform-specific code:** Decision: use `#ifdef` to select between POSIX (`O_NONBLOCK` + `read`) and Windows (`_kbhit` + `_getch`). Rationale: C++ standard library has no portable non-blocking console I/O.
- **UCSRB UCSZ2 bit and UCSRC shared-address scheme:** Decision: simplify. UCSRB (I/O 0x0A) and UCSRC (I/O 0x0D) are treated as independent registers. The real ATmega328P shares I/O address 0x0A between UCSRB and UBRRH using the URSEL bit — we ignore this complexity since baud rate is ignored anyway.
- **`uartPoll()` called once per instruction:** Decision: poll stdin at the top of the execute loop, not lazily on UART access. Rationale: ensures stdin bytes are buffered before firmware checks UCSRA, even if the firmware loop is tight. The overhead of a non-blocking read on an empty fd is negligible.

## 10. Risks & Open Questions

- **[Risk] Platform portability of non-blocking stdin.** POSIX `O_NONBLOCK` on `STDIN_FILENO` works on Linux/macOS but may behave differently under a TTY vs pipe. **Mitigation:** test under both `./emulator firmware.elf` (direct terminal) and `echo "data" | ./emulator firmware.elf` (pipe).
- **[Risk] Windows `_kbhit()` / `_getch()` only work in console mode, not under redirection.** **Mitigation:** document that Windows stdin redirection (`type file.txt | emulator.exe`) is not supported; use POSIX path (WSL/MSYS2) for that use case.
- **[Question] Should `uartPoll()` be called lazily (on first UART register access after a poll-miss) instead of every instruction?** Calling it every instruction (~1 µs of emulated time) means sub-instruction latency for RX, but the poll itself is a syscall. At 16 MHz emulated speed this could be thousands of syscalls per second. **Owner:** implementer — start with per-instruction polling; if perf is an issue, throttle to once per ~1000 instructions.
- **[Question] What should `uartRead(UDR)` return when no byte is available?** The ATmega328P datasheet says UDR read when RXC=0 returns undefined data. **Owner:** return 0x00 for predictability; match common simulator behavior.

## 11. Test Plan

| Layer | Coverage | Method |
|---|---|---|
| Unit — uartRead | Verify UCSRA returns 0x60 (UDRE+TXC) when no RX data, 0xE0 (RXC+UDRE+TXC) when data buffered | Manual: write a small C++ test harness that calls uartPoll, uartRead in sequence |
| Unit — uartWrite | Verify UDR write calls `std::cout.put()` with correct byte | Manual: run emulator with known firmware, capture stdout, compare |
| Integration — TX | `Serial.println("hello")` from Arduino sketch produces "hello\r\n" on stdout | Run emulator with `tests/sketch/` .elf file |
| Integration — RX | Type a character; verify `Serial.read()` returns it | Run emulator interactively, type, observe output |
| Integration — available() | `Serial.available()` returns 0 when no stdin, 1 after typing one character | Same interactive test |
| Regression | Full native build with `-Wall -Wextra -Wpedantic` | `cmake --build build/native` |

The project has no automated test framework. The pragmatic approach: build the module, create a small Arduino sketch that exercises TX and RX, compile it to .elf, and run it through the emulator. Verify stdout matches expected output and that typed characters are echoed back.

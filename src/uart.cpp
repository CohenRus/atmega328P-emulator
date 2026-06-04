// ===========================================================================
// uart.cpp — UART peripheral emulation for ATmega328P.
//
// Provides polled stdin/stdout UART I/O.  TX writes to stdout, RX reads from
// stdin via non-blocking poll.  No interrupts are emulated — firmware must
// poll the UART status register or use a timer-based polling loop.
//
// Key design decisions:
// - stdin is set to non-blocking mode at init via fcntl (POSIX) or relies on
//   _kbhit (Windows).  uartPoll() is called once per instruction cycle and
//   buffers at most one byte at a time, matching the hardware's single-byte
//   receive buffer.
// - UART register state (UBRRL, UCSRB, UCSRC) is stored outside AvrState
//   because these registers are not directly accessed by the core — only via
//   the data-space I/O map in memory.cpp.
// - UCSRA is synthesized on read: bits 6 (TXC) and 5 (UDRE) are always set
//   (transmit complete, data register empty), and bit 7 (RXC) reflects
//   whether a byte has been buffered from stdin.
// - Both ATmega328P (0xC0-0xC6) and legacy ATmega168 (0x29-0x2D) register
//   mappings are supported in uartRead/uartWrite.
// ===========================================================================

#include "uart.h"

#include <cstdio>
#include <iostream>

// ── Platform-specific non-blocking stdin ─────────────────────────────────────

#if defined(_WIN32)
#  include <conio.h>

// Buffered receive state: rxBuffer holds a byte received from stdin,
// stdinReady indicates whether the buffer is full.  This emulates the
// hardware UDR0 register with its single-byte receive buffer.
static bool stdinReady = false;
static char rxBuffer   = 0;

// Initialize UART.  On Windows, no special setup is needed — _kbhit()
// and _getch() operate in non-blocking mode by default.
// @return always true on Windows
bool uartInit() {
    // Windows console I/O is naturally non-blocking with _kbhit()
    return true;
}

// Poll stdin for a byte.  If a byte is already buffered (stdinReady),
// this is a no-op.  Otherwise, checks _kbhit() for pending input and
// consumes one byte from the console via _getch().
// Called once per emulated instruction cycle.
void uartPoll() {
    if (stdinReady) return;               // already have a byte buffered
    if (_kbhit()) {
        rxBuffer   = (char)_getch();
        stdinReady = true;
    }
}

#else  // POSIX (Linux / macOS)
#  include <fcntl.h>
#  include <unistd.h>

// Buffered receive state — same semantics as the Windows version above.
static bool stdinReady = false;
static char rxBuffer   = 0;

// Tracks whether stdin has been set to O_NONBLOCK.  Only set once — on
// POSIX, the fcntl flag persists for the lifetime of the process.
static bool initialized = false;

// Initialize UART by setting stdin to non-blocking mode (O_NONBLOCK).
// This allows uartPoll() to check for input without blocking the emulator.
// Idempotent — subsequent calls are no-ops once initialized.
// @return true on success, false if fcntl fails
bool uartInit() {
    if (initialized) return true;
    int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    if (flags == -1) return false;
    if (fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK) == -1) return false;
    initialized = true;
    return true;
}

// Poll stdin for a byte (non-blocking).
// If a byte is already buffered, returns immediately.
// On non-blocking stdin, read() returns -1 (EAGAIN) when no data is
// available; we only buffer when exactly 1 byte is read.
// Called once per emulated instruction cycle.
void uartPoll() {
    if (stdinReady) return;  // byte already buffered, no-op
    ssize_t n = read(STDIN_FILENO, &rxBuffer, 1);
    if (n == 1) {
        stdinReady = true;   // byte successfully received
    }
}

#endif

// ── UART register state (not stored in AvrState) ─────────────────────────────

// Internal UART registers.  These are not struct fields in AvrState because
// the UART is a peripheral with its own register state; the core only
// interacts with it through the I/O address space.
static uint8_t ubrrl  = 0;  // USART Baud Rate Register Low
static uint8_t ucsrb  = 0;  // USART Control and Status Register B
static uint8_t ucsrc  = 0;  // USART Control and Status Register C

// Synthesize the UCSRA (USART Control and Status Register A) value.
//
// In a real ATmega328P, UCSRA contains hardware status flags:
//   Bit 7: RXC  — Receive Complete (data available in UDR0)
//   Bit 6: TXC  — Transmit Complete
//   Bit 5: UDRE — USART Data Register Empty (ready for next TX byte)
//
// Our emulation:
// - TXC and UDRE are always set (1): we write to stdout immediately on
//   each uartWrite(), so the transmitter is always "ready" and "complete".
// - RXC is set when stdinReady is true: a byte has been buffered from stdin
//   and is available in the receive data register.
//
// @return synthesized UCSRA value
static uint8_t ucsra() {
    // Bit 6 = TXC (transmit complete), bit 5 = UDRE (data register empty)
    uint8_t flags = (1 << 6) | (1 << 5);
    // Bit 7 = RXC (receive complete) — only set when a byte is buffered
    if (stdinReady) flags |= (1 << 7);
    return flags;
}

// Read a UART I/O register.
//
// Handles two register mappings simultaneously:
//   ATmega328P: I/O 0x00-0x06 (data-space 0xC0-0xC6)
//   ATmega168:  I/O 0x09-0x0D (data-space 0x29-0x2D)
//
// Reading UDR0 (0x06 or 0x0C) consumes the buffered byte: stdinReady is
// cleared, returning the UART to "no data available" state.
//
// @param ioAddr — I/O address relative to the UART base (0x00-0x0D)
// @return register value, or 0x00 for unmapped addresses
uint8_t uartRead(uint8_t ioAddr) {
    switch (ioAddr) {
        // ATmega328P extended I/O mapping (data-space 0xC0-0xC6)
        case 0x00: return ucsra();        // UCSR0A — synthesized status flags
        case 0x01: return ucsrb;          // UCSR0B — stored register
        case 0x02: return ucsrc;          // UCSR0C — stored register
        case 0x04: return ubrrl;          // UBRR0L — baud rate register low
        case 0x05: return 0x00;           // UBRR0H — not used in async normal mode
        case 0x06: {                       // UDR0 — receive data register
            // Consume the buffered byte on read.  After this, subsequent
            // UCSRA reads will show RXC = 0 until the next poll catches a byte.
            if (stdinReady) { stdinReady = false; return (uint8_t)rxBuffer; }
            return 0x00;
        }

        // Legacy ATmega168 I/O mapping (data-space 0x29-0x2D)
        case 0x09: return ubrrl;          // UBRRL
        case 0x0A: return ucsrb;          // UCSRB
        case 0x0B: return ucsra();        // UCSRA
        case 0x0C: {                       // UDR
            if (stdinReady) { stdinReady = false; return (uint8_t)rxBuffer; }
            return 0x00;
        }
        case 0x0D: return ucsrc;          // UCSRC
        default:   return 0x00;
    }
}

// Write to a UART I/O register.
//
// Most registers simply store the value.  Writing to UDR0 (0x06 or 0x0C)
// transmits a byte by writing it to stdout (via std::cout).
//
// @param ioAddr — I/O address relative to the UART base (0x00-0x0D)
// @param value  — byte to write
void uartWrite(uint8_t ioAddr, uint8_t value) {
    switch (ioAddr) {
        case 0x04: ubrrl = value; break;   // UBRR0L
        case 0x01: ucsrb = value; break;   // UCSR0B
        case 0x02: ucsrc = value; break;   // UCSR0C
        // Write to UDR0: transmit the byte to stdout immediately.
        // In real hardware this would be asynchronous; our emulation
        // writes synchronously since TX timing is not modeled.
        case 0x06: std::cout.put((char)value); std::cout.flush(); break;
        // Legacy ATmega168 mapping
        case 0x09: ubrrl = value; break;
        case 0x0A: ucsrb = value; break;
        case 0x0C: std::cout.put((char)value); std::cout.flush(); break;
        case 0x0D: ucsrc = value; break;
        default: break;
    }
}
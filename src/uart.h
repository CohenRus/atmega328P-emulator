/*
 * uart.h - ATmega328P UART peripheral and host I/O bridge.
 * Supports non-blocking terminal I/O and thread-safe TUI byte injection.
 */
#pragma once
#include <cstdint>
#include <string>


// ===========================================================================
// UART helpers for the ATmega328P emulator.
//
// Two modes:
//   Headless (default): TX → stdout, RX ← stdin (non-blocking poll)
//   TUI:               TX → internal buffer (retrieve via uartPopTx),
//                       RX ← injected bytes (via uartInjectRx)
//
// All functions operate on I/O addresses (0x00–0x3F), NOT data-space addresses.
// ===========================================================================

// ---- Mode control ----

// Switch to TUI mode.  After this call, TX goes to an internal buffer
// and RX is fed via uartInjectRx() instead of stdin polling.
// Idempotent — subsequent calls are no-ops.
void uartSetTuiMode();

// ---- TX (emulator → host) ----

// Pop all buffered TX output since the last call.  Thread-safe.
// Returns the accumulated string and clears the internal buffer.
std::string uartPopTx();

// ---- RX (host → emulator) ----

// Inject a byte into the UART receive buffer.  Non-blocking:
// if the hardware single-byte buffer is already full the byte is queued.
// Thread-safe.  Safe to call from any thread.
void uartInjectRx(uint8_t byte);

// Check whether a byte is available for firmware to read (RXC flag).
// Used by the render loop to show whether input has been consumed.
bool uartHasRxPending();

// ---- Peripheral lifecycle ----

// Call once at emulator startup.  Puts stdin in non-blocking mode so
// uartPoll() never blocks.  Returns true on success.
// In TUI mode this is a no-op.
bool uartInit();

// Call once per emulated instruction cycle.  In headless mode, checks
// stdin for a pending byte.  In TUI mode, drains the injection queue
// into the single-byte hardware buffer.
void uartPoll();

// Return the value firmware sees when reading an I/O register in the
// UART range.  ioAddr is the I/O address (0x00–0x3F).
uint8_t uartRead(uint8_t ioAddr);

// Handle a write by firmware to an I/O register in the UART range.
// ioAddr is the I/O address (0x00–0x3F).
void uartWrite(uint8_t ioAddr, uint8_t value);

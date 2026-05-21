#pragma once

#include <cstdint>

// ===========================================================================
// UART helpers for the ATmega328P emulator.
//
// Polled I/O only — no interrupts.  TX writes to stdout; RX reads from
// stdin via non-blocking poll.  All functions operate on I/O addresses
// (0x00–0x3F), NOT data-space addresses.
// ===========================================================================

// Call once at emulator startup.  Puts stdin in non-blocking mode so
// uartPoll() never blocks.  Returns true on success.
bool uartInit();

// Call once per emulated instruction cycle.  Checks stdin for a pending
// byte and buffers it internally.  Safe to call when a byte is already
// buffered (no-op).
void uartPoll();

// Return the value firmware sees when reading an I/O register in the
// UART range (0x09–0x0D).  ioAddr is the I/O address (0x00–0x3F).
uint8_t uartRead(uint8_t ioAddr);

// Handle a write by firmware to an I/O register in the UART range.
// ioAddr is the I/O address (0x00–0x3F).
void uartWrite(uint8_t ioAddr, uint8_t value);

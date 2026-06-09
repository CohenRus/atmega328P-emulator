/*
 * interrupt.h - ATmega328P interrupt controller interface.
 * Defines supported vectors and the pending-interrupt dispatch API.
 */
#pragma once

#include <cstdint>

struct AvrState;

// ===========================================================================
// Interrupt controller for the currently emulated ATmega328P peripherals.
//
// Interrupt vector addresses (byte addresses into flash — matching our PC).
// This emulator uses the 4-byte vector entries emitted for ATmega328P
// firmware, so vector N starts at byte address (N - 1) * 4.
//
// When an interrupt fires, the CPU:
//   1. Pushes current PC onto the stack
//   2. Clears the I flag in SREG
//   3. Jumps to the vector's byte address
//
// RETI (executed by firmware's ISR epilogue) pops PC and sets I.
//
// Timer0 and USART vectors are wired by the executor and UART peripheral.
// ===========================================================================

// ATmega328P interrupt vector numbers (datasheet §12.1)
enum class InterruptVector : uint8_t {
    RESET          = 1,
    INT0           = 2,
    INT1           = 3,
    PCINT0         = 4,
    PCINT1         = 5,
    PCINT2         = 6,
    WDT            = 7,
    TIMER2_COMPA   = 8,
    TIMER2_COMPB   = 9,
    TIMER2_OVF     = 10,
    TIMER1_CAPT    = 11,
    TIMER1_COMPA   = 12,
    TIMER1_COMPB   = 13,
    TIMER1_OVF     = 14,
    TIMER0_COMPA   = 15,
    TIMER0_COMPB   = 16,
    TIMER0_OVF     = 17,
    USART_RX       = 19,   // USART Rx Complete
    USART_UDRE     = 20,   // USART Data Register Empty
    USART_TX       = 21,   // USART Tx Complete
};

// Bind the controller to the active CPU state. The state must outlive use of
// the controller.
void interruptSetState(AvrState* state);

// Drop all pending interrupt requests.
void interruptReset();

// Raise an interrupt.  Idempotent — setting the same vector twice is a no-op.
void interruptRaise(InterruptVector vec);

// Dispatch the highest-priority pending vector when SREG.I is set. The
// optional output identifies which peripheral flag the caller should clear.
bool interruptService(InterruptVector* serviced = nullptr);

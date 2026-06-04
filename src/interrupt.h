#pragma once

#include <cstdint>

struct AvrState;

// ===========================================================================
// Minimal Interrupt Controller for ATmega328P.
//
// Interrupt vector addresses (byte addresses into flash — matching our PC).
// On real hardware, the vector table lives at the start of flash.  Each
// entry is typically an RJMP (2 bytes), so vector N is at byte address
// (N - 1) * 2.
//
// When an interrupt fires, the CPU:
//   1. Pushes current PC onto the stack
//   2. Clears the I flag in SREG
//   3. Jumps to the vector's byte address
//
// RETI (executed by firmware's ISR epilogue) pops PC and sets I.
//
// For v1, only TIMER0_OVF is wired.  Other vectors are defined here for
// forward compatibility but are never raised.
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

void interruptSetState(AvrState* state);
void interruptReset();

// Raise an interrupt.  Idempotent — setting the same vector twice is a no-op.
void interruptRaise(InterruptVector vec);

// Call after every instruction.  Returns true if an interrupt was dispatched
// (PC was changed, so the current instruction cycle is done).
bool interruptService();

// Called inside RETI — clears the global flag that prevents the same
// interrupt from re-firing immediately.
void interruptClearFlag(InterruptVector vec);

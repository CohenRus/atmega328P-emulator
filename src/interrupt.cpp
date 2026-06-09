// ===========================================================================
// interrupt.cpp — ATmega328P interrupt controller.
//
// Manages pending interrupt vectors using a 32-bit bitmask.  Each interrupt
// vector N occupies bit (N-1).  Priority is resolved by selecting the lowest
// set bit (lowest vector number = highest priority per ATmega328P §12.1).
//
// Key design decisions:
// - The pending bitmask uses __builtin_ctzl (count trailing zeros on unsigned
//   long) to find the highest-priority pending interrupt in O(1).  Bit 0
//   corresponds to vector 1 (RESET), so priority = ctz + 1.
// - Interrupt dispatch pushes the current PC onto the stack, clears the I
//   flag (disabling nesting), and jumps to the vector's flash address.
// - interruptService() must be called after every emulated instruction.
// - TIMER0 overflow/compare-match A/B and USART RX/TX/UDRE interrupts are wired;
//   remaining vectors are defined for forward compatibility.

#include "interrupt.h"
#include <cstdio>
#include "memory.h"

// ===========================================================================
// Internal state
// ===========================================================================

// Pointer to the emulator state — set once at startup via interruptSetState().
static AvrState* g_state = nullptr;

// Bitmask of pending interrupt vectors.
// Bit 0 = vector 1 (RESET), bit 1 = vector 2 (INT0), …, bit 16 = vector 17 (TIMER0_OVF).
// Only one bit is set per raised interrupt, but multiple can be set simultaneously.
static uint32_t pending = 0;

// ===========================================================================
// Helpers
// ===========================================================================

// Convert an interrupt vector number to the corresponding byte address in flash.
// Each vector slot is 2 bytes (typically an RJMP), so:
//   Vector 1 (RESET)       -> 0x0000
//   Vector 2 (INT0)        -> 0x0002
//   Vector 17 (TIMER0_OVF) -> 0x0020
// @param vec — interrupt vector
// @return flash byte address for the vector's entry in the interrupt table
static uint16_t vectorAddr(InterruptVector vec) {
    return (static_cast<uint8_t>(vec) - 1) * 4;
}

// Find the highest-priority pending interrupt.
//
// ATmega328P §12.1: lower vector numbers have higher priority.
// RESET (vector 1) is highest, then INT0 (2), …, TIMER0_OVF (17).
//
// Uses __builtin_ctzl (count trailing zeros on unsigned long) to find the
// index of the least significant set bit in the pending mask.  Since bit 0
// maps to vector 1, the vector number is (ctz + 1).
//
// Example: if pending = 0b00000000_00000000_00000010_00000000,
// then bit 9 is set, ctz = 9, vector = 10 (TIMER2_OVF).
//
// @return vector number of the highest-priority pending interrupt, or 0 if none
static uint8_t highestPending() {
    if (pending == 0) return 0;
    // __builtin_ctzl returns the count of trailing zero bits in `pending`.
    // Bit 0 of `pending` = vector 1, so the vector number is ctz + 1.
    return static_cast<uint8_t>(__builtin_ctzl(pending)) + 1;
}

// ===========================================================================
// Public interface
// ===========================================================================

// Store the emulator state pointer.  Must be called once before any other
// interrupt functions.
// @param state — pointer to the AvrState struct
void interruptSetState(AvrState* state) {
    g_state = state;
}

// Reset the interrupt controller: clear all pending interrupt flags.
void interruptReset() {
    pending = 0;
}

// Raise (signal) an interrupt vector.
// Idempotent — calling twice for the same vector has no additional effect
// since the bit is already set in the pending mask.
// @param vec — interrupt vector to raise
void interruptRaise(InterruptVector vec) {
    uint32_t mask = 1U << (static_cast<uint8_t>(vec) - 1);
    pending |= mask;  // set the corresponding bit in the pending mask
}

// Service the highest-priority pending interrupt, if any.
//
// Dispatch sequence (matches ATmega328P hardware behavior):
// 1. Verify global interrupt flag (I in SREG) is enabled.
// 2. Find the highest-priority pending vector via highestPending().
// 3. Clear that vector's pending bit.
// 4. Push the current PC onto the stack (so RETI can return).
// 5. Clear the I flag to prevent nested interrupts.
// 6. Jump to the vector's flash address.
//
// Called after every emulated instruction.  Returns true if an interrupt
// was dispatched, signaling the executor to restart the instruction cycle
// at the new PC.
//
// @return true if an interrupt was serviced (PC changed), false otherwise
bool interruptService() {
    if (!g_state) return false;

    // Guard clause: interrupts globally disabled via SREG.I
    if (!getFlag(*g_state, SregBit::I)) return false;

    // Resolve priority: lowest vector number = highest priority
    uint8_t vecNum = highestPending();
    if (vecNum == 0) return false;

    InterruptVector vec = static_cast<InterruptVector>(vecNum);

    // Validate the vector address points to valid code.  Erased flash
    // (0xFFFF) means the interrupt vector is not configured — treat as a
    // silent no-op rather than dispatching to a guaranteed crash.
    uint16_t vecAddr = vectorAddr(vec);
    if (vecAddr + 1 >= AVR_FLASH_SIZE) {
        fprintf(stderr, "interrupt: vector %u at 0x%04X out of flash bounds — skipping\n",
                vecNum, vecAddr);
        pending &= ~(1U << (vecNum - 1));
        return false;
    }
    uint16_t vecWord = g_state->flash[vecAddr] | (g_state->flash[vecAddr + 1] << 8);
    if (vecWord == 0xFFFF) {
        fprintf(stderr, "interrupt: vector %u at 0x%04X is 0xFFFF (unprogrammed) — skipping\n",
                vecNum, vecAddr);
        pending &= ~(1U << (vecNum - 1));
        return false;
    }

    // Acknowledge: clear the pending flag so it doesn't re-fire immediately.
    pending &= ~(1U << (vecNum - 1));

    // Save return address: push the current PC so RETI can pop it back.
    pushWord(*g_state, g_state->pc);

    // Disable global interrupts (clear I flag) to prevent nesting.
    clearFlag(*g_state, SregBit::I);

    // Redirect execution to the interrupt handler.
    g_state->pc = vecAddr;

    return true;
}

// Clear a specific interrupt's pending flag (software clear).
// Used by RETI to prevent immediate re-triggering.
// @param vec — interrupt vector to clear
void interruptClearFlag(InterruptVector vec) {
    uint32_t mask = 1U << (static_cast<uint8_t>(vec) - 1);
    pending &= ~mask;
}

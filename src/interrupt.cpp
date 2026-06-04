#include "interrupt.h"
#include "timer0.h"
#include "state.h"
#include "memory.h"

// ===========================================================================
// Internal state
// ===========================================================================

static AvrState* g_state = nullptr;

// Bitmask of pending interrupt vectors.  Bit N = (1 << (vector - 1)).
static uint32_t pending = 0;

// ===========================================================================
// Helpers
// ===========================================================================

// Convert vector number to byte address in flash.
// Vector 1 (RESET) → 0x0000, Vector 2 (INT0) → 0x0002, …
static uint16_t vectorAddr(InterruptVector vec) {
    return (static_cast<uint8_t>(vec) - 1) * 2;
}

// Find the highest-priority pending interrupt (lowest vector number).
// Returns 0 if none.
static uint8_t highestPending() {
    if (pending == 0) return 0;
    // __builtin_ctz returns the index of the lowest set bit.
    // Bit 0 = vector 1, bit 1 = vector 2, … so vector = ctz + 1.
    return static_cast<uint8_t>(__builtin_ctzl(pending)) + 1;
}

// ===========================================================================
// Public interface
// ===========================================================================

void interruptSetState(AvrState* state) {
    g_state = state;
}

void interruptReset() {
    pending = 0;
}

void interruptRaise(InterruptVector vec) {
    uint32_t mask = 1U << (static_cast<uint8_t>(vec) - 1);
    pending |= mask;
}

bool interruptService() {
    if (!g_state) return false;

    // Global interrupt enable must be set
    if (!getFlag(*g_state, SregBit::I)) return false;

    uint8_t vecNum = highestPending();
    if (vecNum == 0) return false;

    InterruptVector vec = static_cast<InterruptVector>(vecNum);

    // Clear pending for this interrupt
    pending &= ~(1U << (vecNum - 1));

    // Push return address (current PC) onto stack
    pushWord(*g_state, g_state->pc);

    // Clear global interrupt flag to prevent nesting
    clearFlag(*g_state, SregBit::I);

    // Jump to the interrupt vector
    g_state->pc = vectorAddr(vec);

    return true;
}

void interruptClearFlag(InterruptVector vec) {
    uint32_t mask = 1U << (static_cast<uint8_t>(vec) - 1);
    pending &= ~mask;
}

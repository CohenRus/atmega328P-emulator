// ===========================================================================
// timer0.cpp — ATmega328P Timer/Counter 0 peripheral emulation.
//
// Timer0 is an 8-bit timer/counter with a 10-bit prescaler.  This module
// tracks all Timer0 I/O registers (TCCR0A/B, TCNT0, OCR0A/B, TIMSK0, TIFR0)
// as static variables and advances the timer on each call to timer0Tick().
//
// Key design decisions:
// - The prescaler uses an accumulator (prescaler_acc) rather than a
//   countdown register; each tick adds the instruction's cycle count,
//   and the accumulator drains in prescaler-divisor chunks.  This allows
//   variable-length instructions without per-tick overhead.
// - TIFR0 flags are set on overflow/compare-match but never cleared here;
//   clearing happens via timer0AckOverflow() (called after interrupt dispatch)
//   or via firmware writes to TIFR0 (timer0Write handles flag clearing by
//   masking off bits set in the written value).
// - Waveform generation modes (WGM) control the TOP value — CTC mode uses
//   OCR0A, others use 0xFF.
// ===========================================================================

#include "timer0.h"
#include "state.h"

// ── Per-Module State ─────────────────────────────────────────────────────────
// All Timer0 registers and the prescaler accumulator are stored as file-scope
// statics.  g_state is a back-reference for interrupt flag propagation.

static AvrState* g_state = nullptr;
static uint8_t tccr0a = 0;        // Timer/Counter Control Register A
static uint8_t tccr0b = 0;        // Timer/Counter Control Register B
static uint8_t tcnt0  = 0;        // Timer/Counter Register (current count)
static uint8_t ocr0a  = 0;        // Output Compare Register A
static uint8_t ocr0b  = 0;        // Output Compare Register B
static uint8_t timsk0 = 0;        // Timer Interrupt Mask Register
static uint8_t tifr0  = 0;        // Timer Interrupt Flag Register
static uint16_t prescaler_acc = 0; // Fractional cycle accumulator for prescaler

// ── Internal Helpers ─────────────────────────────────────────────────────────

// Extract the prescaler divider ratio from the CS02:CS01:CS00 bits (TCCR0B[2:0]).
// Returns 0 when the clock source is disabled (CS = 0 or undefined).
// @return clock divider (0 = stopped, 1, 8, 64, 256, or 1024)
static uint16_t prescalerDivider() {
    // CS bits are the low 3 bits of TCCR0B
    uint8_t cs = tccr0b & 0x07;
    switch (cs) { case 0: return 0; case 1: return 1; case 2: return 8; case 3: return 64; case 4: return 256; case 5: return 1024; default: return 0; }
}

// Reconstruct the Waveform Generation Mode (WGM[2:0]) from scattered bits:
//   WGM02 = TCCR0B[3], WGM01 = TCCR0A[1], WGM00 = TCCR0A[0]
// The result is a 3-bit value: 0=Normal, 1=PWM Phase Correct, 2=CTC, 3=Fast PWM.
// @return WGM mode (0–3)
static uint8_t wgm() {
    // Bit positions: WGM02 from TCCR0B bit 3, WGM01 from TCCR0A bit 1, WGM00 from TCCR0A bit 0
    return ((tccr0b >> 3) & 1) << 2 | ((tccr0a >> 1) & 1) << 1 | (tccr0a & 1);
}

// ── Public Interface ─────────────────────────────────────────────────────────

// Store the emulator state pointer so timer0Tick can potentially propagate
// events (e.g. for future PWM pin updates).
void timer0SetState(AvrState* state) { g_state = state; }

// Reset all Timer0 registers to their power-on defaults.
void timer0Reset() { tccr0a = 0; tccr0b = 0; tcnt0 = 0; ocr0a = 0; ocr0b = 0; timsk0 = 0; tifr0 = 0; prescaler_acc = 0; }

// Advance the timer by `cycles` CPU clock cycles.
//
// Algorithm:
// 1. If prescaler is disabled (CS == 0), return immediately.
// 2. Add cycles to the fractional accumulator.
// 3. While the accumulator has at least one full prescaler tick worth of
//    cycles, consume one tick and increment TCNT0.
// 4. On each tick, check for overflow (TCNT0 > TOP) and compare-match
//    against OCR0A and OCR0B; set the corresponding TIFR0 flags.
//
// In CTC mode, TOP = OCR0A; otherwise TOP = 0xFF.  Setting a small OCR0A
// means TCNT0 wraps quickly, generating more frequent compare-match interrupts.
//
// @param cycles — number of CPU clock cycles elapsed since last call
void timer0Tick(uint16_t cycles) {
    uint16_t div = prescalerDivider();
    if (div == 0) return;  // Timer is stopped (no clock source selected)

    // Accumulate fractional cycles.  This handles instructions with
    // different cycle counts correctly without per-tick overhead.
    prescaler_acc += cycles;

    // Select TOP: CTC mode uses OCR0A as the top; other modes use 0xFF.
    uint16_t top = (wgm() == T0_WGM_CTC) ? ocr0a : 0xFF;

    // Drain the accumulator one prescaler tick at a time.
    while (prescaler_acc >= div) {
        prescaler_acc -= div;

        // Capture previous value for compare-match detection.
        // On real hardware, OCF0x is set one timer clock cycle after
        // TCNT0 reaches the match value.
        uint8_t prev = tcnt0;

        // Compute next TCNT0 value in 16-bit space to detect overflow.
        uint16_t next = (uint16_t)tcnt0 + 1;
        if (next > top) {
            tcnt0 = 0;
            tifr0 |= (1 << T0_TOV0);
        } else {
            tcnt0 = (uint8_t)next;
        }

        // Compare-match: flag is set one cycle after TCNT0 == OCR0x.
        // Check against the *previous* TCNT0 value.
        if (prev == ocr0a) tifr0 |= (1 << T0_OCF0A);
        if (prev == ocr0b) tifr0 |= (1 << T0_OCF0B);
    }
}

// Read a Timer0 I/O register.
// Maps ATmega328P I/O addresses to the corresponding static register variables.
// @param ioAddr — I/O address (0x00–0x3F)
// @param out    — pointer to receive the register value (only set on success)
// @return true if ioAddr is a Timer0 register, false otherwise
bool timer0Read(uint8_t ioAddr, uint8_t* out) {
    switch (ioAddr) {
        case 0x24: *out = tccr0a; return true;  // TCCR0A
        case 0x25: *out = tccr0b; return true;  // TCCR0B
        case 0x26: *out = tcnt0;  return true;  // TCNT0
        case 0x27: *out = ocr0a;  return true;  // OCR0A
        case 0x28: *out = ocr0b;  return true;  // OCR0B
        case 0x4E: *out = timsk0; return true;  // TIMSK0 (extended I/O)
        case 0x15: *out = tifr0;  return true;  // TIFR0
        default: return false;
    }
}

// Write to a Timer0 I/O register.
// Special case: writing to TIFR0 (0x15) clears flags by masking off
// any bits set in `value` (hardware convention: write-1-to-clear).
// @param ioAddr — I/O address (0x00–0x3F)
// @param value  — byte to write
// @return true if ioAddr is a Timer0 register, false otherwise
bool timer0Write(uint8_t ioAddr, uint8_t value) {
    switch (ioAddr) {
        case 0x24: tccr0a = value; return true;  // TCCR0A
        case 0x25: tccr0b = value; return true;  // TCCR0B
        case 0x26: tcnt0  = value; prescaler_acc = 0; return true;  // TCNT0 (also resets prescaler)
        case 0x27: ocr0a  = value; return true;  // OCR0A
        case 0x28: ocr0b  = value; return true;  // OCR0B
        case 0x4E: timsk0 = value; return true;  // TIMSK0
        case 0x15: tifr0 &= ~value; return true; // TIFR0: write-1-to-clear
        default: return false;
    }
}

// Check whether the Timer0 overflow interrupt should be dispatched.
// True only when both the interrupt enable (TOIE0) and flag (TOV0) are set.
// @return true if Timer0 overflow interrupt is pending
bool timer0OverflowPending() { return (timsk0 & (1 << T0_TOIE0)) && (tifr0 & (1 << T0_TOV0)); }

// Acknowledge the Timer0 overflow interrupt by clearing the TOV0 flag.
// Called by the interrupt controller after dispatching TIMER0_OVF.
void timer0AckOverflow() { tifr0 &= ~(1 << T0_TOV0); }

// Check whether the Timer0 compare-match A interrupt should be dispatched.
bool timer0CompAPending() { return (timsk0 & (1 << T0_OCIE0A)) && (tifr0 & (1 << T0_OCF0A)); }

// Check whether the Timer0 compare-match B interrupt should be dispatched.
bool timer0CompBPending() { return (timsk0 & (1 << T0_OCIE0B)) && (tifr0 & (1 << T0_OCF0B)); }

// Acknowledge the Timer0 compare-match A interrupt by clearing OCF0A.
void timer0AckCompA() { tifr0 &= ~(1 << T0_OCF0A); }

// Acknowledge the Timer0 compare-match B interrupt by clearing OCF0B.
void timer0AckCompB() { tifr0 &= ~(1 << T0_OCF0B); }
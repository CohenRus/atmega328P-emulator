#pragma once

#include <cstdint>

struct AvrState;

// ===========================================================================
// Timer0 — 8-bit timer/counter with prescaler.
//
// Emulates the ATmega328P Timer/Counter 0 peripheral.  Tracks a prescaler
// accumulator, advances TCNT0, detects overflow and compare-match events,
// and sets the corresponding flag bits in TIFR0.
//
// Call timer0Tick() once per emulated instruction, passing the instruction's
// cycle count.  timer0Read/timer0Write route I/O register accesses.
// timer0SetState must be called once before any other functions.
// ===========================================================================

#define T0_TCCR0A  0x44
#define T0_TCCR0B  0x45
#define T0_TCNT0   0x46
#define T0_OCR0A   0x47
#define T0_OCR0B   0x48
#define T0_TIMSK0  0x6E
#define T0_TIFR0   0x35

// TIFR0 bit positions
#define T0_TOV0  0
#define T0_OCF0A 1
#define T0_OCF0B 2

// TIMSK0 bit positions
#define T0_TOIE0  0
#define T0_OCIE0A 1
#define T0_OCIE0B 2

// Waveform Generation Mode bits (2:0 = WGM02:WGM01:WGM00)
#define T0_WGM_NORMAL  0
#define T0_WGM_PWM_PC  1
#define T0_WGM_CTC     2
#define T0_WGM_FAST_PWM 3

void timer0SetState(AvrState* state);
void timer0Reset();
void timer0Tick(uint16_t cycles);

// Returns true if ioAddr falls in the Timer0 register range.
bool timer0Read (uint8_t ioAddr, uint8_t* out);
bool timer0Write(uint8_t ioAddr, uint8_t value);

// True when Timer0 overflow interrupt is both enabled and flagged.
// Used by the interrupt controller to decide whether to dispatch.
bool timer0OverflowPending();

// Clear TOV0 flag (hardware auto-clears this when TIMER0_OVF is serviced).
void timer0AckOverflow();

// True when Timer0 compare-match A interrupt is both enabled and flagged.
bool timer0CompAPending();

// True when Timer0 compare-match B interrupt is both enabled and flagged.
bool timer0CompBPending();

// Clear OCF0A / OCF0B flags after interrupt dispatch.
void timer0AckCompA();
void timer0AckCompB();

#include "timer0.h"
#include "state.h"

static AvrState* g_state = nullptr;
static uint8_t tccr0a = 0;
static uint8_t tccr0b = 0;
static uint8_t tcnt0  = 0;
static uint8_t ocr0a  = 0;
static uint8_t ocr0b  = 0;
static uint8_t timsk0 = 0;
static uint8_t tifr0  = 0;
static uint16_t prescaler_acc = 0;

static uint16_t prescalerDivider() {
    uint8_t cs = tccr0b & 0x07;
    switch (cs) { case 0: return 0; case 1: return 1; case 2: return 8; case 3: return 64; case 4: return 256; case 5: return 1024; default: return 0; }
}
static uint8_t wgm() { return ((tccr0b >> 3) & 1) << 2 | ((tccr0a >> 1) & 1) << 1 | (tccr0a & 1); }

void timer0SetState(AvrState* state) { g_state = state; }
void timer0Reset() { tccr0a = 0; tccr0b = 0; tcnt0 = 0; ocr0a = 0; ocr0b = 0; timsk0 = 0; tifr0 = 0; prescaler_acc = 0; }

void timer0Tick(uint16_t cycles) {
    uint16_t div = prescalerDivider();
    if (div == 0) return;
    prescaler_acc += cycles;
    uint16_t top = (wgm() == T0_WGM_CTC) ? ocr0a : 0xFF;
    while (prescaler_acc >= div) {
        prescaler_acc -= div;
        uint16_t next = (uint16_t)tcnt0 + 1;
        if (next > top) { tcnt0 = 0; tifr0 |= (1 << T0_TOV0); }
        else { tcnt0 = (uint8_t)next; }
        if (tcnt0 == ocr0a) tifr0 |= (1 << T0_OCF0A);
        if (tcnt0 == ocr0b) tifr0 |= (1 << T0_OCF0B);
    }
}

bool timer0Read(uint8_t ioAddr, uint8_t* out) {
    switch (ioAddr) {
        case 0x24: *out = tccr0a; return true;
        case 0x25: *out = tccr0b; return true;
        case 0x26: *out = tcnt0;  return true;
        case 0x27: *out = ocr0a;  return true;
        case 0x28: *out = ocr0b;  return true;
        case 0x4E: *out = timsk0; return true;
        case 0x15: *out = tifr0;  return true;
        default: return false;
    }
}

bool timer0Write(uint8_t ioAddr, uint8_t value) {
    switch (ioAddr) {
        case 0x24: tccr0a = value; return true;
        case 0x25: tccr0b = value; return true;
        case 0x26: tcnt0  = value; return true;
        case 0x27: ocr0a  = value; return true;
        case 0x28: ocr0b  = value; return true;
        case 0x4E: timsk0 = value; return true;
        case 0x15: tifr0 &= ~value; return true;
        default: return false;
    }
}

bool timer0OverflowPending() { return (timsk0 & (1 << T0_TOIE0)) && (tifr0 & (1 << T0_TOV0)); }
void timer0AckOverflow() { tifr0 &= ~(1 << T0_TOV0); }

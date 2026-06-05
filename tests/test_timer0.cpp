#include "catch_amalgamated.hpp"
#include "state.h"
#include "memory.h"
#include "executor.h"
#include "timer0.h"
#include "interrupt.h"

// ===========================================================================
// Helpers
// ===========================================================================

static AvrState freshState() {
    AvrState s{};
    clearState(s);
    s.sp = 0x08FF;
    return s;
}

// Write a Timer0 register through the data-space memory map.
// ioAddr is the I/O address (0x00–0x3F); timer0 registers sit at I/O 0x24–0x28, 0x4E, 0x15.
static void writeTimerIO(AvrState& s, uint8_t ioAddr, uint8_t val) {
    writeDataByte(s, 0x20 + ioAddr, val);
}

// Read a Timer0 register through the data-space memory map.
static uint8_t readTimerIO(AvrState& s, uint8_t ioAddr) {
    return readDataByte(s, 0x20 + ioAddr);
}

// ===========================================================================
// Timer0 — Register I/O
// ===========================================================================

TEST_CASE("Timer0: TCNT0 read/write through data-space", "[timer0][io]") {
    auto s = freshState();
    timer0SetState(&s);
    timer0Reset();

    writeTimerIO(s, 0x26, 0xAB);  // TCNT0
    REQUIRE(readTimerIO(s, 0x26) == 0xAB);
}

TEST_CASE("Timer0: TCCR0B read/write through data-space", "[timer0][io]") {
    auto s = freshState();
    timer0SetState(&s);
    timer0Reset();

    writeTimerIO(s, 0x25, 0x03);  // TCCR0B — prescaler /64
    REQUIRE(readTimerIO(s, 0x25) == 0x03);
}

TEST_CASE("Timer0: TCCR0A read/write through data-space", "[timer0][io]") {
    auto s = freshState();
    timer0SetState(&s);
    timer0Reset();

    writeTimerIO(s, 0x24, 0x42);  // TCCR0A
    REQUIRE(readTimerIO(s, 0x24) == 0x42);
}


TEST_CASE("Timer0: OCR0A read/write through data-space", "[timer0][io]") {
    auto s = freshState();
    timer0SetState(&s);
    timer0Reset();

    writeTimerIO(s, 0x27, 0x7F);
    REQUIRE(readTimerIO(s, 0x27) == 0x7F);
}

TEST_CASE("Timer0: TIMSK0 read/write through data-space", "[timer0][io]") {
    auto s = freshState();
    timer0SetState(&s);
    timer0Reset();

    writeTimerIO(s, 0x4E, 0x01);  // TOIE0 = 1
    REQUIRE(readTimerIO(s, 0x4E) == 0x01);
}

TEST_CASE("Timer0: TIFR0 read/write clears on write-1", "[timer0][io]") {
    auto s = freshState();
    timer0SetState(&s);
    timer0Reset();

    writeTimerIO(s, 0x25, 0x03);  // prescaler /64
    // 256 TCNT0 tick events: 0→1→…→255→0. At /64 = 16384 cycles.
    timer0Tick(16384);

    uint8_t tifr = readTimerIO(s, 0x15);
    REQUIRE((tifr & (1 << T0_TOV0)) != 0);  // TOV0 set

    // Write 1 to clear TOV0
    writeTimerIO(s, 0x15, (1 << T0_TOV0));
    tifr = readTimerIO(s, 0x15);
    REQUIRE((tifr & (1 << T0_TOV0)) == 0);  // TOV0 cleared
}

// ===========================================================================
// Timer0 — Prescaler
// ===========================================================================

TEST_CASE("Timer0: timer stopped when CS=0", "[timer0][prescaler]") {
    auto s = freshState();
    timer0SetState(&s);
    timer0Reset();

    writeTimerIO(s, 0x25, 0x00);  // CS02:CS00 = 0 → timer stopped
    writeTimerIO(s, 0x26, 0x00);  // TCNT0 = 0

    timer0Tick(1000);  // lots of cycles

    REQUIRE(readTimerIO(s, 0x26) == 0x00);  // TCNT0 unchanged
}

TEST_CASE("Timer0: prescaler /1 advances TCNT0 directly", "[timer0][prescaler]") {
    auto s = freshState();
    timer0SetState(&s);
    timer0Reset();

    writeTimerIO(s, 0x25, 0x01);  // prescaler /1
    writeTimerIO(s, 0x26, 0x00);
    timer0Tick(5);
    REQUIRE(readTimerIO(s, 0x26) == 5);  // TCNT0 = cycles / 1 = 5
}

TEST_CASE("Timer0: prescaler /8 accumulates before advancing", "[timer0][prescaler]") {
    auto s = freshState();
    timer0SetState(&s);
    timer0Reset();

    writeTimerIO(s, 0x25, 0x02);  // prescaler /8
    writeTimerIO(s, 0x26, 0x00);

    timer0Tick(7);
    REQUIRE(readTimerIO(s, 0x26) == 0);  // not enough cycles yet

    timer0Tick(1);
    REQUIRE(readTimerIO(s, 0x26) == 1);  // accum hit 8 → tick

    timer0Tick(20);  // 2 full ticks + 4 remainder
    REQUIRE(readTimerIO(s, 0x26) == 3);
}

TEST_CASE("Timer0: prescaler /64 advances at correct rate", "[timer0][prescaler]") {
    auto s = freshState();
    timer0SetState(&s);
    timer0Reset();

    writeTimerIO(s, 0x25, 0x03);  // prescaler /64
    writeTimerIO(s, 0x26, 0x00);

    timer0Tick(256);  // 256 / 64 = 4 ticks
    REQUIRE(readTimerIO(s, 0x26) == 4);
}

// ===========================================================================
// Timer0 — Overflow
// ===========================================================================

TEST_CASE("Timer0: normal mode overflow sets TOV0", "[timer0][overflow]") {
    auto s = freshState();
    timer0SetState(&s);
    timer0Reset();

    writeTimerIO(s, 0x25, 0x01);   // prescaler /1

    // Tick to 0xFF
    timer0Tick(0xFF);
    REQUIRE(readTimerIO(s, 0x26) == 0xFF);
    REQUIRE((readTimerIO(s, 0x15) & (1 << T0_TOV0)) == 0);

    // One more tick → overflow to 0x00, TOV0 set
    timer0Tick(1);
    REQUIRE(readTimerIO(s, 0x26) == 0x00);
    REQUIRE((readTimerIO(s, 0x15) & (1 << T0_TOV0)) != 0);
}

TEST_CASE("Timer0: overflow with prescaler /64", "[timer0][overflow]") {
    auto s = freshState();
    timer0SetState(&s);
    timer0Reset();

    writeTimerIO(s, 0x25, 0x03);   // prescaler /64
    writeTimerIO(s, 0x26, 0xFE);

    // Need 2 more TCNT0 ticks: FE → FF → 00.  That's 2 * 64 = 128 cycles.
    timer0Tick(128);
    REQUIRE(readTimerIO(s, 0x26) == 0x00);
    REQUIRE((readTimerIO(s, 0x15) & (1 << T0_TOV0)) != 0);
}

// ===========================================================================
// Timer0 — CTC mode
// ===========================================================================

TEST_CASE("Timer0: CTC mode resets TCNT0 on OCR0A match", "[timer0][ctc]") {
    auto s = freshState();
    timer0SetState(&s);
    timer0Reset();

    // WGM2:0 = 010 (CTC). WGM02=0 (TCCR0B[3]=0), WGM01=1 (TCCR0A[1]=1), WGM00=0 (TCCR0A[0]=0)
    writeTimerIO(s, 0x24, (1 << 1));  // TCCR0A: WGM01=1
    writeTimerIO(s, 0x25, 0x01);      // TCCR0B: CS=1 (prescaler /1), WGM02=0
    writeTimerIO(s, 0x27, 0x40);      // OCR0A = 64
    writeTimerIO(s, 0x26, 0x00);      // TCNT0 = 0
    timer0Tick(0x40);  // give 64 cycles
    writeTimerIO(s, 0x26, 0x00);      // TCNT0 = 0

    // 65 cycles at prescaler /1: 64 ticks reach OCR0A, the 65th tick detects
    // the match, sets OCF0A, and wraps TCNT0 to 0.
    timer0Tick(65);
    REQUIRE(readTimerIO(s, 0x26) == 0x00);
    REQUIRE((readTimerIO(s, 0x15) & (1 << T0_OCF0A)) != 0);
}
TEST_CASE("Timer0: CTC mode does not set TOV0 on wrap", "[timer0][ctc]") {
    auto s = freshState();
    timer0SetState(&s);
    timer0Reset();
    // WGM2:0 = 010 (CTC). WGM01=1 (TCCR0A[1]=1)
    writeTimerIO(s, 0x24, (1 << 1));  // TCCR0A: WGM01=1
    writeTimerIO(s, 0x25, 0x01);      // TCCR0B: CS=1 (prescaler /1)
    writeTimerIO(s, 0x27, 0x03);      // OCR0A = 3 (small TOP for quick test)
    writeTimerIO(s, 0x26, 0x02);      // TCNT0 = 2
    // Tick 1: TCNT0 2→3 (reaches OCR0A), Tick 2: 3→0 (wraps, OCF0A fires)
    // TOV0 must NOT be set even though TCNT0 wrapped.
    timer0Tick(2);
    REQUIRE(readTimerIO(s, 0x26) == 0x00);
    uint8_t tifr = readTimerIO(s, 0x15);
    REQUIRE((tifr & (1 << T0_OCF0A)) != 0);  // OCF0A must be set
    REQUIRE((tifr & (1 << T0_TOV0)) == 0);   // TOV0 must NOT be set
}
// ===========================================================================
// Timer0 — Overflow pending flag
// ===========================================================================

TEST_CASE("Timer0: overflowPending false when interrupt disabled", "[timer0][overflow]") {
    auto s = freshState();
    timer0SetState(&s);
    timer0Reset();

    writeTimerIO(s, 0x25, 0x01);   // prescaler /1, TOIE0 not set
    timer0Tick(256);               // force overflow

    // TOV0 is set in TIFR0, but TOIE0 is not set in TIMSK0
    REQUIRE((readTimerIO(s, 0x15) & (1 << T0_TOV0)) != 0);
    REQUIRE_FALSE(timer0OverflowPending());
}

TEST_CASE("Timer0: overflowPending true when interrupt enabled", "[timer0][overflow]") {
    auto s = freshState();
    timer0SetState(&s);
    timer0Reset();

    writeTimerIO(s, 0x4E, 0x01);   // TOIE0 = 1
    writeTimerIO(s, 0x25, 0x01);   // prescaler /1
    timer0Tick(256);               // force overflow

    REQUIRE(timer0OverflowPending());
}

// ===========================================================================
// Interrupt Controller
// ===========================================================================

TEST_CASE("Interrupt: raise and service dispatches to correct vector", "[interrupt]") {
    auto s = freshState();
    interruptSetState(&s);
    interruptReset();

    // Enable interrupts globally
    setFlag(s, SregBit::I);
    s.pc = 0x0100;  // arbitrary current PC

    interruptRaise(InterruptVector::TIMER0_OVF);

    bool dispatched = interruptService();
    REQUIRE(dispatched);

    // PC should now point to TIMER0_OVF vector: (17 - 1) * 4 = 64 = 0x0040
    REQUIRE(s.pc == 0x0040);

    // I flag should be cleared (no nesting)
    REQUIRE_FALSE(getFlag(s, SregBit::I));

    // Return address (old PC) should be on the stack
    uint16_t retAddr = popWord(s);
    REQUIRE(retAddr == 0x0100);
}

TEST_CASE("Interrupt: no dispatch when I=0", "[interrupt]") {
    auto s = freshState();
    interruptSetState(&s);
    interruptReset();

    clearFlag(s, SregBit::I);  // I=0
    s.pc = 0x0100;

    interruptRaise(InterruptVector::TIMER0_OVF);

    bool dispatched = interruptService();
    REQUIRE_FALSE(dispatched);
    REQUIRE(s.pc == 0x0100);   // PC unchanged
}

TEST_CASE("Interrupt: no dispatch with no interrupts pending", "[interrupt]") {
    auto s = freshState();
    interruptSetState(&s);
    interruptReset();

    setFlag(s, SregBit::I);
    s.pc = 0x0100;

    // Nothing raised
    bool dispatched = interruptService();
    REQUIRE_FALSE(dispatched);
    REQUIRE(s.pc == 0x0100);
}

TEST_CASE("Interrupt: RETI restores PC and sets I flag", "[interrupt]") {
    auto s = freshState();
    interruptSetState(&s);
    interruptReset();

    // Simulate an interrupt dispatch: push return addr, clear I
    s.pc = 0x0040;  // inside ISR
    clearFlag(s, SregBit::I);
    pushWord(s, 0x01A0);  // return address

    // Execute RETI (what timer ISR would call)
    executeRETI(s);

    REQUIRE(s.pc == 0x01A0);          // PC restored
    REQUIRE(getFlag(s, SregBit::I));  // I flag set
}

TEST_CASE("Interrupt: highest priority wins", "[interrupt]") {
    auto s = freshState();
    interruptSetState(&s);
    interruptReset();

    setFlag(s, SregBit::I);
    s.pc = 0x0100;

    // Raise lower priority first, then higher
    interruptRaise(InterruptVector::TIMER0_OVF);     // vector 17
    interruptRaise(InterruptVector::TIMER0_COMPA);   // vector 15 (higher priority! lower number)

    bool dispatched = interruptService();
    REQUIRE(dispatched);
    // TIMER0_COMPA at (15-1)*4 = 56 = 0x0038
    REQUIRE(s.pc == 0x0038);

    // After first dispatch, the other should still be pending
    // (I is cleared, so second dispatch won't happen until I is set again)
    dispatched = interruptService();
    REQUIRE_FALSE(dispatched);  // I=0 blocks it

    // Set I again, should now dispatch TIMER0_OVF
    setFlag(s, SregBit::I);
    dispatched = interruptService();
    REQUIRE(dispatched);
    REQUIRE(s.pc == 0x0040);   // TIMER0_OVF
}

// ===========================================================================
// End-to-end: Timer0 overflow → interrupt
// ===========================================================================

TEST_CASE("E2E: Timer0 overflow raises interrupt, ISR runs", "[timer0][interrupt][e2e]") {
    auto s = freshState();
    timer0SetState(&s);
    timer0Reset();
    interruptSetState(&s);
    interruptReset();

    // Set up Timer0: normal mode, prescaler /64, overflow interrupt enabled
    // Set up Timer0: normal mode, prescaler /64, overflow interrupt enabled
    writeTimerIO(s, 0x24, 0x00);   // TCCR0A: normal mode
    writeTimerIO(s, 0x25, 0x03);   // TCCR0B: prescaler /64
    writeTimerIO(s, 0x4E, 0x01);   // TIMSK0: TOIE0 = 1
    writeTimerIO(s, 0x26, 0xFE);   // TCNT0 = 254
    // Set up interrupt controller: I=1, put a RETI at vector 0x0040 in flash
    setFlag(s, SregBit::I);
    s.pc = 0x0100;

    // Place a RETI instruction (0x9518) at the TIMER0_OVF vector address
    // TIMER0_OVF = vector 17 → byte address 0x0040
    s.flash[0x0040] = 0x18;
    s.flash[0x0041] = 0x95;  // RETI = 0x9518 (little-endian: low byte 0x18, high byte 0x95)

    // Advance timer: 2 ticks to overflow (FE→FF→00). At /64 prescaler, 128 cycles.
    timer0Tick(128);

    // Timer0 overflow should be pending
    REQUIRE(timer0OverflowPending());

    // Wire to interrupt controller
    interruptRaise(InterruptVector::TIMER0_OVF);

    // Service the interrupt — should jump to ISR at 0x0040
    bool dispatched = interruptService();
    REQUIRE(dispatched);
    REQUIRE(s.pc == 0x0040);
    timer0AckOverflow();

    // ISR is just a RETI. Let's "execute" it by fetching and running the instruction.
    // In a real run, executeProgram would handle this. Here we simulate one instruction.
    uint16_t isrInstr = s.flash[s.pc] | (s.flash[s.pc + 1] << 8);
    s.pc += 2;
    Opcode isrOp;
    REQUIRE(decodeInstruction(isrInstr, isrOp));
    REQUIRE(isrOp.op == AvrOp::RETI);

    // Execute RETI
    executeRETI(s);

    // After RETI, PC should be back to 0x0100 and I flag set
    REQUIRE(s.pc == 0x0100);
    REQUIRE(getFlag(s, SregBit::I));

    // Timer0 overflow flag should have been acked
    REQUIRE_FALSE(timer0OverflowPending());
}

TEST_CASE("E2E: timer0OverflowPending false when TOIE0 disabled", "[timer0][interrupt][e2e]") {
    auto s = freshState();
    timer0SetState(&s);
    timer0Reset();

    writeTimerIO(s, 0x25, 0x01);   // prescaler /1
    // Note: TIMSK0 TOIE0 not set (default 0)

    timer0Tick(256);  // overflow
    REQUIRE((readTimerIO(s, 0x15) & (1 << T0_TOV0)) != 0);  // TOV0 set in TIFR0
    REQUIRE_FALSE(timer0OverflowPending());  // but TOIE0 is 0, so no pending
}

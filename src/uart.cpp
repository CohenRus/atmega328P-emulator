#include "uart.h"

#include <cstdio>
#include <iostream>

// ── Platform-specific non-blocking stdin ─────────────────────────────────────

#if defined(_WIN32)
#  include <conio.h>

static bool stdinReady = false;
static char rxBuffer   = 0;

bool uartInit() {
    // Windows console I/O is naturally non-blocking with _kbhit()
    return true;
}

void uartPoll() {
    if (stdinReady) return;               // already have a byte buffered
    if (_kbhit()) {
        rxBuffer   = (char)_getch();
        stdinReady = true;
    }
}

#else  // POSIX (Linux / macOS)
#  include <fcntl.h>
#  include <unistd.h>

static bool stdinReady = false;
static char rxBuffer   = 0;
static bool initialized = false;

bool uartInit() {
    if (initialized) return true;
    int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    if (flags == -1) return false;
    if (fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK) == -1) return false;
    initialized = true;
    return true;
}

void uartPoll() {
    if (stdinReady) return;
    ssize_t n = read(STDIN_FILENO, &rxBuffer, 1);
    if (n == 1) {
        stdinReady = true;
    }
}

#endif

// ── UART register state (not stored in AvrState) ─────────────────────────────

static uint8_t ubrrl  = 0;   // I/O 0x09 — UBRRL
static uint8_t ucsrb  = 0;   // I/O 0x0A — UCSRB
static uint8_t ucsrc  = 0;   // I/O 0x0D — UCSRC (set via I/O 0x0A with URSEL=1 on real HW; simplified)

// ── Public helpers ───────────────────────────────────────────────────────────

uint8_t uartRead(uint8_t ioAddr) {
    switch (ioAddr) {
        case 0x09: return ubrrl;          // UBRRL
        case 0x0A: return ucsrb;          // UCSRB

        case 0x0B: {                      // UCSRA
            // RXC=bit7, TXC=bit6, UDRE=bit5 — others always 0
            uint8_t flags = (1 << 6)      // TXC  — transmit complete
                          | (1 << 5);     // UDRE — data register empty
            if (stdinReady) flags |= (1 << 7);  // RXC — receive complete
            return flags;
        }

        case 0x0C: {                      // UDR
            if (stdinReady) {
                stdinReady = false;
                return (uint8_t)rxBuffer;
            }
            return 0x00;  // reading empty UDR returns 0 (undefined on real HW)
        }

        case 0x0D: return ucsrc;          // UCSRC
        default:   return 0x00;
    }
}

void uartWrite(uint8_t ioAddr, uint8_t value) {
    switch (ioAddr) {
        case 0x09: ubrrl = value; break;  // UBRRL
        case 0x0A: ucsrb = value; break;  // UCSRB
        case 0x0C:                        // UDR — transmit
            std::cout.put((char)value);
            std::cout.flush();
            break;
        case 0x0D: ucsrc = value; break;  // UCSRC
        default:   break;
    }
}

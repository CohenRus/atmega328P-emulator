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

static uint8_t ubrrl  = 0;
static uint8_t ucsrb  = 0;
static uint8_t ucsrc  = 0;

static uint8_t ucsra() {
    uint8_t flags = (1 << 6) | (1 << 5);
    if (stdinReady) flags |= (1 << 7);
    return flags;
}

uint8_t uartRead(uint8_t ioAddr) {
    switch (ioAddr) {
        // ATmega328P extended I/O mapping (data-space 0xC0-0xC6)
        case 0x00: return ucsra();        // UCSR0A
        case 0x01: return ucsrb;          // UCSR0B
        case 0x02: return ucsrc;          // UCSR0C
        case 0x04: return ubrrl;          // UBRR0L
        case 0x05: return 0x00;           // UBRR0H (not used in async normal mode)
        case 0x06: {                       // UDR0
            if (stdinReady) { stdinReady = false; return (uint8_t)rxBuffer; }
            return 0x00;
        }

        // Legacy ATmega168 I/O mapping (data-space 0x29-0x2D)
        case 0x09: return ubrrl;          // UBRRL
        case 0x0A: return ucsrb;          // UCSRB
        case 0x0B: return ucsra();        // UCSRA
        case 0x0C: {                       // UDR
            if (stdinReady) { stdinReady = false; return (uint8_t)rxBuffer; }
            return 0x00;
        }
        case 0x0D: return ucsrc;          // UCSRC
        default:   return 0x00;
    }
}

void uartWrite(uint8_t ioAddr, uint8_t value) {
    switch (ioAddr) {
        case 0x04: ubrrl = value; break;
        case 0x01: ucsrb = value; break;
        case 0x02: ucsrc = value; break;
        case 0x06: std::cout.put((char)value); std::cout.flush(); break;
        case 0x09: ubrrl = value; break;
        case 0x0A: ucsrb = value; break;
        case 0x0C: std::cout.put((char)value); std::cout.flush(); break;
        case 0x0D: ucsrc = value; break;
        default: break;
    }
}
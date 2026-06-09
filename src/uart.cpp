// ===========================================================================
// uart.cpp — UART peripheral emulation for ATmega328P.
//
// Two modes:
//   Headless (default): TX → stdout, RX ← stdin (non-blocking poll)
//   TUI:               TX → internal buffer (retrieve via uartPopTx),
//                       RX ← injected bytes (via uartInjectRx)
//
// In headless mode, stdin is set to non-blocking via fcntl (POSIX) or
// _kbhit (Windows).  In TUI mode, TX is appended to a mutex-protected
// string and RX is pulled from an injected-byte queue.
//
// UART register state (UBRRL, UCSRB, UCSRC) is stored outside AvrState
// because these registers are not directly accessed by the core — only via
// the data-space I/O map in memory.cpp.
// ===========================================================================

#include "uart.h"
#include "interrupt.h"

#include <atomic>
#include <iostream>
#include <mutex>
#include <queue>
#include <string>

// ── Platform-specific non-blocking stdin (headless only) ────────────────────

#if defined(_WIN32)
#  include <conio.h>
#else
#  include <fcntl.h>
#  include <unistd.h>

static bool initialized = false;
#endif

// ── Headless-mode receive buffer ────────────────────────────────────────────
// (Not used in TUI mode — rxHwReady/rxHwByte replace them.)

static bool stdinReady = false;
static char rxBuffer   = 0;

// ── TUI mode — thread-safe I/O buffers ──────────────────────────────────────

static std::atomic<bool>  tuiMode(false);
static std::mutex         tuiMutex;
static std::string        txBuffer;         // TX output since last pop
static std::queue<uint8_t> rxQueue;         // injected bytes waiting
static bool               rxHwReady = false; // single-byte HW buffer full
static char               rxHwByte  = 0;

void uartSetTuiMode() { tuiMode.store(true); }

std::string uartPopTx() {
    std::lock_guard<std::mutex> lock(tuiMutex);
    std::string result;
    result.swap(txBuffer);
    return result;
}

void uartInjectRx(uint8_t byte) {
    std::lock_guard<std::mutex> lock(tuiMutex);
    rxQueue.push(byte);
}

bool uartHasRxPending() {
    std::lock_guard<std::mutex> lock(tuiMutex);
    return rxHwReady || !rxQueue.empty();
}

// ── UART register state ─────────────────────────────────────────────────────

static uint8_t ubrrl = 0;
static uint8_t ucsrb = 0;
static uint8_t ucsrc = 0;

// ── uartInit ────────────────────────────────────────────────────────────────

bool uartInit() {
    if (tuiMode.load()) return true;  // TUI mode — no stdin setup needed
#if defined(_WIN32)
    return true;
#else
    if (initialized) return true;
    int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    if (flags == -1) return false;
    if (fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK) == -1) return false;
    initialized = true;
    return true;
#endif
}

// ── uartPoll ────────────────────────────────────────────────────────────────

void uartPoll() {
    bool byteReady = false;

    if (tuiMode.load()) {
        std::lock_guard<std::mutex> lock(tuiMutex);
        if (!rxHwReady && !rxQueue.empty()) {
            rxHwByte  = (char)rxQueue.front();
            rxQueue.pop();
            rxHwReady = true;
            byteReady = true;
        }
    } else {
        // Headless mode
        if (!stdinReady) {
#if defined(_WIN32)
            if (_kbhit()) {
                rxBuffer   = (char)_getch();
                stdinReady = true;
                byteReady  = true;
            }
#else
            ssize_t n = read(STDIN_FILENO, &rxBuffer, 1);
            if (n == 1) {
                stdinReady = true;
                byteReady  = true;
            }
#endif
        }
    }

    // If a byte was just buffered and RX Complete Interrupt is enabled
    // (UCSR0B bit 7), fire the USART_RX interrupt so firmware that uses
    // interrupt-driven serial (e.g. Arduino Serial) sees the byte.
    if (byteReady && (ucsrb & (1 << 7))) {
        interruptRaise(InterruptVector::USART_RX);
    }
}

// Synthesize UCSRA: bits 6 (TXC) and 5 (UDRE) always set.
// Bit 7 (RXC) set when a byte is buffered (headless: stdinReady, TUI: rxHwReady).
static uint8_t ucsra() {
    uint8_t flags = (1 << 6) | (1 << 5);
    if (tuiMode.load()) {
        std::lock_guard<std::mutex> lock(tuiMutex);
        if (rxHwReady) flags |= (1 << 7);
    } else {
        if (stdinReady) flags |= (1 << 7);
    }
    return flags;
}

// ── uartRead ────────────────────────────────────────────────────────────────
uint8_t uartRead(uint8_t ioAddr) {
    auto consumeByte = [&]() -> uint8_t {
        if (tuiMode.load()) {
            std::lock_guard<std::mutex> lock(tuiMutex);
            if (rxHwReady) { rxHwReady = false; return (uint8_t)rxHwByte; }
        } else {
            if (stdinReady) { stdinReady = false; return (uint8_t)rxBuffer; }
        }
        return 0x00;
    };

    switch (ioAddr) {
        // ATmega328P extended I/O mapping (data-space 0xC0-0xC6)
        case 0x00: return ucsra();
        case 0x01: return ucsrb;
        case 0x02: return ucsrc;
        case 0x04: return ubrrl;
        case 0x05: return 0x00;
        case 0x06: return consumeByte();

        // Legacy ATmega168 I/O mapping (data-space 0x29-0x2D)
        case 0x09: return ubrrl;
        case 0x0A: return ucsrb;
        case 0x0B: return ucsra();
        case 0x0C: return consumeByte();
        case 0x0D: return ucsrc;
        default:   return 0x00;
    }
}

// ── uartWrite ───────────────────────────────────────────────────────────────

void uartWrite(uint8_t ioAddr, uint8_t value) {
    switch (ioAddr) {
        // ATmega328P extended I/O mapping (data-space 0xC0-0xC6)
        case 0x00:               break; // UCSR0A — mostly read-only; TXCn write-1-to-clear is harmless to ignore
        case 0x01:                       // UCSR0B
            ucsrb = value;
            // If UDRIE0 was just enabled and UDRE is already set (data register
            // is always empty in the emulator), raise the UDRE interrupt.
            if (value & (1 << 5)) {
                interruptRaise(InterruptVector::USART_UDRE);
            }
            break;
        case 0x02: ucsrc = value; break; // UCSR0C
        case 0x03:               break; // reserved
        case 0x04: ubrrl = value; break; // UBRR0L
        case 0x05:               break; // UBRR0H — baud rate high byte, ignored (common baud rates fit in 8 bits)
        case 0x06:                       // UDR0 — transmit
            if (tuiMode.load()) {
                std::lock_guard<std::mutex> lock(tuiMutex);
                txBuffer += (char)value;
            } else {
                std::cout.put((char)value);
                std::cout.flush();
            }
            // TX complete: after transmitting a byte, raise TXC interrupt if enabled.
            if (ucsrb & (1 << 6)) {
                interruptRaise(InterruptVector::USART_TX);
            }
            // UDRE becomes set again after transmit (data register now empty).
            if (ucsrb & (1 << 5)) {
                interruptRaise(InterruptVector::USART_UDRE);
            }
            break;
        // Legacy ATmega168 I/O mapping (data-space 0x29-0x2D)
        case 0x09: ubrrl = value; break; // UBRRL
        case 0x0A:                       // UCSRB
            ucsrb = value;
            if (value & (1 << 5)) {
                interruptRaise(InterruptVector::USART_UDRE);
            }
            break;
        case 0x0B:               break; // UCSRA — read-only
        case 0x0C:                       // UDR — transmit
            if (tuiMode.load()) {
                std::lock_guard<std::mutex> lock(tuiMutex);
                txBuffer += (char)value;
            } else {
                std::cout.put((char)value);
                std::cout.flush();
            }
            if (ucsrb & (1 << 6)) {
                interruptRaise(InterruptVector::USART_TX);
            }
            if (ucsrb & (1 << 5)) {
                interruptRaise(InterruptVector::USART_UDRE);
            }
            break;
        case 0x0D: ucsrc = value; break; // UCSRC
        default: break;
    }
}
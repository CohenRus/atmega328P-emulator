/*
 * main.cpp — ATmega328P emulator entry point with TUI.
 *
 * Usage:
 *   emulator [firmware.elf]    — load and run with TUI
 *   emulator                    — fuzzy-find .elf with fzf, then run
 *
 * The TUI uses a tabbed layout:
 *   Serial     — UART TX output + RX input prompt
 *   Registers  — GP registers, PC, SP, SREG flags, cycle count
 *   Disasm     — instructions around current PC
 *
 * Press Tab/Shift+Tab to switch tabs, Escape to stop, Enter to send input.
 */
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

#include "decoder.h"
#include "disasm.h"
#include "error.h"
#include "executor.h"
#include "loader.h"
#include "state.h"
#include "uart.h"

#include "ftxui/component/app.hpp"
#include "ftxui/component/component.hpp"
#include "ftxui/component/component_base.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/component/screen_interactive.hpp"
#include "ftxui/dom/elements.hpp"

using namespace ftxui;


// --- helpers ---

static void usage() {
  std::cerr << "usage: emulator [firmware.elf]\n";
}

static bool validateFile(const char* fileName) {
  std::ifstream file(fileName, std::ios::binary);
  if (!file.is_open()) return false;
  char mb[4];
  if (!file.read(mb, 4)) return false;
  return (mb[0] == 0x7f && mb[1] == 'E' && mb[2] == 'L' && mb[3] == 'F');
}

// Run fzf on all .elf files found recursively under the current directory.
// Returns the selected path, or empty string on cancel/error.
static std::string fzfSelectElf() {
  // Build the find + fzf pipeline.  fzf reads from stdin.
  std::string cmd =
    "find . -name '*.elf' -type f 2>/dev/null | fzf --prompt='Select firmware .elf> ' --height=20";
  FILE* pipe = popen(cmd.c_str(), "r");
  if (!pipe) return "";
  char buf[4096];
  std::string result;
  if (fgets(buf, sizeof(buf), pipe)) {
    result = buf;
    // strip trailing newline
    while (!result.empty() && (result.back() == '\n' || result.back() == '\r'))
      result.pop_back();
  }
  int rc = pclose(pipe);
  if (rc != 0) return "";  // user cancelled or error
  return result;
}

// --- TUI ---

// Format a single 8-bit value as 2-char hex with leading zero.
static std::string hex2(uint8_t v) {
  char buf[4];
  snprintf(buf, sizeof(buf), "%02X", v);
  return buf;
}

// Format a 16-bit value as 4-char hex.
static std::string hex4(uint16_t v) {
  char buf[8];
  snprintf(buf, sizeof(buf), "%04X", v);
  return buf;
}

// Format SREG flag as a colored indicator.
static Element flagEl(bool set, const char* label) {
  auto el = text(label);
  if (set) {
    return el | bgcolor(Color::Green) | color(Color::Black) | bold;
  } else {
    return el | bgcolor(Color::Grey30) | color(Color::Grey70) | dim;
  }
}

// Build a row of register cells.  `vals` are the register values to display;
// `changed[i]` flags registers that changed since the last snapshot.
static Element regRow(const uint8_t vals[32], const bool changed[32], int col0) {
  Elements cells;
  for (int col = 0; col < 4; col++) {
    int r = col0 + col * 8;
    if (col > 0) cells.push_back(text("  "));
    char label[8];
    snprintf(label, sizeof(label), "R%02d", r);
    cells.push_back(text(label) | dim);
    cells.push_back(text(" "));
    auto valEl = text(hex2(vals[r])) | bold;
    if (changed[r]) {
      valEl = valEl | color(Color::Yellow) | bgcolor(Color::Grey19);
    }
    cells.push_back(valEl);
  }
  return hbox(std::move(cells));
}

int runTui(const std::string& elfPath) {
  uartSetTuiMode();
  AvrState state{};
  clearState(state);
  if (!loadFirmware(state, elfPath.c_str())) {
    return 2;
  }
  AvrState displayState = state;

  // Redirect stderr to a debug file so we can diagnose emulator errors
  // without corrupting the FTXUI display.
  freopen("emu_debug.log", "w", stderr);

  std::atomic<bool> emuDone(false);
  std::atomic<bool> emuError(false);
  std::atomic<bool> refreshStop(false);

  std::thread emuThread([&]() {
    try {
      bool ok = executeProgram(state);
      emuError.store(!ok);
    } catch (const std::exception& e) {
      emuErrorFile(elfPath.c_str(), e.what());
      emuError.store(true);
    } catch (...) {
      emuErrorFile(elfPath.c_str(), "unknown exception in emulator thread");
      emuError.store(true);
    }
    emuDone.store(true);
  });

  // Post refresh events every ~16ms so the FTXUI screen redraws
  // even when no keyboard/mouse events are arriving.
  auto screen = ScreenInteractive::Fullscreen();
  std::thread refreshThread([&]() {
    while (!refreshStop.load(std::memory_order_relaxed)) {
      std::this_thread::sleep_for(std::chrono::milliseconds(16));
      screen.Post(Event::Custom);
    }
  });

  // Shared TUI state.
  std::string coutText;
  std::string cinInput;
  std::string displayName = elfPath;
  auto slash = displayName.rfind('/');
  if (slash != std::string::npos) displayName = displayName.substr(slash + 1);

  int tabIndex = 0;
  Component cinInputComp = Input(&cinInput, "type here, Enter to send");

  // Rate-limiting and change tracking for register/disasm views.
  // Snapshot emulator state every ~200ms (or every frame when paused)
  // so the display remains readable at full execution speed.
  uint8_t  regSnapshot[32] = {};
  bool     regChanged[32] = {};
  uint16_t snapPc = 0, snapSp = 0;
  uint8_t  snapSreg = 0;
  uint64_t snapCycles = 0;
  bool     snapEver = false;
  auto     lastRegSnap = std::chrono::steady_clock::now();

  // Cached disassembly lines + the PC they were decoded for.
  Elements cachedDisasmLines;
  auto     lastDisasmSnap = std::chrono::steady_clock::now();

  // 't' toggles between slow (rate-limited, ~5 fps) and fast (real-time) views.
  bool viewSlow = true;
  auto serialTab = Renderer(cinInputComp, [&] {
    std::string newTx = uartPopTx();
    if (!newTx.empty()) {
      coutText += newTx;
      if (coutText.size() > 100000) {
        coutText = coutText.substr(coutText.size() - 80000);
      }
    }
    auto output = text(coutText.empty() ? "  (no output yet)" : coutText)
                | dim | frame | flex;
    auto divider = separator() | color(Color::Grey30);
    auto prompt = text(" ▶ ") | color(Color::GreenLight) | bold;
    auto inputArea = hbox({
        text(" ") | size(WIDTH, EQUAL, 1),
        prompt,
        cinInputComp->Render() | flex,
        text(" ") | size(WIDTH, EQUAL, 1),
    });
    return vbox({ output | flex, divider, inputArea });
  });

  // ── Tab 1: Registers ──────────────────────────────────────────────────
  auto regTab = Renderer([&] {
    // Rate-limit in slow mode: snapshot every 200ms, or every frame when paused.
    auto now = std::chrono::steady_clock::now();
    bool paused = g_emu_pause.load();
    if (!snapEver || !viewSlow ||
        (now - lastRegSnap > std::chrono::milliseconds(200)) || paused) {
      getEmulatorSnapshot(displayState);
      // Detect changed registers.
      for (int i = 0; i < 32; i++) {
        regChanged[i] = snapEver && (displayState.r[i] != regSnapshot[i]);
      }
      // Take snapshot.
      memcpy(regSnapshot, displayState.r, 32);
      snapPc = displayState.pc;
      snapSp = displayState.sp;
      snapSreg = displayState.sreg;
      snapCycles = displayState.cycle_count;
      lastRegSnap = now;
      snapEver = true;
    }

    // Build register grid from snapshot.
    Elements rows;
    for (int r = 0; r < 8; r++) {
      rows.push_back(regRow(regSnapshot, regChanged, r));
    }
    auto regGrid = vbox(std::move(rows));

    // SREG flags from snapshot.
    auto sregRow = hbox({
      text("SREG: "),
      flagEl(snapSreg & 0x80, "I"),
      text(" "),
      flagEl(snapSreg & 0x40, "T"),
      text(" "),
      flagEl(snapSreg & 0x20, "H"),
      text(" "),
      flagEl(snapSreg & 0x10, "S"),
      text(" "),
      flagEl(snapSreg & 0x08, "V"),
      text(" "),
      flagEl(snapSreg & 0x04, "N"),
      text(" "),
      flagEl(snapSreg & 0x02, "Z"),
      text(" "),
      flagEl(snapSreg & 0x01, "C"),
    });

    // PC, SP, cycles from snapshot.
    char info[128];
    snprintf(info, sizeof(info),
             "PC: 0x%s  SP: 0x%s  Cycles: %llu",
             hex4(snapPc).c_str(),
             hex4(snapSp).c_str(),
             (unsigned long long)snapCycles);

    return vbox({
      text(" Registers ") | bold | color(Color::Cyan),
      separator() | color(Color::Grey30),
      regGrid,
      separator() | color(Color::Grey30),
      sregRow,
      separator() | color(Color::Grey30),
      text(info),
      filler(),
    }) | border | flex;
  });

  // ── Tab 2: Disassembly ────────────────────────────────────────────────
  auto disasmTab = Renderer([&] {
    auto now = std::chrono::steady_clock::now();
    bool paused = g_emu_pause.load();
    // Rate-limit in slow mode: re-decode every 200ms (or every frame when paused).
    if (!viewSlow || paused ||
        (now - lastDisasmSnap > std::chrono::milliseconds(200))) {
      getEmulatorSnapshot(displayState);
      uint16_t pc = displayState.pc;
      lastDisasmSnap = now;
      cachedDisasmLines.clear();

      uint16_t start = (pc > 30) ? pc - 30 : 0;
      start &= ~1u;
      uint16_t scan = start;
      while (scan < pc + 30 && scan + 1 < AVR_FLASH_SIZE) {
        uint16_t w = displayState.flash[scan] |
                     ((uint16_t)displayState.flash[scan + 1] << 8);
        uint16_t extra = 0;
        uint8_t words = 1;
        Opcode op;
        std::string disasm;
        if (decodeInstruction(w, op)) {
          words = op.words;
          if (words == 2 && scan + 3 < AVR_FLASH_SIZE) {
            extra = displayState.flash[scan + 2] |
                    ((uint16_t)displayState.flash[scan + 3] << 8);
          }
          disasm = disassemble(w, extra, scan);
        } else {
          char buf[16];
          snprintf(buf, sizeof(buf), "??? 0x%04X", w);
          disasm = buf;
        }
        bool isCurrent = (scan == pc);
        char addr[8];
        snprintf(addr, sizeof(addr), "%s0x%04X", isCurrent ? ">" : " ", scan);
        cachedDisasmLines.push_back(hbox({
          text(addr) | (isCurrent ? (bold | color(Color::Yellow))
                                  : color(Color::Grey70)),
          text("  "),
          text(disasm) | (isCurrent ? bold : dim),
        }));
        scan += words * 2;
      }
    }

    return vbox({
      text(" Disassembly ") | bold | color(Color::Cyan),
      separator() | color(Color::Grey30),
      vbox(cachedDisasmLines) | frame | flex,
    }) | border | flex;
  });


  // ── Tab container ─────────────────────────────────────────────────────
  auto tabContainer = Container::Tab({
    serialTab,
    regTab,
    disasmTab,
  }, &tabIndex);

  // ── Main renderer ─────────────────────────────────────────────────────
  auto renderer = Renderer(tabContainer, [&] {
    // Header
    std::string statusDot;
    Color      statusColor;
    if (emuDone.load()) {
      statusDot = emuError.load() ? " ERROR " : " STOPPED ";
      statusColor = emuError.load() ? Color::Red : Color::Yellow;
    } else {
      statusDot = " RUNNING ";
      statusColor = Color::Green;
    }
    auto header = hbox({
        text(" ") | size(WIDTH, EQUAL, 1),
        text(displayName) | bold | color(Color::Cyan),
        text("  atmega328p emulator") | dim,
        filler(),
        text(statusDot) | bgcolor(statusColor) | color(Color::Black) | bold,
        text(" ") | size(WIDTH, EQUAL, 1),
    });

    // Tab bar
    auto makeTab = [&](int idx, const std::string& name) {
      auto el = text(" " + name + " ");
      if (idx == tabIndex) {
        el = el | bold | color(Color::White) | bgcolor(Color::Blue);
      } else {
        el = el | dim;
      }
      return el;
    };
    auto tabBar = hbox({
      text(" ") | size(WIDTH, EQUAL, 1),
      makeTab(0, "Serial"),
      makeTab(1, "Registers"),
      makeTab(2, "Disasm"),
      filler(),
    });
    // Status bar
    std::string status;
    if (emuDone.load()) {
      if (emuError.load()) {
        char errBuf[256];
        emuGetLastError(errBuf, sizeof(errBuf));
        status = std::string(" ") + errBuf + " — Esc to exit ";
      } else {
        status = " Emulator stopped — Esc to exit ";
      }
    } else if (g_emu_pause.load()) {
      status = viewSlow
        ? " ⏸ Paused (slow)   Space resume | Esc stop | t speed | Tab switch "
        : " ⏸ Paused (fast)   Space resume | Esc stop | t speed | Tab switch ";
    } else {
      status = viewSlow
        ? " ● Running (slow)   Space pause | Esc stop | t speed | Tab switch | Enter send "
        : " ● Running (fast)   Space pause | Esc stop | t speed | Tab switch | Enter send ";
    }
    auto statusBar = text(status) | inverted | center;

    return vbox({
      header,
      tabBar,
      separator() | color(Color::Grey30),
      tabContainer->Render() | flex,
      statusBar,
    });
  });

  // Event handling.
  renderer |= CatchEvent([&](Event event) {
     if (event == Event::Escape) {
      if (!emuDone.load()) {
        g_emu_stop.store(true);
        return true;
      }
      screen.Exit();
      return true;
    }
    if (event == Event::Character(' ')) {
      if (!emuDone.load()) {
        g_emu_pause.store(!g_emu_pause.load());
      }
      return true;
    }
    if (event == Event::Character('t')) {
      viewSlow = !viewSlow;
      return true;
    }
    if (event == Event::Tab) {
      tabIndex = (tabIndex + 1) % 3;
      return true;
    }
    if (event == Event::TabReverse) {
      tabIndex = (tabIndex + 2) % 3;  // -1 mod 3
      return true;
    }
    if (event == Event::Return) {
      if (!emuDone.load() && tabIndex == 0) {
        for (char c : cinInput) uartInjectRx((uint8_t)c);
        uartInjectRx('\n');
        cinInput.clear();
      }
      return true;
    }
    return false;
  });

  screen.Loop(renderer);

  refreshStop.store(true);
  g_emu_stop.store(true);
  g_emu_pause.store(false);  // unpause so emu thread can exit
  if (refreshThread.joinable()) refreshThread.join();
  if (emuThread.joinable()) emuThread.join();

  return emuError.load() ? 3 : 0;
}

// --- entry point ---

int main(int argc, char** argv) {
  std::string elfPath;

  if (argc >= 2) {
    elfPath = argv[1];
  } else {
    // No argument — use fzf to select a .elf file.
    elfPath = fzfSelectElf();
    if (elfPath.empty()) {
      std::cerr << "No .elf file selected.\n";
      usage();
      return 1;
    }
  }

  if (!validateFile(elfPath.c_str())) {
    emuErrorFile(elfPath.c_str(), "not a readable ELF file (expected 0x7F 'E' 'L' 'F' magic)");
    return 1;
  }

  return runTui(elfPath);
}

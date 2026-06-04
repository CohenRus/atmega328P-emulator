/*
 * main.cpp — ATmega328P emulator entry point with TUI.
 *
 * Usage:
 *   emulator [firmware.elf]    — load and run with TUI
 *   emulator                    — fuzzy-find .elf with fzf, then run
 *
 * The TUI has a top bar showing the filename, a left panel for emulated
 * serial output (cout), and a right panel for serial input (cin).
 * Press Escape or Ctrl+C to stop the emulator.
 */
#include <atomic>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

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

int runTui(const std::string& elfPath) {
  uartSetTuiMode();

  AvrState state;
  std::vector<char> pathBuf(elfPath.begin(), elfPath.end());
  pathBuf.push_back('\0');
  if (!loadFirmware(state, pathBuf.data())) {
    return 2;
  }

  // Suppress stderr from the emulator thread (tick logs) so they don't
  // corrupt the FTXUI display.  In headless mode these go to the terminal;
  // in TUI mode the terminal is occupied by the UI.
  freopen("/dev/null", "w", stderr);

  std::atomic<bool> emuDone(false);
  std::atomic<bool> emuError(false);
  std::thread emuThread([&]() {
    bool ok = executeProgram(state);
    emuError.store(!ok);
    emuDone.store(true);
  });

  // Shared TUI state.
  std::string coutText;
  std::string cinInput;
  constexpr int kPanelWidth = 70;

  std::string displayName = elfPath;
  auto slash = displayName.rfind('/');
  if (slash != std::string::npos) displayName = displayName.substr(slash + 1);

  auto screen = ScreenInteractive::Fullscreen();

  Component cinInputComp = Input(&cinInput, "type here, Enter to send");

  auto renderer = Renderer(cinInputComp, [&] {
    // Drain TX buffer.
    std::string newTx = uartPopTx();
    if (!newTx.empty()) {
      coutText += newTx;
      if (coutText.size() > 100000) {
        coutText = coutText.substr(coutText.size() - 80000);
      }
    }

    auto topBar = text(" Running: " + displayName + " ")
                | bold | center | border;

    auto coutPanel = text(coutText.empty() ? "(no output yet)" : coutText)
                   | frame
                   | size(WIDTH, GREATER_THAN, kPanelWidth)
                   | border;

    auto cinLabel = text(" Serial input: ") | dim;
    auto cinField = cinInputComp->Render() | border;
    auto cinPanel = vbox({
        cinLabel,
        cinField,
        filler(),
    }) | size(WIDTH, GREATER_THAN, kPanelWidth) | border;

    std::string status;
    if (emuDone.load()) {
      status = emuError.load() ? " EMULATOR STOPPED (error) — press Esc to exit "
                               : " EMULATOR STOPPED — press Esc to exit ";
    } else {
      status = " Running… (Esc to stop) ";
    }
    auto statusBar = text(status) | inverted | center;

    return vbox({
        topBar,
        hbox({
            coutPanel,
            separator(),
            cinPanel,
        }) | flex,
        statusBar,
    });
  });

  // Event handling: Enter sends input, Esc stops emu / exits program.
  renderer |= CatchEvent([&](Event event) {
    if (event == Event::Escape) {
      if (!emuDone.load()) {
        g_emu_stop.store(true);
        return true;  // keep running until emu thread joins
      }
      // Emulator already stopped — exit the TUI.
      screen.Exit();
      return true;
    }
    if (event == Event::Return) {
      if (!emuDone.load()) {
        for (char c : cinInput) uartInjectRx((uint8_t)c);
        uartInjectRx('\n');
        cinInput.clear();
      }
      return true;
    }
    return false;
  });

  screen.Loop(renderer);

  g_emu_stop.store(true);
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

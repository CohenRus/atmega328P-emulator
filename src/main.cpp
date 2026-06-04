/*
 * main.cpp — ATmega328P emulator entry point.
 *
 * Validates the firmware file (ELF magic), loads its segments into
 * program memory, and executes until termination or fault.
 *
 * Design: command-line only. Accepts exactly one argument — the path to
 * a valid ELF firmware image. Returns 0 on success, non-zero on error.
 */

#include <iostream>
#include <fstream>
#include "loader.h"
#include "state.h"
#include "executor.h"
#include "error.h"
// ftxui includes — kept for future TUI integration
#include "ftxui/component/app.hpp"             // for App
#include "ftxui/component/captured_mouse.hpp"  // for ftxui
#include "ftxui/component/component.hpp"  // for Button, Horizontal, Renderer
#include "ftxui/component/component_base.hpp"  // for ComponentBase
#include "ftxui/dom/elements.hpp"

// Print usage message to stderr.
void usage();

// Check whether a file has the ELF magic bytes (0x7F 'E' 'L' 'F').
// @param fileName — path to the file to validate
// @return true if the file exists and starts with ELF magic
bool validateFile(const char* fileName);

// Entry point. Expects exactly one argument: the path to the firmware ELF.
// @param argc — argument count
// @param argv — argument vector
// @return 0 on clean execution, 1 on usage/ELF-validation error,
//         2 on load failure, 3 on execution failure
int main(int argc, char** argv) {
  using namespace ftxui;
  AvrState state;

  if (argc != 2) {
    usage();
    return 1;
  }

  if (!validateFile(argv[1])) {
    emuErrorFile(argv[1], "not a readable ELF file (expected 0x7F 'E' 'L' 'F' magic)");
    return 1;
  }

  if (!loadFirmware(state, argv[1])) {
    return 2;
  }

  if (!executeProgram(state)) {
    return 3;
  }

  // Future TUI integration (commented out — not yet wired).
  // auto component = Renderer([&] {
  //     return vbox({
  //         text(""),
  //     }) |
  //     flex | border  // });
  // auto screen = App::Fullscreen();
  // screen.Loop(component);
  return 0;
}

// Print usage string to stderr.
void usage() {
  std::cerr << "usage: emulator <firmware.elf>\n";
}

// Validate ELF magic: reads the first four bytes from the file and
// compares them against 0x7F 'E' 'L' 'F'.
// @param fileName — path to the file to validate
// @return true if file is openable and starts with ELF magic
bool validateFile(const char* fileName) {
  std::ifstream file(fileName, std::ios::binary);
  if (!file.is_open()) {
    return false;
  }

  char mb[4];
  if (!file.read(mb, 4)) {
    return false;
  }
  return (mb[0] == 0x7f && mb[1] == 'E' && mb[2] == 'L' && mb[3] == 'F');
}

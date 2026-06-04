#include <iostream>
#include <fstream>
#include "loader.h"
#include "state.h"
#include "executor.h"
#include "error.h"
// ftxui includes
#include "ftxui/component/app.hpp"             // for App
#include "ftxui/component/captured_mouse.hpp"  // for ftxui
#include "ftxui/component/component.hpp"  // for Button, Horizontal, Renderer
#include "ftxui/component/component_base.hpp"  // for ComponentBase
#include "ftxui/dom/elements.hpp"

void usage();
bool validateFile(const char* fileName);

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

  // Modify the way to render them on screen:
  // auto component = Renderer([&] {
  //     return vbox({
  //         text(""),
  //     }) |
  //     flex | border  // });

  // auto screen = App::Fullscreen();
  // screen.Loop(component);
  return 0;
}

void usage() {
  std::cerr << "usage: emulator <firmware.elf>\n";
}

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

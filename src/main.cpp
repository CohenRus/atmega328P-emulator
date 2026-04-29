#include <iostream>
#include <fstream>
#include "loader.h"
#include "state.h"

void usage();
bool validateFile(char* fileName);

int main(int argc, char** argv) {
  // instantiate global state
  AvrState state; 

  // check args
  if (argc != 2) {
    usage();
    exit(1);
  }

  // check file
  if (!validateFile(argv[1])) {
    std::cout << "invalid file" << std::endl;
    exit(1);
  }


  // load elf file
  if (!loadFirmware(state, argv[1])) {
    std::cout << "failed to load elf file" << std::endl;
    exit(2);
  }
  std::cout << "loaded elf file successfully" << std::endl;
}

void usage() {
  std::cout << "wrong params chud" << std::endl;
  std::cout << "cli <firmware.elf>" << std::endl;
}

bool validateFile(char* fileName) {
  // check for file existing
  std::ifstream file(fileName, std::ios::binary);
  if (!file.is_open()) {
    file.close();
    return false;
  }

  // check magic bytes to verify elf file
  // Byte 0: 0x7f
  // Byte 1: 0x45 (ASCII 'E')
  // Byte 2: 0x4c (ASCII 'L')
  // Byte 3: 0x46 (ASCII 'F')  
  char mb[4];
  file.read(mb, 4);
  file.close();
  return (mb[0] == 0x7f && mb[1] == 'E' && mb[2] == 'L' && mb[3] == 'F');
}

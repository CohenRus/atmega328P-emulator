// Parses an AVR ELF binary and loads the executable text segment into the emulator's flash memory.

#include "loader.h"
#include "error.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <iostream>

namespace {

void reportElfReadFailure(const char* path, const char* what) {
  std::cerr << "error: " << what << " ('" << path << "')";
  if (errno != 0) {
    std::cerr << ": " << std::strerror(errno);
  }
  std::cerr << '\n';
}

}  // namespace

// Loads firmware from an ELF file into emulator flash memory.
// Preconditions: state must be a valid initialized AvrState; fileName must be a path to a well-formed AVR ELF binary.
// Returns true on success, false on any I/O, format, or size error.
bool loadFirmware(AvrState& state, char* fileName) {
  std::ifstream file(fileName, std::ios::binary);

  if (!file.is_open()) {
    emuErrorFile(fileName, "cannot open firmware file");
    return false;
  }

  Elf32_Ehdr header;
  if (!file.read(reinterpret_cast<char*>(&header), sizeof(header))) {
    reportElfReadFailure(fileName, "failed to read ELF executable header");
    return false;
  }

  if (header.e_phoff == 0 || header.e_phnum == 0) {
    std::cerr << "error: ELF has no program headers ('" << fileName << "')\n";
    return false;
  }

  file.seekg(static_cast<std::streamoff>(header.e_phoff), std::ios::beg);
  if (!file) {
    reportElfReadFailure(fileName, "failed to seek to ELF program header table");
    return false;
  }

  Elf32_Phdr buffer{};
  bool found = false;

  for (int i = 0; i < header.e_phnum; i++) {
    if (!file.read(reinterpret_cast<char*>(&buffer), sizeof(buffer))) {
      std::cerr << "error: failed to read ELF program header " << i
                << " of " << static_cast<int>(header.e_phnum)
                << " ('" << fileName << "')\n";
      return false;
    }
    if (isTextPhdr(buffer)) {
      found = true;
      break;
    }
  }
  if (!found) {
    std::cerr << "error: no loadable executable (PT_LOAD, R+X) segment in ELF ('"
              << fileName << "')\n";
    return false;
  }

  file.seekg(static_cast<std::streamoff>(buffer.p_offset), std::ios::beg);
  if (!file) {
    std::cerr << "error: failed to seek to segment at file offset 0x"
              << std::hex << buffer.p_offset << std::dec
              << " ('" << fileName << "')\n";
    return false;
  }

  std::vector<uint8_t> data(buffer.p_filesz);
  if (!file.read(reinterpret_cast<char*>(data.data()), buffer.p_filesz)) {
    std::cerr << "error: failed to read " << buffer.p_filesz
              << " byte .text segment at offset 0x"
              << std::hex << buffer.p_offset << std::dec
              << " ('" << fileName << "')\n";
    return false;
  }

  if (buffer.p_vaddr + data.size() > sizeof(state.flash)) {
    std::cerr << "error: firmware segment does not fit in flash: load at 0x"
              << std::hex << buffer.p_vaddr << ", size " << std::dec << data.size()
              << " bytes, flash size " << sizeof(state.flash)
              << " ('" << fileName << "')\n";
    return false;
  }
  memcpy(state.flash + buffer.p_vaddr, data.data(), data.size());

  state.pc = static_cast<uint16_t>(header.e_entry);
  if (header.e_entry >= AVR_FLASH_SIZE) {
    std::cerr << "error: ELF entry point 0x" << std::hex << header.e_entry
              << std::dec << " is outside flash ('" << fileName << "')\n";
    return false;
  }

  std::cout << "loaded firmware: " << data.size() << " bytes at flash 0x"
            << std::hex << buffer.p_vaddr << ", entry 0x" << header.e_entry
            << std::dec << '\n';
  return true;
}

// Returns true if the program header describes the executable text segment (loadable, readable, executable, not writable).
bool isTextPhdr(Elf32_Phdr& header) {
  return header.p_type == PT_LOAD &&
         (header.p_flags & PF_X) &&
         (header.p_flags & PF_R) &&
         !(header.p_flags & PF_W);
}

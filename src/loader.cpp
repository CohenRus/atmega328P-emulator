// Parses an AVR ELF binary and loads the executable text segment into the emulator's flash memory.

#include "loader.h"
#include <iostream>

// Loads firmware from an ELF file into emulator flash memory.
// Preconditions: state must be a valid initialized AvrState; fileName must be a path to a well-formed AVR ELF binary.
// Returns true on success, false on any I/O, format, or size error.
bool loadFirmware(AvrState& state, char* fileName) {
  std::ifstream file(fileName, std::ios::binary);

  if (!file.is_open()) {
    std::cerr << "failed to open elf file" << std::endl;
    file.close();
    return false;
  }

  // read in the executable header
  Elf32_Ehdr header;
  if (!file.read(reinterpret_cast<char*>(&header), sizeof(header))) {
    std::cerr << "error reading elf executable header" << std::endl;
    file.close();
    return false;
  }
  
  // read program headers in untill find the text one
  Elf32_Phdr buffer;
  bool found = false;

  for (int i = 0; i < header.e_phnum; i++) {
    if (!file.read(reinterpret_cast<char*>(&buffer), sizeof(buffer))) {
      std::cerr << "error reading elf program headers" << std::endl;
      file.close();
      return false;
    }
    if (isTextPhdr(buffer)) {
      found = true;
      break;
    }
  }
  if (!found) {
    // malformed elf file, cannot load
    std::cerr << "error finding text program header in elf file" << std::endl;
    file.close();
    return false;
  }

  // jump to the given offset in the file 
  file.seekg(buffer.p_offset, std::ios::beg);

  // read in the text (should be runable hex)
  std::vector<uint8_t> data(buffer.p_filesz);
  if (!file.read(reinterpret_cast<char*>(data.data()), buffer.p_filesz)) {
    std::cerr << "error reading unable hex" << std::endl;
    file.close();
    return false;
  }

  // copy hex into state flash at the virtual address the linker specified
  if (buffer.p_vaddr + data.size() > sizeof(state.flash)) {
    std::cerr << "firmware segment exceeds flash bounds" << std::endl;
    file.close();
    return false;
  }
  memcpy(state.flash + buffer.p_vaddr, data.data(), data.size());

  // set initial PC from ELF entry point
  state.pc = header.e_entry;

  file.close();
  return true;
}

// Returns true if the program header describes the executable text segment (loadable, readable, executable, not writable).
// Preconditions: header must be a valid Elf32_Phdr populated from an ELF file.
bool isTextPhdr(Elf32_Phdr& header) {
  return header.p_type == PT_LOAD && // loadable
         (header.p_flags & PF_X) && // excutable
         (header.p_flags & PF_R) && // readable
         !(header.p_flags & PF_W); // not writeable
}

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

// AVR memory model in ELF:
//   Program memory (flash):  vaddr 0x00000000 – 0x0007FFFF
//   Data memory (SRAM):      vaddr 0x00800000 – 0x0080FFFF  (subtract 0x800000 for SRAM addr)
//
// avr-gcc places .data initial values in flash immediately after .text.
// The CRT copies from that flash address into SRAM at startup.
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

  // Read all program headers into a vector so we can process them in order.
  std::vector<Elf32_Phdr> phdrs(header.e_phnum);
  for (int i = 0; i < header.e_phnum; i++) {
    if (!file.read(reinterpret_cast<char*>(&phdrs[i]), sizeof(Elf32_Phdr))) {
      std::cerr << "error: failed to read ELF program header " << i
                << " of " << static_cast<int>(header.e_phnum)
                << " ('" << fileName << "')\n";
      return false;
    }
  }

  uint32_t nextFlashAddr = 0;  // where next .data init block goes in flash
  bool     hasText       = false;
  size_t   totalFlash    = 0;

  // Pass 1: load all non-writable (flash) segments, track where .text ends.
  for (auto& ph : phdrs) {
    if (ph.p_type != PT_LOAD) continue;

    bool writable = (ph.p_flags & PF_W) != 0;
    if (writable) continue;  // handled in pass 2

    // .text / read-only segment → load into flash
    if (ph.p_vaddr >= 0x800000) {
      std::cerr << "error: non-writable segment at data-space address 0x"
                << std::hex << ph.p_vaddr << std::dec
                << " ('" << fileName << "')\n";
      return false;
    }

    file.seekg(static_cast<std::streamoff>(ph.p_offset), std::ios::beg);
    std::vector<uint8_t> data(ph.p_filesz);
    if (!file.read(reinterpret_cast<char*>(data.data()), ph.p_filesz)) {
      std::cerr << "error: failed to read segment at offset 0x"
                << std::hex << ph.p_offset << std::dec
                << " ('" << fileName << "')\n";
      return false;
    }

    if (ph.p_vaddr + data.size() > sizeof(state.flash)) {
      std::cerr << "error: text segment does not fit in flash (vaddr 0x"
                << std::hex << ph.p_vaddr << " + " << std::dec
                << data.size() << " bytes) ('" << fileName << "')\n";
      return false;
    }

    memcpy(state.flash + ph.p_vaddr, data.data(), data.size());
    hasText = true;
    totalFlash = ph.p_vaddr + data.size();
    nextFlashAddr = (uint32_t)(ph.p_vaddr + data.size());

    std::cout << "loaded text: " << data.size() << " bytes at flash 0x"
              << std::hex << ph.p_vaddr << std::dec << '\n';
  }

  if (!hasText) {
    std::cerr << "error: no loadable executable segment in ELF ('"
              << fileName << "')\n";
    return false;
  }

  // Pass 2: handle writable segments (.data and .bss).
  // .data initial values go into flash at nextFlashAddr (the CRT reads them
  // via LPM from this address).  Then they are also copied into SRAM.
  // .bss is zeroed in SRAM.
  for (auto& ph : phdrs) {
    if (ph.p_type != PT_LOAD) continue;
    if ((ph.p_flags & PF_W) == 0) continue;  // already handled

    // Convert data-space vaddr (0x80xxxx) to SRAM offset.
    if (ph.p_vaddr < 0x800000 || ph.p_vaddr > 0x80FFFF) {
      std::cerr << "error: writable segment at unexpected vaddr 0x"
                << std::hex << ph.p_vaddr << std::dec
                << " ('" << fileName << "')\n";
      return false;
    }
    uint16_t sramAddr = static_cast<uint16_t>(ph.p_vaddr - 0x800000);

    if (ph.p_filesz > 0) {
      // .data — has initial values in the file.
      // First, load them into flash at nextFlashAddr (CRT reads from here).
      file.seekg(static_cast<std::streamoff>(ph.p_offset), std::ios::beg);
      std::vector<uint8_t> data(ph.p_filesz);
      if (!file.read(reinterpret_cast<char*>(data.data()), ph.p_filesz)) {
        std::cerr << "error: failed to read .data segment at offset 0x"
                  << std::hex << ph.p_offset << std::dec
                  << " ('" << fileName << "')\n";
        return false;
      }

      if (nextFlashAddr + data.size() > sizeof(state.flash)) {
        std::cerr << "error: .data initial values do not fit in flash ('"
                  << fileName << "')\n";
        return false;
      }
      memcpy(state.flash + nextFlashAddr, data.data(), data.size());

      // Also copy to SRAM so the CRT doesn't strictly need to run
      // (but it will anyway; this is a belt-and-suspenders).
      if (sramAddr + data.size() <= sizeof(state.sram)) {
        memcpy(state.sram + sramAddr, data.data(), data.size());
      }

      std::cout << "loaded .data: " << data.size() << " bytes at flash 0x"
                << std::hex << nextFlashAddr << " → sram 0x"
                << sramAddr << std::dec << '\n';

      nextFlashAddr += data.size();
    }

    if (ph.p_memsz > ph.p_filesz) {
      // .bss portion (zero-fill in SRAM).
      uint16_t bssStart = sramAddr + ph.p_filesz;
      uint16_t bssSize  = static_cast<uint16_t>(ph.p_memsz - ph.p_filesz);
      if (bssStart + bssSize <= sizeof(state.sram)) {
        memset(state.sram + bssStart, 0, bssSize);
      }
      std::cout << "zeroed .bss: " << bssSize << " bytes at sram 0x"
                << std::hex << bssStart << std::dec << '\n';
    }
  }

  // Parse symbol table to find timer0_millis address.
  // This symbol tells us where in SRAM the firmware expects the millis counter.
  state.timer0_millis_addr = 0;
  if (header.e_shoff != 0 && header.e_shnum > 0) {
    // Read section headers
    file.seekg(static_cast<std::streamoff>(header.e_shoff), std::ios::beg);
    std::vector<Elf32_Shdr> shdrs(header.e_shnum);
    file.read(reinterpret_cast<char*>(shdrs.data()),
              header.e_shnum * sizeof(Elf32_Shdr));

    // Read section name string table
    Elf32_Shdr& shstrtab = shdrs[header.e_shstrndx];
    std::vector<char> shstr(shstrtab.sh_size);
    file.seekg(static_cast<std::streamoff>(shstrtab.sh_offset), std::ios::beg);
    file.read(shstr.data(), shstrtab.sh_size);

    // Find .symtab and .strtab
    const Elf32_Shdr* symtab = nullptr;
    const char* strtabData = nullptr;
    uint32_t strtabSize = 0;
    for (auto& sh : shdrs) {
      if (sh.sh_type == SHT_SYMTAB) symtab = &sh;
      if (sh.sh_type == SHT_STRTAB && strcmp(shstr.data() + sh.sh_name, ".strtab") == 0) {
        strtabData = new char[sh.sh_size];  // temporary buffer
        strtabSize = sh.sh_size;
        file.seekg(static_cast<std::streamoff>(sh.sh_offset), std::ios::beg);
        file.read(const_cast<char*>(strtabData), sh.sh_size);
      }
    }

    if (symtab && strtabData) {
      size_t count = symtab->sh_size / sizeof(Elf32_Sym);
      file.seekg(static_cast<std::streamoff>(symtab->sh_offset), std::ios::beg);
      std::vector<Elf32_Sym> syms(count);
      file.read(reinterpret_cast<char*>(syms.data()), symtab->sh_size);

      for (auto& sym : syms) {
        if (sym.st_name < strtabSize &&
            strcmp(strtabData + sym.st_name, "timer0_millis") == 0) {
          // Convert data-space vaddr to SRAM offset
          if (sym.st_value >= 0x800000 && sym.st_value <= 0x80FFFF) {
            state.timer0_millis_addr = static_cast<uint16_t>(sym.st_value - 0x800000);
          }
          break;
        }
      }
    }
    delete[] strtabData;
  }

  if (state.timer0_millis_addr != 0) {
    std::cout << "found timer0_millis at sram 0x"
              << std::hex << state.timer0_millis_addr << std::dec << '\n';
  }

  state.pc = static_cast<uint16_t>(header.e_entry);
  if (header.e_entry >= AVR_FLASH_SIZE) {
    std::cerr << "error: ELF entry point 0x" << std::hex << header.e_entry
              << std::dec << " is outside flash ('" << fileName << "')\n";
    return false;
  }

  std::cout << "entry point: 0x" << std::hex << header.e_entry
            << std::dec << ", total flash used: " << totalFlash << " bytes\n";
  return true;
}
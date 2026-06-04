/*
 * loader.cpp — ELF binary parsing and AVR firmware loading implementation.
 *
 * Opens a compiled AVR ELF executable and maps its contents into the emulator's
 * memory model.  The loading process has four phases:
 *
 *   1. ELF header + program header table parsing.
 *   2. Pass 1 — non-writable (flash) segments (.text) loaded into state.flash[].
 *   3. Pass 2 — writable segments (.data init values into flash after .text,
 *      then copied to SRAM; .bss zeroed in SRAM).
 *   4. Symbol table lookup — resolves the timer0_millis address.
 *
 * AVR data-space virtual addresses (0x800000–0x80FFFF) are mapped to SRAM
 * offsets by subtracting 0x800000.  The ELF entry point becomes the initial PC.
 */

#include "loader.h"
#include "error.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <iostream>

namespace {

// Print a human-readable file I/O error to stderr, appending the system errno
// string when set.
//
// @param path — path of the file that failed
// @param what — description of the operation that failed
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
//
// See loader.h for the full contract.
bool loadFirmware(AvrState& state, char* fileName) {

  // ---------- 1. Open file and parse ELF header ----------
  std::ifstream file(fileName, std::ios::binary);

  if (!file.is_open()) {
    emuErrorFile(fileName, "cannot open firmware file");
    return false;
  }

  // Read the 52-byte ELF executable header at offset 0.
  Elf32_Ehdr header;
  if (!file.read(reinterpret_cast<char*>(&header), sizeof(header))) {
    reportElfReadFailure(fileName, "failed to read ELF executable header");
    return false;
  }

  // Bail out if the file lacks program headers — we need at minimum one PT_LOAD.
  if (header.e_phoff == 0 || header.e_phnum == 0) {
    std::cerr << "error: ELF has no program headers ('" << fileName << "')\n";
    return false;
  }

  // Seek to the program header table and read all entries.
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

  // nextFlashAddr tracks where .data initialization values will be placed
  // in flash, immediately after the .text segment.
  uint32_t nextFlashAddr = 0;
  bool     hasText       = false;
  size_t   totalFlash    = 0;

  // ---------- 2. Pass 1: flash (non-writable) segments ----------
  // Load all PT_LOAD segments without the PF_W flag.  On avr-gcc this is
  // typically the .text section.  After this pass, nextFlashAddr points just
  // past the end of .text, where .data init values will go.
  for (auto& ph : phdrs) {
    if (ph.p_type != PT_LOAD) continue;

    bool writable = (ph.p_flags & PF_W) != 0;
    if (writable) continue;  // handled in pass 2

    // Reject non-writable segments in data-address space (shouldn't happen).
    if (ph.p_vaddr >= 0x800000) {
      std::cerr << "error: non-writable segment at data-space address 0x"
                << std::hex << ph.p_vaddr << std::dec
                << " ('" << fileName << "')\n";
      return false;
    }

    // Read segment bytes from the file at p_offset.
    file.seekg(static_cast<std::streamoff>(ph.p_offset), std::ios::beg);
    std::vector<uint8_t> data(ph.p_filesz);
    if (!file.read(reinterpret_cast<char*>(data.data()), ph.p_filesz)) {
      std::cerr << "error: failed to read segment at offset 0x"
                << std::hex << ph.p_offset << std::dec
                << " ('" << fileName << "')\n";
      return false;
    }

    // Bounds check — segment must fit within our 32 KiB flash array.
    if (ph.p_vaddr + data.size() > sizeof(state.flash)) {
      std::cerr << "error: text segment does not fit in flash (vaddr 0x"
                << std::hex << ph.p_vaddr << " + " << std::dec
                << data.size() << " bytes) ('" << fileName << "')\n";
      return false;
    }

    // Copy directly: flash byte address = ELF virtual address.
    memcpy(state.flash + ph.p_vaddr, data.data(), data.size());
    hasText = true;
    totalFlash = ph.p_vaddr + data.size();
    nextFlashAddr = static_cast<uint32_t>(ph.p_vaddr + data.size());

    std::cout << "loaded text: " << data.size() << " bytes at flash 0x"
              << std::hex << ph.p_vaddr << std::dec << '\n';
  }

  if (!hasText) {
    std::cerr << "error: no loadable executable segment in ELF ('"
              << fileName << "')\n";
    return false;
  }

  // ---------- 3. Pass 2: writable segments (.data / .bss) ----------
  // Writable PT_LOAD segments contain initialized data (.data) followed by
  // a zero-fill region (.bss, where p_memsz > p_filesz).
  //
  // Strategy:
  //   - .data init values → placed in flash at nextFlashAddr so the CRT
  //     startup code can copy them via LPM instructions.
  //   - .data init values → also copied directly into SRAM as a convenience
  //     (belt-and-suspenders: the CRT will overwrite this on real startup).
  //   - .bss → zeroed in SRAM (no flash footprint).
  //
  // AVR data-space virtual addresses (0x800000–0x80FFFF) are converted to
  // SRAM byte offsets by subtracting 0x800000.
  for (auto& ph : phdrs) {
    if (ph.p_type != PT_LOAD) continue;
    if ((ph.p_flags & PF_W) == 0) continue;  // already handled in pass 1

    // Validate that the writable segment lives in AVR data address space.
    if (ph.p_vaddr < 0x800000 || ph.p_vaddr > 0x80FFFF) {
      std::cerr << "error: writable segment at unexpected vaddr 0x"
                << std::hex << ph.p_vaddr << std::dec
                << " ('" << fileName << "')\n";
      return false;
    }
    uint16_t sramAddr = static_cast<uint16_t>(ph.p_vaddr - 0x800000);

    if (ph.p_filesz > 0) {
      // .data — segment has an initialized portion in the file.
      // Read it into flash first (CRT entry point for LPM copy), then mirror
      // into SRAM so the emulator can run without the CRT if needed.
      file.seekg(static_cast<std::streamoff>(ph.p_offset), std::ios::beg);
      std::vector<uint8_t> data(ph.p_filesz);
      if (!file.read(reinterpret_cast<char*>(data.data()), ph.p_filesz)) {
        std::cerr << "error: failed to read .data segment at offset 0x"
                  << std::hex << ph.p_offset << std::dec
                  << " ('" << fileName << "')\n";
        return false;
      }

      // Flash placement: must fit after .text.
      if (nextFlashAddr + data.size() > sizeof(state.flash)) {
        std::cerr << "error: .data initial values do not fit in flash ('"
                  << fileName << "')\n";
        return false;
      }
      memcpy(state.flash + nextFlashAddr, data.data(), data.size());

      // SRAM placement: direct copy so the emulator sees initialized globals.
      if (sramAddr + data.size() <= sizeof(state.sram)) {
        memcpy(state.sram + sramAddr, data.data(), data.size());
      }

      std::cout << "loaded .data: " << data.size() << " bytes at flash 0x"
                << std::hex << nextFlashAddr << " → sram 0x"
                << sramAddr << std::dec << '\n';

      nextFlashAddr += static_cast<uint32_t>(data.size());
    }

    if (ph.p_memsz > ph.p_filesz) {
      // .bss — the portion of the segment beyond p_filesz is uninitialized
      // and must be zeroed in SRAM.  It has no flash footprint.
      uint16_t bssStart = sramAddr + ph.p_filesz;
      uint16_t bssSize  = static_cast<uint16_t>(ph.p_memsz - ph.p_filesz);
      if (bssStart + bssSize <= sizeof(state.sram)) {
        memset(state.sram + bssStart, 0, bssSize);
      }
      std::cout << "zeroed .bss: " << bssSize << " bytes at sram 0x"
                << std::hex << bssStart << std::dec << '\n';
    }
  }

  // ---------- 4. Symbol table lookup: find timer0_millis ----------
  // The firmware declares an extern variable timer0_millis that the emulator
  // updates on each Timer0 overflow.  We need its SRAM address so we know
  // where to write.  This symbol is resolved by scanning the ELF symbol table.
  //
  // Required: the ELF must have a section header table (not stripped) with a
  // .symtab section and a .strtab section containing symbol name strings.
  state.timer0_millis_addr = 0;
  if (header.e_shoff != 0 && header.e_shnum > 0) {
    // Read all section headers.
    file.seekg(static_cast<std::streamoff>(header.e_shoff), std::ios::beg);
    std::vector<Elf32_Shdr> shdrs(header.e_shnum);
    file.read(reinterpret_cast<char*>(shdrs.data()),
              header.e_shnum * sizeof(Elf32_Shdr));

    // Read the section name string table (.shstrtab) so we can match section
    // header names like ".strtab" and ".symtab".
    Elf32_Shdr& shstrtab = shdrs[header.e_shstrndx];
    std::vector<char> shstr(shstrtab.sh_size);
    file.seekg(static_cast<std::streamoff>(shstrtab.sh_offset), std::ios::beg);
    file.read(shstr.data(), shstrtab.sh_size);

    // Scan section headers to locate .symtab and .strtab.
    const Elf32_Shdr* symtab = nullptr;
    const char* strtabData = nullptr;
    uint32_t strtabSize = 0;
    for (auto& sh : shdrs) {
      if (sh.sh_type == SHT_SYMTAB) {
        symtab = &sh;
      }
      if (sh.sh_type == SHT_STRTAB &&
          strcmp(shstr.data() + sh.sh_name, ".strtab") == 0) {
        strtabData = new char[sh.sh_size];  // temporary heap buffer for string table
        strtabSize = sh.sh_size;
        file.seekg(static_cast<std::streamoff>(sh.sh_offset), std::ios::beg);
        file.read(const_cast<char*>(strtabData), sh.sh_size);
      }
    }

    // If both tables are present, iterate symbols looking for "timer0_millis".
    if (symtab && strtabData) {
      size_t count = symtab->sh_size / sizeof(Elf32_Sym);
      file.seekg(static_cast<std::streamoff>(symtab->sh_offset), std::ios::beg);
      std::vector<Elf32_Sym> syms(count);
      file.read(reinterpret_cast<char*>(syms.data()), symtab->sh_size);

      for (auto& sym : syms) {
        // sym.st_name is an offset into .strtab; verify it's in bounds.
        if (sym.st_name < strtabSize &&
            strcmp(strtabData + sym.st_name, "timer0_millis") == 0) {
          // Convert AVR data-space vaddr (0x80xxxx) → SRAM byte offset.
          if (sym.st_value >= 0x800000 && sym.st_value <= 0x80FFFF) {
            state.timer0_millis_addr =
                static_cast<uint16_t>(sym.st_value - 0x800000);
          }
          break;  // only need the first match
        }
      }
    }
    delete[] strtabData;  // free the heap-allocated string table copy
  }

  if (state.timer0_millis_addr != 0) {
    std::cout << "found timer0_millis at sram 0x"
              << std::hex << state.timer0_millis_addr << std::dec << '\n';
  }

  // ---------- 5. Set entry point ----------
  // The ELF e_entry field is a word address on AVR (instruction words are
  // 2 bytes each).  Our PC is a byte address, so cast directly.
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

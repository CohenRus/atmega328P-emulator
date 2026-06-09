/*
 * loader.cpp — ELF binary parsing and AVR firmware loading implementation.
 *
 * Opens a compiled AVR ELF executable and maps its contents into the emulator's
 * memory model. The loading process has four phases:
 *
 *   1. ELF header + program header table parsing.
 *   2. Pass 1 — non-writable (flash) segments (.text) loaded into state.flash[].
 *   3. Pass 2 — writable segments (.data init values into flash after .text,
 *      then copied to SRAM; .bss zeroed in SRAM).
 *   4. The ELF entry point becomes the initial program counter.
 *
 * AVR data-space virtual addresses (0x800000–0x80FFFF) are mapped to SRAM
 * offsets by subtracting 0x800000.  The ELF entry point becomes the initial PC.
 */

#include "loader.h"
#include "error.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <iostream>

namespace {

constexpr unsigned char ELFCLASS32 = 1;
constexpr unsigned char ELFDATA2LSB = 1;
constexpr unsigned char EV_CURRENT = 1;
constexpr uint16_t EM_AVR = 83;

bool rangeFits(uint64_t offset, uint64_t size, uint64_t limit) {
  return offset <= limit && size <= limit - offset;
}

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
bool loadFirmware(AvrState& state, const char* fileName) {

  // ---------- 1. Open file and parse ELF header ----------
  std::ifstream file(fileName, std::ios::binary);

  if (!file.is_open()) {
    emuErrorFile(fileName, "cannot open firmware file");
    return false;
  }

  file.seekg(0, std::ios::end);
  const std::streamoff end = file.tellg();
  if (end < static_cast<std::streamoff>(sizeof(Elf32_Ehdr))) {
    emuErrorFile(fileName, "firmware is too small to contain an ELF header");
    return false;
  }
  const uint64_t fileSize = static_cast<uint64_t>(end);
  file.seekg(0, std::ios::beg);

  // Read the 52-byte ELF executable header at offset 0.
  Elf32_Ehdr header;
  if (!file.read(reinterpret_cast<char*>(&header), sizeof(header))) {
    reportElfReadFailure(fileName, "failed to read ELF executable header");
    return false;
  }

  if (header.e_ident[0] != 0x7F || header.e_ident[1] != 'E' ||
      header.e_ident[2] != 'L' || header.e_ident[3] != 'F' ||
      header.e_ident[4] != ELFCLASS32 || header.e_ident[5] != ELFDATA2LSB ||
      header.e_ident[6] != EV_CURRENT || header.e_machine != EM_AVR ||
      header.e_version != EV_CURRENT || header.e_ehsize != sizeof(Elf32_Ehdr)) {
    emuErrorFile(fileName, "not a supported 32-bit little-endian AVR ELF file");
    return false;
  }

  // Bail out if the file lacks program headers — we need at minimum one PT_LOAD.
  if (header.e_phoff == 0 || header.e_phnum == 0) {
    std::cerr << "error: ELF has no program headers ('" << fileName << "')\n";
    return false;
  }

  if (header.e_phentsize != sizeof(Elf32_Phdr) ||
      !rangeFits(header.e_phoff,
                 static_cast<uint64_t>(header.e_phnum) * sizeof(Elf32_Phdr),
                 fileSize)) {
    emuErrorFile(fileName, "invalid ELF program header table");
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

  for (const auto& ph : phdrs) {
    if (ph.p_type != PT_LOAD) continue;
    if (ph.p_filesz > ph.p_memsz || !rangeFits(ph.p_offset, ph.p_filesz, fileSize)) {
      emuErrorFile(fileName, "invalid loadable segment size or file range");
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

    if (!rangeFits(ph.p_vaddr, ph.p_filesz, sizeof(state.flash))) {
      std::cerr << "error: text segment does not fit in flash (vaddr 0x"
                << std::hex << ph.p_vaddr << " + " << std::dec
                << ph.p_filesz << " bytes) ('" << fileName << "')\n";
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

    // Copy directly: flash byte address = ELF virtual address.
    memcpy(state.flash + ph.p_vaddr, data.data(), data.size());
    hasText = true;
    totalFlash = std::max(totalFlash, static_cast<size_t>(ph.p_vaddr + data.size()));
    nextFlashAddr = std::max(nextFlashAddr,
                             static_cast<uint32_t>(ph.p_vaddr + data.size()));

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
    if (!rangeFits(sramAddr, ph.p_memsz, sizeof(state.sram))) {
      std::cerr << "error: writable segment does not fit in SRAM (sram 0x"
                << std::hex << sramAddr << std::dec << " + " << ph.p_memsz
                << " bytes) ('" << fileName << "')\n";
      return false;
    }

    if (ph.p_filesz > 0) {
      uint32_t flashDest;
      if (ph.p_paddr >= 0x800000) {
        flashDest = nextFlashAddr;
      } else {
        flashDest = ph.p_paddr;
      }
      if (!rangeFits(flashDest, ph.p_filesz, sizeof(state.flash))) {
        std::cerr << "error: .data initial values do not fit in flash ('"
                  << fileName << "')\n";
        return false;
      }

      file.seekg(static_cast<std::streamoff>(ph.p_offset), std::ios::beg);
      std::vector<uint8_t> data(ph.p_filesz);
      if (!file.read(reinterpret_cast<char*>(data.data()), ph.p_filesz)) {
        std::cerr << "error: failed to read .data segment at offset 0x"
                  << std::hex << ph.p_offset << std::dec
                  << " ('" << fileName << "')\n";
        return false;
      }
      memcpy(state.flash + flashDest, data.data(), data.size());

      // SRAM placement: direct copy so the emulator sees initialized globals.
      memcpy(state.sram + sramAddr, data.data(), data.size());
      std::cout << "loaded .data: " << data.size() << " bytes at flash 0x"
                << std::hex << flashDest << " → sram 0x"
                << sramAddr << std::dec << '\n';
      nextFlashAddr = flashDest + static_cast<uint32_t>(data.size());
    }

    if (ph.p_memsz > ph.p_filesz) {
      // .bss — the portion of the segment beyond p_filesz is uninitialized
      // and must be zeroed in SRAM.  It has no flash footprint.
      uint32_t bssStart = sramAddr + ph.p_filesz;
      uint32_t bssSize  = ph.p_memsz - ph.p_filesz;
      memset(state.sram + bssStart, 0, bssSize);
      std::cout << "zeroed .bss: " << bssSize << " bytes at sram 0x"
                << std::hex << bssStart << std::dec << '\n';
    }
  }

  // ---------- 4. Set entry point ----------
  if (header.e_entry >= AVR_FLASH_SIZE) {
    std::cerr << "error: ELF entry point 0x" << std::hex << header.e_entry
              << std::dec << " is outside flash ('" << fileName << "')\n";
    return false;
  }
  state.pc = static_cast<uint16_t>(header.e_entry);

  std::cout << "entry point: 0x" << std::hex << header.e_entry
            << std::dec << ", total flash used: " << totalFlash << " bytes\n";
  return true;
}

/*
 * loader.h — ELF binary parser for AVR firmware loading.
 *
 * Defines the subset of ELF32 type definitions, struct layouts, and segment
 * constants needed to parse AVR GCC/avr-gcc output.  Provides loadFirmware() which
 * reads an ELF executable, extracts .text into emulator flash, sets up .data and
 * .bss initialization data, and sets the entry-point program counter.
 *
 * Key design decisions:
 * - Uses only the program header table (PHDR) for segment discovery, not section headers.
 * - AVR data-space virtual addresses (0x800000–0x80FFFF) are translated to SRAM offsets
 *   by subtracting 0x800000.
 * - .data initial values are stored in flash immediately after .text so the CRT startup
 *   routine can copy them via LPM instructions.
 * - The file is a plain C++ stream reader; no mmap or ELF library dependency.
 */
#pragma once

#include <fstream>
#include <vector>
#include "state.h"

// ----- ELF32 standard type aliases -----
// All widths are fixed-size for binary compatibility.
// Elf32_Sword is the only signed type; all others are unsigned.
#define Elf32_Half uint16_t         // unsigned 16-bit halfword
#define Elf32_Word uint32_t         // unsigned 32-bit word
#define Elf32_Sword uint32_t        // signed 32-bit word (same width; sign matters for relocations)
#define Elf32_Addr uint32_t         // virtual / physical address
#define Elf32_Off uint32_t          // file offset
#define EI_NIDENT (16)              // size of e_ident[] identification area

// ----- Program header segment types (p_type) -----
// avr-gcc typically only emits PT_LOAD; the rest are listed for completeness.
#define PT_NULL         0           // unused entry
#define PT_LOAD         1           // loadable segment (text, data, bss)
#define PT_DYNAMIC      2           // dynamic linking information
#define PT_INTERP       3           // path to program interpreter
#define PT_NOTE         4           // auxiliary note / vendor info
#define PT_SHLIB        5           // reserved (no longer used)
#define PT_PHDR         6           // entry pointing to the header table itself
#define PT_TLS          7           // thread-local storage template
#define PT_NUM          8           // number of defined generic types (0–7)
#define PT_LOOS         0x60000000  // start of OS-specific range
#define PT_GNU_EH_FRAME 0x6474e550  // GCC .eh_frame_hdr (exception unwind tables)
#define PT_GNU_STACK    0x6474e551  // stack executability flag
#define PT_GNU_RELRO    0x6474e552  // read-only after relocation
#define PT_LOSUNW       0x6ffffffa
#define PT_SUNWBSS      0x6ffffffa  // Sun Solaris .SUNW_bss
#define PT_SUNWSTACK    0x6ffffffb  // Sun Solaris stack segment
#define PT_HISUNW       0x6fffffff
#define PT_HIOS         0x6fffffff  // end of OS-specific range
#define PT_LOPROC       0x70000000  // start of processor-specific range
#define PT_HIPROC       0x7fffffff  // end of processor-specific range

// ----- Segment permission flags (p_flags) -----
#define PF_X            (1 << 0)    // executable
#define PF_W            (1 << 1)    // writable
#define PF_R            (1 << 2)    // readable
#define PF_MASKOS       0x0ff00000  // OS-specific flag mask
#define PF_MASKPROC     0xf0000000  // processor-specific flag mask

// ----- ELF32 executable header (52 bytes) -----
// The ELF identification block (e_ident) includes a 4-byte magic number
// (0x7F 'E' 'L' 'F'), class (32/64-bit), endianness, version, and padding.
struct Elf32_Ehdr {
  unsigned char e_ident[EI_NIDENT]; // 0x00: magic number, class, encoding, version, padding
  Elf32_Half    e_type;             // 0x10: object file type (ET_EXEC=2 for executables)
  Elf32_Half    e_machine;          // 0x12: target ISA (EM_AVR=83)
  Elf32_Word    e_version;          // 0x14: object file version (EV_CURRENT=1)
  Elf32_Addr    e_entry;            // 0x18: entry point virtual address → initial PC
  Elf32_Off     e_phoff;            // 0x1C: offset to program header table
  Elf32_Off     e_shoff;            // 0x20: offset to section header table (0 if stripped)
  Elf32_Word    e_flags;            // 0x24: processor-specific flags
  Elf32_Half    e_ehsize;           // 0x28: size of this header (52 bytes)
  Elf32_Half    e_phentsize;        // 0x2A: size of one program header entry (32 bytes)
  Elf32_Half    e_phnum;            // 0x2C: number of program header entries
  Elf32_Half    e_shentsize;        // 0x2E: size of one section header entry (40 bytes)
  Elf32_Half    e_shnum;            // 0x30: number of section header entries
  Elf32_Half    e_shstrndx;         // 0x32: index of section name string table
};

// ----- ELF32 program header (32 bytes) -----
// Describes a contiguous segment to be loaded into memory.
// avr-gcc emits two PT_LOAD entries: one for .text (RX) and one for .data (RW).
struct Elf32_Phdr {
  Elf32_Word p_type;                // segment type (PT_LOAD, PT_DYNAMIC, etc.)
  Elf32_Off  p_offset;              // byte offset of segment in the ELF file
  Elf32_Addr p_vaddr;               // virtual address where segment should be loaded
  Elf32_Addr p_paddr;               // physical address (usually == p_vaddr on AVR)
  Elf32_Word p_filesz;              // bytes of the segment in the file image
  Elf32_Word p_memsz;               // bytes of the segment in memory (≥ p_filesz; excess is .bss)
  Elf32_Word p_flags;               // permission flags (PF_R, PF_W, PF_X)
  Elf32_Word p_align;               // alignment (0 or 1 = no alignment, else power of 2)
};

// Load firmware from an ELF executable into emulator state.
//
// Performs a two-pass program header scan:
//   1. Non-writable segments → flash memory (typically .text).
//   2. Writable segments → flash + SRAM (.data init values, .bss zero-fill).
// Sets the program counter to the ELF entry point.
//
// @param state — mutable emulator state; flash, SRAM, and PC are written
// @param fileName — path to the ELF binary file
// @return true on success, false on any I/O error, malformed ELF, or out-of-bounds segment
bool loadFirmware(AvrState& state, const char* fileName);

# REASONIX.md

## Stack
- Language: C++20
- Build system: CMake ≥ 3.20
- WASM target: Emscripten
- No third-party deps; custom ELF loader in `src/loader.cpp`

## Layout
- `src/` — all source + headers (main, decoder, executor, loader, memory, uart, state)
- `cmake/` — Emscripten toolchain file
- `wasm/` — WASM entry point (`bindings.cpp`, not yet created)
- `tests/` — Arduino `.ino` test sketches compiled to `.elf` for emulator input

## Commands
```
# Native (desktop CLI)
cmake -B build/native
cmake --build build/native
./build/native/emulator <firmware.elf>

# WASM (web)
cmake -B build/wasm -DCMAKE_TOOLCHAIN_FILE=cmake/Emscripten.cmake
cmake --build build/wasm

# Compile test firmware
arduino-cli compile --fqbn arduino:avr:uno --export-binaries tests/sketch/
```

## Conventions
- `#pragma once` header guards
- PascalCase structs/enums (`AvrState`, `AvrOp`, `Elf32_Ehdr`)
- camelCase functions (`loadFirmware`, `executeProgram`)
- `.cpp` / `.h` paired per module, colocated in `src/`
- `-Wall -Wextra -Wpedantic` on native builds

## Watch out for
- `src/uart.h` and `src/memory.h` are empty files (0 bytes)
- `wasm/bindings.cpp` is in CMakeLists.txt but doesn't exist — WASM target won't build without it
- Native and WASM use different entry points: `src/main.cpp` vs `wasm/bindings.cpp`
- WASM build requires `EMSDK` env var pointing to an emsdk installation
- Emulator validates ELF magic bytes (`\x7f E L F`) before loading

# Native build definition for the emulator and its test suite.
# Fetches the pinned FTXUI dependency and compiles the shared emulator core.
cmake_minimum_required(VERSION 3.20)
project(atmega328p_emulator VERSION 0.1.0 LANGUAGES CXX)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
include(CTest)

include(FetchContent)
FetchContent_Declare(ftxui
  GIT_REPOSITORY https://github.com/ArthurSonzogni/ftxui
  GIT_TAG 8ce4502b25956df329a1446d3e086c079d975579
)
FetchContent_MakeAvailable(ftxui)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Core sources are shared by the CLI/TUI executable.
set(CORE_SOURCES
    src/error.cpp
    src/decoder.cpp
    src/disasm.cpp
    src/uart.cpp
    src/memory.cpp
    src/loader.cpp
    src/executor.cpp
    src/timer0.cpp
    src/interrupt.cpp
)

add_executable(emulator src/main.cpp ${CORE_SOURCES})
target_link_libraries(emulator
    PRIVATE ftxui::ftxui
    PRIVATE ftxui::screen
    PRIVATE ftxui::dom
    PRIVATE ftxui::component
)
target_compile_options(emulator PRIVATE -Wall -Wextra -Wpedantic)
add_subdirectory(tests)

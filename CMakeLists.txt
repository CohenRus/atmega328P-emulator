cmake_minimum_required(VERSION 3.20)
project(atmega328p_emulator VERSION 0.1.0 LANGUAGES CXX)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)
FetchContent_Declare(ftxui
  GIT_REPOSITORY https://github.com/ArthurSonzogni/ftxui
  GIT_TAG main
)
FetchContent_MakeAvailable(ftxui)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add source files here as you create them
set(CORE_SOURCES
    src/error.cpp
    src/decoder.cpp
    src/uart.cpp
    src/memory.cpp
    src/loader.cpp
    src/executor.cpp
)

if(EMSCRIPTEN)
    add_executable(emulator wasm/bindings.cpp ${CORE_SOURCES})

    target_include_directories(emulator PRIVATE include)

    target_link_options(emulator PRIVATE
        -sEXPORT_ES6=1
        -sMODULARIZE=1
        -sEXPORT_NAME=createEmulator
        -sALLOW_MEMORY_GROWTH=1
        --bind
    )

    set_target_properties(emulator PROPERTIES SUFFIX ".js")
else()
    add_executable(emulator src/main.cpp ${CORE_SOURCES})
    target_link_libraries(emulator
        PRIVATE ftxui::ftxui
        PRIVATE ftxui::screen
        PRIVATE ftxui::dom
        PRIVATE ftxui::component
    )
    target_compile_options(emulator PRIVATE -Wall -Wextra -Wpedantic)
    add_subdirectory(tests)
endif()

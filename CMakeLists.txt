cmake_minimum_required(VERSION 3.20)
project(atmega328p_emulator VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add source files here as you create them
set(CORE_SOURCES
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

    target_compile_options(emulator PRIVATE -Wall -Wextra -Wpedantic)
endif()

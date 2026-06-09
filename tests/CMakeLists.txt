cmake_minimum_required(VERSION 3.20)

add_executable(emulator_test
    catch2/catch_amalgamated.cpp
    test_decoder.cpp
    test_instructions.cpp
    test_timer0.cpp
    ../src/decoder.cpp
    ../src/disasm.cpp
    ../src/error.cpp
    ../src/memory.cpp
    ../src/uart.cpp
    ../src/executor.cpp
    ../src/loader.cpp
    ../src/timer0.cpp
    ../src/interrupt.cpp
)

target_include_directories(emulator_test PRIVATE catch2 ../src)
target_compile_options(emulator_test PRIVATE -Wall -Wextra -Wpedantic)

include(CTest)
add_test(NAME emulator_test COMMAND emulator_test)

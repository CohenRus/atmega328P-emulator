
## helpful resources for development

run is CMakeList changes
cmake -B build/native

build exectuable
cmake --build build/native

run exectuable with test .elf file
./build/native/emulator test/sketch/build/arduino.avr.uno/sketch.ino.elf

compile a .ino to .elf
arduino-cli compile --fqbn arduino:avr:uno --export-binaries <file-path>

elf format cheetsheat
https://gist.github.com/x0nu11byt3/bcb35c3de461e5fb66173071a2379779

avr datasheet
https://ww1.microchip.com/downloads/en/DeviceDoc/Atmel-7810-Automotive-Microcontrollers-ATmega328P_Datasheet.pdf

instruction set manual
https://ww1.microchip.com/downloads/en/DeviceDoc/AVR-InstructionSet-Manual-DS40002198.pdf

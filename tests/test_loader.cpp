/*
 * test_loader.cpp - ELF loader validation and bounds-checking tests.
 * Builds minimal temporary AVR ELF images to cover valid and malformed input.
 */

#include "catch_amalgamated.hpp"
#include "loader.h"
#include "state.h"

#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

class TempElf {
public:
    explicit TempElf(const std::vector<uint8_t>& bytes) {
        auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() /
                ("atmega328p-loader-" + std::to_string(stamp) + ".elf");
        std::ofstream file(path_, std::ios::binary);
        file.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    }

    ~TempElf() {
        std::error_code error;
        std::filesystem::remove(path_, error);
    }

    const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

template <typename T>
T readObject(const std::vector<uint8_t>& bytes, size_t offset = 0) {
    T value{};
    std::memcpy(&value, bytes.data() + offset, sizeof(value));
    return value;
}

template <typename T>
void writeObject(std::vector<uint8_t>& bytes, const T& value, size_t offset = 0) {
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
}

std::vector<uint8_t> minimalAvrElf() {
    Elf32_Ehdr header{};
    header.e_ident[0] = 0x7F;
    header.e_ident[1] = 'E';
    header.e_ident[2] = 'L';
    header.e_ident[3] = 'F';
    header.e_ident[4] = 1;
    header.e_ident[5] = 1;
    header.e_ident[6] = 1;
    header.e_type = 2;
    header.e_machine = 83;
    header.e_version = 1;
    header.e_ehsize = sizeof(Elf32_Ehdr);
    header.e_phoff = sizeof(Elf32_Ehdr);
    header.e_phentsize = sizeof(Elf32_Phdr);
    header.e_phnum = 1;

    Elf32_Phdr segment{};
    segment.p_type = PT_LOAD;
    segment.p_offset = sizeof(Elf32_Ehdr) + sizeof(Elf32_Phdr);
    segment.p_filesz = 2;
    segment.p_memsz = 2;
    segment.p_flags = PF_R | PF_X;

    std::vector<uint8_t> bytes(segment.p_offset + segment.p_filesz);
    writeObject(bytes, header);
    writeObject(bytes, segment, header.e_phoff);
    bytes[segment.p_offset] = 0x00;
    bytes[segment.p_offset + 1] = 0x00;
    return bytes;
}

} // namespace

TEST_CASE("Loader accepts a minimal AVR ELF", "[loader]") {
    TempElf elf(minimalAvrElf());
    auto path = elf.path();
    AvrState state{};

    REQUIRE(loadFirmware(state, path.c_str()));
    REQUIRE(state.flash[0] == 0x00);
    REQUIRE(state.flash[1] == 0x00);
    REQUIRE(state.pc == 0);
}

TEST_CASE("Loader rejects non-AVR ELF headers", "[loader][security]") {
    auto bytes = minimalAvrElf();
    auto header = readObject<Elf32_Ehdr>(bytes);
    header.e_machine = 62;
    writeObject(bytes, header);
    TempElf elf(bytes);
    auto path = elf.path();
    AvrState state{};

    REQUIRE_FALSE(loadFirmware(state, path.c_str()));
}

TEST_CASE("Loader rejects writable segments that exceed SRAM", "[loader][security]") {
    auto bytes = minimalAvrElf();
    auto header = readObject<Elf32_Ehdr>(bytes);
    auto text = readObject<Elf32_Phdr>(bytes, sizeof(Elf32_Ehdr));
    header.e_phnum = 2;
    text.p_offset = sizeof(Elf32_Ehdr) + 2 * sizeof(Elf32_Phdr);

    Elf32_Phdr data{};
    data.p_type = PT_LOAD;
    data.p_offset = text.p_offset + text.p_filesz;
    data.p_vaddr = 0x800000 + AVR_SRAM_SIZE - 1;
    data.p_memsz = 2;
    data.p_flags = PF_R | PF_W;

    bytes.resize(data.p_offset);
    writeObject(bytes, header);
    writeObject(bytes, text, sizeof(Elf32_Ehdr));
    writeObject(bytes, data, sizeof(Elf32_Ehdr) + sizeof(Elf32_Phdr));
    bytes[text.p_offset] = 0x00;
    bytes[text.p_offset + 1] = 0x00;
    TempElf elf(bytes);
    auto path = elf.path();
    AvrState state{};

    REQUIRE_FALSE(loadFirmware(state, path.c_str()));
}

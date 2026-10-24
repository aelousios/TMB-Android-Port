#include "ps2_memory.h"

#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <unistd.h>

namespace {
void put16(std::uint8_t* p, std::uint16_t v) { p[0] = static_cast<std::uint8_t>(v); p[1] = static_cast<std::uint8_t>(v >> 8); }
void put32(std::uint8_t* p, std::uint32_t v) {
    p[0] = static_cast<std::uint8_t>(v); p[1] = static_cast<std::uint8_t>(v >> 8);
    p[2] = static_cast<std::uint8_t>(v >> 16); p[3] = static_cast<std::uint8_t>(v >> 24);
}
void makeLoad(std::uint8_t* p, std::uint32_t offset, std::uint32_t address,
              std::uint32_t fileSize, std::uint32_t memSize, std::uint32_t flags) {
    put32(p, 1); put32(p + 4, offset); put32(p + 8, address); put32(p + 12, address);
    put32(p + 16, fileSize); put32(p + 20, memSize); put32(p + 24, flags); put32(p + 28, 16);
}
std::vector<std::uint8_t> validElf() {
    std::vector<std::uint8_t> elf(256, 0);
    elf[0] = 0x7f; elf[1] = 'E'; elf[2] = 'L'; elf[3] = 'F';
    elf[4] = 1; elf[5] = 1; elf[6] = 1;
    put16(elf.data() + 16, 2); put16(elf.data() + 18, 8); put32(elf.data() + 20, 1);
    put32(elf.data() + 24, 0x00100000); put32(elf.data() + 28, 52);
    put16(elf.data() + 40, 52); put16(elf.data() + 42, 32); put16(elf.data() + 44, 2);
    makeLoad(elf.data() + 52, 0x80, 0x00100000, 32, 32, 5);
    makeLoad(elf.data() + 84, 0xa0, 0x00100100, 16, 64, 6);
    for (std::size_t i = 0; i < 32; ++i) elf[0x80 + i] = static_cast<std::uint8_t>(i + 1);
    for (std::size_t i = 0; i < 16; ++i) elf[0xa0 + i] = static_cast<std::uint8_t>(0x80 + i);
    return elf;
}
int fdFrom(const std::vector<std::uint8_t>& bytes) {
    FILE* f = std::tmpfile(); assert(f != nullptr);
    assert(std::fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size());
    std::fflush(f); std::rewind(f);
    const int fd = dup(fileno(f)); std::fclose(f); return fd;
}
}

int main() {
    auto elf = validElf();
    // Exercise a non-zero ELF offset, as it would be embedded in an ISO.
    std::vector<std::uint8_t> disc(73, 0xAB);
    disc.insert(disc.end(), elf.begin(), elf.end());
    const int fd = fdFrom(disc);

    tmbport::Ps2EeMemory memory;
    auto result = tmbport::load_ps2_elf_into_ee_memory(fd, 73, elf.size(), memory);
    close(fd);
    assert(result.loaded);
    assert(result.entryAddress == 0x00100000);
    assert(result.loadSegments == 2);
    assert(result.fileBytes == 48);
    assert(result.memoryBytes == 96);
    assert(result.message.find("not been executed") != std::string::npos);

    std::array<std::uint8_t, 32> code{};
    assert(memory.read(0x00100000, code.data(), code.size()));
    for (std::size_t i = 0; i < code.size(); ++i) assert(code[i] == i + 1);

    std::array<std::uint8_t, 64> data{};
    assert(memory.read(0x00100100, data.data(), data.size()));
    for (std::size_t i = 0; i < 16; ++i) assert(data[i] == 0x80 + i);
    for (std::size_t i = 16; i < data.size(); ++i) assert(data[i] == 0); // BSS

    std::array<std::uint8_t, 4> alias{};
    assert(memory.read(0x80100000, alias.data(), alias.size())); // KSEG0 alias
    assert(alias[0] == 1 && alias[3] == 4);
    const std::array<std::uint8_t, 4> patch{{0xDE, 0xAD, 0xBE, 0xEF}};
    assert(memory.write(0xA0100000, patch.data(), patch.size())); // KSEG1 alias
    assert(memory.read(0x00100000, alias.data(), alias.size()));
    assert(alias == patch);

    assert(memory.supports(tmbport::Ps2EeMemory::kScratchpadBase, 16));
    const std::array<std::uint8_t, 3> scratch{{1, 3, 5}};
    assert(memory.write(tmbport::Ps2EeMemory::kScratchpadBase, scratch.data(), scratch.size()));
    std::array<std::uint8_t, 3> scratchRead{};
    assert(memory.read(tmbport::Ps2EeMemory::kScratchpadBase, scratchRead.data(), scratchRead.size()));
    assert(scratchRead == scratch);
    assert(!memory.supports(0x10000000, 16)); // outside modeled RAM
    assert(!memory.supports(0x01FFFFF0, 32)); // crosses main RAM boundary

    // Reject a load segment outside actual modeled EE RAM and reset state on error.
    elf = validElf();
    put32(elf.data() + 84 + 8, 0x10000000);
    put32(elf.data() + 84 + 12, 0x10000000);
    const int badFd = fdFrom(elf);
    result = tmbport::load_ps2_elf_into_ee_memory(badFd, 0, elf.size(), memory);
    close(badFd);
    assert(!result.loaded);
    assert(result.message.find("outside the modeled EE") != std::string::npos);
    assert(memory.read(0x00100000, alias.data(), alias.size()));
    assert((alias == std::array<std::uint8_t,4>{}));

    std::puts("PS2 EE memory loader tests passed");
    return 0;
}

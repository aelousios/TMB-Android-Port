#include "ps2_elf.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
#include <unistd.h>

namespace {
void put16(std::uint8_t* p, std::uint16_t v) {
    p[0] = static_cast<std::uint8_t>(v);
    p[1] = static_cast<std::uint8_t>(v >> 8);
}
void put32(std::uint8_t* p, std::uint32_t v) {
    p[0] = static_cast<std::uint8_t>(v);
    p[1] = static_cast<std::uint8_t>(v >> 8);
    p[2] = static_cast<std::uint8_t>(v >> 16);
    p[3] = static_cast<std::uint8_t>(v >> 24);
}
void makeLoadSegment(std::uint8_t* p, std::uint32_t offset, std::uint32_t address,
                     std::uint32_t fileSize, std::uint32_t memSize,
                     std::uint32_t flags, std::uint32_t align) {
    put32(p + 0, 1); // PT_LOAD
    put32(p + 4, offset);
    put32(p + 8, address);
    put32(p + 12, address);
    put32(p + 16, fileSize);
    put32(p + 20, memSize);
    put32(p + 24, flags);
    put32(p + 28, align);
}
std::vector<std::uint8_t> validElf() {
    std::vector<std::uint8_t> elf(256, 0);
    elf[0] = 0x7f; elf[1] = 'E'; elf[2] = 'L'; elf[3] = 'F';
    elf[4] = 1; // ELFCLASS32
    elf[5] = 1; // ELFDATA2LSB
    elf[6] = 1; // EV_CURRENT
    put16(elf.data() + 16, 2); // ET_EXEC
    put16(elf.data() + 18, 8); // EM_MIPS
    put32(elf.data() + 20, 1);
    put32(elf.data() + 24, 0x00100000);
    put32(elf.data() + 28, 52);
    put16(elf.data() + 40, 52);
    put16(elf.data() + 42, 32);
    put16(elf.data() + 44, 2);
    makeLoadSegment(elf.data() + 52, 0x80, 0x00100000, 32, 32, 5, 16);
    makeLoadSegment(elf.data() + 84, 0xa0, 0x00100100, 16, 64, 6, 16);
    return elf;
}
int fdFrom(const std::vector<std::uint8_t>& bytes) {
    FILE* f = std::tmpfile();
    assert(f != nullptr);
    assert(std::fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size());
    std::fflush(f);
    std::rewind(f);
    const int fd = dup(fileno(f));
    std::fclose(f);
    return fd;
}
}

int main() {
    auto elf = validElf();
    int fd = fdFrom(elf);
    auto result = tmbport::inspect_ps2_elf_fd(fd, 0, elf.size());
    close(fd);
    assert(result.signature_found);
    assert(result.valid);
    assert(result.entry_address == 0x00100000);
    assert(result.load_segment_count == 2);
    assert(result.load_file_bytes == 48);
    assert(result.load_memory_bytes == 96);
    assert(result.message.find("Metadata only") != std::string::npos);

    elf[18] = 3; // EM_386, not MIPS
    fd = fdFrom(elf);
    result = tmbport::inspect_ps2_elf_fd(fd, 0, elf.size());
    close(fd);
    assert(result.signature_found);
    assert(!result.valid);

    elf = validElf();
    fd = fdFrom(elf);
    result = tmbport::inspect_ps2_elf_fd(fd, 0, 40);
    close(fd);
    assert(result.signature_found);
    assert(!result.valid);

    elf = validElf();
    put32(elf.data() + 28, 0xfffffff0U); // table points outside file
    fd = fdFrom(elf);
    result = tmbport::inspect_ps2_elf_fd(fd, 0, elf.size());
    close(fd);
    assert(!result.valid);

    std::puts("PS2 ELF tests passed");
    return 0;
}

#include "iso9660.h"
#include "ps2_elf.h"

#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <unistd.h>

namespace {
constexpr std::size_t kSector = 2048;
void put16(std::uint8_t* p, std::uint16_t v) { p[0] = static_cast<std::uint8_t>(v); p[1] = static_cast<std::uint8_t>(v >> 8); }
void put32(std::uint8_t* p, std::uint32_t v) { p[0] = static_cast<std::uint8_t>(v); p[1] = static_cast<std::uint8_t>(v >> 8); p[2] = static_cast<std::uint8_t>(v >> 16); p[3] = static_cast<std::uint8_t>(v >> 24); }
void makeLoad(std::uint8_t* p, std::uint32_t offset, std::uint32_t address, std::uint32_t fileSize, std::uint32_t memSize, std::uint32_t flags) {
    put32(p, 1); put32(p + 4, offset); put32(p + 8, address); put32(p + 12, address);
    put32(p + 16, fileSize); put32(p + 20, memSize); put32(p + 24, flags); put32(p + 28, 16);
}
void put32Both(std::uint8_t* p, std::uint32_t value) {
    p[0] = static_cast<std::uint8_t>(value);
    p[1] = static_cast<std::uint8_t>(value >> 8);
    p[2] = static_cast<std::uint8_t>(value >> 16);
    p[3] = static_cast<std::uint8_t>(value >> 24);
    p[4] = static_cast<std::uint8_t>(value >> 24);
    p[5] = static_cast<std::uint8_t>(value >> 16);
    p[6] = static_cast<std::uint8_t>(value >> 8);
    p[7] = static_cast<std::uint8_t>(value);
}
std::size_t addRecord(std::uint8_t* p, std::uint32_t lba, std::uint32_t bytes,
                      bool directory, const std::vector<std::uint8_t>& name) {
    const std::size_t len = 33 + name.size() + ((name.size() % 2 == 0) ? 1 : 0);
    p[0] = static_cast<std::uint8_t>(len);
    put32Both(p + 2, lba);
    put32Both(p + 10, bytes);
    p[25] = directory ? 2 : 0;
    p[28] = 1; p[30] = 1; // volume sequence number, both endian (one)
    p[32] = static_cast<std::uint8_t>(name.size());
    std::memcpy(p + 33, name.data(), name.size());
    return len;
}
std::vector<std::uint8_t> validImage() {
    std::vector<std::uint8_t> image(kSector * 22, 0);
    auto* pvd = image.data() + kSector * 16;
    pvd[0] = 1; std::memcpy(pvd + 1, "CD001", 5); pvd[6] = 1;
    addRecord(pvd + 156, 20, kSector, true, {0});

    auto* dir = image.data() + kSector * 20;
    std::size_t offset = 0;
    offset += addRecord(dir + offset, 20, kSector, true, {0});
    offset += addRecord(dir + offset, 20, kSector, true, {1});
    const std::string target = "SCUS_971.01;1";
    addRecord(dir + offset, 21, 256, false, std::vector<std::uint8_t>(target.begin(), target.end()));
    auto* elf = image.data() + kSector * 21;
    elf[0] = 0x7f; elf[1] = 'E'; elf[2] = 'L'; elf[3] = 'F';
    elf[4] = 1; elf[5] = 1; elf[6] = 1;
    put16(elf + 16, 2); put16(elf + 18, 8); put32(elf + 20, 1);
    put32(elf + 24, 0x00100000); put32(elf + 28, 52);
    put16(elf + 40, 52); put16(elf + 42, 32); put16(elf + 44, 2);
    makeLoad(elf + 52, 0x80, 0x00100000, 32, 32, 5);
    makeLoad(elf + 84, 0xa0, 0x00100100, 16, 64, 6);
    return image;
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
    auto good = validImage();
    int fd = fdFrom(good);
    auto result = tmbport::inspect_iso_fd(fd);
    close(fd);
    assert(result.valid_iso9660);
    assert(result.target_file_found);
    assert(result.elf_magic_found);
    assert(result.file_lba == 21);
    assert(result.file_size == 256);
    assert(result.ps2_elf_valid);
    assert(result.elf_entry_address == 0x00100000);
    assert(result.load_segment_count == 2);

    auto invalid = validImage();
    invalid[kSector * 16 + 1] = 'X';
    fd = fdFrom(invalid);
    result = tmbport::inspect_iso_fd(fd);
    close(fd);
    assert(!result.valid_iso9660);

    auto wrongGame = validImage();
    const std::string wrong = "OTHER_FILE";
    auto* dir = wrongGame.data() + kSector * 20;
    std::size_t offset = 0;
    offset += dir[0]; offset += dir[offset];
    std::memset(dir + offset + 33, 0, 11);
    std::memcpy(dir + offset + 33, wrong.data(), wrong.size());
    dir[offset + 32] = static_cast<std::uint8_t>(wrong.size());
    fd = fdFrom(wrongGame);
    result = tmbport::inspect_iso_fd(fd);
    close(fd);
    assert(result.valid_iso9660);
    assert(!result.target_file_found);

    std::puts("iso9660 tests passed");
    return 0;
}

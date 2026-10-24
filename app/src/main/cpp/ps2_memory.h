#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace tmbport {

// A small, explicit model of the PS2 Emotion Engine's directly addressable
// main RAM and scratchpad. This is guest memory, not a mapping into host memory.
// It does not model caches, DMA, devices, the IOP, VU units, or GS registers.
class Ps2EeMemory {
public:
    static constexpr std::uint32_t kMainRamBytes = 32U * 1024U * 1024U;
    static constexpr std::uint32_t kScratchpadBase = 0x70000000U;
    static constexpr std::uint32_t kScratchpadBytes = 16U * 1024U;

    Ps2EeMemory();

    void clear();
    bool supports(std::uint32_t guestAddress, std::size_t length) const;
    bool read(std::uint32_t guestAddress, void* output, std::size_t length) const;
    bool write(std::uint32_t guestAddress, const void* input, std::size_t length);
    bool zero(std::uint32_t guestAddress, std::size_t length);

private:
    enum class Region { MainRam, Scratchpad };
    struct Translation {
        Region region;
        std::size_t offset;
    };

    bool translate(std::uint32_t guestAddress, std::size_t length, Translation& out) const;
    std::vector<std::uint8_t> mainRam_;
    std::vector<std::uint8_t> scratchpad_;
};

struct Ps2MemoryLoadResult {
    bool loaded = false;
    std::uint32_t entryAddress = 0;
    std::uint32_t loadSegments = 0;
    std::uint64_t fileBytes = 0;
    std::uint64_t memoryBytes = 0;
    std::string message;
};

// Validates and stages all PT_LOAD segments from a PS2 ELF inside the modeled
// guest memory. baseOffset points to the ELF within the user-selected ISO.
// PT_LOAD bytes are copied into guest RAM and p_memsz - p_filesz bytes are zeroed
// for BSS. No guest instructions are executed.
Ps2MemoryLoadResult load_ps2_elf_into_ee_memory(
    int fd, std::uint64_t baseOffset, std::uint64_t fileSize,
    Ps2EeMemory& memory);

} // namespace tmbport

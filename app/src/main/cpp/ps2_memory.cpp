#include "ps2_memory.h"

#include "ps2_elf.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <sys/types.h>
#include <unistd.h>

namespace tmbport {
namespace {
constexpr std::uint64_t kElf32HeaderSize = 52;
constexpr std::uint64_t kElf32ProgramHeaderSize = 32;
constexpr std::uint32_t kPtLoad = 1;
constexpr std::uint32_t kPfExecute = 1;
constexpr std::uint16_t kMaxProgramHeaders = 128;
constexpr std::size_t kIoChunkBytes = 64U * 1024U;

std::uint16_t read_le16(const std::uint8_t* p) {
    return static_cast<std::uint16_t>(p[0]) |
           static_cast<std::uint16_t>(static_cast<std::uint16_t>(p[1]) << 8);
}

std::uint32_t read_le32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) |
           (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) |
           (static_cast<std::uint32_t>(p[3]) << 24);
}

bool within(std::uint64_t offset, std::uint64_t size, std::uint64_t limit) {
    return offset <= limit && size <= limit - offset;
}

bool pread_exact(int fd, void* output, std::size_t size, std::uint64_t offset) {
    if (offset > static_cast<std::uint64_t>(std::numeric_limits<off_t>::max())) return false;
    auto* bytes = static_cast<std::uint8_t*>(output);
    std::size_t readBytes = 0;
    while (readBytes < size) {
        const std::uint64_t at = offset + readBytes;
        if (at < offset || at > static_cast<std::uint64_t>(std::numeric_limits<off_t>::max())) return false;
        const ssize_t got = pread(fd, bytes + readBytes, size - readBytes, static_cast<off_t>(at));
        if (got <= 0) return false;
        readBytes += static_cast<std::size_t>(got);
    }
    return true;
}

std::string address_string(std::uint32_t address) {
    static constexpr char digits[] = "0123456789ABCDEF";
    std::string out = "0x00000000";
    for (int i = 0; i < 8; ++i) out[9 - i] = digits[(address >> (i * 4)) & 0x0fU];
    return out;
}

struct Segment {
    std::uint32_t offset = 0;
    std::uint32_t address = 0;
    std::uint32_t fileSize = 0;
    std::uint32_t memorySize = 0;
    std::uint32_t flags = 0;
};
} // namespace

Ps2EeMemory::Ps2EeMemory()
    : mainRam_(kMainRamBytes, 0), scratchpad_(kScratchpadBytes, 0) {}

void Ps2EeMemory::clear() {
    std::fill(mainRam_.begin(), mainRam_.end(), 0);
    std::fill(scratchpad_.begin(), scratchpad_.end(), 0);
}

bool Ps2EeMemory::translate(std::uint32_t address, std::size_t length, Translation& out) const {
    // KUSEG/direct physical low addresses.
    if (address < kMainRamBytes) {
        if (length > static_cast<std::size_t>(kMainRamBytes - address)) return false;
        out = {Region::MainRam, address};
        return true;
    }

    // KSEG0 cached and KSEG1 uncached aliases of the first 32 MiB of RAM.
    constexpr std::uint32_t kKseg0Base = 0x80000000U;
    constexpr std::uint32_t kKseg1Base = 0xA0000000U;
    if (address >= kKseg0Base && address - kKseg0Base < kMainRamBytes) {
        const std::uint32_t offset = address - kKseg0Base;
        if (length > static_cast<std::size_t>(kMainRamBytes - offset)) return false;
        out = {Region::MainRam, offset};
        return true;
    }
    if (address >= kKseg1Base && address - kKseg1Base < kMainRamBytes) {
        const std::uint32_t offset = address - kKseg1Base;
        if (length > static_cast<std::size_t>(kMainRamBytes - offset)) return false;
        out = {Region::MainRam, offset};
        return true;
    }

    // EE scratchpad (16 KiB). Device/MMIO space is intentionally not handled.
    if (address >= kScratchpadBase && address - kScratchpadBase < kScratchpadBytes) {
        const std::uint32_t offset = address - kScratchpadBase;
        if (length > static_cast<std::size_t>(kScratchpadBytes - offset)) return false;
        out = {Region::Scratchpad, offset};
        return true;
    }
    return false;
}

bool Ps2EeMemory::supports(std::uint32_t address, std::size_t length) const {
    Translation ignored{};
    return translate(address, length, ignored);
}

bool Ps2EeMemory::read(std::uint32_t address, void* output, std::size_t length) const {
    if (length == 0) return true;
    if (output == nullptr) return false;
    Translation mapped{};
    if (!translate(address, length, mapped)) return false;
    const auto& region = mapped.region == Region::MainRam ? mainRam_ : scratchpad_;
    std::memcpy(output, region.data() + mapped.offset, length);
    return true;
}

bool Ps2EeMemory::write(std::uint32_t address, const void* input, std::size_t length) {
    if (length == 0) return true;
    if (input == nullptr) return false;
    Translation mapped{};
    if (!translate(address, length, mapped)) return false;
    auto& region = mapped.region == Region::MainRam ? mainRam_ : scratchpad_;
    std::memcpy(region.data() + mapped.offset, input, length);
    return true;
}

bool Ps2EeMemory::zero(std::uint32_t address, std::size_t length) {
    if (length == 0) return true;
    Translation mapped{};
    if (!translate(address, length, mapped)) return false;
    auto& region = mapped.region == Region::MainRam ? mainRam_ : scratchpad_;
    std::memset(region.data() + mapped.offset, 0, length);
    return true;
}

Ps2MemoryLoadResult load_ps2_elf_into_ee_memory(
    int fd, std::uint64_t baseOffset, std::uint64_t fileSize, Ps2EeMemory& memory) {
    Ps2MemoryLoadResult result;
    memory.clear();
    const auto fail = [&memory, &result](const std::string& reason) {
        memory.clear();
        result.loaded = false;
        result.message = reason;
        return result;
    };

    const Ps2ElfInspection inspection = inspect_ps2_elf_fd(fd, baseOffset, fileSize);
    if (!inspection.valid) return fail("ELF validation failed: " + inspection.message);
    result.entryAddress = inspection.entry_address;

    std::array<std::uint8_t, kElf32HeaderSize> header{};
    if (!pread_exact(fd, header.data(), header.size(), baseOffset))
        return fail("Could not read ELF header while staging guest memory.");
    const std::uint32_t phoff = read_le32(header.data() + 28);
    const std::uint16_t phentsize = read_le16(header.data() + 42);
    const std::uint16_t phnum = read_le16(header.data() + 44);
    const std::uint64_t tableSize = static_cast<std::uint64_t>(phentsize) * phnum;
    if (phentsize < kElf32ProgramHeaderSize || phnum == 0 || phnum > kMaxProgramHeaders ||
        !within(phoff, tableSize, fileSize)) {
        return fail("Unsupported or out-of-bounds ELF program-header table.");
    }

    std::vector<Segment> segments;
    segments.reserve(phnum);
    for (std::uint16_t i = 0; i < phnum; ++i) {
        const std::uint64_t relative = static_cast<std::uint64_t>(phoff) +
                                       static_cast<std::uint64_t>(i) * phentsize;
        std::array<std::uint8_t, kElf32ProgramHeaderSize> ph{};
        if (!within(relative, ph.size(), fileSize) ||
            !pread_exact(fd, ph.data(), ph.size(), baseOffset + relative)) {
            return fail("Could not read an ELF program header.");
        }
        if (read_le32(ph.data()) != kPtLoad) continue;
        Segment s;
        s.offset = read_le32(ph.data() + 4);
        s.address = read_le32(ph.data() + 8);
        s.fileSize = read_le32(ph.data() + 16);
        s.memorySize = read_le32(ph.data() + 20);
        s.flags = read_le32(ph.data() + 24);
        if (s.fileSize > s.memorySize || !within(s.offset, s.fileSize, fileSize))
            return fail("An ELF PT_LOAD segment extends beyond the executable or has invalid sizes.");
        if (s.memorySize > 0 && !memory.supports(s.address, s.memorySize)) {
            return fail("PT_LOAD segment at " + address_string(s.address) +
                        " is outside the modeled EE main RAM/scratchpad address ranges.");
        }
        if (s.memorySize > 0) segments.push_back(s);
    }
    if (segments.empty()) return fail("ELF contains no non-empty PT_LOAD segments.");

    bool entryExecutable = false;
    for (const Segment& s : segments) {
        const std::uint64_t end = static_cast<std::uint64_t>(s.address) + s.memorySize;
        if ((s.flags & kPfExecute) != 0 && result.entryAddress >= s.address && result.entryAddress < end)
            entryExecutable = true;
    }
    if (!entryExecutable) return fail("ELF entry point is not inside an executable PT_LOAD segment.");

    std::array<std::uint8_t, kIoChunkBytes> buffer{};
    for (const Segment& s : segments) {
        if (!memory.zero(s.address, s.memorySize))
            return fail("Failed to zero a PT_LOAD destination in modeled EE memory.");
        std::uint64_t copied = 0;
        while (copied < s.fileSize) {
            const std::size_t amount = static_cast<std::size_t>(
                std::min<std::uint64_t>(buffer.size(), s.fileSize - copied));
            const std::uint64_t sourceOffset = baseOffset + s.offset + copied;
            if (sourceOffset < baseOffset ||
                !pread_exact(fd, buffer.data(), amount, sourceOffset) ||
                !memory.write(static_cast<std::uint32_t>(s.address + copied), buffer.data(), amount)) {
                return fail("Could not copy a PT_LOAD segment into modeled EE memory.");
            }
            copied += amount;
        }
        result.fileBytes += s.fileSize;
        result.memoryBytes += s.memorySize;
        ++result.loadSegments;
    }

    result.loaded = true;
    result.message = "Staged " + std::to_string(result.loadSegments) + " PT_LOAD segments (" +
                     std::to_string(result.fileBytes) + " file bytes, " +
                     std::to_string(result.memoryBytes) + " memory bytes) into a modeled 32 MiB EE RAM + 16 KiB scratchpad image. Entry " +
                     address_string(result.entryAddress) + ". Guest MIPS code has not been executed; PS2 device/runtime support is still missing.";
    return result;
}

} // namespace tmbport

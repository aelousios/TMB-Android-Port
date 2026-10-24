#include "ps2_elf.h"

#include <array>
#include <cstdint>
#include <limits>
#include <sys/types.h>
#include <unistd.h>

namespace tmbport {
namespace {
constexpr std::uint64_t kElf32HeaderSize = 52;
constexpr std::uint64_t kElf32ProgramHeaderSize = 32;
constexpr std::uint16_t kElfTypeExecutable = 2;
constexpr std::uint16_t kElfMachineMips = 8;
constexpr std::uint32_t kProgramTypeLoad = 1;
constexpr std::uint16_t kMaxProgramHeaders = 128;

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

bool read_exact_at(int fd, void* output, std::size_t size, std::uint64_t offset) {
    if (offset > static_cast<std::uint64_t>(std::numeric_limits<off_t>::max())) return false;
    auto* dst = static_cast<std::uint8_t*>(output);
    std::size_t total = 0;
    while (total < size) {
        const std::uint64_t absolute = offset + total;
        if (absolute < offset || absolute > static_cast<std::uint64_t>(std::numeric_limits<off_t>::max())) return false;
        const ssize_t got = pread(fd, dst + total, size - total, static_cast<off_t>(absolute));
        if (got <= 0) return false;
        total += static_cast<std::size_t>(got);
    }
    return true;
}

std::string hex_address(std::uint32_t value) {
    static constexpr char digits[] = "0123456789ABCDEF";
    std::string out = "0x00000000";
    for (int i = 0; i < 8; ++i) {
        out[9 - i] = digits[(value >> (i * 4)) & 0x0fU];
    }
    return out;
}
} // namespace

Ps2ElfInspection inspect_ps2_elf_fd(int fd, std::uint64_t base_offset,
                                    std::uint64_t file_size) {
    Ps2ElfInspection result;
    if (fd < 0) {
        result.message = "Invalid file descriptor.";
        return result;
    }
    if (!within(base_offset, file_size,
                static_cast<std::uint64_t>(std::numeric_limits<off_t>::max()))) {
        result.message = "Executable range exceeds supported file offsets.";
        return result;
    }
    if (file_size < 4) {
        result.message = "Executable is too short to contain an ELF signature.";
        return result;
    }

    std::array<std::uint8_t, 4> signature{};
    if (!read_exact_at(fd, signature.data(), signature.size(), base_offset)) {
        result.message = "Could not read executable signature.";
        return result;
    }
    result.signature_found = signature[0] == 0x7f && signature[1] == 'E' &&
                             signature[2] == 'L' && signature[3] == 'F';
    if (!result.signature_found) {
        result.message = "The executable does not start with the ELF signature.";
        return result;
    }
    if (file_size < kElf32HeaderSize) {
        result.message = "ELF signature found, but the ELF32 header is truncated.";
        return result;
    }

    std::array<std::uint8_t, kElf32HeaderSize> header{};
    if (!read_exact_at(fd, header.data(), header.size(), base_offset)) {
        result.message = "Could not read the complete ELF32 header.";
        return result;
    }
    if (header[4] != 1) {
        result.message = "ELF signature found, but the executable is not ELF32.";
        return result;
    }
    if (header[5] != 1) {
        result.message = "ELF signature found, but the executable is not little-endian.";
        return result;
    }
    if (header[6] != 1 || read_le32(header.data() + 20) != 1) {
        result.message = "ELF header has an unsupported version.";
        return result;
    }
    if (read_le16(header.data() + 16) != kElfTypeExecutable) {
        result.message = "ELF is not a fixed-address executable (ET_EXEC).";
        return result;
    }
    if (read_le16(header.data() + 18) != kElfMachineMips) {
        result.message = "ELF machine is not MIPS; it is not the expected PS2 executable type.";
        return result;
    }
    if (read_le16(header.data() + 40) < kElf32HeaderSize) {
        result.message = "ELF header size is invalid.";
        return result;
    }

    const std::uint32_t programHeaderOffset = read_le32(header.data() + 28);
    const std::uint16_t programHeaderEntrySize = read_le16(header.data() + 42);
    const std::uint16_t programHeaderCount = read_le16(header.data() + 44);
    if (programHeaderEntrySize < kElf32ProgramHeaderSize || programHeaderCount == 0 ||
        programHeaderCount > kMaxProgramHeaders) {
        result.message = "ELF program-header table has unsupported dimensions.";
        return result;
    }
    const std::uint64_t tableBytes = static_cast<std::uint64_t>(programHeaderEntrySize) * programHeaderCount;
    if (!within(programHeaderOffset, tableBytes, file_size)) {
        result.message = "ELF program-header table extends beyond the executable.";
        return result;
    }

    result.entry_address = read_le32(header.data() + 24);
    bool entryMapped = false;
    for (std::uint16_t i = 0; i < programHeaderCount; ++i) {
        const std::uint64_t relative = static_cast<std::uint64_t>(programHeaderOffset) +
                                       static_cast<std::uint64_t>(i) * programHeaderEntrySize;
        std::array<std::uint8_t, kElf32ProgramHeaderSize> ph{};
        if (!within(relative, ph.size(), file_size) ||
            !read_exact_at(fd, ph.data(), ph.size(), base_offset + relative)) {
            result.message = "Could not read an ELF program header.";
            return result;
        }
        if (read_le32(ph.data()) != kProgramTypeLoad) continue;

        const std::uint32_t pOffset = read_le32(ph.data() + 4);
        const std::uint32_t pVaddr = read_le32(ph.data() + 8);
        const std::uint32_t pFileSize = read_le32(ph.data() + 16);
        const std::uint32_t pMemSize = read_le32(ph.data() + 20);
        const std::uint32_t pAlign = read_le32(ph.data() + 28);
        if (pFileSize > pMemSize || !within(pOffset, pFileSize, file_size) ||
            static_cast<std::uint64_t>(pVaddr) + pMemSize > 0x100000000ULL) {
            result.message = "An ELF load segment has invalid file or memory bounds.";
            return result;
        }
        if (pAlign > 1 && ((pAlign & (pAlign - 1)) != 0 || pOffset % pAlign != pVaddr % pAlign)) {
            result.message = "An ELF load segment has invalid alignment.";
            return result;
        }

        ++result.load_segment_count;
        result.load_file_bytes += pFileSize;
        result.load_memory_bytes += pMemSize;
        if (result.entry_address >= pVaddr &&
            static_cast<std::uint64_t>(result.entry_address) < static_cast<std::uint64_t>(pVaddr) + pMemSize) {
            entryMapped = true;
        }
    }

    if (result.load_segment_count == 0) {
        result.message = "ELF has no PT_LOAD segments.";
        return result;
    }
    if (!entryMapped) {
        result.message = "ELF entry point is not covered by any PT_LOAD segment.";
        return result;
    }

    result.valid = true;
    result.message = "Valid ELF32 little-endian MIPS executable. Entry " +
                     hex_address(result.entry_address) + "; load segments " +
                     std::to_string(result.load_segment_count) + "; file-backed bytes " +
                     std::to_string(result.load_file_bytes) + "; mapped bytes including BSS " +
                     std::to_string(result.load_memory_bytes) + ". Metadata only; no code was loaded or executed.";
    return result;
}

} // namespace tmbport

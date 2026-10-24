#pragma once

#include <cstdint>
#include <string>

namespace tmbport {

// Metadata-only validation for a PS2 game's ELF32 executable stored at an offset
// inside a larger file (normally the user's ISO). This deliberately does not
// map or execute MIPS code on Android.
struct Ps2ElfInspection {
    bool signature_found = false;
    bool valid = false;
    std::uint32_t entry_address = 0;
    std::uint16_t load_segment_count = 0;
    std::uint64_t load_file_bytes = 0;
    std::uint64_t load_memory_bytes = 0;
    std::string message;
};

// All offsets in the ELF header and program headers are relative to the ELF
// file, while base_offset is the location of that ELF inside the opened file.
// Reads are bounded by file_size and never modify the image.
Ps2ElfInspection inspect_ps2_elf_fd(int fd, std::uint64_t base_offset,
                                    std::uint64_t file_size);

} // namespace tmbport

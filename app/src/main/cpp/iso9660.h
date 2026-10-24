#pragma once

#include <cstdint>
#include <string>

namespace tmbport {

struct IsoInspection {
    bool valid_iso9660 = false;
    bool target_file_found = false;
    bool elf_magic_found = false;
    unsigned int file_lba = 0;
    unsigned int file_size = 0;
    bool ps2_elf_valid = false;
    std::uint32_t elf_entry_address = 0;
    unsigned int load_segment_count = 0;
    std::uint64_t load_file_bytes = 0;
    std::uint64_t load_memory_bytes = 0;
    std::string message;
};

// Inspects an already-open, seekable ISO file descriptor. It never modifies the file.
// The implementation is platform-independent and is also host-testable.
IsoInspection inspect_iso_fd(int fd);

} // namespace tmbport

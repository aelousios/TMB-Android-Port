#include "iso9660.h"
#include "ps2_elf.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <set>
#include <string>
#include <sys/types.h>
#include <unistd.h>
#include <vector>

namespace tmbport {
namespace {
constexpr std::size_t kSectorSize = 2048;
constexpr off_t kPvdSector = 16;
constexpr std::size_t kMaxEntries = 20000;
constexpr int kMaxDepth = 12;
bool is_tmb_executable(const std::string& name) {
    return name == "SCUS_971.01" ||  // US original
           name == "SCUS_971.79" ||  // US Greatest Hits
           name == "SCES_503.60";    // PAL
}

std::uint32_t read_le32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) |
           (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) |
           (static_cast<std::uint32_t>(p[3]) << 24);
}

bool read_exact_at(int fd, void* output, std::size_t size, off_t offset) {
    auto* dst = static_cast<std::uint8_t*>(output);
    std::size_t total = 0;
    while (total < size) {
        const ssize_t got = pread(fd, dst + total, size - total, offset + static_cast<off_t>(total));
        if (got <= 0) return false;
        total += static_cast<std::size_t>(got);
    }
    return true;
}

struct Directory {
    std::uint32_t lba;
    std::uint32_t bytes;
    int depth;
};

std::string normalize_identifier(const std::uint8_t* bytes, std::size_t len) {
    std::string name(reinterpret_cast<const char*>(bytes), len);
    // ISO-9660's file-version suffix is introduced by ';' (often ';1').
    // Do not strip a numeric dotted suffix: SCUS_971.01 is the executable's
    // literal identifier, not a basename plus extension.
    const auto semicolon = name.find(';');
    if (semicolon != std::string::npos) name.resize(semicolon);
    std::transform(name.begin(), name.end(), name.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
    return name;
}

bool is_directory(const std::uint8_t* record) { return (record[25] & 0x02U) != 0; }

} // namespace

IsoInspection inspect_iso_fd(int fd) {
    IsoInspection result;
    if (fd < 0) {
        result.message = "Invalid file handle.";
        return result;
    }

    std::array<std::uint8_t, kSectorSize> pvd{};
    if (!read_exact_at(fd, pvd.data(), pvd.size(), kPvdSector * static_cast<off_t>(kSectorSize))) {
        result.message = "Could not read sector 16. Select a complete, seekable PS2 ISO file.";
        return result;
    }
    if (pvd[0] != 1 || std::memcmp(pvd.data() + 1, "CD001", 5) != 0 || pvd[6] != 1) {
        result.message = "No ISO-9660 primary volume descriptor at sector 16. This may be a CHD, BIN/CUE, encrypted, or invalid image.";
        return result;
    }
    result.valid_iso9660 = true;

    constexpr std::size_t rootRecordOffset = 156;
    const std::uint8_t rootRecordLength = pvd[rootRecordOffset];
    if (rootRecordLength < 34 || rootRecordOffset + rootRecordLength > pvd.size()) {
        result.message = "ISO-9660 descriptor found, but its root directory record is malformed.";
        return result;
    }

    std::vector<Directory> queue;
    queue.push_back({read_le32(pvd.data() + rootRecordOffset + 2),
                     read_le32(pvd.data() + rootRecordOffset + 10), 0});
    std::set<std::pair<std::uint32_t, std::uint32_t>> visited;
    std::size_t entriesSeen = 0;

    while (!queue.empty() && entriesSeen < kMaxEntries) {
        const Directory dir = queue.back();
        queue.pop_back();
        if (dir.depth > kMaxDepth || dir.bytes == 0 ||
            !visited.emplace(dir.lba, dir.bytes).second) continue;

        const std::uint64_t dirOffset = static_cast<std::uint64_t>(dir.lba) * kSectorSize;
        std::uint32_t consumed = 0;
        while (consumed < dir.bytes && entriesSeen < kMaxEntries) {
            const std::uint32_t sectorOffset = consumed % static_cast<std::uint32_t>(kSectorSize);
            const std::uint32_t bytesRemainingInSector = static_cast<std::uint32_t>(kSectorSize) - sectorOffset;
            const std::uint32_t bytesRemainingInDirectory = dir.bytes - consumed;
            const std::size_t amount = std::min<std::uint32_t>(bytesRemainingInSector, bytesRemainingInDirectory);
            std::vector<std::uint8_t> block(amount);
            if (!read_exact_at(fd, block.data(), amount,
                               static_cast<off_t>(dirOffset + consumed))) {
                result.message = "ISO directory data is truncated or unreadable.";
                return result;
            }

            std::size_t pos = 0;
            while (pos < block.size() && entriesSeen < kMaxEntries) {
                const std::uint8_t recordLength = block[pos];
                if (recordLength == 0) break; // Remaining bytes in this sector are padding.
                if (recordLength < 34 || pos + recordLength > block.size()) break;

                const std::uint8_t* record = block.data() + pos;
                const std::uint8_t nameLength = record[32];
                if (33U + nameLength <= recordLength && nameLength > 0) {
                    // ISO special directory entries use one-byte identifiers 0 and 1.
                    if (!(nameLength == 1 && (record[33] == 0 || record[33] == 1))) {
                        const std::string name = normalize_identifier(record + 33, nameLength);
                        const std::uint32_t lba = read_le32(record + 2);
                        const std::uint32_t bytes = read_le32(record + 10);
                        if (!is_directory(record) && is_tmb_executable(name)) {
                            result.target_file_found = true;
                            result.file_lba = lba;
                            result.file_size = bytes;
                            const auto elf = inspect_ps2_elf_fd(
                                fd, static_cast<std::uint64_t>(lba) * kSectorSize, bytes);
                            result.elf_magic_found = elf.signature_found;
                            result.ps2_elf_valid = elf.valid;
                            result.elf_entry_address = elf.entry_address;
                            result.load_segment_count = elf.load_segment_count;
                            result.load_file_bytes = elf.load_file_bytes;
                            result.load_memory_bytes = elf.load_memory_bytes;
                            if (elf.valid) {
                                if (name == "SCUS_971.01") {
                                    result.message = "Found US NTSC-U executable " + name + ". " + elf.message;
                                } else {
                                    result.message = "Recognized TMB executable " + name + ". " + elf.message +
                                        " This edition has been recognized for inspection and RAM staging only; "
                                        "game boot has not been verified.";
                                }
                            } else {
                                result.message = "Found candidate TMB executable " + name +
                                    ", but PS2 ELF validation failed: " + elf.message;
                            }
                            return result;
                        }
                        if (is_directory(record) && dir.depth < kMaxDepth && bytes > 0) {
                            queue.push_back({lba, bytes, dir.depth + 1});
                        }
                    }
                }
                ++entriesSeen;
                pos += recordLength;
            }
            // ISO directory records are not allowed to cross a logical sector. Padding is skipped above.
            consumed += static_cast<std::uint32_t>(amount);
        }
    }

    result.message = result.valid_iso9660
        ? "Valid ISO-9660 image, but no known TMB executable was found. "
          "Checked SCUS_971.01 (US original), SCUS_971.79 (US Greatest Hits), and SCES_503.60 (PAL)."
        : "This is not a supported ISO image.";
    return result;
}

} // namespace tmbport

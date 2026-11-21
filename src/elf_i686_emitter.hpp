#pragma once

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

// Standalone ELF32 image with separate RX and RW mappings, no interpreter.
class ElfI686Emitter {
    static constexpr uint32_t imageBase = 0x08048000;
    static constexpr uint32_t pageSize = 0x1000;

public:
    static constexpr uint32_t textRva = pageSize;

    static uint32_t dataRva(size_t textSize) {
        return (textRva + static_cast<uint32_t>(textSize) + pageSize - 1) & ~(pageSize - 1);
    }

    static bool write(const std::string& path, const std::vector<uint8_t>& code,
                      const std::vector<uint8_t>& data, bool module = false) {
        const uint32_t dataOffset = dataRva(code.size());
        std::vector<uint8_t> image(dataOffset + data.size(), 0);
        auto put = [&](size_t offset, uint32_t value, size_t size = 4) {
            for (size_t i = 0; i < size; ++i) image[offset + i] = uint8_t(value >> (8 * i));
        };
        image[0] = 0x7f; image[1] = 'E'; image[2] = 'L'; image[3] = 'F';
        image[4] = 1; image[5] = 1; image[6] = 1; // ELF32, little-endian, current version
        put(16, module ? 3 : 2, 2); // ET_DYN or ET_EXEC
        put(18, 3, 2); // EM_386
        put(20, 1);
        put(24, module ? 0 : imageBase + textRva);
        put(28, 52); // Program headers immediately follow the ELF header.
        put(40, 52, 2);
        put(42, 32, 2);
        put(44, 3, 2);

        auto loadSegment = [&](size_t ph, uint32_t off, uint32_t size, uint32_t flags) {
            put(ph, 1); // PT_LOAD
            put(ph + 4, off);
            put(ph + 8, module ? off : imageBase + off);
            put(ph + 12, module ? off : imageBase + off);
            put(ph + 16, size);
            put(ph + 20, size);
            put(ph + 24, flags);
            put(ph + 28, pageSize);
        };
        loadSegment(52, 0, dataOffset, 5); // PF_R | PF_X
        loadSegment(84, dataOffset, static_cast<uint32_t>(data.size()), 6); // PF_R | PF_W
        put(116, 0x6474e551); // PT_GNU_STACK
        put(140, 6); // PF_R | PF_W
        put(144, 16); // stack alignment
        std::copy(code.begin(), code.end(), image.begin() + textRva);
        std::copy(data.begin(), data.end(), image.begin() + dataOffset);

        std::ofstream out(path, std::ios::binary);
        if (!out.write(reinterpret_cast<const char*>(image.data()), image.size())) return false;
        out.close();
        if (!out) return false;
#ifndef _WIN32
        if (!module) {
            std::error_code error;
            std::filesystem::permissions(path, std::filesystem::perms::owner_exec |
                                              std::filesystem::perms::group_exec |
                                              std::filesystem::perms::others_exec,
                                         std::filesystem::perm_options::add, error);
            if (error) return false;
        }
#endif
        return true;
    }
};

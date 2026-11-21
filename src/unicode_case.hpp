#pragma once

#include "unicode_case_data.hpp"
#include <ostream>

namespace thava_unicode {

inline void emitCppCaseConversion(std::ostream& out) {
    out << "inline constexpr uint64_t _thava_case_mappings[][4] = {\n";
    for (const auto& row : caseMappings) {
        out << "    {" << row[0] << "ULL, " << row[1] << "ULL, "
            << row[2] << "ULL, " << row[3] << "ULL},\n";
    }
    out << R"(
};

inline std::string _thava_str_convert_case(const std::string& s, unsigned mode) {
    std::string out;
    out.reserve(s.size());
    bool newWord = true;
    for (size_t i = 0; i < s.size();) {
        unsigned first = static_cast<unsigned char>(s[i++]);
        uint64_t key = first;
        if (first >= 0xc2 && first <= 0xf4) {
            unsigned width = first < 0xe0 ? 2 : (first < 0xf0 ? 3 : 4);
            for (unsigned byte = 1; byte < width && i < s.size(); ++byte) {
                unsigned next = static_cast<unsigned char>(s[i]);
                if (next < 0x80 || next > 0xbf) break;
                key |= uint64_t(next) << (8 * byte);
                ++i;
            }
        }

        uint64_t mapped = key;
        size_t low = 0, high = sizeof(_thava_case_mappings) / sizeof(_thava_case_mappings[0]);
        while (low < high) {
            size_t mid = low + (high - low) / 2;
            const auto& row = _thava_case_mappings[mid];
            if (row[0] < key) {
                low = mid + 1;
            } else if (row[0] > key) {
                high = mid;
            } else {
                if (mode == 3) {
                    mapped = row[newWord ? 3 : 1];
                    if (row[3] == 0) mapped = key;
                    newWord = row[3] == 0;
                } else {
                    mapped = row[mode];
                }
                break;
            }
        }
        if (low == high) newWord = false;
        do {
            out += static_cast<char>(mapped & 0xff);
            mapped >>= 8;
        } while (mapped);
    }
    return out;
}

inline std::string _thava_str_lower(const std::string& s) {
    return _thava_str_convert_case(s, 1);
}
inline std::string _thava_str_upper(const std::string& s) {
    return _thava_str_convert_case(s, 2);
}
inline std::string _thava_str_title(const std::string& s) {
    return _thava_str_convert_case(s, 3);
}

)";
}

template<typename Emitter>
uint32_t addCaseMappings(Emitter& emitter) {
    const auto offset = emitter.addDataSlot(sizeof(caseMappings));
    uint32_t field = offset;
    for (const auto& row : caseMappings) {
        for (auto value : row) {
            emitter.setDataSlotValue(field, value);
            field += sizeof(value);
        }
    }
    return offset;
}

} // namespace thava_unicode

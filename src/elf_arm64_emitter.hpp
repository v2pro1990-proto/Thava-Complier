#pragma once

#include <vector>
#include <string>
#include <fstream>
#include <cstdint>
#include <cstring>
#include <map>
#include <iostream>

class ElfArm64Emitter {
public:
    // Linux AArch64 standard 64KB page alignment (ensures compatibility with both 4KB and 64KB kernels)
    static const uint32_t PAGE_ALIGN = 0x10000;
    static const uint64_t BASE_VADDR = 0x400000;

    const uint32_t textOffset = PAGE_ALIGN;        // 0x10000
    const uint32_t textRva    = PAGE_ALIGN;        // VAddr = 0x410000

    #pragma pack(push, 1)
    struct Elf64_Ehdr {
        unsigned char e_ident[16]; // 0x7F 'E' 'L' 'F', 64-bit, Little-Endian
        uint16_t      e_type;      // ET_EXEC = 2
        uint16_t      e_machine;   // EM_AARCH64 = 0xB7 (183)
        uint32_t      e_version;   // EV_CURRENT = 1
        uint64_t      e_entry;     // Entry point virtual address
        uint64_t      e_phoff;     // Program header table file offset = 64
        uint64_t      e_shoff;     // Section header table file offset = 0
        uint32_t      e_flags;     // Processor-specific flags = 0
        uint16_t      e_ehsize;    // ELF header size = 64
        uint16_t      e_phentsize; // Program header entry size = 56
        uint16_t      e_phnum;     // Number of program headers = 2
        uint16_t      e_shentsize; // Section header entry size = 64
        uint16_t      e_shnum;     // Number of section headers = 0
        uint16_t      e_shstrndx;  // Section header string table index = 0
    };

    struct Elf64_Phdr {
        uint32_t p_type;   // PT_LOAD = 1
        uint32_t p_flags;  // PF_X(1), PF_W(2), PF_R(4)
        uint64_t p_offset; // Segment file offset
        uint64_t p_vaddr;  // Segment virtual address
        uint64_t p_paddr;  // Segment physical address
        uint64_t p_filesz; // Segment size in file
        uint64_t p_memsz;  // Segment size in memory
        uint64_t p_align;  // Segment alignment = 0x10000
    };
    #pragma pack(pop)

private:
    std::vector<uint8_t> textBytes;
    std::vector<uint8_t> dataBytes;

    std::map<std::string, uint32_t> stringPool;
    std::map<double, uint32_t> doublePool;
    std::map<std::string, uint32_t> funcToSlotOffset;
    std::map<std::string, uint32_t> funcToTableRva;

    static size_t alignUp(size_t val, size_t align) {
        return (val + align - 1) & ~(align - 1);
    }

    template<typename T>
    static void appendVal(std::vector<uint8_t>& buf, T val) {
        size_t off = buf.size();
        buf.resize(off + sizeof(T));
        std::memcpy(&buf[off], &val, sizeof(T));
    }

    static void appendBytes(std::vector<uint8_t>& buf, const void* data, size_t size) {
        const uint8_t* p = static_cast<const uint8_t*>(data);
        buf.insert(buf.end(), p, p + size);
    }

public:
    ElfArm64Emitter() = default;

    uint32_t getDataOffset() const {
        size_t textVirtualSize = textBytes.size();
        return textOffset + static_cast<uint32_t>(alignUp(textVirtualSize == 0 ? 1 : textVirtualSize, PAGE_ALIGN));
    }

    uint32_t getDataRva() const {
        return getDataOffset();
    }

    uint32_t getRdataRva() const {
        return getDataRva();
    }

    // Add string literal to data pool, returns offset within data section
    uint32_t addStringLiteralOffset(const std::string& str) {
        auto it = stringPool.find(str);
        if (it != stringPool.end()) return it->second;

        uint32_t off = static_cast<uint32_t>(dataBytes.size());
        appendBytes(dataBytes, str.c_str(), str.size() + 1); // null-terminated
        stringPool[str] = off;
        return off;
    }

    // Add double literal to data pool, returns offset within data section
    uint32_t addDoubleLiteralOffset(double val) {
        auto it = doublePool.find(val);
        if (it != doublePool.end()) return it->second;

        // Align to 8 bytes for 64-bit float loads
        while (dataBytes.size() % 8 != 0) {
            dataBytes.push_back(0);
        }

        uint32_t off = static_cast<uint32_t>(dataBytes.size());
        appendVal(dataBytes, val);
        doublePool[val] = off;
        return off;
    }

    void setTextCode(const std::vector<uint8_t>& code) {
        textBytes = code;
    }

    // Register a runtime or library function pointer slot
    void registerFuncPointer(const std::string& funcName, uint32_t targetRva) {
        while (dataBytes.size() % 8 != 0) {
            dataBytes.push_back(0);
        }
        uint32_t slotOff = static_cast<uint32_t>(dataBytes.size());
        uint64_t fullVAddr = BASE_VADDR + targetRva;
        appendVal(dataBytes, fullVAddr);

        funcToSlotOffset[funcName] = slotOff;
        funcToTableRva[funcName]   = getDataRva() + slotOff;
    }

    uint32_t getFuncTableRva(const std::string& funcName) const {
        auto it = funcToTableRva.find(funcName);
        if (it != funcToTableRva.end()) return it->second;
        return 0;
    }

    // Build complete ELF64 binary and write to file
    bool buildAndWrite(const std::string& filePath) {
        std::vector<uint8_t> elfFile;

        // 1. ELF64 Header (64 bytes)
        Elf64_Ehdr ehdr;
        std::memset(&ehdr, 0, sizeof(ehdr));

        ehdr.e_ident[0] = 0x7F;
        ehdr.e_ident[1] = 'E';
        ehdr.e_ident[2] = 'L';
        ehdr.e_ident[3] = 'F';
        ehdr.e_ident[4] = 2; // ELFCLASS64
        ehdr.e_ident[5] = 1; // ELFDATA2LSB (Little-Endian)
        ehdr.e_ident[6] = 1; // EV_CURRENT
        ehdr.e_ident[7] = 0; // ELFOSABI_NONE / ELFOSABI_SYSV
        ehdr.e_ident[8] = 0; // ABI Version

        ehdr.e_type      = 2;      // ET_EXEC
        ehdr.e_machine   = 0xB7;   // EM_AARCH64 (183)
        ehdr.e_version   = 1;
        ehdr.e_entry     = BASE_VADDR + textOffset; // 0x410000
        ehdr.e_phoff     = sizeof(Elf64_Ehdr);      // 64 bytes
        ehdr.e_shoff     = 0;
        ehdr.e_flags     = 0;
        ehdr.e_ehsize    = sizeof(Elf64_Ehdr);      // 64
        ehdr.e_phentsize = sizeof(Elf64_Phdr);      // 56
        ehdr.e_phnum     = 2;                       // 2 segments: Text (RX), Data (RW)
        ehdr.e_shentsize = 0;
        ehdr.e_shnum     = 0;
        ehdr.e_shstrndx  = 0;

        appendBytes(elfFile, &ehdr, sizeof(ehdr));

        // 2. Program Headers
        uint32_t dataOff = getDataOffset();
        uint64_t textFileSize = dataOff;

        // Phdr 0: Code Segment (Headers + .text, Read + Execute)
        Elf64_Phdr phdrText;
        std::memset(&phdrText, 0, sizeof(phdrText));
        phdrText.p_type   = 1; // PT_LOAD
        phdrText.p_flags  = 5; // PF_R | PF_X
        phdrText.p_offset = 0;
        phdrText.p_vaddr  = BASE_VADDR;
        phdrText.p_paddr  = BASE_VADDR;
        phdrText.p_filesz = textFileSize;
        phdrText.p_memsz  = textFileSize;
        phdrText.p_align  = PAGE_ALIGN;
        appendBytes(elfFile, &phdrText, sizeof(phdrText));

        // Phdr 1: Data Segment (.data / .rodata + Heap space, Read + Write)
        Elf64_Phdr phdrData;
        std::memset(&phdrData, 0, sizeof(phdrData));
        phdrData.p_type   = 1; // PT_LOAD
        phdrData.p_flags  = 6; // PF_R | PF_W
        phdrData.p_offset = dataOff;
        phdrData.p_vaddr  = BASE_VADDR + dataOff;
        phdrData.p_paddr  = BASE_VADDR + dataOff;
        phdrData.p_filesz = dataBytes.size();
        phdrData.p_memsz  = dataBytes.size() + 0x40000; // Extra 256KB in memory for runtime heap
        phdrData.p_align  = PAGE_ALIGN;
        appendBytes(elfFile, &phdrData, sizeof(phdrData));

        // 3. Pad up to textOffset (0x10000)
        if (elfFile.size() < textOffset) {
            elfFile.resize(textOffset, 0);
        }

        // 4. Append Code Section (.text)
        elfFile.insert(elfFile.end(), textBytes.begin(), textBytes.end());

        // 5. Pad up to dataOff
        if (elfFile.size() < dataOff) {
            elfFile.resize(dataOff, 0);
        }

        // 6. Append Data Section (.data)
        elfFile.insert(elfFile.end(), dataBytes.begin(), dataBytes.end());

        // 7. Write to binary file
        std::ofstream out(filePath, std::ios::binary);
        if (!out.is_open()) {
            std::cerr << "[ElfArm64Emitter Error]: Cannot create output file: " << filePath << "\n";
            return false;
        }

        out.write(reinterpret_cast<const char*>(elfFile.data()), elfFile.size());
        out.close();

        return true;
    }
};

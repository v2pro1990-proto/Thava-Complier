#pragma once

#include <vector>
#include <string>
#include <fstream>
#include <cstdint>
#include <cstring>
#include <map>
#include <iostream>

class ElfEmitter {
public:
    static const uint32_t PAGE_ALIGN = 0x1000;    // 4096 bytes (standard Linux x86-64 page)
    static const uint64_t BASE_VADDR = 0x400000;  // Standard ELF load base

    const uint32_t textOffset = 0x1000;           // .text starts at file offset 0x1000
    const uint32_t textRva    = 0x1000;           // .text VAddr = BASE_VADDR + textRva = 0x401000

    #pragma pack(push, 1)
    struct Elf64_Ehdr {
        unsigned char e_ident[16]; // ELF Identification (Magic, 64-bit, little-endian...)
        uint16_t      e_type;      // Object file type: ET_EXEC = 2
        uint16_t      e_machine;   // Machine architecture: EM_X86_64 = 0x3E (62)
        uint32_t      e_version;   // ELF version: EV_CURRENT = 1
        uint64_t      e_entry;     // Entry point virtual address (0x401000)
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
        uint32_t p_type;   // Segment type: PT_LOAD = 1
        uint32_t p_flags;  // Segment flags: PF_X(1), PF_W(2), PF_R(4)
        uint64_t p_offset; // Segment file offset
        uint64_t p_vaddr;  // Segment virtual address
        uint64_t p_paddr;  // Segment physical address
        uint64_t p_filesz; // Segment size in file
        uint64_t p_memsz;  // Segment size in memory
        uint64_t p_align;  // Segment alignment = 0x1000
    };
    #pragma pack(pop)

private:
    std::vector<uint8_t> textBytes;
    std::vector<uint8_t> dataBytes;

    std::map<std::string, uint32_t> stringPool;
    std::map<std::string, uint32_t> funcToSlotOffset; // funcName -> offset in dataBytes
    std::map<std::string, uint32_t> funcToTableRva;   // funcName -> final RVA of pointer in .data

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
    ElfEmitter() = default;

    // Data section VAddr calculation (starts at next 4KB page after .text)
    uint32_t getDataOffset() const {
        size_t textVirtualSize = textBytes.size();
        return textOffset + (uint32_t)alignUp(textVirtualSize == 0 ? 1 : textVirtualSize, PAGE_ALIGN);
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

        uint32_t off = (uint32_t)dataBytes.size();
        appendBytes(dataBytes, str.c_str(), str.size() + 1); // null-terminated
        stringPool[str] = off;
        return off;
    }

    // Add 8-byte double literal to data pool, returns offset within data section
    uint32_t addDoubleLiteralOffset(double val) {
        while (dataBytes.size() % 8 != 0) dataBytes.push_back(0); // 8-byte align
        uint32_t off = (uint32_t)dataBytes.size();
        appendVal<double>(dataBytes, val);
        return off;
    }

    // Allocate an 8-byte function pointer slot in .data
    void registerFuncPointer(const std::string& funcName, uint32_t targetFuncRva) {
        while (dataBytes.size() % 8 != 0) dataBytes.push_back(0);
        uint32_t slotOffset = (uint32_t)dataBytes.size();
        uint64_t targetVAddr = BASE_VADDR + targetFuncRva;
        appendVal<uint64_t>(dataBytes, targetVAddr);

        funcToSlotOffset[funcName] = slotOffset;
    }

    void setTextBytes(const std::vector<uint8_t>& bytes) {
        textBytes = bytes;
    }

    std::vector<uint8_t>& getTextBytesRef() {
        return textBytes;
    }

    uint32_t getImportIatRva(const std::string& funcName) {
        auto it = funcToTableRva.find(funcName);
        if (it != funcToTableRva.end()) return it->second;
        return 0;
    }

    const std::map<std::string, uint32_t>& getFuncToIatMap() const {
        return funcToTableRva;
    }

    void layoutData() {
        while (dataBytes.size() % 8 != 0) dataBytes.push_back(0);
        uint32_t dataRva = getDataRva();
        funcToTableRva.clear();
        for (const auto& pair : funcToSlotOffset) {
            funcToTableRva[pair.first] = dataRva + pair.second;
        }
    }

    void layoutRdata() {
        layoutData();
    }

    // Build complete ELF64 binary and write to file
    bool buildAndWrite(const std::string& filePath) {
        std::vector<uint8_t> elfFile;

        // 1. ELF64 Header (64 bytes)
        Elf64_Ehdr ehdr;
        std::memset(&ehdr, 0, sizeof(ehdr));

        // Magic: 0x7F 'E' 'L' 'F'
        ehdr.e_ident[0] = 0x7F;
        ehdr.e_ident[1] = 'E';
        ehdr.e_ident[2] = 'L';
        ehdr.e_ident[3] = 'F';
        ehdr.e_ident[4] = 2; // ELFCLASS64 (64-bit architecture)
        ehdr.e_ident[5] = 1; // ELFDATA2LSB (Little-Endian)
        ehdr.e_ident[6] = 1; // EV_CURRENT (ELF Version 1)
        ehdr.e_ident[7] = 0; // ELFOSABI_NONE / ELFOSABI_SYSV
        ehdr.e_ident[8] = 0; // ABI Version

        ehdr.e_type      = 2;      // ET_EXEC (Executable file)
        ehdr.e_machine   = 0x3E;   // EM_X86_64 (AMD x86-64 architecture)
        ehdr.e_version   = 1;      // EV_CURRENT
        ehdr.e_entry     = BASE_VADDR + textOffset; // Entry point: 0x401000
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

        // 2. Program Headers (2 * 56 = 112 bytes)
        uint32_t dataOff = getDataOffset();
        uint64_t textFileSize = dataOff; // Map headers + code into segment 0

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
        phdrData.p_memsz  = dataBytes.size() + 0x20000; // Extra 128KB in memory for dynamic runtime heap
        phdrData.p_align  = PAGE_ALIGN;
        appendBytes(elfFile, &phdrData, sizeof(phdrData));

        // 3. Pad up to textOffset (0x1000)
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

        // 7. Write to file
        std::ofstream out(filePath, std::ios::binary);
        if (!out.is_open()) {
            std::cerr << "[ElfEmitter Error]: Cannot create output file: " << filePath << "\n";
            return false;
        }

        out.write(reinterpret_cast<const char*>(elfFile.data()), elfFile.size());
        out.close();

        return true;
    }
};

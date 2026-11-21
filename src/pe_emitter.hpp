#pragma once

#include <vector>
#include <string>
#include <fstream>
#include <cstdint>
#include <cstring>
#include <map>
#include <iostream>
#include <algorithm>
#include <filesystem>

class PeEmitter {
public:
    static const uint32_t FILE_ALIGN = 0x200;    // 512 bytes
    static const uint32_t SEC_ALIGN  = 0x1000;   // 4096 bytes
    static const uint64_t IMAGE_BASE = 0x140000000ULL;

    const uint32_t textRva  = 0x1000;
    // rdataRva will be computed based on text section size, or fixed if text is <= 4KB

    bool isDll = false;
    std::string dllExportName;
    uint32_t entryPointRva = 0x1000;

    struct ExportEntry {
        std::string name;
        uint32_t textOffset;
    };
    std::vector<ExportEntry> exportList;

    const bool isI686;
    bool isArm64 = false;
    void setArm64(bool a) { isArm64 = a; }

    void setDll(bool dll) { isDll = dll; }
    void setDllExportName(const std::string& name) { dllExportName = name; }
    void setEntryPointRva(uint32_t rva) { entryPointRva = rva; }
    void addExport(const std::string& name, uint32_t textOffset) {
        exportList.push_back({ name, textOffset });
    }

    struct ImportedFunc {
        std::string dllName;
        std::string funcName;
        uint32_t iatRva = 0;
    };

private:
    std::vector<uint8_t> textBytes;
    std::vector<uint8_t> rdataBytes;

    // String literals and constants pool in .rdata
    // Map string content -> offset in rdataBytes
    std::map<std::string, uint32_t> stringPool;

    // List of DLLs and their functions
    std::vector<std::string> dlls = { "msvcrt.dll", "kernel32.dll", "user32.dll" };
    std::map<std::string, std::vector<std::string>> importFuncs;
    std::map<std::pair<std::string, std::string>, uint32_t> qualifiedIatRvas;
    std::map<std::string, uint32_t> funcToIatRva; // funcName -> IAT RVA

    static size_t alignUp(size_t val, size_t align) {
        return (val + align - 1) & ~(align - 1);
    }

    template<typename T>
    static void appendVal(std::vector<uint8_t>& buf, T val) {
        size_t off = buf.size();
        buf.resize(off + sizeof(T));
        std::memcpy(&buf[off], &val, sizeof(T));
    }

    template<typename T>
    static void writeVal(std::vector<uint8_t>& buf, size_t offset, T val) {
        if (offset + sizeof(T) > buf.size()) buf.resize(offset + sizeof(T), 0);
        std::memcpy(&buf[offset], &val, sizeof(T));
    }

    static void appendBytes(std::vector<uint8_t>& buf, const void* data, size_t size) {
        const uint8_t* p = static_cast<const uint8_t*>(data);
        buf.insert(buf.end(), p, p + size);
    }

    static void appendZeroes(std::vector<uint8_t>& buf, size_t count) {
        buf.insert(buf.end(), count, 0);
    }

public:
    explicit PeEmitter(bool x86 = false) : isI686(x86) {
        if (isI686) {
            for (const char* name : {"ExitProcess", "GetStdHandle", "SetConsoleOutputCP", "WriteFile"})
                addImport("kernel32.dll", name);
            return;
        }
        // Register default standard C runtime functions from msvcrt.dll
        addImport("msvcrt.dll", "printf");
        addImport("msvcrt.dll", "puts");
        addImport("msvcrt.dll", "scanf");
        addImport("msvcrt.dll", "getchar");
        addImport("msvcrt.dll", "fflush");
        addImport("msvcrt.dll", "malloc");
        addImport("msvcrt.dll", "free");
        addImport("msvcrt.dll", "realloc");
        addImport("msvcrt.dll", "exit");
        addImport("msvcrt.dll", "strlen");
        addImport("msvcrt.dll", "strcmp");
        addImport("msvcrt.dll", "strcpy");
        addImport("msvcrt.dll", "strcat");
        addImport("msvcrt.dll", "sprintf");
        addImport("msvcrt.dll", "rand");
        addImport("msvcrt.dll", "srand");
        addImport("msvcrt.dll", "time");
        addImport("msvcrt.dll", "atoi");
        addImport("msvcrt.dll", "_atoi64");
        addImport("msvcrt.dll", "atof");
        addImport("msvcrt.dll", "memcpy");
        addImport("msvcrt.dll", "memset");
        addImport("msvcrt.dll", "system");
        addImport("msvcrt.dll", "fopen");
        addImport("msvcrt.dll", "fclose");
        addImport("msvcrt.dll", "fread");
        addImport("msvcrt.dll", "fwrite");
        addImport("msvcrt.dll", "fseek");
        addImport("msvcrt.dll", "ftell");
        addImport("msvcrt.dll", "fputs");
        addImport("msvcrt.dll", "remove");
        addImport("msvcrt.dll", "rename");
        addImport("msvcrt.dll", "getenv");
        addImport("msvcrt.dll", "_access");
        addImport("msvcrt.dll", "_mkdir");
        addImport("msvcrt.dll", "_rmdir");
        addImport("msvcrt.dll", "_chdir");
        addImport("msvcrt.dll", "_getcwd");
        addImport("msvcrt.dll", "pow");
        addImport("msvcrt.dll", "sqrt");
        addImport("msvcrt.dll", "fabs");
        addImport("msvcrt.dll", "fmod");
        addImport("msvcrt.dll", "strstr");
        addImport("msvcrt.dll", "strncmp");

        // Register default Win32 console functions from kernel32.dll
        addImport("kernel32.dll", "ExitProcess");
        addImport("kernel32.dll", "SetConsoleCP");
        addImport("kernel32.dll", "GetStdHandle");
        addImport("kernel32.dll", "GetConsoleMode");
        addImport("kernel32.dll", "ReadConsoleW");
        addImport("kernel32.dll", "WideCharToMultiByte");
        addImport("kernel32.dll", "SetConsoleOutputCP");
        addImport("kernel32.dll", "Sleep");
        addImport("kernel32.dll", "GetCurrentProcessId");
        addImport("kernel32.dll", "GetLocalTime");
        addImport("kernel32.dll", "LoadLibraryA");
        addImport("kernel32.dll", "GetProcAddress");
        addImport("kernel32.dll", "FreeLibrary");

        // Register GUI functions from user32.dll
        addImport("user32.dll", "MessageBoxA");
    }

    void addImport(const std::string& dll, const std::string& func) {
        bool found = false;
        for (const auto& d : dlls) {
            if (d == dll) { found = true; break; }
        }
        if (!found) dlls.push_back(dll);

        for (const auto& f : importFuncs[dll]) {
            if (f == func) return;
        }
        importFuncs[dll].push_back(func);
    }

    uint32_t getRdataRva() const {
        uint32_t textVirtualSize = (uint32_t)textBytes.size();
        return textRva + (uint32_t)alignUp(textVirtualSize == 0 ? 1 : textVirtualSize, SEC_ALIGN);
    }

    // Add string literal to .rdata, returns offset within .rdata
    uint32_t addStringLiteralOffset(const std::string& str) {
        auto it = stringPool.find(str);
        if (it != stringPool.end()) return it->second;

        uint32_t off = (uint32_t)rdataBytes.size();
        appendBytes(rdataBytes, str.c_str(), str.size() + 1); // null-terminated
        stringPool[str] = off;
        return off;
    }

    // Reserve a writable 8-byte slot in .rdata (used for the global-variable
    // base pointer), returns its offset within .rdata.
    uint32_t addDataSlot(size_t bytes = 8) {
        while (rdataBytes.size() % 8 != 0) rdataBytes.push_back(0); // 8-byte align
        uint32_t off = (uint32_t)rdataBytes.size();
        for (size_t i = 0; i < bytes; ++i) rdataBytes.push_back(0);
        return off;
    }

    // Overwrite an already reserved data slot (late metadata pointer).
    void setDataSlotValue(uint32_t offset, uint64_t value) {
        if (offset + sizeof(uint64_t) > rdataBytes.size()) return;
        std::memcpy(&rdataBytes[offset], &value, sizeof(uint64_t));
    }

    // Add 8-byte double literal to .rdata, returns offset within .rdata
    uint32_t addDoubleLiteralOffset(double val) {
        while (rdataBytes.size() % 8 != 0) rdataBytes.push_back(0); // 8-byte align
        uint32_t off = (uint32_t)rdataBytes.size();
        appendVal<double>(rdataBytes, val);
        return off;
    }

    void setTextBytes(const std::vector<uint8_t>& bytes) {
        const uint32_t oldRdataRva = getRdataRva();
        textBytes = bytes;
        const uint32_t delta = getRdataRva() - oldRdataRva;
        if (idtSizeCached == 0 || delta == 0) return;

        // ARM64 helpers need IAT offsets while the text size is still provisional.
        // Shift the stored RVAs when the final text changes the data section's base.
        for (uint32_t offset : rdataRvaFields) {
            uint32_t rva;
            std::memcpy(&rva, rdataBytes.data() + offset, sizeof(rva));
            writeVal<uint32_t>(rdataBytes, offset, rva + delta);
        }
        for (auto& entry : funcToIatRva) entry.second += delta;
        for (auto& entry : qualifiedIatRvas) entry.second += delta;
        if (exportSizeCached) exportRvaCached += delta;
        idtRvaCached += delta;
        iatRvaCached += delta;
    }

    void setTextCode(const std::vector<uint8_t>& code) {
        setTextBytes(code);
    }

    std::vector<uint8_t>& getTextBytesRef() {
        return textBytes;
    }

    uint32_t getImportIatRva(const std::string& funcName) {
        auto it = funcToIatRva.find(funcName);
        if (it != funcToIatRva.end()) return it->second;
        return 0;
    }

    uint32_t getFuncIatOffsetInRdata(const std::string& funcName) {
        uint32_t rva = getImportIatRva(funcName);
        uint32_t rdataRva = getRdataRva();
        if (rva >= rdataRva) return rva - rdataRva;
        return 0;
    }

    uint32_t getFuncIatOffsetInRdata(const std::string& dll, const std::string& func) const {
        return qualifiedIatRvas.at({dll, func}) - getRdataRva();
    }

    const std::map<std::string, uint32_t>& getFuncToIatMap() const {
        return funcToIatRva;
    }

    void appendRdataRva(uint32_t rva) {
        rdataRvaFields.push_back(static_cast<uint32_t>(rdataBytes.size()));
        appendVal<uint32_t>(rdataBytes, rva);
    }

    void appendImportThunk(uint32_t rva) {
        if (rva) appendRdataRva(rva);
        else appendVal<uint32_t>(rdataBytes, 0);
        if (!isI686) appendVal<uint32_t>(rdataBytes, 0);
    }

    // Pre-calculate RVAs of strings and imports so code generator knows targets
    void layoutRdata() {
        uint32_t rdataRva = getRdataRva();

        // 1. Strings and double constants are already in rdataBytes
        // 2. Align to 8 bytes for pointer arrays
        while (rdataBytes.size() % 8 != 0) rdataBytes.push_back(0);

        // Compute Hint/Name offsets for each imported function
        struct FuncEntry {
            std::string dll;
            std::string name;
            uint32_t hintNameRva = 0;
            uint32_t iatRva = 0;
        };

        std::map<std::string, std::vector<FuncEntry>> dllEntries;

        for (const auto& dll : dlls) {
            auto it = importFuncs.find(dll);
            if (it == importFuncs.end()) continue;

            for (const auto& fn : it->second) {
                // 2-byte align for Hint/Name table
                while (rdataBytes.size() % 2 != 0) rdataBytes.push_back(0);
                uint32_t hnOffset = (uint32_t)rdataBytes.size();
                appendVal<uint16_t>(rdataBytes, 0); // Hint = 0
                appendBytes(rdataBytes, fn.c_str(), fn.size() + 1);

                FuncEntry fe;
                fe.dll = dll;
                fe.name = fn;
                fe.hintNameRva = rdataRva + hnOffset;
                dllEntries[dll].push_back(fe);
            }
        }

        // Align to 8 bytes for 64-bit ILT and IAT entries
        while (rdataBytes.size() % 8 != 0) rdataBytes.push_back(0);

        // Build ILT (Import Lookup Tables) and IAT (Import Address Tables)
        std::map<std::string, uint32_t> iltRvas;
        std::map<std::string, uint32_t> iatRvas;

        for (const auto& pair : dllEntries) {
            iltRvas[pair.first] = rdataRva + static_cast<uint32_t>(rdataBytes.size());
            for (const auto& entry : pair.second) appendImportThunk(entry.hintNameRva);
            appendImportThunk(0);
        }

        const uint32_t iatOffset = static_cast<uint32_t>(rdataBytes.size());
        iatRvaCached = rdataRva + iatOffset;
        for (const auto& pair : dllEntries) {
            iatRvas[pair.first] = rdataRva + static_cast<uint32_t>(rdataBytes.size());
            for (const auto& entry : pair.second) {
                const auto slot = rdataRva + static_cast<uint32_t>(rdataBytes.size());
                if (!isArm64 || pair.first == "msvcrt.dll" ||
                    pair.first == "kernel32.dll" || pair.first == "user32.dll")
                    funcToIatRva[entry.name] = slot;
                else
                    funcToIatRva.emplace(entry.name, slot);
                qualifiedIatRvas[{pair.first, entry.name}] = slot;
                appendImportThunk(entry.hintNameRva);
            }
            appendImportThunk(0);
        }
        iatSizeCached = static_cast<uint32_t>(rdataBytes.size()) - iatOffset;

        // DLL name strings
        std::map<std::string, uint32_t> dllNameRvas;
        for (const auto& pair : dllEntries) {
            const std::string& dll = pair.first;
            uint32_t off = (uint32_t)rdataBytes.size();
            dllNameRvas[dll] = rdataRva + off;
            appendBytes(rdataBytes, dll.c_str(), dll.size() + 1);
        }

        // Import Directory Table (IDT)
        // 4-byte align for IDT structs
        while (rdataBytes.size() % 4 != 0) rdataBytes.push_back(0);
        uint32_t idtOffset = (uint32_t)rdataBytes.size();
        idtRvaCached = rdataRva + idtOffset;

        for (const auto& pair : dllEntries) {
            const std::string& dll = pair.first;
            appendRdataRva(iltRvas[dll]);       // OriginalFirstThunk (ILT)
            appendVal<uint32_t>(rdataBytes, 0);                  // TimeDateStamp
            appendVal<uint32_t>(rdataBytes, 0);                  // ForwarderChain
            appendRdataRva(dllNameRvas[dll]);   // Name RVA
            appendRdataRva(iatRvas[dll]);       // FirstThunk (IAT)
        }
        // Terminating 20-byte null descriptor
        appendZeroes(rdataBytes, 20);
        idtSizeCached = (uint32_t)(rdataBytes.size() - idtOffset);

        // Export Directory Table (EDT) if building a dynamic library (DLL / THDL)
        if (isDll && !exportList.empty()) {
            std::sort(exportList.begin(), exportList.end(), [](const ExportEntry& a, const ExportEntry& b) {
                return a.name < b.name;
            });

            while (rdataBytes.size() % 4 != 0) rdataBytes.push_back(0);
            uint32_t exportOffset = (uint32_t)rdataBytes.size();
            exportRvaCached = rdataRva + exportOffset;

            // Reserve 40 bytes for IMAGE_EXPORT_DIRECTORY
            appendZeroes(rdataBytes, 40);

            // DLL Name string
            std::string modName = dllExportName.empty() ? "library.thdl" : std::filesystem::path(dllExportName).filename().string();
            uint32_t dllNameRva = rdataRva + (uint32_t)rdataBytes.size();
            appendBytes(rdataBytes, modName.c_str(), modName.size() + 1);

            // Function name strings
            std::vector<uint32_t> funcNameRvas;
            for (const auto& exp : exportList) {
                funcNameRvas.push_back(rdataRva + (uint32_t)rdataBytes.size());
                appendBytes(rdataBytes, exp.name.c_str(), exp.name.size() + 1);
            }

            while (rdataBytes.size() % 4 != 0) rdataBytes.push_back(0);

            // 1. Export Address Table (EAT)
            uint32_t eatRva = rdataRva + (uint32_t)rdataBytes.size();
            for (const auto& exp : exportList) {
                appendVal<uint32_t>(rdataBytes, textRva + exp.textOffset);
            }

            // 2. Export Name Pointer Table (ENPT)
            uint32_t enptRva = rdataRva + (uint32_t)rdataBytes.size();
            for (size_t i = 0; i < exportList.size(); ++i) {
                appendRdataRva(funcNameRvas[i]);
            }

            // 3. Export Ordinal Table (EOT)
            uint32_t eotRva = rdataRva + (uint32_t)rdataBytes.size();
            for (size_t i = 0; i < exportList.size(); ++i) {
                appendVal<uint16_t>(rdataBytes, (uint16_t)i);
            }

            // Write 40-byte IMAGE_EXPORT_DIRECTORY
            writeVal<uint32_t>(rdataBytes, exportOffset + 0, 0);               // Characteristics
            writeVal<uint32_t>(rdataBytes, exportOffset + 4, 0);               // TimeDateStamp
            writeVal<uint16_t>(rdataBytes, exportOffset + 8, 0);               // MajorVersion
            writeVal<uint16_t>(rdataBytes, exportOffset + 10, 0);              // MinorVersion
            writeVal<uint32_t>(rdataBytes, exportOffset + 12, dllNameRva);     // Name RVA
            writeVal<uint32_t>(rdataBytes, exportOffset + 16, 1);              // Base = 1
            writeVal<uint32_t>(rdataBytes, exportOffset + 20, (uint32_t)exportList.size()); // NumberOfFunctions
            writeVal<uint32_t>(rdataBytes, exportOffset + 24, (uint32_t)exportList.size()); // NumberOfNames
            writeVal<uint32_t>(rdataBytes, exportOffset + 28, eatRva);         // AddressOfFunctions
            writeVal<uint32_t>(rdataBytes, exportOffset + 32, enptRva);        // AddressOfNames
            writeVal<uint32_t>(rdataBytes, exportOffset + 36, eotRva);         // AddressOfNameOrdinals

            for (uint32_t field : {12u, 28u, 32u, 36u})
                rdataRvaFields.push_back(exportOffset + field);
            exportSizeCached = (uint32_t)(rdataBytes.size() - exportOffset);
        }
    }

private:
    uint32_t exportRvaCached = 0;
    uint32_t exportSizeCached = 0;
    uint32_t idtRvaCached = 0;
    uint32_t idtSizeCached = 0;
    uint32_t iatRvaCached = 0;
    uint32_t iatSizeCached = 0;
    std::vector<uint32_t> rdataRvaFields;

public:
    bool buildAndWrite(const std::string& outputPath) {
        uint32_t rdataRva = getRdataRva();

        uint32_t textVirtualSize  = (uint32_t)textBytes.size();
        uint32_t textRawSize      = (uint32_t)alignUp(textVirtualSize == 0 ? 1 : textVirtualSize, FILE_ALIGN);
        uint32_t rdataVirtualSize = (uint32_t)rdataBytes.size();
        uint32_t rdataRawSize     = (uint32_t)alignUp(rdataVirtualSize == 0 ? 1 : rdataVirtualSize, FILE_ALIGN);

        // ARM64 images use ASLR. The emitted code is position-independent, so an
        // ABSOLUTE-only relocation block suffices when no addresses need fixing.
        const bool hasRelocations = isDll || isArm64 || isI686;
        std::vector<uint8_t> relocBytes;
        uint32_t relocRva = 0;
        uint32_t relocVirtualSize = 0;
        uint32_t relocRawSize = 0;
        uint32_t relocRawOffset = 0;

        if (hasRelocations) {
            appendVal<uint32_t>(relocBytes, textRva); // VirtualAddress = 0x1000
            appendVal<uint32_t>(relocBytes, 12);      // SizeOfBlock = 12
            appendVal<uint16_t>(relocBytes, 0);       // Entry 0: IMAGE_REL_BASED_ABSOLUTE
            appendVal<uint16_t>(relocBytes, 0);       // Padding
            relocVirtualSize = (uint32_t)relocBytes.size();
            relocRawSize = (uint32_t)alignUp(relocVirtualSize, FILE_ALIGN);
            relocRva = rdataRva + (uint32_t)alignUp(rdataVirtualSize == 0 ? 1 : rdataVirtualSize, SEC_ALIGN);
        }

        uint32_t headersSize   = FILE_ALIGN; // 0x200 bytes fits DOS + PE + 3 Sections
        uint32_t textRawOffset = headersSize;
        uint32_t rdataRawOffset = textRawOffset + textRawSize;
        if (hasRelocations) {
            relocRawOffset = rdataRawOffset + rdataRawSize;
        }

        uint32_t sizeOfImage = SEC_ALIGN + alignUp(textVirtualSize == 0 ? 1 : textVirtualSize, SEC_ALIGN)
                                         + alignUp(rdataVirtualSize == 0 ? 1 : rdataVirtualSize, SEC_ALIGN);
        if (hasRelocations) {
            sizeOfImage += alignUp(relocVirtualSize == 0 ? 1 : relocVirtualSize, SEC_ALIGN);
        }

        std::vector<uint8_t> peFile;
        peFile.resize(headersSize, 0);

        // 1. DOS Header
        writeVal<uint16_t>(peFile, 0x00, 0x5A4D); // "MZ"
        writeVal<uint32_t>(peFile, 0x3C, 0x80);   // e_lfanew = 0x80

        // Minimal DOS stub
        static const uint8_t dosStub[] = {
            0x0E, 0x1F, 0xBA, 0x0E, 0x00, 0xB4, 0x09, 0xCD, 0x21, 0xB8, 0x01, 0x4C, 0xCD, 0x21,
            'T', 'h', 'i', 's', ' ', 'p', 'r', 'o', 'g', 'r', 'a', 'm', ' ', 'c', 'a', 'n', 'n', 'o', 't', ' ',
            'b', 'e', ' ', 'r', 'u', 'n', ' ', 'i', 'n', ' ', 'D', 'O', 'S', ' ', 'm', 'o', 'd', 'e', '.', '\r', '\r', '\n', '$', 0, 0, 0
        };
        std::memcpy(&peFile[0x40], dosStub, sizeof(dosStub));

        // 2. PE Signature
        size_t peOffset = 0x80;
        writeVal<uint32_t>(peFile, peOffset, 0x00004550); // "PE\0\0"

        // 3. COFF File Header
        size_t coffOffset = peOffset + 4;
        writeVal<uint16_t>(peFile, coffOffset + 0,  isI686 ? 0x014C : (isArm64 ? 0xAA64 : 0x8664));  // ARM64 (0xAA64) vs AMD64 (0x8664)
        writeVal<uint16_t>(peFile, coffOffset + 2,  hasRelocations ? 3 : 2); // .text, .rdata, optional .reloc
        writeVal<uint32_t>(peFile, coffOffset + 4,  0);       // TimeDateStamp
        writeVal<uint32_t>(peFile, coffOffset + 8,  0);       // PointerToSymbolTable
        writeVal<uint32_t>(peFile, coffOffset + 12, 0);       // NumberOfSymbols
        const uint16_t optHeaderSize = isI686 ? 224 : 240;
        writeVal<uint16_t>(peFile, coffOffset + 16, optHeaderSize);
        writeVal<uint16_t>(peFile, coffOffset + 18, (isDll ? 0x2022 : 0x0022) | (isI686 ? 0x0100 : 0)); // EXECUTABLE_IMAGE | LARGE_ADDRESS_AWARE (| IMAGE_FILE_DLL)

        // 4. Optional Header (PE32+ 64-bit)
        size_t optOffset = coffOffset + 20;
        writeVal<uint16_t>(peFile, optOffset + 0,  isI686 ? 0x010B : 0x020B);
        writeVal<uint8_t> (peFile, optOffset + 2,  14);       // MajorLinkerVersion
        writeVal<uint8_t> (peFile, optOffset + 3,  0);        // MinorLinkerVersion
        writeVal<uint32_t>(peFile, optOffset + 4,  textRawSize); // SizeOfCode
        writeVal<uint32_t>(peFile, optOffset + 8,  rdataRawSize);// SizeOfInitializedData
        writeVal<uint32_t>(peFile, optOffset + 12, 0);        // SizeOfUninitializedData
        writeVal<uint32_t>(peFile, optOffset + 16, entryPointRva);  // AddressOfEntryPoint
        writeVal<uint32_t>(peFile, optOffset + 20, textRva);  // BaseOfCode
        if (isI686) {
            writeVal<uint32_t>(peFile, optOffset + 24, rdataRva);   // BaseOfData
            writeVal<uint32_t>(peFile, optOffset + 28, 0x400000);  // ImageBase
        } else {
            writeVal<uint64_t>(peFile, optOffset + 24, IMAGE_BASE);
        }
        writeVal<uint32_t>(peFile, optOffset + 32, SEC_ALIGN);// SectionAlignment = 0x1000
        writeVal<uint32_t>(peFile, optOffset + 36, FILE_ALIGN);// FileAlignment = 0x200
        writeVal<uint16_t>(peFile, optOffset + 40, 6);        // MajorOperatingSystemVersion
        writeVal<uint16_t>(peFile, optOffset + 42, isArm64 ? 2 : 0); // MinorOperatingSystemVersion
        writeVal<uint16_t>(peFile, optOffset + 44, 0);        // MajorImageVersion
        writeVal<uint16_t>(peFile, optOffset + 46, 0);        // MinorImageVersion
        writeVal<uint16_t>(peFile, optOffset + 48, 6);        // MajorSubsystemVersion
        writeVal<uint16_t>(peFile, optOffset + 50, isArm64 ? 2 : 0); // MinorSubsystemVersion
        writeVal<uint32_t>(peFile, optOffset + 52, 0);        // Win32VersionValue
        writeVal<uint32_t>(peFile, optOffset + 56, sizeOfImage); // SizeOfImage
        writeVal<uint32_t>(peFile, optOffset + 60, headersSize); // SizeOfHeaders
        writeVal<uint32_t>(peFile, optOffset + 64, 0);        // CheckSum
        writeVal<uint16_t>(peFile, optOffset + 68, 3);        // Subsystem = IMAGE_SUBSYSTEM_WINDOWS_CUI
        writeVal<uint16_t>(peFile, optOffset + 70, hasRelocations ? 0x8140 : 0x8100); // DYNAMIC_BASE | NX_COMPAT | TERMINAL_SERVER_AWARE
        if (isI686) {
            writeVal<uint32_t>(peFile, optOffset + 72, 0x100000); // StackReserve
            writeVal<uint32_t>(peFile, optOffset + 76, 0x1000);   // StackCommit
            writeVal<uint32_t>(peFile, optOffset + 80, 0x100000); // HeapReserve
            writeVal<uint32_t>(peFile, optOffset + 84, 0x1000);   // HeapCommit
        } else {
            writeVal<uint64_t>(peFile, optOffset + 72, 0x100000);
            writeVal<uint64_t>(peFile, optOffset + 80, 0x1000);
            writeVal<uint64_t>(peFile, optOffset + 88, 0x100000);
            writeVal<uint64_t>(peFile, optOffset + 96, 0x1000);
        }

        const size_t directories = optOffset + (isI686 ? 96 : 112);
        writeVal<uint32_t>(peFile, directories - 4, 16);
        auto writeDirectory = [&](size_t index, uint32_t rva, uint32_t size) {
            writeVal<uint32_t>(peFile, directories + index * 8, rva);
            writeVal<uint32_t>(peFile, directories + index * 8 + 4, size);
        };
        writeDirectory(0, exportRvaCached, exportSizeCached);
        writeDirectory(1, idtRvaCached, idtSizeCached);
        if (hasRelocations) writeDirectory(5, relocRva, relocVirtualSize);
        writeDirectory(12, iatRvaCached, iatSizeCached);

        // 5. Section Headers (at optOffset + 240)
        size_t secHdrOffset = optOffset + optHeaderSize;

        // Section 1: .text
        {
            uint8_t name[8] = { '.', 't', 'e', 'x', 't', 0, 0, 0 };
            std::memcpy(&peFile[secHdrOffset + 0], name, 8);
            writeVal<uint32_t>(peFile, secHdrOffset + 8,  textVirtualSize);
            writeVal<uint32_t>(peFile, secHdrOffset + 12, textRva);
            writeVal<uint32_t>(peFile, secHdrOffset + 16, textRawSize);
            writeVal<uint32_t>(peFile, secHdrOffset + 20, textRawOffset);
            writeVal<uint32_t>(peFile, secHdrOffset + 24, 0);
            writeVal<uint32_t>(peFile, secHdrOffset + 28, 0);
            writeVal<uint16_t>(peFile, secHdrOffset + 32, 0);
            writeVal<uint16_t>(peFile, secHdrOffset + 34, 0);
            writeVal<uint32_t>(peFile, secHdrOffset + 36, 0x60000020); // CODE | EXECUTE | READ
        }

        // Section 2: .rdata
        secHdrOffset += 40;
        {
            uint8_t name[8] = { '.', 'r', 'd', 'a', 't', 'a', 0, 0 };
            std::memcpy(&peFile[secHdrOffset + 0], name, 8);
            writeVal<uint32_t>(peFile, secHdrOffset + 8,  rdataVirtualSize);
            writeVal<uint32_t>(peFile, secHdrOffset + 12, rdataRva);
            writeVal<uint32_t>(peFile, secHdrOffset + 16, rdataRawSize);
            writeVal<uint32_t>(peFile, secHdrOffset + 20, rdataRawOffset);
            writeVal<uint32_t>(peFile, secHdrOffset + 24, 0);
            writeVal<uint32_t>(peFile, secHdrOffset + 28, 0);
            writeVal<uint16_t>(peFile, secHdrOffset + 32, 0);
            writeVal<uint16_t>(peFile, secHdrOffset + 34, 0);
            writeVal<uint32_t>(peFile, secHdrOffset + 36, 0xC0000040); // INITIALIZED_DATA | READ | WRITE
        }

        // Section 3: .reloc
        if (hasRelocations) {
            secHdrOffset += 40;
            uint8_t name[8] = { '.', 'r', 'e', 'l', 'o', 'c', 0, 0 };
            std::memcpy(&peFile[secHdrOffset + 0], name, 8);
            writeVal<uint32_t>(peFile, secHdrOffset + 8,  relocVirtualSize);
            writeVal<uint32_t>(peFile, secHdrOffset + 12, relocRva);
            writeVal<uint32_t>(peFile, secHdrOffset + 16, relocRawSize);
            writeVal<uint32_t>(peFile, secHdrOffset + 20, relocRawOffset);
            writeVal<uint32_t>(peFile, secHdrOffset + 24, 0);
            writeVal<uint32_t>(peFile, secHdrOffset + 28, 0);
            writeVal<uint16_t>(peFile, secHdrOffset + 32, 0);
            writeVal<uint16_t>(peFile, secHdrOffset + 34, 0);
            writeVal<uint32_t>(peFile, secHdrOffset + 36, 0x42000040); // INITIALIZED_DATA | DISCARDABLE | READ
        }

        // Write .text section payload
        peFile.resize(textRawOffset);
        appendBytes(peFile, textBytes.data(), textBytes.size());
        while (peFile.size() < textRawOffset + textRawSize) peFile.push_back(0);

        // Write .rdata section payload
        peFile.resize(rdataRawOffset);
        appendBytes(peFile, rdataBytes.data(), rdataBytes.size());
        while (peFile.size() < rdataRawOffset + rdataRawSize) peFile.push_back(0);

        // Write .reloc section payload
        if (hasRelocations) {
            peFile.resize(relocRawOffset);
            appendBytes(peFile, relocBytes.data(), relocBytes.size());
            while (peFile.size() < relocRawOffset + relocRawSize) peFile.push_back(0);
        }

        // Write to output binary
        std::ofstream out(outputPath, std::ios::binary);
        if (!out.is_open()) return false;
        out.write(reinterpret_cast<const char*>(peFile.data()), peFile.size());
        out.close();
        return bool(out);
    }
};

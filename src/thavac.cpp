#include <iostream>
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <map>
#include <set>
#include <sstream>
#include <fstream>
#include <cstdlib>
#include <cctype>
#include <filesystem>
#include <unordered_set>
#include <chrono>

// ==========================================
// 1. LEXER (Lexical Analysis)
// ==========================================
enum class TokenType {
    TYPE_INT, TYPE_INT64, TYPE_INT16, TYPE_FL, TYPE_DBL, TYPE_BL, TYPE_STR, TYPE_LST, TYPE_TPLE, TYPE_MULTYPE, TYPE_DICT, TYPE_VOID, TYPE_FUNC, TYPE_CUSTOM,
    INT_LIT, FLOAT_LIT, BOOL_LIT, STR_LIT, NONE_LIT, IDENTIFIER,
    ASSIGN, PLUS_ASSIGN, MINUS_ASSIGN, STAR_ASSIGN, SLASH_ASSIGN, MODULO_ASSIGN, POW_ASSIGN,
    PLUS, MINUS, STAR, SLASH, MODULO, FLOOR_DIV, POW,
    EQ, NOT_EQ, AND, OR, NOT, BAND, BOR, BXOR, BNOT, BSHL, BSHR,
    LBRACKET, RBRACKET, LBRACE, RBRACE, LPAREN, RPAREN, DOT,
    COMMA, COLON, LESS, LESS_EQ, GREATER, GREATER_EQ, ARROW,
    IF, ELIF, ELSE, WHILE, FOR, IN, FUNC, FDFUNC, CFUNC, CLASS, ENUM, STRUCT, LOCVAR, PARVAR, RETURN, BREAK, CONTINUE, CONS_OUT, CONS_CLEAR, KEY_INPUT, AS, FILE_OPEN, TRY, EXCEPT, THROW, RAISE_ERROR, INLINE, DEFER, PUBLIC, PRIVATE, PROTECTED, EOF_TOKEN
};

struct Token {
    TokenType type;
    std::string lexeme;
    int line;
};

inline std::set<std::string> g_attachedFiles;
inline std::vector<std::string> g_cAttachedHeaders;
inline std::set<std::string> g_cAttachedSet;
inline bool g_hasCAttach = false;
inline bool g_hasFileOpen = false;
inline bool g_hasTryExcept = false;
inline bool g_hasDict = false;
inline std::unordered_set<std::string> g_isCFunc;
inline std::set<std::string> g_classNames;

// `c.<name>` is the C-interop namespace (see the C/C++ interoperability section).
// A program may also declare its own variable named `c`; then `c.<name>` must be
// ordinary member access. Detected once from the token stream and also applied to
// the snippets parsed for `@{...}` interpolation.
inline bool g_cNameIsVariable = false;

inline bool detectCNameVariable(const std::vector<Token>& toks) {
    auto isTypeTok = [](TokenType t) {
        return t == TokenType::TYPE_INT || t == TokenType::TYPE_INT64 || t == TokenType::TYPE_INT16 ||
               t == TokenType::TYPE_FL || t == TokenType::TYPE_DBL || t == TokenType::TYPE_BL ||
               t == TokenType::TYPE_STR || t == TokenType::TYPE_LST || t == TokenType::TYPE_TPLE ||
               t == TokenType::TYPE_MULTYPE || t == TokenType::TYPE_DICT || t == TokenType::TYPE_FUNC ||
               t == TokenType::TYPE_CUSTOM;
    };
    for (size_t i = 1; i + 1 < toks.size(); ++i) {
        if (toks[i].type != TokenType::IDENTIFIER || toks[i].lexeme != "c") continue;
        size_t typeEnd = i - 1;
        if (typeEnd > 0 && toks[typeEnd].type == TokenType::IDENTIFIER && toks[typeEnd].lexeme == "const")
            --typeEnd;
        if (!isTypeTok(toks[typeEnd].type) && toks[typeEnd].type != TokenType::GREATER) continue;
        // Declarator followed by an initializer, a parameter list or a field list.
        TokenType nt = toks[i + 1].type;
        if (nt == TokenType::ASSIGN || nt == TokenType::COMMA || nt == TokenType::RPAREN ||
            nt == TokenType::IDENTIFIER)
            return true;
    }
    return false;
}

// ==========================================
// 1.5. TYPE SYSTEM & DYNAMIC LIBRARIES (Thava Types & Dynamic Libraries)
// ==========================================
enum class ThavaType { INT, FLOAT, DOUBLE, BOOL, STRING, LIST, TUPLE, MULTYPE, DICT, VOID, FUNC, FILE, CUSTOM, NONE, UNKNOWN };

inline std::string typeToString(ThavaType type) {
    switch (type) {
        case ThavaType::INT: return "int";
        case ThavaType::FLOAT: return "fl";
        case ThavaType::DOUBLE: return "dbl";
        case ThavaType::BOOL: return "bl";
        case ThavaType::STRING: return "str";
        case ThavaType::LIST: return "lst";
        case ThavaType::TUPLE: return "tple";
        case ThavaType::MULTYPE: return "multype";
        case ThavaType::DICT: return "dict";
        case ThavaType::VOID: return "void";
        case ThavaType::FUNC: return "func";
        case ThavaType::FILE: return "file";
        case ThavaType::CUSTOM: return "custom";
        case ThavaType::NONE: return "none";
        default: return "unknown";
    }
}

inline ThavaType stringToType(const std::string& s) {
    if (s == "int" || s == "int16" || s == "int64") return ThavaType::INT;
    if (s == "fl" || s == "float") return ThavaType::FLOAT;
    if (s == "dbl" || s == "double") return ThavaType::DOUBLE;
    if (s == "bl" || s == "bool") return ThavaType::BOOL;
    if (s == "str" || s == "string") return ThavaType::STRING;
    if (s == "lst" || s == "list") return ThavaType::LIST;
    if (s == "tple" || s == "tuple") return ThavaType::TUPLE;
    if (s == "multype") return ThavaType::MULTYPE;
    if (s == "dict" || s == "map") return ThavaType::DICT;
    if (s == "void") return ThavaType::VOID;
    if (s == "func" || s == "fn" || s == "callback") return ThavaType::FUNC;
    if (s == "file") return ThavaType::FILE;
    if (s == "none") return ThavaType::NONE;
    return ThavaType::UNKNOWN;
}

struct EnumInfo {
    std::string name;
    std::unordered_map<std::string, int64_t> members;
};
inline std::unordered_map<std::string, EnumInfo> g_enums;

struct StructFieldInfo {
    std::string name;
    ThavaType type = ThavaType::UNKNOWN;
    std::string customType;
    int32_t offset = 0;
};

struct StructInfo {
    std::string name;
    std::vector<StructFieldInfo> fields;
    std::unordered_map<std::string, size_t> fieldIndex;
    int32_t totalSize = 0;
};
inline std::unordered_map<std::string, StructInfo> g_structs;

// Set when the source uses features the native x86-64 backend cannot emit
// (classes, try/except) so that backend can fail loudly instead of
// silently generating a program that ignores them.
inline bool g_usesNativeUnsupportedFeature = false;

inline std::string cppIntName(const std::string& width) {
    if (width == "int64") return "long long";
    if (width == "int16") return "short";
    return "int"; // <int> is 32-bit by default
}

inline std::string cppTypeNameFor(ThavaType type, const std::string& customName = "") {
    switch (type) {
        case ThavaType::INT: return cppIntName(customName);
        case ThavaType::FLOAT: return "float";
        case ThavaType::DOUBLE: return "double";
        case ThavaType::BOOL: return "bool";
        case ThavaType::STRING: return "std::string";
        case ThavaType::LIST:
        case ThavaType::TUPLE:
        case ThavaType::MULTYPE: return "std::vector<ThavaValue>";
        case ThavaType::DICT: return "ThavaDict";
        case ThavaType::VOID: return "void";
        case ThavaType::FUNC: return "_ThavaFuncWrapper";
        case ThavaType::CUSTOM:
            if (g_structs.count(customName) || g_enums.count(customName)) return customName;
            return "thava_class_" + customName;
        case ThavaType::NONE: return "ThavaNoneType";
        default: return "long long";
    }
}

inline std::string cppParamTypeNameFor(ThavaType type, const std::string& customName = "") {
    switch (type) {
        case ThavaType::INT: return cppIntName(customName);
        case ThavaType::FLOAT: return "float";
        case ThavaType::DOUBLE: return "double";
        case ThavaType::BOOL: return "bool";
        // Strings are passed by value: assigning to a string parameter only
        // rebinds the local copy (same as the native backends, where a string
        // is a pointer and reassigning it never mutates the caller's value).
        case ThavaType::STRING: return "std::string";
        case ThavaType::LIST:
        case ThavaType::MULTYPE: return "std::vector<ThavaValue>&";
        case ThavaType::TUPLE: return "const std::vector<ThavaValue>&";
        case ThavaType::DICT: return "ThavaDict&";
        case ThavaType::VOID: return "void";
        case ThavaType::FUNC: return "const _ThavaFuncWrapper&";
        case ThavaType::CUSTOM:
            if (g_enums.count(customName)) return customName;
            if (g_structs.count(customName)) return customName + "&";
            return "thava_class_" + customName + "&";
        case ThavaType::NONE: return "const ThavaNoneType&";
        default: return "long long";
    }
}

inline bool isCompatible(ThavaType dest, ThavaType src) {
    if (dest == src) return true;
    if (src == ThavaType::NONE || dest == ThavaType::NONE) return true;
    if (dest == ThavaType::FUNC && src == ThavaType::FUNC) return true;
    if ((dest == ThavaType::FUNC && src == ThavaType::INT) || (dest == ThavaType::INT && src == ThavaType::FUNC)) return true;
    if (dest == ThavaType::DOUBLE && (src == ThavaType::FLOAT || src == ThavaType::INT)) return true;
    if (dest == ThavaType::FLOAT && (src == ThavaType::DOUBLE || src == ThavaType::INT)) return true;
    if (dest == ThavaType::INT && (src == ThavaType::FLOAT || src == ThavaType::DOUBLE)) return true;
    if (dest == ThavaType::MULTYPE && (src == ThavaType::TUPLE || src == ThavaType::LIST || src == ThavaType::MULTYPE)) return true;
    if ((dest == ThavaType::TUPLE || dest == ThavaType::LIST) && src == ThavaType::MULTYPE) return true;
    if (dest == ThavaType::DICT && (src == ThavaType::DICT || src == ThavaType::NONE)) return true;
    if (src == ThavaType::DICT && (dest == ThavaType::DICT || dest == ThavaType::NONE)) return true;
    if (dest == ThavaType::CUSTOM && src == ThavaType::CUSTOM) return true;
    if ((dest == ThavaType::CUSTOM && src == ThavaType::INT) || (dest == ThavaType::INT && src == ThavaType::CUSTOM)) return true;
    return false;
}

inline std::map<std::string, ThavaType> g_funcReturnTypes;
// Declared integer width of each function's return type: "" = <int> (32-bit),
// "int64" = <int64>, "int16" = <int16>. Used by the native backends to narrow
// returned integer values to the declared width.
inline std::map<std::string, std::string> g_funcReturnWidths;
inline std::map<std::string, std::vector<ThavaType>> g_funcParamTypes;
// Element type of each list/tuple parameter, inferred from the call sites that
// pass a list whose element type is known (see noteListParamElemType). The native
// backends read this so `param[i]` inside a function is boxed exactly like `arg[i]`
// at the call site; without it a <lst> parameter fell back to <int> and double
// elements were read as their raw bit patterns.
inline std::map<std::string, std::vector<ThavaType>> g_funcListParamElem;
inline std::map<std::string, size_t> g_funcMinArgs;
inline std::map<std::string, std::vector<std::vector<Token>>> g_funcDefaultArgTokens;

struct DLFunctionInfo {
    std::string name;
    ThavaType retType = ThavaType::VOID;
    std::vector<ThavaType> paramTypes;
    std::vector<std::string> paramNames;
    std::string returnWidth;
    std::vector<std::string> paramWidths;
    std::string libPath;
    std::string libBasename;
    uint32_t libTextRva = 0;   // image offset of the library's .text
    uint32_t funcOffset = 0;   // offset of the export inside that .text
    bool hasOffset = false;
};

inline std::map<std::string, DLFunctionInfo> g_dlFunctions;
inline std::vector<std::string> g_dlLibraries;
inline std::set<std::string> g_dlLibrariesSet;
inline bool g_hasDLAttach = false;
// Cross-compiling from Windows for a Linux target: prefer the .so over a sibling
// .thdl/.dll when "@dlattach <name>" has to pick an extension.
inline bool g_dlPreferSo = false;

// Export table of a dynamic library built by this compiler: the image offset of
// its .text, the offset of each exported function, the 8-byte absolute words the
// runtime loader must relocate, and the optional module initializer. The
// zero-libc backends (ELF64 x86-64 and ELF64 AArch64) map the library themselves
// instead of relying on a system dynamic linker.
struct DLLibraryInfo {
    std::string path;
    std::string basename;      // fallback the loader tries in the current directory
    std::string machine;       // "x86-64-elf", "arm64-elf", ... (empty = legacy library)
    uint64_t imageBase = 0x400000;  // base the library was linked for
    uint32_t textRva = 0;
    uint32_t dataRva = 0;
    uint32_t initOffset = 0;
    bool hasInit = false;
    bool hasPEInitializer = false;
    bool hasOffsets = false;
    std::vector<uint32_t> relocs;   // image offsets of 8-byte absolute words
    std::map<std::string, std::string> objectLayouts;
};

inline std::map<std::string, DLLibraryInfo> g_dlLibraryInfos;

// Extra zero-filled tail left after each mapped image: it becomes the module's heap,
// so a library can never hand out addresses that overlap the next window.
inline constexpr int64_t kDLHeapSlack = 0x30000;

// Every attached library gets its own fixed window; the executable keeps 0x400000.
inline uint64_t dlLoadBaseForPath(const std::string& path) {
    static const uint64_t DL_BASE = 0x10000000;
    static const uint64_t DL_STRIDE = 0x4000000;
    size_t idx = 0;
    for (const auto& p : g_dlLibraries) {
        if (p == path) return DL_BASE + idx * DL_STRIDE;
        ++idx;
    }
    return DL_BASE;
}
inline bool g_hasCLib = false;
inline bool g_isLibraryMode = false;
inline int g_optLevel = 0;
inline std::string g_explicitCxx = "";

enum class HostCXXType {
    NONE,
    GCC,      // g++ / c++
    CLANG,    // clang++
    MSVC      // cl.exe
};

struct DetectedCompiler {
    HostCXXType type = HostCXXType::NONE;
    std::string command = "";
    std::string displayName = "";
};

inline bool probeCompilerCommand(const std::string& cmd) {
#ifdef _WIN32
    std::string testCmd = "where " + cmd + " >nul 2>nul";
#else
    std::string testCmd = "command -v " + cmd + " >/dev/null 2>&1";
#endif
    return (std::system(testCmd.c_str()) == 0);
}

inline DetectedCompiler detectHostCXXCompiler(const std::string& explicitCxx = "") {
    std::string candidate = explicitCxx;
    if (candidate.empty()) {
        const char* envCxx = std::getenv("CXX");
        if (envCxx && *envCxx) {
            candidate = envCxx;
        }
    }

    if (!candidate.empty()) {
        if (probeCompilerCommand(candidate)) {
            std::string lower = candidate;
            for (char& c : lower) c = tolower((unsigned char)c);
            if (lower.find("clang") != std::string::npos) {
                return { HostCXXType::CLANG, candidate, "LLVM Clang (" + candidate + ")" };
            } else if (lower == "cl" || lower == "cl.exe") {
                return { HostCXXType::MSVC, candidate, "Microsoft Visual C++ (" + candidate + ")" };
            } else {
                return { HostCXXType::GCC, candidate, "GCC / G++ (" + candidate + ")" };
            }
        }
    }

    // Auto-detection probe in priority order: g++, clang++, c++, cl
    if (probeCompilerCommand("g++")) {
        return { HostCXXType::GCC, "g++", "MinGW-w64 / GCC (g++)" };
    }
    if (probeCompilerCommand("clang++")) {
        return { HostCXXType::CLANG, "clang++", "LLVM Clang (clang++)" };
    }
    if (probeCompilerCommand("c++")) {
        return { HostCXXType::GCC, "c++", "Standard POSIX C++ (c++)" };
    }
    if (probeCompilerCommand("cl")) {
        return { HostCXXType::MSVC, "cl", "Microsoft Visual C++ (cl.exe)" };
    }

    return { HostCXXType::NONE, "", "" };
}

inline void parseDLMetadata(const std::string& content, const std::string& libPath, int line) {
    std::string libBasename = std::filesystem::path(libPath).filename().string();
    std::stringstream ss(content);
    std::string rawLine;
    while (std::getline(ss, rawLine)) {
        size_t first = rawLine.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) continue;
        size_t last = rawLine.find_last_not_of(" \t\r\n");
        std::string lineStr = rawLine.substr(first, last - first + 1);

        if (lineStr.empty() || lineStr[0] == '#') continue;

        DLLibraryInfo& libInfo = g_dlLibraryInfos[libPath];
        if (libInfo.path.empty()) libInfo.path = libPath;
        if (libInfo.basename.empty()) libInfo.basename = libBasename;
        if (lineStr.rfind("image:", 0) == 0) { libInfo.imageBase = std::strtoull(lineStr.c_str() + 6, nullptr, 0); continue; }
        if (lineStr.rfind("text:", 0) == 0)  { libInfo.textRva = (uint32_t)std::strtoul(lineStr.c_str() + 5, nullptr, 0); libInfo.hasOffsets = true; continue; }
        if (lineStr == "pe-dll:1") { libInfo.hasPEInitializer = true; continue; }
        if (lineStr.rfind("machine:", 0) == 0) { libInfo.machine = lineStr.substr(8); continue; }
        if (lineStr.rfind("layout:", 0) == 0) {
            const auto separator = lineStr.find('|', 7);
            if (separator != std::string::npos)
                libInfo.objectLayouts[lineStr.substr(7, separator - 7)] = lineStr.substr(separator + 1);
            continue;
        }
        if (lineStr.rfind("data:", 0) == 0)  { libInfo.dataRva = (uint32_t)std::strtoul(lineStr.c_str() + 5, nullptr, 0); continue; }
        if (lineStr.rfind("init:", 0) == 0)  { libInfo.initOffset = (uint32_t)std::strtoul(lineStr.c_str() + 5, nullptr, 0); libInfo.hasInit = true; continue; }
        if (lineStr.rfind("reloc:", 0) == 0) {
            std::stringstream rStream(lineStr.substr(6));
            std::string entry;
            while (std::getline(rStream, entry, ',')) {
                size_t f = entry.find_first_not_of(" \t");
                if (f == std::string::npos) continue;
                libInfo.relocs.push_back((uint32_t)std::strtoul(entry.c_str() + f, nullptr, 0));
            }
            continue;
        }

        if (lineStr.rfind("func:", 0) != 0) continue;

        std::string fname = "";
        ThavaType retType = ThavaType::VOID;
        std::vector<ThavaType> paramTypes;
        std::vector<std::string> paramNames;
        std::string returnWidth;
        std::vector<std::string> paramWidths;
        uint32_t funcOffset = 0;
        bool hasOffset = false;
        const auto signatureType = [](const std::string& name, std::string& width) {
            if (name.rfind("custom:", 0) == 0) {
                width = name.substr(7);
                return ThavaType::CUSTOM;
            }
            if (name == "int16" || name == "int64") width = name;
            return stringToType(name);
        };

        std::stringstream lineStream(lineStr);
        std::string part;
        while (std::getline(lineStream, part, '|')) {
            if (part.rfind("func:", 0) == 0) {
                fname = part.substr(5);
            } else if (part.rfind("ret:", 0) == 0) {
                const auto name = part.substr(4);
                retType = signatureType(name, returnWidth);
            } else if (part.rfind("off:", 0) == 0) {
                funcOffset = (uint32_t)std::strtoul(part.c_str() + 4, nullptr, 0);
                hasOffset = true;
            } else if (part.rfind("params:", 0) == 0) {
                std::string pStr = part.substr(7);
                if (!pStr.empty()) {
                    std::stringstream pStream(pStr);
                    std::string singleParam;
                    while (std::getline(pStream, singleParam, ',')) {
                        size_t pFirst = singleParam.find_first_not_of(" \t");
                        if (pFirst == std::string::npos) continue;
                        size_t pLast = singleParam.find_last_not_of(" \t");
                        std::string pClean = singleParam.substr(pFirst, pLast - pFirst + 1);
                        size_t spacePos = pClean.find(' ');
                        std::string pTypeName = (spacePos != std::string::npos) ? pClean.substr(0, spacePos) : pClean;
                        std::string pName = (spacePos != std::string::npos) ? pClean.substr(spacePos + 1) : "";
                        std::string width;
                        paramTypes.push_back(signatureType(pTypeName, width));
                        paramNames.push_back(pName);
                        paramWidths.push_back(std::move(width));
                    }
                }
            }
        }

        if (!fname.empty()) {
            DLFunctionInfo info;
            info.name = fname;
            info.retType = retType;
            info.paramTypes = paramTypes;
            info.paramNames = paramNames;
            info.returnWidth = returnWidth;
            info.paramWidths = std::move(paramWidths);
            info.libPath = libPath;
            info.libBasename = libBasename;
            info.libTextRva = g_dlLibraryInfos[libPath].textRva;
            info.funcOffset = funcOffset;
            info.hasOffset = hasOffset;

            g_dlFunctions[fname] = info;
            g_funcReturnTypes[fname] = retType;
            g_funcParamTypes[fname] = paramTypes;
            g_funcMinArgs[fname] = paramTypes.size();
        }
    }
}

inline std::string resolveAndLoadDL(const std::string& rawPath, const std::string& currentFilePath, int line) {
    std::vector<std::string> candidates;
    candidates.push_back(rawPath);

    bool hasExt = rawPath.find('.') != std::string::npos;
    if (!hasExt) {
        if (g_dlPreferSo) {
            candidates.push_back(rawPath + ".so");
            candidates.push_back(rawPath + ".thdl");
            candidates.push_back(rawPath + ".dll");
        } else {
#ifdef _WIN32
            candidates.push_back(rawPath + ".thdl");
            candidates.push_back(rawPath + ".dll");
            candidates.push_back(rawPath + ".so");
#else
            candidates.push_back(rawPath + ".so");
            candidates.push_back(rawPath + ".thdl");
            candidates.push_back(rawPath + ".dll");
#endif
        }
    }

    std::string foundPath = "";
    for (const auto& cand : candidates) {
        std::filesystem::path p(cand);
        if (!currentFilePath.empty() && p.is_relative()) {
            std::filesystem::path baseDir = std::filesystem::path(currentFilePath).parent_path();
            if (!baseDir.empty()) {
                std::filesystem::path full = baseDir / p;
                if (std::filesystem::exists(full)) {
                    foundPath = full.string();
                    break;
                }
            }
        }
        if (std::filesystem::exists(p)) {
            foundPath = p.string();
            break;
        }
    }

    if (foundPath.empty()) {
        std::cerr << "@dlattach Error: Dynamic library file not found '" << rawPath << "' at line " << line << "\n";
        exit(1);
    }

    std::error_code ec;
    auto absolutePath = std::filesystem::absolute(foundPath, ec);
    if (ec) absolutePath = foundPath;
    auto canonicalPath = std::filesystem::weakly_canonical(absolutePath, ec);
    const auto normPath = (ec || canonicalPath.empty() ? absolutePath.lexically_normal()
                                                       : canonicalPath).string();
    if (g_dlLibrariesSet.count(normPath)) {
        return normPath;
    }
    g_dlLibrariesSet.insert(normPath);
    g_dlLibraries.push_back(normPath);
    g_hasDLAttach = true;

    // 1. Priority: Read embedded metadata directly inside binary file (.thdl / .so)
    bool loadedMeta = false;
    std::ifstream binFile(foundPath, std::ios::binary);
    if (binFile.is_open()) {
        std::string binContent((std::istreambuf_iterator<char>(binFile)), std::istreambuf_iterator<char>());
        const std::string startTag = "__THAVA_METADATA_START__\n";
        const std::string startTagAlt = "__THAVA_METADATA_START__";
        const std::string endTag = "__THAVA_METADATA_END__";
        size_t pStart = binContent.find(startTag);
        size_t tagLen = startTag.length();
        if (pStart == std::string::npos) {
            pStart = binContent.find(startTagAlt);
            tagLen = startTagAlt.length();
        }
        size_t pEnd = (pStart != std::string::npos) ? binContent.find(endTag, pStart + tagLen) : std::string::npos;
        if (pStart != std::string::npos && pEnd != std::string::npos) {
            std::string metaContent = binContent.substr(pStart + tagLen, pEnd - (pStart + tagLen));
            parseDLMetadata(metaContent, normPath, line);
            loadedMeta = true;
        }
    }

    // 2. Fallback: Check companion .meta file if present
    if (!loadedMeta) {
        std::string metaPath = foundPath + ".meta";
        if (std::filesystem::exists(metaPath)) {
            std::ifstream mf(metaPath);
            if (mf.is_open()) {
                std::string content((std::istreambuf_iterator<char>(mf)), std::istreambuf_iterator<char>());
                parseDLMetadata(content, normPath, line);
                loadedMeta = true;
            }
        }
    }

    if (!loadedMeta) {
        std::cerr << "@dlattach Warning: Dynamic library '" << foundPath << "' did not contain valid embedded metadata!\n";
    }

    return normPath;
}

inline void inspectLibrary(const std::string& rawPath) {
    std::string foundPath = rawPath;
    if (!std::filesystem::exists(foundPath)) {
#ifdef _WIN32
        if (std::filesystem::exists(rawPath + ".thdl")) foundPath = rawPath + ".thdl";
        else if (std::filesystem::exists(rawPath + ".dll")) foundPath = rawPath + ".dll";
        else if (std::filesystem::exists(rawPath + ".so")) foundPath = rawPath + ".so";
#else
        if (std::filesystem::exists(rawPath + ".so")) foundPath = rawPath + ".so";
        else if (std::filesystem::exists(rawPath + ".thdl")) foundPath = rawPath + ".thdl";
#endif
    }

    if (!std::filesystem::exists(foundPath)) {
        std::cerr << "Error: Dynamic library file not found '" << rawPath << "'\n";
        exit(1);
    }

    std::ifstream binFile(foundPath, std::ios::binary);
    if (!binFile.is_open()) {
        std::cerr << "Error: Cannot open file '" << foundPath << "'\n";
        exit(1);
    }

    std::string binContent((std::istreambuf_iterator<char>(binFile)), std::istreambuf_iterator<char>());
    const std::string startTag = "__THAVA_METADATA_START__\n";
    const std::string startTagAlt = "__THAVA_METADATA_START__";
    const std::string endTag = "__THAVA_METADATA_END__";
    size_t pStart = binContent.find(startTag);
    size_t tagLen = startTag.length();
    if (pStart == std::string::npos) {
        pStart = binContent.find(startTagAlt);
        tagLen = startTagAlt.length();
    }
    size_t pEnd = (pStart != std::string::npos) ? binContent.find(endTag, pStart + tagLen) : std::string::npos;

    std::string metaContent = "";
    if (pStart != std::string::npos && pEnd != std::string::npos) {
        metaContent = binContent.substr(pStart + tagLen, pEnd - (pStart + tagLen));
    } else if (std::filesystem::exists(foundPath + ".meta")) {
        std::ifstream mf(foundPath + ".meta");
        metaContent.assign((std::istreambuf_iterator<char>(mf)), std::istreambuf_iterator<char>());
    }

    if (metaContent.empty()) {
        std::cerr << "[Thava] File '" << foundPath << "' khong chua metadata Thava hop le!\n";
        exit(1);
    }

    std::cout << "========================================================\n";
    std::cout << "[Thava Dynamic Library Info]: " << foundPath << "\n";
    std::cout << "========================================================\n";
    std::cout << "Exported Functions:\n";

    std::stringstream ss(metaContent);
    std::string rawLine;
    int count = 0;
    while (std::getline(ss, rawLine)) {
        size_t first = rawLine.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) continue;
        size_t last = rawLine.find_last_not_of(" \t\r\n");
        std::string lineStr = rawLine.substr(first, last - first + 1);

        if (lineStr.empty() || lineStr[0] == '#') continue;
        if (lineStr.rfind("func:", 0) != 0) continue;

        std::string fname = "";
        std::string retType = "void";
        std::string paramsStr = "";

        std::stringstream lineStream(lineStr);
        std::string part;
        while (std::getline(lineStream, part, '|')) {
            if (part.rfind("func:", 0) == 0) fname = part.substr(5);
            else if (part.rfind("ret:", 0) == 0) retType = part.substr(4);
            else if (part.rfind("params:", 0) == 0) paramsStr = part.substr(7);
        }

        if (!fname.empty()) {
            count++;
            std::cout << "  " << count << ". <" << retType << "> func " << fname << "(";
            if (!paramsStr.empty()) {
                std::stringstream pStream(paramsStr);
                std::string singleParam;
                bool firstP = true;
                while (std::getline(pStream, singleParam, ',')) {
                    size_t pFirst = singleParam.find_first_not_of(" \t");
                    if (pFirst == std::string::npos) continue;
                    size_t pLast = singleParam.find_last_not_of(" \t");
                    std::string pClean = singleParam.substr(pFirst, pLast - pFirst + 1);
                    size_t spacePos = pClean.find(' ');
                    std::string pTypeName = (spacePos != std::string::npos) ? pClean.substr(0, spacePos) : pClean;
                    std::string pName = (spacePos != std::string::npos) ? pClean.substr(spacePos + 1) : "";
                    if (!firstP) std::cout << ", ";
                    std::cout << "<" << pTypeName << "> " << pName;
                    firstP = false;
                }
            }
            std::cout << ")\n";
        }
    }
    std::cout << "Total: " << count << " functions.\n";
}

class Lexer {
    std::string source;
    std::string currentFilePath;
    size_t start = 0;
    size_t current = 0;
    int line = 1;

    bool isAtEnd() { return current >= source.length(); }
    char advance() { return source[current++]; }
    char peek() { return isAtEnd() ? '\0' : source[current]; }
    char peekNext() { return (current + 1 >= source.length()) ? '\0' : source[current + 1]; }

public:
    Lexer(std::string src, std::string curPath = "", int startLine = 1)
        : source(std::move(src)), currentFilePath(std::move(curPath)), line(startLine) {
        if (source.size() >= 3 &&
            static_cast<unsigned char>(source[0]) == 0xEF &&
            static_cast<unsigned char>(source[1]) == 0xBB &&
            static_cast<unsigned char>(source[2]) == 0xBF) {
            source.erase(0, 3);
        }
    }

    std::vector<Token> scanTokens() {
        std::vector<Token> tokens;
        while (!isAtEnd()) {
            start = current;
            char c = advance();
            
            if (isspace(c)) {
                if (c == '\n') line++;
                continue;
            }

            if (c == '#') {
                while (!isAtEnd() && peek() != '\n') advance();
                continue;
            }

            if (c == '<') {
                size_t saved_current = current;
                std::string typeStr = "";
                while (!isAtEnd() && peek() != '>' && !isspace(peek()) && typeStr.length() < 64) {
                    typeStr += advance();
                }
                
                if (!isAtEnd() && peek() == '>') {
                    advance(); // Skip '>'
                    if (typeStr == "int") tokens.push_back({TokenType::TYPE_INT, "<int>", line});
                    else if (typeStr == "int64") tokens.push_back({TokenType::TYPE_INT64, "<int64>", line});
                    else if (typeStr == "int16") tokens.push_back({TokenType::TYPE_INT16, "<int16>", line});
                    else if (typeStr == "fl" || typeStr == "float") tokens.push_back({TokenType::TYPE_FL, "<fl>", line});
                    else if (typeStr == "dbl" || typeStr == "double") tokens.push_back({TokenType::TYPE_DBL, "<dbl>", line});
                    else if (typeStr == "bl" || typeStr == "bool") tokens.push_back({TokenType::TYPE_BL, "<bl>", line});
                    else if (typeStr == "str" || typeStr == "string") tokens.push_back({TokenType::TYPE_STR, "<str>", line});
                    else if (typeStr == "lst" || typeStr == "list") tokens.push_back({TokenType::TYPE_LST, "<lst>", line});
                    else if (typeStr == "tple" || typeStr == "tuple") tokens.push_back({TokenType::TYPE_TPLE, "<tple>", line});
                    else if (typeStr == "multype") tokens.push_back({TokenType::TYPE_MULTYPE, "<multype>", line});
                    else if (typeStr == "dict" || typeStr == "map") tokens.push_back({TokenType::TYPE_DICT, "<dict>", line});
                    else if (typeStr == "void") tokens.push_back({TokenType::TYPE_VOID, "<void>", line});
                    else if (typeStr == "func" || typeStr == "fn" || typeStr == "callback") tokens.push_back({TokenType::TYPE_FUNC, "<func>", line});
                    else {
                        auto isValidIdent = [](const std::string& s) -> bool {
                            if (s.empty()) return false;
                            if (!isalpha((unsigned char)s[0]) && s[0] != '_') return false;
                            for (char ch : s) {
                                if (!isalnum((unsigned char)ch) && ch != '_') return false;
                            }
                            return true;
                        };
                        if (isValidIdent(typeStr)) {
                            tokens.push_back({TokenType::TYPE_CUSTOM, typeStr, line});
                        } else {
                            current = saved_current;
                            if (peek() == '=') { advance(); tokens.push_back({TokenType::LESS_EQ, "<=", line}); }
                            else tokens.push_back({TokenType::LESS, "<", line});
                        }
                    }
                } else {
                    current = saved_current;
                    if (peek() == '=') { advance(); tokens.push_back({TokenType::LESS_EQ, "<=", line}); }
                    else tokens.push_back({TokenType::LESS, "<", line});
                }
            }
            else if (c == '>') {
                if (peek() == '=') { advance(); tokens.push_back({TokenType::GREATER_EQ, ">=", line}); }
                else tokens.push_back({TokenType::GREATER, ">", line});
            }
            else if (isdigit(c)) {
                while (isdigit(peek())) advance();
                if (peek() == '.' && isdigit(peekNext())) {
                    advance();
                    while (isdigit(peek())) advance();
                    tokens.push_back({TokenType::FLOAT_LIT, source.substr(start, current - start), line});
                } else {
                    tokens.push_back({TokenType::INT_LIT, source.substr(start, current - start), line});
                }
            }
            else if (c == '"') {
                const int stringLine = line;
                std::string strVal;
                while (!isAtEnd() && peek() != '"') {
                    if (peek() == '\n') line++;
                    if (peek() == '\\') {
                        advance(); // Skip '\'
                        if (isAtEnd()) break;
                        char esc = advance();
                        if (esc == 'n') strVal += '\n';
                        else if (esc == 't') strVal += '\t';
                        else if (esc == 'r') strVal += '\r';
                        else if (esc == '"') strVal += '"';
                        else if (esc == '\\') strVal += '\\';
                        else { strVal += '\\'; strVal += esc; }
                    } else {
                        strVal += advance();
                    }
                }
                if (isAtEnd()) { std::cerr << "Lexer Error: Unterminated string literal at line " << line << "\n"; exit(1); }
                advance(); // Skip closing '"'
                tokens.push_back({TokenType::STR_LIT, std::move(strVal), stringLine});
            }
            else if (isalpha(c) || c == '_') {
                while (isalnum(peek()) || peek() == '_') advance(); // Dot '.' is no longer merged into variable names
                std::string text = source.substr(start, current - start);

                // Only merge dots for specific built-in keywords (cons.out, key.input, key.input.upper/lower/title)
                // Other cases (a.strip(), a.strip().title()...) are handled by Parser via separate DOT tokens,
                // enabling method chaining on results of prior method calls.
                auto tryConsumeLiteral = [&](const std::string& lit) -> bool {
                    if (source.compare(current, lit.size(), lit) == 0) { current += lit.size(); return true; }
                    return false;
                };
                if (text == "cons" && tryConsumeLiteral(".out")) text = "cons.out";
                else if (text == "cons" && tryConsumeLiteral(".clear")) text = "cons.clear";
                else if (text == "key" && tryConsumeLiteral(".input")) {
                    text = "key.input";
                    if (tryConsumeLiteral(".upper")) text = "key.input.upper";
                    else if (tryConsumeLiteral(".lower")) text = "key.input.lower";
                    else if (tryConsumeLiteral(".title")) text = "key.input.title";
                }
                else if (text == "file" && tryConsumeLiteral(".open")) text = "file.open";

                if (text == "true" || text == "false") tokens.push_back({TokenType::BOOL_LIT, text, line});
                else if (text == "none" || text == "None" || text == "null") tokens.push_back({TokenType::NONE_LIT, text, line});
                else if (text == "and") tokens.push_back({TokenType::AND, text, line});
                else if (text == "or") tokens.push_back({TokenType::OR, text, line});
                else if (text == "not") tokens.push_back({TokenType::NOT, text, line});
                else if (text == "band") tokens.push_back({TokenType::BAND, text, line});
                else if (text == "bor") tokens.push_back({TokenType::BOR, text, line});
                else if (text == "bxor") tokens.push_back({TokenType::BXOR, text, line});
                else if (text == "bnot") tokens.push_back({TokenType::BNOT, text, line});
                else if (text == "bshl") tokens.push_back({TokenType::BSHL, text, line});
                else if (text == "bshr") tokens.push_back({TokenType::BSHR, text, line});
                else if (text == "if") tokens.push_back({TokenType::IF, text, line});
                else if (text == "elif") tokens.push_back({TokenType::ELIF, text, line});
                else if (text == "else") tokens.push_back({TokenType::ELSE, text, line});
                else if (text == "while") tokens.push_back({TokenType::WHILE, text, line});
                else if (text == "for") tokens.push_back({TokenType::FOR, text, line});
                else if (text == "in") tokens.push_back({TokenType::IN, text, line});
                else if (text == "func") tokens.push_back({TokenType::FUNC, text, line});
                else if (text == "fdfunc") tokens.push_back({TokenType::FDFUNC, text, line});
                else if (text == "cfunc") tokens.push_back({TokenType::CFUNC, text, line});
                else if (text == "class") {
                    tokens.push_back({TokenType::CLASS, text, line});
                    size_t tmpCur = current;
                    while (tmpCur < source.length() && isspace((unsigned char)source[tmpCur])) tmpCur++;
                    if (tmpCur < source.length() && (isalpha((unsigned char)source[tmpCur]) || source[tmpCur] == '_')) {
                        size_t idStart = tmpCur;
                        while (tmpCur < source.length() && (isalnum((unsigned char)source[tmpCur]) || source[tmpCur] == '_')) tmpCur++;
                        std::string clsName = source.substr(idStart, tmpCur - idStart);
                        g_classNames.insert(clsName);
                    }
                }
                else if (text == "struct") {
                    tokens.push_back({TokenType::STRUCT, text, line});
                    size_t tmpCur = current;
                    while (tmpCur < source.length() && isspace((unsigned char)source[tmpCur])) tmpCur++;
                    if (tmpCur < source.length() && (isalpha((unsigned char)source[tmpCur]) || source[tmpCur] == '_')) {
                        size_t idStart = tmpCur;
                        while (tmpCur < source.length() && (isalnum((unsigned char)source[tmpCur]) || source[tmpCur] == '_')) tmpCur++;
                        std::string sName = source.substr(idStart, tmpCur - idStart);
                        g_classNames.insert(sName);
                    }
                }
                else if (text == "enum") {
                    tokens.push_back({TokenType::ENUM, text, line});
                    size_t tmpCur = current;
                    while (tmpCur < source.length() && isspace((unsigned char)source[tmpCur])) tmpCur++;
                    if (tmpCur < source.length() && (isalpha((unsigned char)source[tmpCur]) || source[tmpCur] == '_')) {
                        size_t idStart = tmpCur;
                        while (tmpCur < source.length() && (isalnum((unsigned char)source[tmpCur]) || source[tmpCur] == '_')) tmpCur++;
                        std::string eName = source.substr(idStart, tmpCur - idStart);
                        g_classNames.insert(eName);
                    }
                }
                else if (text == "locvar") tokens.push_back({TokenType::LOCVAR, text, line});
                else if (text == "parvar") tokens.push_back({TokenType::PARVAR, text, line});
                else if (text == "return") tokens.push_back({TokenType::RETURN, text, line});
                else if (text == "break") tokens.push_back({TokenType::BREAK, text, line});
                else if (text == "continue") tokens.push_back({TokenType::CONTINUE, text, line});
                else if (text == "public") tokens.push_back({TokenType::PUBLIC, text, line});
                else if (text == "private") tokens.push_back({TokenType::PRIVATE, text, line});
                else if (text == "protected") tokens.push_back({TokenType::PROTECTED, text, line});
                else if (text == "cons.out") tokens.push_back({TokenType::CONS_OUT, text, line});
                else if (text == "cons.clear") tokens.push_back({TokenType::CONS_CLEAR, text, line});
                else if (text == "key.input") tokens.push_back({TokenType::KEY_INPUT, text, line});
                else if (text == "file.open") {
                    tokens.push_back({TokenType::FILE_OPEN, text, line});
                    g_hasFileOpen = true;
                }
                else if (text == "as") tokens.push_back({TokenType::AS, text, line});
                else if (text == "try") {
                    tokens.push_back({TokenType::TRY, text, line});
                    g_hasTryExcept = true;
                }
                else if (text == "except") tokens.push_back({TokenType::EXCEPT, text, line});
                else if (text == "throw" || text == "raise") tokens.push_back({TokenType::THROW, text, line});
                else if (text == "raise_error") {
                    tokens.push_back({TokenType::RAISE_ERROR, text, line});
                    g_hasTryExcept = true;
                }
                else if (text == "inline") tokens.push_back({TokenType::INLINE, text, line});
                else if (text == "defer") tokens.push_back({TokenType::DEFER, text, line});
                else tokens.push_back({TokenType::IDENTIFIER, text, line});
            }
            else {
                switch (c) {
                    case '=': 
                        if (peek() == '=') { advance(); tokens.push_back({TokenType::EQ, "==", line}); }
                        else tokens.push_back({TokenType::ASSIGN, "=", line});
                        break;
                    case '!':
                        if (peek() == '=') { advance(); tokens.push_back({TokenType::NOT_EQ, "!=", line}); }
                        else tokens.push_back({TokenType::NOT, "not", line});
                        break;
                    case '/':
                        if (peek() == '/') { advance(); tokens.push_back({TokenType::FLOOR_DIV, "//", line}); }
                        else if (peek() == '=') { advance(); tokens.push_back({TokenType::SLASH_ASSIGN, "/=", line}); }
                        else tokens.push_back({TokenType::SLASH, "/", line});
                        break;
                    case '%':
                        if (peek() == '=') { advance(); tokens.push_back({TokenType::MODULO_ASSIGN, "%=", line}); }
                        else tokens.push_back({TokenType::MODULO, "%", line});
                        break;
                    case '+':
                        if (peek() == '=') { advance(); tokens.push_back({TokenType::PLUS_ASSIGN, "+=", line}); }
                        else tokens.push_back({TokenType::PLUS, "+", line});
                        break;
                    case '-':
                        if (peek() == '>') { advance(); tokens.push_back({TokenType::ARROW, "->", line}); }
                        else if (peek() == '=') { advance(); tokens.push_back({TokenType::MINUS_ASSIGN, "-=", line}); }
                        else tokens.push_back({TokenType::MINUS, "-", line});
                        break;
                    case '*':
                        if (peek() == '*') {
                            advance();
                            if (peek() == '=') { advance(); tokens.push_back({TokenType::POW_ASSIGN, "**=", line}); }
                            else tokens.push_back({TokenType::POW, "**", line});
                        }
                        else if (peek() == '=') { advance(); tokens.push_back({TokenType::STAR_ASSIGN, "*=", line}); }
                        else tokens.push_back({TokenType::STAR, "*", line});
                        break;
                    case '(': tokens.push_back({TokenType::LPAREN, "(", line}); break;
                    case ')': tokens.push_back({TokenType::RPAREN, ")", line}); break;
                    case '[': tokens.push_back({TokenType::LBRACKET, "[", line}); break;
                    case ']': tokens.push_back({TokenType::RBRACKET, "]", line}); break;
                    case '{': tokens.push_back({TokenType::LBRACE, "{", line}); break;
                    case '}': tokens.push_back({TokenType::RBRACE, "}", line}); break;
                    case ',': tokens.push_back({TokenType::COMMA, ",", line}); break;
                    case ':': tokens.push_back({TokenType::COLON, ":", line}); break;
                    case ';': break; // Skip semicolon ';' as whitespace/separator
                    case '.': tokens.push_back({TokenType::DOT, ".", line}); break;
                    case '@':
                        if (source.compare(current, 7, "cattach") == 0) {
                            current += 7; // Skip "cattach"
                            while (!isAtEnd() && (source[current] == ' ' || source[current] == '\t')) current++;

                            std::string cPath = "";
                            bool isSys = false;
                            if (!isAtEnd() && source[current] == '<') {
                                isSys = true;
                                current++; // Skip '<'
                                while (!isAtEnd() && source[current] != '>' && source[current] != '\n') {
                                    cPath += source[current++];
                                }
                                if (isAtEnd() || source[current] != '>') {
                                    std::cerr << "Lexer Error: Missing closing '>' in @cattach path at line " << line << "\n";
                                    exit(1);
                                }
                                current++; // Skip '>'
                            } else if (!isAtEnd() && source[current] == '"') {
                                isSys = false;
                                current++; // Skip '"'
                                while (!isAtEnd() && source[current] != '"' && source[current] != '\n') {
                                    cPath += source[current++];
                                }
                                if (isAtEnd() || source[current] != '"') {
                                    std::cerr << "Lexer Error: Missing closing '\"' in @cattach path at line " << line << "\n";
                                    exit(1);
                                }
                                current++; // Skip '"'
                            } else {
                                std::cerr << "Lexer Error: @cattach requires <file.h/hpp> or \"file.h/hpp\" at line " << line << "\n";
                                exit(1);
                            }

                            if (!isSys && !currentFilePath.empty()) {
                                std::filesystem::path relP(cPath);
                                if (relP.is_relative()) {
                                    std::filesystem::path baseDir = std::filesystem::path(currentFilePath).parent_path();
                                    if (!baseDir.empty()) {
                                        std::filesystem::path fullC = baseDir / relP;
                                        if (std::filesystem::exists(fullC)) {
                                            std::error_code ec;
                                            cPath = std::filesystem::weakly_canonical(fullC, ec).generic_string();
                                        }
                                    }
                                }
                            }
                            std::string incDirective = isSys ? ("<" + cPath + ">") : ("\"" + cPath + "\"");
                            if (!g_cAttachedSet.count(incDirective)) {
                                g_cAttachedSet.insert(incDirective);
                                g_cAttachedHeaders.push_back(incDirective);
                            }
                            g_hasCAttach = true;
                        }
                        else if (source.compare(current, 8, "dlattach") == 0) {
                            current += 8; // Skip "dlattach"
                            while (!isAtEnd() && (source[current] == ' ' || source[current] == '\t')) current++;

                            std::string dlPath = "";
                            if (!isAtEnd() && source[current] == '<') {
                                current++; // Skip '<'
                                while (!isAtEnd() && source[current] != '>' && source[current] != '\n') {
                                    dlPath += source[current++];
                                }
                                if (isAtEnd() || source[current] != '>') {
                                    std::cerr << "Lexer Error: Missing closing '>' in @dlattach path at line " << line << "\n";
                                    exit(1);
                                }
                                current++; // Skip '>'
                            } else if (!isAtEnd() && source[current] == '"') {
                                current++; // Skip '"'
                                while (!isAtEnd() && source[current] != '"' && source[current] != '\n') {
                                    dlPath += source[current++];
                                }
                                if (isAtEnd() || source[current] != '"') {
                                    std::cerr << "Lexer Error: Missing closing '\"' in @dlattach path at line " << line << "\n";
                                    exit(1);
                                }
                                current++; // Skip '"'
                            } else {
                                std::cerr << "Lexer Error: @dlattach requires <library> or \"library\" at line " << line << "\n";
                                exit(1);
                            }

                            resolveAndLoadDL(dlPath, currentFilePath, line);
                        }
                        else if (source.compare(current, 6, "attach") == 0) {
                            current += 6; // Skip "attach"
                            while (!isAtEnd() && (source[current] == ' ' || source[current] == '\t')) current++;

                            std::string attachPath = "";
                            if (!isAtEnd() && source[current] == '<') {
                                current++; // Skip '<'
                                while (!isAtEnd() && source[current] != '>' && source[current] != '\n') {
                                    attachPath += source[current++];
                                }
                                if (isAtEnd() || source[current] != '>') {
                                    std::cerr << "Lexer Error: Missing closing '>' in @attach path at line " << line << "\n";
                                    exit(1);
                                }
                                current++; // Skip '>'
                            } else if (!isAtEnd() && source[current] == '"') {
                                current++; // Skip '"'
                                while (!isAtEnd() && source[current] != '"' && source[current] != '\n') {
                                    attachPath += source[current++];
                                }
                                if (isAtEnd() || source[current] != '"') {
                                    std::cerr << "Lexer Error: Missing closing '\"' in @attach path at line " << line << "\n";
                                    exit(1);
                                }
                                current++; // Skip '"'
                            } else {
                                std::cerr << "Lexer Error: @attach requires <path/file.thv> or \"path/file.thv\" at line " << line << "\n";
                                exit(1);
                            }

                            std::filesystem::path target = attachPath;
                            if (!currentFilePath.empty() && target.is_relative()) {
                                std::filesystem::path baseDir = std::filesystem::path(currentFilePath).parent_path();
                                if (!baseDir.empty()) target = baseDir / target;
                            }

                            std::error_code ec;
                            std::string normPath = std::filesystem::weakly_canonical(target, ec).string();
                            if (!std::filesystem::exists(target)) {
                                std::cerr << "@attach Error: File not found '" << attachPath << "' at line " << line << "\n";
                                exit(1);
                            }

                            std::string attachExt = target.extension().string();
                            if (attachExt == ".thdl" || attachExt == ".so" || attachExt == ".dll") {
                                resolveAndLoadDL(attachPath, currentFilePath, line);
                                break;
                            }

                            if (!g_attachedFiles.count(normPath)) {
                                g_attachedFiles.insert(normPath);
                                std::ifstream childFile(target);
                                if (!childFile.is_open()) {
                                    std::cerr << "@attach Error: Cannot open file '" << attachPath << "' at line " << line << "\n";
                                    exit(1);
                                }
                                std::string childSrc((std::istreambuf_iterator<char>(childFile)), std::istreambuf_iterator<char>());
                                Lexer childLexer(childSrc, normPath);
                                std::vector<Token> childTokens = childLexer.scanTokens();
                                while (!childTokens.empty() && childTokens.back().type == TokenType::EOF_TOKEN) {
                                    childTokens.pop_back();
                                }
                                tokens.insert(tokens.end(), childTokens.begin(), childTokens.end());
                            }
                        } else {
                            std::cerr << "Lexer Error: Invalid character '@' at line " << line << "\n";
                            exit(1);
                        }
                        break;
                    default:
                        std::cerr << "Lexer Error: Unknown character '" << c << "' at line " << line << "\n";
                        exit(1);
                }
            }
        }
        tokens.push_back({TokenType::EOF_TOKEN, "", line});
        return tokens;
    }
};

enum class AccessModifier { PUBLIC, PRIVATE, PROTECTED };

inline std::string accessModifierToString(AccessModifier a) {
    switch (a) {
        case AccessModifier::PUBLIC: return "public";
        case AccessModifier::PRIVATE: return "private";
        case AccessModifier::PROTECTED: return "protected";
        default: return "public";
    }
}

struct ClassMemberInfo {
    std::string className;
    std::string baseClassName;
    std::vector<AccessModifier> constructorAccess;
    std::map<std::string, AccessModifier> fieldAccess;
    std::map<std::string, ThavaType> fieldTypes;
    std::map<std::string, std::string> fieldCustomTypes;
    std::map<std::string, AccessModifier> methodAccess;
    std::map<std::string, ThavaType> methodReturnTypes;
    std::map<std::string, std::string> methodCustomReturnTypes;
};

inline std::map<std::string, ClassMemberInfo> g_classInfo;

inline bool isSubclassOf(const std::string& childClass, const std::string& parentClass) {
    if (childClass.empty() || parentClass.empty()) return false;
    if (childClass == parentClass) return true;
    std::string cur = childClass;
    while (!cur.empty()) {
        auto it = g_classInfo.find(cur);
        if (it == g_classInfo.end()) break;
        if (it->second.baseClassName == parentClass) return true;
        cur = it->second.baseClassName;
    }
    return false;
}

struct MemberLookupResult {
    bool found = false;
    AccessModifier access = AccessModifier::PUBLIC;
    std::string definedInClass = "";
    ThavaType type = ThavaType::UNKNOWN;
    std::string customType = "";
};

inline MemberLookupResult lookupField(const std::string& className, const std::string& fieldName) {
    std::string cur = className;
    while (!cur.empty()) {
        auto it = g_classInfo.find(cur);
        if (it == g_classInfo.end()) break;
        const auto& info = it->second;
        auto fit = info.fieldAccess.find(fieldName);
        if (fit != info.fieldAccess.end()) {
            MemberLookupResult res;
            res.found = true;
            res.access = fit->second;
            res.definedInClass = cur;
            auto tit = info.fieldTypes.find(fieldName);
            if (tit != info.fieldTypes.end()) res.type = tit->second;
            auto cit = info.fieldCustomTypes.find(fieldName);
            if (cit != info.fieldCustomTypes.end()) res.customType = cit->second;
            return res;
        }
        cur = info.baseClassName;
    }
    return {false, AccessModifier::PUBLIC, "", ThavaType::UNKNOWN, ""};
}

inline MemberLookupResult lookupMethod(const std::string& className, const std::string& methodName) {
    std::string cur = className;
    while (!cur.empty()) {
        auto it = g_classInfo.find(cur);
        if (it == g_classInfo.end()) break;
        const auto& info = it->second;
        auto mit = info.methodAccess.find(methodName);
        if (mit != info.methodAccess.end()) {
            MemberLookupResult res;
            res.found = true;
            res.access = mit->second;
            res.definedInClass = cur;
            auto tit = info.methodReturnTypes.find(methodName);
            if (tit != info.methodReturnTypes.end()) res.type = tit->second;
            auto cit = info.methodCustomReturnTypes.find(methodName);
            if (cit != info.methodCustomReturnTypes.end()) res.customType = cit->second;
            return res;
        }
        cur = info.baseClassName;
    }
    return {false, AccessModifier::PUBLIC, "", ThavaType::UNKNOWN, ""};
}

// ==========================================
// 2. ENVIRONMENT & SCOPE RESOLUTION (Environment)
// ==========================================
class VarDeclNode;

struct Environment {
    std::unordered_map<std::string, ThavaType> variables;
    std::unordered_map<std::string, const VarDeclNode*> constants;
    std::string functionName;
    std::unordered_set<std::string> writableGlobals;
    // Names declared at the top level of the program (true globals).
    std::map<std::string, bool> globals;
    std::unordered_map<std::string, std::string> customTypes;
    std::string currentClassName = "";
    std::string currentBaseClassName = "";
    std::shared_ptr<Environment> parent;
    
    Environment(std::shared_ptr<Environment> p = nullptr) : functionName(p ? p->functionName : ""), parent(p) {}
    
    bool count(const std::string& name) const {
        if (variables.count(name)) return true;
        if (parent) return parent->count(name);
        return false;
    }
    
    ThavaType get(const std::string& name) const {
        if (variables.count(name)) return variables.at(name);
        if (parent) return parent->get(name);
        return ThavaType::UNKNOWN;
    }

    bool isConst(const std::string& name) const {
        if (variables.count(name)) return constants.count(name) != 0;
        return parent && parent->isConst(name);
    }

    std::string getCustomType(const std::string& name) const {
        if (customTypes.count(name)) return customTypes.at(name);
        if (parent) return parent->getCustomType(name);
        return "";
    }

    std::string getCurrentClassName() const {
        if (!currentClassName.empty()) return currentClassName;
        if (parent) return parent->getCurrentClassName();
        return "";
    }

    std::string getCurrentBaseClassName() const {
        if (!currentBaseClassName.empty()) return currentBaseClassName;
        if (parent) return parent->getCurrentBaseClassName();
        return "";
    }

    std::unordered_map<std::string, ThavaType> listElemTypes;

    void define(const std::string& name, ThavaType type, const std::string& custom = "",
                const VarDeclNode* constDeclaration = nullptr) {
        variables[name] = type;
        if (constDeclaration) constants[name] = constDeclaration;
        else constants.erase(name);
        if (!custom.empty()) customTypes[name] = custom;
    }

    void defineListElem(const std::string& name, ThavaType elemT) {
        listElemTypes[name] = elemT;
    }

    ThavaType getListElem(const std::string& name) const {
        if (listElemTypes.count(name)) return listElemTypes.at(name);
        if (parent) return parent->getListElem(name);
        return ThavaType::UNKNOWN;
    }
};

// ==========================================
// 3. ABSTRACT SYNTAX TREE (AST NODES)
// ==========================================
// ============================================================
// Compiler diagnostics
// ------------------------------------------------------------
// Errors are reported as:
//   error: <message>
//     --> file.thv:12
//    12 | <the source line>
//       |    ^
//     hint: what to check
// ============================================================
namespace diag {

    inline std::string& primaryFile()  { static std::string f; return f; }
    inline std::vector<std::string>& primaryLines() { static std::vector<std::string> l; return l; }
    inline int& tokenLine() { static int n = 0; return n; }
    inline std::string& tokenText() { static std::string t; return t; }
    inline int& prevTokenLine() { static int n = 0; return n; }
    inline std::string& prevTokenText() { static std::string t; return t; }

    inline void loadSource(const std::string& path, const std::string& src) {
        primaryFile() = path;
        primaryLines().clear();
        std::string cur;
        for (char c : src) {
            if (c == '\n') { primaryLines().push_back(cur); cur.clear(); }
            else if (c != '\r') cur += c;
        }
        if (!cur.empty()) primaryLines().push_back(cur);
    }

    inline std::string hintFor(const std::string& msg) {
        auto has = [&](const char* s) { return msg.find(s) != std::string::npos; };
        if (has("Unterminated string"))
            return "Close the string literal with a closing quote.";
        if (has("Missing ')'") || has("Missing closing ')'") || has("Missing ')'"))
            return "A ')' is missing: count the '(' and ')' of this expression.";
        if (has("Missing '('"))
            return "A '(' is missing right after the function or keyword name.";
        if (has("Missing ']'") || has("Missing closing ']'"))
            return "A ']' is missing: index syntax is name[index].";
        if (has("Missing '}'") || has("Expected '}'"))
            return "A '}' is missing: every '{' block must be closed.";
        if (has("Expected '{'"))
            return "The body of if/while/for must start with '{'.";
        if (has("Missing loop variable"))
            return "for loops look like: for <int> i = 0; i < n; i = i + 1 { ... }  (or: for i in 0..n { ... })";
        if (has("not declared") || has("Not declared") || has("undeclared") || has("Undeclared"))
            return "Declare the name before using it, e.g. <int> x = 0";
        if (has("Unknown type") || has("unknown type"))
            return "Valid type tags: <int> <fl> <dbl> <str> <lst> <dict> <bl> <void>";
        if (has("type") || has("Type"))
            return "The value type does not match the declared type; use an explicit cast such as <dbl>value or to_int(value).";
        if (has("Unexpected") || has("unexpected"))
            return "Check the previous line: a bracket, ')' or keyword is probably missing.";
        if (has("cons.clear"))
            return "cons.clear() takes no argument: write cons.clear()";
        if (has("min") || has("max"))
            return "min()/max() need exactly two arguments of the same numeric type: min(a, b)";
        return "";
    }

    inline void emitErrorLine(const std::string& raw);

    class Streambuf : public std::streambuf {
        std::string pending;
    protected:
        int_type overflow(int_type ch) override {
            if (ch == traits_type::eof()) return traits_type::not_eof(ch);
            char c = traits_type::to_char_type(ch);
            if (c == '\n') { emitErrorLine(pending); pending.clear(); }
            else pending.push_back(c);
            return ch;
        }
        std::streamsize xsputn(const char* s, std::streamsize n) override {
            for (std::streamsize i = 0; i < n; ++i) overflow((unsigned char)s[i]);
            return n;
        }
        int sync() override { return 0; }
    };

    inline void emitErrorLine(const std::string& raw) {
        if (raw.empty()) return;
        bool isError = raw.find("Error") != std::string::npos ||
                       raw.find("error") != std::string::npos;
        if (!isError) {
            std::fputs(raw.c_str(), stderr);
            std::fputc('\n', stderr);
            return;
        }

        // Split "Prefix: message" and "[Prefix]: message"
        std::string prefix = "error";
        std::string msg = raw;
        size_t bracket = raw.find("]: ");
        size_t colon = raw.find(": ");
        if (bracket != std::string::npos && (colon == std::string::npos || bracket < colon)) {
            prefix = raw.substr(0, bracket + 1);
            msg = raw.substr(bracket + 2);
        } else if (colon != std::string::npos) {
            prefix = raw.substr(0, colon);
            msg = raw.substr(colon + 2);
        }

        // Extract the reported line number
        int lineNo = 0;
        size_t where = msg.find("at line ");
        if (where == std::string::npos) where = msg.find("line ");
        if (where != std::string::npos) {
            size_t p = msg.find_first_of("0123456789", where);
            if (p != std::string::npos) {
                size_t e = msg.find_first_not_of("0123456789", p);
                try { lineNo = std::stoi(msg.substr(p, e - p)); } catch (...) { lineNo = 0; }
            }
        }

        std::string clean = msg;
        while (!clean.empty() && (clean.front() == ' ' || clean.front() == '\t')) clean.erase(clean.begin());
        size_t tail = clean.find(" at line ");
        if (tail != std::string::npos && lineNo > 0) clean = clean.substr(0, tail);
        while (!clean.empty() && (clean.back() == '\n' || clean.back() == '\r')) clean.pop_back();

        // A "Missing ..." error points at where parsing stopped; the real culprit is
        // usually the token before it, so prefer the previous token in that case.
        std::string caretToken = tokenText();
        if (lineNo > 0 && prevTokenLine() > 0 && clean.find("Missing") != std::string::npos) {
            lineNo = prevTokenLine();
            caretToken = prevTokenText();
        }

        // Codegen errors carry no line number: locate the quoted identifier from the
        // message in the source so the user still gets file:line context.
        if (lineNo == 0 && !primaryLines().empty()) {
            std::string quoted;
            size_t q1 = clean.find('\'');
            if (q1 != std::string::npos) {
                size_t q2 = clean.find('\'', q1 + 1);
                if (q2 != std::string::npos && q2 > q1 + 1) quoted = clean.substr(q1 + 1, q2 - q1 - 1);
            }
            if (quoted.empty()) {
                size_t d1 = clean.find('"');
                if (d1 != std::string::npos) {
                    size_t d2 = clean.find('"', d1 + 1);
                    if (d2 != std::string::npos && d2 > d1 + 1) quoted = clean.substr(d1 + 1, d2 - d1 - 1);
                }
            }
            if (!quoted.empty()) {
                for (size_t i = 0; i < primaryLines().size(); ++i) {
                    if (primaryLines()[i].find(quoted) != std::string::npos) {
                        lineNo = (int)i + 1;
                        caretToken = quoted;
                        break;
                    }
                }
            }
        }

        std::string out;
        out += "error: " + clean + "\n";
        if (lineNo > 0) {
            std::string file = primaryFile().empty() ? std::string("<input>") : primaryFile();
            out += "  --> " + file + ":" + std::to_string(lineNo) + "\n";
        }
        if (lineNo > 0 && lineNo <= (int)primaryLines().size()) {
            const std::string& src = primaryLines()[lineNo - 1];
            std::string num = std::to_string(lineNo);
            std::string pad(num.size(), ' ');
            out += "  " + num + " | " + src + "\n";
            size_t col = std::string::npos;
            if (!caretToken.empty()) col = src.find(caretToken);
            if (col == std::string::npos) col = src.find_first_not_of(" \t");
            if (col == std::string::npos) col = 0;
            out += "  " + pad + " | " + std::string(col, ' ') + "^\n";
        }
        if (lineNo > 0) {
            std::string hint = hintFor(clean);
            if (!hint.empty()) out += "  hint: " + hint + "\n";
        }
        out += "\n";
        std::fputs(out.c_str(), stderr);
    }
}

struct ASTNode {
    virtual ~ASTNode() = default;
    virtual std::string generateCode() = 0;
    virtual ThavaType getType(std::shared_ptr<Environment> env) = 0;
};

using ASTPtr = std::unique_ptr<ASTNode>;

// Helper function to safely escape string literals in generated C++ code
inline std::string escapeCppString(const std::string& s) {
    std::string res;
    for (char ch : s) {
        if (ch == '\\') res += "\\\\";
        else if (ch == '"') res += "\\\"";
        else if (ch == '\n') res += "\\n";
        else if (ch == '\t') res += "\\t";
        else if (ch == '\r') res += "\\r";
        else res += ch;
    }
    return res;
}

class LiteralNode : public ASTNode {
    std::string val;
    ThavaType type;
public:
    LiteralNode(std::string v, ThavaType t) : val(std::move(v)), type(t) {}
    
    std::string getVal() const { return val; }
    ThavaType getLitType() const { return type; }

    std::string generateCode() override {
        if (type == ThavaType::STRING) return "std::string(\"" + escapeCppString(val) + "\")";
        if (type == ThavaType::BOOL) return val;
        if (type == ThavaType::FLOAT || type == ThavaType::DOUBLE) return val;
        if (type == ThavaType::INT) return val + "LL"; // Append LL suffix to avoid ambiguity during ThavaValue initialization
        if (type == ThavaType::NONE) return "_thava_none";
        return val;
    }
    ThavaType getType(std::shared_ptr<Environment>) override { return type; }
};

class InterpolatedStringNode : public ASTNode {
public:
    std::vector<ASTPtr> parts;
    explicit InterpolatedStringNode(std::vector<ASTPtr> p) : parts(std::move(p)) {}

    std::string generateCode() override {
        std::string code = "([&]() { std::ostringstream _thava_interpolation; _thava_interpolation << std::boolalpha";
        for (const auto& part : parts) code += " << (" + part->generateCode() + ")";
        return code + "; return _thava_interpolation.str(); }())";
    }

    ThavaType getType(std::shared_ptr<Environment> env) override {
        for (const auto& part : parts) part->getType(env);
        return ThavaType::STRING;
    }
};

// Names declared at the top level of the program: the C++ backend emits them once
// at file scope, so they are prefixed to avoid clashing with C library names
// (log, abs, index, ...) and references are rewritten to match.
static std::unordered_set<std::string> g_cppTopLevelGlobals;
// Variables declared in the function/method body currently being generated: a
// local with the same name as a global shadows it.
static std::unordered_set<std::string> g_cppCurrentLocals;

static bool cppIsGlobalRef(const std::string& n) {
    return g_cppTopLevelGlobals.count(n) > 0 && !g_cppCurrentLocals.count(n);
}

static std::string cppVarRef(const std::string& n) {
    return cppIsGlobalRef(n) ? "thava_g_" + n : n;
}

class VarNode : public ASTNode {
    std::string name;
public:
    VarNode(std::string n) : name(std::move(n)) {}
    const std::string& getName() const { return name; }
    std::string generateCode() override {
        if (g_funcReturnTypes.count(name)) return "thava_func_" + name;
        return cppVarRef(name);
    }
    ThavaType getType(std::shared_ptr<Environment> env) override {
        if (env->count(name)) return env->get(name);
        if (g_funcReturnTypes.count(name)) return ThavaType::FUNC;
        std::cerr << "Error: Variable '" << name << "' is not declared!\n";
        exit(1);
    }
};

// AST node for unary minus, e.g. -1, -x
class UnaryMinusNode : public ASTNode {
public:
    ASTPtr expr;
    UnaryMinusNode(ASTPtr e) : expr(std::move(e)) {}
    std::string generateCode() override {
        return "(-(" + expr->generateCode() + "))";
    }
    ThavaType getType(std::shared_ptr<Environment> env) override {
        ThavaType t = expr->getType(env);
        if (t != ThavaType::INT && t != ThavaType::FLOAT && t != ThavaType::DOUBLE && t != ThavaType::UNKNOWN) {
            std::cerr << "[Static Type Error]: Unary '-' operator only applies to numeric types!\n";
            exit(1);
        }
        return t;
    }
};

// AST node for unary logical not, e.g. not flag
class UnaryNotNode : public ASTNode {
public:
    ASTPtr expr;
    UnaryNotNode(ASTPtr e) : expr(std::move(e)) {}
    std::string generateCode() override {
        return "(!(" + expr->generateCode() + "))";
    }
    ThavaType getType(std::shared_ptr<Environment> env) override {
        ThavaType t = expr->getType(env);
        if (t != ThavaType::BOOL && t != ThavaType::UNKNOWN) {
            std::cerr << "[Static Type Error]: 'not' operator only applies to boolean <bl>!\n";
            exit(1);
        }
        return ThavaType::BOOL;
    }
};

// AST node for unary bitwise not: bnot x
class UnaryBNotNode : public ASTNode {
public:
    ASTPtr expr;
    UnaryBNotNode(ASTPtr e) : expr(std::move(e)) {}
    std::string generateCode() override {
        return "(~(" + expr->generateCode() + "))";
    }
    ThavaType getType(std::shared_ptr<Environment> env) override {
        ThavaType t = expr->getType(env);
        if (t != ThavaType::INT && t != ThavaType::UNKNOWN) {
            std::cerr << "[Static Type Error]: 'bnot' operator only applies to integer <int>!\n";
            exit(1);
        }
        return ThavaType::INT;
    }
};

// AST node for ternary conditional expression: val_true if cond else val_false
class ConditionalExprNode : public ASTNode {
public:
    ASTPtr condition;
    ASTPtr trueExpr;
    ASTPtr falseExpr;

    ConditionalExprNode(ASTPtr c, ASTPtr t, ASTPtr f)
        : condition(std::move(c)), trueExpr(std::move(t)), falseExpr(std::move(f)) {}

    std::string generateCode() override {
        return "((" + condition->generateCode() + ") ? (" + trueExpr->generateCode() + ") : (" + falseExpr->generateCode() + "))";
    }

    ThavaType getType(std::shared_ptr<Environment> env) override {
        ThavaType ct = condition->getType(env);
        if (ct != ThavaType::BOOL && ct != ThavaType::INT && ct != ThavaType::UNKNOWN) {
            std::cerr << "[Static Type Error]: Ternary condition must be of type <bl>!\n";
            exit(1);
        }
        ThavaType tt = trueExpr->getType(env);
        ThavaType ft = falseExpr->getType(env);
        if (tt == ft) return tt;
        if ((tt == ThavaType::INT && ft == ThavaType::FLOAT) || (tt == ThavaType::FLOAT && ft == ThavaType::INT)) return ThavaType::FLOAT;
        if ((tt == ThavaType::INT && ft == ThavaType::DOUBLE) || (tt == ThavaType::DOUBLE && ft == ThavaType::INT)) return ThavaType::DOUBLE;
        if (tt == ThavaType::UNKNOWN) return ft;
        if (ft == ThavaType::UNKNOWN) return tt;
        return ThavaType::MULTYPE;
    }
};

class BinaryOpNode : public ASTNode {
public:
    ASTPtr left, right;
    std::string op;
    BinaryOpNode(ASTPtr l, std::string o, ASTPtr r) : left(std::move(l)), op(std::move(o)), right(std::move(r)) {}
    std::string generateCode() override {
        if (op == "/" || op == "//") {
            return "(_thava_div(" + left->generateCode() + ", " + right->generateCode() + "))";
        }
        if (op == "%") {
            return "(_thava_mod(" + left->generateCode() + ", " + right->generateCode() + "))";
        }
        if (op == "**") {
            return "(_thava_pow(" + left->generateCode() + ", " + right->generateCode() + "))";
        }
        if (op == "band") return "((" + left->generateCode() + ") & (" + right->generateCode() + "))";
        if (op == "bor")  return "((" + left->generateCode() + ") | (" + right->generateCode() + "))";
        if (op == "bxor") return "((" + left->generateCode() + ") ^ (" + right->generateCode() + "))";
        if (op == "bshl") return "((" + left->generateCode() + ") << (" + right->generateCode() + "))";
        if (op == "bshr") return "((" + left->generateCode() + ") >> (" + right->generateCode() + "))";
        std::string actualOp = op;
        if (op == "and") actualOp = "&&";
        else if (op == "or") actualOp = "||";
        return "(" + left->generateCode() + " " + actualOp + " " + right->generateCode() + ")";
    }
    ThavaType getType(std::shared_ptr<Environment> env) override {
        ThavaType lt = left->getType(env);
        ThavaType rt = right->getType(env);

        if (op == "**") {
            if (lt == ThavaType::DOUBLE || rt == ThavaType::DOUBLE) return ThavaType::DOUBLE;
            if (lt == ThavaType::FLOAT || rt == ThavaType::FLOAT) return ThavaType::FLOAT;
            return ThavaType::INT;
        }

        if (op == "band" || op == "bor" || op == "bxor" || op == "bshl" || op == "bshr") {
            if ((lt != ThavaType::INT && lt != ThavaType::UNKNOWN) || (rt != ThavaType::INT && rt != ThavaType::UNKNOWN)) {
                std::cerr << "[Static Type Error]: Bitwise operator '" << op << "' only applies to integer <int>!\n";
                exit(1);
            }
            return ThavaType::INT;
        }

        // Check string operations
        if (op == "+") {
            if (lt == ThavaType::STRING && rt == ThavaType::STRING) return ThavaType::STRING;
            if (lt == ThavaType::STRING || rt == ThavaType::STRING) {
                if (lt != ThavaType::UNKNOWN && rt != ThavaType::UNKNOWN) {
                    std::cerr << "[Static Type Error]: Operator '+' cannot be applied between <str> and non-string type!\n"
                              << "Hint: Cast to <str> before concatenating.\n";
                    exit(1);
                }
                return ThavaType::STRING;
            }
        } else if (op == "*") {
            if ((lt == ThavaType::STRING && rt == ThavaType::INT) || (lt == ThavaType::INT && rt == ThavaType::STRING)) {
                return ThavaType::STRING;
            }
            if (lt == ThavaType::STRING || rt == ThavaType::STRING) {
                if (lt != ThavaType::UNKNOWN && rt != ThavaType::UNKNOWN) {
                    std::cerr << "[Static Type Error]: String multiplication '*' only accepts <str> and integer <int>!\n";
                    exit(1);
                }
            }
        } else if (op == "-" || op == "/" || op == "//" || op == "%") {
            if (lt == ThavaType::STRING || rt == ThavaType::STRING) {
                if (lt != ThavaType::UNKNOWN && rt != ThavaType::UNKNOWN) {
                    std::cerr << "[Static Type Error]: Operator '" << op << "' is not permitted on string <str>!\n";
                    exit(1);
                }
            }
        }

        if (op == "==" || op == "!=" || op == "and" || op == "or" ||
            op == "<" || op == "<=" || op == ">" || op == ">=") {
            if ((lt == ThavaType::STRING && rt != ThavaType::STRING && rt != ThavaType::UNKNOWN && rt != ThavaType::NONE) ||
                (rt == ThavaType::STRING && lt != ThavaType::STRING && lt != ThavaType::UNKNOWN && lt != ThavaType::NONE)) {
                std::cerr << "[Static Type Error]: Cannot compare string <str> with different data type!\n";
                exit(1);
            }
            return ThavaType::BOOL;
        }

        if (op == "%" || op == "//") return ThavaType::INT;
        if (lt == ThavaType::DOUBLE || rt == ThavaType::DOUBLE) return ThavaType::DOUBLE;
        if (lt == ThavaType::FLOAT || rt == ThavaType::FLOAT) return ThavaType::FLOAT;
        return ThavaType::INT;
    }
};

class CollectionNode : public ASTNode {
    std::vector<ASTPtr> elements;
    bool isTuple;
public:
    CollectionNode(std::vector<ASTPtr> el, bool tuple = false) : elements(std::move(el)), isTuple(tuple) {}
    std::vector<ASTPtr>& getElements() { return elements; }
    bool getIsTuple() const { return isTuple; }
    std::string generateCode() override {
        std::string res = "std::vector<ThavaValue>{";
        for (size_t i = 0; i < elements.size(); ++i) {
            res += elements[i]->generateCode();
            if (i + 1 < elements.size()) res += ", ";
        }
        res += "}";
        return res;
    }
    ThavaType getType(std::shared_ptr<Environment>) override {
        return isTuple ? ThavaType::TUPLE : ThavaType::LIST;
    }
};

class DictNode : public ASTNode {
public:
    std::vector<std::pair<ASTPtr, ASTPtr>> entries;
    DictNode(std::vector<std::pair<ASTPtr, ASTPtr>> e = {}) : entries(std::move(e)) {}

    std::string generateCode() override {
        std::string res = "ThavaDict({";
        for (size_t i = 0; i < entries.size(); ++i) {
            res += "{" + entries[i].first->generateCode() + ", " + entries[i].second->generateCode() + "}";
            if (i + 1 < entries.size()) res += ", ";
        }
        res += "})";
        return res;
    }

    ThavaType getType(std::shared_ptr<Environment>) override {
        return ThavaType::DICT;
    }
};

// Thava is function-scoped, so the C++ backend must declare every local at the top
// of its function instead of inside the block where it appears. Names collected
// here are emitted by thavaCppBodyCode() and their declaration site becomes an
// assignment.
static std::unordered_set<std::string> g_cppHoisted;
std::string thavaCppBodyCode(const std::vector<ASTPtr>& body, const std::string& indent,
                             const std::vector<std::pair<ThavaType, std::string>>& params);

// Every top-level variable declaration, with its type, made visible to functions.
static std::map<std::string, std::pair<ThavaType, std::string>> g_globalVars;
static std::shared_ptr<Environment> g_programEnv;

// Is this name declared by the local scope chain (parameter/local variable) rather
// than being one of the program's top-level variables?
static bool declaredInLocalScope(std::shared_ptr<Environment> env, const std::string& name) {
    for (auto e = env; e; e = e->parent) {
        if (g_programEnv && e.get() == g_programEnv.get()) break;
        if (e->variables.count(name)) return true;
    }
    return false;
}

static bool canWriteGlobal(std::shared_ptr<Environment> env, const std::string& name) {
    for (auto scope = env; scope && scope != g_programEnv; scope = scope->parent)
        if (scope->writableGlobals.count(name)) return true;
    return false;
}

static void rejectConstWrite(const Environment& env, const std::string& name) {
    if (!env.isConst(name)) return;
    std::cerr << "[Static Type Error]: Cannot modify const variable '" << name << "'.\n";
    exit(1);
}

static void rejectVariableWrite(std::shared_ptr<Environment> env, const std::string& name,
                                const std::string& where) {
    rejectConstWrite(*env, name);
    if (where.empty() || !g_globalVars.count(name) || declaredInLocalScope(env, name) ||
        canWriteGlobal(env, name)) return;
    std::cerr << "[Static Type Error]: Cannot modify global variable '" << name
              << "' inside '" << where << "'. Declare global <type> " << name << " first.\n";
    exit(1);
}

class GlobalDeclNode : public ASTNode {
public:
    ThavaType type;
    std::string customType, name;
    GlobalDeclNode(ThavaType t, std::string ct, std::string n)
        : type(t), customType(std::move(ct)), name(std::move(n)) {}
    std::string generateCode() override { return ""; }
    ThavaType getType(std::shared_ptr<Environment>) override { return ThavaType::VOID; }
};

class VarDeclNode : public ASTNode {
public:
    ThavaType varType;
    std::string customTypeName;
    std::string varName;
    ASTPtr initExpr;
    bool isConst;

    VarDeclNode(ThavaType t, std::string n, ASTPtr expr, std::string ct = "", bool constant = false)
        : varType(t), customTypeName(std::move(ct)), varName(std::move(n)), initExpr(std::move(expr)), isConst(constant) {}

    std::string cppTypeName() const {
        switch (varType) {
            case ThavaType::INT: return cppIntName(customTypeName);
            case ThavaType::FLOAT: return "float";
            case ThavaType::DOUBLE: return "double";
            case ThavaType::BOOL: return "bool";
            case ThavaType::STRING: return "std::string";
            case ThavaType::LIST:
            case ThavaType::TUPLE:
            case ThavaType::MULTYPE: return "std::vector<ThavaValue>";
            case ThavaType::DICT: return "ThavaDict";
            case ThavaType::CUSTOM:
                if (g_structs.count(customTypeName) || g_enums.count(customTypeName)) {
                    return customTypeName;
                }
                return "thava_class_" + customTypeName;
            default: return "auto";
        }
    }

    std::string generateCode() override {
        if (cppIsGlobalRef(varName) || g_cppHoisted.count(varName)) {
            if (initExpr) return cppVarRef(varName) + " = " + initExpr->generateCode() + ";";
            return std::string();
        }
        std::string cppType;
        switch (varType) {
            case ThavaType::INT: cppType = cppIntName(customTypeName); break;
            case ThavaType::FLOAT: cppType = "float"; break;
            case ThavaType::DOUBLE: cppType = "double"; break;
            case ThavaType::BOOL: cppType = "bool"; break;
            case ThavaType::STRING: cppType = "std::string"; break;
            case ThavaType::LIST: 
            case ThavaType::TUPLE:
            case ThavaType::MULTYPE: cppType = "std::vector<ThavaValue>"; break;
            case ThavaType::DICT: cppType = "ThavaDict"; break;
            case ThavaType::CUSTOM:
                if (g_structs.count(customTypeName) || g_enums.count(customTypeName)) {
                    cppType = customTypeName;
                } else {
                    cppType = "thava_class_" + customTypeName;
                }
                break;
            default: cppType = "auto";
        }
        if (initExpr) {
            return cppType + " " + varName + " = " + initExpr->generateCode() + ";";
        }
        return cppType + " " + varName + ";";
    }

    ThavaType getType(std::shared_ptr<Environment>) override { return varType; }
};

class AssignNode : public ASTNode {
public:
    std::string name;
    ASTPtr expr;
    std::string op = "=";

    AssignNode(std::string n, ASTPtr e, std::string o = "=") 
        : name(std::move(n)), expr(std::move(e)), op(std::move(o)) {}

    std::string generateCode() override {
        const std::string n = cppVarRef(name);
        if (op == "=") return n + " = " + expr->generateCode() + ";";
        // Expanded form: the right-hand side may be a ThavaValue (e.g. a list
        // element), and both `lhs += rhs` and `lhs = lhs + rhs` need the mixed
        // ThavaValue/number operators to resolve the element's type.
        if (op == "+=") return n + " = " + n + " + " + expr->generateCode() + ";";
        if (op == "-=") return n + " = " + n + " - " + expr->generateCode() + ";";
        if (op == "*=") return n + " = " + n + " * " + expr->generateCode() + ";";
        if (op == "/=") return n + " = _thava_div(" + n + ", " + expr->generateCode() + ");";
        if (op == "%=") return n + " = _thava_mod(" + n + ", " + expr->generateCode() + ");";
        if (op == "**=") return n + " = _thava_pow(" + n + ", " + expr->generateCode() + ");";
        return n + " " + op + " " + expr->generateCode() + ";";
    }
    ThavaType getType(std::shared_ptr<Environment>) override {
        return ThavaType::UNKNOWN;
    }
};

class BlockNode : public ASTNode {
public:
    std::vector<ASTPtr> statements;

    BlockNode(std::vector<ASTPtr> stmts) : statements(std::move(stmts)) {}

    std::string generateCode() override {
        std::string code = "{\n";
        for (const auto& stmt : statements) {
            code += "    " + stmt->generateCode() + "\n";
        }
        code += "}";
        return code;
    }
    ThavaType getType(std::shared_ptr<Environment>) override {
        return ThavaType::UNKNOWN;
    }
};

class IfNode : public ASTNode {
public:
    ASTPtr cond;
    ASTPtr thenBranch;
    ASTPtr elseBranch;

    IfNode(ASTPtr c, ASTPtr t, ASTPtr e) 
        : cond(std::move(c)), thenBranch(std::move(t)), elseBranch(std::move(e)) {}

    std::string generateCode() override {
        std::string code = "if (" + cond->generateCode() + ") " + thenBranch->generateCode();
        if (elseBranch) {
            code += " else " + elseBranch->generateCode();
        }
        return code;
    }
    ThavaType getType(std::shared_ptr<Environment>) override {
        return ThavaType::UNKNOWN;
    }
};

class WhileNode : public ASTNode {
public:
    ASTPtr cond;
    ASTPtr body;

    WhileNode(ASTPtr c, ASTPtr b) : cond(std::move(c)), body(std::move(b)) {}

    std::string generateCode() override {
        return "while (" + cond->generateCode() + ") " + body->generateCode();
    }
    ThavaType getType(std::shared_ptr<Environment>) override {
        return ThavaType::UNKNOWN;
    }
};

// AST node for for loop: for <type> ten = kh?i_t?o, dieu_kien, buoc_nhay { than_vong_lap }
class ForNode : public ASTNode {
public:
    ASTPtr init;   // VarDeclNode (loop counter, scoped to the loop)
    ASTPtr cond;   // Loop condition expression, must be <bl>
    ASTPtr update; // AssignNode hoac ExpressionStatementNode (buoc nhay)
    ASTPtr body;

    ForNode(ASTPtr i, ASTPtr c, ASTPtr u, ASTPtr b)
        : init(std::move(i)), cond(std::move(c)), update(std::move(u)), body(std::move(b)) {}

    std::string generateCode() override {
        std::string initCode = init->generateCode();     // Da co dau ';' o cuoi (VarDeclNode)
        std::string updateCode = update->generateCode();  // Contains trailing ';', strip for loop header
        if (!updateCode.empty() && updateCode.back() == ';') updateCode.pop_back();
        return "for (" + initCode + " " + cond->generateCode() + "; " + updateCode + ") " + body->generateCode();
    }

    ThavaType getType(std::shared_ptr<Environment>) override {
        return ThavaType::UNKNOWN;
    }
};

// AST node for Python-style range loop: for <type> name in range(start, stop, step) { }
// Dynamically chooses step direction (increment/decrement) at runtime based on step sign.
class RangeForNode : public ASTNode {
public:
    ThavaType varType;
    std::string varName;
    ASTPtr startExpr, stopExpr, stepExpr;
    ASTPtr body;

    RangeForNode(ThavaType vt, std::string vn, ASTPtr s, ASTPtr e, ASTPtr st, ASTPtr b)
        : varType(vt), varName(std::move(vn)), startExpr(std::move(s)), stopExpr(std::move(e)),
          stepExpr(std::move(st)), body(std::move(b)) {}

    std::string generateCode() override {
        std::string cppType;
        switch (varType) {
            case ThavaType::FLOAT: cppType = "float"; break;
            case ThavaType::DOUBLE: cppType = "double"; break;
            default: cppType = "long long"; break; // range() applies to numeric types, default INT
        }
        std::string stepVar = "_thava_step_" + varName;
        return "for (" + cppType + " " + varName + " = " + startExpr->generateCode() +
               ", " + stepVar + " = " + stepExpr->generateCode() + "; " +
               "(" + stepVar + " > 0) ? (" + varName + " < " + stopExpr->generateCode() +
               ") : (" + varName + " > " + stopExpr->generateCode() + "); " +
               varName + " += " + stepVar + ") " + body->generateCode();
    }

    ThavaType getType(std::shared_ptr<Environment>) override { return ThavaType::UNKNOWN; }
};

// AST node for break statement in loops
class BreakNode : public ASTNode {
public:
    std::string generateCode() override { return "break;"; }
    ThavaType getType(std::shared_ptr<Environment>) override { return ThavaType::VOID; }
};

// AST node for continue statement in loops
class ContinueNode : public ASTNode {
public:
    std::string generateCode() override { return "continue;"; }
    ThavaType getType(std::shared_ptr<Environment>) override { return ThavaType::VOID; }
};

// AST node for Foreach loop: for [<type>] name in collectionExpr { }
class ForEachNode : public ASTNode {
public:
    ThavaType varType;
    std::string varName;
    ASTPtr collectionExpr;
    ASTPtr body;

    ForEachNode(ThavaType vt, std::string vn, ASTPtr coll, ASTPtr b)
        : varType(vt), varName(std::move(vn)), collectionExpr(std::move(coll)), body(std::move(b)) {}

    std::string generateCode() override {
        std::string cppType;
        std::string extract;
        switch (varType) {
            case ThavaType::INT: cppType = "long long"; extract = "_thava_to_int(_thava_item)"; break;
            case ThavaType::FLOAT: cppType = "float"; extract = "_thava_to_float(_thava_item)"; break;
            case ThavaType::DOUBLE: cppType = "double"; extract = "_thava_to_double(_thava_item)"; break;
            case ThavaType::BOOL: cppType = "bool"; extract = "_thava_to_bool(_thava_item)"; break;
            case ThavaType::STRING: cppType = "std::string"; extract = "_thava_to_string(_thava_item)"; break;
            case ThavaType::LIST: cppType = "std::vector<ThavaValue>"; extract = "(std::vector<ThavaValue>)_thava_item"; break;
            default: cppType = "ThavaValue"; extract = "_thava_item"; break;
        }
        std::string code = "{\n";
        code += "    auto _thava_raw_coll = " + collectionExpr->generateCode() + ";\n";
        code += "    for (const auto& _thava_item : _thava_to_iterable(_thava_raw_coll)) {\n";
        code += "        " + cppType + " " + varName + " = " + extract + ";\n";
        code += "        " + body->generateCode() + "\n";
        code += "    }\n";
        code += "}\n";
        return code;
    }

    ThavaType getType(std::shared_ptr<Environment> env) override {
        if (collectionExpr) collectionExpr->getType(env);
        if (body) body->getType(env);
        return ThavaType::VOID;
    }
};

// AST node for forward function declaration (signature only, no body)
// e.g. <int>fdfunc calc(<int>a, <int>b) - enables mutual recursion
class ForwardDeclNode : public ASTNode {
public:
    ThavaType returnType;
    std::string customReturnType;
    std::string name;
    std::vector<std::pair<ThavaType, std::string>> params;
    std::vector<std::string> paramCustomTypes;

    ForwardDeclNode(ThavaType rt, std::string n, std::vector<std::pair<ThavaType, std::string>> p, std::vector<std::string> pct = {}, std::string crt = "")
        : returnType(rt), customReturnType(std::move(crt)), name(std::move(n)), params(std::move(p)), paramCustomTypes(std::move(pct)) {}

    std::string generateCode() override {
        std::string retTypeStr = cppTypeNameFor(returnType, customReturnType);
        std::string code = retTypeStr + " thava_func_" + name + "(";
        for (size_t i = 0; i < params.size(); ++i) {
            std::string ctype = (i < paramCustomTypes.size()) ? paramCustomTypes[i] : "";
            code += cppParamTypeNameFor(params[i].first, ctype);
            if (!params[i].second.empty()) code += " " + params[i].second;
            if (i + 1 < params.size()) code += ", ";
        }
        code += ");\n";
        return code;
    }

    ThavaType getType(std::shared_ptr<Environment>) override { return ThavaType::UNKNOWN; }
};

// AST node for cfunc declaration: C function signature in Thava type checker
class CFuncDeclNode : public ASTNode {
public:
    ThavaType returnType;
    std::string name;
    std::vector<std::pair<ThavaType, std::string>> params;
    CFuncDeclNode(ThavaType rt, std::string n, std::vector<std::pair<ThavaType, std::string>> p)
        : returnType(rt), name(std::move(n)), params(std::move(p)) {}

    std::string generateCode() override {
        return "// [cfunc] " + name + "\n";
    }

    ThavaType getType(std::shared_ptr<Environment>) override { return returnType; }
};

// AST node for direct C function call via namespace: c.<func_name>(arg1, arg2, ...)
class CCallNode : public ASTNode {
public:
    std::string funcName;
    std::vector<ASTPtr> args;
    CCallNode(std::string name, std::vector<ASTPtr> a)
        : funcName(std::move(name)), args(std::move(a)) {}

    std::string generateCode() override {
        std::string code = funcName + "(";
        for (size_t i = 0; i < args.size(); ++i) {
            code += "_thava_c_arg(" + args[i]->generateCode() + ")";
            if (i + 1 < args.size()) code += ", ";
        }
        code += ")";
        return code;
    }

    ThavaType getType(std::shared_ptr<Environment>) override { return ThavaType::UNKNOWN; }
};

// AST node for C constant/variable access via namespace: c.<const_name>
class CVarNode : public ASTNode {
public:
    std::string varName;
    CVarNode(std::string name) : varName(std::move(name)) {}

    std::string generateCode() override {
        return varName;
    }

    ThavaType getType(std::shared_ptr<Environment>) override { return ThavaType::UNKNOWN; }
};

// AST node for 'in' membership check in list/tuple (element-wise comparison)
class InNode : public ASTNode {
public:
    ASTPtr left;
    ASTPtr listExpr;
    InNode(ASTPtr l, ASTPtr r) : left(std::move(l)), listExpr(std::move(r)) {}

    std::string generateCode() override {
        return "_thava_value_in(ThavaValue(" + left->generateCode() + "), " + listExpr->generateCode() + ")";
    }

    ThavaType getType(std::shared_ptr<Environment>) override { return ThavaType::BOOL; }
};

class ConsClearNode : public ASTNode {
public:
    std::string generateCode() override {
        return "std::cout << \"\\033[2J\\033[H\" << std::flush;\n";
    }

    ThavaType getType(std::shared_ptr<Environment>) override { return ThavaType::VOID; }
};

class ConsOutNode : public ASTNode {
    ASTPtr expr;
public:
    ConsOutNode(ASTPtr e) : expr(std::move(e)) {}

    ASTNode* getExpr() const { return expr.get(); }

    std::string generateCode() override {
        return "std::cout << " + expr->generateCode() + " << std::endl;";
    }

    ThavaType getType(std::shared_ptr<Environment>) override {
        return ThavaType::UNKNOWN; 
    }
};

// Reads exactly one line from std::cin into the local `_l`.
// Never leaves std::cin in a failed state (so one bad/blank/EOF read cannot
// poison every later key.input call), and strips the trailing CR of CRLF input.
inline std::string thavaCppReadLineStmt() {
    return "std::string _l = _thava_read_console_line(); ";
}

class InputNode : public ASTNode {
public:
    ThavaType inputType;
    ASTPtr prompt;
    InputNode(ThavaType t, ASTPtr p) : inputType(t), prompt(std::move(p)) {}

    std::string generateCode() override {
        std::string cppType;
        std::string convert;

        if (inputType == ThavaType::INT) {
            cppType = "long long";
            convert = "long long val = 0; try { val = (long long)std::stoll(_l); } catch (...) { val = 0; } return val;";
        } else if (inputType == ThavaType::FLOAT) {
            cppType = "float";
            convert = "float val = 0.0f; try { val = (float)std::stod(_l); } catch (...) { val = 0.0f; } return val;";
        } else if (inputType == ThavaType::DOUBLE) {
            cppType = "double";
            convert = "double val = 0.0; try { val = std::stod(_l); } catch (...) { val = 0.0; } return val;";
        } else if (inputType == ThavaType::BOOL) {
            cppType = "bool";
            convert = "bool val = (_l == \"true\" || _l == \"True\" || _l == \"TRUE\" || "
                      "_l == \"1\" || _l == \"yes\"); return val;";
        } else {
            cppType = "std::string";
            convert = "return _l;";
        }

        return "[&]() -> " + cppType + " { std::cout << " + prompt->generateCode() +
               " << std::flush; " + thavaCppReadLineStmt() + convert + " }()";
    }

    ThavaType getType(std::shared_ptr<Environment>) override {
        return inputType;
    }
};

// AST node for key.input.upper / key.input.lower - specialized for <str>
class InputCaseNode : public ASTNode {
public:
    ASTPtr prompt;
    std::string mode; // "upper" | "lower" | "title"
    InputCaseNode(ASTPtr p, std::string m) : prompt(std::move(p)), mode(std::move(m)) {}

    std::string generateCode() override {
        std::string helper = "_thava_str_" + mode;
        return helper + "([&]() -> std::string { std::cout << " + prompt->generateCode() +
               " << std::flush; " + thavaCppReadLineStmt() + "return _l; }())";
    }

    ThavaType getType(std::shared_ptr<Environment>) override { return ThavaType::STRING; }
};

// AST node for string methods: .lower(), .upper(), .title(), .strip()
// String methods accept any arbitrary expression as base (not limited to identifiers),
// thus enabling method chaining: a.strip().title()
class StrMethodNode : public ASTNode {
public:
    ASTPtr base;
    std::string method;
    StrMethodNode(ASTPtr b, std::string m) : base(std::move(b)), method(std::move(m)) {}

    std::string generateCode() override {
        std::string arg = base->generateCode();
        if (method == "lower") return "_thava_str_lower(" + arg + ")";
        if (method == "upper") return "_thava_str_upper(" + arg + ")";
        if (method == "title") return "_thava_str_title(" + arg + ")";
        if (method == "strip") return "_thava_str_strip(" + arg + ")";
        std::cerr << "Error: Unknown string method: " << method << "\n";
        exit(1);
    }

    ThavaType getType(std::shared_ptr<Environment>) override { return ThavaType::STRING; }
};

// AST node for advanced string operations:
// .replace(old, new), .find(sub), .index(sub), .startswith(prefix), .endswith(suffix),
// .count(sub), .join(list), .split(delim)
class StrAdvMethodNode : public ASTNode {
public:
    ASTPtr base;
    std::string method;
    std::vector<ASTPtr> args;
    int line = 0;

    StrAdvMethodNode(ASTPtr b, std::string m, std::vector<ASTPtr> a, int l = 0)
        : base(std::move(b)), method(std::move(m)), args(std::move(a)), line(l) {}

    std::string generateCode() override {
        std::string baseCode = base->generateCode();
        if (method == "replace") {
            if (args.size() != 2) {
                std::cerr << "Compilation Error: .replace(old, new) requires exactly 2 arguments at line " << line << "\n";
                exit(1);
            }
            return "_thava_str_replace(" + baseCode + ", " + args[0]->generateCode() + ", " + args[1]->generateCode() + ")";
        }
        if (method == "find" || method == "index") {
            if (args.size() != 1) {
                std::cerr << "Compilation Error: ." << method << "(sub) requires exactly 1 argument at line " << line << "\n";
                exit(1);
            }
            return "_thava_str_find(" + baseCode + ", " + args[0]->generateCode() + ")";
        }
        if (method == "startswith") {
            if (args.size() != 1) {
                std::cerr << "Compilation Error: .startswith(prefix) requires exactly 1 argument at line " << line << "\n";
                exit(1);
            }
            return "_thava_str_startswith(" + baseCode + ", " + args[0]->generateCode() + ")";
        }
        if (method == "endswith") {
            if (args.size() != 1) {
                std::cerr << "Compilation Error: .endswith(suffix) requires exactly 1 argument at line " << line << "\n";
                exit(1);
            }
            return "_thava_str_endswith(" + baseCode + ", " + args[0]->generateCode() + ")";
        }
        if (method == "count") {
            if (args.size() != 1) {
                std::cerr << "Compilation Error: .count(sub) requires exactly 1 argument at line " << line << "\n";
                exit(1);
            }
            return "_thava_str_count(" + baseCode + ", " + args[0]->generateCode() + ")";
        }
        if (method == "join") {
            if (args.size() != 1) {
                std::cerr << "Compilation Error: .join(list) requires exactly 1 argument at line " << line << "\n";
                exit(1);
            }
            return "_thava_str_join(" + baseCode + ", " + args[0]->generateCode() + ")";
        }
        if (method == "split") {
            std::string delimCode = args.empty() ? "std::string(\" \")" : args[0]->generateCode();
            return "_thava_str_split_all(" + baseCode + ", " + delimCode + ")";
        }
        std::cerr << "Compilation Error: Unknown string method: " << method << " at line " << line << "\n";
        exit(1);
    }

    ThavaType getType(std::shared_ptr<Environment> env) override {
        if (base) base->getType(env);
        for (auto& a : args) if (a) a->getType(env);
        if (method == "replace" || method == "join") return ThavaType::STRING;
        if (method == "find" || method == "index" || method == "count") return ThavaType::INT;
        if (method == "startswith" || method == "endswith") return ThavaType::BOOL;
        if (method == "split") return ThavaType::LIST;
        return ThavaType::UNKNOWN;
    }
};

// AST node for expr.split(delim, [a]) or .split(delim, [a, b]) - substring splitting/indexing
// Base is an arbitrary expression (supports chaining)
class StrSplitNode : public ASTNode {
public:
    ASTPtr base;
    ASTPtr delimExpr;
    std::string idxACode;
    std::string idxBCode; // Identical to idxACode when only one index [a] is supplied
    StrSplitNode(ASTPtr b, ASTPtr d, std::string a, std::string bc)
        : base(std::move(b)), delimExpr(std::move(d)), idxACode(std::move(a)), idxBCode(std::move(bc)) {}

    std::string generateCode() override {
        return "_thava_str_split(" + base->generateCode() + ", " + delimExpr->generateCode() + ", " + idxACode + ", " + idxBCode + ")";
    }

    ThavaType getType(std::shared_ptr<Environment>) override { return ThavaType::STRING; }
};

// AST node for expr.getsubstring(start, end) - 1-based character substring, UTF-8 safe
// Base is an arbitrary expression (supports chaining)
class StrSubstringNode : public ASTNode {
public:
    ASTPtr base;
    ASTPtr startExpr, endExpr;
    StrSubstringNode(ASTPtr b, ASTPtr s, ASTPtr e)
        : base(std::move(b)), startExpr(std::move(s)), endExpr(std::move(e)) {}

    std::string generateCode() override {
        return "_thava_str_getsubstring(" + base->generateCode() + ", " + startExpr->generateCode() + ", " + endExpr->generateCode() + ")";
    }

    ThavaType getType(std::shared_ptr<Environment>) override { return ThavaType::STRING; }
};

// AST node for list/tuple indexing, supports negative indices (a[-1] = last element)
class IndexNode : public ASTNode {
public:
    ASTPtr base;
    ASTPtr indexExpr;
    IndexNode(ASTPtr b, ASTPtr i) : base(std::move(b)), indexExpr(std::move(i)) {}

    std::string generateCode() override {
        return "_thava_index(" + base->generateCode() + ", " + indexExpr->generateCode() + ")";
    }

    ThavaType getType(std::shared_ptr<Environment>) override { return ThavaType::UNKNOWN; }
};

// AST node for array index assignment: arr[i] = expr
class IndexAssignNode : public ASTNode {
public:
    std::string varName;
    ASTPtr indexExpr;
    ASTPtr valExpr;
    std::string op = "=";

    IndexAssignNode(std::string v, ASTPtr i, ASTPtr val, std::string o = "=")
        : varName(std::move(v)), indexExpr(std::move(i)), valExpr(std::move(val)), op(std::move(o)) {}

    std::string generateCode() override {
        const std::string varName = cppVarRef(this->varName);
        if (op == "=") {
            return "_thava_list_assign(" + varName + ", " + indexExpr->generateCode() + ", " + valExpr->generateCode() + ");";
        }
        if (op == "+=") {
            return "_thava_list_assign(" + varName + ", " + indexExpr->generateCode() + ", _thava_add(_thava_index(" + varName + ", " + indexExpr->generateCode() + "), " + valExpr->generateCode() + "));";
        }
        if (op == "-=") {
            return "_thava_list_assign(" + varName + ", " + indexExpr->generateCode() + ", _thava_sub(_thava_index(" + varName + ", " + indexExpr->generateCode() + "), " + valExpr->generateCode() + "));";
        }
        if (op == "*=") {
            return "_thava_list_assign(" + varName + ", " + indexExpr->generateCode() + ", _thava_mul(_thava_index(" + varName + ", " + indexExpr->generateCode() + "), " + valExpr->generateCode() + "));";
        }
        if (op == "/=") {
            return "_thava_list_assign(" + varName + ", " + indexExpr->generateCode() + ", _thava_div(_thava_index(" + varName + ", " + indexExpr->generateCode() + "), " + valExpr->generateCode() + "));";
        }
        if (op == "%=") {
            return "_thava_list_assign(" + varName + ", " + indexExpr->generateCode() + ", _thava_mod(_thava_index(" + varName + ", " + indexExpr->generateCode() + "), " + valExpr->generateCode() + "));";
        }
        if (op == "**=") {
            return "_thava_list_assign(" + varName + ", " + indexExpr->generateCode() + ", _thava_pow(_thava_index(" + varName + ", " + indexExpr->generateCode() + "), " + valExpr->generateCode() + "));";
        }
        return "_thava_list_assign(" + varName + ", " + indexExpr->generateCode() + ", " + valExpr->generateCode() + ");";
    }

    ThavaType getType(std::shared_ptr<Environment>) override { return ThavaType::INT; }
};

// AST node for collection length via len(...) or .len() / .size()
class LenNode : public ASTNode {
public:
    ASTPtr arg;
    LenNode(ASTPtr a) : arg(std::move(a)) {}

    std::string generateCode() override {
        return "_thava_len(" + arg->generateCode() + ")";
    }

    ThavaType getType(std::shared_ptr<Environment> env) override {
        if (arg) arg->getType(env);
        return ThavaType::INT;
    }
};

// AST node for scoped file block: file.open(path, [mode]) as f { ... }
class FileOpenBlockNode : public ASTNode {
public:
    ASTPtr pathExpr;
    ASTPtr modeExpr;
    std::string varName;
    std::vector<ASTPtr> body;

    FileOpenBlockNode(ASTPtr p, ASTPtr m, std::string v, std::vector<ASTPtr> b)
        : pathExpr(std::move(p)), modeExpr(std::move(m)), varName(std::move(v)), body(std::move(b)) {}

    std::string generateCode() override {
        std::string modeCode = modeExpr ? modeExpr->generateCode() : "std::string(\"r\")";
        std::string code = "{\n";
        code += "    _ThavaFileStream " + varName + "(" + pathExpr->generateCode() + ", " + modeCode + ");\n";
        for (const auto& stmt : body) {
            code += "    " + stmt->generateCode() + "\n";
        }
        code += "}\n";
        return code;
    }

    ThavaType getType(std::shared_ptr<Environment>) override {
        return ThavaType::VOID;
    }
};

// AST node for file stream methods within file.open block:
// f.read(), f.read_line(), f.read_lines(), f.write(s), f.write_line(s)
class FileMethodNode : public ASTNode {
public:
    std::string varName;
    std::string method;
    std::vector<ASTPtr> args;

    FileMethodNode(std::string v, std::string m, std::vector<ASTPtr> a)
        : varName(std::move(v)), method(std::move(m)), args(std::move(a)) {}

    std::string generateCode() override {
        std::string actualMethod = method;
        if (method == "readline") actualMethod = "read_line";
        else if (method == "readlines") actualMethod = "read_lines";
        else if (method == "writeline") actualMethod = "write_line";

        std::string code = varName + "." + actualMethod + "(";
        for (size_t i = 0; i < args.size(); ++i) {
            code += args[i]->generateCode();
            if (i + 1 < args.size()) code += ", ";
        }
        code += ")";
        return code;
    }

    ThavaType getType(std::shared_ptr<Environment> env) override {
        if (!env->count(varName) || env->get(varName) != ThavaType::FILE) {
            std::cerr << "[Static Type Error]: Variable '" << varName << "' is not a valid file handle!\n";
            exit(1);
        }
        for (auto& a : args) a->getType(env);

        if (method == "read" || method == "read_line" || method == "readline") {
            return ThavaType::STRING;
        }
        if (method == "read_lines" || method == "readlines") {
            return ThavaType::LIST;
        }
        return ThavaType::VOID;
    }
};

// Clause structure for except blocks: except [ExceptionType] [as varName] { body }
struct ExceptClause {
    std::string exceptionType; // vd: "RuntimeError", "Exception", hoac ""
    std::string varName;       // vd: "e", hoac ""
    std::vector<ASTPtr> body;
};

// AST node for try { ... } except ... { ... } block
class TryExceptNode : public ASTNode {
public:
    std::vector<ASTPtr> tryBody;
    std::vector<ExceptClause> exceptClauses;

    TryExceptNode(std::vector<ASTPtr> tBody, std::vector<ExceptClause> eClauses)
        : tryBody(std::move(tBody)), exceptClauses(std::move(eClauses)) {}

    std::string generateCode() override {
        std::string code = "{\n    try {\n";
        for (const auto& stmt : tryBody) {
            code += "        " + stmt->generateCode() + "\n";
        }
        code += "    }\n";
        code += "    catch (const ThavaException& _thava_ex) {\n";
        bool hasCatchAll = false;
        bool hasIf = false;
        for (size_t i = 0; i < exceptClauses.size(); ++i) {
            const auto& clause = exceptClauses[i];
            bool isCatchAll = clause.exceptionType.empty() || clause.exceptionType == "Exception" || clause.exceptionType == "RuntimeError";

            code += "        ";
            if (i > 0) code += "else ";

            if (isCatchAll) {
                code += "{\n";
                hasCatchAll = true;
            } else {
                code += "if (_thava_ex.typeName == \"" + clause.exceptionType + "\") {\n";
                hasIf = true;
            }
            if (!clause.varName.empty()) {
                code += "            std::string " + clause.varName + " = _thava_ex.what();\n";
            }
            for (const auto& stmt : clause.body) {
                code += "            " + stmt->generateCode() + "\n";
            }
            code += "        }\n";
            if (isCatchAll) break;
        }
        if (!hasCatchAll && hasIf) {
            code += "        else { throw; }\n";
        }
        code += "    }\n";

        // Catch standard C++ exceptions that are not ThavaException
        const ExceptClause* catchAllClause = nullptr;
        for (const auto& c : exceptClauses) {
            if (c.exceptionType.empty() || c.exceptionType == "Exception" || c.exceptionType == "RuntimeError") {
                catchAllClause = &c;
                break;
            }
        }
        if (catchAllClause) {
            code += "    catch (const std::exception& _thava_std_ex) {\n";
            if (!catchAllClause->varName.empty()) {
                code += "        std::string " + catchAllClause->varName + " = _thava_std_ex.what();\n";
            }
            for (const auto& stmt : catchAllClause->body) {
                code += "        " + stmt->generateCode() + "\n";
            }
            code += "    }\n";
        }
        code += "}";
        return code;
    }

    ThavaType getType(std::shared_ptr<Environment>) override {
        return ThavaType::UNKNOWN;
    }
};

// AST node for throw / raise statements
class ThrowNode : public ASTNode {
public:
    ASTPtr expr;
    ThrowNode(ASTPtr e) : expr(std::move(e)) {}

    std::string generateCode() override {
        if (expr) {
            return "throw ThavaException(_thava_to_string(" + expr->generateCode() + "));";
        }
        return "throw ThavaException(\"Runtime Error!\");";
    }

    ThavaType getType(std::shared_ptr<Environment> env) override {
        if (expr) expr->getType(env);
        return ThavaType::UNKNOWN;
    }
};

// AST node for raise_error(r|c, type, msg) statement
class RaiseErrorNode : public ASTNode {
public:
    bool isCompileTime;
    ASTPtr typeExpr;
    ASTPtr msgExpr;
    int line;

    RaiseErrorNode(bool isC, ASTPtr t, ASTPtr m, int l)
        : isCompileTime(isC), typeExpr(std::move(t)), msgExpr(std::move(m)), line(l) {}

    std::string generateCode() override {
        if (isCompileTime) {
            return ""; // No code generated since compilation halts immediately
        }
        std::string typeCode = typeExpr ? "_thava_to_string(" + typeExpr->generateCode() + ")" : "\"RuntimeError\"";
        std::string msgCode = msgExpr ? "_thava_to_string(" + msgExpr->generateCode() + ")" : "\"Runtime Error!\"";
        return "throw ThavaException(" + typeCode + ", " + msgCode + ");";
    }

    ThavaType getType(std::shared_ptr<Environment> env) override {
        if (typeExpr) typeExpr->getType(env);
        if (msgExpr) msgExpr->getType(env);
        return ThavaType::UNKNOWN;
    }
};

// AST node for list mutation methods: addval / removeval / assign / delval
class ListMethodNode : public ASTNode {
public:
    std::string varName;
    std::string method;
    std::vector<ASTPtr> args;

    // Extract index expression from [index] form (CollectionNode with 1 element)
    static std::string extractIndexExpr(ASTNode* node) {
        auto coll = dynamic_cast<CollectionNode*>(node);
        if (!coll || coll->getIsTuple() || coll->getElements().size() != 1) {
            std::cerr << "Parser Error: Index must be specified as [index]!\n";
            exit(1);
        }
        return coll->getElements()[0]->generateCode();
    }

public:
    ListMethodNode(std::string v, std::string m, std::vector<ASTPtr> a)
        : varName(std::move(v)), method(std::move(m)), args(std::move(a)) {}

    std::string generateCode() override {
        const std::string varName = cppVarRef(this->varName);
        if (method == "addval") {
            if (args.size() == 1) {
                return "_thava_list_addval(" + varName + ", " + args[0]->generateCode() + ")";
            } else if (args.size() == 2) {
                std::string idxCode = extractIndexExpr(args[0].get());
                return "_thava_list_addval_at(" + varName + ", " + idxCode + ", " + args[1]->generateCode() + ")";
            }
            std::cerr << "Error: addval() accepts 1 (value) or 2 ([index], value) arguments!\n";
            exit(1);
        } else if (method == "removeval") {
            if (args.size() != 1) { std::cerr << "Error: removeval() requires exactly 1 argument!\n"; exit(1); }
            auto coll = dynamic_cast<CollectionNode*>(args[0].get());
            if (coll && !coll->getIsTuple() && coll->getElements().size() == 1) {
                return "_thava_list_removeval_at(" + varName + ", " + coll->getElements()[0]->generateCode() + ")";
            }
            return "_thava_list_removeval_by_value(" + varName + ", " + args[0]->generateCode() + ")";
        } else if (method == "assign") {
            if (args.size() != 2) { std::cerr << "Error: assign() requires exactly 2 arguments ([index], value)!\n"; exit(1); }
            std::string idxCode = extractIndexExpr(args[0].get());
            return "_thava_list_assign(" + varName + ", " + idxCode + ", " + args[1]->generateCode() + ")";
        } else if (method == "delval") {
            if (!args.empty()) { std::cerr << "Error: delval() takes no arguments!\n"; exit(1); }
            return "_thava_list_delval(" + varName + ")";
        }
        std::cerr << "Error: Unknown list method: " << method << "\n";
        exit(1);
    }

    ThavaType getType(std::shared_ptr<Environment> env) override {
        if (env) {
            rejectVariableWrite(env, varName, env->functionName);
            for (auto& arg : args) arg->getType(env);
        }
        return ThavaType::UNKNOWN;
    }
};

// AST node for standard os library (OS interaction: Windows / Linux / MessageBox)
class OsMethodNode : public ASTNode {
public:
    std::string method;
    std::vector<ASTPtr> args;
    int line = 0;

    OsMethodNode(std::string m, std::vector<ASTPtr> a, int l = 0)
        : method(std::move(m)), args(std::move(a)), line(l) {}

    std::string generateCode() override {
        if (method == "name") {
            return "_thava_os_name()";
        } else if (method == "system") {
            if (args.empty()) { std::cerr << "Error: os.system() requires command argument at line " << line << "\n"; exit(1); }
            return "_thava_os_system(" + args[0]->generateCode() + ")";
        } else if (method == "getpid") {
            return "_thava_os_getpid()";
        } else if (method == "cpu_count") {
            return "_thava_os_cpu_count()";
        } else if (method == "getcwd") {
            return "_thava_os_getcwd()";
        } else if (method == "chdir") {
            if (args.empty()) { std::cerr << "Error: os.chdir() requires path argument at line " << line << "\n"; exit(1); }
            return "_thava_os_chdir(" + args[0]->generateCode() + ")";
        } else if (method == "mkdir") {
            if (args.empty()) { std::cerr << "Error: os.mkdir() requires path argument at line " << line << "\n"; exit(1); }
            return "_thava_os_mkdir(" + args[0]->generateCode() + ")";
        } else if (method == "rmdir") {
            if (args.empty()) { std::cerr << "Error: os.rmdir() requires path argument at line " << line << "\n"; exit(1); }
            return "_thava_os_rmdir(" + args[0]->generateCode() + ")";
        } else if (method == "remove") {
            if (args.empty()) { std::cerr << "Error: os.remove() requires path argument at line " << line << "\n"; exit(1); }
            return "_thava_os_remove(" + args[0]->generateCode() + ")";
        } else if (method == "rename") {
            if (args.size() < 2) { std::cerr << "Error: os.rename() requires 2 arguments (old, new) at line " << line << "\n"; exit(1); }
            return "_thava_os_rename(" + args[0]->generateCode() + ", " + args[1]->generateCode() + ")";
        } else if (method == "exists") {
            if (args.empty()) { std::cerr << "Error: os.exists() requires path argument at line " << line << "\n"; exit(1); }
            return "_thava_os_exists(" + args[0]->generateCode() + ")";
        } else if (method == "is_file") {
            if (args.empty()) { std::cerr << "Error: os.is_file() requires path argument at line " << line << "\n"; exit(1); }
            return "_thava_os_is_file(" + args[0]->generateCode() + ")";
        } else if (method == "is_dir") {
            if (args.empty()) { std::cerr << "Error: os.is_dir() requires path argument at line " << line << "\n"; exit(1); }
            return "_thava_os_is_dir(" + args[0]->generateCode() + ")";
        } else if (method == "getsize") {
            if (args.empty()) { std::cerr << "Error: os.getsize() requires path argument at line " << line << "\n"; exit(1); }
            return "_thava_os_getsize(" + args[0]->generateCode() + ")";
        } else if (method == "listdir") {
            std::string pathCode = args.empty() ? "\".\"" : args[0]->generateCode();
            return "_thava_os_listdir(" + pathCode + ")";
        } else if (method == "getenv") {
            if (args.empty()) { std::cerr << "Error: os.getenv() requires variable name argument at line " << line << "\n"; exit(1); }
            return "_thava_os_getenv(" + args[0]->generateCode() + ")";
        } else if (method == "setenv") {
            if (args.size() < 2) { std::cerr << "Error: os.setenv() requires 2 arguments (key, value) at line " << line << "\n"; exit(1); }
            return "_thava_os_setenv(" + args[0]->generateCode() + ", " + args[1]->generateCode() + ")";
        } else if (method == "sleep") {
            if (args.empty()) { std::cerr << "Error: os.sleep() requires ms argument at line " << line << "\n"; exit(1); }
            return "_thava_os_sleep(" + args[0]->generateCode() + ")";
        } else if (method == "showmessagebox") {
            if (args.size() < 4) {
                std::cerr << "Error: os.showmessagebox requires 4 arguments: (icon, title, content, buttons) at line " << line << "\n";
                exit(1);
            }
            return "_thava_os_showmessagebox(" + args[0]->generateCode() + ", " + args[1]->generateCode() + ", " + args[2]->generateCode() + ", " + args[3]->generateCode() + ")";
        }
        std::cerr << "Error: os method os." << method << " is not supported at line " << line << "\n";
        exit(1);
    }

    ThavaType getType(std::shared_ptr<Environment>) override {
        if (method == "name" || method == "getcwd" || method == "getenv") return ThavaType::STRING;
        if (method == "system" || method == "getpid" || method == "cpu_count" || method == "getsize" || method == "showmessagebox" || method == "sleep") return ThavaType::INT;
        if (method == "chdir" || method == "mkdir" || method == "rmdir" || method == "remove" || method == "rename" || method == "exists" || method == "is_file" || method == "is_dir" || method == "setenv") return ThavaType::BOOL;
        if (method == "listdir") return ThavaType::LIST;
        return ThavaType::UNKNOWN;
    }
};

// AST node for standard time library (datetime formatting, timestamps, sleep)
class TimeMethodNode : public ASTNode {
public:
    std::string method;
    std::vector<ASTPtr> args;
    int line = 0;

    TimeMethodNode(std::string m, std::vector<ASTPtr> a, int l = 0)
        : method(std::move(m)), args(std::move(a)), line(l) {}

    std::string generateCode() override {
        if (method == "now") {
            if (args.empty()) return "_thava_time_now()";
            return "_thava_time_now(" + args[0]->generateCode() + ")";
        } else if (method == "date") {
            if (args.empty()) return "_thava_time_date()";
            return "_thava_time_date(" + args[0]->generateCode() + ")";
        } else if (method == "year") {
            return "_thava_time_year()";
        } else if (method == "month") {
            return "_thava_time_month()";
        } else if (method == "day") {
            return "_thava_time_day()";
        } else if (method == "hour") {
            return "_thava_time_hour()";
        } else if (method == "minute") {
            return "_thava_time_minute()";
        } else if (method == "second") {
            return "_thava_time_second()";
        } else if (method == "millis") {
            return "_thava_time_millis()";
        } else if (method == "timestamp") {
            return "_thava_time_timestamp()";
        } else if (method == "sleep") {
            if (args.empty()) return "_thava_time_sleep(0)";
            return "_thava_time_sleep(" + args[0]->generateCode() + ")";
        }
        std::cerr << "Error: time method time." << method << " is not supported at line " << line << "\n";
        exit(1);
    }

    ThavaType getType(std::shared_ptr<Environment>) override {
        if (method == "now" || method == "date") return ThavaType::STRING;
        return ThavaType::INT;
    }
};

// AST node for clib library (Foreign Function Interface / Dynamic Library Interop)
class CLibMethodNode : public ASTNode {
public:
    std::string method; // "load", "call", "call_str", "call_dbl", "free", "has"
    ASTPtr target;      // Handle instance if called as lib.call(...). Nullptr if called directly as clib.call(...)
    std::vector<ASTPtr> args;
    int line = 0;

    CLibMethodNode(std::string m, ASTPtr t, std::vector<ASTPtr> a, int l = 0)
        : method(std::move(m)), target(std::move(t)), args(std::move(a)), line(l) {}

    std::string generateCode() override {
        if (method == "load") {
            if (args.empty()) { std::cerr << "Error: clib.load() requires library path at line " << line << "\n"; exit(1); }
            return "_thava_clib_load(" + args[0]->generateCode() + ")";
        }
        else if (method == "free") {
            if (target) return "_thava_clib_free(" + target->generateCode() + ")";
            if (args.empty()) { std::cerr << "Error: clib.free() requires library handle at line " << line << "\n"; exit(1); }
            return "_thava_clib_free(" + args[0]->generateCode() + ")";
        }
        else if (method == "has") {
            if (target) {
                if (args.empty()) { std::cerr << "Error: lib.has() requires function name at line " << line << "\n"; exit(1); }
                return "_thava_clib_has(" + target->generateCode() + ", " + args[0]->generateCode() + ")";
            }
            if (args.size() < 2) { std::cerr << "Error: clib.has() requires (lib, func) arguments at line " << line << "\n"; exit(1); }
            return "_thava_clib_has_direct(" + args[0]->generateCode() + ", " + args[1]->generateCode() + ")";
        }
        else if (method == "call" || method == "call_str" || method == "call_dbl") {
            std::string invoker = "_thava_clib_invoke";
            std::string directInvoker = "_thava_clib_call_direct";
            if (method == "call_str") {
                invoker = "_thava_clib_invoke_str";
                directInvoker = "_thava_clib_call_direct_str";
            } else if (method == "call_dbl") {
                invoker = "_thava_clib_invoke_dbl";
                directInvoker = "_thava_clib_call_direct_dbl";
            }

            if (target) {
                if (args.empty()) { std::cerr << "Error: lib.call() requires function name at line " << line << "\n"; exit(1); }
                std::string code = invoker + "(" + target->generateCode() + ", " + args[0]->generateCode();
                for (size_t i = 1; i < args.size(); ++i) {
                    code += ", _thava_c_arg(" + args[i]->generateCode() + ")";
                }
                code += ")";
                return code;
            } else {
                if (args.size() < 2) { std::cerr << "Error: clib.call() requires at least 2 arguments (lib, func) at line " << line << "\n"; exit(1); }
                std::string code = directInvoker + "(" + args[0]->generateCode() + ", " + args[1]->generateCode();
                for (size_t i = 2; i < args.size(); ++i) {
                    code += ", _thava_c_arg(" + args[i]->generateCode() + ")";
                }
                code += ")";
                return code;
            }
        }
        else if (method == "alloc") {
            if (args.empty()) { std::cerr << "Error: clib.alloc() requires byte size at line " << line << "\n"; exit(1); }
            return "_thava_clib_alloc(" + args[0]->generateCode() + ")";
        }
        else if (method == "free_mem") {
            if (args.empty()) { std::cerr << "Error: clib.free_mem() requires pointer address at line " << line << "\n"; exit(1); }
            return "_thava_clib_free_mem(" + args[0]->generateCode() + ")";
        }
        else if (method == "peek" || method == "peek32") {
            if (args.empty()) { std::cerr << "Error: clib." << method << "() requires pointer address at line " << line << "\n"; exit(1); }
            std::string off = (args.size() > 1) ? args[1]->generateCode() : "0";
            return "_thava_clib_peek32(" + args[0]->generateCode() + ", " + off + ")";
        }
        else if (method == "peek_u8") {
            if (args.empty()) { std::cerr << "Error: clib.peek_u8() requires pointer address at line " << line << "\n"; exit(1); }
            std::string off = (args.size() > 1) ? args[1]->generateCode() : "0";
            return "_thava_clib_peek_u8(" + args[0]->generateCode() + ", " + off + ")";
        }
        else if (method == "peek64") {
            if (args.empty()) { std::cerr << "Error: clib.peek64() requires pointer address at line " << line << "\n"; exit(1); }
            std::string off = (args.size() > 1) ? args[1]->generateCode() : "0";
            return "_thava_clib_peek64(" + args[0]->generateCode() + ", " + off + ")";
        }
        else if (method == "peek_f") {
            if (args.empty()) { std::cerr << "Error: clib.peek_f() requires pointer address at line " << line << "\n"; exit(1); }
            std::string off = (args.size() > 1) ? args[1]->generateCode() : "0";
            return "_thava_clib_peek_f(" + args[0]->generateCode() + ", " + off + ")";
        }
        else if (method == "poke" || method == "poke32") {
            if (args.size() < 3) { std::cerr << "Error: clib." << method << "() requires (ptr, offset, val) at line " << line << "\n"; exit(1); }
            return "_thava_clib_poke32(" + args[0]->generateCode() + ", " + args[1]->generateCode() + ", " + args[2]->generateCode() + ")";
        }
        else if (method == "poke64") {
            if (args.size() < 3) { std::cerr << "Error: clib.poke64() requires (ptr, offset, val) at line " << line << "\n"; exit(1); }
            return "_thava_clib_poke64(" + args[0]->generateCode() + ", " + args[1]->generateCode() + ", " + args[2]->generateCode() + ")";
        }
        else if (method == "poke_f") {
            if (args.size() < 3) { std::cerr << "Error: clib.poke_f() requires (ptr, offset, val) at line " << line << "\n"; exit(1); }
            return "_thava_clib_poke_f(" + args[0]->generateCode() + ", " + args[1]->generateCode() + ", " + args[2]->generateCode() + ")";
        }
        std::cerr << "Error: clib method clib." << method << " is invalid at line " << line << "\n";
        exit(1);
    }

    ThavaType getType(std::shared_ptr<Environment>) override {
        if (method == "call_str") return ThavaType::STRING;
        if (method == "call_dbl" || method == "peek_f") return ThavaType::DOUBLE;
        if (method == "has") return ThavaType::BOOL;
        if (method == "free_mem") return ThavaType::VOID;
        return ThavaType::INT;
    }
};

// AST node for Dictionary/Map methods: .keys(), .values(), .items(), .get(k), .has(k), .remove(k), .clear()
class DictMethodNode : public ASTNode {
public:
    ASTPtr base;
    std::string method;
    std::vector<ASTPtr> args;
    int line = 0;
    DictMethodNode(ASTPtr b, std::string m, std::vector<ASTPtr> a = {}, int l = 0)
        : base(std::move(b)), method(std::move(m)), args(std::move(a)), line(l) {}

    bool isMutating() const { return method == "clear" || method == "remove" || method == "del"; }

    std::string generateCode() override {
        if (method == "keys") return "((" + base->generateCode() + ").keys())";
        if (method == "values") return "((" + base->generateCode() + ").values())";
        if (method == "items") return "((" + base->generateCode() + ").items())";
        if (method == "clear") return "((" + base->generateCode() + ").clear())";
        if (method == "has" || method == "contains") {
            if (args.empty()) { std::cerr << "Error: ." << method << "() requires key argument at line " << line << "\n"; exit(1); }
            return "((" + base->generateCode() + ").has(" + args[0]->generateCode() + "))";
        }
        if (method == "get") {
            if (args.empty()) { std::cerr << "Error: .get() requires key argument at line " << line << "\n"; exit(1); }
            if (args.size() == 1) return "((" + base->generateCode() + ").get(" + args[0]->generateCode() + "))";
            return "((" + base->generateCode() + ").get(" + args[0]->generateCode() + ", " + args[1]->generateCode() + "))";
        }
        if (method == "remove" || method == "del") {
            if (args.empty()) { std::cerr << "Error: ." << method << "() requires key argument at line " << line << "\n"; exit(1); }
            return "((" + base->generateCode() + ").remove(" + args[0]->generateCode() + "))";
        }
        return base->generateCode();
    }

    ThavaType getType(std::shared_ptr<Environment> env) override {
        if (env && isMutating()) {
            if (auto var = dynamic_cast<VarNode*>(base.get()))
                rejectVariableWrite(env, var->getName(), env->functionName);
        }
        if (base) base->getType(env);
        for (auto& a : args) a->getType(env);
        if (method == "keys" || method == "values" || method == "items") return ThavaType::LIST;
        if (method == "has" || method == "contains") return ThavaType::BOOL;
        if (isMutating()) return ThavaType::VOID;
        return ThavaType::UNKNOWN;
    }
};

class ExpressionStatementNode : public ASTNode {
public:
    ASTPtr expr;
    ExpressionStatementNode(ASTPtr e) : expr(std::move(e)) {}
    std::string generateCode() override {
        return expr->generateCode() + ";";
    }
    ThavaType getType(std::shared_ptr<Environment> env) override {
        return expr->getType(env);
    }
};

// AST node for function declarations: <int>func name(<int> a, <int> b) { ... }
class FunctionDeclNode : public ASTNode {
public:
    ThavaType returnType;
    std::string customReturnType;
    std::string name;
    std::vector<std::pair<ThavaType, std::string>> params;
    std::vector<std::string> paramCustomTypes;
    std::vector<ASTPtr> body;
    bool isInline = false;

    FunctionDeclNode(ThavaType rt, std::string n, std::vector<std::pair<ThavaType, std::string>> p, std::vector<ASTPtr> b, std::vector<std::string> pct = {}, std::string crt = "", bool inl = false)
        : returnType(rt), customReturnType(std::move(crt)), name(std::move(n)), params(std::move(p)), paramCustomTypes(std::move(pct)), body(std::move(b)), isInline(inl) {}

    std::string generateCode() override {
        std::string retTypeStr = cppTypeNameFor(returnType, customReturnType);
        std::string code = (isInline ? "inline " : "") + retTypeStr + " thava_func_" + name + "(";
        for (size_t i = 0; i < params.size(); ++i) {
            std::string ctype = (i < paramCustomTypes.size()) ? paramCustomTypes[i] : "";
            code += cppParamTypeNameFor(params[i].first, ctype) + " " + params[i].second;
            if (i + 1 < params.size()) code += ", ";
        }
        code += ") {\n";
        code += thavaCppBodyCode(body, "    ", params);
        // Safe fallback in case function body lacks return in all branches (avoids C++ missing return warning)
        if (returnType != ThavaType::VOID) {
            code += "    " + retTypeStr + " _thava_fallback_ret{};\n";
            code += "    return _thava_fallback_ret;\n";
        }
        code += "}\n";
        return code;
    }

    ThavaType getType(std::shared_ptr<Environment>) override { return ThavaType::UNKNOWN; }
};

// AST node for return statement: return expr; or return;
class ReturnNode : public ASTNode {
public:
    ASTPtr expr;
    ReturnNode(ASTPtr e) : expr(std::move(e)) {}
    std::string generateCode() override {
        if (expr) return "return " + expr->generateCode() + ";";
        return "return;";
    }
    ThavaType getType(std::shared_ptr<Environment> env) override {
        if (expr) return expr->getType(env);
        return ThavaType::VOID;
    }
};

// AST node for defer statement: defer stmt; or defer { ... }
class DeferNode : public ASTNode {
public:
    ASTPtr stmt;
    static inline int deferCounter = 0;
    DeferNode(ASTPtr s) : stmt(std::move(s)) {}

    std::string generateCode() override {
        int id = ++deferCounter;
        return "_ThavaScopeGuard _thava_defer_guard_" + std::to_string(id) + "([&]() {\n        " + stmt->generateCode() + "\n    });";
    }

    ThavaType getType(std::shared_ptr<Environment>) override { return ThavaType::VOID; }
};

// AST node for user function or cfunc call
std::string thavaCppArgCode(const std::string& fname, size_t i, ASTNode* argNode);

class CallNode : public ASTNode {
public:
    std::string name;
    std::vector<ASTPtr> args;
    ThavaType returnType;
    CallNode(std::string n, std::vector<ASTPtr> a, ThavaType rt)
        : name(std::move(n)), args(std::move(a)), returnType(rt) {}

    std::string generateCode() override {
        if (g_dlFunctions.count(name)) {
            const auto& dlInfo = g_dlFunctions[name];
            std::string code = name + "(";
            for (size_t i = 0; i < args.size(); ++i) {
                code += "_thava_c_arg(" + args[i]->generateCode() + ")";
                if (i + 1 < args.size()) code += ", ";
            }
            code += ")";
            if (dlInfo.retType == ThavaType::STRING) {
                return "std::string(" + code + ")";
            }
            return code;
        }

        bool isC = g_isCFunc.count(name) > 0;
        bool isUserFunc = g_funcReturnTypes.count(name) > 0;
        std::string prefix = isC ? "" : (isUserFunc ? "thava_func_" : "");
        std::string code = prefix + name + "(";
        for (size_t i = 0; i < args.size(); ++i) {
            if (isC) {
                code += "_thava_c_arg(" + args[i]->generateCode() + ")";
            } else {
                code += thavaCppArgCode(name, i, args[i].get());
            }
            if (i + 1 < args.size()) code += ", ";
        }
        code += ")";
        return code;
    }

    ThavaType getType(std::shared_ptr<Environment> env) override {
        if (g_funcParamTypes.count(name)) {
            const auto& expectedTypes = g_funcParamTypes[name];
            if (args.size() == expectedTypes.size()) {
                for (size_t i = 0; i < args.size(); ++i) {
                    ThavaType actual = args[i]->getType(env);
                    if (!isCompatible(expectedTypes[i], actual) && actual != ThavaType::UNKNOWN) {
                        std::cerr << "[Static Type Error]: Argument #" << (i + 1)
                                  << " of function '" << name << "' expects type <" << typeToString(expectedTypes[i])
                                  << "> but received type <" << typeToString(actual) << ">!\n";
                        exit(1);
                    }
                }
            }
        }
        return returnType;
    }
};

// AST node for explicit type cast / type annotation
class CastNode : public ASTNode {
public:
    ThavaType targetType;
    std::string customTarget; // "int64" / "int16" for integer widths
    ASTPtr expr;
    CastNode(ThavaType t, ASTPtr e, std::string ct = "")
        : targetType(t), customTarget(std::move(ct)), expr(std::move(e)) {}

    std::string generateCode() override {
        std::string innerCode = expr->generateCode();
        switch (targetType) {
            case ThavaType::INT:
                return "((" + cppIntName(customTarget) + ")_thava_to_int(" + innerCode + "))";
            case ThavaType::FLOAT: return "(_thava_to_float(" + innerCode + "))";
            case ThavaType::DOUBLE: return "(_thava_to_double(" + innerCode + "))";
            case ThavaType::BOOL: return "(_thava_to_bool(" + innerCode + "))";
            case ThavaType::STRING: return "(_thava_to_string(" + innerCode + "))";
            default: return innerCode;
        }
    }

    ThavaType getType(std::shared_ptr<Environment> env) override {
        if (expr) expr->getType(env);
        return targetType;
    }
};

// AST node for built-in random functions (rand, randint, randres)
class RandCallNode : public ASTNode {
public:
    std::string funcName;
    std::vector<ASTPtr> args;
    ASTPtr weightExpr; // Used for randres(), nullptr if weight is not specified

    RandCallNode(std::string name, std::vector<ASTPtr> arguments, ASTPtr weight = nullptr)
        : funcName(std::move(name)), args(std::move(arguments)), weightExpr(std::move(weight)) {}

    std::string generateCode() override {
        if (funcName == "rand") {
            if (args.empty()) {
                return "(_thava_rand_double(0.0, 1.0))";
            } else if (args.size() == 2) {
                return "(_thava_rand_double(" + args[0]->generateCode() + ", " + args[1]->generateCode() + "))";
            } else {
                std::cerr << "Error: rand() accepts 0 or 2 arguments!\n";
                exit(1);
            }
        } else if (funcName == "randint") {
            if (args.empty()) {
                return "(_thava_rand_int(0, 10))";
            } else if (args.size() == 2) {
                return "(_thava_rand_int(" + args[0]->generateCode() + ", " + args[1]->generateCode() + "))";
            } else {
                std::cerr << "Error: randint() accepts 0 or 2 arguments!\n";
                exit(1);
            }
        } else { // randres
            if (args.size() == 1) {
                if (weightExpr) {
                    return "(_thava_rand_choice_weighted(" + args[0]->generateCode() + ", " + weightExpr->generateCode() + "))";
                }
                return "(_thava_rand_choice(" + args[0]->generateCode() + "))";
            } else {
                std::cerr << "Error: randres() requires exactly 1 argument (list)!\n";
                exit(1);
            }
        }
    }

    ThavaType getType(std::shared_ptr<Environment>) override {
        if (funcName == "rand") return ThavaType::DOUBLE;
        if (funcName == "randint") return ThavaType::INT;
        return ThavaType::UNKNOWN; 
    }
};

// AST node for locvar (reference to current object inside method/constructor)
class LocVarNode : public ASTNode {
public:
    LocVarNode() = default;
    std::string generateCode() override {
        return "(*this)";
    }
    ThavaType getType(std::shared_ptr<Environment>) override {
        return ThavaType::CUSTOM;
    }
};

// AST node for parvar (reference to parent class inside method/constructor)
class ParVarNode : public ASTNode {
public:
    std::string baseClassName;
    ParVarNode(std::string base) : baseClassName(std::move(base)) {}
    std::string generateCode() override {
        return "(*static_cast<thava_class_" + baseClassName + "*>(this))";
    }
    ThavaType getType(std::shared_ptr<Environment>) override {
        return ThavaType::CUSTOM;
    }
};

// Forward declaration for resolveTargetClass
inline std::string resolveTargetClass(ASTNode* target, std::shared_ptr<Environment> env,
                                      const std::string& currentClassName = "",
                                      const std::string& currentBaseClassName = "");

// AST node for class instantiation: ClassName(arg1, arg2, ...)
class ClassInstantiateNode : public ASTNode {
public:
    std::string className;
    std::vector<ASTPtr> args;
    int line = 0;
    ClassInstantiateNode(std::string name, std::vector<ASTPtr> a, int l = 0)
        : className(std::move(name)), args(std::move(a)), line(l) {}

    std::string generateCode() override {
        std::string code = "thava_class_" + className + "(";
        for (size_t i = 0; i < args.size(); ++i) {
            code += args[i]->generateCode();
            if (i + 1 < args.size()) code += ", ";
        }
        code += ")";
        return code;
    }

    ThavaType getType(std::shared_ptr<Environment>) override {
        return ThavaType::CUSTOM;
    }
};

// AST node for struct instantiation: StructName(arg1, arg2, ...) or StructName()
class StructInstantiateNode : public ASTNode {
public:
    std::string structName;
    std::vector<ASTPtr> args;
    int line = 0;
    StructInstantiateNode(std::string name, std::vector<ASTPtr> a, int l = 0)
        : structName(std::move(name)), args(std::move(a)), line(l) {}

    std::string generateCode() override {
        // C++ brace-initialisation rejects narrowing conversions (e.g. 40000
        // into a <int16> field), so cast integer arguments to the field width.
        const StructInfo* si = nullptr;
        auto sit = g_structs.find(structName);
        if (sit != g_structs.end()) si = &sit->second;
        std::string code = structName + "{";
        for (size_t i = 0; i < args.size(); ++i) {
            std::string argCode = args[i]->generateCode();
            if (si && i < si->fields.size() && si->fields[i].type == ThavaType::INT) {
                argCode = "static_cast<" + cppIntName(si->fields[i].customType) + ">(" + argCode + ")";
            }
            code += argCode;
            if (i + 1 < args.size()) code += ", ";
        }
        code += "}";
        return code;
    }

    ThavaType getType(std::shared_ptr<Environment>) override {
        return ThavaType::CUSTOM;
    }
};

// AST node for member access: target.field (e.g. locvar.name, p.age, parvar.field)
class MemberAccessNode : public ASTNode {
public:
    ASTPtr target;
    std::string memberName;
    int line = 0;
    MemberAccessNode(ASTPtr t, std::string m, int l = 0)
        : target(std::move(t)), memberName(std::move(m)), line(l) {}

    std::string generateCode() override {
        if (auto par = dynamic_cast<ParVarNode*>(target.get())) {
            return "(((this->thava_class_" + par->baseClassName + "::" + memberName + ")))";
        }
        return "(((" + target->generateCode() + ")." + memberName + "))";
    }

    ThavaType getType(std::shared_ptr<Environment> env) override {
        std::string targetClass = resolveTargetClass(target.get(), env);
        if (!targetClass.empty()) {
            auto res = lookupField(targetClass, memberName);
            if (res.found) return res.type;
        }
        return ThavaType::UNKNOWN;
    }
};

// A collection argument that is not a plain variable is an rvalue in C++, but the
// generated parameter is a non-const reference. Bind such values to a reusable
// thread-local slot so the call still compiles (and the mutation is discarded,
// exactly like passing a temporary in the native backends).
static bool thavaIsLvalueArg(ASTNode* n) {
    if (!n) return false;
    if (dynamic_cast<VarNode*>(n)) return true;
    if (dynamic_cast<IndexNode*>(n)) return true;
    if (dynamic_cast<MemberAccessNode*>(n)) return true;
    return false;
}

std::string thavaCppArgCode(const std::string& fname, size_t i, ASTNode* argNode) {
    std::string argCode = argNode ? argNode->generateCode() : std::string();
    auto it = g_funcParamTypes.find(fname);
    if (it != g_funcParamTypes.end() && i < it->second.size()) {
        ThavaType pt = it->second[i];
        bool byRef = (pt == ThavaType::LIST || pt == ThavaType::MULTYPE || pt == ThavaType::DICT);
        if (byRef && !thavaIsLvalueArg(argNode)) {
            return "_thava_tmp_ref(" + argCode + ")";
        }
    }
    return argCode;
}

// AST node for member assignment: target.field = expr (e.g. locvar.age = 20)
class MemberAssignNode : public ASTNode {
public:
    ASTPtr target;
    std::string memberName;
    ASTPtr expr;
    int line = 0;
    std::string op = "=";
    MemberAssignNode(ASTPtr t, std::string m, ASTPtr e, int l = 0, std::string o = "=")
        : target(std::move(t)), memberName(std::move(m)), expr(std::move(e)), line(l), op(std::move(o)) {}

    std::string generateCode() override {
        std::string targetCode = "(" + target->generateCode() + ")." + memberName;
        if (auto par = dynamic_cast<ParVarNode*>(target.get())) {
            targetCode = "this->thava_class_" + par->baseClassName + "::" + memberName;
        }
        if (op == "=") return targetCode + " = " + expr->generateCode() + ";";
        if (op == "+=") return targetCode + " += " + expr->generateCode() + ";";
        if (op == "-=") return targetCode + " -= " + expr->generateCode() + ";";
        if (op == "*=") return targetCode + " *= " + expr->generateCode() + ";";
        if (op == "/=") return targetCode + " = _thava_div(" + targetCode + ", " + expr->generateCode() + ");";
        if (op == "%=") return targetCode + " = _thava_mod(" + targetCode + ", " + expr->generateCode() + ");";
        if (op == "**=") return targetCode + " = _thava_pow(" + targetCode + ", " + expr->generateCode() + ");";
        return targetCode + " " + op + " " + expr->generateCode() + ";";
    }

    ThavaType getType(std::shared_ptr<Environment>) override {
        return ThavaType::UNKNOWN;
    }
};

// AST node for method invocation: target.method(args...) (e.g. locvar.greet())
class MethodCallNode : public ASTNode {
public:
    ASTPtr target;
    std::string methodName;
    std::vector<ASTPtr> args;
    int line = 0;
    MethodCallNode(ASTPtr t, std::string m, std::vector<ASTPtr> a, int l = 0)
        : target(std::move(t)), methodName(std::move(m)), args(std::move(a)), line(l) {}

    std::string generateCode() override {
        if (auto par = dynamic_cast<ParVarNode*>(target.get())) {
            std::string code = "(((this->thava_class_" + par->baseClassName + "::thava_method_" + methodName + "(";
            for (size_t i = 0; i < args.size(); ++i) {
                code += args[i]->generateCode();
                if (i + 1 < args.size()) code += ", ";
            }
            code += "))))";
            return code;
        }

        std::string code = "(((" + target->generateCode() + ").thava_method_" + methodName + "(";
        for (size_t i = 0; i < args.size(); ++i) {
            code += args[i]->generateCode();
            if (i + 1 < args.size()) code += ", ";
        }
        code += ")))";
        return code;
    }

    ThavaType getType(std::shared_ptr<Environment> env) override {
        std::string targetClass = resolveTargetClass(target.get(), env);
        if (!targetClass.empty()) {
            auto res = lookupMethod(targetClass, methodName);
            if (res.found) return res.type;
        }
        return ThavaType::UNKNOWN;
    }
};

struct ClassFieldDecl {
    AccessModifier access = AccessModifier::PUBLIC;
    ThavaType type = ThavaType::UNKNOWN;
    std::string customTypeName = "";
    std::string name = "";
    ASTPtr initExpr = nullptr;
    int line = 0;
};

struct ClassConstructorDecl {
    AccessModifier access = AccessModifier::PUBLIC;
    std::vector<std::pair<ThavaType, std::string>> params;
    std::vector<std::string> paramCustomTypes;
    std::vector<ASTPtr> body;
    int line = 0;
};

struct ClassMethodDecl {
    AccessModifier access = AccessModifier::PUBLIC;
    ThavaType returnType = ThavaType::VOID;
    std::string customReturnType = "";
    std::string name = "";
    std::vector<std::pair<ThavaType, std::string>> params;
    std::vector<std::string> paramCustomTypes;
    std::vector<ASTPtr> body;
    int line = 0;
};

// AST node for class declaration: class ClassName { ... }
class ClassDeclNode : public ASTNode {
public:
    std::string className;
    std::string baseClassName;
    std::vector<ClassFieldDecl> fields;
    std::vector<ClassConstructorDecl> constructors;
    std::vector<ClassMethodDecl> methods;

    ClassDeclNode(std::string name, std::string base, std::vector<ClassFieldDecl> f,
                  std::vector<ClassConstructorDecl> c, std::vector<ClassMethodDecl> m)
        : className(std::move(name)), baseClassName(std::move(base)), fields(std::move(f)),
          constructors(std::move(c)), methods(std::move(m)) {}

    std::string generateCode() override {
        std::string code = "struct thava_class_" + className;
        if (!baseClassName.empty()) {
            code += " : public thava_class_" + baseClassName;
        }
        code += " {\n";
        code += "public:\n";
        code += "    virtual ~thava_class_" + className + "() = default;\n";
        code += "    bool _is_none = false;\n";
        code += "    thava_class_" + className + "(const ThavaNoneType&) : " +
                (baseClassName.empty() ? "" : "thava_class_" + baseClassName + "(_thava_none), ") + "_is_none(true) {}\n";
        code += "    thava_class_" + className + "& operator=(const ThavaNoneType&) { _is_none = true; return *this;\n}\n";
        code += "    bool operator==(const ThavaNoneType&) const { return _is_none; }\n";
        code += "    bool operator!=(const ThavaNoneType&) const { return !_is_none; }\n";
        code += "    friend bool operator==(const ThavaNoneType&, const thava_class_" + className + "& o) { return o._is_none; }\n";
        code += "    friend bool operator!=(const ThavaNoneType&, const thava_class_" + className + "& o) { return !o._is_none; }\n";
        code += "    friend std::ostream& operator<<(std::ostream& os, const thava_class_" + className + "& o) {\n";
        code += "        if (o._is_none) os << \"none\";\n";
        code += "        else os << \"<" + className + " object>\";\n";
        code += "        return os;\n";
        code += "    }\n\n";

        // Default constructor if no explicit 0-argument constructor exists
        bool hasDefaultCtor = false;
        for (const auto& c : constructors) {
            if (c.params.empty()) { hasDefaultCtor = true; break; }
        }
        if (!hasDefaultCtor) {
            code += "    thava_class_" + className + "() : _is_none(true) {}\n";
            code += "    virtual void thava_method_init() {}\n\n";
        }

        auto emitSection = [&](AccessModifier mod, const std::string& sectionName) {
            std::string secCode = "";

            // Fields
            for (const auto& f : fields) {
                if (f.access != mod) continue;
                std::string tName;
                if (f.type == ThavaType::CUSTOM) tName = "thava_class_" + f.customTypeName;
                else tName = cppTypeNameFor(f.type, f.customTypeName);
                secCode += "    " + tName + " " + f.name;
                if (f.initExpr) {
                    secCode += " = " + f.initExpr->generateCode();
                } else {
                    if (f.type == ThavaType::INT) secCode += " = 0LL";
                    else if (f.type == ThavaType::FLOAT) secCode += " = 0.0f";
                    else if (f.type == ThavaType::DOUBLE) secCode += " = 0.0";
                    else if (f.type == ThavaType::BOOL) secCode += " = false";
                    else if (f.type == ThavaType::STRING) secCode += " = \"\"";
                    else if (f.type == ThavaType::LIST || f.type == ThavaType::TUPLE || f.type == ThavaType::MULTYPE) secCode += " = {}";
                }
                secCode += ";\n";
            }
            if (!secCode.empty()) secCode += "\n";

            // Constructors
            for (const auto& c : constructors) {
                if (c.access != mod) continue;
                secCode += "    thava_class_" + className + "(";
                for (size_t i = 0; i < c.params.size(); ++i) {
                    std::string pTypeStr;
                    if (c.params[i].first == ThavaType::CUSTOM) pTypeStr = "thava_class_" + c.paramCustomTypes[i] + "&";
                    else pTypeStr = cppParamTypeNameFor(c.params[i].first);
                    secCode += pTypeStr + " " + c.params[i].second;
                    if (i + 1 < c.params.size()) secCode += ", ";
                }
                secCode += ") : _is_none(false) {\n";
                secCode += thavaCppBodyCode(c.body, "        ", c.params);
                secCode += "    }\n\n";

                // Method thava_method_init to support parvar.init(...)
                secCode += "    virtual void thava_method_init(";
                for (size_t i = 0; i < c.params.size(); ++i) {
                    std::string pTypeStr;
                    if (c.params[i].first == ThavaType::CUSTOM) pTypeStr = "thava_class_" + c.paramCustomTypes[i] + "&";
                    else pTypeStr = cppParamTypeNameFor(c.params[i].first);
                    secCode += pTypeStr + " " + c.params[i].second;
                    if (i + 1 < c.params.size()) secCode += ", ";
                }
                secCode += ") {\n        this->_is_none = false;\n";
                secCode += thavaCppBodyCode(c.body, "        ", c.params);
                secCode += "    }\n\n";
            }

            // Phuong thuc (Methods)
            for (const auto& m : methods) {
                if (m.access != mod) continue;
                std::string retTypeStr;
                if (m.returnType == ThavaType::CUSTOM) retTypeStr = "thava_class_" + m.customReturnType;
                else retTypeStr = cppTypeNameFor(m.returnType, m.customReturnType);
                secCode += "    virtual " + retTypeStr + " thava_method_" + m.name + "(";
                for (size_t i = 0; i < m.params.size(); ++i) {
                    std::string pTypeStr;
                    if (m.params[i].first == ThavaType::CUSTOM) pTypeStr = "thava_class_" + m.paramCustomTypes[i] + "&";
                    else pTypeStr = cppParamTypeNameFor(m.params[i].first);
                    secCode += pTypeStr + " " + m.params[i].second;
                    if (i + 1 < m.params.size()) secCode += ", ";
                }
                secCode += ") {\n";
                secCode += thavaCppBodyCode(m.body, "        ", m.params);
                if (m.returnType != ThavaType::VOID) {
                    secCode += "        " + retTypeStr + " _thava_fallback_ret{};\n";
                    secCode += "        return _thava_fallback_ret;\n";
                }
                secCode += "    }\n\n";
            }

            if (!secCode.empty()) {
                return sectionName + ":\n" + secCode;
            }
            return std::string("");
        };

        code += emitSection(AccessModifier::PUBLIC, "public");
        code += emitSection(AccessModifier::PROTECTED, "protected");
        code += emitSection(AccessModifier::PRIVATE, "private");

        code += "};\n";
        return code;
    }

    ThavaType getType(std::shared_ptr<Environment>) override {
        return ThavaType::UNKNOWN;
    }
};

// AST node for enum declaration: enum Direction { NORTH = 1, SOUTH, EAST = 10, WEST }
class EnumDeclNode : public ASTNode {
public:
    std::string enumName;
    std::vector<std::pair<std::string, int64_t>> members;
    EnumDeclNode(std::string name, std::vector<std::pair<std::string, int64_t>> m)
        : enumName(std::move(name)), members(std::move(m)) {}

    std::string generateCode() override {
        std::string code = "enum " + enumName + " : long long {\n";
        for (size_t i = 0; i < members.size(); ++i) {
            code += "    " + enumName + "_" + members[i].first + " = " + std::to_string(members[i].second);
            if (i + 1 < members.size()) code += ",";
            code += "\n";
        }
        code += "};\n";
        return code;
    }

    ThavaType getType(std::shared_ptr<Environment>) override { return ThavaType::INT; }
};

// AST node for struct declaration: struct Point { <dbl> x; <dbl> y; }
class StructDeclNode : public ASTNode {
public:
    std::string structName;
    std::vector<ClassFieldDecl> fields;
    StructDeclNode(std::string name, std::vector<ClassFieldDecl> f)
        : structName(std::move(name)), fields(std::move(f)) {}

    std::string generateCode() override {
        std::string code = "struct " + structName + " {\n";
        for (const auto& f : fields) {
            std::string tName;
            if (f.type == ThavaType::CUSTOM) {
                if (g_structs.count(f.customTypeName) || g_enums.count(f.customTypeName)) {
                    tName = f.customTypeName;
                } else {
                    tName = "thava_class_" + f.customTypeName;
                }
            } else {
                tName = cppTypeNameFor(f.type, f.customTypeName);
            }
            code += "    " + tName + " " + f.name;
            if (f.initExpr) {
                code += " = " + f.initExpr->generateCode();
            } else {
                if (f.type == ThavaType::INT) code += " = 0LL";
                else if (f.type == ThavaType::FLOAT) code += " = 0.0f";
                else if (f.type == ThavaType::DOUBLE) code += " = 0.0";
                else if (f.type == ThavaType::BOOL) code += " = false";
                else if (f.type == ThavaType::STRING) code += " = \"\"";
            }
            code += ";\n";
        }
        code += "    friend std::ostream& operator<<(std::ostream& os, const " + structName + "& s) {\n";
        code += "        os << \"<struct " + structName + ">\";\n";
        code += "        return os;\n";
        code += "    }\n";
        code += "};\n";
        return code;
    }

    ThavaType getType(std::shared_ptr<Environment>) override { return ThavaType::CUSTOM; }
};

inline std::string resolveTargetClass(ASTNode* target, std::shared_ptr<Environment> env,
                                      const std::string& currentClassName,
                                      const std::string& currentBaseClassName) {
    if (!target) return "";
    if (dynamic_cast<LocVarNode*>(target)) {
        if (!currentClassName.empty()) return currentClassName;
        if (env) return env->getCurrentClassName();
        return "";
    }
    if (auto par = dynamic_cast<ParVarNode*>(target)) {
        if (!currentBaseClassName.empty()) return currentBaseClassName;
        if (env && !env->getCurrentBaseClassName().empty()) return env->getCurrentBaseClassName();
        return par->baseClassName;
    }
    if (auto var = dynamic_cast<VarNode*>(target)) {
        if (env) return env->getCustomType(var->getName());
        return "";
    }
    if (auto cls = dynamic_cast<ClassInstantiateNode*>(target)) {
        return cls->className;
    }
    if (auto structure = dynamic_cast<StructInstantiateNode*>(target)) return structure->structName;
    if (auto call = dynamic_cast<CallNode*>(target); call && call->returnType == ThavaType::CUSTOM) {
        const auto imported = g_dlFunctions.find(call->name);
        if (imported != g_dlFunctions.end()) return imported->second.returnWidth;
    }
    if (auto mem = dynamic_cast<MemberAccessNode*>(target)) {
        std::string subTargetClass = resolveTargetClass(mem->target.get(), env, currentClassName, currentBaseClassName);
        if (!subTargetClass.empty()) {
            auto res = lookupField(subTargetClass, mem->memberName);
            if (res.found && !res.customType.empty()) return res.customType;
        }
        return "";
    }
    if (auto mc = dynamic_cast<MethodCallNode*>(target)) {
        std::string subTargetClass = resolveTargetClass(mc->target.get(), env, currentClassName, currentBaseClassName);
        if (!subTargetClass.empty()) {
            auto res = lookupMethod(subTargetClass, mc->methodName);
            if (res.found && !res.customType.empty()) return res.customType;
        }
        return "";
    }
    return "";
}

inline void enforceAccess(const std::string& targetClass, const std::string& memberName, bool isMethod,
                          int line, const std::string& currentClassName) {
    if (targetClass.empty()) return;

    MemberLookupResult res = isMethod ? lookupMethod(targetClass, memberName)
                                      : lookupField(targetClass, memberName);
    if (!res.found) return;

    std::string memberKind = isMethod ? "phuong thuc" : "thuoc tinh";

    if (res.access == AccessModifier::PRIVATE) {
        if (currentClassName != res.definedInClass) {
            std::cerr << "[Static Type Error]: Cannot access " << memberKind
                      << " private '" << memberName << "' cua class '" << res.definedInClass
                      << "'" << (currentClassName.empty() ? " from outside" : " from class '" + currentClassName + "'")
                      << (line > 0 ? (" at line " + std::to_string(line)) : "") << "!\n";
            exit(1);
        }
    } else if (res.access == AccessModifier::PROTECTED) {
        if (currentClassName != res.definedInClass && !isSubclassOf(currentClassName, res.definedInClass)) {
            std::cerr << "[Static Type Error]: Cannot access " << memberKind
                      << " protected '" << memberName << "' cua class '" << res.definedInClass
                      << "'" << (currentClassName.empty() ? " from outside" : " from class '" + currentClassName + "'")
                      << (line > 0 ? (" at line " + std::to_string(line)) : "") << "!\n";
            exit(1);
        }
    }
}

// Element type of a list/tuple literal or of a list variable, as known by the
// type checker (defined further down, after the AST nodes it inspects).
static ThavaType inferCollectionElemType(ASTNode* expr, std::shared_ptr<Environment> env);
// Record the element type an argument brings for a <lst>/<tple> parameter.
static void noteListParamElemType(const std::string& fname, size_t idx, ThavaType et);
static void noteMethodListParamElemType(const std::string& key, size_t idx, ThavaType et);

static void rejectConstTargetWrite(const Environment& env, const ASTNode* target) {
    while (target) {
        if (auto var = dynamic_cast<const VarNode*>(target)) {
            rejectConstWrite(env, var->getName());
            return;
        }
        if (auto member = dynamic_cast<const MemberAccessNode*>(target)) {
            target = member->target.get();
        } else if (auto index = dynamic_cast<const IndexNode*>(target)) {
            target = index->base.get();
        } else {
            return;
        }
    }
}

inline void checkExpr(ASTNode* expr, std::shared_ptr<Environment> env,
                      const std::string& currentClassName = "",
                      const std::string& currentBaseClassName = "") {
    if (!expr) return;
    if (auto interpolation = dynamic_cast<InterpolatedStringNode*>(expr)) {
        for (const auto& part : interpolation->parts) {
            part->getType(env);
            checkExpr(part.get(), env, currentClassName, currentBaseClassName);
        }
        return;
    }
    if (auto input = dynamic_cast<InputNode*>(expr)) {
        checkExpr(input->prompt.get(), env, currentClassName, currentBaseClassName);
        return;
    }
    if (auto input = dynamic_cast<InputCaseNode*>(expr)) {
        checkExpr(input->prompt.get(), env, currentClassName, currentBaseClassName);
        return;
    }

    if (auto mem = dynamic_cast<MemberAccessNode*>(expr)) {
        checkExpr(mem->target.get(), env, currentClassName, currentBaseClassName);
        std::string targetClass = resolveTargetClass(mem->target.get(), env, currentClassName, currentBaseClassName);
        enforceAccess(targetClass, mem->memberName, false, mem->line, currentClassName);
    }
    else if (auto ma = dynamic_cast<MemberAssignNode*>(expr)) {
        rejectConstTargetWrite(*env, ma->target.get());
        checkExpr(ma->target.get(), env, currentClassName, currentBaseClassName);
        checkExpr(ma->expr.get(), env, currentClassName, currentBaseClassName);
        std::string targetClass = resolveTargetClass(ma->target.get(), env, currentClassName, currentBaseClassName);
        enforceAccess(targetClass, ma->memberName, false, ma->line, currentClassName);
    }
    else if (auto mc = dynamic_cast<MethodCallNode*>(expr)) {
        checkExpr(mc->target.get(), env, currentClassName, currentBaseClassName);
        for (auto& a : mc->args) checkExpr(a.get(), env, currentClassName, currentBaseClassName);
        std::string targetClass = resolveTargetClass(mc->target.get(), env, currentClassName, currentBaseClassName);
        if (!targetClass.empty()) {
            // Key the entry the way the backend emits the body: an inherited method
            // is emitted under the class that declares it.
            auto res = lookupMethod(targetClass, mc->methodName);
            std::string owner = res.found ? res.definedInClass : targetClass;
            for (size_t i = 0; i < mc->args.size(); ++i)
                noteMethodListParamElemType(owner + "::" + mc->methodName, i,
                                            inferCollectionElemType(mc->args[i].get(), env));
        }
        enforceAccess(targetClass, mc->methodName, true, mc->line, currentClassName);
    }
    else if (auto bin = dynamic_cast<BinaryOpNode*>(expr)) {
        checkExpr(bin->left.get(), env, currentClassName, currentBaseClassName);
        checkExpr(bin->right.get(), env, currentClassName, currentBaseClassName);
    }
    else if (auto unM = dynamic_cast<UnaryMinusNode*>(expr)) {
        checkExpr(unM->expr.get(), env, currentClassName, currentBaseClassName);
    }
    else if (auto unN = dynamic_cast<UnaryNotNode*>(expr)) {
        checkExpr(unN->expr.get(), env, currentClassName, currentBaseClassName);
    }
    else if (auto unBN = dynamic_cast<UnaryBNotNode*>(expr)) {
        checkExpr(unBN->expr.get(), env, currentClassName, currentBaseClassName);
    }
    else if (auto condExpr = dynamic_cast<ConditionalExprNode*>(expr)) {
        checkExpr(condExpr->condition.get(), env, currentClassName, currentBaseClassName);
        checkExpr(condExpr->trueExpr.get(), env, currentClassName, currentBaseClassName);
        checkExpr(condExpr->falseExpr.get(), env, currentClassName, currentBaseClassName);
    }
    else if (auto c = dynamic_cast<CastNode*>(expr)) {
        checkExpr(c->expr.get(), env, currentClassName, currentBaseClassName);
    }
    else if (auto idx = dynamic_cast<IndexNode*>(expr)) {
        checkExpr(idx->base.get(), env, currentClassName, currentBaseClassName);
        checkExpr(idx->indexExpr.get(), env, currentClassName, currentBaseClassName);
    }
    else if (auto coll = dynamic_cast<CollectionNode*>(expr)) {
        for (auto& e : coll->getElements()) checkExpr(e.get(), env, currentClassName, currentBaseClassName);
    }
    else if (auto dictNode = dynamic_cast<DictNode*>(expr)) {
        for (auto& p : dictNode->entries) {
            checkExpr(p.first.get(), env, currentClassName, currentBaseClassName);
            checkExpr(p.second.get(), env, currentClassName, currentBaseClassName);
        }
    }
    else if (auto listMethod = dynamic_cast<ListMethodNode*>(expr)) {
        listMethod->getType(env);
        for (auto& arg : listMethod->args) checkExpr(arg.get(), env, currentClassName, currentBaseClassName);
        if (listMethod->method == "addval" && !listMethod->args.empty() && canWriteGlobal(env, listMethod->varName)) {
            auto element = listMethod->args.back()->getType(env);
            if (element != ThavaType::UNKNOWN && g_programEnv->getListElem(listMethod->varName) == ThavaType::UNKNOWN)
                g_programEnv->defineListElem(listMethod->varName, element);
        }
    }
    else if (auto dictM = dynamic_cast<DictMethodNode*>(expr)) {
        if (dictM->isMutating()) rejectConstTargetWrite(*env, dictM->base.get());
        checkExpr(dictM->base.get(), env, currentClassName, currentBaseClassName);
        for (auto& a : dictM->args) checkExpr(a.get(), env, currentClassName, currentBaseClassName);
    }
    else if (auto call = dynamic_cast<CallNode*>(expr)) {
        for (auto& a : call->args) checkExpr(a.get(), env, currentClassName, currentBaseClassName);
        // Remember the element type of every list argument, so the callee's
        // `param[i]` is unboxed as int/double/string and not as raw bits.
        for (size_t i = 0; i < call->args.size(); ++i)
            noteListParamElemType(call->name, i,
                                  inferCollectionElemType(call->args[i].get(), env));
    }
    else if (auto osM = dynamic_cast<OsMethodNode*>(expr)) {
        for (auto& a : osM->args) checkExpr(a.get(), env, currentClassName, currentBaseClassName);
    }
    else if (auto timeM = dynamic_cast<TimeMethodNode*>(expr)) {
        for (auto& a : timeM->args) checkExpr(a.get(), env, currentClassName, currentBaseClassName);
    }
    else if (auto clibM = dynamic_cast<CLibMethodNode*>(expr)) {
        if (clibM->target) checkExpr(clibM->target.get(), env, currentClassName, currentBaseClassName);
        for (auto& a : clibM->args) checkExpr(a.get(), env, currentClassName, currentBaseClassName);
    }
    else if (auto ci = dynamic_cast<ClassInstantiateNode*>(expr)) {
        for (auto& a : ci->args) checkExpr(a.get(), env, currentClassName, currentBaseClassName);
        for (size_t i = 0; i < ci->args.size(); ++i)
            noteMethodListParamElemType(ci->className + "::init", i,
                                        inferCollectionElemType(ci->args[i].get(), env));
        auto it = g_classInfo.find(ci->className);
        if (it != g_classInfo.end() && !it->second.constructorAccess.empty()) {
            bool hasAccessibleCtor = false;
            for (auto acc : it->second.constructorAccess) {
                if (acc == AccessModifier::PUBLIC) {
                    hasAccessibleCtor = true;
                    break;
                } else if (acc == AccessModifier::PROTECTED) {
                    if (currentClassName == ci->className || isSubclassOf(currentClassName, ci->className)) {
                        hasAccessibleCtor = true;
                        break;
                    }
                } else if (acc == AccessModifier::PRIVATE) {
                    if (currentClassName == ci->className) {
                        hasAccessibleCtor = true;
                        break;
                    }
                }
            }
            if (!hasAccessibleCtor) {
                std::cerr << "[Static Type Error]: Cannot instantiate class '"
                          << ci->className << "' because constructor is not accessible from "
                          << (currentClassName.empty() ? "outside" : "class '" + currentClassName + "'")
                          << (ci->line > 0 ? (" at line " + std::to_string(ci->line)) : "") << "!\n";
                exit(1);
            }
        }
    }
    else if (auto sm = dynamic_cast<StrMethodNode*>(expr)) {
        checkExpr(sm->base.get(), env, currentClassName, currentBaseClassName);
    }
    else if (auto sam = dynamic_cast<StrAdvMethodNode*>(expr)) {
        checkExpr(sam->base.get(), env, currentClassName, currentBaseClassName);
        for (auto& a : sam->args) checkExpr(a.get(), env, currentClassName, currentBaseClassName);
    }
    else if (auto ss = dynamic_cast<StrSplitNode*>(expr)) {
        checkExpr(ss->base.get(), env, currentClassName, currentBaseClassName);
        checkExpr(ss->delimExpr.get(), env, currentClassName, currentBaseClassName);
    }
}

// ==========================================
// 4. SYNTAX ANALYSIS (PARSER)
// ==========================================

class Parser {
    std::vector<Token> tokens;
    size_t current = 0;
    std::string currentParsingClass = "";
    std::string currentParsingBaseClass = "";

    bool isAtEnd() const { return current >= tokens.size() || tokens[current].type == TokenType::EOF_TOKEN; }
    Token peek() const {
        if (current >= tokens.size()) {
            return {TokenType::EOF_TOKEN, "", tokens.empty() ? 1 : tokens.back().line};
        }
        diag::tokenLine() = tokens[current].line;
        diag::tokenText() = tokens[current].lexeme;
        return tokens[current];
    }
    Token peekAt(size_t offset) const {
        size_t idx = current + offset;
        if (idx >= tokens.size()) {
            return {TokenType::EOF_TOKEN, "", tokens.empty() ? 1 : tokens.back().line};
        }
        return tokens[idx];
    }
    Token advance() {
        if (current < tokens.size()) {
            diag::tokenLine() = tokens[current].line;
            diag::tokenText() = tokens[current].lexeme;
            diag::prevTokenLine() = tokens[current].line;
            diag::prevTokenText() = tokens[current].lexeme;
            return tokens[current++];
        }
        return {TokenType::EOF_TOKEN, "", tokens.empty() ? 1 : tokens.back().line};
    }
    bool check(TokenType type) { return peek().type == type; }
    bool match(TokenType type) {
        if (check(type)) { advance(); return true; }
        return false;
    }

    Token previous() const {
        if (current > 0 && current - 1 < tokens.size()) return tokens[current - 1];
        return {TokenType::EOF_TOKEN, "", 1};
    }

    bool hasElseBeforeBrace(size_t from) const {
        size_t k = from;
        int parenDepth = 0;
        while (k < tokens.size()) {
            if (tokens[k].type == TokenType::LPAREN) parenDepth++;
            else if (tokens[k].type == TokenType::RPAREN) {
                if (parenDepth > 0) parenDepth--;
            }
            else if (parenDepth == 0) {
                if (tokens[k].type == TokenType::LBRACE || tokens[k].type == TokenType::EOF_TOKEN) return false;
                if (tokens[k].type == TokenType::ELSE) return true;
            }
            k++;
        }
        return false;
    }

    bool isAssignOp(TokenType t) const {
        return t == TokenType::ASSIGN ||
               t == TokenType::PLUS_ASSIGN ||
               t == TokenType::MINUS_ASSIGN ||
               t == TokenType::STAR_ASSIGN ||
               t == TokenType::SLASH_ASSIGN ||
               t == TokenType::MODULO_ASSIGN ||
               t == TokenType::POW_ASSIGN;
    }

    bool isTypeToken(TokenType t) {
        return t == TokenType::TYPE_INT || t == TokenType::TYPE_INT64 || t == TokenType::TYPE_INT16 ||
               t == TokenType::TYPE_FL ||
               t == TokenType::TYPE_DBL || t == TokenType::TYPE_BL ||
               t == TokenType::TYPE_STR || t == TokenType::TYPE_LST ||
               t == TokenType::TYPE_TPLE || t == TokenType::TYPE_MULTYPE ||
               t == TokenType::TYPE_DICT || t == TokenType::TYPE_VOID ||
               t == TokenType::TYPE_FUNC || t == TokenType::TYPE_CUSTOM;
    }

    ThavaType parseType(std::string* outCustomName = nullptr) {
        if (match(TokenType::TYPE_INT)) return ThavaType::INT;
        if (match(TokenType::TYPE_INT64)) { if (outCustomName) *outCustomName = "int64"; return ThavaType::INT; }
        if (match(TokenType::TYPE_INT16)) { if (outCustomName) *outCustomName = "int16"; return ThavaType::INT; }
        if (match(TokenType::TYPE_FL)) return ThavaType::FLOAT;
        if (match(TokenType::TYPE_DBL)) return ThavaType::DOUBLE;
        if (match(TokenType::TYPE_BL)) return ThavaType::BOOL;
        if (match(TokenType::TYPE_STR)) return ThavaType::STRING;
        if (match(TokenType::TYPE_LST)) return ThavaType::LIST;
        if (match(TokenType::TYPE_TPLE)) return ThavaType::TUPLE;
        if (match(TokenType::TYPE_MULTYPE)) return ThavaType::MULTYPE;
        if (match(TokenType::TYPE_DICT)) { g_hasDict = true; return ThavaType::DICT; }
        if (match(TokenType::TYPE_VOID)) return ThavaType::VOID;
        if (match(TokenType::TYPE_FUNC)) return ThavaType::FUNC;
        if (check(TokenType::TYPE_CUSTOM)) {
            Token tok = advance();
            if (outCustomName) *outCustomName = tok.lexeme;
            return ThavaType::CUSTOM;
        }
        return ThavaType::UNKNOWN;
    }

    ASTPtr parseStringLiteral(const Token& token) {
        const auto& text = token.lexeme;
        if (text.find("@{") == std::string::npos)
            return std::make_unique<LiteralNode>(text, ThavaType::STRING);

        std::vector<ASTPtr> parts;
        std::string plain;
        int line = token.line;
        for (size_t pos = 0; pos < text.size();) {
            if (text.compare(pos, 3, "\\@{") == 0) {
                plain += "@{";
                pos += 3;
                continue;
            }
            if (text.compare(pos, 2, "@{") != 0) {
                if (text[pos] == '\n') ++line;
                plain += text[pos++];
                continue;
            }

            if (!plain.empty()) {
                parts.push_back(std::make_unique<LiteralNode>(std::move(plain), ThavaType::STRING));
                plain.clear();
            }
            const size_t begin = pos + 2;
            const int exprLine = line;
            pos = begin;
            size_t depth = 1;
            bool quoted = false;
            for (; pos < text.size(); ++pos) {
                const char ch = text[pos];
                if (ch == '\n') ++line;
                if (quoted && ch == '\\' && pos + 1 < text.size()) {
                    ++pos;
                } else if (ch == '"') {
                    quoted = !quoted;
                } else if (!quoted && ch == '{') {
                    ++depth;
                } else if (!quoted && ch == '}' && --depth == 0) {
                    break;
                }
            }
            if (pos == text.size()) {
                std::cerr << "Parser Error: Unterminated string interpolation at line " << exprLine << "\n";
                exit(1);
            }
            Lexer lexer(text.substr(begin, pos - begin), "", exprLine);
            Parser parser(lexer.scanTokens());
            parts.push_back(parser.parseExpressionPublic(true));
            ++pos;
        }
        if (parts.empty()) return std::make_unique<LiteralNode>(std::move(plain), ThavaType::STRING);
        if (!plain.empty()) parts.push_back(std::make_unique<LiteralNode>(std::move(plain), ThavaType::STRING));
        return std::make_unique<InterpolatedStringNode>(std::move(parts));
    }

    ASTPtr primary() {
        if (check(TokenType::RAISE_ERROR)) return raiseErrorStatement();

        // locvar keyword (current object reference)
        if (match(TokenType::LOCVAR)) {
            return std::make_unique<LocVarNode>();
        }

        // parvar keyword (parent class reference)
        if (match(TokenType::PARVAR)) {
            if (currentParsingBaseClass.empty()) {
                std::cerr << "Parser Error: 'parvar' can only be used inside subclasses inheriting from a parent class at line " << peek().line << "\n";
                exit(1);
            }
            return std::make_unique<ParVarNode>(currentParsingBaseClass);
        }

        if (match(TokenType::INT_LIT)) return std::make_unique<LiteralNode>(tokens[current-1].lexeme, ThavaType::INT);
        if (match(TokenType::FLOAT_LIT)) return std::make_unique<LiteralNode>(tokens[current-1].lexeme, ThavaType::FLOAT);
        if (match(TokenType::BOOL_LIT)) return std::make_unique<LiteralNode>(tokens[current-1].lexeme, ThavaType::BOOL);
        if (match(TokenType::STR_LIT)) return parseStringLiteral(tokens[current - 1]);
        if (match(TokenType::NONE_LIT)) return std::make_unique<LiteralNode>("none", ThavaType::NONE);
        
        // C namespace syntax: c.<func_name>(...) or c.<const_name>
        if (check(TokenType::IDENTIFIER) && peek().lexeme == "c" && !g_cNameIsVariable &&
            peekAt(1).type == TokenType::DOT && peekAt(2).type == TokenType::IDENTIFIER) {
            advance(); // Skip "c"
            advance(); // Skip "."
            std::string cItem = advance().lexeme;
            if (match(TokenType::LPAREN)) {
                std::vector<ASTPtr> args;
                if (!check(TokenType::RPAREN)) {
                    do { args.push_back(expression()); } while (match(TokenType::COMMA));
                }
                if (!match(TokenType::RPAREN)) {
                    std::cerr << "Parser Error: Missing ')' after c." << cItem << " at line " << peek().line << "\n";
                    exit(1);
                }
                return std::make_unique<CCallNode>(cItem, std::move(args));
            } else {
                return std::make_unique<CVarNode>(cItem);
            }
        }

        // Parse built-in random calls (rand, randint, randres) and len
        if (check(TokenType::IDENTIFIER)) {
            std::string name = peek().lexeme;
            if (name == "len") {
                advance(); // Skip "len"
                if (!match(TokenType::LPAREN)) {
                    std::cerr << "Parser Error: Missing '(' after 'len' at line " << peek().line << "\n";
                    exit(1);
                }
                ASTPtr arg = expression();
                if (!match(TokenType::RPAREN)) {
                    std::cerr << "Parser Error: Missing ')' after len(...) expression at line " << peek().line << "\n";
                    exit(1);
                }
                return std::make_unique<LenNode>(std::move(arg));
            }
            if (name == "to_int" || name == "to_str" || name == "to_float" || name == "to_double") {
                advance(); // Skip function name
                if (!match(TokenType::LPAREN)) {
                    std::cerr << "Parser Error: Missing '(' after '" << name << "' at line " << peek().line << "\n";
                    exit(1);
                }
                ASTPtr arg = expression();
                if (!match(TokenType::RPAREN)) {
                    std::cerr << "Parser Error: Missing ')' after expression " << name << "(...) at line " << peek().line << "\n";
                    exit(1);
                }
                ThavaType targetType = ThavaType::INT;
                if (name == "to_str") targetType = ThavaType::STRING;
                else if (name == "to_float") targetType = ThavaType::FLOAT;
                else if (name == "to_double") targetType = ThavaType::DOUBLE;
                return std::make_unique<CastNode>(targetType, std::move(arg));
            }
            if (name == "rand" || name == "randint" || name == "randres") {
                advance(); // Skip function name
                if (match(TokenType::LPAREN)) {
                    std::vector<ASTPtr> args;
                    ASTPtr weightExpr = nullptr;
                    if (!check(TokenType::RPAREN)) {
                        do {
                            // randres() supports named argument: weight=[...]
                            if (name == "randres" && check(TokenType::IDENTIFIER) && peek().lexeme == "weight" &&
                                peekAt(1).type == TokenType::ASSIGN) {
                                advance(); // Skip "weight"
                                advance(); // Skip "="
                                weightExpr = expression();
                            } else {
                                args.push_back(expression());
                            }
                        } while (match(TokenType::COMMA));
                    }
                    if (!match(TokenType::RPAREN)) {
                        std::cerr << "Parser Error: Missing closing ')' in function call " << name << "!\n";
                        exit(1);
                    }
                    return std::make_unique<RandCallNode>(name, std::move(args), std::move(weightExpr));
                }
                // If no '(', backtrack as standard variable
                current--; 
            }

            // Dang rieng: key.input.upper(<str> "...") / key.input.lower(<str> "...") / key.input.title(<str> "...")
            if (name == "key.input.upper" || name == "key.input.lower" || name == "key.input.title") {
                std::string mode = name.substr(std::string("key.input.").size());
                advance();
                if (!match(TokenType::LPAREN)) {
                    std::cerr << "Parser Error: Missing '(' after " << name << " at line " << peek().line << "\n";
                    exit(1);
                }
                if (!match(TokenType::TYPE_STR)) {
                    std::cerr << "Parser Error: " << name << " only supports <str> type at line " << peek().line << "\n";
                    exit(1);
                }
                match(TokenType::COMMA); // docs use key.input.upper(<str>, "prompt")
                if (!check(TokenType::STR_LIT)) {
                    std::cerr << "Parser Error: " << name << " requires prompt string at line " << peek().line << "\n";
                    exit(1);
                }
                auto prompt = parseStringLiteral(advance());
                if (!match(TokenType::RPAREN)) {
                    std::cerr << "Parser Error: Missing ')' after " << name << " at line " << peek().line << "\n";
                    exit(1);
                }
                return std::make_unique<InputCaseNode>(std::move(prompt), mode);
            }

            // Struct instantiation: StructName(args...)
            if (g_structs.count(name) && peekAt(1).type == TokenType::LPAREN) {
                int ctorLine = peek().line;
                advance(); // Skip struct name
                advance(); // Skip '('
                std::vector<ASTPtr> args;
                if (!check(TokenType::RPAREN)) {
                    do { args.push_back(expression()); } while (match(TokenType::COMMA));
                }
                if (!match(TokenType::RPAREN)) {
                    std::cerr << "Parser Error: Missing ')' in struct instantiation '" << name << "' at line " << peek().line << "\n";
                    exit(1);
                }
                return std::make_unique<StructInstantiateNode>(name, std::move(args), ctorLine);
            }

            // Class instantiation: ClassName(args...)
            if (g_classNames.count(name) && peekAt(1).type == TokenType::LPAREN) {
                int ctorLine = peek().line;
                advance(); // Skip class name
                advance(); // Skip '('
                std::vector<ASTPtr> args;
                if (!check(TokenType::RPAREN)) {
                    do { args.push_back(expression()); } while (match(TokenType::COMMA));
                }
                if (!match(TokenType::RPAREN)) {
                    std::cerr << "Parser Error: Missing ')' in class instantiation '" << name << "' at line " << peek().line << "\n";
                    exit(1);
                }
                return std::make_unique<ClassInstantiateNode>(name, std::move(args), ctorLine);
            }

            // Call user-defined function or callback/function pointer: func_name(a, b, ...)
            if (peekAt(1).type == TokenType::LPAREN) {
                advance(); // Skip function name or callback variable
                advance(); // Skip '('
                std::vector<ASTPtr> args;
                if (!check(TokenType::RPAREN)) {
                    do { args.push_back(expression()); } while (match(TokenType::COMMA));
                }
                if (!match(TokenType::RPAREN)) {
                    std::cerr << "Parser Error: Missing ')' after function call " << name << " at line " << peek().line << "\n";
                    exit(1);
                }
                if (g_funcReturnTypes.count(name)) {
                    size_t totalExpected = g_funcParamTypes[name].size();
                    size_t minExpected = g_funcMinArgs.count(name) ? g_funcMinArgs[name] : totalExpected;
                    if (args.size() < minExpected || args.size() > totalExpected) {
                        std::cerr << "Parser Error: Function '" << name << "' expects between " << minExpected << " and " << totalExpected
                                  << " arguments, but called with " << args.size() << " arguments, at line " << peek().line << "\n";
                        exit(1);
                    }
                    // Supply missing default arguments
                    for (size_t i = args.size(); i < totalExpected; ++i) {
                        if (g_funcDefaultArgTokens.count(name) && i < g_funcDefaultArgTokens[name].size()) {
                            const auto& defTokens = g_funcDefaultArgTokens[name][i];
                            Parser defParser(defTokens);
                            args.push_back(defParser.parseExpressionPublic());
                        }
                    }
                    return std::make_unique<CallNode>(name, std::move(args), g_funcReturnTypes[name]);
                }
                // If not in g_funcReturnTypes, this is an indirect call via callback variable
                return std::make_unique<CallNode>(name, std::move(args), ThavaType::UNKNOWN);
            }
        }

        if (peek().type == TokenType::IDENTIFIER) {
            std::string idName = peek().lexeme;
            if (peekAt(1).type != TokenType::LPAREN && peekAt(1).type != TokenType::DOT) {
                if (idName == "IDOK") { advance(); return std::make_unique<LiteralNode>("1", ThavaType::INT); }
                if (idName == "IDCANCEL") { advance(); return std::make_unique<LiteralNode>("2", ThavaType::INT); }
                if (idName == "IDABORT") { advance(); return std::make_unique<LiteralNode>("3", ThavaType::INT); }
                if (idName == "IDRETRY") { advance(); return std::make_unique<LiteralNode>("4", ThavaType::INT); }
                if (idName == "IDIGNORE") { advance(); return std::make_unique<LiteralNode>("5", ThavaType::INT); }
                if (idName == "IDYES") { advance(); return std::make_unique<LiteralNode>("6", ThavaType::INT); }
                if (idName == "IDNO") { advance(); return std::make_unique<LiteralNode>("7", ThavaType::INT); }
                // Icon constants
                if (idName == "Warning") { advance(); return std::make_unique<LiteralNode>("48", ThavaType::INT); }
                if (idName == "Information" || idName == "Infomation") { advance(); return std::make_unique<LiteralNode>("64", ThavaType::INT); }
                if (idName == "Question") { advance(); return std::make_unique<LiteralNode>("32", ThavaType::INT); }
                if (idName == "Error") { advance(); return std::make_unique<LiteralNode>("16", ThavaType::INT); }
                if (idName == "None") { advance(); return std::make_unique<LiteralNode>("0", ThavaType::INT); }
                // Button constants
                if (idName == "OK") { advance(); return std::make_unique<LiteralNode>("0", ThavaType::INT); }
                if (idName == "OKCancel") { advance(); return std::make_unique<LiteralNode>("1", ThavaType::INT); }
                if (idName == "YesNoCancel") { advance(); return std::make_unique<LiteralNode>("3", ThavaType::INT); }
                if (idName == "YesNo") { advance(); return std::make_unique<LiteralNode>("4", ThavaType::INT); }
                if (idName == "RetryCancel") { advance(); return std::make_unique<LiteralNode>("5", ThavaType::INT); }
            }
        }

        if (match(TokenType::IDENTIFIER)) return std::make_unique<VarNode>(tokens[current-1].lexeme);
        
        if (match(TokenType::KEY_INPUT)) {
            if (!match(TokenType::LPAREN)) { std::cerr << "Parser Error: Missing '(' after key.input at line " << peek().line << "\n"; exit(1); }

            ThavaType inputType = ThavaType::UNKNOWN;
            if (match(TokenType::TYPE_INT)) inputType = ThavaType::INT;
            else if (match(TokenType::TYPE_INT64)) inputType = ThavaType::INT;
            else if (match(TokenType::TYPE_INT16)) inputType = ThavaType::INT;
            else if (match(TokenType::TYPE_FL)) inputType = ThavaType::FLOAT;
            else if (match(TokenType::TYPE_DBL)) inputType = ThavaType::DOUBLE;
            else if (match(TokenType::TYPE_BL)) inputType = ThavaType::BOOL;
            else if (match(TokenType::TYPE_STR)) inputType = ThavaType::STRING;
            else { std::cerr << "Parser Error: key.input requires type specification <type> at line " << peek().line << "\n"; exit(1); }

            match(TokenType::COMMA); // docs use key.input(<int>, "prompt")
            if (!check(TokenType::STR_LIT)) { std::cerr << "Parser Error: key.input requires prompt string at line " << peek().line << "\n"; exit(1); }
            auto prompt = parseStringLiteral(advance());

            if (!match(TokenType::RPAREN)) { std::cerr << "Parser Error: Missing ')' after key.input at line " << peek().line << "\n"; exit(1); }

            return std::make_unique<InputNode>(inputType, std::move(prompt));
        }

        if (match(TokenType::LPAREN)) {
            ASTPtr expr = expression();
            if (!match(TokenType::RPAREN)) {
                std::cerr << "Parser Error: Missing closing ')'!\n";
                exit(1);
            }
            return expr;
        }

        if (match(TokenType::LBRACKET)) {
            std::vector<ASTPtr> elements;
            if (!check(TokenType::RBRACKET)) {
                do { elements.push_back(expression()); } while (match(TokenType::COMMA));
            }
            if (!match(TokenType::RBRACKET)) { std::cerr << "Parser Error: Missing ']'\n"; exit(1); }
            return std::make_unique<CollectionNode>(std::move(elements), false);
        }

        if (match(TokenType::LBRACE)) {
            // 1. Empty {} -> empty Dictionary / Map
            if (match(TokenType::RBRACE)) {
                g_hasDict = true;
                return std::make_unique<DictNode>();
            }

            ASTPtr firstExpr = expression();
            if (match(TokenType::COLON)) {
                g_hasDict = true;
                // Day la Dictionary / Map literal: { key: val, ... }
                ASTPtr firstVal = expression();
                std::vector<std::pair<ASTPtr, ASTPtr>> entries;
                entries.push_back({std::move(firstExpr), std::move(firstVal)});
                while (match(TokenType::COMMA)) {
                    if (check(TokenType::RBRACE)) break; // Support trailing comma
                    ASTPtr k = expression();
                    if (!match(TokenType::COLON)) {
                        std::cerr << "Parser Error: Missing ':' between key and value in dictionary at line " << peek().line << "\n";
                        exit(1);
                    }
                    ASTPtr v = expression();
                    entries.push_back({std::move(k), std::move(v)});
                }
                if (!match(TokenType::RBRACE)) {
                    std::cerr << "Parser Error: Missing '}' closing dictionary at line " << peek().line << "\n";
                    exit(1);
                }
                return std::make_unique<DictNode>(std::move(entries));
            } else {
                // Legacy tuple: { elem1, elem2, ... }
                std::vector<ASTPtr> elements;
                elements.push_back(std::move(firstExpr));
                while (match(TokenType::COMMA)) {
                    if (check(TokenType::RBRACE)) break;
                    elements.push_back(expression());
                }
                if (!match(TokenType::RBRACE)) { std::cerr << "Parser Error: Missing '}' at line " << peek().line << "\n"; exit(1); }
                return std::make_unique<CollectionNode>(std::move(elements), true);
            }
        }

        std::cerr << "Parser Error: Invalid syntax near token '" << peek().lexeme << "'\n";
        exit(1);
    }

    ASTPtr postfix() {
        ASTPtr expr = primary();
        while (true) {
            if (check(TokenType::LBRACKET)) {
                advance(); // Skip '['
                ASTPtr idx = expression();
                if (!match(TokenType::RBRACKET)) {
                    std::cerr << "Parser Error: Missing ']' in index access at line " << peek().line << "\n";
                    exit(1);
                }
                expr = std::make_unique<IndexNode>(std::move(expr), std::move(idx));
            } else if (check(TokenType::DOT)) {
                advance(); // Skip '.'
                int memberLine = peek().line;
                if (!check(TokenType::IDENTIFIER)) {
                    std::cerr << "Parser Error: Expected method name after '.' at line " << peek().line << "\n";
                    exit(1);
                }
                std::string memberName = advance().lexeme;
                auto* osVarNode = dynamic_cast<VarNode*>(expr.get());
                if (osVarNode && osVarNode->getName() == "os") {
                    if (match(TokenType::LPAREN)) {
                        std::vector<ASTPtr> args;
                        if (!check(TokenType::RPAREN)) {
                            do { args.push_back(expression()); } while (match(TokenType::COMMA));
                        }
                        if (!match(TokenType::RPAREN)) {
                            std::cerr << "Parser Error: Missing ')' after os." << memberName << " at line " << peek().line << "\n";
                            exit(1);
                        }
                        expr = std::make_unique<OsMethodNode>(memberName, std::move(args), memberLine);
                    } else {
                        if (memberName == "IDOK") expr = std::make_unique<LiteralNode>("1", ThavaType::INT);
                        else if (memberName == "IDCANCEL") expr = std::make_unique<LiteralNode>("2", ThavaType::INT);
                        else if (memberName == "IDABORT") expr = std::make_unique<LiteralNode>("3", ThavaType::INT);
                        else if (memberName == "IDRETRY") expr = std::make_unique<LiteralNode>("4", ThavaType::INT);
                        else if (memberName == "IDIGNORE") expr = std::make_unique<LiteralNode>("5", ThavaType::INT);
                        else if (memberName == "IDYES") expr = std::make_unique<LiteralNode>("6", ThavaType::INT);
                        else if (memberName == "IDNO") expr = std::make_unique<LiteralNode>("7", ThavaType::INT);
                        else if (memberName == "Warning") expr = std::make_unique<LiteralNode>("48", ThavaType::INT);
                        else if (memberName == "Information" || memberName == "Infomation") expr = std::make_unique<LiteralNode>("64", ThavaType::INT);
                        else if (memberName == "Question") expr = std::make_unique<LiteralNode>("32", ThavaType::INT);
                        else if (memberName == "Error") expr = std::make_unique<LiteralNode>("16", ThavaType::INT);
                        else if (memberName == "None") expr = std::make_unique<LiteralNode>("0", ThavaType::INT);
                        else if (memberName == "OK") expr = std::make_unique<LiteralNode>("0", ThavaType::INT);
                        else if (memberName == "OKCancel") expr = std::make_unique<LiteralNode>("1", ThavaType::INT);
                        else if (memberName == "YesNoCancel") expr = std::make_unique<LiteralNode>("3", ThavaType::INT);
                        else if (memberName == "YesNo") expr = std::make_unique<LiteralNode>("4", ThavaType::INT);
                        else if (memberName == "RetryCancel") expr = std::make_unique<LiteralNode>("5", ThavaType::INT);
                        else if (memberName == "name") expr = std::make_unique<OsMethodNode>("name", std::vector<ASTPtr>{}, memberLine);
                        else {
                            std::cerr << "Parser Error: Property or constant 'os." << memberName << "' is unknown at line " << memberLine << "\n";
                            exit(1);
                        }
                    }
                    continue;
                }

                auto* timeVarNode = dynamic_cast<VarNode*>(expr.get());
                if (timeVarNode && timeVarNode->getName() == "time") {
                    std::vector<ASTPtr> args;
                    if (match(TokenType::LPAREN)) {
                        if (!check(TokenType::RPAREN)) {
                            do { args.push_back(expression()); } while (match(TokenType::COMMA));
                        }
                        if (!match(TokenType::RPAREN)) {
                            std::cerr << "Parser Error: Missing ')' after time." << memberName << " at line " << peek().line << "\n";
                            exit(1);
                        }
                    }
                    expr = std::make_unique<TimeMethodNode>(memberName, std::move(args), memberLine);
                    continue;
                }

                auto* clibVarNode = dynamic_cast<VarNode*>(expr.get());
                if (clibVarNode && clibVarNode->getName() == "clib") {
                    std::vector<ASTPtr> args;
                    if (match(TokenType::LPAREN)) {
                        if (!check(TokenType::RPAREN)) {
                            do { args.push_back(expression()); } while (match(TokenType::COMMA));
                        }
                        if (!match(TokenType::RPAREN)) {
                            std::cerr << "Parser Error: Missing ')' after clib." << memberName << " at line " << peek().line << "\n";
                            exit(1);
                        }
                    }
                    g_hasCLib = true;
                    expr = std::make_unique<CLibMethodNode>(memberName, nullptr, std::move(args), memberLine);
                    continue;
                }

                if (match(TokenType::LPAREN)) {
                    if (memberName == "call" || memberName == "call_str" || memberName == "call_dbl" ||
                        memberName == "free") {
                        std::vector<ASTPtr> args;
                        if (!check(TokenType::RPAREN)) {
                            do { args.push_back(expression()); } while (match(TokenType::COMMA));
                        }
                        if (!match(TokenType::RPAREN)) {
                            std::cerr << "Parser Error: Missing ')' after ." << memberName << "() at line " << peek().line << "\n";
                            exit(1);
                        }
                        g_hasCLib = true;
                        expr = std::make_unique<CLibMethodNode>(memberName, std::move(expr), std::move(args), memberLine);
                    }
                    else if (memberName == "to_int" || memberName == "to_str" || memberName == "to_float" || memberName == "to_double") {
                        if (!match(TokenType::RPAREN)) {
                            std::cerr << "Parser Error: '." << memberName << "()' takes no arguments at line " << peek().line << "\n";
                            exit(1);
                        }
                        ThavaType targetType = ThavaType::INT;
                        if (memberName == "to_str") targetType = ThavaType::STRING;
                        else if (memberName == "to_float") targetType = ThavaType::FLOAT;
                        else if (memberName == "to_double") targetType = ThavaType::DOUBLE;
                        expr = std::make_unique<CastNode>(targetType, std::move(expr));
                    }
                    else if (memberName == "lower" || memberName == "upper" || memberName == "title" || memberName == "strip") {
                        if (!match(TokenType::RPAREN)) {
                            std::cerr << "Parser Error: '." << memberName << "()' khong nhan arguments, at line " << peek().line << "\n";
                            exit(1);
                        }
                        expr = std::make_unique<StrMethodNode>(std::move(expr), memberName);
                    }
                    else if (memberName == "len" || memberName == "size") {
                        if (!match(TokenType::RPAREN)) {
                            std::cerr << "Parser Error: '." << memberName << "()' khong nhan arguments, at line " << peek().line << "\n";
                            exit(1);
                        }
                        expr = std::make_unique<LenNode>(std::move(expr));
                    }
                    else if (memberName == "keys" || memberName == "values" || memberName == "items" ||
                             memberName == "clear" || memberName == "get" || memberName == "has" ||
                             memberName == "contains" || memberName == "remove" || memberName == "del") {
                        std::vector<ASTPtr> args;
                        if (!check(TokenType::RPAREN)) {
                            do { args.push_back(expression()); } while (match(TokenType::COMMA));
                        }
                        if (!match(TokenType::RPAREN)) {
                            std::cerr << "Parser Error: Missing ')' after ." << memberName << "() at line " << peek().line << "\n";
                            exit(1);
                        }
                        g_hasDict = true;
                        expr = std::make_unique<DictMethodNode>(std::move(expr), memberName, std::move(args), memberLine);
                    }
                    else if (memberName == "replace" || memberName == "find" || memberName == "index" ||
                             memberName == "startswith" || memberName == "endswith" ||
                             memberName == "count" || memberName == "join") {
                        std::vector<ASTPtr> args;
                        if (!check(TokenType::RPAREN)) {
                            do { args.push_back(expression()); } while (match(TokenType::COMMA));
                        }
                        if (!match(TokenType::RPAREN)) {
                            std::cerr << "Parser Error: Missing ')' after ." << memberName << "() at line " << peek().line << "\n";
                            exit(1);
                        }
                        expr = std::make_unique<StrAdvMethodNode>(std::move(expr), memberName, std::move(args), memberLine);
                    }
                    else if (memberName == "split") {
                        std::vector<ASTPtr> args;
                        if (!check(TokenType::RPAREN)) {
                            ASTPtr firstArg = expression();
                            if (match(TokenType::COMMA)) {
                                ASTPtr idxArg = expression();
                                if (!match(TokenType::RPAREN)) {
                                    std::cerr << "Parser Error: Missing ')' after .split() at line " << peek().line << "\n";
                                    exit(1);
                                }
                                auto coll = dynamic_cast<CollectionNode*>(idxArg.get());
                                if (!coll || coll->getIsTuple() || coll->getElements().empty() || coll->getElements().size() > 2) {
                                    std::cerr << "Parser Error: .split() requires index in form [a] or [a, b] at line " << peek().line << "\n";
                                    exit(1);
                                }
                                std::string aCode = coll->getElements()[0]->generateCode();
                                std::string bCode = coll->getElements().size() == 2 ? coll->getElements()[1]->generateCode() : aCode;
                                expr = std::make_unique<StrSplitNode>(std::move(expr), std::move(firstArg), aCode, bCode);
                                continue;
                            } else {
                                args.push_back(std::move(firstArg));
                            }
                        }
                        if (!match(TokenType::RPAREN)) {
                            std::cerr << "Parser Error: Missing ')' after .split() at line " << peek().line << "\n";
                            exit(1);
                        }
                        if (args.empty()) {
                            args.push_back(std::make_unique<LiteralNode>("\" \"", ThavaType::STRING));
                        }
                        expr = std::make_unique<StrAdvMethodNode>(std::move(expr), "split", std::move(args), memberLine);
                    }
                    else if (memberName == "getsubstring") {
                        ASTPtr startExpr = expression();
                        if (!match(TokenType::COMMA)) {
                            std::cerr << "Parser Error: .getsubstring() requires 2 arguments (start, end) at line " << peek().line << "\n";
                            exit(1);
                        }
                        ASTPtr endExpr = expression();
                        if (!match(TokenType::RPAREN)) {
                            std::cerr << "Parser Error: Missing ')' after .getsubstring() at line " << peek().line << "\n";
                            exit(1);
                        }
                        expr = std::make_unique<StrSubstringNode>(std::move(expr), std::move(startExpr), std::move(endExpr));
                    }
                    else if (memberName == "addval" || memberName == "removeval" || memberName == "assign" || memberName == "delval") {
                        auto* varNode = dynamic_cast<VarNode*>(expr.get());
                        if (!varNode) {
                            std::cerr << "Parser Error: ." << memberName << "() can only be called directly on list/tuple variable names, "
                                      << "cannot be used on result of an expression, at line " << peek().line << "\n";
                            exit(1);
                        }
                        std::string varName = varNode->getName();
                        std::vector<ASTPtr> args;
                        if (!check(TokenType::RPAREN)) {
                            do { args.push_back(expression()); } while (match(TokenType::COMMA));
                        }
                        if (!match(TokenType::RPAREN)) {
                            std::cerr << "Parser Error: Missing ')' after ." << memberName << " at line " << peek().line << "\n";
                            exit(1);
                        }
                        expr = std::make_unique<ListMethodNode>(varName, memberName, std::move(args));
                    }
                    else if (memberName == "read" || memberName == "read_line" || memberName == "readline" ||
                             memberName == "read_lines" || memberName == "readlines" ||
                             memberName == "write" || memberName == "write_line" || memberName == "writeline") {
                        auto* varNode = dynamic_cast<VarNode*>(expr.get());
                        if (!varNode) {
                            std::cerr << "Parser Error: ." << memberName << "() can only be called directly on file variables, "
                                      << "cannot be used on result of an expression, at line " << peek().line << "\n";
                            exit(1);
                        }
                        std::string varName = varNode->getName();
                        std::vector<ASTPtr> args;
                        if (!check(TokenType::RPAREN)) {
                            do { args.push_back(expression()); } while (match(TokenType::COMMA));
                        }
                        if (!match(TokenType::RPAREN)) {
                            std::cerr << "Parser Error: Missing ')' after ." << memberName << " at line " << peek().line << "\n";
                            exit(1);
                        }
                        expr = std::make_unique<FileMethodNode>(varName, memberName, std::move(args));
                    }
                    else {
                        // Class method: obj.method(args...) or locvar.method(args...)
                        std::vector<ASTPtr> args;
                        if (!check(TokenType::RPAREN)) {
                            do { args.push_back(expression()); } while (match(TokenType::COMMA));
                        }
                        if (!match(TokenType::RPAREN)) {
                            std::cerr << "Parser Error: Missing ')' after ." << memberName << " at line " << peek().line << "\n";
                            exit(1);
                        }
                        expr = std::make_unique<MethodCallNode>(std::move(expr), memberName, std::move(args), memberLine);
                    }
                } else {
                    auto* enumVarNode = dynamic_cast<VarNode*>(expr.get());
                    if (enumVarNode && g_enums.count(enumVarNode->getName())) {
                        std::string enumName = enumVarNode->getName();
                        const auto& einfo = g_enums[enumName];
                        auto mit = einfo.members.find(memberName);
                        if (mit == einfo.members.end()) {
                            std::cerr << "Parser Error: Enum '" << enumName << "' has no member '" << memberName << "' at line " << memberLine << "\n";
                            exit(1);
                        }
                        expr = std::make_unique<LiteralNode>(std::to_string(mit->second), ThavaType::INT);
                    } else {
                        // Property access of Class or Struct: obj.field or locvar.field
                        expr = std::make_unique<MemberAccessNode>(std::move(expr), memberName, memberLine);
                    }
                }
            } else {
                break;
            }
        }
        return expr;
    }

    ASTPtr unary() {
        // Explicit type cast syntax: <type> expression
        if (isTypeToken(peek().type) && peek().type != TokenType::TYPE_VOID && peek().type != TokenType::TYPE_CUSTOM) {
            std::string castWidth;
            ThavaType targetType = parseType(&castWidth);
            ASTPtr expr = unary();
            return std::make_unique<CastNode>(targetType, std::move(expr), castWidth);
        }
        if (check(TokenType::NOT)) {
            advance();
            return std::make_unique<UnaryNotNode>(unary());
        }
        if (check(TokenType::BNOT)) {
            advance();
            return std::make_unique<UnaryBNotNode>(unary());
        }
        if (check(TokenType::MINUS)) {
            advance();
            return std::make_unique<UnaryMinusNode>(unary());
        }
        if (check(TokenType::PLUS)) {
            advance();
            return unary();
        }
        return postfix();
    }

    ASTPtr power() {
        ASTPtr expr = unary();
        if (check(TokenType::POW)) {
            advance();
            expr = std::make_unique<BinaryOpNode>(std::move(expr), "**", power());
        }
        return expr;
    }

    ASTPtr multiplication() {
        ASTPtr expr = power();
        while (check(TokenType::STAR) || check(TokenType::SLASH) || check(TokenType::MODULO) || check(TokenType::FLOOR_DIV)) {
            Token op = advance();
            expr = std::make_unique<BinaryOpNode>(std::move(expr), op.lexeme, power());
        }
        return expr;
    }

    ASTPtr addition() {
        ASTPtr expr = multiplication();
        while (check(TokenType::PLUS) || check(TokenType::MINUS)) {
            Token op = advance();
            expr = std::make_unique<BinaryOpNode>(std::move(expr), op.lexeme, multiplication());
        }
        return expr;
    }

    ASTPtr bitShift() {
        ASTPtr expr = addition();
        while (check(TokenType::BSHL) || check(TokenType::BSHR)) {
            Token op = advance();
            expr = std::make_unique<BinaryOpNode>(std::move(expr), op.lexeme, addition());
        }
        return expr;
    }

    ASTPtr bitAnd() {
        ASTPtr expr = bitShift();
        while (check(TokenType::BAND)) {
            Token op = advance();
            expr = std::make_unique<BinaryOpNode>(std::move(expr), op.lexeme, bitShift());
        }
        return expr;
    }

    ASTPtr bitXor() {
        ASTPtr expr = bitAnd();
        while (check(TokenType::BXOR)) {
            Token op = advance();
            expr = std::make_unique<BinaryOpNode>(std::move(expr), op.lexeme, bitAnd());
        }
        return expr;
    }

    ASTPtr bitOr() {
        ASTPtr expr = bitXor();
        while (check(TokenType::BOR)) {
            Token op = advance();
            expr = std::make_unique<BinaryOpNode>(std::move(expr), op.lexeme, bitXor());
        }
        return expr;
    }

    ASTPtr relational() {
        ASTPtr expr = bitOr();
        while (check(TokenType::LESS) || check(TokenType::LESS_EQ) ||
               check(TokenType::GREATER) || check(TokenType::GREATER_EQ)) {
            Token op = advance();
            expr = std::make_unique<BinaryOpNode>(std::move(expr), op.lexeme, bitOr());
        }
        return expr;
    }

    ASTPtr comparison() {
        ASTPtr expr = relational();
        while (check(TokenType::EQ) || check(TokenType::NOT_EQ) || check(TokenType::IN)) {
            if (check(TokenType::IN)) {
                advance();
                expr = std::make_unique<InNode>(std::move(expr), relational());
            } else {
                Token op = advance();
                expr = std::make_unique<BinaryOpNode>(std::move(expr), op.lexeme, relational());
            }
        }
        return expr;
    }

    ASTPtr logicalAnd() {
        ASTPtr expr = comparison();
        while (check(TokenType::AND)) {
            Token op = advance();
            expr = std::make_unique<BinaryOpNode>(std::move(expr), op.lexeme, comparison());
        }
        return expr;
    }

    ASTPtr logicalOr() {
        ASTPtr expr = logicalAnd();
        while (check(TokenType::OR)) {
            Token op = advance();
            expr = std::make_unique<BinaryOpNode>(std::move(expr), op.lexeme, logicalAnd());
        }
        return expr;
    }

    ASTPtr conditional() {
        ASTPtr expr = logicalOr();
        if (check(TokenType::IF) && hasElseBeforeBrace(current + 1)) {
            advance();
            ASTPtr cond = logicalOr();
            if (!match(TokenType::ELSE)) {
                std::cerr << "Parser Error: Missing 'else' in conditional expression at line " << peek().line << "\n";
                exit(1);
            }
            ASTPtr falseExpr = conditional();
            return std::make_unique<ConditionalExprNode>(std::move(cond), std::move(expr), std::move(falseExpr));
        }
        return expr;
    }

    ASTPtr expression() { 
        return conditional(); 
    }

    ASTPtr varDeclStatement() {
        std::string customTypeName = "";
        ThavaType targetType = parseType(&customTypeName);
        if (targetType == ThavaType::VOID || targetType == ThavaType::UNKNOWN) {
            std::cerr << "Parser Error: Cannot declare variable of type <void> at line " << peek().line << "\n";
            exit(1);
        }

        // Treat const as a modifier in a variable declaration. Keep existing
        // declarations such as <int> const = 1 valid for native backends.
        const bool isConst = check(TokenType::IDENTIFIER) && peek().lexeme == "const" &&
                             peekAt(1).type != TokenType::ASSIGN;
        if (isConst) advance();
        if (!check(TokenType::IDENTIFIER)) { std::cerr << "Parser Error: Missing variable name at line " << peek().line << "\n"; exit(1); }
        std::string name = advance().lexeme;

        ASTPtr expr = nullptr;
        if (match(TokenType::ASSIGN)) {
            expr = expression();
        } else if (isConst) {
            std::cerr << "Parser Error: Const variable '" << name << "' requires an initializer at line "
                      << tokens[current - 1].line << "\n";
            exit(1);
        } else if (targetType != ThavaType::CUSTOM) {
            std::cerr << "Parser Error: Missing '=' in assignment at line " << peek().line << "\n";
            exit(1);
        }

        return std::make_unique<VarDeclNode>(targetType, name, std::move(expr), customTypeName, isConst);
    }

    ASTPtr assignOrExprStatement() {
        // 0. Array/list element assignment by index: arr[i] = expr
        if (check(TokenType::IDENTIFIER) && peekAt(1).type == TokenType::LBRACKET) {
            // Tim dau ']' tuong ung
            size_t depth = 0;
            size_t k = current + 1; // vi tri '['
            bool foundClose = false;
            while (k < tokens.size()) {
                if (tokens[k].type == TokenType::LBRACKET) depth++;
                else if (tokens[k].type == TokenType::RBRACKET) {
                    depth--;
                    if (depth == 0) {
                        foundClose = true;
                        break;
                    }
                }
                k++;
            }
            if (foundClose && k + 1 < tokens.size() && isAssignOp(tokens[k + 1].type)) {
                std::string varName = advance().lexeme;
                advance(); // Skip '['
                ASTPtr idxExpr = expression();
                if (!match(TokenType::RBRACKET)) {
                    std::cerr << "Parser Error: Missing ']' in element assignment '" << varName << "[...]' at line " << peek().line << "\n";
                    exit(1);
                }
                std::string op = advance().lexeme; // Skip assignment operator (=, +=, -=, ...)
                ASTPtr valExpr = expression();
                return std::make_unique<IndexAssignNode>(varName, std::move(idxExpr), std::move(valExpr), op);
            }
        }

        // 1. Gan thuoc tinh: locvar.field = expr hoac parvar.field = expr hoac obj.field = expr hoac obj.sub.field = expr
        if (peek().type == TokenType::LOCVAR || peek().type == TokenType::PARVAR || peek().type == TokenType::IDENTIFIER) {
            size_t look = 1;
            while (peekAt(look).type == TokenType::DOT && peekAt(look + 1).type == TokenType::IDENTIFIER) {
                look += 2;
            }
            if (look > 1 && isAssignOp(peekAt(look).type)) {
                if (peek().type == TokenType::IDENTIFIER && g_enums.count(peek().lexeme)) {
                    std::cerr << "Parser Error: Cannot assign value to enum member '" << peek().lexeme << "' at line " << peek().line << "\n";
                    exit(1);
                }
                ASTPtr target = nullptr;
                if (peek().type == TokenType::LOCVAR) {
                    advance();
                    target = std::make_unique<LocVarNode>();
                } else if (peek().type == TokenType::PARVAR) {
                    advance();
                    if (currentParsingBaseClass.empty()) {
                        std::cerr << "Parser Error: 'parvar' can only be used inside subclasses inheriting from a parent class at line " << peek().line << "\n";
                        exit(1);
                    }
                    target = std::make_unique<ParVarNode>(currentParsingBaseClass);
                } else {
                    target = std::make_unique<VarNode>(advance().lexeme);
                }
                while (check(TokenType::DOT) && peekAt(2).type == TokenType::DOT) {
                    advance(); // Skip '.'
                    int subLine = peek().line;
                    std::string subField = advance().lexeme;
                    target = std::make_unique<MemberAccessNode>(std::move(target), subField, subLine);
                }
                advance(); // Skip '.' truoc field cuoi cung
                int lastLine = peek().line;
                std::string lastField = advance().lexeme;
                std::string op = advance().lexeme; // Skip assignment operator (=, +=, -=, ...)
                ASTPtr valExpr = expression();
                return std::make_unique<MemberAssignNode>(std::move(target), lastField, std::move(valExpr), lastLine, op);
            }
        }

        // 2. Simple assignment: x = expr, x += expr, ...
        if (check(TokenType::IDENTIFIER) && current + 1 < tokens.size() && isAssignOp(tokens[current + 1].type)) {
            std::string name = advance().lexeme;
            std::string op = advance().lexeme; // Skip assignment operator (=, +=, -=, ...)
            ASTPtr expr = expression();
            return std::make_unique<AssignNode>(name, std::move(expr), op);
        }
        return std::make_unique<ExpressionStatementNode>(expression());
    }

    ASTPtr consClearStatement() {
        advance(); // Skip 'cons.clear'
        if (match(TokenType::LPAREN)) {
            if (!match(TokenType::RPAREN)) {
                std::cerr << "Parser Error: cons.clear() does not take any argument at line " << peek().line << "\n";
                exit(1);
            }
        }
        return std::make_unique<ConsClearNode>();
    }

    ASTPtr consOutStatement() {
        advance(); 
        if (!match(TokenType::LPAREN)) { std::cerr << "Parser Error: Missing '(' in cons.out at line " << peek().line << "\n"; exit(1); }

        ASTPtr expr = expression();

        if (!match(TokenType::RPAREN)) { std::cerr << "Parser Error: Missing closing ')' in cons.out at line " << peek().line << "\n"; exit(1); }

        return std::make_unique<ConsOutNode>(std::move(expr));
    }

    ASTPtr ifStatement() {
        ASTPtr cond = expression();
        if (!check(TokenType::LBRACE)) { 
            std::cerr << "Parser Error: Expected '{' after 'if' condition at line " << peek().line << "\n"; 
            exit(1); 
        }
        ASTPtr thenBranch = statement();
        
        ASTPtr elseBranch = nullptr;
        if (match(TokenType::ELSE)) {
            elseBranch = statement();
        } else if (match(TokenType::ELIF)) {
            elseBranch = ifStatement();
        }
        return std::make_unique<IfNode>(std::move(cond), std::move(thenBranch), std::move(elseBranch));
    }

    ASTPtr forStatement() {
        // Parse loop header: <type> name = ... OR <type> name in ... OR name in ...
        ThavaType targetType = ThavaType::UNKNOWN;
        bool hasType = false;
        if (match(TokenType::TYPE_INT)) { targetType = ThavaType::INT; hasType = true; }
        else if (match(TokenType::TYPE_INT64)) { targetType = ThavaType::INT; hasType = true; }
        else if (match(TokenType::TYPE_INT16)) { targetType = ThavaType::INT; hasType = true; }
        else if (match(TokenType::TYPE_FL)) { targetType = ThavaType::FLOAT; hasType = true; }
        else if (match(TokenType::TYPE_DBL)) { targetType = ThavaType::DOUBLE; hasType = true; }
        else if (match(TokenType::TYPE_BL)) { targetType = ThavaType::BOOL; hasType = true; }
        else if (match(TokenType::TYPE_STR)) { targetType = ThavaType::STRING; hasType = true; }
        else if (match(TokenType::TYPE_LST)) { targetType = ThavaType::LIST; hasType = true; }
        else if (match(TokenType::TYPE_TPLE)) { targetType = ThavaType::TUPLE; hasType = true; }

        if (!check(TokenType::IDENTIFIER)) { std::cerr << "Parser Error: Missing loop variable name in 'for' at line " << peek().line << "\n"; exit(1); }
        std::string name = advance().lexeme;

        // Python-style / Foreach syntax: for [<type>] name in ... { }
        if (match(TokenType::IN)) {
            if (check(TokenType::IDENTIFIER) && peek().lexeme == "range") {
                advance(); // Skip "range"
                if (!match(TokenType::LPAREN)) { std::cerr << "Parser Error: Missing '(' after range at line " << peek().line << "\n"; exit(1); }

                std::vector<ASTPtr> args;
                args.push_back(expression());
                while (match(TokenType::COMMA)) args.push_back(expression());
                if (!match(TokenType::RPAREN)) { std::cerr << "Parser Error: Missing ')' after range(...) at line " << peek().line << "\n"; exit(1); }

                if (args.size() < 1 || args.size() > 3) {
                    std::cerr << "Parser Error: range() accepts 1 to 3 arguments (stop) / (start, stop) / (start, stop, step) at line " << peek().line << "\n";
                    exit(1);
                }

                ASTPtr startExpr, stopExpr, stepExpr;
                if (args.size() == 1) {
                    startExpr = std::make_unique<LiteralNode>("0", ThavaType::INT);
                    stopExpr = std::move(args[0]);
                    stepExpr = std::make_unique<LiteralNode>("1", ThavaType::INT);
                } else if (args.size() == 2) {
                    startExpr = std::move(args[0]);
                    stopExpr = std::move(args[1]);
                    stepExpr = std::make_unique<LiteralNode>("1", ThavaType::INT);
                } else {
                    startExpr = std::move(args[0]);
                    stopExpr = std::move(args[1]);
                    stepExpr = std::move(args[2]);
                }

                if (!check(TokenType::LBRACE)) {
                    std::cerr << "Parser Error: Expected '{' to begin 'for' body at line " << peek().line << "\n";
                    exit(1);
                }
                ASTPtr body = statement();
                if (!hasType) targetType = ThavaType::INT;
                return std::make_unique<RangeForNode>(targetType, name, std::move(startExpr), std::move(stopExpr),
                                                       std::move(stepExpr), std::move(body));
            } else {
                // Foreach: for [<type>] ten in collectionExpr { }
                ASTPtr collExpr = expression();
                if (!check(TokenType::LBRACE)) {
                    std::cerr << "Parser Error: Expected '{' to begin 'for' body at line " << peek().line << "\n";
                    exit(1);
                }
                ASTPtr body = statement();
                return std::make_unique<ForEachNode>(targetType, name, std::move(collExpr), std::move(body));
            }
        }

        // C-style loop: for <type> name = init, cond, step { }
        if (!hasType) {
            std::cerr << "Parser Error: C-style 'for' requires <type> declaration at line " << peek().line << "\n";
            exit(1);
        }
        if (!match(TokenType::ASSIGN)) { std::cerr << "Parser Error: Missing '=' in 'for' counter initialization at line " << peek().line << "\n"; exit(1); }
        ASTPtr initExpr = expression();
        auto initNode = std::make_unique<VarDeclNode>(targetType, name, std::move(initExpr));

        if (!match(TokenType::COMMA)) { std::cerr << "Parser Error: 'for' requires ',' after initialization at line " << peek().line << "\n"; exit(1); }
        ASTPtr cond = expression();

        if (!match(TokenType::COMMA)) { std::cerr << "Parser Error: 'for' requires ',' after condition at line " << peek().line << "\n"; exit(1); }
        ASTPtr update = assignOrExprStatement();

        if (!check(TokenType::LBRACE)) {
            std::cerr << "Parser Error: Expected '{' to begin 'for' body at line " << peek().line << "\n";
            exit(1);
        }
        ASTPtr body = statement();

        return std::make_unique<ForNode>(std::move(initNode), std::move(cond), std::move(update), std::move(body));
    }

    ASTPtr whileStatement() {
        ASTPtr cond = expression();
        if (!check(TokenType::LBRACE)) { 
            std::cerr << "Parser Error: Expected '{' after 'while' condition at line " << peek().line << "\n"; 
            exit(1); 
        }
        ASTPtr body = statement();
        return std::make_unique<WhileNode>(std::move(cond), std::move(body));
    }

    ASTPtr blockStatement() {
        advance(); // Skip '{'
        std::vector<ASTPtr> statements;
        while (!check(TokenType::RBRACE) && !check(TokenType::EOF_TOKEN)) {
            statements.push_back(statement());
        }
        if (!match(TokenType::RBRACE)) { std::cerr << "Parser Error: Missing '}' to close block scope at line " << peek().line << "\n"; exit(1); }
        return std::make_unique<BlockNode>(std::move(statements));
    }

    ASTPtr fileOpenStatement() {
        advance(); // Skip 'file.open'
        if (!match(TokenType::LPAREN)) {
            std::cerr << "Parser Error: Missing '(' after file.open at line " << peek().line << "\n";
            exit(1);
        }
        ASTPtr pathExpr = expression();
        ASTPtr modeExpr = nullptr;
        if (match(TokenType::COMMA)) {
            modeExpr = expression();
        }
        if (!match(TokenType::RPAREN)) {
            std::cerr << "Parser Error: Missing ')' after file.open(...) arguments at line " << peek().line << "\n";
            exit(1);
        }
        if (!match(TokenType::AS)) {
            std::cerr << "Parser Error: Expected 'as' keyword after file.open(...) at line " << peek().line << "\n";
            exit(1);
        }
        if (!check(TokenType::IDENTIFIER)) {
            std::cerr << "Parser Error: Missing file variable name after 'as' at line " << peek().line << "\n";
            exit(1);
        }
        std::string varName = advance().lexeme;
        if (!match(TokenType::LBRACE)) {
            std::cerr << "Parser Error: Expected '{' to begin file.open block at line " << peek().line << "\n";
            exit(1);
        }
        std::vector<ASTPtr> body;
        while (!check(TokenType::RBRACE) && !check(TokenType::EOF_TOKEN)) {
            body.push_back(statement());
        }
        if (!match(TokenType::RBRACE)) {
            std::cerr << "Parser Error: Missing '}' to close file.open block at line " << peek().line << "\n";
            exit(1);
        }
        return std::make_unique<FileOpenBlockNode>(std::move(pathExpr), std::move(modeExpr), varName, std::move(body));
    }

    ASTPtr tryStatement() {
        g_usesNativeUnsupportedFeature = true;
        advance(); // Skip 'try'
        if (!match(TokenType::LBRACE)) {
            std::cerr << "Parser Error: Expected '{' after 'try' at line " << peek().line << "\n";
            exit(1);
        }
        std::vector<ASTPtr> tryBody;
        while (!check(TokenType::RBRACE) && !check(TokenType::EOF_TOKEN)) {
            tryBody.push_back(statement());
        }
        if (!match(TokenType::RBRACE)) {
            std::cerr << "Parser Error: Missing '}' closing 'try' block at line " << peek().line << "\n";
            exit(1);
        }

        std::vector<ExceptClause> exceptClauses;
        while (check(TokenType::EXCEPT)) {
            advance(); // Skip 'except'
            std::string excType = "";
            std::string varName = "";

            if (check(TokenType::IDENTIFIER) && peek().lexeme != "as") {
                excType = advance().lexeme;
            }
            if (match(TokenType::AS)) {
                if (!check(TokenType::IDENTIFIER)) {
                    std::cerr << "Parser Error: Expected variable name after 'as' in 'except' at line " << peek().line << "\n";
                    exit(1);
                }
                varName = advance().lexeme;
            }
            if (!match(TokenType::LBRACE)) {
                std::cerr << "Parser Error: Expected '{' after 'except' at line " << peek().line << "\n";
                exit(1);
            }
            std::vector<ASTPtr> excBody;
            while (!check(TokenType::RBRACE) && !check(TokenType::EOF_TOKEN)) {
                excBody.push_back(statement());
            }
            if (!match(TokenType::RBRACE)) {
                std::cerr << "Parser Error: Missing '}' closing 'except' block at line " << peek().line << "\n";
                exit(1);
            }
            exceptClauses.push_back({excType, varName, std::move(excBody)});
        }

        if (exceptClauses.empty()) {
            std::cerr << "Parser Error: 'try' block must have at least one 'except' block at line " << peek().line << "\n";
            exit(1);
        }

        return std::make_unique<TryExceptNode>(std::move(tryBody), std::move(exceptClauses));
    }

    ASTPtr throwStatement() {
        advance(); // Skip 'throw' or 'raise'
        ASTPtr expr = nullptr;
        int throwLine = tokens[current - 1].line;
        if (!check(TokenType::RBRACE) && !check(TokenType::EOF_TOKEN) && peek().line == throwLine) {
            expr = expression();
        }
        return std::make_unique<ThrowNode>(std::move(expr));
    }

    ASTPtr raiseErrorStatement() {
        int errLine = peek().line;
        advance(); // Skip 'raise_error'
        if (!match(TokenType::LPAREN)) {
            std::cerr << "Parser Error: Missing '(' after 'raise_error' at line " << errLine << "\n";
            exit(1);
        }

        // Parameter 1: r (runtime) or c (compile-time)
        std::string kind = "";
        if (check(TokenType::IDENTIFIER)) {
            kind = advance().lexeme;
        } else if (check(TokenType::STR_LIT)) {
            kind = advance().lexeme;
        } else {
            std::cerr << "Parser Error: First argument of raise_error must be 'r' (runtime) or 'c' (compile-time) at line " << peek().line << "\n";
            exit(1);
        }

        std::string lowerKind = kind;
        for (char& ch : lowerKind) ch = tolower((unsigned char)ch);
        if (lowerKind != "r" && lowerKind != "c" && lowerKind != "runtime" && lowerKind != "compile") {
            std::cerr << "Parser Error: First argument of raise_error must be 'r' or 'c', got '" << kind << "' at line " << errLine << "\n";
            exit(1);
        }
        bool isCompileTime = (lowerKind == "c" || lowerKind == "compile");

        if (!match(TokenType::COMMA)) {
            std::cerr << "Parser Error: Missing ',' after first argument of raise_error at line " << peek().line << "\n";
            exit(1);
        }

        // Parameter 2: errorType
        ASTPtr typeExpr = expression();

        if (!match(TokenType::COMMA)) {
            std::cerr << "Parser Error: Missing ',' after second argument of raise_error at line " << peek().line << "\n";
            exit(1);
        }

        // Parameter 3: msgExpr
        ASTPtr msgExpr = expression();

        if (!match(TokenType::RPAREN)) {
            std::cerr << "Parser Error: Missing ')' closing raise_error at line " << peek().line << "\n";
            exit(1);
        }

        return std::make_unique<RaiseErrorNode>(isCompileTime, std::move(typeExpr), std::move(msgExpr), errLine);
    }

    ASTPtr statement() {
        if (check(TokenType::IDENTIFIER) && peek().lexeme == "global" && isTypeToken(peekAt(1).type)) {
            advance();
            std::string customType;
            ThavaType type = parseType(&customType);
            if (!check(TokenType::IDENTIFIER)) {
                std::cerr << "Parser Error: Expected a name after global type at line " << peek().line << "\n";
                exit(1);
            }
            return std::make_unique<GlobalDeclNode>(type, customType, advance().lexeme);
        }
        if (check(TokenType::TRY)) return tryStatement();
        if (check(TokenType::THROW)) return throwStatement();
        if (check(TokenType::RAISE_ERROR)) return raiseErrorStatement();
        if (check(TokenType::FILE_OPEN)) return fileOpenStatement();
        if (match(TokenType::IF)) return ifStatement();
        if (match(TokenType::WHILE)) return whileStatement();
        if (match(TokenType::FOR)) return forStatement();
        if (match(TokenType::BREAK)) return std::make_unique<BreakNode>();
        if (match(TokenType::CONTINUE)) return std::make_unique<ContinueNode>();
        if (match(TokenType::CLASS)) return classDeclStatement();
        if (match(TokenType::STRUCT)) return structDeclStatement();
        if (match(TokenType::ENUM)) return enumDeclStatement();
        if (match(TokenType::RETURN)) {
            int retLine = tokens[current - 1].line;
            ASTPtr e = nullptr;
            if (!check(TokenType::RBRACE) && !check(TokenType::EOF_TOKEN) && peek().line == retLine) {
                e = expression();
            }
            return std::make_unique<ReturnNode>(std::move(e));
        }
        if (match(TokenType::DEFER)) {
            ASTPtr deferredStmt = nullptr;
            if (check(TokenType::LBRACE)) {
                deferredStmt = blockStatement();
            } else {
                deferredStmt = statement();
            }
            return std::make_unique<DeferNode>(std::move(deferredStmt));
        }
        if (check(TokenType::LBRACE)) return blockStatement();
        if (check(TokenType::CONS_OUT)) return consOutStatement();
        if (check(TokenType::CONS_CLEAR)) return consClearStatement();

        // 0. Syntax starting with 'inline': inline <type> func ... or inline func ...
        if (check(TokenType::INLINE)) {
            advance(); // Skip 'inline'
            if (isTypeToken(peek().type)) {
                return functionDeclStatement(true, true);
            }
            return functionDeclStatement(false, true);
        }

        // 0b. Syntax: <type> inline func name(...) { ... }
        if (isTypeToken(peek().type) && peekAt(1).type == TokenType::INLINE) {
            return functionDeclStatement(true, true);
        }

        // 1. Syntax starting with 'func':
        //    func <type> ten(...) { ... }
        //    func ten(...) -> <type> { ... }
        //    func name(...) { ... } (default void)
        if (check(TokenType::FUNC)) {
            return functionDeclStatement(false);
        }

        // 2. Syntax starting with 'fdfunc':
        if (check(TokenType::FDFUNC)) {
            return forwardDeclStatement(false);
        }

        // 2b. Syntax starting with 'cfunc':
        if (check(TokenType::CFUNC)) {
            return cfuncDeclStatement(false);
        }

        // 3. Classical syntax: <type>func name(...) or <type>fdfunc name(...) or <type>cfunc name(...)
        if (isTypeToken(peek().type) && peekAt(1).type == TokenType::FUNC) {
            return functionDeclStatement(true);
        }
        if (isTypeToken(peek().type) && peekAt(1).type == TokenType::FDFUNC) {
            return forwardDeclStatement(true);
        }
        if (isTypeToken(peek().type) && peekAt(1).type == TokenType::CFUNC) {
            return cfuncDeclStatement(true);
        }

        // 4. Variable declaration: <type> name = ...
        if (isTypeToken(peek().type) && peek().type != TokenType::TYPE_VOID) {
            return varDeclStatement();
        }

        return assignOrExprStatement();
    }

    ASTPtr classDeclStatement() {
        if (!check(TokenType::IDENTIFIER)) {
            std::cerr << "Parser Error: Missing class name at line " << peek().line << "\n";
            exit(1);
        }
        std::string className = advance().lexeme;
        g_classNames.insert(className);

        std::string baseClassName = "";
        if (match(TokenType::COLON)) {
            if (!check(TokenType::IDENTIFIER)) {
                std::cerr << "Parser Error: Expected base class name after ':' at line " << peek().line << "\n";
                exit(1);
            }
            baseClassName = advance().lexeme;
            if (g_classNames.find(baseClassName) == g_classNames.end()) {
                std::cerr << "Parser Error: Base class '" << baseClassName << "' is not declared before subclass '" << className << "' at line " << peek().line << "\n";
                exit(1);
            }
        } else if (match(TokenType::LPAREN)) {
            if (!check(TokenType::IDENTIFIER)) {
                std::cerr << "Parser Error: Expected base class name inside parentheses at line " << peek().line << "\n";
                exit(1);
            }
            baseClassName = advance().lexeme;
            if (!match(TokenType::RPAREN)) {
                std::cerr << "Parser Error: Missing ')' after base class name at line " << peek().line << "\n";
                exit(1);
            }
            if (g_classNames.find(baseClassName) == g_classNames.end()) {
                std::cerr << "Parser Error: Base class '" << baseClassName << "' is not declared before subclass '" << className << "' at line " << peek().line << "\n";
                exit(1);
            }
        }

        std::string prevClass = currentParsingClass;
        std::string prevBaseClass = currentParsingBaseClass;
        currentParsingClass = className;
        currentParsingBaseClass = baseClassName;

        if (!match(TokenType::LBRACE)) {
            std::cerr << "Parser Error: Missing '{' after class name '" << className << "' at line " << peek().line << "\n";
            exit(1);
        }

        std::vector<ClassFieldDecl> fields;
        std::vector<ClassConstructorDecl> constructors;
        std::vector<ClassMethodDecl> methods;

        while (!check(TokenType::RBRACE) && !check(TokenType::EOF_TOKEN)) {
            AccessModifier currentAccess = AccessModifier::PUBLIC;
            int memberLine = peek().line;

            if (match(TokenType::PUBLIC)) {
                currentAccess = AccessModifier::PUBLIC;
            } else if (match(TokenType::PRIVATE)) {
                currentAccess = AccessModifier::PRIVATE;
            } else if (match(TokenType::PROTECTED)) {
                currentAccess = AccessModifier::PROTECTED;
            }

            if (check(TokenType::COLON)) {
                std::cerr << "Parser Error: Thava does not use block label '" << accessModifierToString(currentAccess) 
                          << ":', vui long su dung tien to cho tung thanh vien (Style 1: vi du: " 
                          << accessModifierToString(currentAccess) << " func ... hoac " 
                          << accessModifierToString(currentAccess) << " <type> ...) at line " << peek().line << "\n";
                exit(1);
            }

            // 1. Constructor or Method
            if (check(TokenType::FUNC) || (isTypeToken(peek().type) && peekAt(1).type == TokenType::FUNC)) {
                bool hasPrefixType = isTypeToken(peek().type);
                ThavaType retType = ThavaType::VOID;
                std::string customRetType = "";
                if (hasPrefixType) {
                    retType = parseType(&customRetType);
                    match(TokenType::FUNC);
                } else {
                    match(TokenType::FUNC);
                    if (isTypeToken(peek().type)) {
                        retType = parseType(&customRetType);
                    }
                }

                if (!check(TokenType::IDENTIFIER)) {
                    std::cerr << "Parser Error: Missing method name in class '" << className << "' at line " << peek().line << "\n";
                    exit(1);
                }
                std::string memberName = advance().lexeme;

                if (!match(TokenType::LPAREN)) {
                    std::cerr << "Parser Error: Missing '(' after method name '" << memberName << "' at line " << peek().line << "\n";
                    exit(1);
                }

                std::vector<std::pair<ThavaType, std::string>> params;
                std::vector<std::string> paramCustomTypes;
                if (!check(TokenType::RPAREN)) {
                    do {
                        if (!isTypeToken(peek().type)) {
                            std::cerr << "Parser Error: Expected <type> for method parameter '" << memberName << "' at line " << peek().line << "\n";
                            exit(1);
                        }
                        std::string pCustom = "";
                        ThavaType pType = parseType(&pCustom);
                        if (!check(TokenType::IDENTIFIER)) {
                            std::cerr << "Parser Error: Missing method parameter name at line " << peek().line << "\n";
                            exit(1);
                        }
                        std::string pName = advance().lexeme;
                        params.push_back({pType, pName});
                        paramCustomTypes.push_back(pCustom);
                    } while (match(TokenType::COMMA));
                }
                if (!match(TokenType::RPAREN)) {
                    std::cerr << "Parser Error: Missing ')' after method parameters at line " << peek().line << "\n";
                    exit(1);
                }

                if (match(TokenType::ARROW)) {
                    if (!isTypeToken(peek().type)) {
                        std::cerr << "Parser Error: Expected return type after '->' at line " << peek().line << "\n";
                        exit(1);
                    }
                    retType = parseType(&customRetType);
                }

                if (!match(TokenType::LBRACE)) {
                    std::cerr << "Parser Error: Missing '{' starting method body at line " << peek().line << "\n";
                    exit(1);
                }
                std::vector<ASTPtr> body;
                while (!check(TokenType::RBRACE) && !check(TokenType::EOF_TOKEN)) {
                    body.push_back(statement());
                }
                if (!match(TokenType::RBRACE)) {
                    std::cerr << "Parser Error: Missing '}' ending method body at line " << peek().line << "\n";
                    exit(1);
                }

                // Check if constructor or method:
                // Supports both func init(...) and func ClassName(...)
                if (memberName == "init" || memberName == className) {
                    constructors.push_back({currentAccess, std::move(params), std::move(paramCustomTypes), std::move(body), memberLine});
                } else {
                    methods.push_back({currentAccess, retType, customRetType, memberName, std::move(params), std::move(paramCustomTypes), std::move(body), memberLine});
                }
            }
            // 2. Thuoc tinh (Field)
            else if (isTypeToken(peek().type)) {
                std::string fCustom = "";
                ThavaType fType = parseType(&fCustom);
                if (!check(TokenType::IDENTIFIER)) {
                    std::cerr << "Parser Error: Missing property name in class '" << className << "' at line " << peek().line << "\n";
                    exit(1);
                }
                std::string fName = advance().lexeme;
                ASTPtr initExpr = nullptr;
                if (match(TokenType::ASSIGN)) {
                    initExpr = expression();
                }
                fields.push_back({currentAccess, fType, fCustom, fName, std::move(initExpr), memberLine});
            }
            else {
                std::cerr << "Parser Error: Invalid syntax inside class '" << className << "' at line " << peek().line << " (token '" << peek().lexeme << "')\n";
                exit(1);
            }
        }

        if (!match(TokenType::RBRACE)) {
            std::cerr << "Parser Error: Missing '}' ending class '" << className << "' at line " << peek().line << "\n";
            exit(1);
        }

        ClassMemberInfo cInfo;
        cInfo.className = className;
        cInfo.baseClassName = baseClassName;
        for (const auto& c : constructors) {
            cInfo.constructorAccess.push_back(c.access);
        }
        for (const auto& f : fields) {
            cInfo.fieldAccess[f.name] = f.access;
            cInfo.fieldTypes[f.name] = f.type;
            cInfo.fieldCustomTypes[f.name] = f.customTypeName;
        }
        for (const auto& m : methods) {
            cInfo.methodAccess[m.name] = m.access;
            cInfo.methodReturnTypes[m.name] = m.returnType;
            cInfo.methodCustomReturnTypes[m.name] = m.customReturnType;
        }
        g_classInfo[className] = cInfo;

        currentParsingClass = prevClass;
        currentParsingBaseClass = prevBaseClass;
        return std::make_unique<ClassDeclNode>(className, baseClassName, std::move(fields), std::move(constructors), std::move(methods));
    }

    ASTPtr structDeclStatement() {
        if (!check(TokenType::IDENTIFIER)) {
            std::cerr << "Parser Error: Missing struct name at line " << peek().line << "\n";
            exit(1);
        }
        std::string structName = advance().lexeme;
        g_classNames.insert(structName);

        if (!match(TokenType::LBRACE)) {
            std::cerr << "Parser Error: Missing '{' after struct name '" << structName << "' at line " << peek().line << "\n";
            exit(1);
        }

        std::vector<ClassFieldDecl> fields;
        while (!check(TokenType::RBRACE) && !check(TokenType::EOF_TOKEN)) {
            if (!isTypeToken(peek().type)) {
                std::cerr << "Parser Error: Expected data type for field in struct '" << structName << "' at line " << peek().line << "\n";
                exit(1);
            }
            int fLine = peek().line;
            std::string fCustom = "";
            ThavaType fType = parseType(&fCustom);
            if (!check(TokenType::IDENTIFIER)) {
                std::cerr << "Parser Error: Missing field name in struct '" << structName << "' at line " << peek().line << "\n";
                exit(1);
            }
            std::string fName = advance().lexeme;
            ASTPtr initExpr = nullptr;
            if (match(TokenType::ASSIGN)) {
                initExpr = expression();
            }
            fields.push_back({AccessModifier::PUBLIC, fType, fCustom, fName, std::move(initExpr), fLine});
            if (match(TokenType::COMMA)) {}
        }

        if (!match(TokenType::RBRACE)) {
            std::cerr << "Parser Error: Missing '}' ending struct '" << structName << "' at line " << peek().line << "\n";
            exit(1);
        }

        // 1. Register into g_classInfo for static type checking & OOP field lookup
        ClassMemberInfo cInfo;
        cInfo.className = structName;
        for (const auto& f : fields) {
            cInfo.fieldAccess[f.name] = AccessModifier::PUBLIC;
            cInfo.fieldTypes[f.name] = f.type;
            cInfo.fieldCustomTypes[f.name] = f.customTypeName;
        }
        g_classInfo[structName] = cInfo;

        // 2. Register into g_structs with natural memory alignment for Native Codegen
        StructInfo sinfo;
        sinfo.name = structName;
        int32_t currentOffset = 0;
        int32_t maxAlign = 4;
        for (size_t i = 0; i < fields.size(); ++i) {
            const auto& f = fields[i];
            int32_t align = 8;
            int32_t size = 8;
            if (f.type == ThavaType::FLOAT) {
                align = 4;
                size = 4;
            } else if (f.type == ThavaType::BOOL) {
                align = 1;
                size = 1;
            }
            if (align > maxAlign) maxAlign = align;
            if (currentOffset % align != 0) {
                currentOffset += (align - (currentOffset % align));
            }
            StructFieldInfo sfi;
            sfi.name = f.name;
            sfi.type = f.type;
            sfi.customType = f.customTypeName;
            sfi.offset = currentOffset;
            sinfo.fields.push_back(sfi);
            sinfo.fieldIndex[f.name] = i;
            currentOffset += size;
        }
        if (maxAlign > 0 && currentOffset % maxAlign != 0) {
            currentOffset += (maxAlign - (currentOffset % maxAlign));
        }
        if (currentOffset == 0) currentOffset = 8;
        sinfo.totalSize = currentOffset;
        g_structs[structName] = sinfo;

        return std::make_unique<StructDeclNode>(structName, std::move(fields));
    }

    ASTPtr enumDeclStatement() {
        if (!check(TokenType::IDENTIFIER)) {
            std::cerr << "Parser Error: Missing enum name at line " << peek().line << "\n";
            exit(1);
        }
        std::string enumName = advance().lexeme;
        g_classNames.insert(enumName);

        if (!match(TokenType::LBRACE)) {
            std::cerr << "Parser Error: Missing '{' after enum name '" << enumName << "' at line " << peek().line << "\n";
            exit(1);
        }

        std::vector<std::pair<std::string, int64_t>> members;
        int64_t nextVal = 0;
        while (!check(TokenType::RBRACE) && !check(TokenType::EOF_TOKEN)) {
            if (!check(TokenType::IDENTIFIER)) {
                std::cerr << "Parser Error: Expected enum member name in '" << enumName << "' at line " << peek().line << "\n";
                exit(1);
            }
            std::string mName = advance().lexeme;
            int64_t mVal = nextVal;
            if (match(TokenType::ASSIGN)) {
                bool isNeg = false;
                if (match(TokenType::MINUS)) isNeg = true;
                if (!check(TokenType::INT_LIT)) {
                    std::cerr << "Parser Error: Enum member value must be an integer at line " << peek().line << "\n";
                    exit(1);
                }
                mVal = std::stoll(advance().lexeme);
                if (isNeg) mVal = -mVal;
            }
            members.push_back({mName, mVal});
            nextVal = mVal + 1;

            if (match(TokenType::COMMA)) {}
        }

        if (!match(TokenType::RBRACE)) {
            std::cerr << "Parser Error: Missing '}' ending enum '" << enumName << "' at line " << peek().line << "\n";
            exit(1);
        }

        EnumInfo einfo;
        einfo.name = enumName;
        for (const auto& m : members) {
            einfo.members[m.first] = m.second;
        }
        g_enums[enumName] = einfo;

        return std::make_unique<EnumDeclNode>(enumName, std::move(members));
    }

    ASTPtr functionDeclStatement(bool hasPrefixType, bool isInline = false) {
        ThavaType returnType = ThavaType::VOID;
        std::string customReturnType = "";
        if (hasPrefixType) {
            returnType = parseType(&customReturnType);
            if (match(TokenType::INLINE)) isInline = true;
            match(TokenType::FUNC);
        } else {
            if (match(TokenType::INLINE)) isInline = true;
            match(TokenType::FUNC);
            if (isTypeToken(peek().type)) {
                returnType = parseType(&customReturnType);
            }
        }

        if (!check(TokenType::IDENTIFIER)) {
            std::cerr << "Parser Error: Missing function name at line " << peek().line << "\n";
            exit(1);
        }
        std::string name = advance().lexeme;

        if (!match(TokenType::LPAREN)) {
            std::cerr << "Parser Error: Missing '(' after function name at line " << peek().line << "\n";
            exit(1);
        }

        std::vector<std::pair<ThavaType, std::string>> params;
        std::vector<std::string> paramCustomTypes;
        std::vector<std::vector<Token>> defaultArgTokens;
        size_t minArgs = 0;
        bool hasDefault = false;

        if (!check(TokenType::RPAREN)) {
            do {
                if (!isTypeToken(peek().type)) {
                    std::cerr << "Parser Error: Expected <type> for function parameter at line " << peek().line << "\n";
                    exit(1);
                }
                std::string pCustom = "";
                ThavaType pType = parseType(&pCustom);
                if (!check(TokenType::IDENTIFIER)) {
                    std::cerr << "Parser Error: Missing parameter name at line " << peek().line << "\n";
                    exit(1);
                }
                std::string pName = advance().lexeme;
                params.push_back({pType, pName});
                paramCustomTypes.push_back(pCustom);

                if (match(TokenType::ASSIGN)) {
                    hasDefault = true;
                    std::vector<Token> defTokens;
                    int pDepth = 0, bDepth = 0, brDepth = 0;
                    while (!check(TokenType::EOF_TOKEN)) {
                        if (pDepth == 0 && bDepth == 0 && brDepth == 0) {
                            if (check(TokenType::COMMA) || check(TokenType::RPAREN)) break;
                        }
                        if (check(TokenType::LPAREN)) pDepth++;
                        else if (check(TokenType::RPAREN)) pDepth--;
                        else if (check(TokenType::LBRACKET)) bDepth++;
                        else if (check(TokenType::RBRACKET)) bDepth--;
                        else if (check(TokenType::LBRACE)) brDepth++;
                        else if (check(TokenType::RBRACE)) brDepth--;
                        defTokens.push_back(advance());
                    }
                    defTokens.push_back({TokenType::EOF_TOKEN, "", peek().line});
                    defaultArgTokens.push_back(defTokens);
                } else {
                    if (hasDefault) {
                        std::cerr << "Parser Error: Parameter '" << pName << "' has no default value but follows a parameter with default value at line " << peek().line << "\n";
                        exit(1);
                    }
                    minArgs++;
                    defaultArgTokens.push_back({});
                }
            } while (match(TokenType::COMMA));
        }
        if (!match(TokenType::RPAREN)) {
            std::cerr << "Parser Error: Missing ')' after function parameters at line " << peek().line << "\n";
            exit(1);
        }

        // Support return type arrow syntax: func name(...) -> <type>
        if (match(TokenType::ARROW)) {
            if (!isTypeToken(peek().type)) {
                std::cerr << "Parser Error: Expected return type after '->' at line " << peek().line << "\n";
                exit(1);
            }
            returnType = parseType(&customReturnType);
        }

        std::vector<ThavaType> pTypes;
        for (auto& p : params) pTypes.push_back(p.first);

        if (g_funcReturnTypes.count(name)) {
            if (g_funcReturnTypes[name] != returnType || g_funcParamTypes[name] != pTypes) {
                std::cerr << "Parser Error: Definition of function '" << name << "' does not match its forward declaration at line " << peek().line << "\n";
                exit(1);
            }
        }
        g_funcReturnTypes[name] = returnType;
        if (returnType == ThavaType::INT) g_funcReturnWidths[name] = customReturnType;
        g_funcParamTypes[name] = pTypes;
        g_funcMinArgs[name] = minArgs;
        g_funcDefaultArgTokens[name] = defaultArgTokens;

        if (!match(TokenType::LBRACE)) {
            std::cerr << "Parser Error: Expected '{' starting function body at line " << peek().line << "\n";
            exit(1);
        }
        std::vector<ASTPtr> body;
        while (!check(TokenType::RBRACE) && !check(TokenType::EOF_TOKEN)) {
            body.push_back(statement());
        }
        if (!match(TokenType::RBRACE)) {
            std::cerr << "Parser Error: Missing '}' ending function body at line " << peek().line << "\n";
            exit(1);
        }

        return std::make_unique<FunctionDeclNode>(returnType, name, std::move(params), std::move(body), std::move(paramCustomTypes), customReturnType, isInline);
    }

    ASTPtr forwardDeclStatement(bool hasPrefixType) {
        ThavaType returnType = ThavaType::VOID;
        std::string customReturnType = "";
        if (hasPrefixType) {
            returnType = parseType(&customReturnType);
            match(TokenType::FDFUNC);
        } else {
            match(TokenType::FDFUNC);
            if (isTypeToken(peek().type)) {
                returnType = parseType(&customReturnType);
            }
        }

        if (!check(TokenType::IDENTIFIER)) {
            std::cerr << "Parser Error: Missing function name at line " << peek().line << "\n";
            exit(1);
        }
        std::string name = advance().lexeme;

        if (!match(TokenType::LPAREN)) {
            std::cerr << "Parser Error: Missing '(' after function name at line " << peek().line << "\n";
            exit(1);
        }

        std::vector<std::pair<ThavaType, std::string>> params;
        std::vector<std::string> paramCustomTypes;
        std::vector<std::vector<Token>> defaultArgTokens;
        size_t minArgs = 0;
        bool hasDefault = false;

        if (!check(TokenType::RPAREN)) {
            do {
                if (!isTypeToken(peek().type)) {
                    std::cerr << "Parser Error: Expected <type> for function parameter at line " << peek().line << "\n";
                    exit(1);
                }
                std::string pCustom = "";
                ThavaType pType = parseType(&pCustom);
                std::string pName = "";
                if (check(TokenType::IDENTIFIER)) pName = advance().lexeme;
                params.push_back({pType, pName});
                paramCustomTypes.push_back(pCustom);

                if (match(TokenType::ASSIGN)) {
                    hasDefault = true;
                    std::vector<Token> defTokens;
                    int pDepth = 0, bDepth = 0, brDepth = 0;
                    while (!check(TokenType::EOF_TOKEN)) {
                        if (pDepth == 0 && bDepth == 0 && brDepth == 0) {
                            if (check(TokenType::COMMA) || check(TokenType::RPAREN)) break;
                        }
                        if (check(TokenType::LPAREN)) pDepth++;
                        else if (check(TokenType::RPAREN)) pDepth--;
                        else if (check(TokenType::LBRACKET)) bDepth++;
                        else if (check(TokenType::RBRACKET)) bDepth--;
                        else if (check(TokenType::LBRACE)) brDepth++;
                        else if (check(TokenType::RBRACE)) brDepth--;
                        defTokens.push_back(advance());
                    }
                    defTokens.push_back({TokenType::EOF_TOKEN, "", peek().line});
                    defaultArgTokens.push_back(defTokens);
                } else {
                    if (hasDefault) {
                        std::cerr << "Parser Error: Parameter '" << pName << "' has no default value but follows a parameter with default value at line " << peek().line << "\n";
                        exit(1);
                    }
                    minArgs++;
                    defaultArgTokens.push_back({});
                }
            } while (match(TokenType::COMMA));
        }
        if (!match(TokenType::RPAREN)) {
            std::cerr << "Parser Error: Missing ')' after function parameters at line " << peek().line << "\n";
            exit(1);
        }

        if (match(TokenType::ARROW)) {
            if (!isTypeToken(peek().type)) {
                std::cerr << "Parser Error: Expected return type after '->' at line " << peek().line << "\n";
                exit(1);
            }
            returnType = parseType(&customReturnType);
        }

        std::vector<ThavaType> pTypes;
        for (auto& p : params) pTypes.push_back(p.first);

        g_funcReturnTypes[name] = returnType;
        if (returnType == ThavaType::INT) g_funcReturnWidths[name] = customReturnType;
        g_funcParamTypes[name] = pTypes;
        g_funcMinArgs[name] = minArgs;
        g_funcDefaultArgTokens[name] = defaultArgTokens;

        return std::make_unique<ForwardDeclNode>(returnType, name, std::move(params), std::move(paramCustomTypes), customReturnType);
    }

    ASTPtr cfuncDeclStatement(bool hasPrefixType) {
        std::string returnWidth;
        ThavaType returnType = ThavaType::VOID;
        if (hasPrefixType) {
            returnType = parseType(&returnWidth);
            match(TokenType::CFUNC);
        } else {
            match(TokenType::CFUNC);
            if (isTypeToken(peek().type)) {
                returnType = parseType(&returnWidth);
            }
        }

        if (!check(TokenType::IDENTIFIER)) {
            std::cerr << "Parser Error: Missing cfunc function name at line " << peek().line << "\n";
            exit(1);
        }
        std::string name = advance().lexeme;

        if (!match(TokenType::LPAREN)) {
            std::cerr << "Parser Error: Missing '(' after function name cfunc at line " << peek().line << "\n";
            exit(1);
        }

        std::vector<std::pair<ThavaType, std::string>> params;
        std::vector<std::vector<Token>> defaultArgTokens;
        size_t minArgs = 0;
        bool hasDefault = false;

        if (!check(TokenType::RPAREN)) {
            do {
                if (!isTypeToken(peek().type)) {
                    std::cerr << "Parser Error: Expected <type> for cfunc parameter at line " << peek().line << "\n";
                    exit(1);
                }
                ThavaType pType = parseType();
                std::string pName = "";
                if (check(TokenType::IDENTIFIER)) pName = advance().lexeme;
                params.push_back({pType, pName});

                if (match(TokenType::ASSIGN)) {
                    hasDefault = true;
                    std::vector<Token> defTokens;
                    int pDepth = 0, bDepth = 0, brDepth = 0;
                    while (!check(TokenType::EOF_TOKEN)) {
                        if (pDepth == 0 && bDepth == 0 && brDepth == 0) {
                            if (check(TokenType::COMMA) || check(TokenType::RPAREN)) break;
                        }
                        if (check(TokenType::LPAREN)) pDepth++;
                        else if (check(TokenType::RPAREN)) pDepth--;
                        else if (check(TokenType::LBRACKET)) bDepth++;
                        else if (check(TokenType::RBRACKET)) bDepth--;
                        else if (check(TokenType::LBRACE)) brDepth++;
                        else if (check(TokenType::RBRACE)) brDepth--;
                        defTokens.push_back(advance());
                    }
                    defTokens.push_back({TokenType::EOF_TOKEN, "", peek().line});
                    defaultArgTokens.push_back(defTokens);
                } else {
                    if (hasDefault) {
                        std::cerr << "Parser Error: cfunc parameter '" << pName << "' has no default value but follows a parameter with default value at line " << peek().line << "\n";
                        exit(1);
                    }
                    minArgs++;
                    defaultArgTokens.push_back({});
                }
            } while (match(TokenType::COMMA));
        }
        if (!match(TokenType::RPAREN)) {
            std::cerr << "Parser Error: Missing ')' after function parameters cfunc at line " << peek().line << "\n";
            exit(1);
        }

        if (match(TokenType::ARROW)) {
            if (!isTypeToken(peek().type)) {
                std::cerr << "Parser Error: Expected return type after '->' at line " << peek().line << "\n";
                exit(1);
            }
            returnType = parseType(&returnWidth);
        }

        std::vector<ThavaType> pTypes;
        for (auto& p : params) pTypes.push_back(p.first);

        g_funcReturnTypes[name] = returnType;
        if (returnType == ThavaType::INT) g_funcReturnWidths[name] = returnWidth;
        g_funcParamTypes[name] = pTypes;
        g_funcMinArgs[name] = minArgs;
        g_funcDefaultArgTokens[name] = defaultArgTokens;
        g_isCFunc.insert(name);

        return std::make_unique<CFuncDeclNode>(returnType, name, std::move(params));
    }

public:
    Parser(std::vector<Token> t) : tokens(std::move(t)) {}

    ASTPtr parseExpressionPublic(bool requireEnd = false) {
        auto parsed = expression();
        if (requireEnd && !check(TokenType::EOF_TOKEN)) {
            std::cerr << "Parser Error: Unexpected token in string interpolation at line " << peek().line << "\n";
            exit(1);
        }
        return parsed;
    }

    std::vector<ASTPtr> parse() {
        std::vector<ASTPtr> statements;
        while (!check(TokenType::EOF_TOKEN)) {
            statements.push_back(statement());
        }
        return statements;
    }
};

// ==========================================
// 5. STATIC TYPE CHECKER (Type Verification)
// ==========================================
// Element type of a list/tuple literal (or of another list variable), recorded so the
// native backends know whether `list[i]` must be unboxed as int, double or string.
static ThavaType inferCollectionElemType(ASTNode* expr, std::shared_ptr<Environment> env) {
    if (!expr) return ThavaType::UNKNOWN;
    if (auto os = dynamic_cast<OsMethodNode*>(expr); os && os->method == "listdir") return ThavaType::STRING;
    if (auto coll = dynamic_cast<CollectionNode*>(expr)) {
        ThavaType found = ThavaType::UNKNOWN;
        for (auto& el : coll->getElements()) {
            ThavaType t = ThavaType::UNKNOWN;
            if (auto lit = dynamic_cast<LiteralNode*>(el.get())) {
                t = lit->getLitType();
            } else if (auto v = dynamic_cast<VarNode*>(el.get())) {
                t = env ? env->get(v->getName()) : ThavaType::UNKNOWN;
            }
            if (t == ThavaType::FLOAT || t == ThavaType::DOUBLE) return ThavaType::DOUBLE;
            if (t == ThavaType::STRING) found = ThavaType::STRING;
            else if (t == ThavaType::INT && found == ThavaType::UNKNOWN) found = ThavaType::INT;
        }
        return found;
    }
    if (auto v = dynamic_cast<VarNode*>(expr)) {
        if (env) {
            ThavaType et = env->getListElem(v->getName());
            if (et != ThavaType::UNKNOWN) return et;
        }
    }
    return ThavaType::UNKNOWN;
}

// A <lst> parameter has no element type of its own, so take it from the lists the
// function is actually called with. Conflicting numeric call sites promote to
// <dbl> (the same rule list literals use: a double anywhere makes the whole list
// double), strings win over integers.
static ThavaType mergeListElemType(ThavaType cur, ThavaType et) {
    if (cur == ThavaType::UNKNOWN || cur == et) return et;
    if (cur == ThavaType::DOUBLE || et == ThavaType::DOUBLE) return ThavaType::DOUBLE;
    if (cur == ThavaType::STRING || et == ThavaType::STRING) return ThavaType::STRING;
    return ThavaType::INT;
}

static void noteListParamElemType(const std::string& fname, size_t idx, ThavaType et) {
    if (et == ThavaType::UNKNOWN || et == ThavaType::NONE) return;
    auto pit = g_funcParamTypes.find(fname);
    if (pit == g_funcParamTypes.end()) return;
    if (idx >= pit->second.size()) return;
    ThavaType pt = pit->second[idx];
    if (pt != ThavaType::LIST && pt != ThavaType::TUPLE) return;

    auto& slot = g_funcListParamElem[fname];
    if (slot.size() < pit->second.size()) slot.resize(pit->second.size(), ThavaType::UNKNOWN);
    slot[idx] = mergeListElemType(slot[idx], et);
}

// Same for a class method/constructor. The key is "Class::method" ("Class::init"
// for a constructor); only collection parameters ever consume the entry, so the
// argument position does not have to be validated against a signature here.
static void noteMethodListParamElemType(const std::string& key, size_t idx, ThavaType et) {
    if (et == ThavaType::UNKNOWN || et == ThavaType::NONE) return;
    if (key.empty()) return;
    auto& slot = g_funcListParamElem[key];
    if (slot.size() <= idx) slot.resize(idx + 1, ThavaType::UNKNOWN);
    slot[idx] = mergeListElemType(slot[idx], et);
}

void checkTypes(ASTNode* node, std::shared_ptr<Environment> env,
                ThavaType currentFuncRetType = ThavaType::UNKNOWN,
                const std::string& currentFuncName = "",
                const std::string& currentClassName = "",
                const std::string& currentBaseClassName = "") {
    if (!node) return;
    if (auto listMethod = dynamic_cast<ListMethodNode*>(node))
        checkExpr(listMethod, env, currentClassName, currentBaseClassName);
    if (auto dictMethod = dynamic_cast<DictMethodNode*>(node)) {
        dictMethod->getType(env);
        checkExpr(dictMethod, env, currentClassName, currentBaseClassName);
    }
    if (dynamic_cast<StructDeclNode*>(node)) return;
    if (dynamic_cast<EnumDeclNode*>(node)) return;

    if (auto decl = dynamic_cast<GlobalDeclNode*>(node)) {
        auto it = g_globalVars.find(decl->name);
        if (currentFuncName.empty() || it == g_globalVars.end() || it->second.first != decl->type ||
            it->second.second != decl->customType || declaredInLocalScope(env, decl->name)) {
            std::cerr << "[Static Type Error]: Invalid global declaration for '" << decl->name
                      << "'. Expected a matching top-level variable and no conflicting local.\n";
            exit(1);
        }
        rejectConstWrite(*env, decl->name);
        env->writableGlobals.insert(decl->name);
        return;
    }
    if (auto varDecl = dynamic_cast<VarDeclNode*>(node)) {
        // Top-level variables were pre-registered as globals, so their own
        // declaration is not a duplicate. The type checker may visit a const
        // declaration again while inferring collection types across calls.
        const auto previousConst = env->constants.find(varDecl->varName);
        const bool sameConst = previousConst != env->constants.end() && previousConst->second == varDecl;
        const bool registeredMutableGlobal = env.get() == g_programEnv.get() &&
            env->globals.count(varDecl->varName) && !varDecl->isConst && previousConst == env->constants.end();
        if ((canWriteGlobal(env, varDecl->varName) || env->variables.count(varDecl->varName)) &&
            !sameConst && !registeredMutableGlobal) {
            std::cerr << "[Static Type Error]: Variable '" << varDecl->varName 
                      << "' has already been declared in the same scope!\n";
            exit(1);
        }
        if (varDecl->initExpr) {
            ThavaType exprType = varDecl->initExpr->getType(env);
            if (!isCompatible(varDecl->varType, exprType) && exprType != ThavaType::UNKNOWN) {
                std::cerr << "[Static Type Error]: Variable '" << varDecl->varName 
                          << "' was declared with type <" << typeToString(varDecl->varType)
                          << "> but assigned a value of type <" << typeToString(exprType) << ">!\n";
                exit(1);
            }
            checkExpr(varDecl->initExpr.get(), env, currentClassName, currentBaseClassName);
            if (auto sam = dynamic_cast<StrAdvMethodNode*>(varDecl->initExpr.get())) {
                if (sam->method == "split") env->defineListElem(varDecl->varName, ThavaType::STRING);
            }
        }
        env->define(varDecl->varName, varDecl->varType, varDecl->customTypeName,
                    varDecl->isConst ? varDecl : nullptr);
        if ((varDecl->varType == ThavaType::LIST || varDecl->varType == ThavaType::TUPLE) &&
            varDecl->initExpr && env->getListElem(varDecl->varName) == ThavaType::UNKNOWN) {
            ThavaType elemT = inferCollectionElemType(varDecl->initExpr.get(), env);
            if (elemT != ThavaType::UNKNOWN) env->defineListElem(varDecl->varName, elemT);
        }
    }
    else if (auto assign = dynamic_cast<AssignNode*>(node)) {
        if (!env->count(assign->name)) {
            std::cerr << "[Static Type Error]: Variable '" << assign->name << "' is not declared before assignment!\n";
            exit(1);
        }
        rejectVariableWrite(env, assign->name, currentFuncName);
        if (auto sam = dynamic_cast<StrAdvMethodNode*>(assign->expr.get())) {
            if (sam->method == "split") env->defineListElem(assign->name, ThavaType::STRING);
        }
        else if (env->get(assign->name) == ThavaType::LIST || env->get(assign->name) == ThavaType::TUPLE) {
            // `xs = [...]` may change the element representation (int <-> double <-> string).
            ThavaType elemT = inferCollectionElemType(assign->expr.get(), env);
            if (elemT != ThavaType::UNKNOWN) env->defineListElem(assign->name, elemT);
        }
        ThavaType varType = env->get(assign->name);
        ThavaType exprType = assign->expr->getType(env);
        if (!isCompatible(varType, exprType) && exprType != ThavaType::UNKNOWN && exprType != ThavaType::NONE) {
            std::cerr << "[Static Type Error]: Cannot assign expression of type <" << typeToString(exprType)
                      << "> to variable '" << assign->name << "' of type <" << typeToString(varType) << ">!\n";
            exit(1);
        }
        if (assign->op == "-=" || assign->op == "*=" || assign->op == "/=" || assign->op == "%=") {
            if (varType == ThavaType::STRING) {
                std::cerr << "[Static Type Error]: Operator '" << assign->op << "' is not permitted on string <str>!\n";
                exit(1);
            }
        }
        // Share the normal expression semantics (including exponentiation and string concatenation)
        // across all backends, then store through the existing global assignment path.
        if (assign->op != "=" && canWriteGlobal(env, assign->name)) {
            if ((varType != ThavaType::INT && varType != ThavaType::FLOAT &&
                 varType != ThavaType::DOUBLE && !(varType == ThavaType::STRING && assign->op == "+=")) ||
                (assign->op == "%=" && (varType != ThavaType::INT ||
                 (exprType != ThavaType::INT && exprType != ThavaType::UNKNOWN)))) {
                std::cerr << "[Static Type Error]: Unsupported global compound assignment '" << assign->op
                          << "' for '" << assign->name << "'.\n";
                exit(1);
            }
            assign->expr = std::make_unique<BinaryOpNode>(std::make_unique<VarNode>(assign->name),
                assign->op.substr(0, assign->op.size() - 1), std::move(assign->expr));
            assign->op = "=";
        }
        checkExpr(assign->expr.get(), env, currentClassName, currentBaseClassName);
    }
    else if (auto idxAssign = dynamic_cast<IndexAssignNode*>(node)) {
        if (!env->count(idxAssign->varName)) {
            std::cerr << "[Static Type Error]: Variable '" << idxAssign->varName << "' is not declared before index assignment!\n";
            exit(1);
        }
        rejectVariableWrite(env, idxAssign->varName, currentFuncName);
        idxAssign->indexExpr->getType(env);
        idxAssign->valExpr->getType(env);
        checkExpr(idxAssign->indexExpr.get(), env, currentClassName, currentBaseClassName);
        checkExpr(idxAssign->valExpr.get(), env, currentClassName, currentBaseClassName);
    }
    else if (auto deferred = dynamic_cast<DeferNode*>(node)) {
        checkTypes(deferred->stmt.get(), env, currentFuncRetType, currentFuncName, currentClassName, currentBaseClassName);
    }
    else if (auto block = dynamic_cast<BlockNode*>(node)) {
        // Thava uses function-level scoping: a declaration inside a block
        // (if/while/for/try) stays visible until the end of the function.
        auto innerEnv = env;
        for (const auto& stmt : block->statements) {
            checkTypes(stmt.get(), innerEnv, currentFuncRetType, currentFuncName, currentClassName, currentBaseClassName);
        }
    }
    else if (auto ifNode = dynamic_cast<IfNode*>(node)) {
        ThavaType condType = ifNode->cond->getType(env);
        if (condType != ThavaType::BOOL && condType != ThavaType::UNKNOWN) {
            std::cerr << "[Static Type Error]: 'if' condition must be of type <bl>!\n";
            exit(1);
        }
        checkExpr(ifNode->cond.get(), env, currentClassName, currentBaseClassName);
        checkTypes(ifNode->thenBranch.get(), env, currentFuncRetType, currentFuncName, currentClassName, currentBaseClassName);
        if (ifNode->elseBranch) {
            checkTypes(ifNode->elseBranch.get(), env, currentFuncRetType, currentFuncName, currentClassName, currentBaseClassName);
        }
    }
    else if (auto whileNode = dynamic_cast<WhileNode*>(node)) {
        ThavaType condType = whileNode->cond->getType(env);
        if (condType != ThavaType::BOOL && condType != ThavaType::UNKNOWN) {
            std::cerr << "[Static Type Error]: 'while' condition must be of type <bl>!\n";
            exit(1);
        }
        checkExpr(whileNode->cond.get(), env, currentClassName, currentBaseClassName);
        checkTypes(whileNode->body.get(), env, currentFuncRetType, currentFuncName, currentClassName, currentBaseClassName);
    }
    else if (auto funcDecl = dynamic_cast<FunctionDeclNode*>(node)) {
        // A function sees its own parameters/locals plus the program's globals.
        auto funcEnv = std::make_shared<Environment>(g_programEnv);
        funcEnv->functionName = funcDecl->name;
        for (size_t i = 0; i < funcDecl->params.size(); ++i) {
            std::string cType = (i < funcDecl->paramCustomTypes.size()) ? funcDecl->paramCustomTypes[i] : "";
            funcEnv->define(funcDecl->params[i].second, funcDecl->params[i].first, cType);
            // Give a collection parameter the element type its call sites use, so
            // `param[i]` is typed (and printed/compared) correctly in this body.
            if (funcDecl->params[i].first == ThavaType::LIST || funcDecl->params[i].first == ThavaType::TUPLE) {
                auto eit = g_funcListParamElem.find(funcDecl->name);
                if (eit != g_funcListParamElem.end() && i < eit->second.size() &&
                    eit->second[i] != ThavaType::UNKNOWN) {
                    funcEnv->defineListElem(funcDecl->params[i].second, eit->second[i]);
                }
            }
        }
        for (const auto& stmt : funcDecl->body) {
            checkTypes(stmt.get(), funcEnv, funcDecl->returnType, funcDecl->name, "", "");
        }
    }
    else if (auto ret = dynamic_cast<ReturnNode*>(node)) {
        if (ret->expr) {
            ThavaType retType = ret->expr->getType(env);
            if (currentFuncRetType == ThavaType::VOID) {
                std::cerr << "[Static Type Error]: Function '" << currentFuncName
                          << "' is a void function and cannot return a value!\n";
                exit(1);
            }
            if (currentFuncRetType != ThavaType::UNKNOWN) {
                if (!isCompatible(currentFuncRetType, retType) && retType != ThavaType::UNKNOWN) {
                    std::cerr << "[Static Type Error]: Function '" << currentFuncName
                              << "' khai bao tra ve <" << typeToString(currentFuncRetType)
                              << "> but 'return' returns type <" << typeToString(retType) << ">!\n";
                    exit(1);
                }
            }
            checkExpr(ret->expr.get(), env, currentClassName, currentBaseClassName);
        } else {
            if (currentFuncRetType != ThavaType::VOID && currentFuncRetType != ThavaType::UNKNOWN) {
                std::cerr << "[Static Type Error]: Function '" << currentFuncName
                          << "' khai bao tra ve <" << typeToString(currentFuncRetType)
                          << "> cannot use empty 'return'!\n";
                exit(1);
            }
        }
    }
    else if (auto rangeFor = dynamic_cast<RangeForNode*>(node)) {
        // Thava uses function-level scoping: a declaration inside a block
        // (if/while/for/try) stays visible until the end of the function.
        auto innerEnv = env;
        ThavaType startT = rangeFor->startExpr->getType(env);
        ThavaType stopT = rangeFor->stopExpr->getType(env);
        ThavaType stepT = rangeFor->stepExpr->getType(env);
        if (!isCompatible(rangeFor->varType, startT) && startT != ThavaType::UNKNOWN) {
            std::cerr << "[Static Type Error]: range() start value type mismatch <"
                      << typeToString(rangeFor->varType) << ">!\n";
            exit(1);
        }
        if (!isCompatible(rangeFor->varType, stopT) && stopT != ThavaType::UNKNOWN) {
            std::cerr << "[Static Type Error]: range() stop value type mismatch <"
                      << typeToString(rangeFor->varType) << ">!\n";
            exit(1);
        }
        if (!isCompatible(rangeFor->varType, stepT) && stepT != ThavaType::UNKNOWN) {
            std::cerr << "[Static Type Error]: range() step value type mismatch <"
                      << typeToString(rangeFor->varType) << ">!\n";
            exit(1);
        }
        checkExpr(rangeFor->startExpr.get(), env, currentClassName, currentBaseClassName);
        checkExpr(rangeFor->stopExpr.get(), env, currentClassName, currentBaseClassName);
        checkExpr(rangeFor->stepExpr.get(), env, currentClassName, currentBaseClassName);
        rejectConstWrite(*innerEnv, rangeFor->varName);
        innerEnv->variables[rangeFor->varName] = rangeFor->varType;
        checkTypes(rangeFor->body.get(), innerEnv, currentFuncRetType, currentFuncName, currentClassName, currentBaseClassName);
    }
    else if (auto forNode = dynamic_cast<ForNode*>(node)) {
        // Counter variable is strictly scoped to the for loop
        // Thava uses function-level scoping: a declaration inside a block
        // (if/while/for/try) stays visible until the end of the function.
        auto innerEnv = env;
        checkTypes(forNode->init.get(), innerEnv, currentFuncRetType, currentFuncName, currentClassName, currentBaseClassName);
        ThavaType condType = forNode->cond->getType(innerEnv);
        if (condType != ThavaType::BOOL && condType != ThavaType::UNKNOWN) {
            std::cerr << "[Static Type Error]: 'for' condition must be of type <bl>!\n";
            exit(1);
        }
        checkExpr(forNode->cond.get(), innerEnv, currentClassName, currentBaseClassName);
        checkTypes(forNode->update.get(), innerEnv, currentFuncRetType, currentFuncName, currentClassName, currentBaseClassName);
        checkTypes(forNode->body.get(), innerEnv, currentFuncRetType, currentFuncName, currentClassName, currentBaseClassName);
    }
    else if (dynamic_cast<BreakNode*>(node) || dynamic_cast<ContinueNode*>(node)) {
        // break and continue are valid in type checking
    }
    else if (auto forEach = dynamic_cast<ForEachNode*>(node)) {
        ThavaType collType = forEach->collectionExpr->getType(env);
        if (collType != ThavaType::LIST && collType != ThavaType::TUPLE && collType != ThavaType::STRING && collType != ThavaType::DICT && collType != ThavaType::UNKNOWN) {
            std::cerr << "[Static Type Error]: 'for ... in' requires a collection (list, tuple, string, or dict), got <"
                      << typeToString(collType) << ">!\n";
            exit(1);
        }
        checkExpr(forEach->collectionExpr.get(), env, currentClassName, currentBaseClassName);
        // Thava uses function-level scoping: a declaration inside a block
        // (if/while/for/try) stays visible until the end of the function.
        auto innerEnv = env;
        ThavaType elemType = forEach->varType;
        if (elemType == ThavaType::UNKNOWN) {
            if (collType == ThavaType::STRING || collType == ThavaType::DICT) elemType = ThavaType::STRING;
            else {
                if (auto v = dynamic_cast<VarNode*>(forEach->collectionExpr.get())) {
                    ThavaType le = env->getListElem(v->getName());
                    if (le != ThavaType::UNKNOWN) elemType = le;
                    else elemType = ThavaType::INT;
                } else if (auto sam = dynamic_cast<StrAdvMethodNode*>(forEach->collectionExpr.get())) {
                    if (sam->method == "split") elemType = ThavaType::STRING;
                    else elemType = ThavaType::INT;
                } else {
                    elemType = ThavaType::INT;
                }
            }
            forEach->varType = elemType;
        }
        rejectConstWrite(*innerEnv, forEach->varName);
        innerEnv->variables[forEach->varName] = forEach->varType;
        checkTypes(forEach->body.get(), innerEnv, currentFuncRetType, currentFuncName, currentClassName, currentBaseClassName);
    }
    else if (auto consOut = dynamic_cast<ConsOutNode*>(node)) {
        consOut->getExpr()->getType(env);
        checkExpr(consOut->getExpr(), env, currentClassName, currentBaseClassName);
    }
    else if (auto exprStmt = dynamic_cast<ExpressionStatementNode*>(node)) {
        exprStmt->expr->getType(env);
        checkExpr(exprStmt->expr.get(), env, currentClassName, currentBaseClassName);
    }
    else if (auto memAssign = dynamic_cast<MemberAssignNode*>(node)) {
        checkExpr(memAssign, env, currentClassName, currentBaseClassName);
    }
    else if (auto methodCall = dynamic_cast<MethodCallNode*>(node)) {
        checkExpr(methodCall, env, currentClassName, currentBaseClassName);
    }
    else if (auto clsInst = dynamic_cast<ClassInstantiateNode*>(node)) {
        checkExpr(clsInst, env, currentClassName, currentBaseClassName);
    }
    else if (auto fileBlock = dynamic_cast<FileOpenBlockNode*>(node)) {
        ThavaType pt = fileBlock->pathExpr->getType(env);
        if (pt != ThavaType::STRING && pt != ThavaType::UNKNOWN) {
            std::cerr << "[Static Type Error]: File path in file.open(...) must be <str>!\n";
            exit(1);
        }
        if (fileBlock->modeExpr) {
            ThavaType mt = fileBlock->modeExpr->getType(env);
            if (mt != ThavaType::STRING && mt != ThavaType::UNKNOWN) {
                std::cerr << "[Static Type Error]: Mode in file.open(path, mode) must be <str>!\n";
                exit(1);
            }
        }
        checkExpr(fileBlock->pathExpr.get(), env, currentClassName, currentBaseClassName);
        if (fileBlock->modeExpr) checkExpr(fileBlock->modeExpr.get(), env, currentClassName, currentBaseClassName);
        // Thava uses function-level scoping: a declaration inside a block
        // (if/while/for/try) stays visible until the end of the function.
        auto innerEnv = env;
        rejectConstWrite(*innerEnv, fileBlock->varName);
        innerEnv->variables[fileBlock->varName] = ThavaType::FILE;
        for (const auto& stmt : fileBlock->body) {
            checkTypes(stmt.get(), innerEnv, currentFuncRetType, currentFuncName, currentClassName, currentBaseClassName);
        }
    }
    else if (auto fm = dynamic_cast<FileMethodNode*>(node)) {
        fm->getType(env);
    }
    else if (auto osM = dynamic_cast<OsMethodNode*>(node)) {
        osM->getType(env);
        for (auto& a : osM->args) checkExpr(a.get(), env, currentClassName, currentBaseClassName);
    }
    else if (auto timeM = dynamic_cast<TimeMethodNode*>(node)) {
        timeM->getType(env);
        for (auto& a : timeM->args) checkExpr(a.get(), env, currentClassName, currentBaseClassName);
    }
    else if (auto clibM = dynamic_cast<CLibMethodNode*>(node)) {
        clibM->getType(env);
        if (clibM->target) checkExpr(clibM->target.get(), env, currentClassName, currentBaseClassName);
        for (auto& a : clibM->args) checkExpr(a.get(), env, currentClassName, currentBaseClassName);
    }
    else if (auto tryExcept = dynamic_cast<TryExceptNode*>(node)) {
        auto tryEnv = env;
        for (const auto& stmt : tryExcept->tryBody) {
            checkTypes(stmt.get(), tryEnv, currentFuncRetType, currentFuncName, currentClassName, currentBaseClassName);
        }
        for (const auto& clause : tryExcept->exceptClauses) {
            auto excEnv = env;
            if (!clause.varName.empty()) {
                rejectConstWrite(*excEnv, clause.varName);
                excEnv->variables[clause.varName] = ThavaType::STRING;
            }
            for (const auto& stmt : clause.body) {
                checkTypes(stmt.get(), excEnv, currentFuncRetType, currentFuncName, currentClassName, currentBaseClassName);
            }
        }
    }
    else if (auto throwNode = dynamic_cast<ThrowNode*>(node)) {
        if (throwNode->expr) {
            throwNode->expr->getType(env);
            checkExpr(throwNode->expr.get(), env, currentClassName, currentBaseClassName);
        }
    }
    else if (auto raiseNode = dynamic_cast<RaiseErrorNode*>(node)) {
        if (raiseNode->isCompileTime) {
            std::string errType = "CompileError";
            std::string errMsg = "Compilation Error!";

            if (auto lit = dynamic_cast<LiteralNode*>(raiseNode->typeExpr.get())) {
                errType = lit->getVal();
            }
            if (auto lit = dynamic_cast<LiteralNode*>(raiseNode->msgExpr.get())) {
                errMsg = lit->getVal();
            }

            std::cerr << "[Compilation Error (" << errType << ")]: " << errMsg << " at line " << raiseNode->line << "\n";
            exit(1);
        }
        if (raiseNode->typeExpr) {
            raiseNode->typeExpr->getType(env);
            checkExpr(raiseNode->typeExpr.get(), env, currentClassName, currentBaseClassName);
        }
        if (raiseNode->msgExpr) {
            raiseNode->msgExpr->getType(env);
            checkExpr(raiseNode->msgExpr.get(), env, currentClassName, currentBaseClassName);
        }
    }
    else if (auto clsDecl = dynamic_cast<ClassDeclNode*>(node)) {
        auto classEnv = std::make_shared<Environment>(env);
        classEnv->currentClassName = clsDecl->className;
        classEnv->currentBaseClassName = clsDecl->baseClassName;
        for (const auto& f : clsDecl->fields) {
            classEnv->define(f.name, f.type, f.customTypeName);
            if (f.initExpr) {
                f.initExpr->getType(classEnv);
                checkExpr(f.initExpr.get(), classEnv, clsDecl->className, clsDecl->baseClassName);
            }
        }

        for (const auto& c : clsDecl->constructors) {
            auto ctorEnv = std::make_shared<Environment>(classEnv);
            ctorEnv->functionName = "init";
            auto ceit = g_funcListParamElem.find(clsDecl->className + "::init");
            for (size_t i = 0; i < c.params.size(); ++i) {
                std::string cType = (i < c.paramCustomTypes.size()) ? c.paramCustomTypes[i] : "";
                ctorEnv->define(c.params[i].second, c.params[i].first, cType);
                if ((c.params[i].first == ThavaType::LIST || c.params[i].first == ThavaType::TUPLE) &&
                    ceit != g_funcListParamElem.end() && i < ceit->second.size() &&
                    ceit->second[i] != ThavaType::UNKNOWN) {
                    ctorEnv->defineListElem(c.params[i].second, ceit->second[i]);
                }
            }
            for (const auto& stmt : c.body) {
                checkTypes(stmt.get(), ctorEnv, ThavaType::VOID, "init", clsDecl->className, clsDecl->baseClassName);
            }
        }

        for (const auto& m : clsDecl->methods) {
            auto methodEnv = std::make_shared<Environment>(classEnv);
            methodEnv->functionName = m.name;
            auto meit = g_funcListParamElem.find(clsDecl->className + "::" + m.name);
            for (size_t i = 0; i < m.params.size(); ++i) {
                std::string cType = (i < m.paramCustomTypes.size()) ? m.paramCustomTypes[i] : "";
                methodEnv->define(m.params[i].second, m.params[i].first, cType);
                if ((m.params[i].first == ThavaType::LIST || m.params[i].first == ThavaType::TUPLE) &&
                    meit != g_funcListParamElem.end() && i < meit->second.size() &&
                    meit->second[i] != ThavaType::UNKNOWN) {
                    methodEnv->defineListElem(m.params[i].second, meit->second[i]);
                }
            }
            for (const auto& stmt : m.body) {
                checkTypes(stmt.get(), methodEnv, m.returnType, m.name, clsDecl->className, clsDecl->baseClassName);
            }
        }
    }
}

// ==========================================
// 6. CODE GENERATOR & NATIVE COMPILER
// ==========================================
#include "pe_emitter.hpp"
#include "elf_emitter.hpp"
#include "x86_encoder.hpp"
#include "native_codegen.hpp"
#include "ast_optimizer.hpp"
#include "arm64_encoder.hpp"
#include "elf_arm64_emitter.hpp"
#include "arm64_runtime.hpp"
#include "arm64_codegen.hpp" 

// Collect every variable declared anywhere inside a statement tree (blocks included),
// so the C++ backend can declare them at function scope.
static void thavaCollectVarDecls(ASTNode* node, std::vector<VarDeclNode*>& out) {
    if (!node) return;
    if (auto v = dynamic_cast<VarDeclNode*>(node)) { out.push_back(v); return; }
    if (auto b = dynamic_cast<BlockNode*>(node)) {
        for (const auto& s : b->statements) thavaCollectVarDecls(s.get(), out);
        return;
    }
    if (auto i = dynamic_cast<IfNode*>(node)) {
        thavaCollectVarDecls(i->thenBranch.get(), out);
        thavaCollectVarDecls(i->elseBranch.get(), out);
        return;
    }
    if (auto w = dynamic_cast<WhileNode*>(node)) { thavaCollectVarDecls(w->body.get(), out); return; }
    if (auto f = dynamic_cast<ForNode*>(node)) {
        thavaCollectVarDecls(f->init.get(), out);
        thavaCollectVarDecls(f->body.get(), out);
        return;
    }
    if (auto r = dynamic_cast<RangeForNode*>(node)) { thavaCollectVarDecls(r->body.get(), out); return; }
    if (auto fe = dynamic_cast<ForEachNode*>(node)) { thavaCollectVarDecls(fe->body.get(), out); return; }
    if (auto t = dynamic_cast<TryExceptNode*>(node)) {
        for (const auto& s : t->tryBody) thavaCollectVarDecls(s.get(), out);
        for (const auto& c : t->exceptClauses) {
            for (const auto& s : c.body) thavaCollectVarDecls(s.get(), out);
        }
        return;
    }
}

#include "i686_codegen.hpp"

// part 0 = globals of built-in types (declared before the class definitions, so
//          class methods can already refer to them)
// part 1 = globals holding class/struct values (declared after those definitions)
std::string thavaCppGlobalDecls(const std::vector<ASTNode*>& body, int part = 0) {
    std::string code;
    for (auto* n : body) {
        auto* vd = dynamic_cast<VarDeclNode*>(n);
        if (!vd) continue;
        std::string t = vd->cppTypeName();
        if (t == "auto") continue;
        g_cppTopLevelGlobals.insert(vd->varName);
        bool custom = (vd->varType == ThavaType::CUSTOM);
        if ((part == 1) != custom) continue;
        code += t + " thava_g_" + vd->varName + (g_classInfo.count(vd->customTypeName) && vd->initExpr ? "{_thava_none};\n" : "{};\n");
    }
    return code;
}

std::string thavaCppBodyCodePtrs(const std::vector<ASTNode*>& body, const std::string& indent,
                                 bool keepHoisted = false) {
    std::vector<VarDeclNode*> decls;
    for (auto* s : body) thavaCollectVarDecls(s, decls);

    if (!keepHoisted) g_cppHoisted.clear();
    std::string code;
    for (auto* d : decls) {
        // Globals are already declared at file scope; redeclaring them here would
        // shadow them inside the program body.
        if (keepHoisted && g_cppTopLevelGlobals.count(d->varName)) continue;
        std::string t = d->cppTypeName();
        if (t == "auto") continue; // cannot be used in a hoisted declaration
        if (!g_cppHoisted.insert(d->varName).second) continue;
        code += indent + t + " " + d->varName + (g_classInfo.count(d->customTypeName) && d->initExpr ? "{_thava_none};\n" : "{};\n");
    }

    // Names declared in this body shadow globals of the same name while the body
    // is generated.
    auto savedLocals = g_cppCurrentLocals;
    for (auto* d : decls) {
        if (keepHoisted && g_cppTopLevelGlobals.count(d->varName)) continue;
        g_cppCurrentLocals.insert(d->varName);
    }

    for (auto* s : body) code += indent + s->generateCode() + "\n";
    g_cppHoisted.clear();
    g_cppCurrentLocals = savedLocals;
    return code;
}

std::string thavaCppBodyCode(const std::vector<ASTPtr>& body, const std::string& indent,
                             const std::vector<std::pair<ThavaType, std::string>>& params) {
    auto savedLocals = g_cppCurrentLocals;
    for (const auto& param : params) g_cppCurrentLocals.insert(param.second);
    std::vector<ASTNode*> ptrs;
    ptrs.reserve(body.size());
    for (const auto& s : body) ptrs.push_back(s.get());
    auto code = thavaCppBodyCodePtrs(ptrs, indent);
    g_cppCurrentLocals = std::move(savedLocals);
    return code;
}

int main(int argc, char* argv[]) {
    // Route every compile error through the diagnostic formatter (file:line + source line + hint)
    static diag::Streambuf diagBuf;
    std::cerr.rdbuf(&diagBuf);

    std::string inputFile = "";
    std::vector<std::string> inputFiles;
    std::string outputFile = "";
    bool explicitOutput = false;
#ifdef _WIN32
    bool useCppBackend = false; // Default Native PE on Windows
    bool isElfMode = false;
#else
    bool useCppBackend = false; // Default Native ELF64 on Linux!
    bool isElfMode = true;
#endif
    bool isArm64Mode = false;
    bool isWinArm64Mode = false;
    bool isI686Mode = false;
    bool explicitNative = false;
    bool explicitCpp = false;
    bool isLibFlag = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if ((arg == "--info" || arg == "--meta") && i + 1 < argc) {
            inspectLibrary(argv[++i]);
            return 0;
        } else if (arg == "-o" && i + 1 < argc) {
            outputFile = argv[++i];
            explicitOutput = true;
        } else if (arg == "--backend" && i + 1 < argc) {
            std::string b = argv[++i];
            if (b == "cpp") { useCppBackend = true; explicitCpp = true; }
            else if (b == "cpp-i686" || b == "native-i686" || b == "elf-i686" || b == "pe-i686") {
                isI686Mode = true;
                isArm64Mode = false;
                isWinArm64Mode = false;
                explicitNative = b != "cpp-i686";
                explicitCpp = !explicitNative;
                useCppBackend = explicitCpp;
                if (b == "elf-i686" || b == "pe-i686") isElfMode = b == "elf-i686";
            }
            else if (b == "native") { useCppBackend = false; explicitNative = true; }
            else if (b == "elf") { useCppBackend = false; explicitNative = true; isElfMode = true; }
            else if (b == "pe")  { useCppBackend = false; explicitNative = true; isElfMode = false; isArm64Mode = false; }
            else if (b == "arm64" || b == "elf-arm64") { useCppBackend = false; explicitNative = true; isArm64Mode = true; isElfMode = true; isWinArm64Mode = false; }
            else if (b == "pe-arm64" || b == "win-arm64" || b == "arm64-pe") { useCppBackend = false; explicitNative = true; isArm64Mode = true; isElfMode = false; isWinArm64Mode = true; }
            else if (b == "cpp-arm64") { useCppBackend = true; explicitCpp = true; isArm64Mode = true; }
        } else if (arg == "--cpp") {
            useCppBackend = true; explicitCpp = true;
        } else if (arg == "--native") {
            useCppBackend = false; explicitNative = true;
        } else if (arg == "--elf") {
            useCppBackend = false; explicitNative = true; isElfMode = true;
        } else if (arg == "--pe") {
            useCppBackend = false; explicitNative = true; isElfMode = false; isArm64Mode = false;
        } else if (arg == "--arm64" || arg == "--aarch64") {
            isArm64Mode = true; isI686Mode = false;
        } else if (arg == "--target" && i + 1 < argc) {
            std::string t = argv[++i];
            if (t == "win-i686" || t == "linux-i686") {
                isI686Mode = true;
                isArm64Mode = false;
                isWinArm64Mode = false;
                isElfMode = t == "linux-i686";
            } else if (t == "win-arm64" || t == "windows-arm64" || t == "arm64-windows" || t == "pe-arm64") {
                isArm64Mode = true; isI686Mode = false; isWinArm64Mode = true; isElfMode = false;
            } else if (t == "linux-arm64" || t == "arm64-linux" || t == "elf-arm64") {
                isArm64Mode = true; isI686Mode = false; isWinArm64Mode = false; isElfMode = true;
            } else if (t == "win-x64" || t == "windows-x64") {
                isArm64Mode = false; isI686Mode = false; isWinArm64Mode = false; isElfMode = false;
            } else if (t == "linux-x64") {
                isArm64Mode = false; isI686Mode = false; isWinArm64Mode = false; isElfMode = true;
            }
        } else if (arg == "--os" && i + 1 < argc) {
            std::string o = argv[++i];
            if (o == "windows" || o == "win") {
                if (isArm64Mode) { isWinArm64Mode = true; isElfMode = false; }
                else { isElfMode = false; }
            } else if (o == "linux") {
                if (isArm64Mode) { isWinArm64Mode = false; isElfMode = true; }
                else { isElfMode = true; }
            }
        } else if (arg == "--arch" && i + 1 < argc) {
            std::string a = argv[++i];
            if (a == "arm64" || a == "aarch64") { isArm64Mode = true; isI686Mode = false; }
            else if (a == "i686" || a == "x86") {
                isI686Mode = true; isArm64Mode = false; isWinArm64Mode = false;
            } else if (a == "x86_64" || a == "x64" || a == "amd64") {
                isArm64Mode = false; isWinArm64Mode = false; isI686Mode = false;
            }
        } else if (arg == "--lib" || arg == "--shared" || arg == "--dll") {
            isLibFlag = true;
        } else if (arg == "-O0" || arg == "--O0") {
            g_optLevel = 0;
        } else if (arg == "-O1" || arg == "--O1") {
            g_optLevel = 1;
        } else if (arg == "-O2" || arg == "--O2") {
            g_optLevel = 2;
        } else if (arg == "-O3" || arg == "--O3") {
            g_optLevel = 3;
        } else if (arg == "--cxx" && i + 1 < argc) {
            g_explicitCxx = argv[++i];
        } else if (!arg.empty() && arg[0] != '-') { if (inputFile.empty()) inputFile = arg; inputFiles.push_back(arg); }
    }

    if (inputFile.empty()) {
        std::cout << "Usage: thavac <source.thv> [-o <output_binary>] [--backend native|elf|pe|arm64|cpp|cpp-i686|native-i686|elf-i686|pe-i686] [--arch x86_64|arm64|i686] [--lib] [-O0|-O1|-O2|-O3]\n"
                  << "       thavac <source.thv> --target win-i686|linux-i686 [--native|--cpp] [-o <output_binary>]\n"
                  << "       thavac --info <file.thdl|.so> (view metadata of dynamic library)\n";
        return 1;
    }

    if (isI686Mode) {
        if (explicitCpp && explicitNative) {
            std::cerr << "[Thava Compiler Error]: Choose either native i686 or the i686 C++ backend.\n";
            return 1;
        }
        useCppBackend = !explicitNative;
        explicitCpp = useCppBackend;
    }

    // Automatically detect dynamic library mode
    if (isLibFlag) {
        g_isLibraryMode = true;
    } else if (explicitOutput) {
        if ((outputFile.length() >= 5 && outputFile.substr(outputFile.length() - 5) == ".thdl") ||
            (outputFile.length() >= 3 && outputFile.substr(outputFile.length() - 3) == ".so") ||
            (outputFile.length() >= 4 && outputFile.substr(outputFile.length() - 4) == ".dll")) {
            g_isLibraryMode = true;
        }
    }

    // Handle output file extension
    std::string stemName = std::filesystem::path(inputFile).stem().string();
    if (!explicitOutput) {
        if (g_isLibraryMode) {
            outputFile = stemName + (isElfMode ? ".so" : ".thdl");
        } else {
            if (isArm64Mode) {
                if (isWinArm64Mode) outputFile = stemName + "_arm64.exe";
                else outputFile = stemName + "_arm64";
            }
            else if (isElfMode) outputFile = "output";
            else outputFile = "output.exe";
        }
    } else {
        if (g_isLibraryMode) {
            if (std::filesystem::path(outputFile).extension().empty())
                outputFile += isElfMode ? ".so" : ".thdl";
        } else if ((!isElfMode && !isArm64Mode) || isWinArm64Mode) {
            if (std::filesystem::path(outputFile).extension() != ".exe") outputFile += ".exe";
        }
    }

    // 1. Initialize global tables & include guard for @attach, @cattach & @dlattach
    g_dlPreferSo = isElfMode;
    g_attachedFiles.clear();
    g_cAttachedHeaders.clear();
    g_cAttachedSet.clear();
    g_hasCAttach = false;
    g_isCFunc.clear();
    g_classNames.clear();
    g_enums.clear();
    g_structs.clear();
    g_funcReturnTypes.clear();
    g_funcParamTypes.clear();
    g_funcMinArgs.clear();
    g_funcDefaultArgTokens.clear();
    g_dlFunctions.clear();
    g_dlLibraries.clear();
    g_dlLibrariesSet.clear();
    g_dlLibraryInfos.clear();
    g_hasDLAttach = false;
    g_hasCLib = false;

    std::vector<Token> tokens;
    int lastLine = 1;
    bool firstSource = true;
    for (const auto& inFile : inputFiles) {
        std::error_code ec;
        std::string normInput = std::filesystem::weakly_canonical(inFile, ec).string();
        if (normInput.empty()) normInput = inFile;
        if (g_attachedFiles.count(normInput)) {
            continue;
        }
        std::ifstream file(inFile);
        if (!file.is_open()) {
            std::cerr << "Error: Cannot open file " << inFile << "\n";
            return 1;
        }
        std::stringstream buffer;
        buffer << file.rdbuf();
        std::string source = buffer.str();

        if (firstSource) { diag::loadSource(inFile, source); firstSource = false; }

        g_attachedFiles.insert(normInput);
        Lexer lexer(source, normInput);
        auto fileTokens = lexer.scanTokens();
        if (!fileTokens.empty() && fileTokens.back().type == TokenType::EOF_TOKEN) {
            lastLine = fileTokens.back().line;
            fileTokens.pop_back();
        }
        tokens.insert(tokens.end(), fileTokens.begin(), fileTokens.end());
    }
    tokens.push_back({TokenType::EOF_TOKEN, "", lastLine});

    // A program may declare its own variable named `c`; then `c.<name>` is member
    // access, not the C-interop namespace (also honoured by @{...} snippets).
    g_cNameIsVariable = detectCNameVariable(tokens);

    // Dynamic Library Mode (--lib): fallback to C++ transpiler only when @cattach or explicitCpp is specified
    // By default, native direct machine code emission is used!
    if (g_isLibraryMode && (g_hasCAttach || explicitCpp)) {
        useCppBackend = true;
    }

    // Check @cattach directive (only rule triggering C++ transpiler fallback):
    if (g_hasCAttach) {
        if (explicitNative) {
            std::cerr << "[Compiler Error]: Source file uses @cattach (depends on C/C++ headers).\n"
                      << "Cannot compile directly via --backend native!\n"
                      << "Please remove --backend native to allow C++ fallback backend.\n";
            return 1;
        }
        if (!useCppBackend) {
            std::cout << "[Thava Compiler] Detected @cattach: Switching to C++ Transpiler to link C/C++ headers...\n";
            useCppBackend = true;
        }
    }

    // 2. Parser
    Parser parser(tokens);
    auto ast = parser.parse();

    // 3. Static Type Checker (Full scope traversal)
    auto globalEnv = std::make_shared<Environment>();
    // Top-level variables are globals: they are visible inside every function and
    // method, so register them all before checking anything (order independent).
    g_programEnv = globalEnv;
    g_globalVars.clear();
    for (const auto& stmt : ast) {
        if (auto vd = dynamic_cast<VarDeclNode*>(stmt.get())) {
            if (globalEnv->variables.count(vd->varName) && (vd->isConst || globalEnv->isConst(vd->varName))) {
                std::cerr << "[Static Type Error]: Const variable '" << vd->varName
                          << "' has already been declared in the same scope!\n";
                return 1;
            }
            globalEnv->define(vd->varName, vd->varType, vd->customTypeName, vd->isConst ? vd : nullptr);
            globalEnv->globals[vd->varName] = true;
            g_globalVars[vd->varName] = { vd->varType, vd->customTypeName };
        }
    }
    // A collection parameter's element type comes from its call sites, and those
    // calls may live in later functions, so repeat the traversal until the inferred
    // types stop changing (each pass propagates one call level further).
    for (int typePass = 0; typePass < 8; ++typePass) {
        auto before = g_funcListParamElem;
        for (const auto& stmt : ast) {
            checkTypes(stmt.get(), globalEnv);
        }
        if (before == g_funcListParamElem) break;
    }

    // 3.5. Optimization Pipeline (AST transformations for -O1, -O2, -O3)
    if (g_optLevel > 0) {
        std::cout << "[Thava Optimizer] Activating optimization level: -O" << g_optLevel;
        if (g_optLevel == 1) std::cout << " (Constant Folding, Dead Branch Pruning, Native Peephole)\n";
        else if (g_optLevel == 2) std::cout << " (Local Constant Propagation, Constant Folding, Dead Branch Pruning)\n";
        else if (g_optLevel >= 3) std::cout << " (Automatic Leaf Inlining, Local Constant Propagation, Constant Folding)\n";

        ASTOptimizer astOpt(g_optLevel);
        ast = astOpt.optimize(std::move(ast));
    }

    // 4. Code Generation
    if (isI686Mode && explicitNative) {
        try {
            i686::Codegen codegen(!isElfMode);
            if (codegen.compile(ast, outputFile)) {
                std::cout << "[Thava Compiler] Success! Native i686 "
                          << (isElfMode ? "ELF32" : "PE32") << ": " << outputFile << "\n";
                return 0;
            }
            std::cerr << "[Thava Compiler Error]: Could not write native i686 output '" << outputFile << "'.\n";
        } catch (const std::exception& ex) {
            std::cerr << "[i686 Codegen Error]: " << ex.what() << "\n";
        }
        return 1;
    }

    if (!useCppBackend) {
        if (isArm64Mode) {
            if (isWinArm64Mode) {
                std::cout << "[Thava Compiler] [Experimental] Generating Native Windows ARM64 (PE32+) executable directly (Zero-Dependencies)...\n";
                PeEmitter pe;
                pe.setArm64(true);
                arm64::Encoder enc;
                Arm64PeCodegen arm64Gen(pe, enc, globalEnv);
                arm64Gen.setOptLevel(g_optLevel);
                if (arm64Gen.compileProgram(ast, outputFile)) {
                    std::cout << "[Thava Compiler] Success! [Experimental] Native Windows ARM64 executable: " << outputFile << "\n";
                    return 0;
                } else {
                    std::cerr << "[Thava Compiler] Direct Windows ARM64 generation failed! Falling back to C++ cross-compiler...\n";
                    useCppBackend = true;
                }
            } else {
                std::cout << "[Thava Compiler] Generating Native Linux AArch64 (ARM64) ELF64 executable directly (Zero-Dependencies)...\n";
                ElfArm64Emitter elf;
                arm64::Encoder enc;
                Arm64ElfCodegen arm64Gen(elf, enc, globalEnv);
                arm64Gen.setOptLevel(g_optLevel);
                if (arm64Gen.compileProgram(ast, outputFile)) {
                    std::cout << "[Thava Compiler] Success! Native Linux AArch64 (ARM64) executable: " << outputFile << "\n";
                    return 0;
                } else {
                    std::cerr << "[Thava Compiler] Direct ARM64 generation failed! Falling back to C++ cross-compiler...\n";
                    useCppBackend = true;
                }
            }
        } else if (isElfMode) {
            // --- Native Linux ELF64 Backend (Direct x86-64 Machine Code & Linux Syscalls, Zero external tools) ---
            if (g_isLibraryMode) {
                std::cout << "[Thava Compiler] Generating Native ELF64 Linux dynamic library (.so) directly (Zero-Dependencies)...\n";
            } else {
                std::cout << "[Thava Compiler] Generating Native ELF64 Linux executable directly (Zero-Dependencies)...\n";
            }
            ElfEmitter elf;
            x86::Encoder enc;
            NativeCodegen<ElfEmitter> nativeGen(elf, enc, globalEnv);
            nativeGen.setOptLevel(g_optLevel);
            if (nativeGen.compileProgram(ast, outputFile)) {
                if (g_isLibraryMode) {
                    std::cout << "[Thava Compiler] Success! Native ELF64 Linux dynamic library: " << outputFile << "\n";
                } else {
                    std::cout << "[Thava Compiler] Success! Native ELF64 Linux executable: " << outputFile << "\n";
                }
                return 0;
            } else {
                std::cerr << "[Thava Compiler] Direct ELF generation failed! Falling back to C++...\n";
                useCppBackend = true;
            }
        } else {
            // --- Native PE Backend (Direct x86-64 Machine Code, Windows .exe / .thdl) ---
            if (g_isLibraryMode) {
                std::cout << "[Thava Compiler] Generating Native PE dynamic library (.thdl) x86-64 directly (Zero-Dependencies)...\n";
            } else {
                std::cout << "[Thava Compiler] Generating Native PE (.exe) x86-64 directly...\n";
            }
            PeEmitter pe;
            x86::Encoder enc;
            NativeCodegen<PeEmitter> nativeGen(pe, enc, globalEnv);
            nativeGen.setOptLevel(g_optLevel);
            if (nativeGen.compileProgram(ast, outputFile)) {
                if (g_isLibraryMode) {
                    std::cout << "[Thava Compiler] Success! Native PE dynamic library: " << outputFile << " (100% Embedded Metadata)\n";
                } else {
                    std::cout << "[Thava Compiler] Success! Native PE executable: " << outputFile << "\n";
                }
                return 0;
            } else {
                std::cerr << "[Thava Compiler] Direct PE generation failed! Falling back to C++...\n";
                useCppBackend = true;
            }
        }
    }

    // --- C++ Transpiler Backend (Optimized C++ Output) ---
    std::stringstream cppOutput;
    if (isI686Mode) {
        cppOutput << "#if !(defined(__i386__) || defined(_M_IX86)) || "
                  << (isElfMode ? "!defined(__linux__)" : "!defined(_WIN32)")
                  << "\n#error The selected C++ compiler must target "
                  << (isElfMode ? "Linux" : "Windows")
                  << " i686.\n#endif\n";
    } else if (isWinArm64Mode) {
        cppOutput << "#if !defined(_WIN32) || !(defined(__aarch64__) || defined(_M_ARM64))\n"
                  << "#error Select an ARM64 Windows C++ compiler or an ARM64 MSVC developer environment.\n"
                  << "#endif\n";
    }
    cppOutput << "// === Thava Compiler Generated C++ Source ===\n"
              << "// Optimizations: Fast I/O, pass-by-const-ref, inline runtime helpers\n\n";

    if (!g_cAttachedHeaders.empty()) {
        cppOutput << "// C/C++ headers attached via @cattach:\n";
        for (const auto& h : g_cAttachedHeaders) {
            cppOutput << "#include " << h << "\n";
        }
        cppOutput << "\n";
    }

    cppOutput << "#include <iostream>\n"
              << "#include <fstream>\n"
              << "#include <sstream>\n"
              << "#include <string>\n"
              << "#include <vector>\n"
              << "#include <map>\n"
              << "#include <functional>\n"
              << "#include <random>\n"
              << "#include <ctime>\n"
              << "#include <cstdint>\n"
              << "#include <cmath>\n"
              << "#include <type_traits>\n"
              << "#include <stdexcept>\n"
              << "#include <filesystem>\n"
              << "#include <thread>\n"
              << "#include <chrono>\n"
              << "#include <cstdlib>\n"
              << "#ifdef _WIN32\n"
              << "#include <windows.h>\n"
              << "#else\n"
              << "#include <unistd.h>\n"
              << "#include <sys/types.h>\n"
              << "#include <sys/stat.h>\n"
              << "#include <sys/wait.h>\n"
              << "#include <dlfcn.h>\n"
              << "#endif\n\n"
              << "#if defined(_WIN32)\n"
              << "#define THAVA_EXPORT __declspec(dllexport)\n"
              << "#else\n"
              << "#define THAVA_EXPORT __attribute__((visibility(\"default\")))\n"
              << "#endif\n\n"
              << "struct _ThavaScopeGuard {\n"
              << "    std::function<void()> fn;\n"
              << "    _ThavaScopeGuard(std::function<void()> f) : fn(std::move(f)) {}\n"
              << "    ~_ThavaScopeGuard() { if (fn) fn(); }\n"
              << "    _ThavaScopeGuard(const _ThavaScopeGuard&) = delete;\n"
              << "    _ThavaScopeGuard& operator=(const _ThavaScopeGuard&) = delete;\n"
              << "};\n\n"
              << "struct _ThavaFuncWrapper {\n"
              << "    void* ptr = nullptr;\n"
              << "    _ThavaFuncWrapper() : ptr(nullptr) {}\n"
              << "    _ThavaFuncWrapper(void* p) : ptr(p) {}\n"
              << "    _ThavaFuncWrapper(uintptr_t p) : ptr(reinterpret_cast<void*>(p)) {}\n"
              << "    template<typename R, typename... Args>\n"
              << "    _ThavaFuncWrapper(R (*p)(Args...)) : ptr(reinterpret_cast<void*>(p)) {}\n"
              << "    template<typename R, typename... Args>\n"
              << "    _ThavaFuncWrapper& operator=(R (*p)(Args...)) {\n"
              << "        ptr = reinterpret_cast<void*>(p);\n"
              << "        return *this;\n"
              << "    }\n"
              << "    template<typename... Args>\n"
              << "    long long operator()(Args... args) const {\n"
              << "        if (!ptr) return 0LL;\n"
              << "        typedef long long (*FnType)(Args...);\n"
              << "        return reinterpret_cast<FnType>(ptr)(args...);\n"
              << "    }\n"
              << "    explicit operator bool() const { return ptr != nullptr; }\n"
              << "    friend std::ostream& operator<<(std::ostream& os, const _ThavaFuncWrapper& w) {\n"
              << "        os << \"<func:\" << w.ptr << \">\";\n"
              << "        return os;\n"
              << "    }\n"
              << "};\n\n"
              << "struct ThavaException : public std::runtime_error {\n"
              << "    std::string typeName;\n"
              << "    ThavaException(const std::string& type, const std::string& msg)\n"
              << "        : std::runtime_error(msg), typeName(type) {}\n"
              << "    ThavaException(const std::string& msg)\n"
              << "        : std::runtime_error(msg), typeName(\"RuntimeError\") {}\n"
              << "};\n\n"
              << "template<typename T1, typename T2>\n"
              << "inline auto _thava_div(T1 a, T2 b) {\n"
              << "    if (b == 0) throw ThavaException(\"RuntimeError\", \"Division by zero!\");\n"
              << "    return a / b;\n"
              << "}\n"
              << "template<typename T1, typename T2>\n"
              << "inline auto _thava_mod(T1 a, T2 b) {\n"
              << "    if constexpr (std::is_floating_point_v<T1> || std::is_floating_point_v<T2>) {\n"
              << "        if (b == 0) throw ThavaException(\"RuntimeError\", \"Modulo by zero!\");\n"
              << "        return std::fmod(a, b);\n"
              << "    } else {\n"
              << "        if (b == 0) throw ThavaException(\"RuntimeError\", \"Modulo by zero!\");\n"
              << "        return a % b;\n"
              << "    }\n"
              << "}\n\n"
              << "template<typename T1, typename T2>\n"
              << "inline auto _thava_pow(T1 a, T2 b) {\n"
              << "    if constexpr (std::is_floating_point_v<T1> || std::is_floating_point_v<T2>) {\n"
              << "        return std::pow((double)a, (double)b);\n"
              << "    } else {\n"
              << "        if (b < 0) return 0LL;\n"
              << "        long long base = a, exp = b, res = 1;\n"
              << "        while (exp > 0) {\n"
              << "            if (exp & 1) res *= base;\n"
              << "            base *= base;\n"
              << "            exp >>= 1;\n"
              << "        }\n"
              << "        return res;\n"
              << "    }\n"
              << "}\n\n"
              << "// Automatically convert Thava strings to const char* when passing into C functions\n"
              << "template<typename T>\n"
              << "inline decltype(auto) _thava_c_arg(T&& val) {\n"
              << "    if constexpr (std::is_same_v<std::decay_t<T>, std::string>) {\n"
              << "        return val.c_str();\n"
              << "    } else {\n"
              << "        return std::forward<T>(val);\n"
              << "    }\n"
              << "}\n\n"
              << "struct ThavaNoneType;\n"
              << "struct ThavaValue;\n\n"
              << "inline long long _thava_to_int(long long v) { return v; }\n"
              << "inline long long _thava_to_int(int v) { return (long long)v; }\n"
              << "inline long long _thava_to_int(double v) { return (long long)v; }\n"
              << "inline long long _thava_to_int(float v) { return (long long)v; }\n"
              << "inline long long _thava_to_int(bool v) { return v ? 1LL : 0LL; }\n"
              << "inline long long _thava_to_int(const std::string& s) { try { return std::stoll(s); } catch (...) { return 0LL; } }\n\n"
              << "inline float _thava_to_float(float v) { return v; }\n"
              << "inline float _thava_to_float(double v) { return (float)v; }\n"
              << "inline float _thava_to_float(long long v) { return (float)v; }\n"
              << "inline float _thava_to_float(int v) { return (float)v; }\n"
              << "inline float _thava_to_float(bool v) { return v ? 1.0f : 0.0f; }\n"
              << "inline float _thava_to_float(const std::string& s) { try { return std::stof(s); } catch (...) { return 0.0f; } }\n\n"
              << "inline double _thava_to_double(double v) { return v; }\n"
              << "inline double _thava_to_double(float v) { return (double)v; }\n"
              << "inline double _thava_to_double(long long v) { return (double)v; }\n"
              << "inline double _thava_to_double(int v) { return (double)v; }\n"
              << "inline double _thava_to_double(bool v) { return v ? 1.0 : 0.0; }\n"
              << "inline double _thava_to_double(const std::string& s) { try { return std::stod(s); } catch (...) { return 0.0; } }\n\n"
              << "inline bool _thava_to_bool(bool v) { return v; }\n"
              << "inline bool _thava_to_bool(long long v) { return v != 0; }\n"
              << "inline bool _thava_to_bool(double v) { return v != 0.0; }\n"
              << "inline bool _thava_to_bool(float v) { return v != 0.0f; }\n"
              << "inline bool _thava_to_bool(const std::string& s) { return !s.empty() && s != \"false\" && s != \"0\"; }\n\n"
              << "inline std::string _thava_to_string(const std::string& s) { return s; }\n"
              << "inline std::string _thava_to_string(long long v) { return std::to_string(v); }\n"
              << "inline std::string _thava_to_string(int v) { return std::to_string(v); }\n"
              << "inline std::string _thava_to_string(short v) { return std::to_string((int)v); }\n"
              << "inline std::string _thava_to_string(double v) { std::string s = std::to_string(v); if (s.find('.') != std::string::npos) { while (s.back() == '0') s.pop_back(); if (s.back() == '.') s.pop_back(); } return s; }\n"
              << "inline std::string _thava_to_string(float v) { return _thava_to_string((double)v); }\n"
              << "inline std::string _thava_to_string(bool v) { return v ? \"true\" : \"false\"; }\n\n"
              << "inline long long _thava_to_int(const ThavaNoneType&) { return 0LL; }\n"
              << "inline float _thava_to_float(const ThavaNoneType&) { return 0.0f; }\n"
              << "inline double _thava_to_double(const ThavaNoneType&) { return 0.0; }\n"
              << "inline std::string _thava_to_string(const ThavaNoneType&) { return \"none\"; }\n"
              << "inline bool _thava_to_bool(const ThavaNoneType&) { return false; }\n\n"
              << "inline long long _thava_to_int(const ThavaValue& v);\n"
              << "inline float _thava_to_float(const ThavaValue& v);\n"
              << "inline double _thava_to_double(const ThavaValue& v);\n"
              << "inline bool _thava_to_bool(const ThavaValue& v);\n"
              << "inline std::string _thava_to_string(const ThavaValue& v);\n\n"
              << "std::ostream& operator<<(std::ostream& os, const std::vector<ThavaValue>& vec);\n"
              << "struct ThavaDict;\n"
              << "std::ostream& operator<<(std::ostream& os, const ThavaDict& d);\n\n"
              << "struct ThavaValue {\n"
              << "    long long iVal = 0;\n"
              << "    double fVal = 0.0;\n"
              << "    bool bVal = false;\n"
              << "    std::string sVal = \"\";\n"
              << "    std::vector<ThavaValue> lVal;\n"
              << "    std::shared_ptr<ThavaDict> dVal;\n"
              << "    int type = 0; // 0: empty, 1: int, 2: float/double, 3: bool, 4: string, 5: list, 6: dict\n"
              << "    ThavaValue() {}\n"
              << "    ThavaValue(long long v) : iVal(v), type(1) {}\n"
              << "    ThavaValue(int v) : iVal((long long)v), type(1) {}\n"
              << "    ThavaValue(double v) : fVal(v), type(2) {}\n"
              << "    ThavaValue(float v) : fVal(v), type(2) {}\n"
              << "    ThavaValue(bool v) : bVal(v), type(3) {}\n"
              << "    ThavaValue(std::string v) : sVal(v), type(4) {}\n"
              << "    ThavaValue(const char* v) : sVal(v ? v : \"\"), type(4) {}\n"
              << "    ThavaValue(std::vector<ThavaValue> v) : lVal(v), type(5) {}\n"
              << "    ThavaValue(const ThavaDict& d);\n"
              << "    ThavaValue(const ThavaNoneType&);\n\n"
              << "    // Provide implicit conversion operators to easily convert back to native C++ types.\n"
              << "    // Numeric conversions honour the runtime tag so an int-tagged value is never\n"
              << "    // read through the double slot (that used to yield 0 for list/dict reads\n"
              << "    // returned from a function).\n"
              << "    operator long long() const { return type == 2 ? (long long)fVal : iVal; }\n"
              << "    operator int() const { return type == 2 ? (int)fVal : (int)iVal; }\n"
              << "    operator double() const { return type == 1 ? (double)iVal : fVal; }\n"
              << "    operator float() const { return type == 1 ? (float)iVal : (float)fVal; }\n"
              << "    operator bool() const { return type == 3 ? bVal : (type == 2 ? (fVal != 0.0) : (iVal != 0)); }\n"
              << "    operator std::string() const { return sVal; }\n"
              << "    operator std::vector<ThavaValue>() const { return lVal; }\n"
              << "    operator ThavaDict() const;\n\n"
              << "    bool has(const ThavaValue& k) const;\n"
              << "    bool contains(const ThavaValue& k) const;\n"
              << "    ThavaValue get(const ThavaValue& k, const ThavaValue& def = ThavaValue()) const;\n"
              << "    void set(const ThavaValue& k, const ThavaValue& v);\n"
              << "    bool remove(const ThavaValue& k);\n"
              << "    void clear();\n"
              << "    long long size() const;\n"
              << "    long long len() const;\n"
              << "    std::vector<ThavaValue> keys() const;\n"
              << "    std::vector<ThavaValue> values() const;\n"
              << "    std::vector<ThavaValue> items() const;\n"
              << "};\n\n"
              << "struct ThavaNoneType {\n"
              << "    operator long long() const { return 0LL; }\n"
              << "    operator double() const { return 0.0; }\n"
              << "    operator float() const { return 0.0f; }\n"
              << "    operator bool() const { return false; }\n"
              << "    operator std::string() const { return \"\"; }\n"
              << "    operator std::vector<ThavaValue>() const { return {}; }\n"
              << "    operator ThavaValue() const { return ThavaValue(); }\n"
              << "    operator ThavaDict() const;\n"
              << "    template<typename T> operator T*() const { return nullptr; }\n"
              << "};\n"
              << "inline constexpr ThavaNoneType _thava_none{};\n"
              << "inline ThavaValue::ThavaValue(const ThavaNoneType&) : type(0) {}\n\n"
              << "struct ThavaDict {\n"
              << "    std::vector<std::pair<ThavaValue, ThavaValue>> entries;\n"
              << "    ThavaDict() {}\n"
              << "    ThavaDict(std::initializer_list<std::pair<ThavaValue, ThavaValue>> init) : entries(init) {}\n"
              << "    ThavaDict(const std::vector<std::pair<ThavaValue, ThavaValue>>& init) : entries(init) {}\n"
              << "    bool has(const ThavaValue& key) const;\n"
              << "    bool contains(const ThavaValue& key) const { return has(key); }\n"
              << "    ThavaValue get(const ThavaValue& key, const ThavaValue& defaultVal = ThavaValue()) const;\n"
              << "    ThavaValue get_or_throw(const ThavaValue& key) const;\n"
              << "    void set(const ThavaValue& key, const ThavaValue& val);\n"
              << "    bool remove(const ThavaValue& key);\n"
              << "    void clear() { entries.clear(); }\n"
              << "    long long size() const { return (long long)entries.size(); }\n"
              << "    long long len() const { return (long long)entries.size(); }\n"
              << "    std::vector<ThavaValue> keys() const;\n"
              << "    std::vector<ThavaValue> values() const;\n"
              << "    std::vector<ThavaValue> items() const;\n"
              << "};\n\n"
              << "inline ThavaValue::ThavaValue(const ThavaDict& d) : dVal(std::make_shared<ThavaDict>(d)), type(6) {}\n"
              << "inline ThavaValue::operator ThavaDict() const { if (type == 6 && dVal) return *dVal; return ThavaDict(); }\n"
              << "inline ThavaNoneType::operator ThavaDict() const { return ThavaDict(); }\n\n"
              << "inline bool operator==(const ThavaDict& d, const ThavaNoneType&) { return d.entries.empty(); }\n"
              << "inline bool operator==(const ThavaNoneType&, const ThavaDict& d) { return d.entries.empty(); }\n"
              << "inline bool operator!=(const ThavaDict& d, const ThavaNoneType&) { return !d.entries.empty(); }\n"
              << "inline bool operator!=(const ThavaNoneType&, const ThavaDict& d) { return !d.entries.empty(); }\n\n"
              << "inline bool operator==(const ThavaNoneType&, const ThavaNoneType&) { return true; }\n"
              << "inline bool operator!=(const ThavaNoneType&, const ThavaNoneType&) { return false; }\n\n"
              << "template<typename T, typename std::enable_if<std::is_arithmetic<T>::value, int>::type = 0>\n"
              << "inline bool operator==(T val, const ThavaNoneType&) { return val == 0; }\n"
              << "template<typename T, typename std::enable_if<std::is_arithmetic<T>::value, int>::type = 0>\n"
              << "inline bool operator==(const ThavaNoneType&, T val) { return val == 0; }\n"
              << "template<typename T, typename std::enable_if<std::is_arithmetic<T>::value, int>::type = 0>\n"
              << "inline bool operator!=(T val, const ThavaNoneType&) { return val != 0; }\n"
              << "template<typename T, typename std::enable_if<std::is_arithmetic<T>::value, int>::type = 0>\n"
              << "inline bool operator!=(const ThavaNoneType&, T val) { return val != 0; }\n\n"
              << "inline bool operator==(const std::string& s, const ThavaNoneType&) { return s.empty(); }\n"
              << "inline bool operator==(const ThavaNoneType&, const std::string& s) { return s.empty(); }\n"
              << "inline bool operator!=(const std::string& s, const ThavaNoneType&) { return !s.empty(); }\n"
              << "inline bool operator!=(const ThavaNoneType&, const std::string& s) { return !s.empty(); }\n\n"
              << "inline bool operator==(const std::vector<ThavaValue>& v, const ThavaNoneType&) { return v.empty(); }\n"
              << "inline bool operator==(const ThavaNoneType&, const std::vector<ThavaValue>& v) { return v.empty(); }\n"
              << "inline bool operator!=(const std::vector<ThavaValue>& v, const ThavaNoneType&) { return !v.empty(); }\n"
              << "inline bool operator!=(const ThavaNoneType&, const std::vector<ThavaValue>& v) { return !v.empty(); }\n\n"
              << "inline bool operator==(const ThavaValue& a, const ThavaNoneType&) {\n"
              << "    return a.type == 0 || (a.type == 1 && a.iVal == 0) || (a.type == 4 && a.sVal.empty()) || (a.type == 5 && a.lVal.empty()) || (a.type == 6 && (!a.dVal || a.dVal->entries.empty()));\n"
              << "}\n"
              << "inline bool operator==(const ThavaNoneType&, const ThavaValue& a) { return a == _thava_none; }\n"
              << "inline bool operator!=(const ThavaValue& a, const ThavaNoneType&) { return !(a == _thava_none); }\n"
              << "inline bool operator!=(const ThavaNoneType&, const ThavaValue& a) { return !(a == _thava_none); }\n\n"
              << "inline std::ostream& operator<<(std::ostream& os, const ThavaNoneType&) { os << \"none\"; return os; }\n\n"
              << "inline ThavaValue _thava_add(const ThavaValue& a, const ThavaValue& b) {\n"
              << "    if (a.type == 4 || b.type == 4) return ThavaValue(_thava_to_string(a) + _thava_to_string(b));\n"
              << "    if (a.type == 2 || b.type == 2) return ThavaValue(_thava_to_double(a) + _thava_to_double(b));\n"
              << "    return ThavaValue(_thava_to_int(a) + _thava_to_int(b));\n"
              << "}\n"
              << "inline ThavaValue _thava_sub(const ThavaValue& a, const ThavaValue& b) {\n"
              << "    if (a.type == 2 || b.type == 2) return ThavaValue(_thava_to_double(a) - _thava_to_double(b));\n"
              << "    return ThavaValue(_thava_to_int(a) - _thava_to_int(b));\n"
              << "}\n"
              << "inline ThavaValue _thava_mul(const ThavaValue& a, const ThavaValue& b) {\n"
              << "    if (a.type == 2 || b.type == 2) return ThavaValue(_thava_to_double(a) * _thava_to_double(b));\n"
              << "    return ThavaValue(_thava_to_int(a) * _thava_to_int(b));\n"
              << "}\n"
              << "inline ThavaValue _thava_div(const ThavaValue& a, const ThavaValue& b) {\n"
              << "    if (a.type == 2 || b.type == 2) return ThavaValue(_thava_to_double(a) / _thava_to_double(b));\n"
              << "    long long bVal = _thava_to_int(b);\n"
              << "    if (bVal == 0) throw ThavaException(\"RuntimeError\", \"Division by zero!\");\n"
              << "    return ThavaValue(_thava_to_int(a) / bVal);\n"
              << "}\n"
              << "inline ThavaValue _thava_mod(const ThavaValue& a, const ThavaValue& b) {\n"
              << "    long long bVal = _thava_to_int(b);\n"
              << "    if (bVal == 0) throw ThavaException(\"RuntimeError\", \"Modulo by zero!\");\n"
              << "    return ThavaValue(_thava_to_int(a) % bVal);\n"
              << "}\n"
              << "inline ThavaValue _thava_pow(const ThavaValue& a, const ThavaValue& b) {\n"
              << "    if (a.type == 2 || b.type == 2) return ThavaValue(std::pow(_thava_to_double(a), _thava_to_double(b)));\n"
              << "    long long exp = _thava_to_int(b);\n"
              << "    if (exp < 0) return ThavaValue(std::pow(_thava_to_double(a), (double)exp));\n"
              << "    long long base = _thava_to_int(a), res = 1;\n"
              << "    while (exp > 0) {\n"
              << "        if (exp & 1) res *= base;\n"
              << "        base *= base;\n"
              << "        exp >>= 1;\n"
              << "    }\n"
              << "    return ThavaValue(res);\n"
              << "}\n\n"
              << "inline long long _thava_to_int(const ThavaValue& v) { if (v.type == 1) return v.iVal; if (v.type == 2) return (long long)v.fVal; if (v.type == 3) return v.bVal ? 1LL : 0LL; if (v.type == 4) return _thava_to_int(v.sVal); return 0LL; }\n\n"
              << "inline float _thava_to_float(const ThavaValue& v) { if (v.type == 1) return (float)v.iVal; if (v.type == 2) return (float)v.fVal; if (v.type == 3) return v.bVal ? 1.0f : 0.0f; if (v.type == 4) return _thava_to_float(v.sVal); return 0.0f; }\n\n"
              << "inline double _thava_to_double(const ThavaValue& v) { if (v.type == 1) return (double)v.iVal; if (v.type == 2) return v.fVal; if (v.type == 3) return v.bVal ? 1.0 : 0.0; if (v.type == 4) return _thava_to_double(v.sVal); return 0.0; }\n\n"
              << "inline bool _thava_to_bool(const ThavaValue& v) { if (v.type == 1) return v.iVal != 0; if (v.type == 2) return v.fVal != 0.0; if (v.type == 3) return v.bVal; if (v.type == 4) return _thava_to_bool(v.sVal); if (v.type == 5) return !v.lVal.empty(); if (v.type == 6) return v.dVal && !v.dVal->entries.empty(); return false; }\n\n"
              << "inline std::string _thava_to_string(const ThavaDict& d);\n"
              << "inline std::string _thava_to_string(const ThavaValue& v) { if (v.type == 1) return _thava_to_string(v.iVal); if (v.type == 2) return _thava_to_string(v.fVal); if (v.type == 3) return _thava_to_string(v.bVal); if (v.type == 4) return v.sVal; if (v.type == 6 && v.dVal) return _thava_to_string(*v.dVal); return \"\"; }\n\n"
              << "inline const std::vector<ThavaValue>& _thava_to_iterable(const std::vector<ThavaValue>& v) { return v; }\n"
              << "template <typename T>\n"
              << "inline T& _thava_tmp_ref(const T& v) {\n"
              << "    static thread_local std::vector<T> _thava_pool(8);\n"
              << "    static thread_local size_t _thava_pool_idx = 0;\n"
              << "    T& _thava_slot = _thava_pool[_thava_pool_idx++ % _thava_pool.size()];\n"
              << "    _thava_slot = v;\n"
              << "    return _thava_slot;\n"
              << "}\n\n"
              << "inline std::vector<std::string> _thava_to_iterable(const std::string& s) { std::vector<std::string> res; for (char c : s) res.push_back(std::string(1, c)); return res; }\n"
              << "inline std::vector<ThavaValue> _thava_to_iterable(const ThavaDict& d);\n"
              << "inline std::vector<ThavaValue> _thava_to_iterable(const ThavaValue& v) { if (v.type == 5) return v.lVal; if (v.type == 6 && v.dVal) return v.dVal->keys(); if (v.type == 4) { std::vector<ThavaValue> res; for (char c : v.sVal) res.push_back(ThavaValue(std::string(1, c))); return res; } return {}; }\n\n"
              << "std::ostream& operator<<(std::ostream& os, const ThavaValue& val);\n\n"
              << "std::ostream& operator<<(std::ostream& os, const std::vector<ThavaValue>& vec) {\n"
              << "    os << \"[\";\n"
              << "    for (size_t i = 0; i < vec.size(); ++i) {\n"
              << "        os << vec[i];\n"
              << "        if (i + 1 < vec.size()) os << \", \";\n"
              << "    }\n"
              << "    os << \"]\";\n"
              << "    return os;\n"
              << "}\n\n"
              << "std::ostream& operator<<(std::ostream& os, const ThavaValue& val) {\n"
              << "    if (val.type == 0) os << \"none\";\n"
              << "    else if (val.type == 1) os << val.iVal;\n"
              << "    else if (val.type == 2) os << val.fVal;\n"
              << "    else if (val.type == 3) os << (val.bVal ? \"true\" : \"false\");\n"
              << "    else if (val.type == 4) os << val.sVal;\n"
              << "    else if (val.type == 5) os << val.lVal;\n"
              << "    else if (val.type == 6 && val.dVal) os << *val.dVal;\n"
              << "    return os;\n"
              << "}\n\n"
              << "// === Thava Standard OS Library Runtime Helpers ===\n"
              << "inline std::string _thava_os_name() {\n"
              << "#ifdef _WIN32\n"
              << "    return \"windows\";\n"
              << "#else\n"
              << "    return \"linux\";\n"
              << "#endif\n"
              << "}\n\n"
              << "inline long long _thava_os_system(const std::string& cmd) {\n"
              << "    return (long long)std::system(cmd.c_str());\n"
              << "}\n\n"
              << "inline long long _thava_os_getpid() {\n"
              << "#ifdef _WIN32\n"
              << "    return (long long)GetCurrentProcessId();\n"
              << "#else\n"
              << "    return (long long)getpid();\n"
              << "#endif\n"
              << "}\n\n"
              << "inline long long _thava_os_cpu_count() {\n"
              << "    unsigned int n = std::thread::hardware_concurrency();\n"
              << "    return n == 0 ? 4LL : (long long)n;\n"
              << "}\n\n"
              << "inline std::string _thava_os_getcwd() {\n"
              << "    try { return std::filesystem::current_path().string(); }\n"
              << "    catch (...) { return \"\"; }\n"
              << "}\n\n"
              << "inline bool _thava_os_chdir(const std::string& p) {\n"
              << "    try { std::filesystem::current_path(p); return true; }\n"
              << "    catch (...) { return false; }\n"
              << "}\n\n"
              << "inline bool _thava_os_mkdir(const std::string& p) {\n"
              << "    try { return std::filesystem::create_directory(p); }\n"
              << "    catch (...) { return false; }\n"
              << "}\n\n"
              << "inline bool _thava_os_rmdir(const std::string& p) {\n"
              << "    try { return std::filesystem::remove(p); }\n"
              << "    catch (...) { return false; }\n"
              << "}\n\n"
              << "inline bool _thava_os_remove(const std::string& p) {\n"
              << "    try { return std::filesystem::remove(p); }\n"
              << "    catch (...) { return false; }\n"
              << "}\n\n"
              << "inline bool _thava_os_rename(const std::string& o, const std::string& n) {\n"
              << "    try { std::filesystem::rename(o, n); return true; }\n"
              << "    catch (...) { return false; }\n"
              << "}\n\n"
              << "inline bool _thava_os_exists(const std::string& p) {\n"
              << "    try { return std::filesystem::exists(p); }\n"
              << "    catch (...) { return false; }\n"
              << "}\n\n"
              << "inline bool _thava_os_is_file(const std::string& p) {\n"
              << "    try { return std::filesystem::is_regular_file(p); }\n"
              << "    catch (...) { return false; }\n"
              << "}\n\n"
              << "inline bool _thava_os_is_dir(const std::string& p) {\n"
              << "    try { return std::filesystem::is_directory(p); }\n"
              << "    catch (...) { return false; }\n"
              << "}\n\n"
              << "inline long long _thava_os_getsize(const std::string& p) {\n"
              << "    try { return (long long)std::filesystem::file_size(p); }\n"
              << "    catch (...) { return -1LL; }\n"
              << "}\n\n"
              << "inline std::vector<ThavaValue> _thava_os_listdir(const std::string& p) {\n"
              << "    std::vector<ThavaValue> res;\n"
              << "    try {\n"
              << "        for (const auto& entry : std::filesystem::directory_iterator(p)) {\n"
              << "            res.push_back(ThavaValue(entry.path().filename().string()));\n"
              << "        }\n"
              << "    } catch (...) {}\n"
              << "    return res;\n"
              << "}\n\n"
              << "inline std::string _thava_os_getenv(const std::string& k) {\n"
              << "    const char* v = std::getenv(k.c_str());\n"
              << "    return v ? std::string(v) : \"\";\n"
              << "}\n\n"
              << "inline bool _thava_os_setenv(const std::string& k, const std::string& v) {\n"
              << "#ifdef _WIN32\n"
              << "    return _putenv_s(k.c_str(), v.c_str()) == 0;\n"
              << "#else\n"
              << "    return setenv(k.c_str(), v.c_str(), 1) == 0;\n"
              << "#endif\n"
              << "}\n\n"
              << "inline void _thava_os_sleep(long long ms) {\n"
              << "    std::this_thread::sleep_for(std::chrono::milliseconds(ms));\n"
              << "}\n\n"
              << "template<typename T1, typename T2>\n"
              << "inline long long _thava_os_showmessagebox(const T1& icon, const std::string& title, const std::string& content, const T2& buttons) {\n"
              << "    int iconFlag = 0;\n"
              << "    if constexpr (std::is_integral_v<std::decay_t<T1>>) {\n"
              << "        iconFlag = (int)icon;\n"
              << "    } else {\n"
              << "        std::string s = icon;\n"
              << "        for (char& c : s) c = (char)tolower((unsigned char)c);\n"
              << "        if (s == \"warning\") iconFlag = 0x30;\n"
              << "        else if (s == \"information\" || s == \"infomation\" || s == \"info\") iconFlag = 0x40;\n"
              << "        else if (s == \"question\") iconFlag = 0x20;\n"
              << "        else if (s == \"error\") iconFlag = 0x10;\n"
              << "        else iconFlag = 0;\n"
              << "    }\n"
              << "    int btnFlag = 0;\n"
              << "    if constexpr (std::is_integral_v<std::decay_t<T2>>) {\n"
              << "        btnFlag = (int)buttons;\n"
              << "    } else {\n"
              << "        std::string s = buttons;\n"
              << "        for (char& c : s) c = (char)tolower((unsigned char)c);\n"
              << "        if (s == \"ok\") btnFlag = 0;\n"
              << "        else if (s == \"ok/cancel\" || s == \"okcancel\") btnFlag = 1;\n"
              << "        else if (s == \"yes/no/cancel\" || s == \"yesnocancel\") btnFlag = 3;\n"
              << "        else if (s == \"yes/no\" || s == \"yesno\") btnFlag = 4;\n"
              << "        else if (s == \"retry/cancel\" || s == \"retrycancel\") btnFlag = 5;\n"
              << "        else btnFlag = 0;\n"
              << "    }\n"
              << "#ifdef _WIN32\n"
              << "    return (long long)MessageBoxA(NULL, content.c_str(), title.c_str(), (UINT)(iconFlag | btnFlag | 0x40000 /* MB_TOPMOST */));\n"
              << "#else\n"
              << "    if (std::system(\"which zenity >/dev/null 2>&1\") == 0 && (std::getenv(\"DISPLAY\") || std::getenv(\"WAYLAND_DISPLAY\"))) {\n"
              << "        std::string zenType = \"--info\";\n"
              << "        if (iconFlag == 0x30) zenType = \"--warning\";\n"
              << "        else if (iconFlag == 0x10) zenType = \"--error\";\n"
              << "        else if (iconFlag == 0x20 || btnFlag == 4 || btnFlag == 3) zenType = \"--question\";\n"
              << "        std::string cmd = \"zenity \" + zenType + \" --title=\\\"\" + title + \"\\\" --text=\\\"\" + content + \"\\\"\";\n"
              << "        if (btnFlag == 4) {\n"
              << "            cmd += \" --ok-label=\\\"Yes\\\" --cancel-label=\\\"No\\\"\";\n"
              << "            int ret = std::system(cmd.c_str());\n"
              << "            return (WEXITSTATUS(ret) == 0) ? 6LL : 7LL;\n"
              << "        } else if (btnFlag == 1) {\n"
              << "            int ret = std::system(cmd.c_str());\n"
              << "            return (WEXITSTATUS(ret) == 0) ? 1LL : 2LL;\n"
              << "        } else {\n"
              << "            std::system(cmd.c_str());\n"
              << "            return 1LL;\n"
              << "        }\n"
              << "    }\n"
              << "    std::cout << \"\\n[\" << title << \"] \" << content << \"\\n\";\n"
              << "    if (btnFlag == 4) {\n"
              << "        std::cout << \"[Yes/No] (y/n): \";\n"
              << "        std::string ans; std::cin >> ans;\n"
              << "        if (!ans.empty() && (ans[0] == 'y' || ans[0] == 'Y')) return 6LL;\n"
              << "        return 7LL;\n"
              << "    }\n"
              << "    std::cout << \"[Press Enter to continue...]\";\n"
              << "    std::string dummy; std::getline(std::cin, dummy);\n"
              << "    return 1LL;\n"
              << "#endif\n"
              << "}\n\n"
              << "inline std::tm _thava_get_localtime() {\n"
              << "    std::time_t t = std::time(nullptr);\n"
              << "    std::tm tm_buf;\n"
              << "#if defined(_WIN32) || defined(_WIN64)\n"
              << "    localtime_s(&tm_buf, &t);\n"
              << "#else\n"
              << "    localtime_r(&t, &tm_buf);\n"
              << "#endif\n"
              << "    return tm_buf;\n"
              << "}\n\n"
              << "inline long long _thava_time_timestamp() {\n"
              << "    return (long long)std::time(nullptr);\n"
              << "}\n\n"
              << "inline long long _thava_time_millis() {\n"
              << "    auto now = std::chrono::system_clock::now();\n"
              << "    return std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();\n"
              << "}\n\n"
              << "inline long long _thava_time_year() {\n"
              << "    auto tm = _thava_get_localtime();\n"
              << "    return tm.tm_year + 1900;\n"
              << "}\n\n"
              << "inline long long _thava_time_month() {\n"
              << "    auto tm = _thava_get_localtime();\n"
              << "    return tm.tm_mon + 1;\n"
              << "}\n\n"
              << "inline long long _thava_time_day() {\n"
              << "    auto tm = _thava_get_localtime();\n"
              << "    return tm.tm_mday;\n"
              << "}\n\n"
              << "inline long long _thava_time_hour() {\n"
              << "    auto tm = _thava_get_localtime();\n"
              << "    return tm.tm_hour;\n"
              << "}\n\n"
              << "inline long long _thava_time_minute() {\n"
              << "    auto tm = _thava_get_localtime();\n"
              << "    return tm.tm_min;\n"
              << "}\n\n"
              << "inline long long _thava_time_second() {\n"
              << "    auto tm = _thava_get_localtime();\n"
              << "    return tm.tm_sec;\n"
              << "}\n\n"
              << "inline void _thava_time_sleep(long long ms) {\n"
              << "    std::this_thread::sleep_for(std::chrono::milliseconds(ms));\n"
              << "}\n\n"
              << "inline std::string _thava_time_now(std::string fmt = \"\") {\n"
              << "    auto tm = _thava_get_localtime();\n"
              << "    char buf[128];\n"
              << "    if (fmt.empty() || fmt == \"datetime\") {\n"
              << "        std::snprintf(buf, sizeof(buf), \"%04d-%02d-%02d %02d:%02d:%02d\",\n"
              << "                      tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,\n"
              << "                      tm.tm_hour, tm.tm_min, tm.tm_sec);\n"
              << "        return std::string(buf);\n"
              << "    }\n"
              << "    if (fmt == \"date\") {\n"
              << "        std::snprintf(buf, sizeof(buf), \"%04d-%02d-%02d\",\n"
              << "                      tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);\n"
              << "        return std::string(buf);\n"
              << "    }\n"
              << "    if (fmt == \"time\") {\n"
              << "        std::snprintf(buf, sizeof(buf), \"%02d:%02d:%02d\",\n"
              << "                      tm.tm_hour, tm.tm_min, tm.tm_sec);\n"
              << "        return std::string(buf);\n"
              << "    }\n"
              << "    std::string f = fmt;\n"
              << "    auto replaceAll = [](std::string& str, const std::string& from, const std::string& to) {\n"
              << "        size_t start_pos = 0;\n"
              << "        while((start_pos = str.find(from, start_pos)) != std::string::npos) {\n"
              << "            str.replace(start_pos, from.length(), to);\n"
              << "            start_pos += to.length();\n"
              << "        }\n"
              << "    };\n"
              << "    char yBuf[8], mBuf[4], dBuf[4], hBuf[4], minBuf[4], sBuf[4];\n"
              << "    std::snprintf(yBuf, sizeof(yBuf), \"%04d\", tm.tm_year + 1900);\n"
              << "    std::snprintf(mBuf, sizeof(mBuf), \"%02d\", tm.tm_mon + 1);\n"
              << "    std::snprintf(dBuf, sizeof(dBuf), \"%02d\", tm.tm_mday);\n"
              << "    std::snprintf(hBuf, sizeof(hBuf), \"%02d\", tm.tm_hour);\n"
              << "    std::snprintf(minBuf, sizeof(minBuf), \"%02d\", tm.tm_min);\n"
              << "    std::snprintf(sBuf, sizeof(sBuf), \"%02d\", tm.tm_sec);\n"
              << "    replaceAll(f, \"%Y\", yBuf); replaceAll(f, \"yyyy\", yBuf); replaceAll(f, \"YYYY\", yBuf);\n"
              << "    std::string colonMin = \":\" + std::string(minBuf);\n"
              << "    std::string minColon = std::string(minBuf) + \":\";\n"
              << "    replaceAll(f, \":mm\", colonMin); replaceAll(f, \":MM\", colonMin);\n"
              << "    replaceAll(f, \"mm:\", minColon); replaceAll(f, \"MM:\", minColon);\n"
              << "    replaceAll(f, \"%m\", mBuf); replaceAll(f, \"mm\", mBuf); replaceAll(f, \"MM\", mBuf);\n"
              << "    replaceAll(f, \"%d\", dBuf); replaceAll(f, \"dd\", dBuf); replaceAll(f, \"DD\", dBuf);\n"
              << "    replaceAll(f, \"%H\", hBuf); replaceAll(f, \"hh\", hBuf); replaceAll(f, \"HH\", hBuf);\n"
              << "    replaceAll(f, \"%M\", minBuf);\n"
              << "    replaceAll(f, \"%S\", sBuf); replaceAll(f, \"ss\", sBuf); replaceAll(f, \"SS\", sBuf);\n"
              << "    return f;\n"
              << "}\n\n"
              << "inline std::string _thava_time_date(std::string fmt = \"\") {\n"
              << "    if (fmt.empty()) fmt = \"date\";\n"
              << "    return _thava_time_now(fmt);\n"
              << "}\n\n"
              << "// --- Standard clib library (Foreign Function Interface / Dynamic Library Interop) ---\n"
              << "inline uint64_t _thava_clib_load(std::string path) {\n"
              << "    if (path.empty()) return 0;\n"
              << "#ifdef _WIN32\n"
              << "    if (path.find('.') == std::string::npos) path += \".dll\";\n"
              << "    HMODULE h = LoadLibraryA(path.c_str());\n"
              << "    return (uint64_t)(uintptr_t)h;\n"
              << "#else\n"
              << "    void* h = nullptr;\n"
              << "    if (path.find('.') == std::string::npos) {\n"
              << "        std::vector<std::string> cands = { \"lib\" + path + \".so\", \"lib\" + path + \".so.6\", path + \".so\", path + \".so.6\" };\n"
              << "        for (const auto& c : cands) {\n"
              << "            h = dlopen(c.c_str(), RTLD_LAZY | RTLD_GLOBAL);\n"
              << "            if (h) break;\n"
              << "        }\n"
              << "    } else {\n"
              << "        h = dlopen(path.c_str(), RTLD_LAZY | RTLD_GLOBAL);\n"
              << "    }\n"
              << "    return (uint64_t)(uintptr_t)h;\n"
              << "#endif\n"
              << "}\n\n"
              << "template<typename H>\n"
              << "inline void _thava_clib_free(H handle) {\n"
              << "    if (!handle) return;\n"
              << "#ifdef _WIN32\n"
              << "    FreeLibrary((HMODULE)(uintptr_t)handle);\n"
              << "#else\n"
              << "    dlclose((void*)(uintptr_t)handle);\n"
              << "#endif\n"
              << "}\n\n"
              << "template<typename H>\n"
              << "inline void* _thava_clib_get_symbol(H handle, const std::string& name) {\n"
              << "    if (!handle) return nullptr;\n"
              << "#ifdef _WIN32\n"
              << "    return (void*)GetProcAddress((HMODULE)(uintptr_t)handle, name.c_str());\n"
              << "#else\n"
              << "    return dlsym((void*)(uintptr_t)handle, name.c_str());\n"
              << "#endif\n"
              << "}\n\n"
              << "template<typename H>\n"
              << "inline bool _thava_clib_has(H handle, const std::string& name) {\n"
              << "    return _thava_clib_get_symbol(handle, name) != nullptr;\n"
              << "}\n\n"
              << "inline std::map<std::string, uint64_t>& _thava_clib_cache() {\n"
              << "    static std::map<std::string, uint64_t> cache;\n"
              << "    return cache;\n"
              << "}\n\n"
              << "inline bool _thava_clib_has_direct(const std::string& libName, const std::string& name) {\n"
              << "    auto& cache = _thava_clib_cache();\n"
              << "    uint64_t h = cache[libName];\n"
              << "    if (!h) {\n"
              << "        h = _thava_clib_load(libName);\n"
              << "        if (h) cache[libName] = h;\n"
              << "    }\n"
              << "    return _thava_clib_has(h, name);\n"
              << "}\n\n"
              << "template<typename H, typename... Args>\n"
              << "inline uint64_t _thava_clib_invoke(H handle, const std::string& funcName, Args... args) {\n"
              << "    void* proc = _thava_clib_get_symbol(handle, funcName);\n"
              << "    if (!proc) {\n"
              << "        std::cerr << \"[clib Error]: Function not found '\" << funcName << \"' in library!\\n\";\n"
              << "        return 0;\n"
              << "    }\n"
              << "    auto fn = reinterpret_cast<uint64_t(*)(Args...)>(proc);\n"
              << "    return fn(args...);\n"
              << "}\n\n"
              << "template<typename H, typename... Args>\n"
              << "inline std::string _thava_clib_invoke_str(H handle, const std::string& funcName, Args... args) {\n"
              << "    void* proc = _thava_clib_get_symbol(handle, funcName);\n"
              << "    if (!proc) {\n"
              << "        std::cerr << \"[clib Error]: Function not found '\" << funcName << \"' in library!\\n\";\n"
              << "        return \"\";\n"
              << "    }\n"
              << "    auto fn = reinterpret_cast<const char*(*)(Args...)>(proc);\n"
              << "    const char* s = fn(args...);\n"
              << "    return s ? std::string(s) : \"\";\n"
              << "}\n\n"
              << "template<typename H, typename... Args>\n"
              << "inline double _thava_clib_invoke_dbl(H handle, const std::string& funcName, Args... args) {\n"
              << "    void* proc = _thava_clib_get_symbol(handle, funcName);\n"
              << "    if (!proc) {\n"
              << "        std::cerr << \"[clib Error]: Function not found '\" << funcName << \"' in library!\\n\";\n"
              << "        return 0.0;\n"
              << "    }\n"
              << "    auto fn = reinterpret_cast<double(*)(Args...)>(proc);\n"
              << "    return fn(args...);\n"
              << "}\n\n"
              << "template<typename... Args>\n"
              << "inline uint64_t _thava_clib_call_direct(const std::string& libName, const std::string& funcName, Args... args) {\n"
              << "    auto& cache = _thava_clib_cache();\n"
              << "    uint64_t h = cache[libName];\n"
              << "    if (!h) {\n"
              << "        h = _thava_clib_load(libName);\n"
              << "        if (!h) {\n"
              << "            std::cerr << \"[clib Error]: Cannot load library '\" << libName << \"'!\\n\";\n"
              << "            return 0;\n"
              << "        }\n"
              << "        cache[libName] = h;\n"
              << "    }\n"
              << "    return _thava_clib_invoke(h, funcName, args...);\n"
              << "}\n\n"
              << "template<typename... Args>\n"
              << "inline std::string _thava_clib_call_direct_str(const std::string& libName, const std::string& funcName, Args... args) {\n"
              << "    auto& cache = _thava_clib_cache();\n"
              << "    uint64_t h = cache[libName];\n"
              << "    if (!h) {\n"
              << "        h = _thava_clib_load(libName);\n"
              << "        if (!h) {\n"
              << "            std::cerr << \"[clib Error]: Cannot load library '\" << libName << \"'!\\n\";\n"
              << "            return \"\";\n"
              << "        }\n"
              << "        cache[libName] = h;\n"
              << "    }\n"
              << "    return _thava_clib_invoke_str(h, funcName, args...);\n"
              << "}\n\n"
              << "template<typename... Args>\n"
              << "inline double _thava_clib_call_direct_dbl(const std::string& libName, const std::string& funcName, Args... args) {\n"
              << "    auto& cache = _thava_clib_cache();\n"
              << "    uint64_t h = cache[libName];\n"
              << "    if (!h) {\n"
              << "        h = _thava_clib_load(libName);\n"
              << "        if (!h) {\n"
              << "            std::cerr << \"[clib Error]: Cannot load library '\" << libName << \"'!\\n\";\n"
              << "            return 0.0;\n"
              << "        }\n"
              << "        cache[libName] = h;\n"
              << "    }\n"
              << "    return _thava_clib_invoke_dbl(h, funcName, args...);\n"
              << "}\n\n"
              << "inline uint64_t _thava_clib_alloc(size_t size) {\n"
              << "    return (uint64_t)(uintptr_t)std::malloc(size);\n"
              << "}\n\n"
              << "inline void _thava_clib_free_mem(uint64_t ptr) {\n"
              << "    if (ptr) std::free((void*)(uintptr_t)ptr);\n"
              << "}\n\n"
              << "inline int64_t _thava_clib_peek32(uint64_t ptr, int64_t offset = 0) {\n"
              << "    if (!ptr) return 0;\n"
              << "    return (int64_t)*(int32_t*)((char*)(uintptr_t)ptr + offset);\n"
              << "}\n\n"
              << "inline int64_t _thava_clib_peek_u8(uint64_t ptr, int64_t offset = 0) {\n"
              << "    if (!ptr) return 0;\n"
              << "    return (int64_t)*(uint8_t*)((char*)(uintptr_t)ptr + offset);\n"
              << "}\n\n"
              << "inline int64_t _thava_clib_peek64(uint64_t ptr, int64_t offset = 0) {\n"
              << "    if (!ptr) return 0;\n"
              << "    return *(int64_t*)((char*)(uintptr_t)ptr + offset);\n"
              << "}\n\n"
              << "inline double _thava_clib_peek_f(uint64_t ptr, int64_t offset = 0) {\n"
              << "    if (!ptr) return 0.0;\n"
              << "    return (double)*(float*)((char*)(uintptr_t)ptr + offset);\n"
              << "}\n\n"
              << "inline int64_t _thava_clib_poke32(uint64_t ptr, int64_t offset, int64_t val) {\n"
              << "    if (ptr) *(int32_t*)((char*)(uintptr_t)ptr + offset) = (int32_t)val;\n"
              << "    return 0;\n"
              << "}\n\n"
              << "inline int64_t _thava_clib_poke64(uint64_t ptr, int64_t offset, int64_t val) {\n"
              << "    if (ptr) *(int64_t*)((char*)(uintptr_t)ptr + offset) = val;\n"
              << "    return 0;\n"
              << "}\n\n"
              << "inline int64_t _thava_clib_poke_f(uint64_t ptr, int64_t offset, double val) {\n"
              << "    if (ptr) *(float*)((char*)(uintptr_t)ptr + offset) = (float)val;\n"
              << "    return 0;\n"
              << "}\n\n"
              // Native C++ random helper functions
              << "inline std::mt19937& _thava_get_rng() {\n"
              << "    static std::random_device rd;\n"
              << "    static std::mt19937 rng(rd());\n"
              << "    return rng;\n"
              << "}\n\n"
              << "inline double _thava_rand_double(double min, double max) {\n"
              << "    std::uniform_real_distribution<double> dist(min, max);\n"
              << "    return dist(_thava_get_rng());\n"
              << "}\n\n"
              << "inline long long _thava_rand_int(long long min, long long max) {\n"
              << "    std::uniform_int_distribution<long long> dist(min, max);\n"
              << "    return dist(_thava_get_rng());\n"
              << "}\n\n"
              << "inline ThavaValue _thava_rand_choice(const std::vector<ThavaValue>& vec) {\n"
              << "    if (vec.empty()) return ThavaValue();\n"
              << "    std::uniform_int_distribution<size_t> dist(0, vec.size() - 1);\n"
              << "    return vec[dist(_thava_get_rng())];\n"
              << "}\n\n"
              // Select random element with weights: higher weight = higher probability.
              // Accepts weights of type <int> or <fl>/<dbl>; negative weights treated as 0.
              << "inline double _thava_value_as_double(const ThavaValue& v) {\n"
              << "    if (v.type == 1) return (double)v.iVal;\n"
              << "    if (v.type == 2) return v.fVal;\n"
              << "    return 0.0;\n"
              << "}\n\n"
              << "inline ThavaValue _thava_rand_choice_weighted(const std::vector<ThavaValue>& vec, const std::vector<ThavaValue>& weights) {\n"
              << "    if (vec.empty()) return ThavaValue();\n"
              << "    if (weights.size() != vec.size()) {\n"
              << "        std::cerr << \"Runtime Error: randres() - weight count must match list size!\\n\"; exit(1);\n"
              << "    }\n"
              << "    std::vector<double> w; double total = 0.0;\n"
              << "    for (const auto& x : weights) { double d = _thava_value_as_double(x); if (d < 0) d = 0; w.push_back(d); total += d; }\n"
              << "    if (total <= 0.0) {\n"
              << "        std::uniform_int_distribution<size_t> dist(0, vec.size() - 1);\n"
              << "        return vec[dist(_thava_get_rng())];\n"
              << "    }\n"
              << "    std::uniform_real_distribution<double> dist(0.0, total);\n"
              << "    double r = dist(_thava_get_rng());\n"
              << "    double cum = 0.0;\n"
              << "    for (size_t i = 0; i < vec.size(); ++i) {\n"
              << "        cum += w[i];\n"
              << "        if (r <= cum) return vec[i];\n"
              << "    }\n"
              << "    return vec.back();\n"
              << "}\n\n"
              // ===== Full Unicode (UTF-8) handling: decode -> code-point case mapping -> encode =====
              // Decode one UTF-8 code point from position i, return code point and advance i
              << "inline uint32_t _thava_utf8_decode(const std::string& s, size_t& i) {\n"
              << "    unsigned char c = (unsigned char)s[i];\n"
              << "    uint32_t cp = 0; int extra = 0;\n"
              << "    if ((c & 0x80) == 0) { cp = c; extra = 0; }\n"
              << "    else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; extra = 1; }\n"
              << "    else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; extra = 2; }\n"
              << "    else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; extra = 3; }\n"
              << "    else { cp = c; extra = 0; }\n"
              << "    i++;\n"
              << "    for (int k = 0; k < extra && i < s.size(); ++k) {\n"
              << "        unsigned char cc = (unsigned char)s[i];\n"
              << "        if ((cc & 0xC0) != 0x80) break;\n"
              << "        cp = (cp << 6) | (cc & 0x3F);\n"
              << "        i++;\n"
              << "    }\n"
              << "    return cp;\n"
              << "}\n\n";
    cppOutput << R"(
inline std::string _thava_read_console_line() {
#if defined(_WIN32)
    HANDLE console = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode;
    if (GetConsoleMode(console, &mode)) {
        wchar_t buffer[1024];
        std::wstring line;
        DWORD length = 0;
        while (ReadConsoleW(console, buffer, 1024, &length, nullptr) && length != 0) {
            line.append(buffer, length);
            if (line.back() == L'\n' || (mode & ENABLE_LINE_INPUT) == 0) break;
        }
        while (!line.empty() && (line.back() == L'\r' || line.back() == L'\n')) line.pop_back();
        if (line.empty()) return {};
        int size = WideCharToMultiByte(CP_UTF8, 0, line.data(), static_cast<int>(line.size()),
                                       nullptr, 0, nullptr, nullptr);
        if (size <= 0) return {};
        std::string utf8(size, '\0');
        WideCharToMultiByte(CP_UTF8, 0, line.data(), static_cast<int>(line.size()),
                            utf8.data(), size, nullptr, nullptr);
        return utf8;
    }
#endif
    std::string line;
    if (!std::getline(std::cin, line)) {
        std::cin.clear();
        return {};
    }
    if (!line.empty() && line.back() == '\r') line.pop_back();
    return line;
}

)";
    thava_unicode::emitCppCaseConversion(cppOutput);
    cppOutput
              << "inline std::string _thava_str_strip(std::string s) {\n"
              << "    size_t startPos = s.find_first_not_of(\" \\t\\n\\r\\f\\v\");\n"
              << "    if (startPos == std::string::npos) return \"\";\n"
              << "    size_t endPos = s.find_last_not_of(\" \\t\\n\\r\\f\\v\");\n"
              << "    return s.substr(startPos, endPos - startPos + 1);\n"
              << "}\n\n"
              // Split string by delimiter, extract index [a] or slice [a..b] (ASCII delimiter is UTF-8 safe)
              << "inline std::string _thava_str_split(const std::string& s, const std::string& delim, long long a, long long b) {\n"
              << "    std::vector<std::string> parts;\n"
              << "    if (delim.empty()) { parts.push_back(s); }\n"
              << "    else {\n"
              << "        size_t pos = 0, found;\n"
              << "        while ((found = s.find(delim, pos)) != std::string::npos) {\n"
              << "            parts.push_back(s.substr(pos, found - pos));\n"
              << "            pos = found + delim.size();\n"
              << "        }\n"
              << "        parts.push_back(s.substr(pos));\n"
              << "    }\n"
              << "    long long n = (long long)parts.size();\n"
              << "    long long ai = a, bi = b;\n"
              << "    if (ai < 0) ai += n; if (bi < 0) bi += n;\n"
              << "    if (n == 0) return \"\";\n"
              << "    if (ai < 0) ai = 0; if (ai >= n) ai = n - 1;\n"
              << "    if (bi < 0) bi = 0; if (bi >= n) bi = n - 1;\n"
              << "    if (ai > bi) std::swap(ai, bi);\n"
              << "    std::string result;\n"
              << "    for (long long k = ai; k <= bi; ++k) { result += parts[k]; if (k < bi) result += delim; }\n"
              << "    return result;\n"
              << "}\n\n"
              << "inline std::string _thava_str_replace(const std::string& s, const std::string& oldSub, const std::string& newSub) {\n"
              << "    if (oldSub.empty()) return s;\n"
              << "    std::string res;\n"
              << "    size_t pos = 0, found;\n"
              << "    while ((found = s.find(oldSub, pos)) != std::string::npos) {\n"
              << "        res.append(s, pos, found - pos);\n"
              << "        res.append(newSub);\n"
              << "        pos = found + oldSub.size();\n"
              << "    }\n"
              << "    res.append(s, pos, std::string::npos);\n"
              << "    return res;\n"
              << "}\n\n"
              << "inline long long _thava_str_find(const std::string& s, const std::string& sub) {\n"
              << "    size_t pos = s.find(sub);\n"
              << "    return (pos == std::string::npos) ? -1LL : (long long)pos;\n"
              << "}\n\n"
              << "inline bool _thava_str_startswith(const std::string& s, const std::string& prefix) {\n"
              << "    if (prefix.size() > s.size()) return false;\n"
              << "    return s.compare(0, prefix.size(), prefix) == 0;\n"
              << "}\n\n"
              << "inline bool _thava_str_endswith(const std::string& s, const std::string& suffix) {\n"
              << "    if (suffix.size() > s.size()) return false;\n"
              << "    return s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;\n"
              << "}\n\n"
              << "inline long long _thava_str_count(const std::string& s, const std::string& sub) {\n"
              << "    if (sub.empty()) return 0LL;\n"
              << "    long long cnt = 0;\n"
              << "    size_t pos = 0;\n"
              << "    while ((pos = s.find(sub, pos)) != std::string::npos) {\n"
              << "        ++cnt;\n"
              << "        pos += sub.size();\n"
              << "    }\n"
              << "    return cnt;\n"
              << "}\n\n"
              << "inline std::string _thava_str_join(const std::string& delim, const std::vector<ThavaValue>& list) {\n"
              << "    std::string res;\n"
              << "    for (size_t i = 0; i < list.size(); ++i) {\n"
              << "        res += _thava_to_string(list[i]);\n"
              << "        if (i + 1 < list.size()) res += delim;\n"
              << "    }\n"
              << "    return res;\n"
              << "}\n\n"
              << "inline std::string _thava_str_join(const std::string& delim, const std::string& s) { return s; }\n\n"
              << "inline std::vector<ThavaValue> _thava_str_split_all(const std::string& s, const std::string& delim) {\n"
              << "    std::vector<ThavaValue> parts;\n"
              << "    if (delim.empty()) {\n"
              << "        parts.push_back(ThavaValue(s));\n"
              << "        return parts;\n"
              << "    }\n"
              << "    size_t pos = 0, found;\n"
              << "    while ((found = s.find(delim, pos)) != std::string::npos) {\n"
              << "        parts.push_back(ThavaValue(s.substr(pos, found - pos)));\n"
              << "        pos = found + delim.size();\n"
              << "    }\n"
              << "    parts.push_back(ThavaValue(s.substr(pos)));\n"
              << "    return parts;\n"
              << "}\n\n"
              // Substring by character count (1-based, inclusive) - UTF-8 safe
              << "inline std::string _thava_str_getsubstring(const std::string& s, long long start, long long end) {\n"
              << "    std::vector<std::string> chars;\n"
              << "    size_t i = 0;\n"
              << "    while (i < s.size()) {\n"
              << "        size_t startI = i;\n"
              << "        _thava_utf8_decode(s, i);\n"
              << "        chars.push_back(s.substr(startI, i - startI));\n"
              << "    }\n"
              << "    long long n = (long long)chars.size();\n"
              << "    long long a = start, b = end;\n"
              << "    if (a < 1) a = 1; if (b > n) b = n;\n"
              << "    if (a > b || n == 0) return \"\";\n"
              << "    std::string result;\n"
              << "    for (long long k = a; k <= b; ++k) result += chars[k - 1];\n"
              << "    return result;\n"
              << "}\n\n"
              // List/tuple index access, supports negative index (a[-1] = last element)
              << "inline ThavaValue _thava_index(const std::vector<ThavaValue>& vec, long long idx) {\n"
              << "    long long i = idx;\n"
              << "    if (i < 0) i += (long long)vec.size();\n"
              << "    if (i < 0 || i >= (long long)vec.size()) {\n"
              << "        throw ThavaException(\"RuntimeError\", \"Index out of range for list/tuple!\");\n"
              << "    }\n"
              << "    return vec[(size_t)i];\n"
              << "}\n\n"
              << "inline std::string _thava_index(const std::string& s, long long idx) {\n"
              << "    long long n = (long long)s.size();\n"
              << "    if (n == 0) return \"\";\n"
              << "    long long i = idx;\n"
              << "    if (i < 0) i += n;\n"
              << "    return _thava_str_getsubstring(s, i + 1, i + 1);\n"
              << "}\n\n"
              // Compare ThavaValue values (used for removeval by value and 'in' / '==' operators)
              << "inline bool _thava_value_equals(const ThavaValue& a, const ThavaValue& b) {\n"
              << "    if (a.type != b.type) return false;\n"
              << "    switch (a.type) {\n"
              << "        case 1: return a.iVal == b.iVal;\n"
              << "        case 2: return a.fVal == b.fVal;\n"
              << "        case 3: return a.bVal == b.bVal;\n"
              << "        case 4: return a.sVal == b.sVal;\n"
              << "        case 5: {\n"
              << "            if (a.lVal.size() != b.lVal.size()) return false;\n"
              << "            for (size_t i = 0; i < a.lVal.size(); ++i) {\n"
              << "                if (!_thava_value_equals(a.lVal[i], b.lVal[i])) return false;\n"
              << "            }\n"
              << "            return true;\n"
              << "        }\n"
              << "        case 6: {\n"
              << "            if (!a.dVal && !b.dVal) return true;\n"
              << "            if (!a.dVal || !b.dVal) return false;\n"
              << "            if (a.dVal->entries.size() != b.dVal->entries.size()) return false;\n"
              << "            for (const auto& kv : a.dVal->entries) {\n"
              << "                if (!b.dVal->has(kv.first)) return false;\n"
              << "                if (!_thava_value_equals(b.dVal->get(kv.first), kv.second)) return false;\n"
              << "            }\n"
              << "            return true;\n"
              << "        }\n"
              << "        default: return false;\n"
              << "    }\n"
              << "}\n\n"
              << "inline bool operator==(const ThavaDict& a, const ThavaDict& b) {\n"
              << "    if (a.entries.size() != b.entries.size()) return false;\n"
              << "    for (const auto& kv : a.entries) {\n"
              << "        if (!b.has(kv.first)) return false;\n"
              << "        if (!_thava_value_equals(b.get(kv.first), kv.second)) return false;\n"
              << "    }\n"
              << "    return true;\n"
              << "}\n"
              << "inline bool operator!=(const ThavaDict& a, const ThavaDict& b) { return !(a == b); }\n\n"
              << "inline bool ThavaDict::has(const ThavaValue& key) const {\n"
              << "    for (const auto& kv : entries) if (_thava_value_equals(kv.first, key)) return true;\n"
              << "    return false;\n"
              << "}\n\n"
              << "inline ThavaValue ThavaDict::get(const ThavaValue& key, const ThavaValue& defaultVal) const {\n"
              << "    for (const auto& kv : entries) if (_thava_value_equals(kv.first, key)) return kv.second;\n"
              << "    return defaultVal;\n"
              << "}\n\n"
              << "inline ThavaValue ThavaDict::get_or_throw(const ThavaValue& key) const {\n"
              << "    for (const auto& kv : entries) if (_thava_value_equals(kv.first, key)) return kv.second;\n"
              << "    throw ThavaException(\"KeyError\", \"Key not found in dictionary: \" + _thava_to_string(key));\n"
              << "}\n\n"
              << "inline void ThavaDict::set(const ThavaValue& key, const ThavaValue& val) {\n"
              << "    for (auto& kv : entries) {\n"
              << "        if (_thava_value_equals(kv.first, key)) { kv.second = val; return; }\n"
              << "    }\n"
              << "    entries.push_back({key, val});\n"
              << "}\n\n"
              << "inline bool ThavaDict::remove(const ThavaValue& key) {\n"
              << "    for (size_t i = 0; i < entries.size(); ++i) {\n"
              << "        if (_thava_value_equals(entries[i].first, key)) {\n"
              << "            entries.erase(entries.begin() + i);\n"
              << "            return true;\n"
              << "        }\n"
              << "    }\n"
              << "    return false;\n"
              << "}\n\n"
              << "inline std::vector<ThavaValue> ThavaDict::keys() const {\n"
              << "    std::vector<ThavaValue> res;\n"
              << "    res.reserve(entries.size());\n"
              << "    for (const auto& kv : entries) res.push_back(kv.first);\n"
              << "    return res;\n"
              << "}\n\n"
              << "inline std::vector<ThavaValue> ThavaDict::values() const {\n"
              << "    std::vector<ThavaValue> res;\n"
              << "    res.reserve(entries.size());\n"
              << "    for (const auto& kv : entries) res.push_back(kv.second);\n"
              << "    return res;\n"
              << "}\n\n"
              << "inline std::vector<ThavaValue> ThavaDict::items() const {\n"
              << "    std::vector<ThavaValue> res;\n"
              << "    res.reserve(entries.size());\n"
              << "    for (const auto& kv : entries) res.push_back(ThavaValue(std::vector<ThavaValue>{kv.first, kv.second}));\n"
              << "    return res;\n"
              << "}\n\n"
              << "inline bool ThavaValue::has(const ThavaValue& k) const { if (type == 6 && dVal) return dVal->has(k); return false; }\n"
              << "inline bool ThavaValue::contains(const ThavaValue& k) const { return has(k); }\n"
              << "inline ThavaValue ThavaValue::get(const ThavaValue& k, const ThavaValue& def) const { if (type == 6 && dVal) return dVal->get(k, def); return def; }\n"
              << "inline void ThavaValue::set(const ThavaValue& k, const ThavaValue& v) { if (type == 6 && dVal) dVal->set(k, v); }\n"
              << "inline bool ThavaValue::remove(const ThavaValue& k) { if (type == 6 && dVal) return dVal->remove(k); return false; }\n"
              << "inline void ThavaValue::clear() { if (type == 6 && dVal) dVal->clear(); }\n"
              << "inline long long ThavaValue::size() const { if (type == 6 && dVal) return dVal->size(); if (type == 5) return (long long)lVal.size(); if (type == 4) return (long long)sVal.size(); return 0LL; }\n"
              << "inline long long ThavaValue::len() const { return size(); }\n"
              << "inline std::vector<ThavaValue> ThavaValue::keys() const { if (type == 6 && dVal) return dVal->keys(); return {}; }\n"
              << "inline std::vector<ThavaValue> ThavaValue::values() const { if (type == 6 && dVal) return dVal->values(); return {}; }\n"
              << "inline std::vector<ThavaValue> ThavaValue::items() const { if (type == 6 && dVal) return dVal->items(); return {}; }\n\n"
              << "inline std::ostream& operator<<(std::ostream& os, const ThavaDict& d) {\n"
              << "    os << \"{\";\n"
              << "    for (size_t i = 0; i < d.entries.size(); ++i) {\n"
              << "        if (d.entries[i].first.type == 4) os << \"\\\"\" << d.entries[i].first.sVal << \"\\\"\";\n"
              << "        else os << d.entries[i].first;\n"
              << "        os << \": \";\n"
              << "        if (d.entries[i].second.type == 4) os << \"\\\"\" << d.entries[i].second.sVal << \"\\\"\";\n"
              << "        else os << d.entries[i].second;\n"
              << "        if (i + 1 < d.entries.size()) os << \", \";\n"
              << "    }\n"
              << "    os << \"}\";\n"
              << "    return os;\n"
              << "}\n\n"
              << "inline std::string _thava_to_string(const ThavaDict& d) {\n"
              << "    std::ostringstream ss; ss << d; return ss.str();\n"
              << "}\n\n"
              << "inline std::vector<ThavaValue> _thava_to_iterable(const ThavaDict& d) { return d.keys(); }\n"
              << "inline ThavaValue _thava_index(const ThavaDict& d, const ThavaValue& key) { return d.get_or_throw(key); }\n"
              << "inline int _thava_list_assign(ThavaDict& d, const ThavaValue& key, const ThavaValue& val) { d.set(key, val); return 0; }\n"
              << "inline bool _thava_value_in(const ThavaValue& val, const ThavaDict& d) { return d.has(val); }\n"
              << "inline long long _thava_len(const ThavaDict& d) { return d.len(); }\n\n"
              << "inline ThavaValue _thava_index(const ThavaValue& v, const ThavaValue& key) {\n"
              << "    if (v.type == 6 && v.dVal) return v.dVal->get_or_throw(key);\n"
              << "    if (v.type == 5) return _thava_index(v.lVal, _thava_to_int(key));\n"
              << "    if (v.type == 4) return ThavaValue(_thava_index(v.sVal, _thava_to_int(key)));\n"
              << "    throw ThavaException(\"RuntimeError\", \"Data type does not support indexing!\");\n"
              << "}\n\n"
              // Membership check for list/tuple ('in' operator), strict element-wise comparison
              << "inline bool _thava_value_in(const ThavaValue& val, const std::vector<ThavaValue>& vec) {\n"
              << "    for (const auto& v : vec) if (_thava_value_equals(v, val)) return true;\n"
              << "    return false;\n"
              << "}\n\n"
              << "inline bool _thava_value_in(const ThavaValue& val, const std::string& s) {\n"
              << "    return val.type == 4 && s.find(val.sVal) != std::string::npos;\n"
              << "}\n\n"
              << "inline bool _thava_value_in(const ThavaValue& val, const ThavaValue& v) {\n"
              << "    if (v.type == 6 && v.dVal) return v.dVal->has(val);\n"
              << "    if (v.type == 5) return _thava_value_in(val, v.lVal);\n"
              << "    if (v.type == 4) return _thava_value_in(val, v.sVal);\n"
              << "    return false;\n"
              << "}\n\n"
              // List/tuple mutation functions: addval / removeval / assign / delval
              << "inline int _thava_list_addval(std::vector<ThavaValue>& vec, ThavaValue val) {\n"
              << "    vec.push_back(val); return 0;\n"
              << "}\n\n"
              << "inline int _thava_list_addval_at(std::vector<ThavaValue>& vec, long long idx, ThavaValue val) {\n"
              << "    long long i = idx;\n"
              << "    if (i < 0) i += (long long)vec.size();\n"
              << "    if (i < 0) i = 0;\n"
              << "    if (i > (long long)vec.size()) i = (long long)vec.size();\n"
              << "    vec.insert(vec.begin() + i, val); return 0;\n"
              << "}\n\n"
              << "inline int _thava_list_removeval_at(std::vector<ThavaValue>& vec, long long idx) {\n"
              << "    long long i = idx;\n"
              << "    if (i < 0) i += (long long)vec.size();\n"
              << "    if (i < 0 || i >= (long long)vec.size()) {\n"
              << "        throw ThavaException(\"RuntimeError\", \"Index out of range for deletion!\");\n"
              << "    }\n"
              << "    vec.erase(vec.begin() + i); return 0;\n"
              << "}\n\n"
              << "inline int _thava_list_removeval_by_value(std::vector<ThavaValue>& vec, ThavaValue val) {\n"
              << "    for (size_t k = 0; k < vec.size(); ++k) {\n"
              << "        if (_thava_value_equals(vec[k], val)) { vec.erase(vec.begin() + k); return 0; }\n"
              << "    }\n"
              << "    return 0;\n"
              << "}\n\n"
              << "inline int _thava_list_assign(std::vector<ThavaValue>& vec, long long idx, ThavaValue val) {\n"
              << "    long long i = idx;\n"
              << "    if (i < 0) i += (long long)vec.size();\n"
              << "    if (i < 0 || i >= (long long)vec.size()) {\n"
              << "        throw ThavaException(\"RuntimeError\", \"Index out of range for assignment!\");\n"
              << "    }\n"
              << "    vec[(size_t)i] = val; return 0;\n"
              << "}\n\n"
              << "inline int _thava_list_delval(std::vector<ThavaValue>& vec) {\n"
              << "    vec.clear(); return 0;\n"
              << "}\n\n"
              << "inline bool operator==(const ThavaValue& a, const ThavaValue& b) { return _thava_value_equals(a, b); }\n"
              << "inline bool operator!=(const ThavaValue& a, const ThavaValue& b) { return !_thava_value_equals(a, b); }\n"
              << "inline bool operator<(const ThavaValue& a, const ThavaValue& b) {\n"
              << "    if (a.type == 1 && b.type == 1) return a.iVal < b.iVal;\n"
              << "    if ((a.type == 1 || a.type == 2) && (b.type == 1 || b.type == 2)) {\n"
              << "        double da = (a.type == 1) ? (double)a.iVal : a.fVal;\n"
              << "        double db = (b.type == 1) ? (double)b.iVal : b.fVal;\n"
              << "        return da < db;\n"
              << "    }\n"
              << "    if (a.type == 4 && b.type == 4) return a.sVal < b.sVal;\n"
              << "    return a.iVal < b.iVal;\n"
              << "}\n"
              << "inline bool operator>(const ThavaValue& a, const ThavaValue& b) { return b < a; }\n"
              << "inline bool operator<=(const ThavaValue& a, const ThavaValue& b) { return !(b < a); }\n"
              << "inline bool operator>=(const ThavaValue& a, const ThavaValue& b) { return !(a < b); }\n\n"
              << "template<typename T, typename std::enable_if<std::is_arithmetic<T>::value, int>::type = 0>\n"
              << "inline bool operator<(const ThavaValue& a, T b) { return _thava_to_double(a) < (double)b; }\n"
              << "template<typename T, typename std::enable_if<std::is_arithmetic<T>::value, int>::type = 0>\n"
              << "inline bool operator>(const ThavaValue& a, T b) { return _thava_to_double(a) > (double)b; }\n"
              << "template<typename T, typename std::enable_if<std::is_arithmetic<T>::value, int>::type = 0>\n"
              << "inline bool operator<=(const ThavaValue& a, T b) { return _thava_to_double(a) <= (double)b; }\n"
              << "template<typename T, typename std::enable_if<std::is_arithmetic<T>::value, int>::type = 0>\n"
              << "inline bool operator>=(const ThavaValue& a, T b) { return _thava_to_double(a) >= (double)b; }\n"
              << "template<typename T, typename std::enable_if<std::is_arithmetic<T>::value, int>::type = 0>\n"
              << "inline bool operator<(T a, const ThavaValue& b) { return (double)a < _thava_to_double(b); }\n"
              << "template<typename T, typename std::enable_if<std::is_arithmetic<T>::value, int>::type = 0>\n"
              << "inline bool operator>(T a, const ThavaValue& b) { return (double)a > _thava_to_double(b); }\n"
              << "template<typename T, typename std::enable_if<std::is_arithmetic<T>::value, int>::type = 0>\n"
              << "inline bool operator<=(T a, const ThavaValue& b) { return (double)a <= _thava_to_double(b); }\n"
              << "template<typename T, typename std::enable_if<std::is_arithmetic<T>::value, int>::type = 0>\n"
              << "inline bool operator>=(T a, const ThavaValue& b) { return (double)a >= _thava_to_double(b); }\n\n"
              << "// Arithmetic between a typed value and a ThavaValue (typically a list/dict element).\n"
              << "// Without these the built-in candidates (int/float/double/long long) are all reachable\n"
              << "// through ThavaValue's conversion operators, so `total + xs[i]` was ambiguous.\n"
              << "// The result keeps the primitive operand's type, so integer math stays integer.\n"
              << "template<typename T, typename std::enable_if<std::is_arithmetic<T>::value, int>::type = 0>\n"
              << "inline T _thava_mixed_add(const ThavaValue& a, T b) {\n"
              << "    if constexpr (std::is_integral<T>::value) {\n"
              << "        if (a.type == 2) return (T)(a.fVal + (double)b);\n"
              << "        return (T)(_thava_to_int(a) + (long long)b);\n"
              << "    } else { return (T)(_thava_to_double(a) + (double)b); }\n"
              << "}\n"
              << "template<typename T, typename std::enable_if<std::is_arithmetic<T>::value, int>::type = 0>\n"
              << "inline T _thava_mixed_sub(const ThavaValue& a, T b) {\n"
              << "    if constexpr (std::is_integral<T>::value) {\n"
              << "        if (a.type == 2) return (T)(a.fVal - (double)b);\n"
              << "        return (T)(_thava_to_int(a) - (long long)b);\n"
              << "    } else { return (T)(_thava_to_double(a) - (double)b); }\n"
              << "}\n"
              << "template<typename T, typename std::enable_if<std::is_arithmetic<T>::value, int>::type = 0>\n"
              << "inline T _thava_mixed_mul(const ThavaValue& a, T b) {\n"
              << "    if constexpr (std::is_integral<T>::value) {\n"
              << "        if (a.type == 2) return (T)(a.fVal * (double)b);\n"
              << "        return (T)(_thava_to_int(a) * (long long)b);\n"
              << "    } else { return (T)(_thava_to_double(a) * (double)b); }\n"
              << "}\n"
              << "template<typename T, typename std::enable_if<std::is_arithmetic<T>::value, int>::type = 0>\n"
              << "inline T operator+(const ThavaValue& a, T b) { return _thava_mixed_add(a, b); }\n"
              << "template<typename T, typename std::enable_if<std::is_arithmetic<T>::value, int>::type = 0>\n"
              << "inline T operator+(T a, const ThavaValue& b) { return _thava_mixed_add(b, a); }\n"
              << "template<typename T, typename std::enable_if<std::is_arithmetic<T>::value, int>::type = 0>\n"
              << "inline T operator-(const ThavaValue& a, T b) { return _thava_mixed_sub(a, b); }\n"
              << "template<typename T, typename std::enable_if<std::is_arithmetic<T>::value, int>::type = 0>\n"
              << "inline T operator-(T a, const ThavaValue& b) { return (T)(a - _thava_to_double(b)); }\n"
              << "template<typename T, typename std::enable_if<std::is_arithmetic<T>::value, int>::type = 0>\n"
              << "inline T operator*(const ThavaValue& a, T b) { return _thava_mixed_mul(a, b); }\n"
              << "template<typename T, typename std::enable_if<std::is_arithmetic<T>::value, int>::type = 0>\n"
              << "inline T operator*(T a, const ThavaValue& b) { return _thava_mixed_mul(b, a); }\n\n"
              << "inline long long _thava_len(const std::vector<ThavaValue>& vec) { return (long long)vec.size(); }\n"
              << "inline long long _thava_len(const std::string& s) { return (long long)s.size(); }\n"
              << "inline long long _thava_len(const ThavaValue& v) {\n"
              << "    if (v.type == 6 && v.dVal) return v.dVal->len();\n"
              << "    if (v.type == 5) return (long long)v.lVal.size();\n"
              << "    if (v.type == 4) return (long long)v.sVal.size();\n"
              << "    return 0LL;\n"
              << "}\n\n"
              << "// Safe file stream manager structure (RAII Scoped Context Block)\n"
              << "struct _ThavaFileStream {\n"
              << "    std::string path;\n"
              << "    std::string mode;\n"
              << "    std::fstream fs;\n"
              << "    bool is_open = false;\n\n"
              << "    _ThavaFileStream(const std::string& p, const std::string& m = \"r\") : path(p), mode(m) {\n"
              << "        std::ios_base::openmode om = std::ios_base::binary;\n"
              << "        if (mode == \"r\") om |= std::ios_base::in;\n"
              << "        else if (mode == \"w\") om |= (std::ios_base::out | std::ios_base::trunc);\n"
              << "        else if (mode == \"a\") om |= (std::ios_base::out | std::ios_base::app);\n"
              << "        else om |= std::ios_base::in;\n\n"
              << "        fs.open(path, om);\n"
              << "        if (!fs.is_open()) {\n"
              << "            throw ThavaException(\"RuntimeError\", \"Cannot open file '\" + path + \"' in mode '\" + mode + \"' (File does not exist or permission denied)!\");\n"
              << "        }\n"
              << "        is_open = true;\n"
              << "    }\n\n"
              << "    ~_ThavaFileStream() {\n"
              << "        if (is_open && fs.is_open()) fs.close();\n"
              << "    }\n\n"
              << "    std::string read() {\n"
              << "        if (!fs.is_open()) return \"\";\n"
              << "        std::stringstream buffer; buffer << fs.rdbuf();\n"
              << "        return buffer.str();\n"
              << "    }\n\n"
              << "    std::string read_line() {\n"
              << "        if (!fs.is_open()) return \"\";\n"
              << "        std::string line;\n"
              << "        if (std::getline(fs, line)) {\n"
              << "            if (!line.empty() && line.back() == '\\r') line.pop_back();\n"
              << "            return line;\n"
              << "        }\n"
              << "        return \"\";\n"
              << "    }\n\n"
              << "    std::vector<ThavaValue> read_lines() {\n"
              << "        std::vector<ThavaValue> lines;\n"
              << "        if (!fs.is_open()) return lines;\n"
              << "        std::string line;\n"
              << "        while (std::getline(fs, line)) {\n"
              << "            if (!line.empty() && line.back() == '\\r') line.pop_back();\n"
              << "            lines.push_back(ThavaValue(line));\n"
              << "        }\n"
              << "        return lines;\n"
              << "    }\n\n"
              << "    template<typename T>\n"
              << "    void write(const T& val) {\n"
              << "        if (fs.is_open()) fs << _thava_to_string(val);\n"
              << "    }\n\n"
              << "    template<typename T>\n"
              << "    void write_line(const T& val) {\n"
              << "        if (fs.is_open()) fs << _thava_to_string(val) << \"\\n\";\n"
              << "    }\n\n"
              << "    void write_line() {\n"
              << "        if (fs.is_open()) fs << \"\\n\";\n"
              << "    }\n"
              << "};\n\n";

    // Dynamic library imported function declarations (@dlattach)
    if (!g_dlFunctions.empty()) {
        cppOutput << "// Dynamic Library Imported Functions (@dlattach):\n"
                  << "extern \"C\" {\n";
        for (const auto& [fname, info] : g_dlFunctions) {
            std::string retCpp;
            if (info.retType == ThavaType::STRING) retCpp = "const char*";
            else retCpp = cppTypeNameFor(info.retType);

            cppOutput << "    " << retCpp << " " << fname << "(";
            for (size_t i = 0; i < info.paramTypes.size(); ++i) {
                if (info.paramTypes[i] == ThavaType::STRING) {
                    cppOutput << "const char*";
                } else {
                    cppOutput << cppTypeNameFor(info.paramTypes[i]);
                }
                if (i + 1 < info.paramTypes.size()) cppOutput << ", ";
            }
            cppOutput << ");\n";
        }
        cppOutput << "}\n\n";
    }

    // Separate top-level function declarations from statement flow:
    // Forward declarations are emitted first, followed by function definitions, and finally main() / init.
    // (Thava function names are prefixed with "thava_func_" to avoid colliding with C++ main)
    // (allowing users to define a Thava function named "main" without conflict.)
    // Top-level variables are real globals: their names are registered first,
    // because class methods and functions (emitted below, before the declarations)
    // must already refer to the prefixed names. The declarations themselves are
    // emitted after the class/struct definitions they may use.
    std::vector<ASTNode*> topGlobalPtrs;
    for (const auto& node : ast) {
        if (dynamic_cast<VarDeclNode*>(node.get())) topGlobalPtrs.push_back(node.get());
    }
    std::string cppGlobalDecls = thavaCppGlobalDecls(topGlobalPtrs, 0);
    std::string cppCustomGlobalDecls = thavaCppGlobalDecls(topGlobalPtrs, 1);
    cppOutput << cppGlobalDecls;

    for (const auto& node : ast) {
        if (auto enumDecl = dynamic_cast<EnumDeclNode*>(node.get())) {
            cppOutput << enumDecl->generateCode() << "\n";
        }
    }
    for (const auto& node : ast) {
        if (auto structDecl = dynamic_cast<StructDeclNode*>(node.get())) {
            cppOutput << structDecl->generateCode() << "\n";
        }
    }
    for (const auto& node : ast) {
        if (auto fwdDecl = dynamic_cast<ForwardDeclNode*>(node.get())) {
            cppOutput << fwdDecl->generateCode();
        }
    }
    for (const auto& node : ast) {
        if (auto clsDecl = dynamic_cast<ClassDeclNode*>(node.get())) {
            cppOutput << clsDecl->generateCode() << "\n";
        }
    }
    // Class-typed globals need the definitions above; declare them now, before the
    // functions and main() that use them.
    cppOutput << cppCustomGlobalDecls;

    for (const auto& node : ast) {
        if (auto funcDecl = dynamic_cast<FunctionDeclNode*>(node.get())) {
            cppOutput << funcDecl->generateCode() << "\n";
            if (g_isLibraryMode) {
                // Generate C export wrapper so dynamic library can be called from other languages/apps
                std::string cRetType;
                if (funcDecl->returnType == ThavaType::STRING) cRetType = "const char*";
                else cRetType = cppTypeNameFor(funcDecl->returnType);

                if (funcDecl->returnType == ThavaType::STRING) {
                    cppOutput << "static thread_local std::string _thava_dl_ret_" << funcDecl->name << ";\n";
                }

                cppOutput << "extern \"C\" THAVA_EXPORT " << cRetType << " " << funcDecl->name << "(";
                for (size_t i = 0; i < funcDecl->params.size(); ++i) {
                    if (funcDecl->params[i].first == ThavaType::STRING) {
                        cppOutput << "const char* " << funcDecl->params[i].second;
                    } else {
                        cppOutput << cppParamTypeNameFor(funcDecl->params[i].first) << " " << funcDecl->params[i].second;
                    }
                    if (i + 1 < funcDecl->params.size()) cppOutput << ", ";
                }
                cppOutput << ") {\n";

                std::string callArgs = "";
                for (size_t i = 0; i < funcDecl->params.size(); ++i) {
                    if (funcDecl->params[i].first == ThavaType::STRING) {
                        callArgs += "std::string(" + funcDecl->params[i].second + " ? " + funcDecl->params[i].second + " : \"\")";
                    } else {
                        callArgs += funcDecl->params[i].second;
                    }
                    if (i + 1 < funcDecl->params.size()) callArgs += ", ";
                }

                if (funcDecl->returnType == ThavaType::VOID) {
                    cppOutput << "    thava_func_" << funcDecl->name << "(" << callArgs << ");\n";
                } else if (funcDecl->returnType == ThavaType::STRING) {
                    cppOutput << "    _thava_dl_ret_" << funcDecl->name << " = thava_func_" << funcDecl->name << "(" << callArgs << ");\n";
                    cppOutput << "    return _thava_dl_ret_" << funcDecl->name << ".c_str();\n";
                } else {
                    cppOutput << "    return thava_func_" << funcDecl->name << "(" << callArgs << ");\n";
                }
                cppOutput << "}\n\n";
            }
        }
    }

    std::string metaString = "# THAVA DYNAMIC LIBRARY METADATA\n";
    if (g_isLibraryMode) {
        for (const auto& node : ast) {
            if (auto funcDecl = dynamic_cast<FunctionDeclNode*>(node.get())) {
                metaString += "func:" + funcDecl->name + "|ret:" + typeToString(funcDecl->returnType) + "|params:";
                for (size_t i = 0; i < funcDecl->params.size(); ++i) {
                    metaString += typeToString(funcDecl->params[i].first) + " " + funcDecl->params[i].second;
                    if (i + 1 < funcDecl->params.size()) metaString += ",";
                }
                metaString += "\n";
            }
        }

        cppOutput << "// Embedded Metadata for Dynamic Library\n"
                  << "extern \"C\" THAVA_EXPORT const char* _thava_get_metadata() {\n"
                  << "    return \"__THAVA_METADATA_START__\\n\"\n";
        std::stringstream metaLines(metaString);
        std::string mline;
        while (std::getline(metaLines, mline)) {
            if (!mline.empty()) {
                cppOutput << "           \"" << mline << "\\n\"\n";
            }
        }
        cppOutput << "           \"__THAVA_METADATA_END__\\n\";\n"
                  << "}\n\n";

        cppOutput << "// Module initialization for dynamic library\n"
                  << "static void _thava_module_init() {\n"
                                     << "    try {\n";
        std::vector<ASTNode*> topNodes;
        for (const auto& node : ast) {
            if (dynamic_cast<FunctionDeclNode*>(node.get())) continue;
            if (dynamic_cast<ForwardDeclNode*>(node.get())) continue;
            if (dynamic_cast<CFuncDeclNode*>(node.get())) continue;
            if (dynamic_cast<ClassDeclNode*>(node.get())) continue;
            if (dynamic_cast<StructDeclNode*>(node.get())) continue;
            if (dynamic_cast<EnumDeclNode*>(node.get())) continue;
            topNodes.push_back(node.get());
        }
        cppOutput << thavaCppBodyCodePtrs(topNodes, "        ", true);
        cppOutput << "    } catch (...) {}\n"
                  << "}\n\n"
                  << "struct _ThavaModuleAutoInit {\n"
                  << "    _ThavaModuleAutoInit() {\n"
                  << "        _thava_module_init();\n"
                  << "    }\n"
                  << "};\n"
                  << "static _ThavaModuleAutoInit _thava_auto_init_instance;\n";
    } else {
        cppOutput << "int main() {\n"
                  << "#ifdef _WIN32\n"
                  << "    SetConsoleCP(65001);\n"
                  << "    SetConsoleOutputCP(65001);\n"
                  << "#endif\n"
                  << "    std::ios_base::sync_with_stdio(false);\n"
                  << "    std::cin.tie(nullptr);\n"
                  << "    std::cout << std::boolalpha;\n"
                  << "    try {\n";

        {
            std::vector<ASTNode*> topNodes;
            for (const auto& node : ast) {
                if (dynamic_cast<FunctionDeclNode*>(node.get())) continue; // Code already emitted above, skip here
                if (dynamic_cast<ForwardDeclNode*>(node.get())) continue;  // Code already emitted above, skip here
                if (dynamic_cast<CFuncDeclNode*>(node.get())) continue;    // cfunc declarations do not need emission in main
                if (dynamic_cast<ClassDeclNode*>(node.get())) continue;    // Class/struct code already emitted above, skip here
                if (dynamic_cast<StructDeclNode*>(node.get())) continue;
                if (dynamic_cast<EnumDeclNode*>(node.get())) continue;
                topNodes.push_back(node.get());
            }
            cppOutput << thavaCppBodyCodePtrs(topNodes, "        ", true); // globals already declared
        }

        cppOutput << "    } catch (const ThavaException& _thava_uncaught_e) {\n"
                  << "        if (_thava_uncaught_e.typeName != \"RuntimeError\" && !_thava_uncaught_e.typeName.empty()) {\n"
                  << "            std::cerr << \"[Runtime Error (\" << _thava_uncaught_e.typeName << \")]: \" << _thava_uncaught_e.what() << \"\\n\";\n"
                  << "        } else {\n"
                  << "            std::cerr << \"[Runtime Error]: \" << _thava_uncaught_e.what() << \"\\n\";\n"
                  << "        }\n"
                  << "        return 1;\n"
                  << "    } catch (const std::exception& _thava_uncaught_e) {\n"
                  << "        std::cerr << \"[Runtime Error]: \" << _thava_uncaught_e.what() << \"\\n\";\n"
                  << "        return 1;\n"
                  << "    } catch (...) {\n"
                  << "        std::cerr << \"[Runtime Error]: Unknown error!\\n\";\n"
                  << "        return 1;\n"
                  << "    }\n"
                  << "    std::cout << \"\\n[Thava Native] Build and run successful!\\n\";\n"
                  << "    return 0;\n"
                  << "}\n";
    }

    // 5. MinGW / Linux Compilation
    // Unique per run: two compilations in the same directory must not delete each
    // other's intermediate file.
    std::string tempCppFile = "_thava_temp_" +
        std::to_string((unsigned long long)std::chrono::steady_clock::now().time_since_epoch().count()) + ".cpp";
    std::ofstream tempFile(tempCppFile);
    tempFile << cppOutput.str();
    tempFile.close();

    std::string optFlag = "-O" + std::to_string(g_optLevel == 0 ? 0 : g_optLevel);
    std::string compileCmd;
    DetectedCompiler comp;

    const bool targetedCpp = isI686Mode || isWinArm64Mode;
    if (isArm64Mode && !isWinArm64Mode) {
        std::cout << "[Thava Compiler] Cross-compiling for Linux AArch64 (ARM64) via aarch64-linux-gnu-g++ (" << optFlag << ")...\n";
#ifdef _WIN32
        compileCmd = "wsl.exe -e aarch64-linux-gnu-g++ " + optFlag + " -s \"" + tempCppFile + "\" -o \"" + outputFile + "\"";
#else
        compileCmd = "aarch64-linux-gnu-g++ " + optFlag + " -s \"" + tempCppFile + "\" -o \"" + outputFile + "\"";
#endif
        comp.displayName = "aarch64-linux-gnu-g++";
    } else {
        std::string compiler = g_explicitCxx;
        if (targetedCpp && compiler.empty() && !std::getenv("CXX")) {
            const char* cross = isWinArm64Mode ? "aarch64-w64-mingw32-clang++" :
                                isElfMode ? "i686-linux-gnu-g++" : "i686-w64-mingw32-g++";
            if (probeCompilerCommand(cross)) compiler = cross;
#ifdef _WIN32
            else if (!isElfMode && probeCompilerCommand("cl")) compiler = "cl";
#endif
        }
        comp = detectHostCXXCompiler(compiler);
        if (comp.type == HostCXXType::NONE) {
            std::cerr << "[Thava Compiler Error]: No C++ compiler (g++, clang++, or cl) detected on your system!\n"
                      << "The transpiler backend (--cpp) requires a host C++17 compiler.\n\n"
                      << "To resolve this:\n"
                      << "  1. Install MinGW-w64 (GCC), LLVM Clang, or Visual Studio MSVC on your system and add to PATH.\n"
                      << "  2. Or specify a custom compiler path using environment variable CXX or flag '--cxx <path>'.\n"
                      << "  3. Or run without '--cpp' to use Thava's Zero-Dependency Native Backend directly!\n";
            std::remove(tempCppFile.c_str());
            return 1;
        }

        std::cout << "[Thava Compiler] Using detected C++ compiler: " << comp.displayName << " (" << optFlag << ")...\n";

        if (comp.type == HostCXXType::MSVC) {
            std::string msvcOpt = (g_optLevel == 0) ? "/Od" : "/O2";
            compileCmd = comp.command + " /nologo /std:c++17 /EHsc /utf-8 " + msvcOpt;
            if (g_isLibraryMode) compileCmd += " /LD";
            if (isI686Mode && !isElfMode) compileCmd += " /arch:IA32";
            compileCmd += " \"" + tempCppFile + "\"";
            for (const auto& libPath : g_dlLibraries) compileCmd += " \"" + libPath + "\"";
            compileCmd += " gdi32.lib user32.lib kernel32.lib comctl32.lib comdlg32.lib";
            compileCmd += " /Fe:\"" + outputFile + "\"";
        } else {
            compileCmd = comp.command + " -std=c++17 " + optFlag;
            if (comp.type == HostCXXType::GCC)
                compileCmd += " -finput-charset=UTF-8 -fexec-charset=UTF-8";
            if (isI686Mode) compileCmd += " -m32 -march=i686";
            else if (!targetedCpp) compileCmd += " -march=native";
            if (g_isLibraryMode) compileCmd += " -shared -fPIC";
            compileCmd += " -s \"" + tempCppFile + "\"";
            for (const auto& libPath : g_dlLibraries) compileCmd += " \"" + libPath + "\"";
#ifdef _WIN32
            const bool linkWindows = !targetedCpp || !isElfMode;
#else
            const bool linkWindows = targetedCpp && !isElfMode;
#endif
            if (linkWindows) {
                compileCmd += " -lgdi32 -luser32 -lkernel32 -lcomctl32 -lcomdlg32";
                if (isI686Mode && comp.type == HostCXXType::GCC && !g_isLibraryMode) compileCmd += " -static";
            } else {
                compileCmd += " -Wl,-rpath,'$ORIGIN' -Wl,-rpath,. -ldl";
            }
            compileCmd += " -o \"" + outputFile + "\"";
        }
    }

    int res = std::system(compileCmd.c_str());

    if (res == 0) {
        std::remove(tempCppFile.c_str());
        if (g_isLibraryMode) {
            std::cout << "[Thava Compiler] Success! Dynamic library: " << outputFile << " (100% Embedded Metadata, companion .meta not required)\n";
        } else {
            std::cout << "[Thava Compiler] Success! Executable: " << outputFile << "\n";
        }
    } else {
        std::cerr << "[Thava Compiler] Compilation failed using " << comp.displayName << "! (Intermediate C++ code saved at '" << tempCppFile << "')\n";
        return 1;
    }

    return 0;
}

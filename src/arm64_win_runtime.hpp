#pragma once

#include "pe_emitter.hpp"
#include "arm64_encoder.hpp"
#include "arm64_runtime_helpers.hpp"
#include <string>
#include <cstdint>

class Arm64WinRuntime {
public:
    arm64::Encoder& enc;
    PeEmitter& pe;

    uint32_t offFmtStrNL = 0;
    uint32_t offFmtIntNL = 0;
    uint32_t offFmtStr   = 0;
    uint32_t offFmtInt   = 0;
    uint32_t offStrTrue  = 0;
    uint32_t offStrFalse = 0;
    uint32_t offStrNL    = 0;
    uint32_t offFmtFloatNL = 0; // "%g\n"
    uint32_t offFmtFloat   = 0; // "%g"
    uint32_t offFmtScanLine = 0; // " %1023[^\n]" (one console line)
    uint32_t offStrListOpen  = 0; // "["
    uint32_t offStrListComma = 0; // ", "
    uint32_t offStrListClose = 0; // "]\n"

    size_t lblExit = 0;
    size_t lblPrintStrNL = 0;
    size_t lblPrintIntNL = 0;
    size_t lblPrintBoolNL = 0;
    size_t lblPrintStr = 0;
    size_t lblPrintInt = 0;
    size_t lblPrintDouble = 0;
    size_t lblPrintDoubleNL = 0;
    size_t lblMalloc = 0;
    size_t lblPrintNL = 0;
    size_t lblSubstr = 0;
    size_t lblSubstrRange = 0;
    size_t lblStrLen = 0;
    size_t lblStrConcat = 0;
    size_t lblStrMul = 0;
    size_t lblPow = 0;

    // Helpers implemented only by the Linux ARM64 runtime. The codegen refuses
    // to emit code that would use them for the Windows ARM64 backend.
    // lblInputLine / lblAtoi / lblAtoD are no longer in this group: they are
    // implemented below with msvcrt (scanf / _atoi64 / atof).
    size_t lblMemCpy = 0;
    size_t lblStrLower = 0;
    size_t lblStrUpper = 0;
    size_t lblStrTitle = 0;
    size_t lblStrStrip = 0;
    size_t lblStrFind = 0;
    size_t lblStrStartsWith = 0;
    size_t lblStrEndsWith = 0;
    size_t lblStrCount = 0;
    size_t lblStrCmpEq = 0;
    size_t lblStrReplace = 0;
    size_t lblStrSplit = 0;
    size_t lblStrJoin = 0;
    size_t lblListIn = 0;
    size_t lblInputLine = 0;
    size_t lblAtoi = 0;
    size_t lblAtoD = 0;
    size_t lblTimeFormat = 0;
    size_t lblListDir = 0;
    size_t lblSetEnv = 0;
    size_t lblNamedModule = 0;
    size_t lblMessageIcon = 0;
    size_t lblMessageButtons = 0;
    size_t lblSeedRand = 0;
    size_t lblRand = 0;
    size_t lblWeightedChoice = 0;
    size_t lblRandChoice = 0;
    size_t lblGetenv = 0;
    size_t lblDictFind = 0;
    size_t lblFileMake = 0;
    size_t lblFileOpen = 0;
    size_t lblFileClose = 0;
    size_t lblFileWrite = 0;
    size_t lblIntToBuf = 0;
    size_t lblIntToStr = 0;
    size_t lblFileWriteInt = 0;
    size_t lblFileFmtDouble = 0;
    size_t lblFileWriteDouble = 0;
    size_t lblFileReadLine = 0;
    size_t lblFileReadBuffer = 0;
    size_t lblFileReadAll = 0;
    size_t lblFileReadLines = 0;
    uint32_t offFmtIntString = 0;
    uint32_t offFileReadErr = 0;
    uint32_t offStrQuoteNL = 0;

    uint32_t offEmptyStr = 0;
    uint32_t offStrSh = 0;
    uint32_t offStrDashC = 0;
    uint32_t offStrProcEnviron = 0;
    uint32_t offDoubleTen = 0;
    uint32_t offWeightMismatch = 0;
    uint32_t offDoubleMaxRand = 0;

    struct TimeToken { int field; uint32_t token; uint32_t format; };
    std::vector<TimeToken> timeTokens;
    uint32_t offModuleCache = 0;
    uint32_t offDirWildcard = 0, offDot = 0, offDotDot = 0, offEquals = 0;
    std::vector<std::pair<uint32_t, int>> messageIcons, messageButtons;
    uint32_t offTimeDefault[2]{};
    uint32_t offTimeAliases[3]{};
    uint32_t offTimePatterns[3]{};
    std::map<std::string, size_t> windowsImportLabels;
    bool initialized = false;

    Arm64WinRuntime(arm64::Encoder& e, PeEmitter& p)
        : enc(e), pe(p) {
        lblExit        = enc.createLabel();
        lblPrintStrNL  = enc.createLabel();
        lblPrintIntNL  = enc.createLabel();
        lblPrintBoolNL = enc.createLabel();
        lblPrintStr      = enc.createLabel();
        lblPrintInt      = enc.createLabel();
        lblPrintDouble   = enc.createLabel();
        lblPrintDoubleNL = enc.createLabel();
        lblMalloc        = enc.createLabel();
        lblPrintNL       = enc.createLabel();
        lblSubstr        = enc.createLabel();
        lblSubstrRange   = enc.createLabel();
        lblStrLen        = enc.createLabel();
        lblStrConcat     = enc.createLabel();
        lblStrMul        = enc.createLabel();
        lblPow           = enc.createLabel();
        lblMemCpy        = enc.createLabel();
        lblStrLower      = enc.createLabel();
        lblStrUpper      = enc.createLabel();
        lblStrTitle      = enc.createLabel();
        lblStrStrip      = enc.createLabel();
        lblStrFind       = enc.createLabel();
        lblStrStartsWith = enc.createLabel();
        lblStrEndsWith   = enc.createLabel();
        lblStrCount      = enc.createLabel();
        lblStrCmpEq      = enc.createLabel();
        lblStrReplace    = enc.createLabel();
        lblStrSplit      = enc.createLabel();
        lblStrJoin       = enc.createLabel();
        lblListIn        = enc.createLabel();
        lblInputLine     = enc.createLabel();
        lblAtoi          = enc.createLabel();
        lblAtoD          = enc.createLabel();
        lblTimeFormat    = enc.createLabel();
        lblListDir       = enc.createLabel();
        lblSetEnv        = enc.createLabel();
        lblNamedModule   = enc.createLabel();
        lblMessageIcon   = enc.createLabel();
        lblMessageButtons = enc.createLabel();
        lblSeedRand      = enc.createLabel();
        lblRand          = enc.createLabel();
        lblWeightedChoice = enc.createLabel();
        lblRandChoice    = enc.createLabel();
        lblGetenv        = enc.createLabel();
        lblDictFind      = enc.createLabel();
        lblFileMake      = enc.createLabel();
        lblFileOpen      = enc.createLabel();
        lblFileClose     = enc.createLabel();
        lblFileWrite     = enc.createLabel();
        lblIntToBuf      = enc.createLabel();
        lblIntToStr      = enc.createLabel();
        lblFileWriteInt  = enc.createLabel();
        lblFileFmtDouble = enc.createLabel();
        lblFileWriteDouble = enc.createLabel();
        lblFileReadLine  = enc.createLabel();
        lblFileReadBuffer = enc.createLabel();
        lblFileReadAll   = enc.createLabel();
        lblFileReadLines = enc.createLabel();
    }

    size_t importFunction(const char* dll, const std::string& name) {
        auto found = windowsImportLabels.find(name);
        if (found != windowsImportLabels.end()) return found->second;
        pe.addImport(dll, name);
        return windowsImportLabels.emplace(name, enc.createLabel()).first->second;
    }

    void init() {
        if (initialized) return;
        initialized = true;

        for (const char* name : {"GetCurrentProcessId", "Sleep", "GetLocalTime",
                                 "GetFileAttributesA", "GetFileAttributesExA",
                                 "GetActiveProcessorCount", "FindFirstFileA",
                                 "FindNextFileA", "FindClose", "LoadLibraryA", "GetProcAddress", "FreeLibrary"}) {
            pe.addImport("kernel32.dll", name);
            windowsImportLabels.emplace(name, enc.createLabel());
        }
        for (const char* name : {"system", "_chdir", "_mkdir", "_rmdir", "remove",
                                 "rename", "getenv", "_getcwd", "time", "free", "_strdup"}) {
            windowsImportLabels.emplace(name, enc.createLabel());
        }

        windowsImportLabels.emplace("MessageBoxA", enc.createLabel());
        pe.addImport("msvcrt.dll", "_putenv");
        pe.addImport("msvcrt.dll", "_strdup");
        offDirWildcard = pe.addStringLiteralOffset("\\*");
        offDot = pe.addStringLiteralOffset(".");
        offDotDot = pe.addStringLiteralOffset("..");
        offEquals = pe.addStringLiteralOffset("=");
        offModuleCache = pe.addDataSlot();
        for (const auto& entry : std::initializer_list<std::pair<const char*, int>>{
                {"warning", 0x30}, {"information", 0x40}, {"infomation", 0x40},
                {"info", 0x40}, {"question", 0x20}, {"error", 0x10}})
            messageIcons.emplace_back(pe.addStringLiteralOffset(entry.first), entry.second);
        for (const auto& entry : std::initializer_list<std::pair<const char*, int>>{
                {"ok/cancel", 1}, {"okcancel", 1}, {"yes/no/cancel", 3},
                {"yesnocancel", 3}, {"yes/no", 4}, {"yesno", 4},
                {"retry/cancel", 5}, {"retrycancel", 5}})
            messageButtons.emplace_back(pe.addStringLiteralOffset(entry.first), entry.second);

        offTimeDefault[0] = pe.addStringLiteralOffset("%Y-%m-%d %H:%M:%S");
        offTimeDefault[1] = pe.addStringLiteralOffset("%Y-%m-%d");
        offTimeAliases[0] = pe.addStringLiteralOffset("datetime");
        offTimeAliases[1] = pe.addStringLiteralOffset("date");
        offTimeAliases[2] = pe.addStringLiteralOffset("time");
        offTimePatterns[0] = offTimeDefault[0];
        offTimePatterns[1] = offTimeDefault[1];
        offTimePatterns[2] = pe.addStringLiteralOffset("%H:%M:%S");
        const auto addTokens = [&](int field, const char* format,
                                   std::initializer_list<const char*> tokens) {
            const auto formatOffset = pe.addStringLiteralOffset(format);
            for (const auto* token : tokens)
                timeTokens.push_back({field, pe.addStringLiteralOffset(token), formatOffset});
        };
        addTokens(0, "%04d", {"%Y", "yyyy", "YYYY"});
        addTokens(10, ":%02d", {":mm", ":MM"});
        addTokens(10, "%02d:", {"mm:", "MM:"});
        addTokens(2, "%02d", {"%m", "mm", "MM"});
        addTokens(6, "%02d", {"%d", "dd", "DD"});
        addTokens(8, "%02d", {"%H", "hh", "HH"});
        addTokens(10, "%02d", {"%M"});
        addTokens(12, "%02d", {"%S", "ss", "SS"});

        offFmtStrNL = pe.addStringLiteralOffset("%s\n");
        offFmtIntNL = pe.addStringLiteralOffset("%lld\n");
        offFmtStr   = pe.addStringLiteralOffset("%s");
        offFmtInt   = pe.addStringLiteralOffset("%lld");
        offStrTrue  = pe.addStringLiteralOffset("true");
        offStrFalse = pe.addStringLiteralOffset("false");
        offStrNL    = pe.addStringLiteralOffset("\n");
        offFmtFloatNL   = pe.addStringLiteralOffset("%g\n");
        offFmtFloat     = pe.addStringLiteralOffset("%g");
        offFmtScanLine  = pe.addStringLiteralOffset(" %1023[^\n]");
        offStrListOpen  = pe.addStringLiteralOffset("[");
        offStrListComma = pe.addStringLiteralOffset(", ");
        offWeightMismatch = pe.addStringLiteralOffset("Runtime Error: randres() weight count must match list size!\n");
        offDoubleMaxRand = pe.addDoubleLiteralOffset(32767.0);
        offEmptyStr = pe.addStringLiteralOffset("");
        offFmtIntString = pe.addStringLiteralOffset("%lld");
        offFileReadErr = pe.addStringLiteralOffset("[Runtime Error (RuntimeError)]: Cannot open file '");
        offStrQuoteNL = pe.addStringLiteralOffset("'\n");
        pe.addImport("msvcrt.dll", "fgetc");
        offStrListClose = pe.addStringLiteralOffset("]\n");
    }

    void emitHelpers() {
        if (!initialized) init();

        // Must layout .rdata so all string offsets and IAT RVAs are finalized
        pe.layoutRdata();

        uint32_t iatPrintf = pe.getFuncIatOffsetInRdata("printf");
        uint32_t iatExit   = pe.getFuncIatOffsetInRdata("exit");
        uint32_t iatMalloc = pe.getFuncIatOffsetInRdata("malloc");

        // ==========================================
        // 1. print_str_nl(const char* s in X0)
        // Calling printf("%s\n", s) from msvcrt.dll
        // AAPCS64: X0 = fmt, X1 = string
        // ==========================================
        enc.bindLabel(lblPrintStrNL);
        enc.stp_pre(arm64::FP, arm64::LR, arm64::SP, -16);
        enc.mov_rr(arm64::FP, arm64::SP);
        enc.sub_rri(arm64::SP, arm64::SP, 16); // Maintain 16-byte stack alignment

        enc.mov_rr(arm64::X1, arm64::X0);
        enc.adr_data(arm64::X0, offFmtStrNL);
        enc.adr_data(arm64::X16, iatPrintf);
        enc.ldr_imm(arm64::X16, arm64::X16, 0);
        enc.blr(arm64::X16);

        enc.mov_rr(arm64::SP, arm64::FP);
        enc.ldp_post(arm64::FP, arm64::LR, arm64::SP, 16);
        enc.ret();

        // ==========================================
        // 2. print_int_nl(int64_t val in X0)
        // Calling printf("%lld\n", val) from msvcrt.dll
        // AAPCS64: X0 = fmt, X1 = val
        // ==========================================
        enc.bindLabel(lblPrintIntNL);
        enc.stp_pre(arm64::FP, arm64::LR, arm64::SP, -16);
        enc.mov_rr(arm64::FP, arm64::SP);
        enc.sub_rri(arm64::SP, arm64::SP, 16);

        enc.mov_rr(arm64::X1, arm64::X0);
        enc.adr_data(arm64::X0, offFmtIntNL);
        enc.adr_data(arm64::X16, iatPrintf);
        enc.ldr_imm(arm64::X16, arm64::X16, 0);
        enc.blr(arm64::X16);

        enc.mov_rr(arm64::SP, arm64::FP);
        enc.ldp_post(arm64::FP, arm64::LR, arm64::SP, 16);
        enc.ret();

        // ==========================================
        // 3. print_bool_nl(bool val in X0)
        // Calls printf("%s\n", "true" / "false")
        // ==========================================
        enc.bindLabel(lblPrintBoolNL);
        enc.stp_pre(arm64::FP, arm64::LR, arm64::SP, -16);
        enc.mov_rr(arm64::FP, arm64::SP);
        enc.sub_rri(arm64::SP, arm64::SP, 16);

        size_t falseLbl = enc.createLabel();
        size_t callLbl  = enc.createLabel();

        enc.cbz(arm64::X0, falseLbl);
        enc.adr_data(arm64::X1, offStrTrue);
        enc.b(callLbl);

        enc.bindLabel(falseLbl);
        enc.adr_data(arm64::X1, offStrFalse);

        enc.bindLabel(callLbl);
        enc.adr_data(arm64::X0, offFmtStrNL);
        enc.adr_data(arm64::X16, iatPrintf);
        enc.ldr_imm(arm64::X16, arm64::X16, 0);
        enc.blr(arm64::X16);

        enc.mov_rr(arm64::SP, arm64::FP);
        enc.ldp_post(arm64::FP, arm64::LR, arm64::SP, 16);
        enc.ret();

        // ==========================================
        // 4. exit(int code in X0)
        // Calling exit(code) from msvcrt.dll
        // ==========================================
        enc.bindLabel(lblExit);
        enc.adr_data(arm64::X16, iatExit);
        enc.ldr_imm(arm64::X16, arm64::X16, 0);
        enc.blr(arm64::X16);
        enc.ret();

        // ==========================================
        // 5. print_str(const char* s in X0) - no newline
        // ==========================================
        enc.bindLabel(lblPrintStr);
        enc.stp_pre(arm64::FP, arm64::LR, arm64::SP, -16);
        enc.mov_rr(arm64::FP, arm64::SP);
        enc.sub_rri(arm64::SP, arm64::SP, 16);

        enc.mov_rr(arm64::X1, arm64::X0);
        enc.adr_data(arm64::X0, offFmtStr);
        enc.adr_data(arm64::X16, iatPrintf);
        enc.ldr_imm(arm64::X16, arm64::X16, 0);
        enc.blr(arm64::X16);

        enc.mov_rr(arm64::SP, arm64::FP);
        enc.ldp_post(arm64::FP, arm64::LR, arm64::SP, 16);
        enc.ret();

        // ==========================================
        // 6. print_int(int64_t val in X0) - no newline
        // ==========================================
        enc.bindLabel(lblPrintInt);
        enc.stp_pre(arm64::FP, arm64::LR, arm64::SP, -16);
        enc.mov_rr(arm64::FP, arm64::SP);
        enc.sub_rri(arm64::SP, arm64::SP, 16);

        enc.mov_rr(arm64::X1, arm64::X0);
        enc.adr_data(arm64::X0, offFmtInt);
        enc.adr_data(arm64::X16, iatPrintf);
        enc.ldr_imm(arm64::X16, arm64::X16, 0);
        enc.blr(arm64::X16);

        enc.mov_rr(arm64::SP, arm64::FP);
        enc.ldp_post(arm64::FP, arm64::LR, arm64::SP, 16);
        enc.ret();

        // ==========================================
        // 7. print_double(double in D0) - no newline
        // ==========================================
        enc.bindLabel(lblPrintDouble);
        enc.stp_pre(arm64::FP, arm64::LR, arm64::SP, -16);
        enc.mov_rr(arm64::FP, arm64::SP);
        enc.sub_rri(arm64::SP, arm64::SP, 16);

        // Windows ARM64 passes variadic floating-point arguments in integer registers.
        enc.fmov_xd(arm64::X1, arm64::D0);
        enc.adr_data(arm64::X0, offFmtFloat);
        enc.adr_data(arm64::X16, iatPrintf);
        enc.ldr_imm(arm64::X16, arm64::X16, 0);
        enc.blr(arm64::X16);

        enc.mov_rr(arm64::SP, arm64::FP);
        enc.ldp_post(arm64::FP, arm64::LR, arm64::SP, 16);
        enc.ret();

        // ==========================================
        // 8. print_double_nl(double in D0)
        // ==========================================
        enc.bindLabel(lblPrintDoubleNL);
        enc.stp_pre(arm64::FP, arm64::LR, arm64::SP, -16);
        enc.mov_rr(arm64::FP, arm64::SP);
        enc.sub_rri(arm64::SP, arm64::SP, 16);

        // Windows ARM64 passes variadic floating-point arguments in integer registers.
        enc.fmov_xd(arm64::X1, arm64::D0);
        enc.adr_data(arm64::X0, offFmtFloatNL);
        enc.adr_data(arm64::X16, iatPrintf);
        enc.ldr_imm(arm64::X16, arm64::X16, 0);
        enc.blr(arm64::X16);

        enc.mov_rr(arm64::SP, arm64::FP);
        enc.ldp_post(arm64::FP, arm64::LR, arm64::SP, 16);
        enc.ret();

        // ==========================================
        // 9. malloc(size in X0) -> X0  (msvcrt malloc)
        // ==========================================
        enc.bindLabel(lblMalloc);
        enc.stp_pre(arm64::FP, arm64::LR, arm64::SP, -16);
        enc.mov_rr(arm64::FP, arm64::SP);
        enc.sub_rri(arm64::SP, arm64::SP, 16);

        enc.adr_data(arm64::X16, iatMalloc);
        enc.ldr_imm(arm64::X16, arm64::X16, 0);
        enc.blr(arm64::X16);

        enc.mov_rr(arm64::SP, arm64::FP);
        enc.ldp_post(arm64::FP, arm64::LR, arm64::SP, 16);
        enc.ret();

        // ==========================================
        // 10. print_nl()
        // ==========================================
        enc.bindLabel(lblPrintNL);
        enc.stp_pre(arm64::FP, arm64::LR, arm64::SP, -16);
        enc.mov_rr(arm64::FP, arm64::SP);
        enc.sub_rri(arm64::SP, arm64::SP, 16);

        enc.adr_data(arm64::X0, offStrNL);
        enc.bl(lblPrintStr);

        enc.mov_rr(arm64::SP, arm64::FP);
        enc.ldp_post(arm64::FP, arm64::LR, arm64::SP, 16);
        enc.ret();

        // ==========================================
        // 11. substr(s in X0, start in X1, count in X2) -> X0
        // ==========================================
        enc.bindLabel(lblSubstr);
        enc.stp_pre(arm64::FP, arm64::LR, arm64::SP, -96);
        enc.mov_rr(arm64::FP, arm64::SP);
        enc.str_imm(arm64::X0, arm64::FP, 16); // s
        enc.str_imm(arm64::X1, arm64::FP, 24); // start
        enc.str_imm(arm64::X2, arm64::FP, 32); // count

        // out = malloc(count + 1)
        enc.add_rri(arm64::X0, arm64::X2, 1);
        enc.bl(lblMalloc);
        enc.str_imm(arm64::X0, arm64::FP, 40); // out

        enc.movz(arm64::X3, 0, 0);
        enc.str_imm(arm64::X3, arm64::FP, 48); // i

        size_t loopLbl = enc.createLabel();
        size_t doneLbl = enc.createLabel();
        enc.bindLabel(loopLbl);
        enc.ldr_imm(arm64::X3, arm64::FP, 48);
        enc.ldr_imm(arm64::X2, arm64::FP, 32);
        enc.cmp_rr(arm64::X3, arm64::X2);
        enc.b_cond(arm64::GE, doneLbl);

        enc.ldr_imm(arm64::X0, arm64::FP, 16);
        enc.ldr_imm(arm64::X1, arm64::FP, 24);
        enc.add_rrr(arm64::X0, arm64::X0, arm64::X1);
        enc.add_rrr(arm64::X0, arm64::X0, arm64::X3);
        enc.ldrb_imm(arm64::X4, arm64::X0, 0);

        enc.ldr_imm(arm64::X5, arm64::FP, 40);
        enc.add_rrr(arm64::X5, arm64::X5, arm64::X3);
        enc.strb_imm(arm64::X4, arm64::X5, 0);

        enc.add_rri(arm64::X3, arm64::X3, 1);
        enc.str_imm(arm64::X3, arm64::FP, 48);
        enc.b(loopLbl);

        enc.bindLabel(doneLbl);
        enc.ldr_imm(arm64::X5, arm64::FP, 40);
        enc.ldr_imm(arm64::X3, arm64::FP, 32);
        enc.add_rrr(arm64::X5, arm64::X5, arm64::X3);
        enc.movz(arm64::X4, 0, 0);
        enc.strb_imm(arm64::X4, arm64::X5, 0);
        enc.ldr_imm(arm64::X0, arm64::FP, 40);

        enc.ldp_post(arm64::FP, arm64::LR, arm64::SP, 96);
        enc.ret();

        // ==========================================
        // 11b. substr_range(s in X0, a in X1, b in X2)
        // 1-based, inclusive, clamped (x86-compatible)
        // ==========================================
        enc.bindLabel(lblSubstrRange);
        enc.stp_pre(arm64::FP, arm64::LR, arm64::SP, -96);
        enc.mov_rr(arm64::FP, arm64::SP);
        enc.str_imm(arm64::X0, arm64::FP, 16);
        enc.str_imm(arm64::X1, arm64::FP, 24);
        enc.str_imm(arm64::X2, arm64::FP, 32);
        {
            size_t emptyLbl = enc.createLabel();
            size_t aOk = enc.createLabel();
            size_t bOk = enc.createLabel();
            size_t loopLbl = enc.createLabel();
            size_t finLbl = enc.createLabel();

            enc.cbz(arm64::X0, emptyLbl);
            enc.bl(lblStrLen);
            enc.str_imm(arm64::X0, arm64::FP, 40); // n
            enc.cbz(arm64::X0, emptyLbl);

            enc.ldr_imm(arm64::X1, arm64::FP, 24);
            enc.movz(arm64::X3, 1, 0);
            enc.cmp_rr(arm64::X1, arm64::X3);
            enc.b_cond(arm64::GE, aOk);
            enc.str_imm(arm64::X3, arm64::FP, 24);
            enc.bindLabel(aOk);

            enc.ldr_imm(arm64::X2, arm64::FP, 32);
            enc.ldr_imm(arm64::X3, arm64::FP, 40);
            enc.cmp_rr(arm64::X2, arm64::X3);
            enc.b_cond(arm64::LE, bOk);
            enc.str_imm(arm64::X3, arm64::FP, 32);
            enc.bindLabel(bOk);

            enc.ldr_imm(arm64::X1, arm64::FP, 24);
            enc.ldr_imm(arm64::X2, arm64::FP, 32);
            enc.cmp_rr(arm64::X1, arm64::X2);
            enc.b_cond(arm64::GT, emptyLbl);

            enc.sub_rrr(arm64::X3, arm64::X2, arm64::X1);
            enc.add_rri(arm64::X3, arm64::X3, 1);
            enc.str_imm(arm64::X3, arm64::FP, 48); // count
            enc.sub_rri(arm64::X3, arm64::X1, 1);
            enc.str_imm(arm64::X3, arm64::FP, 56); // start0

            enc.ldr_imm(arm64::X0, arm64::FP, 48);
            enc.add_rri(arm64::X0, arm64::X0, 1);
            enc.bl(lblMalloc);
            enc.str_imm(arm64::X0, arm64::FP, 64);

            enc.movz(arm64::X3, 0, 0);
            enc.str_imm(arm64::X3, arm64::FP, 72);
            enc.bindLabel(loopLbl);
            enc.ldr_imm(arm64::X3, arm64::FP, 72);
            enc.ldr_imm(arm64::X2, arm64::FP, 48);
            enc.cmp_rr(arm64::X3, arm64::X2);
            enc.b_cond(arm64::GE, finLbl);
            enc.ldr_imm(arm64::X0, arm64::FP, 16);
            enc.ldr_imm(arm64::X1, arm64::FP, 56);
            enc.add_rrr(arm64::X0, arm64::X0, arm64::X1);
            enc.add_rrr(arm64::X0, arm64::X0, arm64::X3);
            enc.ldrb_imm(arm64::X4, arm64::X0, 0);
            enc.ldr_imm(arm64::X5, arm64::FP, 64);
            enc.add_rrr(arm64::X5, arm64::X5, arm64::X3);
            enc.strb_imm(arm64::X4, arm64::X5, 0);
            enc.add_rri(arm64::X3, arm64::X3, 1);
            enc.str_imm(arm64::X3, arm64::FP, 72);
            enc.b(loopLbl);

            enc.bindLabel(finLbl);
            enc.ldr_imm(arm64::X5, arm64::FP, 64);
            enc.ldr_imm(arm64::X3, arm64::FP, 48);
            enc.add_rrr(arm64::X5, arm64::X5, arm64::X3);
            enc.movz(arm64::X4, 0, 0);
            enc.strb_imm(arm64::X4, arm64::X5, 0);
            enc.ldr_imm(arm64::X0, arm64::FP, 64);
            enc.ldp_post(arm64::FP, arm64::LR, arm64::SP, 96);
            enc.ret();

            enc.bindLabel(emptyLbl);
            enc.movz(arm64::X0, 1, 0);
            enc.bl(lblMalloc);
            enc.movz(arm64::X4, 0, 0);
            enc.strb_imm(arm64::X4, arm64::X0, 0);
            enc.ldp_post(arm64::FP, arm64::LR, arm64::SP, 96);
            enc.ret();
        }

        // ==========================================
        // 12. strlen(const char* in X0) -> X0
        // ==========================================
        {
            enc.bindLabel(lblStrLen);
            enc.mov_rr(arm64::X1, arm64::X0);
            size_t loopLbl = enc.createLabel();
            size_t doneLbl = enc.createLabel();
            enc.bindLabel(loopLbl);
            enc.ldrb_imm(arm64::X2, arm64::X1, 0);
            enc.cbz(arm64::X2, doneLbl);
            enc.add_rri(arm64::X1, arm64::X1, 1);
            enc.b(loopLbl);
            enc.bindLabel(doneLbl);
            enc.sub_rrr(arm64::X0, arm64::X1, arm64::X0);
            enc.ret();
        }

        // ==========================================
        // 13. str_concat(s1 in X0, s2 in X1) -> X0
        // ==========================================
        {
            enc.bindLabel(lblStrConcat);
            enc.stp_pre(arm64::FP, arm64::LR, arm64::SP, -96);
            enc.mov_rr(arm64::FP, arm64::SP);
            enc.str_imm(arm64::X0, arm64::FP, 16);
            enc.str_imm(arm64::X1, arm64::FP, 24);

            enc.bl(lblStrLen);
            enc.str_imm(arm64::X0, arm64::FP, 32); // len1
            enc.ldr_imm(arm64::X0, arm64::FP, 24);
            enc.bl(lblStrLen);
            enc.str_imm(arm64::X0, arm64::FP, 40); // len2

            enc.ldr_imm(arm64::X0, arm64::FP, 32);
            enc.ldr_imm(arm64::X1, arm64::FP, 40);
            enc.add_rrr(arm64::X0, arm64::X0, arm64::X1);
            enc.add_rri(arm64::X0, arm64::X0, 1);
            enc.bl(lblMalloc);
            enc.str_imm(arm64::X0, arm64::FP, 48); // out

            enc.movz(arm64::X3, 0, 0);
            enc.str_imm(arm64::X3, arm64::FP, 56); // i
            size_t l1 = enc.createLabel();
            size_t a1 = enc.createLabel();
            enc.bindLabel(l1);
            enc.ldr_imm(arm64::X3, arm64::FP, 56);
            enc.ldr_imm(arm64::X2, arm64::FP, 32);
            enc.cmp_rr(arm64::X3, arm64::X2);
            enc.b_cond(arm64::GE, a1);
            enc.ldr_imm(arm64::X0, arm64::FP, 16);
            enc.add_rrr(arm64::X0, arm64::X0, arm64::X3);
            enc.ldrb_imm(arm64::X4, arm64::X0, 0);
            enc.ldr_imm(arm64::X5, arm64::FP, 48);
            enc.add_rrr(arm64::X5, arm64::X5, arm64::X3);
            enc.strb_imm(arm64::X4, arm64::X5, 0);
            enc.add_rri(arm64::X3, arm64::X3, 1);
            enc.str_imm(arm64::X3, arm64::FP, 56);
            enc.b(l1);

            enc.bindLabel(a1);
            enc.movz(arm64::X3, 0, 0);
            enc.str_imm(arm64::X3, arm64::FP, 56);
            size_t l2 = enc.createLabel();
            size_t a2 = enc.createLabel();
            enc.bindLabel(l2);
            enc.ldr_imm(arm64::X3, arm64::FP, 56);
            enc.ldr_imm(arm64::X2, arm64::FP, 40);
            enc.cmp_rr(arm64::X3, arm64::X2);
            enc.b_cond(arm64::GE, a2);
            enc.ldr_imm(arm64::X0, arm64::FP, 24);
            enc.add_rrr(arm64::X0, arm64::X0, arm64::X3);
            enc.ldrb_imm(arm64::X4, arm64::X0, 0);
            enc.ldr_imm(arm64::X5, arm64::FP, 48);
            enc.ldr_imm(arm64::X2, arm64::FP, 32);
            enc.add_rrr(arm64::X5, arm64::X5, arm64::X2);
            enc.add_rrr(arm64::X5, arm64::X5, arm64::X3);
            enc.strb_imm(arm64::X4, arm64::X5, 0);
            enc.add_rri(arm64::X3, arm64::X3, 1);
            enc.str_imm(arm64::X3, arm64::FP, 56);
            enc.b(l2);

            enc.bindLabel(a2);
            enc.ldr_imm(arm64::X5, arm64::FP, 48);
            enc.ldr_imm(arm64::X2, arm64::FP, 32);
            enc.ldr_imm(arm64::X3, arm64::FP, 40);
            enc.add_rrr(arm64::X5, arm64::X5, arm64::X2);
            enc.add_rrr(arm64::X5, arm64::X5, arm64::X3);
            enc.movz(arm64::X4, 0, 0);
            enc.strb_imm(arm64::X4, arm64::X5, 0);
            enc.ldr_imm(arm64::X0, arm64::FP, 48);

            enc.ldp_post(arm64::FP, arm64::LR, arm64::SP, 96);
            enc.ret();
        }

        // ==========================================
        // 14. str_mul(s in X0, count in X1) -> X0
        // ==========================================
        {
            enc.bindLabel(lblStrMul);
            enc.stp_pre(arm64::FP, arm64::LR, arm64::SP, -96);
            enc.mov_rr(arm64::FP, arm64::SP);
            enc.str_imm(arm64::X0, arm64::FP, 16);
            enc.str_imm(arm64::X1, arm64::FP, 24);

            enc.bl(lblStrLen);
            enc.str_imm(arm64::X0, arm64::FP, 32); // len
            enc.ldr_imm(arm64::X1, arm64::FP, 24);
            enc.mul_rrr(arm64::X0, arm64::X0, arm64::X1); // total
            enc.str_imm(arm64::X0, arm64::FP, 40);
            enc.add_rri(arm64::X0, arm64::X0, 1);
            enc.bl(lblMalloc);
            enc.str_imm(arm64::X0, arm64::FP, 48); // out

            enc.movz(arm64::X3, 0, 0);
            enc.str_imm(arm64::X3, arm64::FP, 56);
            size_t loopLbl = enc.createLabel();
            size_t doneLbl = enc.createLabel();
            enc.bindLabel(loopLbl);
            enc.ldr_imm(arm64::X3, arm64::FP, 56);
            enc.ldr_imm(arm64::X2, arm64::FP, 40);
            enc.cmp_rr(arm64::X3, arm64::X2);
            enc.b_cond(arm64::GE, doneLbl);

            enc.ldr_imm(arm64::X1, arm64::FP, 32);
            enc.udiv_rrr(arm64::X4, arm64::X3, arm64::X1);
            enc.mul_rrr(arm64::X5, arm64::X4, arm64::X1);
            enc.sub_rrr(arm64::X5, arm64::X3, arm64::X5);

            enc.ldr_imm(arm64::X0, arm64::FP, 16);
            enc.add_rrr(arm64::X0, arm64::X0, arm64::X5);
            enc.ldrb_imm(arm64::X6, arm64::X0, 0);

            enc.ldr_imm(arm64::X5, arm64::FP, 48);
            enc.add_rrr(arm64::X5, arm64::X5, arm64::X3);
            enc.strb_imm(arm64::X6, arm64::X5, 0);

            enc.add_rri(arm64::X3, arm64::X3, 1);
            enc.str_imm(arm64::X3, arm64::FP, 56);
            enc.b(loopLbl);

            enc.bindLabel(doneLbl);
            enc.ldr_imm(arm64::X5, arm64::FP, 48);
            enc.ldr_imm(arm64::X3, arm64::FP, 40);
            enc.add_rrr(arm64::X5, arm64::X5, arm64::X3);
            enc.movz(arm64::X4, 0, 0);
            enc.strb_imm(arm64::X4, arm64::X5, 0);
            enc.ldr_imm(arm64::X0, arm64::FP, 48);

            enc.ldp_post(arm64::FP, arm64::LR, arm64::SP, 96);
            enc.ret();
        }

        // --- pow(x in D0, y in D1) -> D0 (msvcrt pow) ---
        enc.bindLabel(lblPow);
        enc.stp_pre(arm64::FP, arm64::LR, arm64::SP, -16);
        enc.mov_rr(arm64::FP, arm64::SP);
        uint32_t iatPow = pe.getFuncIatOffsetInRdata("pow");
        enc.adr_data(arm64::X16, iatPow);
        enc.ldr_imm(arm64::X16, arm64::X16, 0);
        enc.blr(arm64::X16);
        enc.mov_rr(arm64::SP, arm64::FP);
        enc.ldp_post(arm64::FP, arm64::LR, arm64::SP, 16);
        enc.ret();

        // ==========================================
        // 15. Console input (key.input) for Windows ARM64
        //
        // The shared ARM64 codegen calls lblInputLine(prompt, mode),
        // lblAtoi(str) and lblAtoD(str) for key.input. msvcrt provides all
        // three behaviours, so the helpers are thin wrappers:
        //   - scanf(" %1023[^\n]", buf) reads exactly one line and leaves the
        //     rest of stdin untouched (same format the x86-64 PE backend uses),
        //   - _atoi64 / atof parse the line into int / double.
        // ==========================================
        {
            uint32_t iatAtoi64 = pe.getFuncIatOffsetInRdata("_atoi64");
            enc.bindLabel(lblAtoi);
            enc.adr_data(arm64::X16, iatAtoi64);
            enc.ldr_imm(arm64::X16, arm64::X16, 0);
            enc.br(arm64::X16);

            uint32_t iatAtof = pe.getFuncIatOffsetInRdata("atof");
            enc.bindLabel(lblAtoD);
            enc.adr_data(arm64::X16, iatAtof);
            enc.ldr_imm(arm64::X16, arm64::X16, 0);
            enc.br(arm64::X16);

            uint32_t iatScanf  = pe.getFuncIatOffsetInRdata("scanf");
            uint32_t iatFflush = pe.getFuncIatOffsetInRdata("fflush");

            size_t inNoPrompt = enc.createLabel();
            size_t inCaseDone = enc.createLabel();
            size_t inLowerLoop = enc.createLabel();
            size_t inLowerNext = enc.createLabel();
            size_t inUpperLoop = enc.createLabel();
            size_t inUpperNext = enc.createLabel();

            enc.bindLabel(lblInputLine);
            enc.stp_pre(arm64::FP, arm64::LR, arm64::SP, -64);
            enc.mov_rr(arm64::FP, arm64::SP);
            enc.str_imm(arm64::X0, arm64::FP, 16); // prompt
            enc.str_imm(arm64::X1, arm64::FP, 24); // mode: 0 none, 1 lower, 2 upper

            // Print the prompt when it is non-null and non-empty.
            enc.cbz(arm64::X0, inNoPrompt);
            enc.ldrb_imm(arm64::X2, arm64::X0, 0);
            enc.cbz(arm64::X2, inNoPrompt);
            enc.mov_rr(arm64::X1, arm64::X0);
            enc.adr_data(arm64::X0, offFmtStr);
            enc.adr_data(arm64::X16, iatPrintf);
            enc.ldr_imm(arm64::X16, arm64::X16, 0);
            enc.blr(arm64::X16);
            enc.movz(arm64::X0, 0, 0); // fflush(NULL)
            enc.adr_data(arm64::X16, iatFflush);
            enc.ldr_imm(arm64::X16, arm64::X16, 0);
            enc.blr(arm64::X16);

            enc.bindLabel(inNoPrompt);
            enc.mov_ri64(arm64::X0, 1024);
            enc.bl(lblMalloc);
            enc.str_imm(arm64::X0, arm64::FP, 32); // buf

            // Read one line: the leading space makes scanf skip the newline
            // left by the previous read.
            enc.adr_data(arm64::X0, offFmtScanLine);
            enc.ldr_imm(arm64::X1, arm64::FP, 32);
            enc.adr_data(arm64::X16, iatScanf);
            enc.ldr_imm(arm64::X16, arm64::X16, 0);
            enc.blr(arm64::X16);

            // Optional ASCII case conversion (key.input.lower / .upper).
            enc.movz(arm64::X5, 0, 0);
            enc.str_imm(arm64::X5, arm64::FP, 40); // i
            enc.ldr_imm(arm64::X1, arm64::FP, 24);
            enc.cmp_ri(arm64::X1, 1);
            enc.b_cond(arm64::EQ, inLowerLoop);
            enc.cmp_ri(arm64::X1, 2);
            enc.b_cond(arm64::EQ, inUpperLoop);
            enc.b(inCaseDone);

            enc.bindLabel(inLowerLoop);
            enc.ldr_imm(arm64::X5, arm64::FP, 40);
            enc.ldr_imm(arm64::X3, arm64::FP, 32);
            enc.add_rrr(arm64::X3, arm64::X3, arm64::X5);
            enc.ldrb_imm(arm64::X4, arm64::X3, 0);
            enc.cbz(arm64::X4, inCaseDone);
            enc.cmp_ri(arm64::X4, 'A');
            enc.b_cond(arm64::LT, inLowerNext);
            enc.cmp_ri(arm64::X4, 'Z');
            enc.b_cond(arm64::GT, inLowerNext);
            enc.add_rri(arm64::X4, arm64::X4, 32);
            enc.strb_imm(arm64::X4, arm64::X3, 0);
            enc.bindLabel(inLowerNext);
            enc.add_rri(arm64::X5, arm64::X5, 1);
            enc.str_imm(arm64::X5, arm64::FP, 40);
            enc.b(inLowerLoop);

            enc.bindLabel(inUpperLoop);
            enc.ldr_imm(arm64::X5, arm64::FP, 40);
            enc.ldr_imm(arm64::X3, arm64::FP, 32);
            enc.add_rrr(arm64::X3, arm64::X3, arm64::X5);
            enc.ldrb_imm(arm64::X4, arm64::X3, 0);
            enc.cbz(arm64::X4, inCaseDone);
            enc.cmp_ri(arm64::X4, 'a');
            enc.b_cond(arm64::LT, inUpperNext);
            enc.cmp_ri(arm64::X4, 'z');
            enc.b_cond(arm64::GT, inUpperNext);
            enc.sub_rri(arm64::X4, arm64::X4, 32);
            enc.strb_imm(arm64::X4, arm64::X3, 0);
            enc.bindLabel(inUpperNext);
            enc.add_rri(arm64::X5, arm64::X5, 1);
            enc.str_imm(arm64::X5, arm64::FP, 40);
            enc.b(inUpperLoop);

            enc.bindLabel(inCaseDone);
            enc.ldr_imm(arm64::X0, arm64::FP, 32);
            enc.mov_rr(arm64::SP, arm64::FP);
            enc.ldp_post(arm64::FP, arm64::LR, arm64::SP, 64);
            enc.ret();
        }
        enc.bindLabel(lblMemCpy);
        enc.adr_data(arm64::X16, pe.getFuncIatOffsetInRdata("memcpy"));
        enc.ldr_imm(arm64::X16, arm64::X16, 0);
        enc.br(arm64::X16);

        arm64::emitCaseConvert(enc, lblStrLower, false, lblStrLen, lblMalloc, offEmptyStr);
        arm64::emitCaseConvert(enc, lblStrUpper, true, lblStrLen, lblMalloc, offEmptyStr);
        arm64::emitStrStrip(enc, lblStrStrip, lblStrLen, lblMalloc, lblMemCpy, offEmptyStr);
        arm64::emitStrTitle(enc, lblStrTitle, lblStrLower, offEmptyStr);
        arm64::emitStrFind(enc, lblStrFind, lblStrLen);
        arm64::emitStrStartsWith(enc, lblStrStartsWith, lblStrLen);
        arm64::emitStrEndsWith(enc, lblStrEndsWith, lblStrLen);
        arm64::emitStrCount(enc, lblStrCount, lblStrFind, lblStrLen);
        arm64::emitStrReplace(enc, lblStrReplace, lblStrLen, lblStrFind, lblMalloc, lblMemCpy, offEmptyStr);
        arm64::emitStrSplit(enc, lblStrSplit, lblStrLen, lblStrFind, lblMalloc, lblMemCpy, offEmptyStr);
        arm64::emitStrJoin(enc, lblStrJoin, lblStrLen, lblMalloc, lblMemCpy, offEmptyStr);
        // Seed msvcrt's generator once per program, as on Windows x86-64.
        enc.bindLabel(lblSeedRand);
        enc.stp_pre(arm64::FP, arm64::LR, arm64::SP, -16);
        enc.mov_rr(arm64::FP, arm64::SP);
        enc.movz(arm64::X0, 0, 0);
        enc.adr_data(arm64::X16, pe.getFuncIatOffsetInRdata("time"));
        enc.ldr_imm(arm64::X16, arm64::X16, 0);
        enc.blr(arm64::X16);
        enc.adr_data(arm64::X16, pe.getFuncIatOffsetInRdata("srand"));
        enc.ldr_imm(arm64::X16, arm64::X16, 0);
        enc.blr(arm64::X16);
        enc.ldp_post(arm64::FP, arm64::LR, arm64::SP, 16);
        enc.ret();

        enc.bindLabel(lblRand);
        enc.adr_data(arm64::X16, pe.getFuncIatOffsetInRdata("rand"));
        enc.ldr_imm(arm64::X16, arm64::X16, 0);
        enc.br(arm64::X16);

        arm64::emitStrCmpEq(enc, lblStrCmpEq);
        arm64::emitListIn(enc, lblListIn, lblStrCmpEq);
        arm64::emitRandChoice(enc, lblRandChoice, lblRand, offEmptyStr);
        arm64::emitWeightedChoice(enc, lblWeightedChoice, lblRand, lblRandChoice,
                                  lblPrintStr, lblExit, offWeightMismatch);
        arm64::emitDictFind(enc, lblDictFind, lblStrCmpEq);
        emitFileHelpers();
        emitTimeFormat();
        emitListDir();
        emitSetEnv();
        emitNamedModule();
        emitMessageFlag(lblMessageIcon, messageIcons);
        emitMessageFlag(lblMessageButtons, messageButtons);
        for (const auto& entry : windowsImportLabels) {
            enc.bindLabel(entry.second);
            tailCallImport(entry.first.c_str());
        }
    }

private:
    void loadImportAddress(const char* name) {
        enc.adr_data(arm64::X16, pe.getFuncIatOffsetInRdata(name));
        enc.ldr_imm(arm64::X16, arm64::X16, 0);
    }

    void callCrt(const char* name) {
        loadImportAddress(name);
        enc.blr(arm64::X16);
    }

    void tailCallImport(const char* name) {
        loadImportAddress(name);
        enc.br(arm64::X16);
    }

    void emitListDir() {
        using namespace arm64;
        enc.bindLabel(lblListDir);
        // 336 bytes for WIN32_FIND_DATAA at FP+64, with file names at byte 44.
        enc.stp_pre(FP, LR, SP, -400);
        enc.mov_rr(FP, SP);
        enc.adr_data(X1, offDirWildcard);
        enc.bl(lblStrConcat);
        enc.str_imm(X0, FP, 16);
        enc.add_rri(X1, FP, 64);
        callCrt("FindFirstFileA");
        enc.str_imm(X0, FP, 24); // search handle
        enc.ldr_imm(X0, FP, 16);
        callCrt("free");
        enc.movz(X0, 16, 0);
        enc.bl(lblMalloc);
        enc.str_imm(X0, FP, 32); // result list
        enc.movz(X9, 0, 0);
        enc.str_imm(X9, FP, 40); // count
        enc.movz(X9, 1, 0);
        enc.str_imm(X9, FP, 48); // capacity
        auto loop = enc.createLabel(), next = enc.createLabel();
        auto store = enc.createLabel(), done = enc.createLabel(), oom = enc.createLabel();
        enc.ldr_imm(X0, FP, 24);
        enc.movn(X9, 0, 0);
        enc.cmp_rr(X0, X9);
        enc.b_cond(EQ, done);

        enc.bindLabel(loop);
        for (auto dot : {offDot, offDotDot}) {
            enc.add_rri(X0, FP, 108);
            enc.adr_data(X1, dot);
            enc.bl(lblStrCmpEq);
            enc.cbnz(X0, next);
        }
        enc.add_rri(X0, FP, 108);
        callCrt("_strdup");
        enc.cbz(X0, oom);
        enc.str_imm(X0, FP, 56);
        enc.ldr_imm(X9, FP, 40);
        enc.ldr_imm(X1, FP, 48);
        enc.cmp_rr(X9, X1);
        enc.b_cond(LT, store);
        enc.add_rrr(X1, X1, X1);
        enc.str_imm(X1, FP, 48);
        enc.add_rri(X1, X1, 1);
        enc.lsl_ri(X1, X1, 3);
        enc.ldr_imm(X0, FP, 32);
        callCrt("realloc");
        enc.cbz(X0, oom);
        enc.str_imm(X0, FP, 32);
        enc.bindLabel(store);
        enc.ldr_imm(X9, FP, 40);
        enc.ldr_imm(X10, FP, 32);
        enc.ldr_imm(X11, FP, 56);
        enc.lsl_ri(X12, X9, 3);
        enc.add_rrr(X10, X10, X12);
        enc.str_imm(X11, X10, 8);
        enc.add_rri(X9, X9, 1);
        enc.str_imm(X9, FP, 40);

        enc.bindLabel(next);
        enc.ldr_imm(X0, FP, 24);
        enc.add_rri(X1, FP, 64);
        callCrt("FindNextFileA");
        enc.cbnz(X0, loop);
        enc.ldr_imm(X0, FP, 24);
        callCrt("FindClose");

        enc.bindLabel(done);
        enc.ldr_imm(X0, FP, 32);
        enc.ldr_imm(X9, FP, 40);
        enc.str_imm(X9, X0, 0);
        enc.ldp_post(FP, LR, SP, 400);
        enc.ret();

        enc.bindLabel(oom);
        enc.movz(X0, 1, 0);
        enc.bl(lblExit);
    }

    void emitSetEnv() {
        using namespace arm64;
        enc.bindLabel(lblSetEnv);
        enc.stp_pre(FP, LR, SP, -48);
        enc.mov_rr(FP, SP);
        enc.str_imm(X0, FP, 16); // key
        enc.str_imm(X1, FP, 24); // value
        auto invalid = enc.createLabel(), done = enc.createLabel();
        enc.ldrb_imm(X9, X0, 0);
        enc.cbz(X9, invalid);
        enc.adr_data(X1, offEquals);
        enc.bl(lblStrFind);
        enc.cmp_ri(X0, 0);
        enc.b_cond(GE, invalid);

        enc.ldr_imm(X0, FP, 16);
        enc.adr_data(X1, offEquals);
        enc.bl(lblStrConcat);
        enc.str_imm(X0, FP, 32);
        enc.ldr_imm(X1, FP, 24);
        enc.bl(lblStrConcat);
        enc.str_imm(X0, FP, 40);
        callCrt("_putenv");
        enc.cmp_ri(X0, 0);
        enc.cset(X9, EQ);
        enc.str_imm(X9, FP, 16);
        enc.ldr_imm(X0, FP, 40);
        callCrt("free");
        enc.ldr_imm(X0, FP, 32);
        callCrt("free");
        enc.ldr_imm(X0, FP, 16);
        enc.b(done);
        enc.bindLabel(invalid);
        enc.movz(X0, 0, 0);
        enc.bindLabel(done);
        enc.ldp_post(FP, LR, SP, 48);
        enc.ret();
    }

    // Retain direct-call modules for the lifetime of the process. A function
    // may return a pointer into the DLL even when its declared result is <int64>.
    void emitNamedModule() {
        using namespace arm64;
        enc.bindLabel(lblNamedModule);
        enc.stp_pre(FP, LR, SP, -48);
        enc.mov_rr(FP, SP);
        enc.str_imm(X0, FP, 16);
        auto loop = enc.createLabel(), load = enc.createLabel();
        auto found = enc.createLabel(), done = enc.createLabel();
        enc.adr_data(X9, offModuleCache);
        enc.ldr_imm(X9, X9, 0);
        enc.bindLabel(loop);
        enc.cbz(X9, load);
        enc.str_imm(X9, FP, 24);
        enc.ldr_imm(X0, X9, 8);
        enc.ldr_imm(X1, FP, 16);
        enc.bl(lblStrCmpEq);
        enc.ldr_imm(X9, FP, 24);
        enc.cbnz(X0, found);
        enc.ldr_imm(X9, X9, 0);
        enc.b(loop);

        enc.bindLabel(load);
        enc.ldr_imm(X0, FP, 16);
        callCrt("LoadLibraryA");
        enc.cbz(X0, done);
        enc.str_imm(X0, FP, 32);
        enc.ldr_imm(X0, FP, 16);
        callCrt("_strdup");
        enc.str_imm(X0, FP, 16);
        enc.movz(X0, 24, 0);
        enc.bl(lblMalloc);
        enc.adr_data(X9, offModuleCache);
        enc.ldr_imm(X10, X9, 0);
        enc.str_imm(X10, X0, 0);
        enc.ldr_imm(X10, FP, 16);
        enc.str_imm(X10, X0, 8);
        enc.ldr_imm(X10, FP, 32);
        enc.str_imm(X10, X0, 16);
        enc.str_imm(X0, X9, 0);
        enc.mov_rr(X0, X10);
        enc.b(done);
        enc.bindLabel(found);
        enc.ldr_imm(X0, X9, 16);
        enc.bindLabel(done);
        enc.ldp_post(FP, LR, SP, 48);
        enc.ret();
    }

    void emitMessageFlag(size_t entry, const std::vector<std::pair<uint32_t, int>>& flags) {
        using namespace arm64;
        enc.bindLabel(entry);
        enc.stp_pre(FP, LR, SP, -32);
        enc.mov_rr(FP, SP);
        enc.bl(lblStrLower);
        enc.str_imm(X0, FP, 16);
        auto done = enc.createLabel();
        for (const auto& flag : flags) {
            enc.ldr_imm(X0, FP, 16);
            enc.adr_data(X1, flag.first);
            enc.bl(lblStrCmpEq);
            auto next = enc.createLabel();
            enc.cbz(X0, next);
            enc.movz(X0, flag.second, 0);
            enc.b(done);
            enc.bindLabel(next);
        }
        enc.movz(X0, 0, 0);
        enc.bindLabel(done);
        enc.str_imm(X0, FP, 24);
        enc.ldr_imm(X0, FP, 16);
        callCrt("free");
        enc.ldr_imm(X0, FP, 24);
        enc.ldp_post(FP, LR, SP, 32);
        enc.ret();
    }

    // X0 holds the caller's format and X1 holds the default format.
    void emitTimeFormat() {
        using namespace arm64;
        enc.bindLabel(lblTimeFormat);
        enc.stp_pre(FP, LR, SP, -80);
        enc.mov_rr(FP, SP);

        auto defaultFormat = enc.createLabel(), formatReady = enc.createLabel();
        enc.cbz(X0, defaultFormat);
        enc.ldrb_imm(X9, X0, 0);
        enc.cbz(X9, defaultFormat);
        enc.str_imm(X0, FP, 16);
        for (size_t i = 0; i < 3; ++i) {
            enc.ldr_imm(X0, FP, 16);
            enc.adr_data(X1, offTimeAliases[i]);
            enc.bl(lblStrCmpEq);
            auto next = enc.createLabel();
            enc.cbz(X0, next);
            enc.adr_data(X0, offTimePatterns[i]);
            enc.b(formatReady);
            enc.bindLabel(next);
        }
        enc.ldr_imm(X0, FP, 16);
        enc.b(formatReady);
        enc.bindLabel(defaultFormat);
        enc.mov_rr(X0, X1);
        enc.bindLabel(formatReady);
        enc.str_imm(X0, FP, 16);

        enc.add_rri(X0, FP, 32);
        callCrt("GetLocalTime");
        bool ownsFormat = false;
        for (const auto& token : timeTokens) {
            enc.add_rri(X0, FP, 48);
            enc.adr_data(X1, token.format);
            enc.ldrb_imm(X2, FP, 32 + token.field);
            enc.ldrb_imm(X9, FP, 33 + token.field);
            enc.lsl_ri(X9, X9, 8);
            enc.orr_rrr(X2, X2, X9);
            callCrt("sprintf");
            enc.ldr_imm(X0, FP, 16);
            enc.adr_data(X1, token.token);
            enc.add_rri(X2, FP, 48);
            enc.bl(lblStrReplace);
            if (ownsFormat) {
                enc.str_imm(X0, FP, 24);
                enc.ldr_imm(X0, FP, 16);
                callCrt("free");
                enc.ldr_imm(X0, FP, 24);
            }
            enc.str_imm(X0, FP, 16);
            ownsFormat = true;
        }
        enc.ldp_post(FP, LR, SP, 80);
        enc.ret();
    }

    // Convert the bits in X0 using the Windows ARM64 variadic calling convention.
    void emitNumberToString(size_t label, uint32_t format) {
        enc.bindLabel(label);
        enc.stp_pre(arm64::FP, arm64::LR, arm64::SP, -32);
        enc.mov_rr(arm64::FP, arm64::SP);
        enc.str_imm(arm64::X0, arm64::FP, 16);
        enc.movz(arm64::X0, 64, 0);
        enc.bl(lblMalloc);
        enc.str_imm(arm64::X0, arm64::FP, 24);
        enc.adr_data(arm64::X1, format);
        enc.ldr_imm(arm64::X2, arm64::FP, 16);
        callCrt("sprintf");
        enc.ldr_imm(arm64::X0, arm64::FP, 24);
        enc.ldp_post(arm64::FP, arm64::LR, arm64::SP, 32);
        enc.ret();
    }

    void emitFileWriteNumber(size_t label, size_t formatter) {
        enc.bindLabel(label);
        enc.stp_pre(arm64::FP, arm64::LR, arm64::SP, -32);
        enc.mov_rr(arm64::FP, arm64::SP);
        enc.str_imm(arm64::X0, arm64::FP, 16);
        enc.mov_rr(arm64::X0, arm64::X1);
        enc.bl(formatter);
        enc.str_imm(arm64::X0, arm64::FP, 24);
        enc.bl(lblStrLen);
        enc.mov_rr(arm64::X2, arm64::X0);
        enc.ldr_imm(arm64::X1, arm64::FP, 24);
        enc.ldr_imm(arm64::X0, arm64::FP, 16);
        enc.bl(lblFileWrite);
        enc.ldr_imm(arm64::X0, arm64::FP, 24);
        callCrt("free");
        enc.ldp_post(arm64::FP, arm64::LR, arm64::SP, 32);
        enc.ret();
    }

    void emitRewind() {
        // fseek(FILE*, 0, SEEK_SET)
        enc.movz(arm64::X1, 0, 0);
        enc.movz(arm64::X2, 0, 0);
        callCrt("fseek");
    }

    void emitFileHelpers() {
        enc.bindLabel(lblFileOpen);
        tailCallImport("fopen");
        enc.bindLabel(lblFileClose);
        tailCallImport("fclose");

        // write(file, buffer, length) -> fwrite(buffer, 1, length, file)
        enc.bindLabel(lblFileWrite);
        enc.mov_rr(arm64::X3, arm64::X0);
        enc.mov_rr(arm64::X0, arm64::X1);
        enc.movz(arm64::X1, 1, 0);
        tailCallImport("fwrite");

        emitNumberToString(lblIntToStr, offFmtIntString);
        emitNumberToString(lblFileFmtDouble, offFmtFloat);
        emitFileWriteNumber(lblFileWriteInt, lblIntToStr);
        emitFileWriteNumber(lblFileWriteDouble, lblFileFmtDouble);

        enc.bindLabel(lblFileReadAll);
        enc.stp_pre(arm64::FP, arm64::LR, arm64::SP, -32);
        enc.mov_rr(arm64::FP, arm64::SP);
        enc.str_imm(arm64::X0, arm64::FP, 16);
        emitRewind();
        enc.ldr_imm(arm64::X0, arm64::FP, 16);
        enc.movz(arm64::X1, 0, 0);
        enc.bl(lblFileReadBuffer);
        enc.ldp_post(arm64::FP, arm64::LR, arm64::SP, 32);
        enc.ret();

        enc.bindLabel(lblFileReadLine);
        enc.movz(arm64::X1, 1, 0);
        enc.b(lblFileReadBuffer);

        emitFileReadBuffer();
        emitFileReadLines();
    }

    // Read until EOF or (in line mode) LF. X1 returns 1 when a line was read,
    // including a blank line, and 0 at EOF before any characters.
    void emitFileReadBuffer() {
        enc.bindLabel(lblFileReadBuffer);
        enc.stp_pre(arm64::FP, arm64::LR, arm64::SP, -80);
        enc.mov_rr(arm64::FP, arm64::SP);
        enc.str_imm(arm64::X0, arm64::FP, 16); // file
        enc.str_imm(arm64::X1, arm64::FP, 24); // line mode
        enc.movz(arm64::X0, 128, 0);
        enc.str_imm(arm64::X0, arm64::FP, 40); // capacity
        enc.bl(lblMalloc);
        enc.str_imm(arm64::X0, arm64::FP, 32); // buffer
        enc.movz(arm64::X0, 0, 0);
        enc.str_imm(arm64::X0, arm64::FP, 48); // length
        enc.str_imm(arm64::X0, arm64::FP, 56); // consumed input

        auto loop = enc.createLabel(), append = enc.createLabel();
        auto store = enc.createLabel(), done = enc.createLabel(), failed = enc.createLabel();
        enc.bindLabel(loop);
        enc.ldr_imm(arm64::X0, arm64::FP, 16);
        callCrt("fgetc");
        // fgetc returns a 32-bit int; EOF is -1 in W0.
        enc.mov_ri64(arm64::X9, 0xffffffff);
        enc.and_rrr(arm64::X0, arm64::X0, arm64::X9);
        enc.cmp_rr(arm64::X0, arm64::X9);
        enc.b_cond(arm64::EQ, done);
        enc.movz(arm64::X9, 1, 0);
        enc.str_imm(arm64::X9, arm64::FP, 56);
        enc.ldr_imm(arm64::X9, arm64::FP, 24);
        enc.cbz(arm64::X9, append);
        enc.cmp_ri(arm64::X0, '\n');
        enc.b_cond(arm64::EQ, done);
        enc.cmp_ri(arm64::X0, '\r');
        enc.b_cond(arm64::EQ, loop);
        enc.bindLabel(append);
        enc.str_imm(arm64::X0, arm64::FP, 64);
        enc.ldr_imm(arm64::X9, arm64::FP, 48);
        enc.add_rri(arm64::X9, arm64::X9, 1);
        enc.ldr_imm(arm64::X1, arm64::FP, 40);
        enc.cmp_rr(arm64::X9, arm64::X1);
        enc.b_cond(arm64::LT, store);
        enc.add_rrr(arm64::X1, arm64::X1, arm64::X1);
        enc.str_imm(arm64::X1, arm64::FP, 40);
        enc.ldr_imm(arm64::X0, arm64::FP, 32);
        callCrt("realloc");
        enc.cbz(arm64::X0, failed);
        enc.str_imm(arm64::X0, arm64::FP, 32);
        enc.bindLabel(store);
        enc.ldr_imm(arm64::X9, arm64::FP, 32);
        enc.ldr_imm(arm64::X10, arm64::FP, 48);
        enc.add_rrr(arm64::X9, arm64::X9, arm64::X10);
        enc.ldr_imm(arm64::X11, arm64::FP, 64);
        enc.strb_imm(arm64::X11, arm64::X9, 0);
        enc.add_rri(arm64::X10, arm64::X10, 1);
        enc.str_imm(arm64::X10, arm64::FP, 48);
        enc.b(loop);

        enc.bindLabel(done);
        enc.ldr_imm(arm64::X0, arm64::FP, 32);
        enc.ldr_imm(arm64::X9, arm64::FP, 48);
        enc.add_rrr(arm64::X9, arm64::X0, arm64::X9);
        enc.movz(arm64::X10, 0, 0);
        enc.strb_imm(arm64::X10, arm64::X9, 0);
        enc.ldr_imm(arm64::X1, arm64::FP, 56);
        enc.ldp_post(arm64::FP, arm64::LR, arm64::SP, 80);
        enc.ret();

        enc.bindLabel(failed);
        enc.movz(arm64::X0, 1, 0);
        enc.bl(lblExit);
    }

    void emitFileReadLines() {
        enc.bindLabel(lblFileReadLines);
        enc.stp_pre(arm64::FP, arm64::LR, arm64::SP, -64);
        enc.mov_rr(arm64::FP, arm64::SP);
        enc.str_imm(arm64::X0, arm64::FP, 16); // file
        emitRewind();
        enc.movz(arm64::X0, 16, 0); // count word and one element
        enc.bl(lblMalloc);
        enc.str_imm(arm64::X0, arm64::FP, 24); // list
        enc.movz(arm64::X9, 0, 0);
        enc.str_imm(arm64::X9, arm64::FP, 32); // count
        enc.movz(arm64::X9, 1, 0);
        enc.str_imm(arm64::X9, arm64::FP, 40); // capacity

        auto loop = enc.createLabel(), store = enc.createLabel();
        auto done = enc.createLabel(), failed = enc.createLabel();
        enc.bindLabel(loop);
        enc.ldr_imm(arm64::X0, arm64::FP, 16);
        enc.bl(lblFileReadLine);
        enc.cbz(arm64::X1, done);
        enc.str_imm(arm64::X0, arm64::FP, 48);
        enc.ldr_imm(arm64::X9, arm64::FP, 32);
        enc.ldr_imm(arm64::X1, arm64::FP, 40);
        enc.cmp_rr(arm64::X9, arm64::X1);
        enc.b_cond(arm64::LT, store);
        enc.add_rrr(arm64::X1, arm64::X1, arm64::X1);
        enc.str_imm(arm64::X1, arm64::FP, 40);
        enc.add_rri(arm64::X1, arm64::X1, 1);
        enc.lsl_ri(arm64::X1, arm64::X1, 3);
        enc.ldr_imm(arm64::X0, arm64::FP, 24);
        callCrt("realloc");
        enc.cbz(arm64::X0, failed);
        enc.str_imm(arm64::X0, arm64::FP, 24);
        enc.bindLabel(store);
        enc.ldr_imm(arm64::X9, arm64::FP, 32);
        enc.ldr_imm(arm64::X10, arm64::FP, 24);
        enc.ldr_imm(arm64::X11, arm64::FP, 48);
        enc.lsl_ri(arm64::X12, arm64::X9, 3);
        enc.add_rrr(arm64::X10, arm64::X10, arm64::X12);
        enc.str_imm(arm64::X11, arm64::X10, 8);
        enc.add_rri(arm64::X9, arm64::X9, 1);
        enc.str_imm(arm64::X9, arm64::FP, 32);
        enc.b(loop);
        enc.bindLabel(done);
        callCrt("free");
        enc.ldr_imm(arm64::X0, arm64::FP, 24);
        enc.ldr_imm(arm64::X9, arm64::FP, 32);
        enc.str_imm(arm64::X9, arm64::X0, 0);
        enc.ldp_post(arm64::FP, arm64::LR, arm64::SP, 64);
        enc.ret();

        enc.bindLabel(failed);
        enc.movz(arm64::X0, 1, 0);
        enc.bl(lblExit);
    }
};

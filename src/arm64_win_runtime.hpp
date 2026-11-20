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
    size_t lblSeedRand = 0;
    size_t lblRand = 0;
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
    size_t lblFileReadAll = 0;
    size_t lblFileReadLines = 0;
    uint32_t offFileReadErr = 0;
    uint32_t offStrQuoteNL = 0;

    uint32_t offEmptyStr = 0;
    uint32_t offStrSh = 0;
    uint32_t offStrDashC = 0;
    uint32_t offStrProcEnviron = 0;
    uint32_t offDoubleTen = 0;
    uint32_t offDoubleMaxRand = 0;

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
        lblSeedRand      = enc.createLabel();
        lblRand          = enc.createLabel();
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
        lblFileReadAll   = enc.createLabel();
        lblFileReadLines = enc.createLabel();
    }

    void init() {
        if (initialized) return;
        initialized = true;

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
        offDoubleMaxRand = pe.addDoubleLiteralOffset(32767.0);
        offEmptyStr = pe.addStringLiteralOffset("");
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
        arm64::emitDictFind(enc, lblDictFind, lblStrCmpEq);
    }
};

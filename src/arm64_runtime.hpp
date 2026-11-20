#pragma once

#include "elf_arm64_emitter.hpp"
#include "arm64_encoder.hpp"
#include "arm64_string_helpers.hpp"
#include <string>
#include <vector>
#include <map>
#include <cstdint>

class Arm64NativeRuntime {
public:
    arm64::Encoder& enc;
    ElfArm64Emitter& elf;

    uint32_t offStrNL     = 0;  // "\n"
    uint32_t offStrSpace  = 0;  // " "
    uint32_t offStrTrue   = 0;  // "true"
    uint32_t offStrFalse  = 0;  // "false"
    uint32_t offSuccessMsg= 0;  // "\n[Thava Native] Build and run successful!\n"
    uint32_t offStrListOpen  = 0; // "["
    uint32_t offStrListComma = 0; // ", "
    uint32_t offStrListClose = 0; // "]\n"
    uint32_t offDoubleMillion = 0; // 1000000.0
    uint32_t offDoubleHalf    = 0; // 0.5
    uint32_t offDoubleHuge    = 0; // 1e13 (formatting cutoff)
    uint32_t offDouble1e18    = 0; // 1e18 (huge-magnitude grouping)
    // pow() constants
    uint32_t offSqrt2      = 0;
    uint32_t offDoubleOne  = 0;
    uint32_t offTwoOverLn2 = 0;
    uint32_t offLn2        = 0;
    std::vector<uint32_t> offLogCoef; // 1/3, 1/5, 1/7, 1/9
    std::vector<uint32_t> offExpCoef; // 1/12! .. 1/2!

    // Runtime helper label IDs (pre-allocated in constructor)
    size_t lblExit = 0;
    size_t lblPrintStr = 0;
    size_t lblPrintStrNL = 0;
    size_t lblPrintInt = 0;
    size_t lblPrintIntNL = 0;
    size_t lblPrintNL = 0;
    size_t lblPrintSpace = 0;
    size_t lblPrintBool = 0;
    size_t lblPrintBoolNL = 0;
    size_t lblStrLen = 0;
    size_t lblStrCpy = 0;
    size_t lblMalloc = 0;
    size_t lblPrintDouble = 0;
    size_t lblPrintDoubleNL = 0;
    size_t lblSubstr = 0;
    size_t lblSubstrRange = 0;
    size_t lblStrConcat = 0;
    size_t lblStrMul = 0;
    size_t lblPow = 0;

    // ---- string / input / rand helpers ----
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
    size_t lblRand = 0;
    size_t lblRandChoice = 0;
    size_t lblGetenv = 0;
    size_t lblDictFind = 0;

    // ---- file I/O (handle: [0]=fd [8]=buf [16]=cap [24]=pos [32]=len) ----
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

    uint32_t offEmptyStr = 0;
    uint32_t offStrSh = 0;
    uint32_t offStrDashC = 0;
    uint32_t offStrProcEnviron = 0;
    uint32_t offDoubleTen = 0;
    uint32_t offDoubleMaxRand = 0;
    uint32_t offFileReadErr = 0;
    uint32_t offStrQuoteNL = 0;

    Arm64NativeRuntime(arm64::Encoder& e, ElfArm64Emitter& el)
        : enc(e), elf(el) {
        // Pre-create all helper labels so user code can emit calls to them before or after helpers are emitted
        lblExit        = enc.createLabel();
        lblPrintStr    = enc.createLabel();
        lblPrintStrNL  = enc.createLabel();
        lblPrintInt    = enc.createLabel();
        lblPrintIntNL  = enc.createLabel();
        lblPrintNL     = enc.createLabel();
        lblPrintSpace  = enc.createLabel();
        lblPrintBool   = enc.createLabel();
        lblPrintBoolNL = enc.createLabel();
        lblStrLen      = enc.createLabel();
        lblStrCpy      = enc.createLabel();
        lblMalloc      = enc.createLabel();
        lblPrintDouble = enc.createLabel();
        lblPrintDoubleNL = enc.createLabel();
        lblSubstr = enc.createLabel();
        lblSubstrRange = enc.createLabel();
        lblStrConcat = enc.createLabel();
                lblStrMul        = enc.createLabel();
        lblPow           = enc.createLabel();
        lblMemCpy       = enc.createLabel();
        lblStrLower     = enc.createLabel();
        lblStrUpper     = enc.createLabel();
        lblStrTitle     = enc.createLabel();
        lblStrStrip     = enc.createLabel();
        lblStrFind      = enc.createLabel();
        lblStrStartsWith= enc.createLabel();
        lblStrEndsWith  = enc.createLabel();
        lblStrCount     = enc.createLabel();
        lblStrCmpEq     = enc.createLabel();
        lblStrReplace   = enc.createLabel();
        lblStrSplit     = enc.createLabel();
        lblStrJoin      = enc.createLabel();
        lblListIn       = enc.createLabel();
        lblInputLine    = enc.createLabel();
        lblAtoi         = enc.createLabel();
        lblAtoD         = enc.createLabel();
        lblRand         = enc.createLabel();
        lblRandChoice   = enc.createLabel();
        lblGetenv       = enc.createLabel();
        lblDictFind     = enc.createLabel();
        lblFileMake     = enc.createLabel();
        lblFileOpen     = enc.createLabel();
        lblFileClose    = enc.createLabel();
        lblFileWrite    = enc.createLabel();
        lblIntToBuf     = enc.createLabel();
        lblIntToStr     = enc.createLabel();
        lblFileWriteInt = enc.createLabel();
        lblFileFmtDouble= enc.createLabel();
        lblFileWriteDouble = enc.createLabel();
        lblFileReadLine = enc.createLabel();
        lblFileReadAll  = enc.createLabel();
        lblFileReadLines= enc.createLabel();
    }

    bool initialized = false;

    void init() {
        if (initialized) return;
        initialized = true;
        offStrNL      = elf.addStringLiteralOffset("\n");
        offStrSpace   = elf.addStringLiteralOffset(" ");
        offStrTrue    = elf.addStringLiteralOffset("true");
        offStrFalse   = elf.addStringLiteralOffset("false");
        offSuccessMsg = elf.addStringLiteralOffset("\n[Thava Native] Build and run successful!\n");
        offStrListOpen  = elf.addStringLiteralOffset("[");
        offStrListComma = elf.addStringLiteralOffset(", ");
        offStrListClose = elf.addStringLiteralOffset("]\n");
        offEmptyStr        = elf.addStringLiteralOffset("");
        offStrSh           = elf.addStringLiteralOffset("/bin/sh");
        offStrDashC        = elf.addStringLiteralOffset("-c");
        offStrProcEnviron  = elf.addStringLiteralOffset("/proc/self/environ");
        offDoubleTen       = elf.addDoubleLiteralOffset(10.0);
        offDoubleMaxRand   = elf.addDoubleLiteralOffset(32767.0);
        offFileReadErr     = elf.addStringLiteralOffset("[Runtime Error (RuntimeError)]: Cannot open file '");
        offStrQuoteNL      = elf.addStringLiteralOffset("'\n");
        offDoubleMillion = elf.addDoubleLiteralOffset(1000000.0);
        offDoubleHalf    = elf.addDoubleLiteralOffset(0.5);
        offDoubleHuge    = elf.addDoubleLiteralOffset(1e13);
        offDouble1e18    = elf.addDoubleLiteralOffset(1e18);
        offSqrt2         = elf.addDoubleLiteralOffset(1.4142135623730951);
        offDoubleOne     = elf.addDoubleLiteralOffset(1.0);
        offTwoOverLn2    = elf.addDoubleLiteralOffset(2.0 / 0.6931471805599453);
        offLn2           = elf.addDoubleLiteralOffset(0.6931471805599453);
        offLogCoef.clear();
        offLogCoef.push_back(elf.addDoubleLiteralOffset(1.0 / 3.0));
        offLogCoef.push_back(elf.addDoubleLiteralOffset(1.0 / 5.0));
        offLogCoef.push_back(elf.addDoubleLiteralOffset(1.0 / 7.0));
        offLogCoef.push_back(elf.addDoubleLiteralOffset(1.0 / 9.0));
        offExpCoef.clear();
        {
            double fact = 1.0;
            std::vector<double> inv(13, 0.0);
            for (int k = 1; k <= 12; ++k) { fact *= k; inv[k] = 1.0 / fact; }
            for (int k = 12; k >= 2; --k) offExpCoef.push_back(elf.addDoubleLiteralOffset(inv[k]));
        }
    }

    void emitHelpers() {
        if (!initialized) init();

        emitStrLen();
        emitPrintStr();
        emitPrintNL();
        emitPrintSpace();
        emitPrintStrNL();
        emitPrintInt();
        emitPrintIntNL();
        emitPrintBool();
        emitPrintBoolNL();
        emitExit();
        emitMalloc();
        emitPrintDouble();
        emitPrintDoubleNL();
        emitPow();
        emitSubstr();
        emitSubstrRange();
        emitStrConcat();
        emitStrMul();
        emitMemCpy();
        emitCaseConvert(lblStrLower, false);
        emitCaseConvert(lblStrUpper, true);
        emitStrTitle();
        emitStrStrip();
        emitStrFind();
        emitStrStartsWith();
        emitStrEndsWith();
        emitStrCount();
        emitStrCmpEq();
        emitStrReplace();
        emitStrSplit();
        emitStrJoin();
        emitListIn();
        emitInputLine();
        emitAtoi();
        emitAtoD();
        emitRand();
        emitRandChoice();
        emitGetenv();
        emitDictFind();
        emitFileMake();
        emitFileOpen();
        emitFileClose();
        emitFileWrite();
        emitIntToBuf();
        emitIntToStr();
        emitFileWriteInt();
        emitFileFmtDouble();
        emitFileWriteDouble();
        emitFileReadLine();
        emitFileReadAll();
        emitFileReadLines();
    }

private:
    // ==========================================
    // strlen(const char* s in X0) -> X0
    // ==========================================
    void emitStrLen() {
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
    // print_str(const char* s in X0)
    // ==========================================
    void emitPrintStr() {
        enc.bindLabel(lblPrintStr);
        // Frame: 32 bytes
        enc.stp_pre(arm64::X29, arm64::X30, arm64::SP, -32);
        enc.mov_rr(arm64::X29, arm64::SP);
        enc.str_imm(arm64::X0, arm64::X29, 16); // Save str ptr at [FP + 16]

        // Calculate strlen(X0)
        enc.bl(lblStrLen);
        enc.mov_rr(arm64::X2, arm64::X0);       // X2 = count

        // Restore str ptr
        enc.ldr_imm(arm64::X1, arm64::X29, 16); // X1 = buf
        enc.movz(arm64::X0, 1, 0);              // X0 = 1 (stdout)
        enc.movz(arm64::X8, 64, 0);             // X8 = 64 (sys_write on Linux AArch64)
        enc.svc(0);

        enc.ldp_post(arm64::X29, arm64::X30, arm64::SP, 32);
        enc.ret();
    }

    // ==========================================
    // print_nl()
    // ==========================================
    void emitPrintNL() {
        enc.bindLabel(lblPrintNL);
        enc.stp_pre(arm64::X29, arm64::X30, arm64::SP, -32);
        enc.mov_rr(arm64::X29, arm64::SP);

        enc.adr_data(arm64::X0, offStrNL);
        enc.bl(lblPrintStr);

        enc.ldp_post(arm64::X29, arm64::X30, arm64::SP, 32);
        enc.ret();
    }

    // ==========================================
    // print_space()
    // ==========================================
    void emitPrintSpace() {
        enc.bindLabel(lblPrintSpace);
        enc.stp_pre(arm64::X29, arm64::X30, arm64::SP, -32);
        enc.mov_rr(arm64::X29, arm64::SP);

        enc.adr_data(arm64::X0, offStrSpace);
        enc.bl(lblPrintStr);

        enc.ldp_post(arm64::X29, arm64::X30, arm64::SP, 32);
        enc.ret();
    }

    // ==========================================
    // print_str_nl(const char* s in X0)
    // ==========================================
    void emitPrintStrNL() {
        enc.bindLabel(lblPrintStrNL);
        enc.stp_pre(arm64::X29, arm64::X30, arm64::SP, -32);
        enc.mov_rr(arm64::X29, arm64::SP);

        enc.bl(lblPrintStr);
        enc.bl(lblPrintNL);

        enc.ldp_post(arm64::X29, arm64::X30, arm64::SP, 32);
        enc.ret();
    }

    // ==========================================
    // print_int(int64_t val in X0)
    // ==========================================
    void emitPrintInt() {
        enc.bindLabel(lblPrintInt);
        // Stack frame: 48 bytes (room for 32-byte formatted string buffer)
        enc.stp_pre(arm64::X29, arm64::X30, arm64::SP, -48);
        enc.mov_rr(arm64::X29, arm64::SP);

        size_t notZeroLbl = enc.createLabel();
        size_t isNegLbl   = enc.createLabel();
        size_t loopLbl    = enc.createLabel();
        size_t writeLbl   = enc.createLabel();

        // Check if X0 == 0
        enc.cbnz(arm64::X0, notZeroLbl);

        // Print '0' directly
        enc.movz(arm64::X1, '0', 0);
        enc.strb_imm(arm64::X1, arm64::X29, 32);
        enc.movz(arm64::X1, 0, 0);
        enc.strb_imm(arm64::X1, arm64::X29, 33);
        enc.add_rri(arm64::X0, arm64::X29, 32);
        enc.bl(lblPrintStr);
        enc.b(writeLbl);

        enc.bindLabel(notZeroLbl);
        // Check if negative
        enc.movz(arm64::X19, 0, 0); // is_neg flag in X19
        enc.cmp_ri(arm64::X0, 0);
        enc.b_cond(arm64::GE, isNegLbl);

        // Negative: negate X0 and set is_neg = 1
        enc.movz(arm64::X19, 1, 0);
        enc.sub_rrr(arm64::X0, arm64::XZR, arm64::X0);

        enc.bindLabel(isNegLbl);
        // Buffer pointer starting at end of buffer: [FP + 40]
        enc.add_rri(arm64::X1, arm64::X29, 40);
        // Null terminator
        enc.movz(arm64::X2, 0, 0);
        enc.strb_imm(arm64::X2, arm64::X1, 0);

        // Loop: divide X0 by 10, write remainder digit
        enc.movz(arm64::X3, 10, 0); // divisor = 10

        enc.bindLabel(loopLbl);
        enc.sub_rri(arm64::X1, arm64::X1, 1); // move ptr left

        // X4 = X0 / 10
        enc.udiv_rrr(arm64::X4, arm64::X0, arm64::X3);
        // X5 = X4 * 10
        enc.mul_rrr(arm64::X5, arm64::X4, arm64::X3);
        // Remainder = X0 - X5
        enc.sub_rrr(arm64::X5, arm64::X0, arm64::X5);

        // Char = remainder + '0' (0x30)
        enc.add_rri(arm64::X5, arm64::X5, '0');
        enc.strb_imm(arm64::X5, arm64::X1, 0);

        // X0 = quotient
        enc.mov_rr(arm64::X0, arm64::X4);
        enc.cbnz(arm64::X0, loopLbl);

        // If is_neg was set, prepend '-'
        size_t afterMinusLbl = enc.createLabel();
        enc.cbz(arm64::X19, afterMinusLbl);
        enc.sub_rri(arm64::X1, arm64::X1, 1);
        enc.movz(arm64::X2, '-', 0);
        enc.strb_imm(arm64::X2, arm64::X1, 0);

        enc.bindLabel(afterMinusLbl);
        // Call print_str with X0 = buffer start
        enc.mov_rr(arm64::X0, arm64::X1);
        enc.bl(lblPrintStr);

        enc.bindLabel(writeLbl);
        enc.ldp_post(arm64::X29, arm64::X30, arm64::SP, 48);
        enc.ret();
    }

    // ==========================================
    // print_int_nl(int64_t val in X0)
    // ==========================================
    void emitPrintIntNL() {
        enc.bindLabel(lblPrintIntNL);
        enc.stp_pre(arm64::X29, arm64::X30, arm64::SP, -32);
        enc.mov_rr(arm64::X29, arm64::SP);

        enc.bl(lblPrintInt);
        enc.bl(lblPrintNL);

        enc.ldp_post(arm64::X29, arm64::X30, arm64::SP, 32);
        enc.ret();
    }

    // ==========================================
    // print_bool(int64_t val in X0)
    // ==========================================
    void emitPrintBool() {
        enc.bindLabel(lblPrintBool);
        enc.stp_pre(arm64::X29, arm64::X30, arm64::SP, -32);
        enc.mov_rr(arm64::X29, arm64::SP);

        size_t falseLbl = enc.createLabel();
        size_t doneLbl  = enc.createLabel();

        enc.cbz(arm64::X0, falseLbl);
        enc.adr_data(arm64::X0, offStrTrue);
        enc.bl(lblPrintStr);
        enc.b(doneLbl);

        enc.bindLabel(falseLbl);
        enc.adr_data(arm64::X0, offStrFalse);
        enc.bl(lblPrintStr);

        enc.bindLabel(doneLbl);
        enc.ldp_post(arm64::X29, arm64::X30, arm64::SP, 32);
        enc.ret();
    }

    void emitPrintBoolNL() {
        enc.bindLabel(lblPrintBoolNL);
        enc.stp_pre(arm64::X29, arm64::X30, arm64::SP, -32);
        enc.mov_rr(arm64::X29, arm64::SP);

        enc.bl(lblPrintBool);
        enc.bl(lblPrintNL);

        enc.ldp_post(arm64::X29, arm64::X30, arm64::SP, 32);
        enc.ret();
    }

    // ==========================================
    // exit(int code in X0)
    // ==========================================
    void emitExit() {
        enc.bindLabel(lblExit);
        enc.movz(arm64::X8, 94, 0); // sys_exit_group on Linux AArch64 = 94
        enc.svc(0);
    }

    // ==========================================
    // malloc(size in X0) -> X0 (sys_mmap = syscall 222 on AArch64)
    // map(NULL, size, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0)
    // ==========================================
    void emitMalloc() {
        enc.bindLabel(lblMalloc);
        enc.stp_pre(arm64::X29, arm64::X30, arm64::SP, -32);
        enc.mov_rr(arm64::X29, arm64::SP);

        // Round the length up to a page multiple (4096) for mmap
        enc.add_rri(arm64::X1, arm64::X0, 4095);
        enc.movz(arm64::X2, 0xF000, 0);
        enc.movk(arm64::X2, 0xFFFF, 16);
        enc.movk(arm64::X2, 0xFFFF, 32);
        enc.movk(arm64::X2, 0xFFFF, 48);
        enc.and_rrr(arm64::X1, arm64::X1, arm64::X2);

        // NOTE: mov_rr(reg, XZR) cannot be used here - XZR aliases SP in this encoder
        enc.movz(arm64::X0, 0, 0);           // addr = NULL
        enc.movz(arm64::X2, 3, 0);           // prot = PROT_READ | PROT_WRITE
        enc.movz(arm64::X3, 0x22, 0);        // flags = MAP_PRIVATE | MAP_ANONYMOUS
        enc.movn(arm64::X4, 0, 0);           // fd = -1
        enc.movz(arm64::X5, 0, 0);           // offset = 0
        enc.movz(arm64::X8, 222, 0);         // sys_mmap
        enc.svc(0);

        enc.ldp_post(arm64::X29, arm64::X30, arm64::SP, 32);
        enc.ret();
    }

    // ==========================================
    // print_double(D0 = value): no trailing newline
    // Formats like %g: prints integer part, then up to 6 fractional digits
    // with trailing zeros trimmed (2.0 -> "2", 1.8 -> "1.8").
    // ==========================================
    void emitPrintDouble() {
        enc.bindLabel(lblPrintDouble);
        enc.stp_pre(arm64::X29, arm64::X30, arm64::SP, -80);
        enc.mov_rr(arm64::X29, arm64::SP);
        enc.fstr_imm(arm64::D0, arm64::X29, 16);

        size_t noMinusLbl = enc.createLabel();
        size_t bigLbl     = enc.createLabel();
        size_t doneLbl    = enc.createLabel();
        size_t digitLoop  = enc.createLabel();
        size_t trimLoop   = enc.createLabel();
        size_t trimDone   = enc.createLabel();
        size_t zeroLbl    = enc.createLabel();
        size_t countLoop  = enc.createLabel();
        size_t countDone  = enc.createLabel();
        size_t dOk        = enc.createLabel();
        size_t powLoop    = enc.createLabel();
        size_t powDone    = enc.createLabel();
        size_t noDecLbl   = enc.createLabel();
        size_t scaledOkLbl = enc.createLabel();

        // --- sign ---
        enc.ldr_imm(arm64::X0, arm64::X29, 16);
        enc.movz(arm64::X1, 0, 0);
        enc.movk(arm64::X1, 0x8000, 48);
        enc.and_rrr(arm64::X2, arm64::X0, arm64::X1);
        enc.cbz(arm64::X2, noMinusLbl);

        enc.movz(arm64::X3, '-', 0);
        enc.strb_imm(arm64::X3, arm64::X29, 48);
        enc.movz(arm64::X3, 0, 0);
        enc.strb_imm(arm64::X3, arm64::X29, 49);
        enc.add_rri(arm64::X0, arm64::X29, 48);
        enc.bl(lblPrintStr);

        enc.bindLabel(noMinusLbl);
        // --- absolute value (clear sign bit) ---
        enc.ldr_imm(arm64::X0, arm64::X29, 16);
        enc.movz(arm64::X1, 0xFFFF, 0);
        enc.movk(arm64::X1, 0xFFFF, 16);
        enc.movk(arm64::X1, 0xFFFF, 32);
        enc.movk(arm64::X1, 0x7FFF, 48);
        enc.and_rrr(arm64::X0, arm64::X0, arm64::X1);
        enc.str_imm(arm64::X0, arm64::X29, 16);
        enc.fldr_imm(arm64::D0, arm64::X29, 16);

        // Very large magnitudes: print integer part only
        enc.adr_data(arm64::X16, offDoubleHuge);
        enc.fldr_imm(arm64::D1, arm64::X16, 0);
        enc.fcmp_dd(arm64::D0, arm64::D1);
        enc.b_cond(arm64::GE, bigLbl);

        // scaled6 = trunc(|v| * 1e6 + 0.5)
        enc.adr_data(arm64::X16, offDoubleMillion);
        enc.fldr_imm(arm64::D1, arm64::X16, 0);
        enc.fmul_ddd(arm64::D1, arm64::D0, arm64::D1);
        enc.adr_data(arm64::X16, offDoubleHalf);
        enc.fldr_imm(arm64::D2, arm64::X16, 0);
        enc.fadd_ddd(arm64::D1, arm64::D1, arm64::D2);
        enc.fcvtzs_rd(arm64::X3, arm64::D1);
        enc.cbz(arm64::X3, zeroLbl);

        // digits(scaled6) -> X10, then d = 12 - digits (6 significant digits)
        enc.mov_rr(arm64::X9, arm64::X3);
        enc.mov_ri64(arm64::X10, 1);
        enc.mov_ri64(arm64::X11, 10);
        enc.bindLabel(countLoop);
        enc.cmp_ri(arm64::X9, 10);
        enc.b_cond(arm64::LT, countDone);
        enc.udiv_rrr(arm64::X12, arm64::X9, arm64::X11);
        enc.mov_rr(arm64::X9, arm64::X12);
        enc.add_rri(arm64::X10, arm64::X10, 1);
        enc.b(countLoop);
        enc.bindLabel(countDone);

        enc.mov_ri64(arm64::X12, 12);
        enc.sub_rrr(arm64::X12, arm64::X12, arm64::X10); // d
        enc.cmp_ri(arm64::X12, 0);
        enc.b_cond(arm64::GE, dOk);
        enc.movz(arm64::X12, 0, 0);
        enc.bindLabel(dOk);
        enc.str_imm(arm64::X12, arm64::X29, 32); // d

        // pow10 = 10^d
        enc.mov_ri64(arm64::X11, 1);
        enc.mov_ri64(arm64::X13, 10);
        enc.mov_rr(arm64::X14, arm64::X12);
        enc.cbz(arm64::X14, powDone);
        enc.bindLabel(powLoop);
        enc.mul_rrr(arm64::X11, arm64::X11, arm64::X13);
        enc.sub_rri(arm64::X14, arm64::X14, 1);
        enc.cbnz(arm64::X14, powLoop);
        enc.bindLabel(powDone);
        enc.str_imm(arm64::X11, arm64::X29, 40); // pow10

        // scaled = d == 0 ? scaled6 / 1e6 : trunc(|v| * pow10 + 0.5)
        enc.cbnz(arm64::X12, noDecLbl);
        enc.mov_ri64(arm64::X13, 1000000);
        enc.udiv_rrr(arm64::X5, arm64::X3, arm64::X13);
        enc.b(scaledOkLbl);
        enc.bindLabel(noDecLbl);
        enc.ldr_imm(arm64::X11, arm64::X29, 40);
        enc.scvtf_dr(arm64::D1, arm64::X11);
        enc.fldr_imm(arm64::D2, arm64::X29, 16); // |v|
        enc.fmul_ddd(arm64::D1, arm64::D2, arm64::D1);
        enc.adr_data(arm64::X16, offDoubleHalf);
        enc.fldr_imm(arm64::D2, arm64::X16, 0);
        enc.fadd_ddd(arm64::D1, arm64::D1, arm64::D2);
        enc.fcvtzs_rd(arm64::X5, arm64::D1);
        enc.bindLabel(scaledOkLbl);

        // intPart = scaled / pow10, frac = scaled % pow10
        enc.ldr_imm(arm64::X11, arm64::X29, 40);
        enc.udiv_rrr(arm64::X6, arm64::X5, arm64::X11);
        enc.mul_rrr(arm64::X7, arm64::X6, arm64::X11);
        enc.sub_rrr(arm64::X7, arm64::X5, arm64::X7);
        enc.str_imm(arm64::X7, arm64::X29, 56); // frac

        enc.mov_rr(arm64::X0, arm64::X6);
        enc.bl(lblPrintInt);

        enc.ldr_imm(arm64::X6, arm64::X29, 56);
        enc.cbz(arm64::X6, doneLbl);

        // '.'
        enc.movz(arm64::X7, '.', 0);
        enc.strb_imm(arm64::X7, arm64::X29, 48);
        enc.movz(arm64::X7, 0, 0);
        enc.strb_imm(arm64::X7, arm64::X29, 49);
        enc.add_rri(arm64::X0, arm64::X29, 48);
        enc.bl(lblPrintStr);

        // Build exactly d fraction digits at [FP+66 .. FP+66+d-1], null after
        enc.ldr_imm(arm64::X6, arm64::X29, 56);       // frac (reload after call)
        enc.add_rri(arm64::X8, arm64::X29, 66);
        enc.ldr_imm(arm64::X10, arm64::X29, 32);      // d
        enc.add_rrr(arm64::X8, arm64::X8, arm64::X10); // one past last digit
        enc.movz(arm64::X9, 0, 0);
        enc.strb_imm(arm64::X9, arm64::X8, 0);
        enc.movz(arm64::X9, 10, 0);

        enc.bindLabel(digitLoop);
        enc.sub_rri(arm64::X8, arm64::X8, 1);
        enc.udiv_rrr(arm64::X11, arm64::X6, arm64::X9);
        enc.mul_rrr(arm64::X12, arm64::X11, arm64::X9);
        enc.sub_rrr(arm64::X12, arm64::X6, arm64::X12);
        enc.add_rri(arm64::X12, arm64::X12, '0');
        enc.strb_imm(arm64::X12, arm64::X8, 0);
        enc.mov_rr(arm64::X6, arm64::X11);
        enc.sub_rri(arm64::X10, arm64::X10, 1);
        enc.cbnz(arm64::X10, digitLoop);

        // Trim trailing zeros (never removes the last remaining digit)
        enc.mov_rr(arm64::X13, arm64::X8);            // start
        enc.add_rri(arm64::X14, arm64::X29, 66);
        enc.ldr_imm(arm64::X10, arm64::X29, 32);
        enc.add_rrr(arm64::X14, arm64::X14, arm64::X10); // end (one past last digit)
        enc.bindLabel(trimLoop);
        enc.cmp_rr(arm64::X14, arm64::X13);
        enc.b_cond(arm64::LS, trimDone);
        enc.sub_rri(arm64::X17, arm64::X14, 1);
        enc.ldrb_imm(arm64::X15, arm64::X17, 0);
        enc.cmp_ri(arm64::X15, '0');
        enc.b_cond(arm64::NE, trimDone);
        enc.mov_rr(arm64::X14, arm64::X17);
        enc.b(trimLoop);

        enc.bindLabel(trimDone);
        enc.movz(arm64::X15, 0, 0);
        enc.strb_imm(arm64::X15, arm64::X14, 0);
        enc.mov_rr(arm64::X0, arm64::X13);
        enc.bl(lblPrintStr);
        enc.b(doneLbl);

        enc.bindLabel(zeroLbl);
        enc.movz(arm64::X3, '0', 0);
        enc.strb_imm(arm64::X3, arm64::X29, 48);
        enc.movz(arm64::X3, 0, 0);
        enc.strb_imm(arm64::X3, arm64::X29, 49);
        enc.add_rri(arm64::X0, arm64::X29, 48);
        enc.bl(lblPrintStr);
        enc.b(doneLbl);

        enc.bindLabel(bigLbl);
        enc.fcvtzs_rd(arm64::X0, arm64::D0);
        enc.bl(lblPrintInt);

        enc.bindLabel(doneLbl);
        enc.ldp_post(arm64::X29, arm64::X30, arm64::SP, 80);
        enc.ret();
    }

    // ==========================================
    // substr(s in X0, start in X1, count in X2) -> X0 (new null-terminated string)
    // 0-based start; copies min(count, strlen) bytes.
    // ==========================================
    void emitSubstr() {
        enc.bindLabel(lblSubstr);
        enc.stp_pre(arm64::X29, arm64::X30, arm64::SP, -64);
        enc.mov_rr(arm64::X29, arm64::SP);
        enc.str_imm(arm64::X0, arm64::X29, 16); // s
        enc.str_imm(arm64::X1, arm64::X29, 24); // start
        enc.str_imm(arm64::X2, arm64::X29, 32); // count

        // out = malloc(count + 1)
        enc.add_rri(arm64::X0, arm64::X2, 1);
        enc.bl(lblMalloc);
        enc.str_imm(arm64::X0, arm64::X29, 40); // out

        enc.movz(arm64::X3, 0, 0);
        enc.str_imm(arm64::X3, arm64::X29, 48); // i = 0

        size_t loopLbl = enc.createLabel();
        size_t doneLbl = enc.createLabel();
        enc.bindLabel(loopLbl);
        enc.ldr_imm(arm64::X3, arm64::X29, 48); // i
        enc.ldr_imm(arm64::X2, arm64::X29, 32); // count
        enc.cmp_rr(arm64::X3, arm64::X2);
        enc.b_cond(arm64::GE, doneLbl);

        enc.ldr_imm(arm64::X0, arm64::X29, 16); // s
        enc.ldr_imm(arm64::X1, arm64::X29, 24); // start
        enc.add_rrr(arm64::X0, arm64::X0, arm64::X1);
        enc.add_rrr(arm64::X0, arm64::X0, arm64::X3); // s + start + i
        enc.ldrb_imm(arm64::X4, arm64::X0, 0);

        enc.ldr_imm(arm64::X5, arm64::X29, 40); // out
        enc.add_rrr(arm64::X5, arm64::X5, arm64::X3);
        enc.strb_imm(arm64::X4, arm64::X5, 0);

        enc.add_rri(arm64::X3, arm64::X3, 1);
        enc.str_imm(arm64::X3, arm64::X29, 48);
        enc.b(loopLbl);

        enc.bindLabel(doneLbl);
        enc.ldr_imm(arm64::X5, arm64::X29, 40);
        enc.ldr_imm(arm64::X3, arm64::X29, 32);
        enc.add_rrr(arm64::X5, arm64::X5, arm64::X3);
        enc.movz(arm64::X4, 0, 0);
        enc.strb_imm(arm64::X4, arm64::X5, 0);
        enc.ldr_imm(arm64::X0, arm64::X29, 40);

        enc.ldp_post(arm64::X29, arm64::X30, arm64::SP, 64);
        enc.ret();
    }

    // ==========================================
    // substr_range(s in X0, start in X1, end in X2)
    // x86-compatible semantics: 1-based, inclusive, clamped to the string.
    // ==========================================
    void emitSubstrRange() {
        enc.bindLabel(lblSubstrRange);
        enc.stp_pre(arm64::X29, arm64::X30, arm64::SP, -96);
        enc.mov_rr(arm64::X29, arm64::SP);
        enc.str_imm(arm64::X0, arm64::X29, 16); // s
        enc.str_imm(arm64::X1, arm64::X29, 24); // a
        enc.str_imm(arm64::X2, arm64::X29, 32); // b

        size_t emptyLbl = enc.createLabel();
        size_t aOk = enc.createLabel();
        size_t bOk = enc.createLabel();
        size_t loopLbl = enc.createLabel();
        size_t finLbl = enc.createLabel();

        enc.cbz(arm64::X0, emptyLbl);
        enc.bl(lblStrLen);                       // X0 = n
        enc.str_imm(arm64::X0, arm64::X29, 40);
        enc.cbz(arm64::X0, emptyLbl);

        // a = max(a, 1)
        enc.ldr_imm(arm64::X1, arm64::X29, 24);
        enc.movz(arm64::X3, 1, 0);
        enc.cmp_rr(arm64::X1, arm64::X3);
        enc.b_cond(arm64::GE, aOk);
        enc.str_imm(arm64::X3, arm64::X29, 24);
        enc.bindLabel(aOk);

        // b = min(b, n)
        enc.ldr_imm(arm64::X2, arm64::X29, 32);
        enc.ldr_imm(arm64::X3, arm64::X29, 40);
        enc.cmp_rr(arm64::X2, arm64::X3);
        enc.b_cond(arm64::LE, bOk);
        enc.str_imm(arm64::X3, arm64::X29, 32);
        enc.bindLabel(bOk);

        // if (a > b) empty
        enc.ldr_imm(arm64::X1, arm64::X29, 24);
        enc.ldr_imm(arm64::X2, arm64::X29, 32);
        enc.cmp_rr(arm64::X1, arm64::X2);
        enc.b_cond(arm64::GT, emptyLbl);

        // count = b - a + 1, start0 = a - 1
        enc.sub_rrr(arm64::X3, arm64::X2, arm64::X1);
        enc.add_rri(arm64::X3, arm64::X3, 1);
        enc.str_imm(arm64::X3, arm64::X29, 48);
        enc.sub_rri(arm64::X3, arm64::X1, 1);
        enc.str_imm(arm64::X3, arm64::X29, 56);

        enc.ldr_imm(arm64::X0, arm64::X29, 48);
        enc.add_rri(arm64::X0, arm64::X0, 1);
        enc.bl(lblMalloc);
        enc.str_imm(arm64::X0, arm64::X29, 64); // out

        enc.movz(arm64::X3, 0, 0);
        enc.str_imm(arm64::X3, arm64::X29, 72);
        enc.bindLabel(loopLbl);
        enc.ldr_imm(arm64::X3, arm64::X29, 72);
        enc.ldr_imm(arm64::X2, arm64::X29, 48);
        enc.cmp_rr(arm64::X3, arm64::X2);
        enc.b_cond(arm64::GE, finLbl);
        enc.ldr_imm(arm64::X0, arm64::X29, 16);
        enc.ldr_imm(arm64::X1, arm64::X29, 56);
        enc.add_rrr(arm64::X0, arm64::X0, arm64::X1);
        enc.add_rrr(arm64::X0, arm64::X0, arm64::X3);
        enc.ldrb_imm(arm64::X4, arm64::X0, 0);
        enc.ldr_imm(arm64::X5, arm64::X29, 64);
        enc.add_rrr(arm64::X5, arm64::X5, arm64::X3);
        enc.strb_imm(arm64::X4, arm64::X5, 0);
        enc.add_rri(arm64::X3, arm64::X3, 1);
        enc.str_imm(arm64::X3, arm64::X29, 72);
        enc.b(loopLbl);

        enc.bindLabel(finLbl);
        enc.ldr_imm(arm64::X5, arm64::X29, 64);
        enc.ldr_imm(arm64::X3, arm64::X29, 48);
        enc.add_rrr(arm64::X5, arm64::X5, arm64::X3);
        enc.movz(arm64::X4, 0, 0);
        enc.strb_imm(arm64::X4, arm64::X5, 0);
        enc.ldr_imm(arm64::X0, arm64::X29, 64);
        enc.ldp_post(arm64::X29, arm64::X30, arm64::SP, 96);
        enc.ret();

        enc.bindLabel(emptyLbl);
        enc.movz(arm64::X0, 1, 0);
        enc.bl(lblMalloc);
        enc.movz(arm64::X4, 0, 0);
        enc.strb_imm(arm64::X4, arm64::X0, 0);
        enc.ldp_post(arm64::X29, arm64::X30, arm64::SP, 96);
        enc.ret();
    }

    // ==========================================
    // pow(x in D0, y in D1) -> D0 = x^y  (exp2(y * log2(x)))
    // ==========================================
    void emitPow() {
        enc.bindLabel(lblPow);
        enc.stp_pre(arm64::X29, arm64::X30, arm64::SP, -96);
        enc.mov_rr(arm64::X29, arm64::SP);
        enc.fstr_imm(arm64::D0, arm64::X29, 16); // x
        enc.fstr_imm(arm64::D1, arm64::X29, 24); // y

        size_t noSqrtAdj = enc.createLabel();
        size_t posZ      = enc.createLabel();
        size_t roundDone = enc.createLabel();

        // ---- log2(x) ----
        enc.ldr_imm(arm64::X0, arm64::X29, 16);
        enc.lsr_ri(arm64::X1, arm64::X0, 52);
        enc.movz(arm64::X2, 0x7FF, 0);
        enc.and_rrr(arm64::X1, arm64::X1, arm64::X2);
        enc.mov_ri64(arm64::X2, 1023);
        enc.sub_rrr(arm64::X1, arm64::X1, arm64::X2); // biased exponent
        enc.str_imm(arm64::X1, arm64::X29, 40);

        enc.movz(arm64::X3, 0xFFFF, 0);
        enc.movk(arm64::X3, 0xFFFF, 16);
        enc.movk(arm64::X3, 0xFFFF, 32);
        enc.movk(arm64::X3, 0x000F, 48);
        enc.and_rrr(arm64::X0, arm64::X0, arm64::X3);
        enc.movz(arm64::X3, 0x3FF0, 48);
        enc.orr_rrr(arm64::X0, arm64::X0, arm64::X3);
        enc.str_imm(arm64::X0, arm64::X29, 32); // mantissa bits (1.0 .. 2.0)

        enc.fldr_imm(arm64::D1, arm64::X29, 32);
        enc.adr_data(arm64::X16, offSqrt2);
        enc.fldr_imm(arm64::D2, arm64::X16, 0);
        enc.fcmp_dd(arm64::D1, arm64::D2);
        enc.b_cond(arm64::LT, noSqrtAdj);
        enc.adr_data(arm64::X16, offDoubleHalf);
        enc.fldr_imm(arm64::D2, arm64::X16, 0);
        enc.fmul_ddd(arm64::D1, arm64::D1, arm64::D2);
        enc.fstr_imm(arm64::D1, arm64::X29, 32);
        enc.ldr_imm(arm64::X1, arm64::X29, 40);
        enc.add_rri(arm64::X1, arm64::X1, 1);
        enc.str_imm(arm64::X1, arm64::X29, 40);
        enc.bindLabel(noSqrtAdj);

        // t = (m - 1) / (m + 1), p = t*t
        enc.fldr_imm(arm64::D1, arm64::X29, 32);
        enc.adr_data(arm64::X16, offDoubleOne);
        enc.fldr_imm(arm64::D2, arm64::X16, 0);
        enc.fsub_ddd(arm64::D3, arm64::D1, arm64::D2);
        enc.fadd_ddd(arm64::D4, arm64::D1, arm64::D2);
        enc.fdiv_ddd(arm64::D3, arm64::D3, arm64::D4); // t
        enc.fmul_ddd(arm64::D5, arm64::D3, arm64::D3); // p

        // S = 1/9; S = 1/7 + p*S; 1/5; 1/3; then S = 1 + p*S
        enc.adr_data(arm64::X16, offLogCoef[3]);
        enc.fldr_imm(arm64::D6, arm64::X16, 0);
        for (int i = 2; i >= 0; --i) {
            enc.adr_data(arm64::X16, offLogCoef[i]);
            enc.fldr_imm(arm64::D7, arm64::X16, 0);
            enc.fmul_ddd(arm64::D8, arm64::D5, arm64::D6);
            enc.fadd_ddd(arm64::D6, arm64::D7, arm64::D8);
        }
        enc.adr_data(arm64::X16, offDoubleOne);
        enc.fldr_imm(arm64::D7, arm64::X16, 0);
        enc.fmul_ddd(arm64::D8, arm64::D5, arm64::D6);
        enc.fadd_ddd(arm64::D6, arm64::D7, arm64::D8);

        // log2(m) = (2/ln2) * t * S ; log2(x) = e + log2(m)
        enc.fmul_ddd(arm64::D8, arm64::D3, arm64::D6);
        enc.adr_data(arm64::X16, offTwoOverLn2);
        enc.fldr_imm(arm64::D7, arm64::X16, 0);
        enc.fmul_ddd(arm64::D8, arm64::D8, arm64::D7);
        enc.ldr_imm(arm64::X1, arm64::X29, 40);
        enc.scvtf_dr(arm64::D7, arm64::X1);
        enc.fadd_ddd(arm64::D8, arm64::D8, arm64::D7);

        // z = y * log2(x)
        enc.fldr_imm(arm64::D9, arm64::X29, 24);
        enc.fmul_ddd(arm64::D8, arm64::D8, arm64::D9);
        enc.fstr_imm(arm64::D8, arm64::X29, 48);

        // ---- exp2(z) ----
        // k = trunc(z + (z < 0 ? -0.5 : 0.5))
        enc.ldr_imm(arm64::X5, arm64::X29, 48);
        enc.movz(arm64::X6, 0x8000, 48);
        enc.and_rrr(arm64::X6, arm64::X5, arm64::X6);
        enc.cbz(arm64::X6, posZ);
        enc.fldr_imm(arm64::D0, arm64::X29, 48);
        enc.adr_data(arm64::X16, offDoubleHalf);
        enc.fldr_imm(arm64::D1, arm64::X16, 0);
        enc.fsub_ddd(arm64::D0, arm64::D0, arm64::D1);
        enc.b(roundDone);
        enc.bindLabel(posZ);
        enc.fldr_imm(arm64::D0, arm64::X29, 48);
        enc.adr_data(arm64::X16, offDoubleHalf);
        enc.fldr_imm(arm64::D1, arm64::X16, 0);
        enc.fadd_ddd(arm64::D0, arm64::D0, arm64::D1);
        enc.bindLabel(roundDone);
        enc.fcvtzs_rd(arm64::X7, arm64::D0);
        enc.str_imm(arm64::X7, arm64::X29, 56); // k

        // f = z - k ; r = f * ln2
        enc.fldr_imm(arm64::D0, arm64::X29, 48);
        enc.scvtf_dr(arm64::D1, arm64::X7);
        enc.fsub_ddd(arm64::D0, arm64::D0, arm64::D1);
        enc.adr_data(arm64::X16, offLn2);
        enc.fldr_imm(arm64::D1, arm64::X16, 0);
        enc.fmul_ddd(arm64::D0, arm64::D0, arm64::D1);

        // exp(r) by Horner: S = 1/12!; ... S = 1/j! + r*S ... ; two "1 + r*S" steps
        enc.adr_data(arm64::X16, offExpCoef[0]);
        enc.fldr_imm(arm64::D2, arm64::X16, 0);
        for (size_t i = 1; i < offExpCoef.size(); ++i) {
            enc.adr_data(arm64::X16, offExpCoef[i]);
            enc.fldr_imm(arm64::D1, arm64::X16, 0);
            enc.fmul_ddd(arm64::D3, arm64::D0, arm64::D2);
            enc.fadd_ddd(arm64::D2, arm64::D1, arm64::D3);
        }
        for (int t = 0; t < 2; ++t) {
            enc.adr_data(arm64::X16, offDoubleOne);
            enc.fldr_imm(arm64::D1, arm64::X16, 0);
            enc.fmul_ddd(arm64::D3, arm64::D0, arm64::D2);
            enc.fadd_ddd(arm64::D2, arm64::D1, arm64::D3);
        }

        // scale by 2^k
        enc.fstr_imm(arm64::D2, arm64::X29, 64);
        enc.ldr_imm(arm64::X0, arm64::X29, 64);
        enc.ldr_imm(arm64::X7, arm64::X29, 56);
        enc.lsl_ri(arm64::X7, arm64::X7, 52);
        enc.add_rrr(arm64::X0, arm64::X0, arm64::X7);
        enc.str_imm(arm64::X0, arm64::X29, 64);
        enc.fldr_imm(arm64::D0, arm64::X29, 64);

        enc.ldp_post(arm64::X29, arm64::X30, arm64::SP, 96);
        enc.ret();
    }

    // ==========================================
    // str_concat(s1 in X0, s2 in X1) -> X0 (new string)
    // ==========================================
    void emitStrConcat() {
        enc.bindLabel(lblStrConcat);
        enc.stp_pre(arm64::X29, arm64::X30, arm64::SP, -80);
        enc.mov_rr(arm64::X29, arm64::SP);
        enc.str_imm(arm64::X0, arm64::X29, 16); // s1
        enc.str_imm(arm64::X1, arm64::X29, 24); // s2

        enc.bl(lblStrLen);
        enc.str_imm(arm64::X0, arm64::X29, 32); // len1
        enc.ldr_imm(arm64::X0, arm64::X29, 24);
        enc.bl(lblStrLen);
        enc.str_imm(arm64::X0, arm64::X29, 40); // len2

        enc.ldr_imm(arm64::X0, arm64::X29, 32);
        enc.ldr_imm(arm64::X1, arm64::X29, 40);
        enc.add_rrr(arm64::X0, arm64::X0, arm64::X1);
        enc.add_rri(arm64::X0, arm64::X0, 1);
        enc.bl(lblMalloc);
        enc.str_imm(arm64::X0, arm64::X29, 48); // out

        // copy s1
        enc.movz(arm64::X3, 0, 0);
        enc.str_imm(arm64::X3, arm64::X29, 56);
        size_t l1 = enc.createLabel();
        size_t a1 = enc.createLabel();
        enc.bindLabel(l1);
        enc.ldr_imm(arm64::X3, arm64::X29, 56);
        enc.ldr_imm(arm64::X2, arm64::X29, 32);
        enc.cmp_rr(arm64::X3, arm64::X2);
        enc.b_cond(arm64::GE, a1);
        enc.ldr_imm(arm64::X0, arm64::X29, 16);
        enc.add_rrr(arm64::X0, arm64::X0, arm64::X3);
        enc.ldrb_imm(arm64::X4, arm64::X0, 0);
        enc.ldr_imm(arm64::X5, arm64::X29, 48);
        enc.add_rrr(arm64::X5, arm64::X5, arm64::X3);
        enc.strb_imm(arm64::X4, arm64::X5, 0);
        enc.add_rri(arm64::X3, arm64::X3, 1);
        enc.str_imm(arm64::X3, arm64::X29, 56);
        enc.b(l1);

        // copy s2 after s1
        enc.bindLabel(a1);
        enc.movz(arm64::X3, 0, 0);
        enc.str_imm(arm64::X3, arm64::X29, 56);
        size_t l2 = enc.createLabel();
        size_t a2 = enc.createLabel();
        enc.bindLabel(l2);
        enc.ldr_imm(arm64::X3, arm64::X29, 56);
        enc.ldr_imm(arm64::X2, arm64::X29, 40);
        enc.cmp_rr(arm64::X3, arm64::X2);
        enc.b_cond(arm64::GE, a2);
        enc.ldr_imm(arm64::X0, arm64::X29, 24);
        enc.add_rrr(arm64::X0, arm64::X0, arm64::X3);
        enc.ldrb_imm(arm64::X4, arm64::X0, 0);
        enc.ldr_imm(arm64::X5, arm64::X29, 48);
        enc.ldr_imm(arm64::X2, arm64::X29, 32);
        enc.add_rrr(arm64::X5, arm64::X5, arm64::X2);
        enc.add_rrr(arm64::X5, arm64::X5, arm64::X3);
        enc.strb_imm(arm64::X4, arm64::X5, 0);
        enc.add_rri(arm64::X3, arm64::X3, 1);
        enc.str_imm(arm64::X3, arm64::X29, 56);
        enc.b(l2);

        enc.bindLabel(a2);
        enc.ldr_imm(arm64::X5, arm64::X29, 48);
        enc.ldr_imm(arm64::X2, arm64::X29, 32);
        enc.ldr_imm(arm64::X3, arm64::X29, 40);
        enc.add_rrr(arm64::X5, arm64::X5, arm64::X2);
        enc.add_rrr(arm64::X5, arm64::X5, arm64::X3);
        enc.movz(arm64::X4, 0, 0);
        enc.strb_imm(arm64::X4, arm64::X5, 0);
        enc.ldr_imm(arm64::X0, arm64::X29, 48);

        enc.ldp_post(arm64::X29, arm64::X30, arm64::SP, 80);
        enc.ret();
    }

    // ==========================================
    // str_mul(s in X0, count in X1) -> X0 (new string)
    // ==========================================
    void emitStrMul() {
        enc.bindLabel(lblStrMul);
        enc.stp_pre(arm64::X29, arm64::X30, arm64::SP, -80);
        enc.mov_rr(arm64::X29, arm64::SP);
        enc.str_imm(arm64::X0, arm64::X29, 16); // s
        enc.str_imm(arm64::X1, arm64::X29, 24); // count

        enc.bl(lblStrLen);
        enc.str_imm(arm64::X0, arm64::X29, 32); // len

        enc.ldr_imm(arm64::X1, arm64::X29, 24);
        enc.mul_rrr(arm64::X0, arm64::X0, arm64::X1); // total = len * count
        enc.str_imm(arm64::X0, arm64::X29, 40);
        enc.add_rri(arm64::X0, arm64::X0, 1);
        enc.bl(lblMalloc);
        enc.str_imm(arm64::X0, arm64::X29, 48); // out

        enc.movz(arm64::X3, 0, 0);
        enc.str_imm(arm64::X3, arm64::X29, 56); // t = 0
        size_t loopLbl = enc.createLabel();
        size_t doneLbl = enc.createLabel();
        enc.bindLabel(loopLbl);
        enc.ldr_imm(arm64::X3, arm64::X29, 56);
        enc.ldr_imm(arm64::X2, arm64::X29, 40);
        enc.cmp_rr(arm64::X3, arm64::X2);
        enc.b_cond(arm64::GE, doneLbl);

        // r = t % len
        enc.ldr_imm(arm64::X1, arm64::X29, 32);
        enc.udiv_rrr(arm64::X4, arm64::X3, arm64::X1);
        enc.mul_rrr(arm64::X5, arm64::X4, arm64::X1);
        enc.sub_rrr(arm64::X5, arm64::X3, arm64::X5);

        enc.ldr_imm(arm64::X0, arm64::X29, 16);
        enc.add_rrr(arm64::X0, arm64::X0, arm64::X5);
        enc.ldrb_imm(arm64::X6, arm64::X0, 0);

        enc.ldr_imm(arm64::X5, arm64::X29, 48);
        enc.add_rrr(arm64::X5, arm64::X5, arm64::X3);
        enc.strb_imm(arm64::X6, arm64::X5, 0);

        enc.add_rri(arm64::X3, arm64::X3, 1);
        enc.str_imm(arm64::X3, arm64::X29, 56);
        enc.b(loopLbl);

        enc.bindLabel(doneLbl);
        enc.ldr_imm(arm64::X5, arm64::X29, 48);
        enc.ldr_imm(arm64::X3, arm64::X29, 40);
        enc.add_rrr(arm64::X5, arm64::X5, arm64::X3);
        enc.movz(arm64::X4, 0, 0);
        enc.strb_imm(arm64::X4, arm64::X5, 0);
        enc.ldr_imm(arm64::X0, arm64::X29, 48);

        enc.ldp_post(arm64::X29, arm64::X30, arm64::SP, 80);
        enc.ret();
    }

    // ==========================================
    // memcpy(dst X0, src X1, len X2) -> X0 = dst
    // ==========================================
    void emitMemCpy() {
        enc.bindLabel(lblMemCpy);
        enc.movz(arm64::X3, 0, 0);
        size_t loop = enc.createLabel();
        size_t done = enc.createLabel();
        enc.bindLabel(loop);
        enc.cmp_rr(arm64::X3, arm64::X2);
        enc.b_cond(arm64::GE, done);
        enc.add_rrr(arm64::X4, arm64::X1, arm64::X3);
        enc.ldrb_imm(arm64::X5, arm64::X4, 0);
        enc.add_rrr(arm64::X6, arm64::X0, arm64::X3);
        enc.strb_imm(arm64::X5, arm64::X6, 0);
        enc.add_rri(arm64::X3, arm64::X3, 1);
        enc.b(loop);
        enc.bindLabel(done);
        enc.ret();
    }

    // ==========================================
    // upper/lower(s in X0) -> X0 (new string)
    // ==========================================
    void emitCaseConvert(size_t lbl, bool toUpper) {
        arm64::emitCaseConvert(enc, lbl, toUpper, lblStrLen, lblMalloc, offEmptyStr);
    }

    // ==========================================
    // title(s in X0): lowercase, then capitalize word starts
    // ==========================================
    void emitStrTitle() {
        enc.bindLabel(lblStrTitle);
        enc.stp_pre(arm64::FP, arm64::LR, arm64::SP, -64);
        enc.mov_rr(arm64::FP, arm64::SP);
        size_t emptyLbl = enc.createLabel();
        size_t doneLbl  = enc.createLabel();
        size_t nextLbl  = enc.createLabel();
        size_t loopLbl  = enc.createLabel();

        enc.cbz(arm64::X0, emptyLbl);
        enc.bl(lblStrLower);
        enc.str_imm(arm64::X0, arm64::FP, 16); // p
        enc.movz(arm64::X4, 0, 0);   // i
        enc.movz(arm64::X14, 1, 0);  // prevIsSpace

        enc.bindLabel(loopLbl);
        enc.ldr_imm(arm64::X6, arm64::FP, 16);
        enc.add_rrr(arm64::X9, arm64::X6, arm64::X4);
        enc.ldrb_imm(arm64::X10, arm64::X9, 0);
        enc.cbz(arm64::X10, doneLbl);
        enc.cbz(arm64::X14, nextLbl);
        enc.movz(arm64::X11, 'a', 0);
        enc.cmp_rr(arm64::X10, arm64::X11);
        enc.b_cond(arm64::LT, nextLbl);
        enc.movz(arm64::X11, 'z', 0);
        enc.cmp_rr(arm64::X10, arm64::X11);
        enc.b_cond(arm64::GT, nextLbl);
        enc.sub_rri(arm64::X10, arm64::X10, 32);
        enc.strb_imm(arm64::X10, arm64::X9, 0);

        enc.bindLabel(nextLbl);
        enc.cmp_ri(arm64::X10, ' ');
        enc.cset(arm64::X14, arm64::EQ);
        enc.add_rri(arm64::X4, arm64::X4, 1);
        enc.b(loopLbl);

        enc.bindLabel(doneLbl);
        enc.ldr_imm(arm64::X0, arm64::FP, 16);
        enc.ldp_post(arm64::FP, arm64::LR, arm64::SP, 64);
        enc.ret();

        enc.bindLabel(emptyLbl);
        enc.adr_data(arm64::X0, offEmptyStr);
        enc.ldp_post(arm64::FP, arm64::LR, arm64::SP, 64);
        enc.ret();
    }

    // ==========================================
    // strip(s in X0): trim spaces/tabs/newlines on both ends
    // ==========================================
    void emitStrStrip() {
        arm64::emitStrStrip(enc, lblStrStrip, lblStrLen, lblMalloc, lblMemCpy, offEmptyStr);
    }

    // ==========================================
    // find(s in X0, sub in X1) -> index or -1
    // ==========================================
    void emitStrFind() {
        enc.bindLabel(lblStrFind);
        enc.stp_pre(arm64::FP, arm64::LR, arm64::SP, -80);
        enc.mov_rr(arm64::FP, arm64::SP);
        enc.str_imm(arm64::X0, arm64::FP, 16);
        enc.str_imm(arm64::X1, arm64::FP, 24);

        size_t notFound = enc.createLabel();
        size_t endLbl = enc.createLabel();
        size_t floop = enc.createLabel();
        size_t fj = enc.createLabel();
        size_t found = enc.createLabel();
        size_t fnext = enc.createLabel();

        enc.cbz(arm64::X0, notFound);
        enc.cbz(arm64::X1, notFound);

        enc.bl(lblStrLen);                       // n (X0 = s)
        enc.str_imm(arm64::X0, arm64::FP, 32);
        enc.ldr_imm(arm64::X0, arm64::FP, 24);
        enc.bl(lblStrLen);                       // m
        enc.str_imm(arm64::X0, arm64::FP, 40);

        enc.ldr_imm(arm64::X6, arm64::FP, 32);   // n
        enc.ldr_imm(arm64::X7, arm64::FP, 40);   // m
        enc.sub_rrr(arm64::X8, arm64::X6, arm64::X7); // n - m
        enc.cmp_ri(arm64::X8, 0);
        enc.b_cond(arm64::LT, notFound);
        enc.movz(arm64::X4, 0, 0);

        enc.bindLabel(floop);
        enc.cmp_rr(arm64::X4, arm64::X8);
        enc.b_cond(arm64::GT, notFound);
        enc.movz(arm64::X9, 0, 0);
        enc.bindLabel(fj);
        enc.cmp_rr(arm64::X9, arm64::X7);
        enc.b_cond(arm64::GE, found);
        enc.ldr_imm(arm64::X10, arm64::FP, 16);
        enc.add_rrr(arm64::X10, arm64::X10, arm64::X4);
        enc.add_rrr(arm64::X10, arm64::X10, arm64::X9);
        enc.ldrb_imm(arm64::X11, arm64::X10, 0);
        enc.ldr_imm(arm64::X12, arm64::FP, 24);
        enc.add_rrr(arm64::X12, arm64::X12, arm64::X9);
        enc.ldrb_imm(arm64::X13, arm64::X12, 0);
        enc.cmp_rr(arm64::X11, arm64::X13);
        enc.b_cond(arm64::NE, fnext);
        enc.add_rri(arm64::X9, arm64::X9, 1);
        enc.b(fj);

        enc.bindLabel(fnext);
        enc.add_rri(arm64::X4, arm64::X4, 1);
        enc.b(floop);

        enc.bindLabel(found);
        enc.mov_rr(arm64::X0, arm64::X4);
        enc.b(endLbl);

        enc.bindLabel(notFound);
        enc.movn(arm64::X0, 0, 0);

        enc.bindLabel(endLbl);
        enc.ldp_post(arm64::FP, arm64::LR, arm64::SP, 80);
        enc.ret();
    }

    // ==========================================
    // startswith(s X0, prefix X1) / endswith(s X0, suffix X1) -> 0/1
    // ==========================================
    void emitStrStartsWith() {
        enc.bindLabel(lblStrStartsWith);
        enc.stp_pre(arm64::FP, arm64::LR, arm64::SP, -80);
        enc.mov_rr(arm64::FP, arm64::SP);
        enc.str_imm(arm64::X0, arm64::FP, 16);
        enc.str_imm(arm64::X1, arm64::FP, 24);

        size_t falseLbl = enc.createLabel();
        size_t trueLbl = enc.createLabel();
        size_t endLbl = enc.createLabel();
        size_t loopLbl = enc.createLabel();

        enc.cbz(arm64::X0, falseLbl);
        enc.cbz(arm64::X1, trueLbl);
        enc.bl(lblStrLen);
        enc.str_imm(arm64::X0, arm64::FP, 32);   // lenS
        enc.ldr_imm(arm64::X0, arm64::FP, 24);
        enc.bl(lblStrLen);
        enc.str_imm(arm64::X0, arm64::FP, 40);   // lenPre
        enc.cbz(arm64::X0, trueLbl);
        enc.ldr_imm(arm64::X6, arm64::FP, 32);
        enc.ldr_imm(arm64::X7, arm64::FP, 40);
        enc.cmp_rr(arm64::X6, arm64::X7);
        enc.b_cond(arm64::LT, falseLbl);
        enc.movz(arm64::X4, 0, 0);
        enc.bindLabel(loopLbl);
        enc.cmp_rr(arm64::X4, arm64::X7);
        enc.b_cond(arm64::GE, trueLbl);
        enc.ldr_imm(arm64::X10, arm64::FP, 16);
        enc.add_rrr(arm64::X10, arm64::X10, arm64::X4);
        enc.ldrb_imm(arm64::X11, arm64::X10, 0);
        enc.ldr_imm(arm64::X12, arm64::FP, 24);
        enc.add_rrr(arm64::X12, arm64::X12, arm64::X4);
        enc.ldrb_imm(arm64::X13, arm64::X12, 0);
        enc.cmp_rr(arm64::X11, arm64::X13);
        enc.b_cond(arm64::NE, falseLbl);
        enc.add_rri(arm64::X4, arm64::X4, 1);
        enc.b(loopLbl);

        enc.bindLabel(trueLbl);
        enc.movz(arm64::X0, 1, 0);
        enc.b(endLbl);
        enc.bindLabel(falseLbl);
        enc.movz(arm64::X0, 0, 0);
        enc.bindLabel(endLbl);
        enc.ldp_post(arm64::FP, arm64::LR, arm64::SP, 80);
        enc.ret();
    }

    void emitStrEndsWith() {
        enc.bindLabel(lblStrEndsWith);
        enc.stp_pre(arm64::FP, arm64::LR, arm64::SP, -80);
        enc.mov_rr(arm64::FP, arm64::SP);
        enc.str_imm(arm64::X0, arm64::FP, 16);
        enc.str_imm(arm64::X1, arm64::FP, 24);

        size_t falseLbl = enc.createLabel();
        size_t trueLbl = enc.createLabel();
        size_t endLbl = enc.createLabel();
        size_t loopLbl = enc.createLabel();

        enc.cbz(arm64::X0, falseLbl);
        enc.cbz(arm64::X1, trueLbl);
        enc.bl(lblStrLen);
        enc.str_imm(arm64::X0, arm64::FP, 32);
        enc.ldr_imm(arm64::X0, arm64::FP, 24);
        enc.bl(lblStrLen);
        enc.str_imm(arm64::X0, arm64::FP, 40);
        enc.cbz(arm64::X0, trueLbl);
        enc.ldr_imm(arm64::X6, arm64::FP, 32);
        enc.ldr_imm(arm64::X7, arm64::FP, 40);
        enc.cmp_rr(arm64::X6, arm64::X7);
        enc.b_cond(arm64::LT, falseLbl);
        // offset = n - m
        enc.sub_rrr(arm64::X8, arm64::X6, arm64::X7);
        enc.movz(arm64::X4, 0, 0);
        enc.bindLabel(loopLbl);
        enc.cmp_rr(arm64::X4, arm64::X7);
        enc.b_cond(arm64::GE, trueLbl);
        enc.ldr_imm(arm64::X10, arm64::FP, 16);
        enc.add_rrr(arm64::X10, arm64::X10, arm64::X8);
        enc.add_rrr(arm64::X10, arm64::X10, arm64::X4);
        enc.ldrb_imm(arm64::X11, arm64::X10, 0);
        enc.ldr_imm(arm64::X12, arm64::FP, 24);
        enc.add_rrr(arm64::X12, arm64::X12, arm64::X4);
        enc.ldrb_imm(arm64::X13, arm64::X12, 0);
        enc.cmp_rr(arm64::X11, arm64::X13);
        enc.b_cond(arm64::NE, falseLbl);
        enc.add_rri(arm64::X4, arm64::X4, 1);
        enc.b(loopLbl);

        enc.bindLabel(trueLbl);
        enc.movz(arm64::X0, 1, 0);
        enc.b(endLbl);
        enc.bindLabel(falseLbl);
        enc.movz(arm64::X0, 0, 0);
        enc.bindLabel(endLbl);
        enc.ldp_post(arm64::FP, arm64::LR, arm64::SP, 80);
        enc.ret();
    }

    // ==========================================
    // count(s X0, sub X1): non-overlapping occurrences
    // ==========================================
    void emitStrCount() {
        enc.bindLabel(lblStrCount);
        enc.stp_pre(arm64::FP, arm64::LR, arm64::SP, -80);
        enc.mov_rr(arm64::FP, arm64::SP);
        enc.str_imm(arm64::X0, arm64::FP, 16);
        enc.str_imm(arm64::X1, arm64::FP, 24);

        size_t zeroLbl = enc.createLabel();
        size_t endLbl = enc.createLabel();
        size_t loopLbl = enc.createLabel();

        enc.cbz(arm64::X0, zeroLbl);
        enc.cbz(arm64::X1, zeroLbl);
        enc.ldrb_imm(arm64::X9, arm64::X1, 0);
        enc.cbz(arm64::X9, zeroLbl);

        enc.movz(arm64::X0, 0, 0);
        enc.str_imm(arm64::X0, arm64::FP, 32);  // count
        enc.ldr_imm(arm64::X0, arm64::FP, 16);
        enc.str_imm(arm64::X0, arm64::FP, 40);  // p

        enc.ldr_imm(arm64::X0, arm64::FP, 24);
        enc.bl(lblStrLen);
        enc.str_imm(arm64::X0, arm64::FP, 48);  // m

        enc.bindLabel(loopLbl);
        enc.ldr_imm(arm64::X0, arm64::FP, 40);  // p
        enc.ldr_imm(arm64::X1, arm64::FP, 24);  // sub
        enc.bl(lblStrFind);
        enc.cmp_ri(arm64::X0, 0);
        enc.b_cond(arm64::LT, endLbl);

        enc.ldr_imm(arm64::X6, arm64::FP, 32);
        enc.add_rri(arm64::X6, arm64::X6, 1);
        enc.str_imm(arm64::X6, arm64::FP, 32);   // count++

        enc.ldr_imm(arm64::X7, arm64::FP, 40);   // p
        enc.add_rrr(arm64::X7, arm64::X7, arm64::X0); // p + idx
        enc.ldr_imm(arm64::X8, arm64::FP, 48);   // m
        enc.add_rrr(arm64::X7, arm64::X7, arm64::X8);
        enc.str_imm(arm64::X7, arm64::FP, 40);
        enc.b(loopLbl);

        enc.bindLabel(endLbl);
        enc.ldr_imm(arm64::X0, arm64::FP, 32);
        enc.ldp_post(arm64::FP, arm64::LR, arm64::SP, 80);
        enc.ret();
        enc.bindLabel(zeroLbl);
        enc.movz(arm64::X0, 0, 0);
        enc.ldp_post(arm64::FP, arm64::LR, arm64::SP, 80);
        enc.ret();
    }

    // ==========================================
    // strcmp_eq(s1 X0, s2 X1) -> 0/1
    // ==========================================
    void emitStrCmpEq() {
        enc.bindLabel(lblStrCmpEq);
        size_t neLbl = enc.createLabel();
        size_t loopLbl = enc.createLabel();
        size_t okLbl = enc.createLabel();
        enc.cmp_rr(arm64::X0, arm64::X1);
        enc.b_cond(arm64::EQ, okLbl);
        enc.cbz(arm64::X0, neLbl);
        enc.cbz(arm64::X1, neLbl);
        enc.movz(arm64::X2, 0, 0);
        enc.bindLabel(loopLbl);
        enc.add_rrr(arm64::X3, arm64::X0, arm64::X2);
        enc.ldrb_imm(arm64::X4, arm64::X3, 0);
        enc.add_rrr(arm64::X5, arm64::X1, arm64::X2);
        enc.ldrb_imm(arm64::X6, arm64::X5, 0);
        enc.cmp_rr(arm64::X4, arm64::X6);
        enc.b_cond(arm64::NE, neLbl);
        enc.cbz(arm64::X4, okLbl);
        enc.add_rri(arm64::X2, arm64::X2, 1);
        enc.b(loopLbl);
        enc.bindLabel(okLbl);
        enc.movz(arm64::X0, 1, 0);
        enc.ret();
        enc.bindLabel(neLbl);
        enc.movz(arm64::X0, 0, 0);
        enc.ret();
    }

    // ==========================================
    // replace(s X0, old X1, new X2) -> X0
    // ==========================================
    void emitStrReplace() {
        enc.bindLabel(lblStrReplace);
        enc.stp_pre(arm64::FP, arm64::LR, arm64::SP, -160);
        enc.mov_rr(arm64::FP, arm64::SP);
        enc.str_imm(arm64::X0, arm64::FP, 16); // s
        enc.str_imm(arm64::X1, arm64::FP, 24); // old
        enc.str_imm(arm64::X2, arm64::FP, 32); // new

        size_t sOk = enc.createLabel();
        size_t copyWhole = enc.createLabel();
        size_t countLoop = enc.createLabel();
        size_t countDone = enc.createLabel();
        size_t buildLoop = enc.createLabel();
        size_t tailLbl = enc.createLabel();
        size_t endLbl = enc.createLabel();

        enc.cbnz(arm64::X0, sOk);
        enc.adr_data(arm64::X0, offEmptyStr);
        enc.str_imm(arm64::X0, arm64::FP, 16);
        enc.b(endLbl);

        enc.bindLabel(sOk);
        enc.ldr_imm(arm64::X0, arm64::FP, 24);
        enc.cbz(arm64::X0, copyWhole);
        enc.ldrb_imm(arm64::X9, arm64::X0, 0);
        enc.cbz(arm64::X9, copyWhole);

        enc.ldr_imm(arm64::X0, arm64::FP, 16);
        enc.bl(lblStrLen);
        enc.str_imm(arm64::X0, arm64::FP, 40); // n
        enc.ldr_imm(arm64::X0, arm64::FP, 24);
        enc.bl(lblStrLen);
        enc.str_imm(arm64::X0, arm64::FP, 48); // m
        enc.ldr_imm(arm64::X0, arm64::FP, 32);
        enc.bl(lblStrLen);
        enc.str_imm(arm64::X0, arm64::FP, 56); // k

        enc.movz(arm64::X0, 0, 0);
        enc.str_imm(arm64::X0, arm64::FP, 64); // count
        enc.ldr_imm(arm64::X0, arm64::FP, 16);
        enc.str_imm(arm64::X0, arm64::FP, 88); // p

        enc.bindLabel(countLoop);
        enc.ldr_imm(arm64::X0, arm64::FP, 88);
        enc.ldr_imm(arm64::X1, arm64::FP, 24);
        enc.bl(lblStrFind);
        enc.cmp_ri(arm64::X0, 0);
        enc.b_cond(arm64::LT, countDone);
        enc.ldr_imm(arm64::X6, arm64::FP, 64);
        enc.add_rri(arm64::X6, arm64::X6, 1);
        enc.str_imm(arm64::X6, arm64::FP, 64);
        enc.ldr_imm(arm64::X7, arm64::FP, 88);
        enc.add_rrr(arm64::X7, arm64::X7, arm64::X0);
        enc.ldr_imm(arm64::X8, arm64::FP, 48);
        enc.add_rrr(arm64::X7, arm64::X7, arm64::X8);
        enc.str_imm(arm64::X7, arm64::FP, 88);
        enc.b(countLoop);

        enc.bindLabel(countDone);
        enc.ldr_imm(arm64::X6, arm64::FP, 40);
        enc.ldr_imm(arm64::X7, arm64::FP, 64);
        enc.ldr_imm(arm64::X8, arm64::FP, 56);
        enc.ldr_imm(arm64::X9, arm64::FP, 48);
        enc.sub_rrr(arm64::X10, arm64::X8, arm64::X9);
        enc.mul_rrr(arm64::X10, arm64::X7, arm64::X10);
        enc.add_rrr(arm64::X6, arm64::X6, arm64::X10);
        enc.str_imm(arm64::X6, arm64::FP, 72); // newLen
        enc.add_rri(arm64::X0, arm64::X6, 1);
        enc.bl(lblMalloc);
        enc.str_imm(arm64::X0, arm64::FP, 80); // out

        enc.ldr_imm(arm64::X0, arm64::FP, 16);
        enc.str_imm(arm64::X0, arm64::FP, 88); // p = s
        enc.movz(arm64::X0, 0, 0);
        enc.str_imm(arm64::X0, arm64::FP, 96); // o = 0

        enc.bindLabel(buildLoop);
        enc.ldr_imm(arm64::X0, arm64::FP, 88);
        enc.ldr_imm(arm64::X1, arm64::FP, 24);
        enc.bl(lblStrFind);
        enc.cmp_ri(arm64::X0, 0);
        enc.b_cond(arm64::LT, tailLbl);
        enc.str_imm(arm64::X0, arm64::FP, 104); // idx

        enc.ldr_imm(arm64::X0, arm64::FP, 80);
        enc.ldr_imm(arm64::X5, arm64::FP, 96);
        enc.add_rrr(arm64::X0, arm64::X0, arm64::X5);
        enc.ldr_imm(arm64::X1, arm64::FP, 88);
        enc.ldr_imm(arm64::X2, arm64::FP, 104);
        enc.bl(lblMemCpy);
        enc.ldr_imm(arm64::X5, arm64::FP, 96);
        enc.ldr_imm(arm64::X6, arm64::FP, 104);
        enc.add_rrr(arm64::X5, arm64::X5, arm64::X6);
        enc.str_imm(arm64::X5, arm64::FP, 96);

        enc.ldr_imm(arm64::X0, arm64::FP, 80);
        enc.add_rrr(arm64::X0, arm64::X0, arm64::X5);
        enc.ldr_imm(arm64::X1, arm64::FP, 32);
        enc.ldr_imm(arm64::X2, arm64::FP, 56);
        enc.bl(lblMemCpy);
        enc.ldr_imm(arm64::X5, arm64::FP, 96);
        enc.ldr_imm(arm64::X6, arm64::FP, 56);
        enc.add_rrr(arm64::X5, arm64::X5, arm64::X6);
        enc.str_imm(arm64::X5, arm64::FP, 96);

        enc.ldr_imm(arm64::X7, arm64::FP, 88);
        enc.ldr_imm(arm64::X6, arm64::FP, 104);
        enc.add_rrr(arm64::X7, arm64::X7, arm64::X6);
        enc.ldr_imm(arm64::X6, arm64::FP, 48);
        enc.add_rrr(arm64::X7, arm64::X7, arm64::X6);
        enc.str_imm(arm64::X7, arm64::FP, 88);
        enc.b(buildLoop);

        enc.bindLabel(tailLbl);
        enc.ldr_imm(arm64::X7, arm64::FP, 88);
        enc.ldr_imm(arm64::X6, arm64::FP, 16);
        enc.sub_rrr(arm64::X7, arm64::X7, arm64::X6);
        enc.ldr_imm(arm64::X8, arm64::FP, 40);
        enc.sub_rrr(arm64::X8, arm64::X8, arm64::X7);
        enc.str_imm(arm64::X8, arm64::FP, 104); // rem

        enc.ldr_imm(arm64::X0, arm64::FP, 80);
        enc.ldr_imm(arm64::X5, arm64::FP, 96);
        enc.add_rrr(arm64::X0, arm64::X0, arm64::X5);
        enc.ldr_imm(arm64::X1, arm64::FP, 88);
        enc.ldr_imm(arm64::X2, arm64::FP, 104);
        enc.bl(lblMemCpy);
        enc.ldr_imm(arm64::X5, arm64::FP, 96);
        enc.ldr_imm(arm64::X6, arm64::FP, 104);
        enc.add_rrr(arm64::X5, arm64::X5, arm64::X6);

        enc.ldr_imm(arm64::X0, arm64::FP, 80);
        enc.add_rrr(arm64::X0, arm64::X0, arm64::X5);
        enc.movz(arm64::X6, 0, 0);
        enc.strb_imm(arm64::X6, arm64::X0, 0);
        enc.ldr_imm(arm64::X0, arm64::FP, 80);
        enc.b(endLbl);

        enc.bindLabel(copyWhole);
        enc.ldr_imm(arm64::X0, arm64::FP, 16);
        enc.bl(lblStrLen);
        enc.str_imm(arm64::X0, arm64::FP, 40);
        enc.add_rri(arm64::X0, arm64::X0, 1);
        enc.bl(lblMalloc);
        enc.str_imm(arm64::X0, arm64::FP, 80);
        enc.ldr_imm(arm64::X1, arm64::FP, 16);
        enc.ldr_imm(arm64::X2, arm64::FP, 40);
        enc.bl(lblMemCpy);
        enc.ldr_imm(arm64::X0, arm64::FP, 80);

        enc.bindLabel(endLbl);
        enc.ldp_post(arm64::FP, arm64::LR, arm64::SP, 160);
        enc.ret();
    }

    // ==========================================
    // split(s X0, delim X1) -> list of strings
    // ==========================================
    void emitStrSplit() {
        enc.bindLabel(lblStrSplit);
        enc.stp_pre(arm64::FP, arm64::LR, arm64::SP, -176);
        enc.mov_rr(arm64::FP, arm64::SP);
        enc.str_imm(arm64::X0, arm64::FP, 16);
        enc.str_imm(arm64::X1, arm64::FP, 24);

        size_t sOk = enc.createLabel();
        size_t single = enc.createLabel();
        size_t countLoop = enc.createLabel();
        size_t countDone = enc.createLabel();
        size_t fillLoop = enc.createLabel();
        size_t fillDone = enc.createLabel();
        size_t endLbl = enc.createLabel();

        enc.cbnz(arm64::X0, sOk);
        enc.adr_data(arm64::X0, offEmptyStr);
        enc.str_imm(arm64::X0, arm64::FP, 16);
        enc.bindLabel(sOk);
        enc.bl(lblStrLen);
        enc.str_imm(arm64::X0, arm64::FP, 32); // n

        enc.ldr_imm(arm64::X0, arm64::FP, 24);
        enc.cbz(arm64::X0, single);
        enc.ldrb_imm(arm64::X9, arm64::X0, 0);
        enc.cbz(arm64::X9, single);
        enc.bl(lblStrLen);
        enc.str_imm(arm64::X0, arm64::FP, 40); // m

        enc.movz(arm64::X0, 1, 0);
        enc.str_imm(arm64::X0, arm64::FP, 48); // pieces
        enc.ldr_imm(arm64::X0, arm64::FP, 16);
        enc.str_imm(arm64::X0, arm64::FP, 64); // p

        enc.bindLabel(countLoop);
        enc.ldr_imm(arm64::X0, arm64::FP, 64);
        enc.ldr_imm(arm64::X1, arm64::FP, 24);
        enc.bl(lblStrFind);
        enc.cmp_ri(arm64::X0, 0);
        enc.b_cond(arm64::LT, countDone);
        enc.ldr_imm(arm64::X6, arm64::FP, 48);
        enc.add_rri(arm64::X6, arm64::X6, 1);
        enc.str_imm(arm64::X6, arm64::FP, 48);
        enc.ldr_imm(arm64::X7, arm64::FP, 64);
        enc.add_rrr(arm64::X7, arm64::X7, arm64::X0);
        enc.ldr_imm(arm64::X8, arm64::FP, 40);
        enc.add_rrr(arm64::X7, arm64::X7, arm64::X8);
        enc.str_imm(arm64::X7, arm64::FP, 64);
        enc.b(countLoop);

        enc.bindLabel(countDone);
        enc.ldr_imm(arm64::X0, arm64::FP, 48);
        enc.add_rri(arm64::X0, arm64::X0, 1);
        enc.movz(arm64::X9, 8, 0);
        enc.mul_rrr(arm64::X0, arm64::X0, arm64::X9);
        enc.bl(lblMalloc);
        enc.str_imm(arm64::X0, arm64::FP, 56); // list
        enc.ldr_imm(arm64::X6, arm64::FP, 48);
        enc.str_imm(arm64::X6, arm64::X0, 0);

        enc.ldr_imm(arm64::X0, arm64::FP, 16);
        enc.str_imm(arm64::X0, arm64::FP, 64); // p
        enc.movz(arm64::X0, 0, 0);
        enc.str_imm(arm64::X0, arm64::FP, 72); // idx

        enc.bindLabel(fillLoop);
        enc.ldr_imm(arm64::X0, arm64::FP, 64);
        enc.ldr_imm(arm64::X1, arm64::FP, 24);
        enc.bl(lblStrFind);
        enc.cmp_ri(arm64::X0, 0);
        enc.b_cond(arm64::LT, fillDone);
        enc.str_imm(arm64::X0, arm64::FP, 88); // len
        enc.add_rri(arm64::X0, arm64::X0, 1);
        enc.bl(lblMalloc);
        enc.str_imm(arm64::X0, arm64::FP, 96); // buf
        enc.ldr_imm(arm64::X1, arm64::FP, 64);
        enc.ldr_imm(arm64::X2, arm64::FP, 88);
        enc.bl(lblMemCpy);
        enc.ldr_imm(arm64::X0, arm64::FP, 96);
        enc.ldr_imm(arm64::X2, arm64::FP, 88);
        enc.add_rrr(arm64::X3, arm64::X0, arm64::X2);
        enc.movz(arm64::X4, 0, 0);
        enc.strb_imm(arm64::X4, arm64::X3, 0);

        enc.ldr_imm(arm64::X0, arm64::FP, 56);
        enc.ldr_imm(arm64::X6, arm64::FP, 72);
        enc.movz(arm64::X7, 8, 0);
        enc.mul_rrr(arm64::X6, arm64::X6, arm64::X7);
        enc.add_rrr(arm64::X0, arm64::X0, arm64::X6);
        enc.ldr_imm(arm64::X8, arm64::FP, 96);
        enc.str_imm(arm64::X8, arm64::X0, 8);

        enc.ldr_imm(arm64::X6, arm64::FP, 72);
        enc.add_rri(arm64::X6, arm64::X6, 1);
        enc.str_imm(arm64::X6, arm64::FP, 72);
        enc.ldr_imm(arm64::X7, arm64::FP, 64);
        enc.ldr_imm(arm64::X8, arm64::FP, 88);
        enc.add_rrr(arm64::X7, arm64::X7, arm64::X8);
        enc.ldr_imm(arm64::X8, arm64::FP, 40);
        enc.add_rrr(arm64::X7, arm64::X7, arm64::X8);
        enc.str_imm(arm64::X7, arm64::FP, 64);
        enc.b(fillLoop);

        enc.bindLabel(fillDone);
        enc.ldr_imm(arm64::X0, arm64::FP, 64);
        enc.bl(lblStrLen);
        enc.str_imm(arm64::X0, arm64::FP, 88);
        enc.add_rri(arm64::X0, arm64::X0, 1);
        enc.bl(lblMalloc);
        enc.str_imm(arm64::X0, arm64::FP, 96);
        enc.ldr_imm(arm64::X1, arm64::FP, 64);
        enc.ldr_imm(arm64::X2, arm64::FP, 88);
        enc.bl(lblMemCpy);
        enc.ldr_imm(arm64::X0, arm64::FP, 96);
        enc.ldr_imm(arm64::X2, arm64::FP, 88);
        enc.add_rrr(arm64::X3, arm64::X0, arm64::X2);
        enc.movz(arm64::X4, 0, 0);
        enc.strb_imm(arm64::X4, arm64::X3, 0);
        enc.ldr_imm(arm64::X0, arm64::FP, 56);
        enc.ldr_imm(arm64::X6, arm64::FP, 72);
        enc.movz(arm64::X7, 8, 0);
        enc.mul_rrr(arm64::X6, arm64::X6, arm64::X7);
        enc.add_rrr(arm64::X0, arm64::X0, arm64::X6);
        enc.ldr_imm(arm64::X8, arm64::FP, 96);
        enc.str_imm(arm64::X8, arm64::X0, 8);
        enc.ldr_imm(arm64::X0, arm64::FP, 56);
        enc.b(endLbl);

        enc.bindLabel(single);
        enc.movz(arm64::X0, 16, 0);
        enc.bl(lblMalloc);
        enc.str_imm(arm64::X0, arm64::FP, 56);
        enc.movz(arm64::X6, 1, 0);
        enc.str_imm(arm64::X6, arm64::X0, 0);
        enc.ldr_imm(arm64::X0, arm64::FP, 32);
        enc.add_rri(arm64::X0, arm64::X0, 1);
        enc.bl(lblMalloc);
        enc.str_imm(arm64::X0, arm64::FP, 96);
        enc.ldr_imm(arm64::X1, arm64::FP, 16);
        enc.ldr_imm(arm64::X2, arm64::FP, 32);
        enc.bl(lblMemCpy);
        enc.ldr_imm(arm64::X0, arm64::FP, 96);
        enc.ldr_imm(arm64::X2, arm64::FP, 32);
        enc.add_rrr(arm64::X3, arm64::X0, arm64::X2);
        enc.movz(arm64::X4, 0, 0);
        enc.strb_imm(arm64::X4, arm64::X3, 0);
        enc.ldr_imm(arm64::X0, arm64::FP, 56);
        enc.ldr_imm(arm64::X8, arm64::FP, 96);
        enc.str_imm(arm64::X8, arm64::X0, 8);
        enc.ldr_imm(arm64::X0, arm64::FP, 56);

        enc.bindLabel(endLbl);
        enc.ldp_post(arm64::FP, arm64::LR, arm64::SP, 176);
        enc.ret();
    }

    // ==========================================
    // join(glue X0, list X1) -> string
    // ==========================================
    void emitStrJoin() {
        enc.bindLabel(lblStrJoin);
        enc.stp_pre(arm64::FP, arm64::LR, arm64::SP, -112);
        enc.mov_rr(arm64::FP, arm64::SP);

        size_t glueOk = enc.createLabel();
        size_t emptyLbl = enc.createLabel();
        size_t sumLoop = enc.createLabel();
        size_t sumDone = enc.createLabel();
        size_t buildLoop = enc.createLabel();
        size_t buildDone = enc.createLabel();
        size_t noGlueLbl = enc.createLabel();

        enc.cbnz(arm64::X0, glueOk);
        enc.adr_data(arm64::X0, offEmptyStr);
        enc.bindLabel(glueOk);
        enc.str_imm(arm64::X0, arm64::FP, 16); // glue
        enc.str_imm(arm64::X1, arm64::FP, 24); // list
        enc.cbz(arm64::X1, emptyLbl);
        enc.ldr_imm(arm64::X6, arm64::X1, 0);
        enc.str_imm(arm64::X6, arm64::FP, 32); // n
        enc.cbz(arm64::X6, emptyLbl);

        enc.ldr_imm(arm64::X0, arm64::FP, 16);
        enc.bl(lblStrLen);
        enc.str_imm(arm64::X0, arm64::FP, 64); // glen
        enc.movz(arm64::X0, 0, 0);
        enc.str_imm(arm64::X0, arm64::FP, 40); // total
        enc.movz(arm64::X0, 0, 0);
        enc.str_imm(arm64::X0, arm64::FP, 56); // i

        enc.bindLabel(sumLoop);
        enc.ldr_imm(arm64::X6, arm64::FP, 56);
        enc.ldr_imm(arm64::X7, arm64::FP, 32);
        enc.cmp_rr(arm64::X6, arm64::X7);
        enc.b_cond(arm64::GE, sumDone);
        enc.movz(arm64::X8, 8, 0);
        enc.mul_rrr(arm64::X9, arm64::X6, arm64::X8);
        enc.ldr_imm(arm64::X10, arm64::FP, 24);
        enc.add_rrr(arm64::X10, arm64::X10, arm64::X9);
        enc.ldr_imm(arm64::X0, arm64::X10, 8);
        enc.bl(lblStrLen);
        enc.ldr_imm(arm64::X6, arm64::FP, 40);
        enc.add_rrr(arm64::X6, arm64::X6, arm64::X0);
        enc.str_imm(arm64::X6, arm64::FP, 40);
        enc.ldr_imm(arm64::X6, arm64::FP, 56);
        enc.add_rri(arm64::X6, arm64::X6, 1);
        enc.str_imm(arm64::X6, arm64::FP, 56);
        enc.b(sumLoop);

        enc.bindLabel(sumDone);
        enc.ldr_imm(arm64::X6, arm64::FP, 32);
        enc.sub_rri(arm64::X6, arm64::X6, 1);
        enc.ldr_imm(arm64::X7, arm64::FP, 64);
        enc.mul_rrr(arm64::X6, arm64::X6, arm64::X7);
        enc.ldr_imm(arm64::X7, arm64::FP, 40);
        enc.add_rrr(arm64::X7, arm64::X7, arm64::X6);
        enc.str_imm(arm64::X7, arm64::FP, 72); // totalLen
        enc.add_rri(arm64::X0, arm64::X7, 1);
        enc.bl(lblMalloc);
        enc.str_imm(arm64::X0, arm64::FP, 48); // out
        enc.movz(arm64::X0, 0, 0);
        enc.str_imm(arm64::X0, arm64::FP, 56); // i
        enc.str_imm(arm64::X0, arm64::FP, 80); // o

        enc.bindLabel(buildLoop);
        enc.ldr_imm(arm64::X6, arm64::FP, 56);
        enc.ldr_imm(arm64::X7, arm64::FP, 32);
        enc.cmp_rr(arm64::X6, arm64::X7);
        enc.b_cond(arm64::GE, buildDone);

        // elem = list[8 + i*8], len = strlen(elem)
        enc.movz(arm64::X8, 8, 0);
        enc.mul_rrr(arm64::X9, arm64::X6, arm64::X8);
        enc.ldr_imm(arm64::X10, arm64::FP, 24);
        enc.add_rrr(arm64::X10, arm64::X10, arm64::X9);
        enc.ldr_imm(arm64::X0, arm64::X10, 8);
        enc.str_imm(arm64::X0, arm64::FP, 88); // elem
        enc.bl(lblStrLen);
        enc.str_imm(arm64::X0, arm64::FP, 96); // len

        enc.ldr_imm(arm64::X0, arm64::FP, 48);
        enc.ldr_imm(arm64::X5, arm64::FP, 80);
        enc.add_rrr(arm64::X0, arm64::X0, arm64::X5);
        enc.ldr_imm(arm64::X1, arm64::FP, 88);
        enc.ldr_imm(arm64::X2, arm64::FP, 96);
        enc.bl(lblMemCpy);
        enc.ldr_imm(arm64::X5, arm64::FP, 80);
        enc.ldr_imm(arm64::X6, arm64::FP, 96);
        enc.add_rrr(arm64::X5, arm64::X5, arm64::X6);
        enc.str_imm(arm64::X5, arm64::FP, 80);

        enc.ldr_imm(arm64::X6, arm64::FP, 56);
        enc.ldr_imm(arm64::X7, arm64::FP, 32);
        enc.sub_rri(arm64::X7, arm64::X7, 1);
        enc.cmp_rr(arm64::X6, arm64::X7);
        enc.b_cond(arm64::GE, noGlueLbl);

        enc.ldr_imm(arm64::X0, arm64::FP, 48);
        enc.ldr_imm(arm64::X5, arm64::FP, 80);
        enc.add_rrr(arm64::X0, arm64::X0, arm64::X5);
        enc.ldr_imm(arm64::X1, arm64::FP, 16);
        enc.ldr_imm(arm64::X2, arm64::FP, 64);
        enc.bl(lblMemCpy);
        enc.ldr_imm(arm64::X5, arm64::FP, 80);
        enc.ldr_imm(arm64::X6, arm64::FP, 64);
        enc.add_rrr(arm64::X5, arm64::X5, arm64::X6);
        enc.str_imm(arm64::X5, arm64::FP, 80);

        enc.bindLabel(noGlueLbl);
        enc.ldr_imm(arm64::X6, arm64::FP, 56);
        enc.add_rri(arm64::X6, arm64::X6, 1);
        enc.str_imm(arm64::X6, arm64::FP, 56);
        enc.b(buildLoop);

        enc.bindLabel(buildDone);
        enc.ldr_imm(arm64::X0, arm64::FP, 48);
        enc.ldr_imm(arm64::X5, arm64::FP, 80);
        enc.add_rrr(arm64::X0, arm64::X0, arm64::X5);
        enc.movz(arm64::X6, 0, 0);
        enc.strb_imm(arm64::X6, arm64::X0, 0);
        enc.ldr_imm(arm64::X0, arm64::FP, 48);
        enc.ldp_post(arm64::FP, arm64::LR, arm64::SP, 112);
        enc.ret();

        enc.bindLabel(emptyLbl);
        enc.adr_data(arm64::X0, offEmptyStr);
        enc.ldp_post(arm64::FP, arm64::LR, arm64::SP, 112);
        enc.ret();
    }

    // ==========================================
    // list_in(list X0, item X1, isStr X2) -> 0/1
    // ==========================================
    void emitListIn() {
        enc.bindLabel(lblListIn);
        enc.stp_pre(arm64::FP, arm64::LR, arm64::SP, -80);
        enc.mov_rr(arm64::FP, arm64::SP);
        enc.str_imm(arm64::X0, arm64::FP, 16);
        enc.str_imm(arm64::X1, arm64::FP, 24);
        enc.str_imm(arm64::X2, arm64::FP, 32);

        size_t falseLbl = enc.createLabel();
        size_t trueLbl = enc.createLabel();
        size_t endLbl = enc.createLabel();
        size_t loopLbl = enc.createLabel();
        size_t intCase = enc.createLabel();
        size_t nextLbl = enc.createLabel();

        enc.cbz(arm64::X0, falseLbl);
        enc.ldr_imm(arm64::X6, arm64::X0, 0);
        enc.str_imm(arm64::X6, arm64::FP, 40); // count
        enc.movz(arm64::X0, 0, 0);
        enc.str_imm(arm64::X0, arm64::FP, 48); // i

        enc.bindLabel(loopLbl);
        enc.ldr_imm(arm64::X6, arm64::FP, 48);
        enc.ldr_imm(arm64::X7, arm64::FP, 40);
        enc.cmp_rr(arm64::X6, arm64::X7);
        enc.b_cond(arm64::GE, falseLbl);
        enc.movz(arm64::X8, 8, 0);
        enc.mul_rrr(arm64::X9, arm64::X6, arm64::X8);
        enc.ldr_imm(arm64::X10, arm64::FP, 16);
        enc.add_rrr(arm64::X10, arm64::X10, arm64::X9);
        enc.ldr_imm(arm64::X11, arm64::X10, 8); // elem
        enc.ldr_imm(arm64::X12, arm64::FP, 32);
        enc.cbz(arm64::X12, intCase);
        enc.mov_rr(arm64::X0, arm64::X11);
        enc.ldr_imm(arm64::X1, arm64::FP, 24);
        enc.bl(lblStrCmpEq);
        enc.cbnz(arm64::X0, trueLbl);
        enc.b(nextLbl);

        enc.bindLabel(intCase);
        enc.ldr_imm(arm64::X13, arm64::FP, 24);
        enc.cmp_rr(arm64::X11, arm64::X13);
        enc.b_cond(arm64::EQ, trueLbl);

        enc.bindLabel(nextLbl);
        enc.ldr_imm(arm64::X6, arm64::FP, 48);
        enc.add_rri(arm64::X6, arm64::X6, 1);
        enc.str_imm(arm64::X6, arm64::FP, 48);
        enc.b(loopLbl);

        enc.bindLabel(trueLbl);
        enc.movz(arm64::X0, 1, 0);
        enc.b(endLbl);
        enc.bindLabel(falseLbl);
        enc.movz(arm64::X0, 0, 0);
        enc.bindLabel(endLbl);
        enc.ldp_post(arm64::FP, arm64::LR, arm64::SP, 80);
        enc.ret();
    }

    // ==========================================
    // input_line(prompt X0, mode X1) -> string
    // mode: 0 = as-is, 1 = lower, 2 = upper
    // ==========================================
    void emitInputLine() {
        enc.bindLabel(lblInputLine);
        enc.stp_pre(arm64::FP, arm64::LR, arm64::SP, -64);
        enc.mov_rr(arm64::FP, arm64::SP);
        enc.str_imm(arm64::X0, arm64::FP, 16);
        enc.str_imm(arm64::X1, arm64::FP, 24);

        size_t noPrompt = enc.createLabel();
        size_t rloop = enc.createLabel();
        size_t rdone = enc.createLabel();
        size_t term = enc.createLabel();
        size_t finish = enc.createLabel();
        size_t lowLbl = enc.createLabel();
        size_t upLbl = enc.createLabel();

        enc.cbz(arm64::X0, noPrompt);
        enc.ldrb_imm(arm64::X2, arm64::X0, 0);
        enc.cbz(arm64::X2, noPrompt);
        enc.bl(lblPrintStr);

        enc.bindLabel(noPrompt);
        enc.mov_ri64(arm64::X0, 1024);
        enc.bl(lblMalloc);
        enc.str_imm(arm64::X0, arm64::FP, 32); // buf
        enc.movz(arm64::X0, 0, 0);
        enc.str_imm(arm64::X0, arm64::FP, 40); // i

        enc.bindLabel(rloop);
        enc.ldr_imm(arm64::X5, arm64::FP, 32);
        enc.ldr_imm(arm64::X6, arm64::FP, 40);
        enc.add_rrr(arm64::X1, arm64::X5, arm64::X6);
        enc.movz(arm64::X0, 0, 0);
        enc.movz(arm64::X2, 1, 0);
        enc.movz(arm64::X8, 63, 0); // read
        enc.svc(0);
        enc.cmp_ri(arm64::X0, 0);
        enc.b_cond(arm64::LE, rdone);
        enc.ldr_imm(arm64::X5, arm64::FP, 32);
        enc.ldr_imm(arm64::X6, arm64::FP, 40);
        enc.add_rrr(arm64::X9, arm64::X5, arm64::X6);
        enc.ldrb_imm(arm64::X10, arm64::X9, 0);
        enc.cmp_ri(arm64::X10, '\n');
        enc.b_cond(arm64::EQ, rdone);
        enc.add_rri(arm64::X6, arm64::X6, 1);
        enc.str_imm(arm64::X6, arm64::FP, 40);
        enc.cmp_ri(arm64::X6, 1000);
        enc.b_cond(arm64::GE, rdone);
        enc.b(rloop);

        enc.bindLabel(rdone);
        enc.ldr_imm(arm64::X5, arm64::FP, 32);
        enc.ldr_imm(arm64::X6, arm64::FP, 40);
        enc.cmp_ri(arm64::X6, 0);
        enc.b_cond(arm64::LE, term);
        enc.sub_rri(arm64::X9, arm64::X6, 1);
        enc.add_rrr(arm64::X9, arm64::X5, arm64::X9);
        enc.ldrb_imm(arm64::X10, arm64::X9, 0);
        enc.cmp_ri(arm64::X10, '\r');
        enc.b_cond(arm64::NE, term);
        enc.sub_rri(arm64::X6, arm64::X6, 1);

        enc.bindLabel(term);
        enc.add_rrr(arm64::X9, arm64::X5, arm64::X6);
        enc.movz(arm64::X10, 0, 0);
        enc.strb_imm(arm64::X10, arm64::X9, 0);

        enc.ldr_imm(arm64::X1, arm64::FP, 24);
        enc.cbz(arm64::X1, finish);
        enc.cmp_ri(arm64::X1, 1);
        enc.b_cond(arm64::EQ, lowLbl);
        enc.cmp_ri(arm64::X1, 2);
        enc.b_cond(arm64::EQ, upLbl);
        enc.b(finish);

        enc.bindLabel(lowLbl);
        enc.ldr_imm(arm64::X0, arm64::FP, 32);
        enc.bl(lblStrLower);
        enc.str_imm(arm64::X0, arm64::FP, 32);
        enc.b(finish);
        enc.bindLabel(upLbl);
        enc.ldr_imm(arm64::X0, arm64::FP, 32);
        enc.bl(lblStrUpper);
        enc.str_imm(arm64::X0, arm64::FP, 32);

        enc.bindLabel(finish);
        enc.ldr_imm(arm64::X0, arm64::FP, 32);
        enc.ldp_post(arm64::FP, arm64::LR, arm64::SP, 64);
        enc.ret();
    }

    // ==========================================
    // atoi(s X0) -> X0
    // ==========================================
    void emitAtoi() {
        enc.bindLabel(lblAtoi);
        size_t okLbl = enc.createLabel();
        size_t loopLbl = enc.createLabel();
        size_t doneLbl = enc.createLabel();
        size_t retLbl = enc.createLabel();
        size_t plusLbl = enc.createLabel();

        enc.cbnz(arm64::X0, okLbl);
        enc.movz(arm64::X0, 0, 0);
        enc.ret();

        enc.bindLabel(okLbl);
        enc.movz(arm64::X1, 0, 0);   // i
        enc.movz(arm64::X2, 0, 0);   // acc
        enc.movz(arm64::X5, 1, 0);   // sign
        enc.ldrb_imm(arm64::X3, arm64::X0, 0);
        enc.cmp_ri(arm64::X3, '-');
        enc.b_cond(arm64::NE, plusLbl);
        enc.movn(arm64::X5, 0, 0);
        enc.movz(arm64::X1, 1, 0);

        enc.bindLabel(plusLbl);
        enc.bindLabel(loopLbl);
        enc.add_rrr(arm64::X6, arm64::X0, arm64::X1);
        enc.ldrb_imm(arm64::X7, arm64::X6, 0);
        enc.cmp_ri(arm64::X7, '0');
        enc.b_cond(arm64::LT, doneLbl);
        enc.cmp_ri(arm64::X7, '9');
        enc.b_cond(arm64::GT, doneLbl);
        enc.sub_rri(arm64::X7, arm64::X7, '0');
        enc.movz(arm64::X8, 10, 0);
        enc.mul_rrr(arm64::X2, arm64::X2, arm64::X8);
        enc.add_rrr(arm64::X2, arm64::X2, arm64::X7);
        enc.add_rri(arm64::X1, arm64::X1, 1);
        enc.b(loopLbl);

        enc.bindLabel(doneLbl);
        enc.cmp_ri(arm64::X5, 0);
        enc.b_cond(arm64::GT, retLbl);
        enc.sub_rrr(arm64::X2, arm64::XZR, arm64::X2);
        enc.bindLabel(retLbl);
        enc.mov_rr(arm64::X0, arm64::X2);
        enc.ret();
    }

    // ==========================================
    // atod(s X0) -> X0 (double bit pattern)
    // ==========================================
    void emitAtoD() {
        enc.bindLabel(lblAtoD);
        enc.stp_pre(arm64::FP, arm64::LR, arm64::SP, -64);
        enc.mov_rr(arm64::FP, arm64::SP);

        size_t okLbl = enc.createLabel();
        size_t intLoop = enc.createLabel();
        size_t fracCheck = enc.createLabel();
        size_t fracLoop = enc.createLabel();
        size_t checkExp = enc.createLabel();
        size_t expStart = enc.createLabel();
        size_t expPlus = enc.createLabel();
        size_t expLoop = enc.createLabel();
        size_t expDone = enc.createLabel();
        size_t toDouble = enc.createLabel();
        size_t expMulLoop = enc.createLabel();
        size_t expDivLoop = enc.createLabel();
        size_t applyExpDone = enc.createLabel();
        size_t scaleLoop = enc.createLabel();
        size_t scaleDone = enc.createLabel();
        size_t saveLbl = enc.createLabel();
        size_t outLbl = enc.createLabel();
        size_t zeroLbl = enc.createLabel();

        enc.cbnz(arm64::X0, okLbl);
        enc.bindLabel(zeroLbl);
        enc.movz(arm64::X0, 0, 0);
        enc.ldp_post(arm64::FP, arm64::LR, arm64::SP, 64);
        enc.ret();

        enc.bindLabel(okLbl);
        enc.str_imm(arm64::X0, arm64::FP, 16);
        enc.movz(arm64::X1, 0, 0);
        enc.str_imm(arm64::X1, arm64::FP, 24);  // i
        enc.str_imm(arm64::X1, arm64::FP, 32);  // intAcc
        enc.str_imm(arm64::X1, arm64::FP, 40);  // fracNum
        enc.str_imm(arm64::X1, arm64::FP, 48);  // fracDig
        enc.movz(arm64::X2, 1, 0);
        enc.str_imm(arm64::X2, arm64::FP, 56);  // sign
        enc.movz(arm64::X10, 0, 0);             // exponent value
        enc.movz(arm64::X11, 0, 0);             // exponent is negative

        enc.ldrb_imm(arm64::X3, arm64::X0, 0);
        enc.cmp_ri(arm64::X3, '-');
        enc.b_cond(arm64::NE, intLoop);
        enc.movn(arm64::X2, 0, 0);
        enc.str_imm(arm64::X2, arm64::FP, 56);
        enc.movz(arm64::X1, 1, 0);
        enc.str_imm(arm64::X1, arm64::FP, 24);

        enc.bindLabel(intLoop);
        enc.ldr_imm(arm64::X6, arm64::FP, 16);
        enc.ldr_imm(arm64::X1, arm64::FP, 24);
        enc.add_rrr(arm64::X6, arm64::X6, arm64::X1);
        enc.ldrb_imm(arm64::X7, arm64::X6, 0);
        enc.cmp_ri(arm64::X7, '0');
        enc.b_cond(arm64::LT, fracCheck);
        enc.cmp_ri(arm64::X7, '9');
        enc.b_cond(arm64::GT, fracCheck);
        enc.sub_rri(arm64::X7, arm64::X7, '0');
        enc.ldr_imm(arm64::X8, arm64::FP, 32);
        enc.movz(arm64::X9, 10, 0);
        enc.mul_rrr(arm64::X8, arm64::X8, arm64::X9);
        enc.add_rrr(arm64::X8, arm64::X8, arm64::X7);
        enc.str_imm(arm64::X8, arm64::FP, 32);
        enc.add_rri(arm64::X1, arm64::X1, 1);
        enc.str_imm(arm64::X1, arm64::FP, 24);
        enc.b(intLoop);

        enc.bindLabel(fracCheck);
        enc.cmp_ri(arm64::X7, '.');
        enc.b_cond(arm64::NE, checkExp);
        enc.ldr_imm(arm64::X1, arm64::FP, 24);
        enc.add_rri(arm64::X1, arm64::X1, 1);
        enc.str_imm(arm64::X1, arm64::FP, 24);

        enc.bindLabel(fracLoop);
        enc.ldr_imm(arm64::X6, arm64::FP, 16);
        enc.ldr_imm(arm64::X1, arm64::FP, 24);
        enc.add_rrr(arm64::X6, arm64::X6, arm64::X1);
        enc.ldrb_imm(arm64::X7, arm64::X6, 0);
        enc.cmp_ri(arm64::X7, '0');
        enc.b_cond(arm64::LT, checkExp);
        enc.cmp_ri(arm64::X7, '9');
        enc.b_cond(arm64::GT, checkExp);
        enc.sub_rri(arm64::X7, arm64::X7, '0');
        enc.ldr_imm(arm64::X8, arm64::FP, 40);
        enc.movz(arm64::X9, 10, 0);
        enc.mul_rrr(arm64::X8, arm64::X8, arm64::X9);
        enc.add_rrr(arm64::X8, arm64::X8, arm64::X7);
        enc.str_imm(arm64::X8, arm64::FP, 40);
        enc.ldr_imm(arm64::X8, arm64::FP, 48);
        enc.add_rri(arm64::X8, arm64::X8, 1);
        enc.str_imm(arm64::X8, arm64::FP, 48);
        enc.add_rri(arm64::X1, arm64::X1, 1);
        enc.str_imm(arm64::X1, arm64::FP, 24);
        enc.b(fracLoop);

        // Optional exponent: e / E, optional sign, decimal digits.
        enc.bindLabel(checkExp);
        enc.cmp_ri(arm64::X7, 'e');
        enc.b_cond(arm64::EQ, expStart);
        enc.cmp_ri(arm64::X7, 'E');
        enc.b_cond(arm64::NE, toDouble);

        enc.bindLabel(expStart);
        enc.ldr_imm(arm64::X1, arm64::FP, 24);
        enc.add_rri(arm64::X1, arm64::X1, 1);
        enc.str_imm(arm64::X1, arm64::FP, 24);
        enc.ldr_imm(arm64::X6, arm64::FP, 16);
        enc.add_rrr(arm64::X6, arm64::X6, arm64::X1);
        enc.ldrb_imm(arm64::X7, arm64::X6, 0);
        enc.cmp_ri(arm64::X7, '-');
        enc.b_cond(arm64::NE, expPlus);
        enc.movz(arm64::X11, 1, 0);
        enc.add_rri(arm64::X1, arm64::X1, 1);
        enc.str_imm(arm64::X1, arm64::FP, 24);
        enc.ldr_imm(arm64::X6, arm64::FP, 16);
        enc.add_rrr(arm64::X6, arm64::X6, arm64::X1);
        enc.ldrb_imm(arm64::X7, arm64::X6, 0);
        enc.b(expLoop);

        enc.bindLabel(expPlus);
        enc.cmp_ri(arm64::X7, '+');
        enc.b_cond(arm64::NE, expLoop);
        enc.add_rri(arm64::X1, arm64::X1, 1);
        enc.str_imm(arm64::X1, arm64::FP, 24);
        enc.ldr_imm(arm64::X6, arm64::FP, 16);
        enc.add_rrr(arm64::X6, arm64::X6, arm64::X1);
        enc.ldrb_imm(arm64::X7, arm64::X6, 0);

        enc.bindLabel(expLoop);
        enc.cmp_ri(arm64::X7, '0');
        enc.b_cond(arm64::LT, expDone);
        enc.cmp_ri(arm64::X7, '9');
        enc.b_cond(arm64::GT, expDone);
        enc.sub_rri(arm64::X7, arm64::X7, '0');
        enc.movz(arm64::X9, 10, 0);
        enc.mul_rrr(arm64::X10, arm64::X10, arm64::X9);
        enc.add_rrr(arm64::X10, arm64::X10, arm64::X7);
        enc.ldr_imm(arm64::X1, arm64::FP, 24);
        enc.add_rri(arm64::X1, arm64::X1, 1);
        enc.str_imm(arm64::X1, arm64::FP, 24);
        enc.ldr_imm(arm64::X6, arm64::FP, 16);
        enc.add_rrr(arm64::X6, arm64::X6, arm64::X1);
        enc.ldrb_imm(arm64::X7, arm64::X6, 0);
        enc.b(expLoop);

        // Clamp the exponent so absurd input cannot spin the scaling loop.
        enc.bindLabel(expDone);
        enc.mov_ri64(arm64::X9, 400);
        enc.cmp_rr(arm64::X10, arm64::X9);
        enc.b_cond(arm64::LE, toDouble);
        enc.mov_rr(arm64::X10, arm64::X9);

        enc.bindLabel(toDouble);
        enc.ldr_imm(arm64::X2, arm64::FP, 32);
        enc.scvtf_dr(arm64::D0, arm64::X2);
        enc.movz(arm64::X3, 1, 0);
        enc.mov_ri64(arm64::X4, 10);
        enc.ldr_imm(arm64::X5, arm64::FP, 48);
        enc.bindLabel(scaleLoop);
        enc.cbz(arm64::X5, scaleDone);
        enc.mul_rrr(arm64::X3, arm64::X3, arm64::X4);
        enc.sub_rri(arm64::X5, arm64::X5, 1);
        enc.b(scaleLoop);

        enc.bindLabel(scaleDone);
        enc.ldr_imm(arm64::X2, arm64::FP, 40);
        enc.scvtf_dr(arm64::D1, arm64::X2);
        enc.scvtf_dr(arm64::D2, arm64::X3);
        enc.fdiv_ddd(arm64::D1, arm64::D1, arm64::D2);
        enc.fadd_ddd(arm64::D0, arm64::D0, arm64::D1);

        // Apply the exponent.
        enc.cbz(arm64::X10, applyExpDone);
        enc.movz(arm64::X4, 10, 0);
        enc.scvtf_dr(arm64::D3, arm64::X4);
        enc.cmp_ri(arm64::X11, 1);
        enc.b_cond(arm64::EQ, expDivLoop);
        enc.bindLabel(expMulLoop);
        enc.fmul_ddd(arm64::D0, arm64::D0, arm64::D3);
        enc.sub_rri(arm64::X10, arm64::X10, 1);
        enc.cbz(arm64::X10, applyExpDone);
        enc.b(expMulLoop);
        enc.bindLabel(expDivLoop);
        enc.fdiv_ddd(arm64::D0, arm64::D0, arm64::D3);
        enc.sub_rri(arm64::X10, arm64::X10, 1);
        enc.cbz(arm64::X10, applyExpDone);
        enc.b(expDivLoop);

        enc.bindLabel(applyExpDone);
        enc.fstr_imm(arm64::D0, arm64::FP, 32);
        enc.ldr_imm(arm64::X0, arm64::FP, 32);
        enc.ldr_imm(arm64::X9, arm64::FP, 56);
        enc.cmp_ri(arm64::X9, 0);
        enc.b_cond(arm64::GT, saveLbl);
        enc.movz(arm64::X1, 0, 0);
        enc.movk(arm64::X1, 0x8000, 48);
        enc.eor_rrr(arm64::X0, arm64::X0, arm64::X1);

        enc.bindLabel(saveLbl);
        enc.bindLabel(outLbl);
        enc.ldp_post(arm64::FP, arm64::LR, arm64::SP, 64);
        enc.ret();
    }

    // ==========================================
    // rand() -> X0 in [0, 32767]
    // ==========================================
    void emitRand() {
        enc.bindLabel(lblRand);
        enc.stp_pre(arm64::FP, arm64::LR, arm64::SP, -48);
        enc.mov_rr(arm64::FP, arm64::SP);
        enc.add_rri(arm64::X0, arm64::X29, 16);
        enc.movz(arm64::X1, 0, 0);
        enc.movz(arm64::X8, 113, 0); // clock_gettime
        enc.svc(0);
        enc.ldr_imm(arm64::X2, arm64::X29, 16); // tv_sec
        enc.ldr_imm(arm64::X3, arm64::X29, 24); // tv_nsec
        enc.lsl_ri(arm64::X4, arm64::X2, 16);
        enc.eor_rrr(arm64::X2, arm64::X3, arm64::X4);
        enc.lsl_ri(arm64::X4, arm64::X2, 13);
        enc.eor_rrr(arm64::X2, arm64::X2, arm64::X4);
        enc.lsr_ri(arm64::X4, arm64::X2, 7);
        enc.eor_rrr(arm64::X2, arm64::X2, arm64::X4);
        enc.lsl_ri(arm64::X4, arm64::X2, 17);
        enc.eor_rrr(arm64::X2, arm64::X2, arm64::X4);
        enc.movz(arm64::X4, 0x7FFF, 0);
        enc.and_rrr(arm64::X0, arm64::X2, arm64::X4);
        enc.ldp_post(arm64::FP, arm64::LR, arm64::SP, 48);
        enc.ret();
    }

    // ==========================================
    // rand_choice(list X0) -> random element (string)
    // ==========================================
    void emitRandChoice() {
        enc.bindLabel(lblRandChoice);
        enc.stp_pre(arm64::FP, arm64::LR, arm64::SP, -64);
        enc.mov_rr(arm64::FP, arm64::SP);
        size_t emptyLbl = enc.createLabel();
        enc.cbz(arm64::X0, emptyLbl);
        enc.str_imm(arm64::X0, arm64::FP, 16);
        enc.ldr_imm(arm64::X6, arm64::X0, 0);
        enc.cbz(arm64::X6, emptyLbl);
        enc.str_imm(arm64::X6, arm64::FP, 24); // count
        enc.bl(lblRand);
        enc.ldr_imm(arm64::X6, arm64::FP, 24);
        enc.udiv_rrr(arm64::X7, arm64::X0, arm64::X6);
        enc.mul_rrr(arm64::X7, arm64::X7, arm64::X6);
        enc.sub_rrr(arm64::X0, arm64::X0, arm64::X7);
        enc.movz(arm64::X8, 8, 0);
        enc.mul_rrr(arm64::X9, arm64::X0, arm64::X8);
        enc.ldr_imm(arm64::X10, arm64::FP, 16);
        enc.add_rrr(arm64::X10, arm64::X10, arm64::X9);
        enc.ldr_imm(arm64::X0, arm64::X10, 8);
        enc.ldp_post(arm64::FP, arm64::LR, arm64::SP, 64);
        enc.ret();
        enc.bindLabel(emptyLbl);
        enc.adr_data(arm64::X0, offEmptyStr);
        enc.ldp_post(arm64::FP, arm64::LR, arm64::SP, 64);
        enc.ret();
    }

    // ==========================================
    // getenv(name X0) -> string (from /proc/self/environ)
    // ==========================================
    void emitGetenv() {
        enc.bindLabel(lblGetenv);
        enc.stp_pre(arm64::FP, arm64::LR, arm64::SP, -112);
        enc.mov_rr(arm64::FP, arm64::SP);

        size_t okLbl = enc.createLabel();
        size_t emptyLbl = enc.createLabel();
        size_t closeEmpty = enc.createLabel();
        size_t scanLoop = enc.createLabel();
        size_t cmpLoop = enc.createLabel();
        size_t matched = enc.createLabel();
        size_t nextEntry = enc.createLabel();
        size_t advLbl = enc.createLabel();
        size_t advDone = enc.createLabel();
        size_t doneLbl = enc.createLabel();

        enc.cbnz(arm64::X0, okLbl);
        enc.b(emptyLbl);

        enc.bindLabel(okLbl);
        enc.str_imm(arm64::X0, arm64::FP, 16); // name
        enc.movn(arm64::X0, 99, 0);            // AT_FDCWD = -100
        enc.adr_data(arm64::X1, offStrProcEnviron);
        enc.movz(arm64::X2, 0, 0);
        enc.movz(arm64::X3, 0, 0);
        enc.movz(arm64::X8, 56, 0); // openat
        enc.svc(0);
        enc.cmp_ri(arm64::X0, 0);
        enc.b_cond(arm64::LT, emptyLbl);
        enc.str_imm(arm64::X0, arm64::FP, 24); // fd

        enc.mov_ri64(arm64::X0, 65536);
        enc.bl(lblMalloc);
        enc.str_imm(arm64::X0, arm64::FP, 40); // buf
        enc.ldr_imm(arm64::X0, arm64::FP, 24);
        enc.ldr_imm(arm64::X1, arm64::FP, 40);
        enc.mov_ri64(arm64::X2, 65535);
        enc.movz(arm64::X8, 63, 0); // read
        enc.svc(0);
        enc.cmp_ri(arm64::X0, 0);
        enc.b_cond(arm64::LE, closeEmpty);
        enc.str_imm(arm64::X0, arm64::FP, 32); // total

        enc.ldr_imm(arm64::X0, arm64::FP, 24);
        enc.movz(arm64::X8, 57, 0); // close
        enc.svc(0);
        enc.ldr_imm(arm64::X0, arm64::FP, 40);
        enc.ldr_imm(arm64::X6, arm64::FP, 32);
        enc.add_rrr(arm64::X0, arm64::X0, arm64::X6);
        enc.movz(arm64::X7, 0, 0);
        enc.strb_imm(arm64::X7, arm64::X0, 0);

        enc.ldr_imm(arm64::X0, arm64::FP, 40);
        enc.str_imm(arm64::X0, arm64::FP, 48); // p

        enc.bindLabel(scanLoop);
        enc.ldr_imm(arm64::X6, arm64::FP, 48);
        enc.ldrb_imm(arm64::X7, arm64::X6, 0);
        enc.cbz(arm64::X7, emptyLbl);

        enc.ldr_imm(arm64::X0, arm64::FP, 16);
        enc.bl(lblStrLen);
        enc.mov_rr(arm64::X8, arm64::X0); // nameLen
        enc.ldr_imm(arm64::X6, arm64::FP, 48);
        enc.movz(arm64::X9, 0, 0);

        enc.bindLabel(cmpLoop);
        enc.cmp_rr(arm64::X9, arm64::X8);
        enc.b_cond(arm64::GE, matched);
        enc.add_rrr(arm64::X10, arm64::X6, arm64::X9);
        enc.ldrb_imm(arm64::X11, arm64::X10, 0);
        enc.ldr_imm(arm64::X12, arm64::FP, 16);
        enc.add_rrr(arm64::X12, arm64::X12, arm64::X9);
        enc.ldrb_imm(arm64::X13, arm64::X12, 0);
        enc.cmp_rr(arm64::X11, arm64::X13);
        enc.b_cond(arm64::NE, nextEntry);
        enc.add_rri(arm64::X9, arm64::X9, 1);
        enc.b(cmpLoop);

        enc.bindLabel(matched);
        enc.add_rrr(arm64::X10, arm64::X6, arm64::X8);
        enc.ldrb_imm(arm64::X11, arm64::X10, 0);
        enc.cmp_ri(arm64::X11, '=');
        enc.b_cond(arm64::NE, nextEntry);
        enc.add_rri(arm64::X0, arm64::X10, 1);
        enc.b(doneLbl);

        enc.bindLabel(nextEntry);
        enc.ldr_imm(arm64::X6, arm64::FP, 48);
        enc.bindLabel(advLbl);
        enc.ldrb_imm(arm64::X7, arm64::X6, 0);
        enc.cbz(arm64::X7, advDone);
        enc.add_rri(arm64::X6, arm64::X6, 1);
        enc.b(advLbl);
        enc.bindLabel(advDone);
        enc.add_rri(arm64::X6, arm64::X6, 1);
        enc.str_imm(arm64::X6, arm64::FP, 48);
        enc.b(scanLoop);

        enc.bindLabel(closeEmpty);
        enc.ldr_imm(arm64::X0, arm64::FP, 24);
        enc.movz(arm64::X8, 57, 0);
        enc.svc(0);
        enc.bindLabel(emptyLbl);
        enc.adr_data(arm64::X0, offEmptyStr);

        enc.bindLabel(doneLbl);
        enc.ldp_post(arm64::FP, arm64::LR, arm64::SP, 112);
        enc.ret();
    }

    // ==========================================
    // dict_find(dict X0, key X1, isStr X2) -> index or -1
    // Entry layout (x86-compatible): [count][cap] then 32-byte entries at
    // 16 + i*32: +0 key, +8 value, +16 keyType, +24 valType
    // ==========================================
    void emitDictFind() {
        enc.bindLabel(lblDictFind);
        enc.stp_pre(arm64::FP, arm64::LR, arm64::SP, -80);
        enc.mov_rr(arm64::FP, arm64::SP);
        enc.str_imm(arm64::X0, arm64::FP, 16);
        enc.str_imm(arm64::X1, arm64::FP, 24);
        enc.str_imm(arm64::X2, arm64::FP, 32);

        size_t notFound = enc.createLabel();
        size_t endLbl = enc.createLabel();
        size_t loopLbl = enc.createLabel();
        size_t intCmp = enc.createLabel();
        size_t foundLbl = enc.createLabel();
        size_t nextLbl = enc.createLabel();

        enc.cbz(arm64::X0, notFound);
        enc.ldr_imm(arm64::X6, arm64::X0, 0);
        enc.str_imm(arm64::X6, arm64::FP, 40); // count
        enc.movz(arm64::X0, 0, 0);
        enc.str_imm(arm64::X0, arm64::FP, 48); // i

        enc.bindLabel(loopLbl);
        enc.ldr_imm(arm64::X6, arm64::FP, 48);
        enc.ldr_imm(arm64::X7, arm64::FP, 40);
        enc.cmp_rr(arm64::X6, arm64::X7);
        enc.b_cond(arm64::GE, notFound);

        enc.movz(arm64::X8, 32, 0);
        enc.mul_rrr(arm64::X9, arm64::X6, arm64::X8);
        enc.ldr_imm(arm64::X10, arm64::FP, 16);
        enc.add_rrr(arm64::X10, arm64::X10, arm64::X9);
        enc.ldr_imm(arm64::X11, arm64::X10, 16); // entry key

        enc.ldr_imm(arm64::X12, arm64::FP, 32);
        enc.cbz(arm64::X12, intCmp);
        enc.mov_rr(arm64::X0, arm64::X11);
        enc.ldr_imm(arm64::X1, arm64::FP, 24);
        enc.bl(lblStrCmpEq);
        enc.cbnz(arm64::X0, foundLbl);
        enc.b(nextLbl);

        enc.bindLabel(intCmp);
        enc.ldr_imm(arm64::X13, arm64::FP, 24);
        enc.cmp_rr(arm64::X11, arm64::X13);
        enc.b_cond(arm64::EQ, foundLbl);

        enc.bindLabel(nextLbl);
        enc.ldr_imm(arm64::X6, arm64::FP, 48);
        enc.add_rri(arm64::X6, arm64::X6, 1);
        enc.str_imm(arm64::X6, arm64::FP, 48);
        enc.b(loopLbl);

        enc.bindLabel(foundLbl);
        enc.ldr_imm(arm64::X0, arm64::FP, 48);
        enc.b(endLbl);
        enc.bindLabel(notFound);
        enc.movn(arm64::X0, 0, 0);
        enc.bindLabel(endLbl);
        enc.ldp_post(arm64::FP, arm64::LR, arm64::SP, 80);
        enc.ret();
    }

    // ==========================================
    // file_make(fd X0) -> handle
    // handle: [0]=fd [8]=buf [16]=cap [24]=pos [32]=len
    // ==========================================
    void emitFileMake() {
        enc.bindLabel(lblFileMake);
        enc.stp_pre(arm64::FP, arm64::LR, arm64::SP, -64);
        enc.mov_rr(arm64::FP, arm64::SP);
        enc.str_imm(arm64::X0, arm64::FP, 16); // fd
        enc.mov_ri64(arm64::X0, 40);
        enc.bl(lblMalloc);
        enc.str_imm(arm64::X0, arm64::FP, 24); // handle
        enc.ldr_imm(arm64::X1, arm64::FP, 16);
        enc.str_imm(arm64::X1, arm64::X0, 0);
        enc.mov_ri64(arm64::X0, 4096);
        enc.bl(lblMalloc);
        enc.str_imm(arm64::X0, arm64::FP, 32);
        enc.ldr_imm(arm64::X0, arm64::FP, 24);
        enc.ldr_imm(arm64::X1, arm64::FP, 32);
        enc.str_imm(arm64::X1, arm64::X0, 8);
        enc.mov_ri64(arm64::X1, 4096);
        enc.str_imm(arm64::X1, arm64::X0, 16);
        enc.movz(arm64::X1, 0, 0);
        enc.str_imm(arm64::X1, arm64::X0, 24);
        enc.str_imm(arm64::X1, arm64::X0, 32);
        enc.ldr_imm(arm64::X0, arm64::FP, 24);
        enc.ldp_post(arm64::FP, arm64::LR, arm64::SP, 64);
        enc.ret();
    }

    // ==========================================
    // file_open(path X0, mode X1) -> handle or 0
    // mode: 'w' -> O_WRONLY|O_CREAT|O_TRUNC, 'a' -> ...|O_APPEND, else O_RDONLY
    // ==========================================
    void emitFileOpen() {
        enc.bindLabel(lblFileOpen);
        enc.stp_pre(arm64::FP, arm64::LR, arm64::SP, -64);
        enc.mov_rr(arm64::FP, arm64::SP);
        enc.str_imm(arm64::X0, arm64::FP, 16);
        enc.str_imm(arm64::X1, arm64::FP, 24);

        size_t failLbl = enc.createLabel();
        size_t flagsDone = enc.createLabel();
        size_t notW = enc.createLabel();
        size_t endLbl = enc.createLabel();

        enc.cbz(arm64::X0, failLbl);
        enc.movz(arm64::X0, 0, 0);
        enc.str_imm(arm64::X0, arm64::FP, 32);
        enc.ldr_imm(arm64::X1, arm64::FP, 24);
        enc.cbz(arm64::X1, flagsDone);
        enc.ldrb_imm(arm64::X2, arm64::X1, 0);
        enc.cmp_ri(arm64::X2, 'w');
        enc.b_cond(arm64::NE, notW);
        enc.mov_ri64(arm64::X0, 0x241);
        enc.b(flagsDone);
        enc.bindLabel(notW);
        enc.cmp_ri(arm64::X2, 'a');
        enc.b_cond(arm64::NE, flagsDone);
        enc.mov_ri64(arm64::X0, 0x441);

        enc.bindLabel(flagsDone);
        enc.str_imm(arm64::X0, arm64::FP, 32);
        enc.movn(arm64::X0, 99, 0);
        enc.ldr_imm(arm64::X1, arm64::FP, 16);
        enc.ldr_imm(arm64::X2, arm64::FP, 32);
        enc.mov_ri64(arm64::X3, 0x1A4);
        enc.movz(arm64::X8, 56, 0); // openat
        enc.svc(0);
        enc.cmp_ri(arm64::X0, 0);
        enc.b_cond(arm64::LT, failLbl);
        enc.bl(lblFileMake);
        enc.b(endLbl);

        enc.bindLabel(failLbl);
        enc.movz(arm64::X0, 0, 0);
        enc.bindLabel(endLbl);
        enc.ldp_post(arm64::FP, arm64::LR, arm64::SP, 64);
        enc.ret();
    }

    // file_close(handle X0)
    void emitFileClose() {
        enc.bindLabel(lblFileClose);
        size_t endLbl = enc.createLabel();
        enc.cbz(arm64::X0, endLbl);
        enc.ldr_imm(arm64::X0, arm64::X0, 0);
        enc.movz(arm64::X8, 57, 0);
        enc.svc(0);
        enc.bindLabel(endLbl);
        enc.ret();
    }

    // file_write(handle X0, buf X1, len X2)
    void emitFileWrite() {
        enc.bindLabel(lblFileWrite);
        enc.stp_pre(arm64::FP, arm64::LR, arm64::SP, -64);
        enc.mov_rr(arm64::FP, arm64::SP);
        enc.str_imm(arm64::X0, arm64::FP, 16);
        enc.str_imm(arm64::X1, arm64::FP, 24);
        enc.str_imm(arm64::X2, arm64::FP, 32);

        size_t loopLbl = enc.createLabel();
        size_t doneLbl = enc.createLabel();
        enc.bindLabel(loopLbl);
        enc.ldr_imm(arm64::X6, arm64::FP, 32);
        enc.cbz(arm64::X6, doneLbl);
        enc.ldr_imm(arm64::X0, arm64::FP, 16);
        enc.ldr_imm(arm64::X0, arm64::X0, 0);
        enc.ldr_imm(arm64::X1, arm64::FP, 24);
        enc.mov_rr(arm64::X2, arm64::X6);
        enc.movz(arm64::X8, 64, 0); // write
        enc.svc(0);
        enc.cmp_ri(arm64::X0, 0);
        enc.b_cond(arm64::LE, doneLbl);
        enc.ldr_imm(arm64::X7, arm64::FP, 24);
        enc.add_rrr(arm64::X7, arm64::X7, arm64::X0);
        enc.str_imm(arm64::X7, arm64::FP, 24);
        enc.ldr_imm(arm64::X7, arm64::FP, 32);
        enc.sub_rrr(arm64::X7, arm64::X7, arm64::X0);
        enc.str_imm(arm64::X7, arm64::FP, 32);
        enc.b(loopLbl);

        enc.bindLabel(doneLbl);
        enc.movz(arm64::X0, 0, 0);
        enc.ldp_post(arm64::FP, arm64::LR, arm64::SP, 64);
        enc.ret();
    }

    // ==========================================
    // int_to_buf(buf X0, val X1) -> X0 = end pointer
    // ==========================================
    // int_to_str(X0 = value) -> X0 = heap string
    void emitIntToStr() {
        enc.bindLabel(lblIntToStr);
        enc.stp_pre(arm64::FP, arm64::LR, arm64::SP, -32);
        enc.mov_rr(arm64::FP, arm64::SP);
        enc.str_imm(arm64::X0, arm64::FP, 16);
        enc.mov_ri64(arm64::X0, 32);
        enc.bl(lblMalloc);
        enc.str_imm(arm64::X0, arm64::FP, 24); // keep the buffer start
        enc.ldr_imm(arm64::X1, arm64::FP, 16);
        enc.bl(lblIntToBuf); // returns one past the last digit
        enc.ldr_imm(arm64::X0, arm64::FP, 24);
        enc.ldp_post(arm64::FP, arm64::LR, arm64::SP, 32);
        enc.ret();
    }

    void emitIntToBuf() {
        enc.bindLabel(lblIntToBuf);
        // 80-byte scratch; the top (SP+64) stays below the caller's SP so the
        // caller's saved FP/LR (stored at [callerSP]) is never touched.
        enc.sub_rri(arm64::SP, arm64::SP, 80);
        enc.mov_rr(arm64::X9, arm64::X0);   // dst
        enc.mov_rr(arm64::X2, arm64::X1);   // val
        enc.movz(arm64::X10, 0, 0);         // neg flag
        size_t posLbl = enc.createLabel();
        size_t digLoop = enc.createLabel();
        size_t noMinus = enc.createLabel();
        size_t copyLoop = enc.createLabel();
        size_t finLbl = enc.createLabel();

        enc.cmp_ri(arm64::X2, 0);
        enc.b_cond(arm64::GE, posLbl);
        enc.movz(arm64::X10, 1, 0);
        enc.sub_rrr(arm64::X2, arm64::XZR, arm64::X2);

        enc.bindLabel(posLbl);
        enc.add_rri(arm64::X11, arm64::SP, 64);
        enc.movz(arm64::X15, 0, 0);
        enc.strb_imm(arm64::X15, arm64::X11, 0); // NUL terminator at the top
        enc.movz(arm64::X12, 10, 0);
        enc.bindLabel(digLoop);
        enc.sub_rri(arm64::X11, arm64::X11, 1);
        enc.udiv_rrr(arm64::X13, arm64::X2, arm64::X12);
        enc.mul_rrr(arm64::X14, arm64::X13, arm64::X12);
        enc.sub_rrr(arm64::X14, arm64::X2, arm64::X14);
        enc.add_rri(arm64::X14, arm64::X14, '0');
        enc.strb_imm(arm64::X14, arm64::X11, 0);
        enc.mov_rr(arm64::X2, arm64::X13);
        enc.cbnz(arm64::X2, digLoop);

        enc.cbz(arm64::X10, noMinus);
        enc.sub_rri(arm64::X11, arm64::X11, 1);
        enc.movz(arm64::X14, '-', 0);
        enc.strb_imm(arm64::X14, arm64::X11, 0);

        enc.bindLabel(noMinus);
        enc.bindLabel(copyLoop);
        enc.ldrb_imm(arm64::X14, arm64::X11, 0);
        enc.strb_imm(arm64::X14, arm64::X9, 0);
        enc.cbz(arm64::X14, finLbl);
        enc.add_rri(arm64::X11, arm64::X11, 1);
        enc.add_rri(arm64::X9, arm64::X9, 1);
        enc.b(copyLoop);

        enc.bindLabel(finLbl);
        enc.mov_rr(arm64::X0, arm64::X9);
        enc.add_rri(arm64::SP, arm64::SP, 80);
        enc.ret();
    }

    // file_write_int(handle X0, val X1)
    void emitFileWriteInt() {
        enc.bindLabel(lblFileWriteInt);
        enc.stp_pre(arm64::FP, arm64::LR, arm64::SP, -64);
        enc.mov_rr(arm64::FP, arm64::SP);
        enc.str_imm(arm64::X0, arm64::FP, 16);
        enc.add_rri(arm64::X0, arm64::FP, 32);
        enc.bl(lblIntToBuf);
        enc.add_rri(arm64::X3, arm64::FP, 32);
        enc.sub_rrr(arm64::X2, arm64::X0, arm64::X3);
        enc.ldr_imm(arm64::X0, arm64::FP, 16);
        enc.mov_rr(arm64::X1, arm64::X3);
        enc.bl(lblFileWrite);
        enc.ldp_post(arm64::FP, arm64::LR, arm64::SP, 64);
        enc.ret();
    }

    // ==========================================
    // file_fmt_double(bits X0) -> X0 = malloc'd string (same %g-style as printing)
    // ==========================================
    void emitFileFmtDouble() {
        enc.bindLabel(lblFileFmtDouble);
        enc.stp_pre(arm64::FP, arm64::LR, arm64::SP, -192);
        enc.mov_rr(arm64::FP, arm64::SP);
        enc.str_imm(arm64::X0, arm64::FP, 16); // raw bits

        size_t noSign = enc.createLabel();
        size_t bigCase = enc.createLabel();
        size_t zeroCase = enc.createLabel();
        size_t powLoop = enc.createLabel();
        size_t powDone = enc.createLabel();
        size_t hasDec = enc.createLabel();
        size_t scaledOk = enc.createLabel();
        size_t digLoop = enc.createLabel();
        size_t trimLoop = enc.createLabel();
        size_t finLbl = enc.createLabel();

        enc.mov_ri64(arm64::X0, 64);
        enc.bl(lblMalloc);
        enc.str_imm(arm64::X0, arm64::FP, 24); // out
        enc.movz(arm64::X0, 0, 0);
        enc.str_imm(arm64::X0, arm64::FP, 32); // o

        enc.ldr_imm(arm64::X0, arm64::FP, 16);
        enc.movz(arm64::X1, 0, 0);
        enc.movk(arm64::X1, 0x8000, 48);
        enc.and_rrr(arm64::X2, arm64::X0, arm64::X1);
        enc.cbz(arm64::X2, noSign);
        enc.ldr_imm(arm64::X0, arm64::FP, 24);
        enc.movz(arm64::X1, '-', 0);
        enc.strb_imm(arm64::X1, arm64::X0, 0);
        enc.movz(arm64::X1, 1, 0);
        enc.str_imm(arm64::X1, arm64::FP, 32);

        enc.bindLabel(noSign);
        enc.ldr_imm(arm64::X0, arm64::FP, 16);
        enc.movz(arm64::X1, 0xFFFF, 0);
        enc.movk(arm64::X1, 0xFFFF, 16);
        enc.movk(arm64::X1, 0xFFFF, 32);
        enc.movk(arm64::X1, 0x7FFF, 48);
        enc.and_rrr(arm64::X0, arm64::X0, arm64::X1);
        enc.str_imm(arm64::X0, arm64::FP, 16);
        enc.fldr_imm(arm64::D0, arm64::FP, 16);

        enc.adr_data(arm64::X16, offDoubleHuge);
        enc.fldr_imm(arm64::D1, arm64::X16, 0);
        enc.fcmp_dd(arm64::D0, arm64::D1);
        enc.b_cond(arm64::GE, bigCase);

        enc.adr_data(arm64::X16, offDoubleMillion);
        enc.fldr_imm(arm64::D1, arm64::X16, 0);
        enc.fmul_ddd(arm64::D1, arm64::D0, arm64::D1);
        enc.adr_data(arm64::X16, offDoubleHalf);
        enc.fldr_imm(arm64::D2, arm64::X16, 0);
        enc.fadd_ddd(arm64::D1, arm64::D1, arm64::D2);
        enc.fcvtzs_rd(arm64::X3, arm64::D1);
        enc.cbz(arm64::X3, zeroCase);

        // Fixed six decimal places (trailing zeros are trimmed below), the same
        // rule the x86-64 PE/ELF and --cpp backends use for <str>(double).
        enc.mov_ri64(arm64::X12, 6);
        enc.str_imm(arm64::X12, arm64::FP, 40); // d

        enc.mov_ri64(arm64::X11, 1);
        enc.mov_ri64(arm64::X13, 10);
        enc.mov_rr(arm64::X14, arm64::X12);
        enc.cbz(arm64::X14, powDone);
        enc.bindLabel(powLoop);
        enc.mul_rrr(arm64::X11, arm64::X11, arm64::X13);
        enc.sub_rri(arm64::X14, arm64::X14, 1);
        enc.cbnz(arm64::X14, powLoop);
        enc.bindLabel(powDone);
        enc.str_imm(arm64::X11, arm64::FP, 48); // pow10

        enc.cbnz(arm64::X12, hasDec);
        enc.mov_ri64(arm64::X13, 1000000);
        enc.udiv_rrr(arm64::X5, arm64::X3, arm64::X13);
        enc.b(scaledOk);
        enc.bindLabel(hasDec);
        enc.ldr_imm(arm64::X11, arm64::FP, 48);
        enc.scvtf_dr(arm64::D1, arm64::X11);
        enc.fldr_imm(arm64::D2, arm64::FP, 16);
        enc.fmul_ddd(arm64::D1, arm64::D2, arm64::D1);
        enc.adr_data(arm64::X16, offDoubleHalf);
        enc.fldr_imm(arm64::D2, arm64::X16, 0);
        enc.fadd_ddd(arm64::D1, arm64::D1, arm64::D2);
        enc.fcvtzs_rd(arm64::X5, arm64::D1);

        enc.bindLabel(scaledOk);
        enc.ldr_imm(arm64::X11, arm64::FP, 48);
        enc.udiv_rrr(arm64::X6, arm64::X5, arm64::X11);
        enc.mul_rrr(arm64::X7, arm64::X6, arm64::X11);
        enc.sub_rrr(arm64::X7, arm64::X5, arm64::X7);
        enc.str_imm(arm64::X7, arm64::FP, 56); // frac

        enc.ldr_imm(arm64::X0, arm64::FP, 24);
        enc.ldr_imm(arm64::X1, arm64::FP, 32);
        enc.add_rrr(arm64::X0, arm64::X0, arm64::X1);
        enc.mov_rr(arm64::X1, arm64::X6);
        enc.bl(lblIntToBuf);
        enc.ldr_imm(arm64::X1, arm64::FP, 24);
        enc.sub_rrr(arm64::X2, arm64::X0, arm64::X1);
        enc.str_imm(arm64::X2, arm64::FP, 32);

        enc.ldr_imm(arm64::X7, arm64::FP, 56);
        enc.cbz(arm64::X7, finLbl);

        enc.ldr_imm(arm64::X0, arm64::FP, 24);
        enc.ldr_imm(arm64::X1, arm64::FP, 32);
        enc.add_rrr(arm64::X0, arm64::X0, arm64::X1);
        enc.movz(arm64::X2, '.', 0);
        enc.strb_imm(arm64::X2, arm64::X0, 0);
        enc.add_rri(arm64::X1, arm64::X1, 1);
        enc.str_imm(arm64::X1, arm64::FP, 32);

        enc.ldr_imm(arm64::X8, arm64::FP, 40);  // d
        enc.ldr_imm(arm64::X0, arm64::FP, 24);
        enc.add_rrr(arm64::X0, arm64::X0, arm64::X1);
        enc.add_rrr(arm64::X2, arm64::X0, arm64::X8); // end = base + d
        enc.ldr_imm(arm64::X4, arm64::FP, 56);  // frac
        enc.movz(arm64::X5, 10, 0);
        enc.bindLabel(digLoop);
        enc.sub_rri(arm64::X8, arm64::X8, 1);
        enc.sub_rri(arm64::X2, arm64::X2, 1);
        enc.udiv_rrr(arm64::X9, arm64::X4, arm64::X5);
        enc.mul_rrr(arm64::X10, arm64::X9, arm64::X5);
        enc.sub_rrr(arm64::X10, arm64::X4, arm64::X10);
        enc.add_rri(arm64::X10, arm64::X10, '0');
        enc.strb_imm(arm64::X10, arm64::X2, 0);
        enc.mov_rr(arm64::X4, arm64::X9);
        enc.cbnz(arm64::X8, digLoop);

        enc.ldr_imm(arm64::X6, arm64::FP, 24);
        enc.ldr_imm(arm64::X1, arm64::FP, 32);
        enc.add_rrr(arm64::X6, arm64::X6, arm64::X1);
        enc.ldr_imm(arm64::X7, arm64::FP, 40);
        enc.add_rrr(arm64::X6, arm64::X6, arm64::X7); // one past the last digit
        enc.movz(arm64::X11, 0, 0);
        enc.strb_imm(arm64::X11, arm64::X6, 0);
        enc.ldr_imm(arm64::X1, arm64::FP, 24);
        enc.sub_rrr(arm64::X2, arm64::X6, arm64::X1);
        enc.str_imm(arm64::X2, arm64::FP, 32);

        enc.bindLabel(trimLoop);
        enc.ldr_imm(arm64::X2, arm64::FP, 32);
        enc.sub_rri(arm64::X4, arm64::X2, 1);
        enc.ldr_imm(arm64::X1, arm64::FP, 24);
        enc.add_rrr(arm64::X4, arm64::X1, arm64::X4);
        enc.ldrb_imm(arm64::X5, arm64::X4, 0);
        enc.cmp_ri(arm64::X5, '0');
        enc.b_cond(arm64::NE, finLbl);
        enc.sub_rri(arm64::X6, arm64::X4, 1);
        enc.ldrb_imm(arm64::X7, arm64::X6, 0);
        enc.cmp_ri(arm64::X7, '.');
        enc.b_cond(arm64::EQ, finLbl);
        enc.movz(arm64::X8, 0, 0);
        enc.strb_imm(arm64::X8, arm64::X4, 0);
        enc.sub_rri(arm64::X2, arm64::X2, 1);
        enc.str_imm(arm64::X2, arm64::FP, 32);
        enc.b(trimLoop);

        enc.bindLabel(zeroCase);
        enc.ldr_imm(arm64::X0, arm64::FP, 24);
        enc.ldr_imm(arm64::X1, arm64::FP, 32);
        enc.add_rrr(arm64::X0, arm64::X0, arm64::X1);
        enc.movz(arm64::X2, '0', 0);
        enc.strb_imm(arm64::X2, arm64::X0, 0);
        enc.add_rri(arm64::X0, arm64::X0, 1);
        enc.movz(arm64::X2, 0, 0);
        enc.strb_imm(arm64::X2, arm64::X0, 0);
        enc.b(finLbl);

        enc.bindLabel(bigCase);
        enc.fldr_imm(arm64::D0, arm64::FP, 16);
        // Reduce in groups of 1e18 so the int64 conversion cannot saturate.
        enc.movz(arm64::X9, 0, 0);
        size_t hugeLoop = enc.createLabel();
        size_t hugeDone = enc.createLabel();
        enc.bindLabel(hugeLoop);
        enc.adr_data(arm64::X16, offDouble1e18);
        enc.fldr_imm(arm64::D1, arm64::X16, 0);
        enc.fcmp_dd(arm64::D0, arm64::D1);
        enc.b_cond(arm64::LT, hugeDone);
        enc.fdiv_ddd(arm64::D0, arm64::D0, arm64::D1);
        enc.add_rri(arm64::X9, arm64::X9, 1);
        enc.b(hugeLoop);
        enc.bindLabel(hugeDone);
        enc.str_imm(arm64::X9, arm64::FP, 64); // groups of 1e18
        enc.fcvtzs_rd(arm64::X1, arm64::D0);
        enc.ldr_imm(arm64::X0, arm64::FP, 24);
        enc.ldr_imm(arm64::X2, arm64::FP, 32);
        enc.add_rrr(arm64::X0, arm64::X0, arm64::X2);
        enc.bl(lblIntToBuf);
        enc.ldr_imm(arm64::X9, arm64::FP, 64);
        size_t zeroDone = enc.createLabel();
        enc.cbz(arm64::X9, zeroDone);
        enc.mov_ri64(arm64::X10, 18);
        enc.mul_rrr(arm64::X9, arm64::X9, arm64::X10);
        size_t zeroLoop = enc.createLabel();
        enc.bindLabel(zeroLoop);
        enc.movz(arm64::X11, '0', 0);
        enc.strb_imm(arm64::X11, arm64::X0, 0);
        enc.add_rri(arm64::X0, arm64::X0, 1);
        enc.sub_rri(arm64::X9, arm64::X9, 1);
        enc.cbnz(arm64::X9, zeroLoop);
        enc.bindLabel(zeroDone);
        enc.movz(arm64::X1, 0, 0);
        enc.strb_imm(arm64::X1, arm64::X0, 0);

        enc.bindLabel(finLbl);
        enc.ldr_imm(arm64::X0, arm64::FP, 24);
        enc.ldp_post(arm64::FP, arm64::LR, arm64::SP, 192);
        enc.ret();
    }

    // file_write_double(handle X0, bits X1)
    void emitFileWriteDouble() {
        enc.bindLabel(lblFileWriteDouble);
        enc.stp_pre(arm64::FP, arm64::LR, arm64::SP, -64);
        enc.mov_rr(arm64::FP, arm64::SP);
        enc.str_imm(arm64::X0, arm64::FP, 16);
        enc.mov_rr(arm64::X0, arm64::X1);
        enc.bl(lblFileFmtDouble);
        enc.str_imm(arm64::X0, arm64::FP, 24);
        enc.bl(lblStrLen);
        enc.mov_rr(arm64::X2, arm64::X0);
        enc.ldr_imm(arm64::X0, arm64::FP, 16);
        enc.ldr_imm(arm64::X1, arm64::FP, 24);
        enc.bl(lblFileWrite);
        enc.ldp_post(arm64::FP, arm64::LR, arm64::SP, 64);
        enc.ret();
    }

    // ==========================================
    // file_read_line(handle X0) -> X0 (line, "" at EOF)
    // ==========================================
    void emitFileReadLine() {
        enc.bindLabel(lblFileReadLine);
        enc.stp_pre(arm64::FP, arm64::LR, arm64::SP, -112);
        enc.mov_rr(arm64::FP, arm64::SP);
        enc.str_imm(arm64::X0, arm64::FP, 16);
        enc.mov_ri64(arm64::X0, 4096);
        enc.bl(lblMalloc);
        enc.str_imm(arm64::X0, arm64::FP, 24); // out
        enc.movz(arm64::X0, 0, 0);
        enc.str_imm(arm64::X0, arm64::FP, 32); // len

        size_t loopLbl = enc.createLabel();
        size_t haveBuf = enc.createLabel();
        size_t doneLbl = enc.createLabel();
        enc.bindLabel(loopLbl);
        enc.ldr_imm(arm64::X6, arm64::FP, 16);
        enc.ldr_imm(arm64::X7, arm64::X6, 24); // pos
        enc.ldr_imm(arm64::X8, arm64::X6, 32); // len of buffer
        enc.cmp_rr(arm64::X7, arm64::X8);
        enc.b_cond(arm64::LT, haveBuf);

        enc.ldr_imm(arm64::X0, arm64::X6, 0);
        enc.ldr_imm(arm64::X1, arm64::X6, 8);
        enc.mov_ri64(arm64::X2, 4096);
        enc.movz(arm64::X8, 63, 0); // read
        enc.svc(0);
        enc.cmp_ri(arm64::X0, 0);
        enc.b_cond(arm64::LE, doneLbl);
        enc.ldr_imm(arm64::X6, arm64::FP, 16);
        enc.str_imm(arm64::X0, arm64::X6, 32);
        enc.movz(arm64::X7, 0, 0);
        enc.str_imm(arm64::X7, arm64::X6, 24);
        enc.b(loopLbl);

        enc.bindLabel(haveBuf);
        enc.ldr_imm(arm64::X9, arm64::X6, 8);
        enc.add_rrr(arm64::X9, arm64::X9, arm64::X7);
        enc.ldrb_imm(arm64::X10, arm64::X9, 0);
        enc.add_rri(arm64::X7, arm64::X7, 1);
        enc.str_imm(arm64::X7, arm64::X6, 24);
        enc.cmp_ri(arm64::X10, '\n');
        enc.b_cond(arm64::EQ, doneLbl);
        enc.cmp_ri(arm64::X10, '\r');
        enc.b_cond(arm64::EQ, loopLbl);

        enc.ldr_imm(arm64::X11, arm64::FP, 24);
        enc.ldr_imm(arm64::X12, arm64::FP, 32);
        enc.add_rrr(arm64::X12, arm64::X11, arm64::X12);
        enc.strb_imm(arm64::X10, arm64::X12, 0);
        enc.ldr_imm(arm64::X12, arm64::FP, 32);
        enc.add_rri(arm64::X12, arm64::X12, 1);
        enc.str_imm(arm64::X12, arm64::FP, 32);
        enc.cmp_ri(arm64::X12, 4090);
        enc.b_cond(arm64::GE, doneLbl);
        enc.b(loopLbl);

        enc.bindLabel(doneLbl);
        enc.ldr_imm(arm64::X11, arm64::FP, 24);
        enc.ldr_imm(arm64::X12, arm64::FP, 32);
        enc.add_rrr(arm64::X11, arm64::X11, arm64::X12);
        enc.movz(arm64::X13, 0, 0);
        enc.strb_imm(arm64::X13, arm64::X11, 0);
        enc.ldr_imm(arm64::X0, arm64::FP, 24);
        enc.ldp_post(arm64::FP, arm64::LR, arm64::SP, 112);
        enc.ret();
    }

    // file_read_all(handle X0) -> X0 (remaining content)
    void emitFileReadAll() {
        enc.bindLabel(lblFileReadAll);
        enc.stp_pre(arm64::FP, arm64::LR, arm64::SP, -96);
        enc.mov_rr(arm64::FP, arm64::SP);
        enc.str_imm(arm64::X0, arm64::FP, 16);
        // Rewind to the start first (x86 read_all seeks SEEK_END then SEEK_SET).
        enc.ldr_imm(arm64::X6, arm64::FP, 16);
        enc.ldr_imm(arm64::X0, arm64::X6, 0);
        enc.movz(arm64::X1, 0, 0);
        enc.movz(arm64::X2, 0, 0);
        enc.movz(arm64::X8, 62, 0); // lseek
        enc.svc(0);
        enc.mov_ri64(arm64::X0, 1048576);
        enc.bl(lblMalloc);
        enc.str_imm(arm64::X0, arm64::FP, 24); // buf
        enc.movz(arm64::X0, 0, 0);
        enc.str_imm(arm64::X0, arm64::FP, 32); // total

        size_t loopLbl = enc.createLabel();
        size_t doneLbl = enc.createLabel();
        enc.bindLabel(loopLbl);
        enc.ldr_imm(arm64::X6, arm64::FP, 16);
        enc.ldr_imm(arm64::X0, arm64::X6, 0);
        enc.ldr_imm(arm64::X1, arm64::FP, 24);
        enc.ldr_imm(arm64::X7, arm64::FP, 32);
        enc.add_rrr(arm64::X1, arm64::X1, arm64::X7);
        enc.mov_ri64(arm64::X2, 1048575);
        enc.sub_rrr(arm64::X2, arm64::X2, arm64::X7);
        enc.movz(arm64::X8, 63, 0);
        enc.svc(0);
        enc.cmp_ri(arm64::X0, 0);
        enc.b_cond(arm64::LE, doneLbl);
        enc.ldr_imm(arm64::X7, arm64::FP, 32);
        enc.add_rrr(arm64::X7, arm64::X7, arm64::X0);
        enc.str_imm(arm64::X7, arm64::FP, 32);
        enc.mov_ri64(arm64::X8, 1048570);
        enc.cmp_rr(arm64::X7, arm64::X8);
        enc.b_cond(arm64::GE, doneLbl);
        enc.b(loopLbl);

        enc.bindLabel(doneLbl);
        enc.ldr_imm(arm64::X0, arm64::FP, 24);
        enc.ldr_imm(arm64::X7, arm64::FP, 32);
        enc.add_rrr(arm64::X0, arm64::X0, arm64::X7);
        enc.movz(arm64::X8, 0, 0);
        enc.strb_imm(arm64::X8, arm64::X0, 0);
        enc.ldr_imm(arm64::X0, arm64::FP, 24);
        enc.ldp_post(arm64::FP, arm64::LR, arm64::SP, 96);
        enc.ret();
    }

    // file_read_lines(handle X0) -> list of lines
    void emitFileReadLines() {
        enc.bindLabel(lblFileReadLines);
        enc.stp_pre(arm64::FP, arm64::LR, arm64::SP, -112);
        enc.mov_rr(arm64::FP, arm64::SP);
        enc.str_imm(arm64::X0, arm64::FP, 16);
        enc.movz(arm64::X0, 16, 0);
        enc.bl(lblMalloc);
        enc.str_imm(arm64::X0, arm64::FP, 24); // list
        enc.movz(arm64::X0, 0, 0);
        enc.str_imm(arm64::X0, arm64::FP, 32); // count
        enc.movz(arm64::X0, 1, 0);
        enc.str_imm(arm64::X0, arm64::FP, 40); // cap
        // Rewind to the start first so read_lines always starts at the beginning.
        enc.ldr_imm(arm64::X6, arm64::FP, 16);
        enc.ldr_imm(arm64::X0, arm64::X6, 0);
        enc.movz(arm64::X1, 0, 0);
        enc.movz(arm64::X2, 0, 0);
        enc.movz(arm64::X8, 62, 0); // lseek
        enc.svc(0);

        size_t loopLbl = enc.createLabel();
        size_t storeLbl = enc.createLabel();
        size_t copyLoop = enc.createLabel();
        size_t copyDone = enc.createLabel();
        size_t doneLbl = enc.createLabel();
        enc.bindLabel(loopLbl);
        enc.ldr_imm(arm64::X0, arm64::FP, 16);
        enc.bl(lblFileReadLine);
        enc.str_imm(arm64::X0, arm64::FP, 48);
        enc.ldrb_imm(arm64::X7, arm64::X0, 0);
        enc.cbz(arm64::X7, doneLbl);

        enc.ldr_imm(arm64::X6, arm64::FP, 32);
        enc.ldr_imm(arm64::X7, arm64::FP, 40);
        enc.cmp_rr(arm64::X6, arm64::X7);
        enc.b_cond(arm64::NE, storeLbl);

        enc.add_rrr(arm64::X7, arm64::X7, arm64::X7);
        enc.str_imm(arm64::X7, arm64::FP, 56);
        enc.add_rri(arm64::X0, arm64::X7, 1);
        enc.movz(arm64::X8, 8, 0);
        enc.mul_rrr(arm64::X0, arm64::X0, arm64::X8);
        enc.bl(lblMalloc);
        enc.str_imm(arm64::X0, arm64::FP, 64);
        enc.movz(arm64::X0, 0, 0);
        enc.str_imm(arm64::X0, arm64::FP, 72);
        enc.bindLabel(copyLoop);
        enc.ldr_imm(arm64::X0, arm64::FP, 72);
        enc.ldr_imm(arm64::X6, arm64::FP, 32);
        enc.cmp_rr(arm64::X0, arm64::X6);
        enc.b_cond(arm64::GE, copyDone);
        enc.movz(arm64::X8, 8, 0);
        enc.mul_rrr(arm64::X9, arm64::X0, arm64::X8);
        enc.ldr_imm(arm64::X10, arm64::FP, 24);
        enc.add_rrr(arm64::X10, arm64::X10, arm64::X9);
        enc.ldr_imm(arm64::X11, arm64::X10, 8);
        enc.ldr_imm(arm64::X10, arm64::FP, 64);
        enc.add_rrr(arm64::X10, arm64::X10, arm64::X9);
        enc.str_imm(arm64::X11, arm64::X10, 8);
        enc.add_rri(arm64::X0, arm64::X0, 1);
        enc.str_imm(arm64::X0, arm64::FP, 72);
        enc.b(copyLoop);
        enc.bindLabel(copyDone);
        enc.ldr_imm(arm64::X0, arm64::FP, 64);
        enc.str_imm(arm64::X0, arm64::FP, 24);
        enc.ldr_imm(arm64::X0, arm64::FP, 56);
        enc.str_imm(arm64::X0, arm64::FP, 40);

        enc.bindLabel(storeLbl);
        enc.ldr_imm(arm64::X6, arm64::FP, 32);
        enc.movz(arm64::X8, 8, 0);
        enc.mul_rrr(arm64::X9, arm64::X6, arm64::X8);
        enc.ldr_imm(arm64::X10, arm64::FP, 24);
        enc.add_rrr(arm64::X10, arm64::X10, arm64::X9);
        enc.ldr_imm(arm64::X11, arm64::FP, 48);
        enc.str_imm(arm64::X11, arm64::X10, 8);
        enc.add_rri(arm64::X6, arm64::X6, 1);
        enc.str_imm(arm64::X6, arm64::FP, 32);
        enc.b(loopLbl);

        enc.bindLabel(doneLbl);
        enc.ldr_imm(arm64::X0, arm64::FP, 24);
        enc.ldr_imm(arm64::X6, arm64::FP, 32);
        enc.str_imm(arm64::X6, arm64::X0, 0);
        enc.ldp_post(arm64::FP, arm64::LR, arm64::SP, 112);
        enc.ret();
    }

    void emitPrintDoubleNL() {
        enc.bindLabel(lblPrintDoubleNL);
        enc.stp_pre(arm64::X29, arm64::X30, arm64::SP, -32);
        enc.mov_rr(arm64::X29, arm64::SP);
        enc.fstr_imm(arm64::D0, arm64::X29, 16);

        enc.bl(lblPrintDouble);
        enc.bl(lblPrintNL);

        enc.ldp_post(arm64::X29, arm64::X30, arm64::SP, 32);
        enc.ret();
    }
};

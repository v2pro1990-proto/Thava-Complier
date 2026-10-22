#pragma once

#include "pe_emitter.hpp"
#include "arm64_encoder.hpp"
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

    size_t lblExit = 0;
    size_t lblPrintStrNL = 0;
    size_t lblPrintIntNL = 0;
    size_t lblPrintBoolNL = 0;

    bool initialized = false;

    Arm64WinRuntime(arm64::Encoder& e, PeEmitter& p)
        : enc(e), pe(p) {
        lblExit        = enc.createLabel();
        lblPrintStrNL  = enc.createLabel();
        lblPrintIntNL  = enc.createLabel();
        lblPrintBoolNL = enc.createLabel();
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
    }

    void emitHelpers() {
        if (!initialized) init();

        // Must layout .rdata so all string offsets and IAT RVAs are finalized
        pe.layoutRdata();

        uint32_t iatPrintf = pe.getFuncIatOffsetInRdata("printf");
        uint32_t iatExit   = pe.getFuncIatOffsetInRdata("exit");

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
    }
};

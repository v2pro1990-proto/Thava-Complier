#pragma once

#include "elf_arm64_emitter.hpp"
#include "arm64_encoder.hpp"
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
    uint32_t offSuccessMsg= 0;  // "\n[Thava Native ARM64] Build and run successful!\n"

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
    }

    bool initialized = false;

    void init() {
        if (initialized) return;
        initialized = true;
        offStrNL      = elf.addStringLiteralOffset("\n");
        offStrSpace   = elf.addStringLiteralOffset(" ");
        offStrTrue    = elf.addStringLiteralOffset("true");
        offStrFalse   = elf.addStringLiteralOffset("false");
        offSuccessMsg = elf.addStringLiteralOffset("\n[Thava Native ARM64] Build and run successful!\n");
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
        enc.movz(arm64::X8, 93, 0); // sys_exit on Linux AArch64 = 93
        enc.svc(0);
    }
};

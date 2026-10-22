#pragma once

#include <vector>
#include <cstdint>
#include <cstring>
#include <string>
#include <map>
#include <stdexcept>
#include <iostream>

namespace arm64 {

enum Reg {
    X0 = 0,   X1 = 1,   X2 = 2,   X3 = 3,
    X4 = 4,   X5 = 5,   X6 = 6,   X7 = 7,
    X8 = 8,   X9 = 9,   X10 = 10, X11 = 11,
    X12 = 12, X13 = 13, X14 = 14, X15 = 15,
    X16 = 16, X17 = 17, X18 = 18, X19 = 19,
    X20 = 20, X21 = 21, X22 = 22, X23 = 23,
    X24 = 24, X25 = 25, X26 = 26, X27 = 27,
    X28 = 28, X29 = 29, X30 = 30,
    SP  = 31, XZR = 31,
    FP  = 29, LR  = 30
};

enum VReg {
    D0 = 0,   D1 = 1,   D2 = 2,   D3 = 3,
    D4 = 4,   D5 = 5,   D6 = 6,   D7 = 7,
    D8 = 8,   D9 = 9,   D10 = 10, D11 = 11,
    D12 = 12, D13 = 13, D14 = 14, D15 = 15,
    D16 = 16, D17 = 17, D18 = 18, D19 = 19,
    D20 = 20, D21 = 21, D22 = 22, D23 = 23,
    D24 = 24, D25 = 25, D26 = 26, D27 = 27,
    D28 = 28, D29 = 29, D30 = 30, D31 = 31
};

enum Cond {
    EQ = 0x0, // Equal (Z == 1)
    NE = 0x1, // Not equal (Z == 0)
    CS = 0x2, // Carry set / unsigned higher or same (C == 1)
    CC = 0x3, // Carry clear / unsigned lower (C == 0)
    MI = 0x4, // Minus / negative (N == 1)
    PL = 0x5, // Plus / positive or zero (N == 0)
    VS = 0x6, // Overflow (V == 1)
    VC = 0x7, // No overflow (V == 0)
    HI = 0x8, // Unsigned higher (C == 1 and Z == 0)
    LS = 0x9, // Unsigned lower or same (C == 0 or Z == 1)
    GE = 0xA, // Signed greater than or equal (N == V)
    LT = 0xB, // Signed less than (N != V)
    GT = 0xC, // Signed greater than (Z == 0 and N == V)
    LE = 0xD, // Signed less than or equal (Z == 1 or N != V)
    AL = 0xE  // Always (unconditional)
};

class Encoder {
public:
    std::vector<uint8_t> code;

    struct Label {
        int boundOffset = -1; // -1 if not yet bound
    };

    enum FixupType {
        FIXUP_B_26,        // B instruction (26-bit imm, +/- 128MB)
        FIXUP_BL_26,       // BL instruction (26-bit imm, +/- 128MB)
        FIXUP_BCOND_19,    // B.cond (19-bit imm, +/- 1MB)
        FIXUP_CBZ_19,      // CBZ (19-bit imm, +/- 1MB)
        FIXUP_CBNZ_19,     // CBNZ (19-bit imm, +/- 1MB)
        FIXUP_ADR_21       // ADR (21-bit imm, +/- 1MB)
    };

    struct JumpFixup {
        size_t codePos;
        size_t labelId;
        FixupType type;
    };

    struct DataFixup {
        size_t codePos;
        uint32_t offsetInData;
        FixupType type;
    };

    std::vector<Label> labels;
    std::vector<JumpFixup> jumpFixups;
    std::vector<DataFixup> dataFixups;

    size_t createLabel() {
        labels.push_back(Label{});
        return labels.size() - 1;
    }

    void bindLabel(size_t labelId) {
        if (labelId < labels.size()) {
            labels[labelId].boundOffset = (int)code.size();
        }
    }

    int getLabelOffset(size_t labelId) const {
        if (labelId < labels.size()) return labels[labelId].boundOffset;
        return -1;
    }

    // Helper to emit a 32-bit instruction (little-endian)
    void emit32(uint32_t val) {
        code.push_back(static_cast<uint8_t>(val & 0xFF));
        code.push_back(static_cast<uint8_t>((val >> 8) & 0xFF));
        code.push_back(static_cast<uint8_t>((val >> 16) & 0xFF));
        code.push_back(static_cast<uint8_t>((val >> 24) & 0xFF));
    }

    // ==========================================
    // 1. DATA PROCESSING - ARITHMETIC & LOGIC (64-BIT)
    // ==========================================

    // ADD Xd, Xn, Xm
    void add_rrr(Reg rd, Reg rn, Reg rm) {
        emit32(0x8B000000 | ((rm & 31) << 16) | ((rn & 31) << 5) | (rd & 31));
    }

    // ADD Xd, Xn, #imm12 (0..4095)
    void add_rri(Reg rd, Reg rn, uint32_t imm12) {
        emit32(0x91000000 | ((imm12 & 0xFFF) << 10) | ((rn & 31) << 5) | (rd & 31));
    }

    // SUB Xd, Xn, Xm
    void sub_rrr(Reg rd, Reg rn, Reg rm) {
        emit32(0xCB000000 | ((rm & 31) << 16) | ((rn & 31) << 5) | (rd & 31));
    }

    // SUB Xd, Xn, #imm12 (0..4095)
    void sub_rri(Reg rd, Reg rn, uint32_t imm12) {
        emit32(0xD1000000 | ((imm12 & 0xFFF) << 10) | ((rn & 31) << 5) | (rd & 31));
    }

    // MUL Xd, Xn, Xm (MADD Xd, Xn, Xm, XZR)
    void mul_rrr(Reg rd, Reg rn, Reg rm) {
        emit32(0x9B007C00 | ((rm & 31) << 16) | ((rn & 31) << 5) | (rd & 31));
    }

    // SDIV Xd, Xn, Xm (Signed 64-bit integer divide)
    void sdiv_rrr(Reg rd, Reg rn, Reg rm) {
        emit32(0x9AC00C00 | ((rm & 31) << 16) | ((rn & 31) << 5) | (rd & 31));
    }

    // UDIV Xd, Xn, Xm (Unsigned 64-bit integer divide)
    void udiv_rrr(Reg rd, Reg rn, Reg rm) {
        emit32(0x9AC00800 | ((rm & 31) << 16) | ((rn & 31) << 5) | (rd & 31));
    }

    // AND Xd, Xn, Xm
    void and_rrr(Reg rd, Reg rn, Reg rm) {
        emit32(0x8A000000 | ((rm & 31) << 16) | ((rn & 31) << 5) | (rd & 31));
    }

    // ORR Xd, Xn, Xm
    void orr_rrr(Reg rd, Reg rn, Reg rm) {
        emit32(0xAA000000 | ((rm & 31) << 16) | ((rn & 31) << 5) | (rd & 31));
    }

    // EOR Xd, Xn, Xm (Bitwise XOR)
    void eor_rrr(Reg rd, Reg rn, Reg rm) {
        emit32(0xCA000000 | ((rm & 31) << 16) | ((rn & 31) << 5) | (rd & 31));
    }

    // MOV Xd, Xm (Handles SP correctly via ADD Xd, SP, #0 when SP is involved)
    void mov_rr(Reg rd, Reg rm) {
        if (rm == SP) {
            add_rri(rd, SP, 0);
        } else if (rd == SP) {
            add_rri(SP, rm, 0);
        } else {
            emit32(0xAA0003E0 | ((rm & 31) << 16) | (rd & 31));
        }
    }

    // CMP Xn, Xm (Alias: SUBS XZR, Xn, Xm)
    void cmp_rr(Reg rn, Reg rm) {
        emit32(0xEB00001F | ((rm & 31) << 16) | ((rn & 31) << 5));
    }

    // CMP Xn, #imm12 (Alias: SUBS XZR, Xn, #imm12)
    void cmp_ri(Reg rn, uint32_t imm12) {
        emit32(0xF100001F | ((imm12 & 0xFFF) << 10) | ((rn & 31) << 5));
    }

    // CSET Xd, cond (Sets Xd to 1 if cond is met, 0 otherwise)
    // CSINC Xd, XZR, XZR, invert(cond) -> opcode: 0x9A9F07E0 | ((cond ^ 1) << 12) | rd
    void cset(Reg rd, Cond cond) {
        emit32(0x9A9F07E0 | (((cond ^ 1) & 0xF) << 12) | (rd & 31));
    }

    // ==========================================
    // 2. IMMEDIATE VALUE LOADING (MOVZ, MOVK, MOVN)
    // ==========================================

    // MOVZ Xd, #imm16, LSL #shift (shift in {0, 16, 32, 48})
    void movz(Reg rd, uint16_t imm16, uint8_t shift = 0) {
        uint32_t hw = (shift / 16) & 3;
        emit32(0xD2800000 | (hw << 21) | ((uint32_t)imm16 << 5) | (rd & 31));
    }

    // MOVK Xd, #imm16, LSL #shift (Keep other bits, replace 16-bit lane)
    void movk(Reg rd, uint16_t imm16, uint8_t shift = 0) {
        uint32_t hw = (shift / 16) & 3;
        emit32(0xF2800000 | (hw << 21) | ((uint32_t)imm16 << 5) | (rd & 31));
    }

    // MOVN Xd, #imm16, LSL #shift (Move bitwise NOT of 16-bit imm)
    void movn(Reg rd, uint16_t imm16, uint8_t shift = 0) {
        uint32_t hw = (shift / 16) & 3;
        emit32(0x92800000 | (hw << 21) | ((uint32_t)imm16 << 5) | (rd & 31));
    }

    // Smart 64-bit immediate loader
    void mov_ri64(Reg rd, int64_t imm) {
        if (imm == 0) {
            movz(rd, 0, 0);
            return;
        }

        uint64_t u = static_cast<uint64_t>(imm);

        // Fits in 16-bit unsigned
        if ((u & ~0xFFFFULL) == 0) {
            movz(rd, static_cast<uint16_t>(u), 0);
            return;
        }

        // Small negative number that fits in MOVN
        if ((~u & ~0xFFFFULL) == 0) {
            movn(rd, static_cast<uint16_t>(~u), 0);
            return;
        }

        // Decompose into 16-bit chunks
        uint16_t c0 = static_cast<uint16_t>(u & 0xFFFF);
        uint16_t c1 = static_cast<uint16_t>((u >> 16) & 0xFFFF);
        uint16_t c2 = static_cast<uint16_t>((u >> 32) & 0xFFFF);
        uint16_t c3 = static_cast<uint16_t>((u >> 48) & 0xFFFF);

        bool first = true;
        if (c0 != 0 || (c1 == 0 && c2 == 0 && c3 == 0)) {
            movz(rd, c0, 0);
            first = false;
        }
        if (c1 != 0) {
            if (first) { movz(rd, c1, 16); first = false; }
            else       { movk(rd, c1, 16); }
        }
        if (c2 != 0) {
            if (first) { movz(rd, c2, 32); first = false; }
            else       { movk(rd, c2, 32); }
        }
        if (c3 != 0) {
            if (first) { movz(rd, c3, 48); first = false; }
            else       { movk(rd, c3, 48); }
        }
    }

    // ==========================================
    // 3. LOAD / STORE OPERATIONS
    // ==========================================

    // LDR Xd, [Xn, #offset] (64-bit load)
    void ldr_imm(Reg rt, Reg rn, int32_t offset) {
        if (offset >= 0 && offset <= 32760 && (offset % 8 == 0)) {
            // Unsigned scaled immediate: 0xF9400000 | (imm12 << 10) | (rn << 5) | rt
            uint32_t imm12 = (offset / 8) & 0xFFF;
            emit32(0xF9400000 | (imm12 << 10) | ((rn & 31) << 5) | (rt & 31));
        } else if (offset >= -256 && offset <= 255) {
            // LDUR Xd, [Xn, #imm9] (Unscaled signed 9-bit immediate)
            uint32_t imm9 = offset & 0x1FF;
            emit32(0xF8400000 | (imm9 << 12) | ((rn & 31) << 5) | (rt & 31));
        } else {
            // Out of single-instruction immediate range: use X16 as scratch register
            mov_ri64(X16, offset);
            ldr_reg(rt, rn, X16);
        }
    }

    // STR Xt, [Xn, #offset] (64-bit store)
    void str_imm(Reg rt, Reg rn, int32_t offset) {
        if (offset >= 0 && offset <= 32760 && (offset % 8 == 0)) {
            // Unsigned scaled immediate: 0xF9000000 | (imm12 << 10) | (rn << 5) | rt
            uint32_t imm12 = (offset / 8) & 0xFFF;
            emit32(0xF9000000 | (imm12 << 10) | ((rn & 31) << 5) | (rt & 31));
        } else if (offset >= -256 && offset <= 255) {
            // STUR Xt, [Xn, #imm9] (Unscaled signed 9-bit immediate)
            uint32_t imm9 = offset & 0x1FF;
            emit32(0xF8000000 | (imm9 << 12) | ((rn & 31) << 5) | (rt & 31));
        } else {
            mov_ri64(X16, offset);
            str_reg(rt, rn, X16);
        }
    }

    // LDR Xd, [Xn, Xm] (Register offset load)
    void ldr_reg(Reg rt, Reg rn, Reg rm) {
        // 0xF8606800 | (rm << 16) | (rn << 5) | rt (LDR Xd, [Xn, Xm, LSL #0])
        emit32(0xF8606800 | ((rm & 31) << 16) | ((rn & 31) << 5) | (rt & 31));
    }

    // STR Xt, [Xn, Xm] (Register offset store)
    void str_reg(Reg rt, Reg rn, Reg rm) {
        emit32(0xF8206800 | ((rm & 31) << 16) | ((rn & 31) << 5) | (rt & 31));
    }

    // LDRB Wt, [Xn, #offset] (Byte load with zero extension)
    void ldrb_imm(Reg rt, Reg rn, int32_t offset) {
        if (offset >= 0 && offset <= 4095) {
            emit32(0x39400000 | ((offset & 0xFFF) << 10) | ((rn & 31) << 5) | (rt & 31));
        } else if (offset >= -256 && offset <= 255) {
            emit32(0x38400000 | ((offset & 0x1FF) << 12) | ((rn & 31) << 5) | (rt & 31));
        } else {
            mov_ri64(X16, offset);
            emit32(0x38606800 | ((X16 & 31) << 16) | ((rn & 31) << 5) | (rt & 31));
        }
    }

    // STRB Wt, [Xn, #offset] (Byte store)
    void strb_imm(Reg rt, Reg rn, int32_t offset) {
        if (offset >= 0 && offset <= 4095) {
            emit32(0x39000000 | ((offset & 0xFFF) << 10) | ((rn & 31) << 5) | (rt & 31));
        } else if (offset >= -256 && offset <= 255) {
            emit32(0x38000000 | ((offset & 0x1FF) << 12) | ((rn & 31) << 5) | (rt & 31));
        } else {
            mov_ri64(X16, offset);
            emit32(0x38206800 | ((X16 & 31) << 16) | ((rn & 31) << 5) | (rt & 31));
        }
    }

    // STP Xt1, Xt2, [Xn, #offset]! (Pre-index store pair, e.g. STP X29, X30, [SP, #-32]!)
    void stp_pre(Reg rt1, Reg rt2, Reg rn, int32_t offset) {
        uint32_t imm7 = (offset / 8) & 0x7F;
        emit32(0xA9800000 | (imm7 << 15) | ((rt2 & 31) << 10) | ((rn & 31) << 5) | (rt1 & 31));
    }

    // LDP Xt1, Xt2, [Xn], #offset (Post-index load pair, e.g. LDP X29, X30, [SP], #32)
    void ldp_post(Reg rt1, Reg rt2, Reg rn, int32_t offset) {
        uint32_t imm7 = (offset / 8) & 0x7F;
        emit32(0xA8C00000 | (imm7 << 15) | ((rt2 & 31) << 10) | ((rn & 31) << 5) | (rt1 & 31));
    }

    // STP Xt1, Xt2, [Xn, #offset] (Signed offset store pair without base update)
    void stp_signed(Reg rt1, Reg rt2, Reg rn, int32_t offset) {
        uint32_t imm7 = (offset / 8) & 0x7F;
        emit32(0xA9000000 | (imm7 << 15) | ((rt2 & 31) << 10) | ((rn & 31) << 5) | (rt1 & 31));
    }

    // LDP Xt1, Xt2, [Xn, #offset] (Signed offset load pair without base update)
    void ldp_signed(Reg rt1, Reg rt2, Reg rn, int32_t offset) {
        uint32_t imm7 = (offset / 8) & 0x7F;
        emit32(0xA9400000 | (imm7 << 15) | ((rt2 & 31) << 10) | ((rn & 31) << 5) | (rt1 & 31));
    }

    // ==========================================
    // 4. FLOATING POINT (SIMD & FP - 64-BIT DOUBLE)
    // ==========================================

    // FADD Dd, Dn, Dm
    void fadd_ddd(VReg rd, VReg rn, VReg rm) {
        emit32(0x1E602800 | ((rm & 31) << 16) | ((rn & 31) << 5) | (rd & 31));
    }

    // FSUB Dd, Dn, Dm
    void fsub_ddd(VReg rd, VReg rn, VReg rm) {
        emit32(0x1E603800 | ((rm & 31) << 16) | ((rn & 31) << 5) | (rd & 31));
    }

    // FMUL Dd, Dn, Dm
    void fmul_ddd(VReg rd, VReg rn, VReg rm) {
        emit32(0x1E600800 | ((rm & 31) << 16) | ((rn & 31) << 5) | (rd & 31));
    }

    // FDIV Dd, Dn, Dm
    void fdiv_ddd(VReg rd, VReg rn, VReg rm) {
        emit32(0x1E601800 | ((rm & 31) << 16) | ((rn & 31) << 5) | (rd & 31));
    }

    // FCMP Dn, Dm
    void fcmp_dd(VReg rn, VReg rm) {
        emit32(0x1E602000 | ((rm & 31) << 16) | ((rn & 31) << 5));
    }

    // SCVTF Dd, Xn (Convert signed 64-bit integer to 64-bit float)
    void scvtf_dr(VReg rd, Reg rn) {
        emit32(0x9E620000 | ((rn & 31) << 5) | (rd & 31));
    }

    // FCVTZS Xd, Dn (Convert 64-bit float to signed 64-bit integer, round towards zero)
    void fcvtzs_rd(Reg rd, VReg rn) {
        emit32(0x9E780000 | ((rn & 31) << 5) | (rd & 31));
    }

    // LDR Dt, [Xn, #offset] (64-bit FP load)
    void fldr_imm(VReg rt, Reg rn, int32_t offset) {
        if (offset >= 0 && offset <= 32760 && (offset % 8 == 0)) {
            uint32_t imm12 = (offset / 8) & 0xFFF;
            emit32(0xFD400000 | (imm12 << 10) | ((rn & 31) << 5) | (rt & 31));
        } else if (offset >= -256 && offset <= 255) {
            uint32_t imm9 = offset & 0x1FF;
            emit32(0xFC400000 | (imm9 << 12) | ((rn & 31) << 5) | (rt & 31));
        } else {
            mov_ri64(X16, offset);
            emit32(0xFC606800 | ((X16 & 31) << 16) | ((rn & 31) << 5) | (rt & 31));
        }
    }

    // STR Dt, [Xn, #offset] (64-bit FP store)
    void fstr_imm(VReg rt, Reg rn, int32_t offset) {
        if (offset >= 0 && offset <= 32760 && (offset % 8 == 0)) {
            uint32_t imm12 = (offset / 8) & 0xFFF;
            emit32(0xFD000000 | (imm12 << 10) | ((rn & 31) << 5) | (rt & 31));
        } else if (offset >= -256 && offset <= 255) {
            uint32_t imm9 = offset & 0x1FF;
            emit32(0xFC000000 | (imm9 << 12) | ((rn & 31) << 5) | (rt & 31));
        } else {
            mov_ri64(X16, offset);
            emit32(0xFC206800 | ((X16 & 31) << 16) | ((rn & 31) << 5) | (rt & 31));
        }
    }

    // ==========================================
    // 5. BRANCHES & CONTROL FLOW
    // ==========================================

    // B label (Unconditional branch, +/- 128MB)
    void b(size_t labelId) {
        jumpFixups.push_back({code.size(), labelId, FIXUP_B_26});
        emit32(0x14000000);
    }

    // BL label (Branch with Link / Call function, return addr into X30 LR)
    void bl(size_t labelId) {
        jumpFixups.push_back({code.size(), labelId, FIXUP_BL_26});
        emit32(0x94000000);
    }

    // B.cond label (Conditional branch, +/- 1MB)
    void b_cond(Cond cond, size_t labelId) {
        jumpFixups.push_back({code.size(), labelId, FIXUP_BCOND_19});
        emit32(0x54000000 | (cond & 0xF));
    }

    // CBZ Xt, label (Compare and branch if zero, +/- 1MB)
    void cbz(Reg rt, size_t labelId) {
        jumpFixups.push_back({code.size(), labelId, FIXUP_CBZ_19});
        emit32(0xB4000000 | (rt & 31));
    }

    // CBNZ Xt, label (Compare and branch if non-zero, +/- 1MB)
    void cbnz(Reg rt, size_t labelId) {
        jumpFixups.push_back({code.size(), labelId, FIXUP_CBNZ_19});
        emit32(0xB5000000 | (rt & 31));
    }

    // BR Xn (Indirect branch to address in register)
    void br(Reg rn) {
        emit32(0xD61F0000 | ((rn & 31) << 5));
    }

    // BLR Xn (Indirect call to address in register)
    void blr(Reg rn) {
        emit32(0xD63F0000 | ((rn & 31) << 5));
    }

    // RET [Xn] (Default Xn = X30 LR -> 0xD65F03C0)
    void ret(Reg rn = LR) {
        emit32(0xD65F0000 | ((rn & 31) << 5));
    }

    // ==========================================
    // 6. PC-RELATIVE ADDRESSING & SYSTEM CALLS
    // ==========================================

    // ADR Xd, label (+/- 1MB range)
    void adr(Reg rd, size_t labelId) {
        jumpFixups.push_back({code.size(), labelId, FIXUP_ADR_21});
        emit32(0x10000000 | (rd & 31));
    }

    // ADR Xd, dataOffset (References string or literal in .rodata / .data)
    void adr_data(Reg rd, uint32_t offsetInData) {
        dataFixups.push_back({code.size(), offsetInData, FIXUP_ADR_21});
        emit32(0x10000000 | (rd & 31));
    }

    // SVC #imm16 (Supervisor Call / Linux Syscall)
    void svc(uint16_t imm = 0) {
        emit32(0xD4000001 | (((uint32_t)imm & 0xFFFF) << 5));
    }

    // NOP
    void nop() {
        emit32(0xD503201F);
    }

    // ==========================================
    // 7. FIXUP RESOLUTION (LINKING LABELS)
    // ==========================================
    void applyFixups(uint32_t textRva = 0, uint32_t dataRva = 0) {
        // 1. Resolve internal label jumps
        for (const auto& jf : jumpFixups) {
            if (jf.labelId >= labels.size()) continue;
            int targetOffset = labels[jf.labelId].boundOffset;
            if (targetOffset < 0) continue; // Unbound label

            int32_t dispBytes = targetOffset - static_cast<int32_t>(jf.codePos);
            int32_t dispWords = dispBytes / 4;

            uint32_t* instrPtr = reinterpret_cast<uint32_t*>(&code[jf.codePos]);
            uint32_t instr = *instrPtr;

            switch (jf.type) {
                case FIXUP_B_26:
                case FIXUP_BL_26: {
                    // 26-bit signed immediate
                    uint32_t imm26 = static_cast<uint32_t>(dispWords) & 0x03FFFFFF;
                    instr = (instr & 0xFC000000) | imm26;
                    break;
                }
                case FIXUP_BCOND_19:
                case FIXUP_CBZ_19:
                case FIXUP_CBNZ_19: {
                    // 19-bit signed immediate (bits 23..5)
                    uint32_t imm19 = static_cast<uint32_t>(dispWords) & 0x7FFFF;
                    instr = (instr & 0xFF00001F) | (imm19 << 5);
                    break;
                }
                case FIXUP_ADR_21: {
                    // 21-bit signed byte displacement:
                    // bits 30..29 = disp[1:0], bits 23..5 = disp[20:2]
                    uint32_t imm21 = static_cast<uint32_t>(dispBytes) & 0x1FFFFF;
                    uint32_t lo2 = imm21 & 3;
                    uint32_t hi19 = (imm21 >> 2) & 0x7FFFF;
                    instr = (instr & 0x9F00001F) | (lo2 << 29) | (hi19 << 5);
                    break;
                }
            }
            *instrPtr = instr;
        }

        // 2. Resolve data section references (e.g., ADR to string pool)
        for (const auto& df : dataFixups) {
            uint32_t targetVAddr = dataRva + df.offsetInData;
            uint32_t pcVAddr     = textRva + static_cast<uint32_t>(df.codePos);
            int32_t dispBytes    = static_cast<int32_t>(targetVAddr - pcVAddr);

            uint32_t* instrPtr = reinterpret_cast<uint32_t*>(&code[df.codePos]);
            uint32_t instr = *instrPtr;

            if (df.type == FIXUP_ADR_21) {
                uint32_t imm21 = static_cast<uint32_t>(dispBytes) & 0x1FFFFF;
                uint32_t lo2 = imm21 & 3;
                uint32_t hi19 = (imm21 >> 2) & 0x7FFFF;
                instr = (instr & 0x9F00001F) | (lo2 << 29) | (hi19 << 5);
            }
            *instrPtr = instr;
        }
    }
};

} // namespace arm64

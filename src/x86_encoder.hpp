#pragma once

#include <vector>
#include <cstdint>
#include <cstring>
#include <string>
#include <map>
#include <stdexcept>
#include <iostream>

namespace x86 {

enum Reg {
    RAX = 0,
    RCX = 1,
    RDX = 2,
    RBX = 3,
    RSP = 4,
    RBP = 5,
    RSI = 6,
    RDI = 7,
    R8  = 8,
    R9  = 9,
    R10 = 10,
    R11 = 11,
    R12 = 12,
    R13 = 13,
    R14 = 14,
    R15 = 15
};

enum Xmm {
    XMM0 = 0,
    XMM1 = 1,
    XMM2 = 2,
    XMM3 = 3,
    XMM4 = 4,
    XMM5 = 5,
    XMM6 = 6,
    XMM7 = 7
};

enum Cond {
    B  = 0x2, // Below / Carry (unsigned <)
    AE = 0x3, // Above or Equal / Not Carry (unsigned >=)
    E  = 0x4, // Equal / Zero
    NE = 0x5, // Not Equal / Not Zero
    BE = 0x6, // Below or Equal (unsigned <=)
    A  = 0x7, // Above (unsigned >)
    L  = 0xC, // Less (signed <)
    GE = 0xD, // Greater or Equal (signed >=)
    LE = 0xE, // Less or Equal (signed <=)
    G  = 0xF  // Greater (signed >)
};

class Encoder {
public:
    std::vector<uint8_t> code;

    // Label management for jumps and calls
    struct Label {
        int boundOffset = -1; // -1 if not yet bound
    };

    struct JumpFixup {
        size_t codePos;      // position of 4-byte displacement in code
        size_t nextInstrPos; // position after jump instruction
        size_t labelId;
    };

    struct RipFixup {
        size_t codePos;
        size_t nextInstrPos;
        uint32_t targetRva;
    };

    struct RdataFixup {
        size_t codePos;
        size_t nextInstrPos;
        uint32_t offsetInRdata;
    };

    struct ImportFixup {
        size_t codePos;
        size_t nextInstrPos;
        std::string funcName;
    };

    std::vector<Label> labels;
    std::vector<JumpFixup> jumpFixups;
    std::vector<RipFixup> ripFixups;
    std::vector<RdataFixup> rdataFixups;
    std::vector<ImportFixup> importFixups;

    size_t createLabel() {
        labels.push_back(Label{});
        return labels.size() - 1;
    }

    void bindLabel(size_t labelId) {
        if (labelId >= labels.size()) return;
        labels[labelId].boundOffset = (int)code.size();
    }

    int getLabelOffset(size_t labelId) const {
        if (labelId < labels.size()) return labels[labelId].boundOffset;
        return -1;
    }

    // Emit helpers
    void emit8(uint8_t b) {
        code.push_back(b);
    }

    void emit16(uint16_t w) {
        emit8(w & 0xFF);
        emit8((w >> 8) & 0xFF);
    }

    void emit32(uint32_t d) {
        emit8(d & 0xFF);
        emit8((d >> 8) & 0xFF);
        emit8((d >> 16) & 0xFF);
        emit8((d >> 24) & 0xFF);
    }

    void emit64(uint64_t q) {
        emit32((uint32_t)(q & 0xFFFFFFFF));
        emit32((uint32_t)((q >> 32) & 0xFFFFFFFF));
    }

    void emitBytes(const std::vector<uint8_t>& bytes) {
        code.insert(code.end(), bytes.begin(), bytes.end());
    }

    uint8_t makeRex(bool w, bool r, bool x, bool b) {
        return 0x40 | (w ? 0x08 : 0) | (r ? 0x04 : 0) | (x ? 0x02 : 0) | (b ? 0x01 : 0);
    }

    uint8_t makeModRm(uint8_t mod, uint8_t reg, uint8_t rm) {
        return ((mod & 3) << 6) | ((reg & 7) << 3) | (rm & 7);
    }

    // syscall (0x0F 0x05) - Linux 64-bit direct kernel system call
    void syscall() {
        emit8(0x0F);
        emit8(0x05);
    }

    // --- Basic Instructions ---

    // push reg
    void push(Reg r) {
        if (r >= 8) emit8(makeRex(false, false, false, true));
        emit8(0x50 + (r & 7));
    }

    // pop reg
    void pop(Reg r) {
        if (r >= 8) emit8(makeRex(false, false, false, true));
        emit8(0x58 + (r & 7));
    }

    // sub rsp, imm32
    void sub_rsp(int32_t imm) {
        if (imm >= -128 && imm <= 127) {
            emit8(makeRex(true, false, false, false));
            emit8(0x83);
            emit8(makeModRm(3, 5, 4)); // /5 on RSP
            emit8((uint8_t)(int8_t)imm);
        } else {
            emit8(makeRex(true, false, false, false));
            emit8(0x81);
            emit8(makeModRm(3, 5, 4));
            emit32(imm);
        }
    }

    // add rsp, imm32
    void add_rsp(int32_t imm) {
        if (imm >= -128 && imm <= 127) {
            emit8(makeRex(true, false, false, false));
            emit8(0x83);
            emit8(makeModRm(3, 0, 4)); // /0 on RSP
            emit8((uint8_t)(int8_t)imm);
        } else {
            emit8(makeRex(true, false, false, false));
            emit8(0x81);
            emit8(makeModRm(3, 0, 4));
            emit32(imm);
        }
    }

    // mov dst, src (64-bit reg to reg)
    void mov_rr(Reg dst, Reg src) {
        emit8(makeRex(true, src >= 8, false, dst >= 8));
        emit8(0x89);
        emit8(makeModRm(3, src & 7, dst & 7));
    }

    // mov reg, imm64
    void mov_ri64(Reg dst, int64_t imm) {
        if (imm == 0) {
            xor_r32(dst, dst);
            return;
        }
        // If fits in 32-bit signed: mov r/m64, imm32 (sign extended) -> 7 bytes
        if (imm >= -2147483648LL && imm <= 2147483647LL) {
            emit8(makeRex(true, false, false, dst >= 8));
            emit8(0xC7);
            emit8(makeModRm(3, 0, dst & 7));
            emit32((int32_t)imm);
        } else {
            // Full 64-bit immediate -> 10 bytes
            emit8(makeRex(true, false, false, dst >= 8));
            emit8(0xB8 + (dst & 7));
            emit64((uint64_t)imm);
        }
    }

    // mov dst, [RBP + disp]
    void mov_rm_rbp(Reg dst, int32_t disp) {
        emit8(makeRex(true, dst >= 8, false, false));
        emit8(0x8B);
        if (disp >= -128 && disp <= 127) {
            emit8(makeModRm(1, dst & 7, 5)); // mod=01, rm=5 (RBP)
            emit8((uint8_t)(int8_t)disp);
        } else {
            emit8(makeModRm(2, dst & 7, 5)); // mod=10, rm=5 (RBP)
            emit32(disp);
        }
    }

    // mov [RBP + disp], src
    void mov_mr_rbp(int32_t disp, Reg src) {
        emit8(makeRex(true, src >= 8, false, false));
        emit8(0x89);
        if (disp >= -128 && disp <= 127) {
            emit8(makeModRm(1, src & 7, 5));
            emit8((uint8_t)(int8_t)disp);
        } else {
            emit8(makeModRm(2, src & 7, 5));
            emit32(disp);
        }
    }

    // mov dst, [base + disp] (general register base, no SIB if base != RSP/R12)
    void mov_rm(Reg dst, Reg base, int32_t disp) {
        if (base == RBP || base == R13) {
            emit8(makeRex(true, dst >= 8, false, base >= 8));
            emit8(0x8B);
            if (disp >= -128 && disp <= 127) {
                emit8(makeModRm(1, dst & 7, base & 7));
                emit8((uint8_t)(int8_t)disp);
            } else {
                emit8(makeModRm(2, dst & 7, base & 7));
                emit32(disp);
            }
            return;
        }

        emit8(makeRex(true, dst >= 8, false, base >= 8));
        emit8(0x8B);
        if (disp == 0 && (base & 7) != 5) {
            emit8(makeModRm(0, dst & 7, base & 7));
        } else if (disp >= -128 && disp <= 127) {
            emit8(makeModRm(1, dst & 7, base & 7));
            emit8((uint8_t)(int8_t)disp);
        } else {
            emit8(makeModRm(2, dst & 7, base & 7));
            emit32(disp);
        }
    }

    // mov [base + disp], src
    void mov_mr(Reg base, int32_t disp, Reg src) {
        if (base == RBP || base == R13) {
            emit8(makeRex(true, src >= 8, false, base >= 8));
            emit8(0x89);
            if (disp >= -128 && disp <= 127) {
                emit8(makeModRm(1, src & 7, base & 7));
                emit8((uint8_t)(int8_t)disp);
            } else {
                emit8(makeModRm(2, src & 7, base & 7));
                emit32(disp);
            }
            return;
        }

        emit8(makeRex(true, src >= 8, false, base >= 8));
        emit8(0x89);
        if (disp == 0 && (base & 7) != 5) {
            emit8(makeModRm(0, src & 7, base & 7));
        } else if (disp >= -128 && disp <= 127) {
            emit8(makeModRm(1, src & 7, base & 7));
            emit8((uint8_t)(int8_t)disp);
        } else {
            emit8(makeModRm(2, src & 7, base & 7));
            emit32(disp);
        }
    }

    // mov [RSP + disp], src
    void mov_mr_rsp(int32_t disp, Reg src) {
        emit8(makeRex(true, src >= 8, false, false));
        emit8(0x89);
        if (disp >= -128 && disp <= 127) {
            emit8(makeModRm(1, src & 7, 4));
            emit8(0x24);
            emit8((uint8_t)(int8_t)disp);
        } else {
            emit8(makeModRm(2, src & 7, 4));
            emit8(0x24);
            emit32(disp);
        }
    }

    // movzx r64, byte ptr [base + disp]
    void movzx_rm8(Reg dst, Reg base, int32_t disp = 0) {
        emit8(makeRex(true, dst >= 8, false, base >= 8));
        emit8(0x0F);
        emit8(0xB6);
        if (disp == 0 && (base & 7) != 5) {
            emit8(makeModRm(0, dst & 7, base & 7));
        } else if (disp >= -128 && disp <= 127) {
            emit8(makeModRm(1, dst & 7, base & 7));
            emit8((uint8_t)(int8_t)disp);
        } else {
            emit8(makeModRm(2, dst & 7, base & 7));
            emit32(disp);
        }
    }

    // movzx r64, word ptr [base + disp]
    void movzx_rm16(Reg dst, Reg base, int32_t disp = 0) {
        emit8(makeRex(true, dst >= 8, false, base >= 8));
        emit8(0x0F);
        emit8(0xB7);
        if (disp == 0 && (base & 7) != 5) {
            emit8(makeModRm(0, dst & 7, base & 7));
        } else if (disp >= -128 && disp <= 127) {
            emit8(makeModRm(1, dst & 7, base & 7));
            emit8((uint8_t)(int8_t)disp);
        } else {
            emit8(makeModRm(2, dst & 7, base & 7));
            emit32(disp);
        }
    }

    // mov byte ptr [base + disp], src
    void mov_mr8(Reg base, int32_t disp, Reg src) {
        emit8(makeRex(false, src >= 8, false, base >= 8));
        emit8(0x88);
        if (disp == 0 && (base & 7) != 5) {
            emit8(makeModRm(0, src & 7, base & 7));
        } else if (disp >= -128 && disp <= 127) {
            emit8(makeModRm(1, src & 7, base & 7));
            emit8((uint8_t)(int8_t)disp);
        } else {
            emit8(makeModRm(2, src & 7, base & 7));
            emit32(disp);
        }
    }

    // mov word ptr [base + disp], src16
    void mov_mr16(Reg base, int32_t disp, Reg src) {
        emit8(0x66);
        if (src >= 8 || base >= 8) {
            emit8(makeRex(false, src >= 8, false, base >= 8));
        }
        emit8(0x89);
        if (disp == 0 && (base & 7) != 5) {
            emit8(makeModRm(0, src & 7, base & 7));
        } else if (disp >= -128 && disp <= 127) {
            emit8(makeModRm(1, src & 7, base & 7));
            emit8((uint8_t)(int8_t)disp);
        } else {
            emit8(makeModRm(2, src & 7, base & 7));
            emit32(disp);
        }
    }

    // lea dst, [rip + disp32] (to load string or data RVA)
    void lea_rip(Reg dst, uint32_t targetRva) {
        emit8(makeRex(true, dst >= 8, false, false));
        emit8(0x8D);
        emit8(makeModRm(0, dst & 7, 5)); // mod=00, rm=5 in 64-bit is RIP-relative!
        size_t fixupPos = code.size();
        emit32(0); // placeholder
        ripFixups.push_back(RipFixup{ fixupPos, code.size(), targetRva });
    }

    void lea_rip_rdata(Reg dst, uint32_t offsetInRdata) {
        emit8(makeRex(true, dst >= 8, false, false));
        emit8(0x8D);
        emit8(makeModRm(0, dst & 7, 5));
        size_t fixupPos = code.size();
        emit32(0);
        rdataFixups.push_back(RdataFixup{ fixupPos, code.size(), offsetInRdata });
    }

    // lea dst, [RIP + disp32] targeting an internal code label (for function pointers/callbacks)
    void lea_rip_label(Reg dst, size_t labelId) {
        emit8(makeRex(true, dst >= 8, false, false));
        emit8(0x8D);
        emit8(makeModRm(0, dst & 7, 5));
        size_t fixupPos = code.size();
        emit32(0);
        jumpFixups.push_back(JumpFixup{ fixupPos, code.size(), labelId });
    }

    // lea dst, [RBP + disp]
    void lea_rm_rbp(Reg dst, int32_t disp) {
        emit8(makeRex(true, dst >= 8, false, false));
        emit8(0x8D);
        if (disp >= -128 && disp <= 127) {
            emit8(makeModRm(1, dst & 7, 5)); // mod=01, rm=5 (RBP)
            emit8((uint8_t)(int8_t)disp);
        } else {
            emit8(makeModRm(2, dst & 7, 5)); // mod=10, rm=5 (RBP)
            emit32(disp);
        }
    }

    // --- Arithmetic & Logic ---

    // add dst, src (dst += src)
    void add_rr(Reg dst, Reg src) {
        emit8(makeRex(true, src >= 8, false, dst >= 8));
        emit8(0x01);
        emit8(makeModRm(3, src & 7, dst & 7));
    }

    // sub dst, src (dst -= src)
    void sub_rr(Reg dst, Reg src) {
        emit8(makeRex(true, src >= 8, false, dst >= 8));
        emit8(0x29);
        emit8(makeModRm(3, src & 7, dst & 7));
    }

    // imul dst, src (dst *= src)
    void imul_rr(Reg dst, Reg src) {
        emit8(makeRex(true, dst >= 8, false, src >= 8));
        emit8(0x0F);
        emit8(0xAF);
        emit8(makeModRm(3, dst & 7, src & 7));
    }

    // cqo (sign extend RAX -> RDX:RAX)
    void cqo() {
        emit8(0x48);
        emit8(0x99);
    }

    // idiv divisor (RDX:RAX / divisor -> RAX = quotient, RDX = remainder)
    void idiv_r(Reg divisor) {
        emit8(makeRex(true, false, false, divisor >= 8));
        emit8(0xF7);
        emit8(makeModRm(3, 7, divisor & 7)); // /7 on divisor
    }

    // div divisor (RDX:RAX / divisor unsigned -> RAX = quotient, RDX = remainder)
    void div_r(Reg divisor) {
        emit8(makeRex(true, false, false, divisor >= 8));
        emit8(0xF7);
        emit8(makeModRm(3, 6, divisor & 7)); // /6 on divisor
    }

    // and dst, src
    void and_rr(Reg dst, Reg src) {
        emit8(makeRex(true, src >= 8, false, dst >= 8));
        emit8(0x21);
        emit8(makeModRm(3, src & 7, dst & 7));
    }

    // or dst, src
    void or_rr(Reg dst, Reg src) {
        emit8(makeRex(true, src >= 8, false, dst >= 8));
        emit8(0x09);
        emit8(makeModRm(3, src & 7, dst & 7));
    }

    // xor dst, src (64-bit)
    void xor_rr(Reg dst, Reg src) {
        emit8(makeRex(true, src >= 8, false, dst >= 8));
        emit8(0x31);
        emit8(makeModRm(3, src & 7, dst & 7));
    }

    // xor reg32, reg32 (clears 64-bit reg faster, 2 bytes)
    void xor_r32(Reg dst, Reg src) {
        if (dst >= 8 || src >= 8) {
            emit8(makeRex(false, src >= 8, false, dst >= 8));
        }
        emit8(0x31);
        emit8(makeModRm(3, src & 7, dst & 7));
    }

    // neg dst (dst = -dst)
    void neg_r(Reg dst) {
        emit8(makeRex(true, false, false, dst >= 8));
        emit8(0xF7);
        emit8(makeModRm(3, 3, dst & 7));
    }

    // not dst (dst = ~dst)
    void not_r(Reg dst) {
        emit8(makeRex(true, false, false, dst >= 8));
        emit8(0xF7);
        emit8(makeModRm(3, 2, dst & 7));
    }

    // add reg, imm32
    void add_ri(Reg r, int32_t imm) {
        emit8(makeRex(true, false, false, r >= 8));
        if (imm >= -128 && imm <= 127) {
            emit8(0x83);
            emit8(makeModRm(3, 0, r & 7));
            emit8((uint8_t)(int8_t)imm);
        } else {
            emit8(0x81);
            emit8(makeModRm(3, 0, r & 7));
            emit32(imm);
        }
    }

    // sub reg, imm32
    void sub_ri(Reg r, int32_t imm) {
        emit8(makeRex(true, false, false, r >= 8));
        if (imm >= -128 && imm <= 127) {
            emit8(0x83);
            emit8(makeModRm(3, 5, r & 7));
            emit8((uint8_t)(int8_t)imm);
        } else {
            emit8(0x81);
            emit8(makeModRm(3, 5, r & 7));
            emit32(imm);
        }
    }

    // inc reg
    void inc_r(Reg r) {
        add_ri(r, 1);
    }

    // dec reg
    void dec_r(Reg r) {
        sub_ri(r, 1);
    }

    // shl reg, imm8
    void shl_ri(Reg r, uint8_t imm) {
        emit8(makeRex(true, false, false, r >= 8));
        if (imm == 1) {
            emit8(0xD1);
            emit8(makeModRm(3, 4, r & 7));
        } else {
            emit8(0xC1);
            emit8(makeModRm(3, 4, r & 7));
            emit8(imm);
        }
    }

    // shr reg, imm8
    void shr_ri(Reg r, uint8_t imm) {
        emit8(makeRex(true, false, false, r >= 8));
        if (imm == 1) {
            emit8(0xD1);
            emit8(makeModRm(3, 5, r & 7));
        } else {
            emit8(0xC1);
            emit8(makeModRm(3, 5, r & 7));
            emit8(imm);
        }
    }

    // shl reg, cl (variable shift left)
    void shl_cl(Reg r) {
        emit8(makeRex(true, false, false, r >= 8));
        emit8(0xD3);
        emit8(makeModRm(3, 4, r & 7));
    }

    // shr reg, cl (variable logical shift right)
    void shr_cl(Reg r) {
        emit8(makeRex(true, false, false, r >= 8));
        emit8(0xD3);
        emit8(makeModRm(3, 5, r & 7));
    }

    // sar reg, cl (variable arithmetic shift right)
    void sar_cl(Reg r) {
        emit8(makeRex(true, false, false, r >= 8));
        emit8(0xD3);
        emit8(makeModRm(3, 7, r & 7));
    }

    // sar reg, imm8 (arithmetic shift right)
    void sar_ri(Reg r, uint8_t imm) {
        emit8(makeRex(true, false, false, r >= 8));
        if (imm == 1) {
            emit8(0xD1);
            emit8(makeModRm(3, 7, r & 7));
        } else {
            emit8(0xC1);
            emit8(makeModRm(3, 7, r & 7));
            emit8(imm);
        }
    }

    // and reg, imm32
    void and_ri(Reg r, int32_t imm) {
        emit8(makeRex(true, false, false, r >= 8));
        if (imm >= -128 && imm <= 127) {
            emit8(0x83);
            emit8(makeModRm(3, 4, r & 7));
            emit8((uint8_t)(int8_t)imm);
        } else {
            emit8(0x81);
            emit8(makeModRm(3, 4, r & 7));
            emit32(imm);
        }
    }

    // imul reg, imm32 (or imul dst, src, imm32)
    void imul_ri(Reg dst, Reg src, int32_t imm) {
        emit8(makeRex(true, dst >= 8, false, src >= 8));
        if (imm >= -128 && imm <= 127) {
            emit8(0x6B);
            emit8(makeModRm(3, dst & 7, src & 7));
            emit8((uint8_t)(int8_t)imm);
        } else {
            emit8(0x69);
            emit8(makeModRm(3, dst & 7, src & 7));
            emit32(imm);
        }
    }
    void imul_ri(Reg r, int32_t imm) {
        imul_ri(r, r, imm);
    }

    // cmp reg, imm32
    void cmp_ri(Reg r, int32_t imm) {
        emit8(makeRex(true, false, false, r >= 8));
        if (imm >= -128 && imm <= 127) {
            emit8(0x83);
            emit8(makeModRm(3, 7, r & 7));
            emit8((uint8_t)(int8_t)imm);
        } else {
            emit8(0x81);
            emit8(makeModRm(3, 7, r & 7));
            emit32(imm);
        }
    }

    void add_ri32(Reg r, int32_t imm) { add_ri(r, imm); }
    void sub_ri32(Reg r, int32_t imm) { sub_ri(r, imm); }
    void cmp_ri32(Reg r, int32_t imm) { cmp_ri(r, imm); }

    // --- Comparison & Conditional Set ---

    // cmp r1, r2 (compares r1 with r2)
    void cmp_rr(Reg r1, Reg r2) {
        emit8(makeRex(true, r2 >= 8, false, r1 >= 8));
        emit8(0x39);
        emit8(makeModRm(3, r2 & 7, r1 & 7));
    }

    // test r1, r2 (r1 & r2 flags)
    void test_rr(Reg r1, Reg r2) {
        emit8(makeRex(true, r2 >= 8, false, r1 >= 8));
        emit8(0x85);
        emit8(makeModRm(3, r2 & 7, r1 & 7));
    }

    // setcc AL and zero extend to RAX
    void setcc_to_rax(Cond cond) {
        emit8(0x0F);
        emit8(0x90 + cond);
        emit8(makeModRm(3, 0, 0)); // AL (reg=0, rm=0, mod=3)

        // movzx rax, al: 48 0F B6 C0
        emit8(0x48);
        emit8(0x0F);
        emit8(0xB6);
        emit8(makeModRm(3, 0, 0));
    }

    // --- Jumps and Calls ---

    // unconditional jmp label
    void jmp(size_t labelId) {
        emit8(0xE9);
        size_t fixupPos = code.size();
        emit32(0);
        jumpFixups.push_back(JumpFixup{ fixupPos, code.size(), labelId });
    }

    // conditional jump jcc label
    void jcc(Cond cond, size_t labelId) {
        emit8(0x0F);
        emit8(0x80 + cond);
        size_t fixupPos = code.size();
        emit32(0);
        jumpFixups.push_back(JumpFixup{ fixupPos, code.size(), labelId });
    }

    void je(size_t labelId)  { jcc(Cond::E, labelId); }
    void jne(size_t labelId) { jcc(Cond::NE, labelId); }
    void jl(size_t labelId)  { jcc(Cond::L, labelId); }
    void jle(size_t labelId) { jcc(Cond::LE, labelId); }
    void jg(size_t labelId)  { jcc(Cond::G, labelId); }
    void jge(size_t labelId) { jcc(Cond::GE, labelId); }

    // jump if not sign / positive (0x0F 0x89)
    void jns(size_t labelId) {
        emit8(0x0F);
        emit8(0x89);
        size_t fixupPos = code.size();
        emit32(0);
        jumpFixups.push_back(JumpFixup{ fixupPos, code.size(), labelId });
    }

    // call label (internal function call)
    void call_label(size_t labelId) {
        emit8(0xE8);
        size_t fixupPos = code.size();
        emit32(0);
        jumpFixups.push_back(JumpFixup{ fixupPos, code.size(), labelId });
    }

    // call [rip + disp32] (call IAT imported function)
    void call_iat(uint32_t iatRva) {
        emit8(0xFF);
        emit8(makeModRm(0, 2, 5)); // /2 on RIP-relative
        size_t fixupPos = code.size();
        emit32(0);
        ripFixups.push_back(RipFixup{ fixupPos, code.size(), iatRva });
    }

    void call_iat_func(const std::string& funcName) {
        emit8(0xFF);
        emit8(makeModRm(0, 2, 5));
        size_t fixupPos = code.size();
        emit32(0);
        importFixups.push_back(ImportFixup{ fixupPos, code.size(), funcName });
    }

    // call reg (indirect function call via register)
    void call_r(Reg r) {
        if (r >= 8) emit8(makeRex(false, false, false, true));
        emit8(0xFF);
        emit8(makeModRm(3, 2, r & 7));
    }

    // ret
    void ret() {
        emit8(0xC3);
    }

    // --- SSE2 Floating Point (Double precision 64-bit) ---

    // movsd xmm_dst, [rip + disp32] (load float/double constant)
    void movsd_rip(Xmm dst, uint32_t targetRva) {
        emit8(0xF2);
        emit8(0x0F);
        emit8(0x10);
        emit8(makeModRm(0, dst & 7, 5)); // RIP-relative
        size_t fixupPos = code.size();
        emit32(0);
        ripFixups.push_back(RipFixup{ fixupPos, code.size(), targetRva });
    }

    void movsd_rip_rdata(Xmm dst, uint32_t offsetInRdata) {
        emit8(0xF2);
        emit8(0x0F);
        emit8(0x10);
        emit8(makeModRm(0, dst & 7, 5));
        size_t fixupPos = code.size();
        emit32(0);
        rdataFixups.push_back(RdataFixup{ fixupPos, code.size(), offsetInRdata });
    }

    // movsd xmm_dst, [RBP + disp]
    void movsd_rm_rbp(Xmm dst, int32_t disp) {
        emit8(0xF2);
        emit8(0x0F);
        emit8(0x10);
        if (disp >= -128 && disp <= 127) {
            emit8(makeModRm(1, dst & 7, 5));
            emit8((uint8_t)(int8_t)disp);
        } else {
            emit8(makeModRm(2, dst & 7, 5));
            emit32(disp);
        }
    }

    // movsd [RBP + disp], xmm_src
    void movsd_mr_rbp(int32_t disp, Xmm src) {
        emit8(0xF2);
        emit8(0x0F);
        emit8(0x11);
        if (disp >= -128 && disp <= 127) {
            emit8(makeModRm(1, src & 7, 5));
            emit8((uint8_t)(int8_t)disp);
        } else {
            emit8(makeModRm(2, src & 7, 5));
            emit32(disp);
        }
    }

    // movsd [RSP + disp], xmm_src
    void movsd_mr_rsp(int32_t disp, Xmm src) {
        emit8(0xF2);
        if (src >= 8) emit8(makeRex(false, true, false, false));
        emit8(0x0F);
        emit8(0x11);
        if (disp >= -128 && disp <= 127) {
            emit8(makeModRm(1, src & 7, 4));
            emit8(0x24);
            emit8((uint8_t)(int8_t)disp);
        } else {
            emit8(makeModRm(2, src & 7, 4));
            emit8(0x24);
            emit32(disp);
        }
    }

    // movsd xmm_dst, xmm_src
    void movsd_rr(Xmm dst, Xmm src) {
        emit8(0xF2);
        emit8(0x0F);
        emit8(0x10);
        emit8(makeModRm(3, dst & 7, src & 7));
    }

    // addsd dst, src
    void addsd_rr(Xmm dst, Xmm src) {
        emit8(0xF2);
        emit8(0x0F);
        emit8(0x58);
        emit8(makeModRm(3, dst & 7, src & 7));
    }

    // subsd dst, src
    void subsd_rr(Xmm dst, Xmm src) {
        emit8(0xF2);
        emit8(0x0F);
        emit8(0x5C);
        emit8(makeModRm(3, dst & 7, src & 7));
    }

    // mulsd dst, src
    void mulsd_rr(Xmm dst, Xmm src) {
        emit8(0xF2);
        emit8(0x0F);
        emit8(0x59);
        emit8(makeModRm(3, dst & 7, src & 7));
    }

    // divsd dst, src
    void divsd_rr(Xmm dst, Xmm src) {
        emit8(0xF2);
        emit8(0x0F);
        emit8(0x5E);
        emit8(makeModRm(3, dst & 7, src & 7));
    }

    // ucomisd dst, src
    void ucomisd_rr(Xmm dst, Xmm src) {
        emit8(0x66);
        emit8(0x0F);
        emit8(0x2E);
        emit8(makeModRm(3, dst & 7, src & 7));
    }

    // cvtsi2sd xmm_dst, reg_src (convert int64 in reg to double in xmm)
    void cvtsi2sd(Xmm dst, Reg src) {
        emit8(0xF2);
        emit8(makeRex(true, false, false, src >= 8));
        emit8(0x0F);
        emit8(0x2A);
        emit8(makeModRm(3, dst & 7, src & 7));
    }

    // cvttsd2si reg_dst, xmm_src (convert double in xmm to int64 in reg)
    void cvttsd2si(Reg dst, Xmm src) {
        emit8(0xF2);
        emit8(makeRex(true, dst >= 8, false, false));
        emit8(0x0F);
        emit8(0x2C);
        emit8(makeModRm(3, dst & 7, src & 7));
    }

    // cvtsd2ss dst, src (convert double in xmm to float in xmm)
    void cvtsd2ss(Xmm dst, Xmm src) {
        emit8(0xF2);
        if (dst >= 8 || src >= 8) emit8(makeRex(false, dst >= 8, false, src >= 8));
        emit8(0x0F);
        emit8(0x5A);
        emit8(makeModRm(3, dst & 7, src & 7));
    }

    // cvtss2sd dst, src (convert float in xmm to double in xmm)
    void cvtss2sd(Xmm dst, Xmm src) {
        emit8(0xF3);
        if (dst >= 8 || src >= 8) emit8(makeRex(false, dst >= 8, false, src >= 8));
        emit8(0x0F);
        emit8(0x5A);
        emit8(makeModRm(3, dst & 7, src & 7));
    }

    // movss [base + disp], xmm_src (store 32-bit float)
    void movss_mr(Reg base, int32_t disp, Xmm src) {
        emit8(0xF3);
        if (src >= 8 || base >= 8) emit8(makeRex(false, src >= 8, false, base >= 8));
        emit8(0x0F);
        emit8(0x11);
        if (disp == 0 && (base & 7) != 5) {
            emit8(makeModRm(0, src & 7, base & 7));
        } else if (disp >= -128 && disp <= 127) {
            emit8(makeModRm(1, src & 7, base & 7));
            emit8((uint8_t)(int8_t)disp);
        } else {
            emit8(makeModRm(2, src & 7, base & 7));
            emit32(disp);
        }
    }

    // movss xmm_dst, [base + disp] (load 32-bit float)
    void movss_rm(Xmm dst, Reg base, int32_t disp = 0) {
        emit8(0xF3);
        if (dst >= 8 || base >= 8) emit8(makeRex(false, dst >= 8, false, base >= 8));
        emit8(0x0F);
        emit8(0x10);
        if (disp == 0 && (base & 7) != 5) {
            emit8(makeModRm(0, dst & 7, base & 7));
        } else if (disp >= -128 && disp <= 127) {
            emit8(makeModRm(1, dst & 7, base & 7));
            emit8((uint8_t)(int8_t)disp);
        } else {
            emit8(makeModRm(2, dst & 7, base & 7));
            emit32(disp);
        }
    }

    // movsd [base + disp], xmm_src (store 64-bit double)
    void movsd_mr(Reg base, int32_t disp, Xmm src) {
        emit8(0xF2);
        if (src >= 8 || base >= 8) emit8(makeRex(false, src >= 8, false, base >= 8));
        emit8(0x0F);
        emit8(0x11);
        if (disp == 0 && (base & 7) != 5) {
            emit8(makeModRm(0, src & 7, base & 7));
        } else if (disp >= -128 && disp <= 127) {
            emit8(makeModRm(1, src & 7, base & 7));
            emit8((uint8_t)(int8_t)disp);
        } else {
            emit8(makeModRm(2, src & 7, base & 7));
            emit32(disp);
        }
    }

    // movsd xmm_dst, [base + disp] (load 64-bit double)
    void movsd_rm(Xmm dst, Reg base, int32_t disp = 0) {
        emit8(0xF2);
        if (dst >= 8 || base >= 8) emit8(makeRex(false, dst >= 8, false, base >= 8));
        emit8(0x0F);
        emit8(0x10);
        if (disp == 0 && (base & 7) != 5) {
            emit8(makeModRm(0, dst & 7, base & 7));
        } else if (disp >= -128 && disp <= 127) {
            emit8(makeModRm(1, dst & 7, base & 7));
            emit8((uint8_t)(int8_t)disp);
        } else {
            emit8(makeModRm(2, dst & 7, base & 7));
            emit32(disp);
        }
    }

    // mov r32, dword ptr [base + disp] (32-bit load zero-extended to r64)
    void mov_rm32(Reg dst, Reg base, int32_t disp = 0) {
        if (dst >= 8 || base >= 8) emit8(makeRex(false, dst >= 8, false, base >= 8));
        emit8(0x8B);
        if (disp == 0 && (base & 7) != 5) {
            emit8(makeModRm(0, dst & 7, base & 7));
        } else if (disp >= -128 && disp <= 127) {
            emit8(makeModRm(1, dst & 7, base & 7));
            emit8((uint8_t)(int8_t)disp);
        } else {
            emit8(makeModRm(2, dst & 7, base & 7));
            emit32(disp);
        }
    }

    // mov dword ptr [base + disp], r32 (32-bit store)
    void mov_mr32(Reg base, int32_t disp, Reg src) {
        if (src >= 8 || base >= 8) emit8(makeRex(false, src >= 8, false, base >= 8));
        emit8(0x89);
        if (disp == 0 && (base & 7) != 5) {
            emit8(makeModRm(0, src & 7, base & 7));
        } else if (disp >= -128 && disp <= 127) {
            emit8(makeModRm(1, src & 7, base & 7));
            emit8((uint8_t)(int8_t)disp);
        } else {
            emit8(makeModRm(2, src & 7, base & 7));
            emit32(disp);
        }
    }

    // movsxd r64, dword ptr [base + disp] (32-bit load sign-extended to r64)
    void movsxd_rm(Reg dst, Reg base, int32_t disp = 0) {
        emit8(makeRex(true, dst >= 8, false, base >= 8));
        emit8(0x63);
        if (disp == 0 && (base & 7) != 5) {
            emit8(makeModRm(0, dst & 7, base & 7));
        } else if (disp >= -128 && disp <= 127) {
            emit8(makeModRm(1, dst & 7, base & 7));
            emit8((uint8_t)(int8_t)disp);
        } else {
            emit8(makeModRm(2, dst & 7, base & 7));
            emit32(disp);
        }
    }

    // --- Backpatching & Resolving Fixups ---
    void resolveFixups(uint32_t textRva, uint32_t rdataRva = 0, const std::map<std::string, uint32_t>& funcToIatRva = {}) {
        // 1. Resolve internal label jumps and calls
        for (const auto& jmp : jumpFixups) {
            if (jmp.labelId >= labels.size()) continue;
            int targetOffset = labels[jmp.labelId].boundOffset;
            if (targetOffset < 0) {
                std::cerr << "[x86 Encoder Error]: Unresolved label id " << jmp.labelId << "\n";
                continue;
            }
            int32_t disp = (int32_t)(targetOffset - (int)jmp.nextInstrPos);
            std::memcpy(&code[jmp.codePos], &disp, sizeof(int32_t));
        }

        // 2. Resolve RIP-relative addresses to explicit target RVA
        for (const auto& rip : ripFixups) {
            uint32_t ripAfter = textRva + (uint32_t)rip.nextInstrPos;
            int32_t disp = (int32_t)(rip.targetRva - ripAfter);
            std::memcpy(&code[rip.codePos], &disp, sizeof(int32_t));
        }

        // 3. Resolve Rdata fixups (strings, floats)
        for (const auto& rf : rdataFixups) {
            uint32_t ripAfter = textRva + (uint32_t)rf.nextInstrPos;
            uint32_t targetRva = rdataRva + rf.offsetInRdata;
            int32_t disp = (int32_t)(targetRva - ripAfter);
            std::memcpy(&code[rf.codePos], &disp, sizeof(int32_t));
        }

        // 4. Resolve IAT calls by function name
        for (const auto& imp : importFixups) {
            uint32_t ripAfter = textRva + (uint32_t)imp.nextInstrPos;
            auto it = funcToIatRva.find(imp.funcName);
            if (it == funcToIatRva.end()) {
                std::cerr << "[x86 Encoder Error]: Unknown imported function: " << imp.funcName << "\n";
                continue;
            }
            uint32_t targetRva = it->second;
            int32_t disp = (int32_t)(targetRva - ripAfter);
            std::memcpy(&code[imp.codePos], &disp, sizeof(int32_t));
        }
    }
};

} // namespace x86

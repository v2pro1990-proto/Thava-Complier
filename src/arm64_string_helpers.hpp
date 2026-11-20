#pragma once

#include "arm64_encoder.hpp"

namespace arm64 {

inline void emitCaseConvert(Encoder& enc, size_t lbl, bool toUpper, size_t lblStrLen, size_t lblMalloc, uint32_t offEmptyStr) {
    enc.bindLabel(lbl);
    enc.stp_pre(arm64::FP, arm64::LR, arm64::SP, -64);
    enc.mov_rr(arm64::FP, arm64::SP);
    enc.str_imm(arm64::X0, arm64::FP, 16);
    size_t haveStr = enc.createLabel();
    enc.cbnz(arm64::X0, haveStr);
    enc.adr_data(arm64::X0, offEmptyStr);
    enc.str_imm(arm64::X0, arm64::FP, 16);
    enc.bindLabel(haveStr);
    enc.bl(lblStrLen);
    enc.str_imm(arm64::X0, arm64::FP, 24); // n
    enc.add_rri(arm64::X0, arm64::X0, 1);
    enc.bl(lblMalloc);
    enc.str_imm(arm64::X0, arm64::FP, 32); // out

    enc.ldr_imm(arm64::X6, arm64::FP, 16);
    enc.ldr_imm(arm64::X5, arm64::FP, 24);
    enc.ldr_imm(arm64::X7, arm64::FP, 32);
    enc.movz(arm64::X4, 0, 0);

    size_t loop = enc.createLabel();
    size_t store = enc.createLabel();
    size_t done = enc.createLabel();
    enc.bindLabel(loop);
    enc.cmp_rr(arm64::X4, arm64::X5);
    enc.b_cond(arm64::GE, done);
    enc.add_rrr(arm64::X9, arm64::X6, arm64::X4);
    enc.ldrb_imm(arm64::X10, arm64::X9, 0);
    enc.movz(arm64::X11, toUpper ? 'a' : 'A', 0);
    enc.cmp_rr(arm64::X10, arm64::X11);
    enc.b_cond(arm64::LT, store);
    enc.movz(arm64::X11, toUpper ? 'z' : 'Z', 0);
    enc.cmp_rr(arm64::X10, arm64::X11);
    enc.b_cond(arm64::GT, store);
    if (toUpper) enc.sub_rri(arm64::X10, arm64::X10, 32);
    else         enc.add_rri(arm64::X10, arm64::X10, 32);
    enc.bindLabel(store);
    enc.add_rrr(arm64::X12, arm64::X7, arm64::X4);
    enc.strb_imm(arm64::X10, arm64::X12, 0);
    enc.add_rri(arm64::X4, arm64::X4, 1);
    enc.b(loop);
    enc.bindLabel(done);
    enc.add_rrr(arm64::X12, arm64::X7, arm64::X5);
    enc.movz(arm64::X13, 0, 0);
    enc.strb_imm(arm64::X13, arm64::X12, 0);
    enc.mov_rr(arm64::X0, arm64::X7);
    enc.ldp_post(arm64::FP, arm64::LR, arm64::SP, 64);
    enc.ret();
}

inline void emitStrStrip(Encoder& enc, size_t lblStrStrip, size_t lblStrLen, size_t lblMalloc, size_t lblMemCpy, uint32_t offEmptyStr) {
    enc.bindLabel(lblStrStrip);
    enc.stp_pre(arm64::FP, arm64::LR, arm64::SP, -96);
    enc.mov_rr(arm64::FP, arm64::SP);
    enc.str_imm(arm64::X0, arm64::FP, 16); // s

    size_t emptyLbl = enc.createLabel();
    size_t lloop = enc.createLabel();
    size_t lskip = enc.createLabel();
    size_t ldone = enc.createLabel();
    size_t rloop = enc.createLabel();
    size_t rskip = enc.createLabel();
    size_t rdone = enc.createLabel();

    enc.cbz(arm64::X0, emptyLbl);
    enc.bl(lblStrLen);
    enc.str_imm(arm64::X0, arm64::FP, 24); // n
    enc.cbz(arm64::X0, emptyLbl);

    enc.ldr_imm(arm64::X6, arm64::FP, 16);
    enc.movz(arm64::X4, 0, 0);
    enc.bindLabel(lloop);
    enc.add_rrr(arm64::X9, arm64::X6, arm64::X4);
    enc.ldrb_imm(arm64::X10, arm64::X9, 0);
    enc.cbz(arm64::X10, emptyLbl);
    enc.cmp_ri(arm64::X10, ' ');
    enc.b_cond(arm64::EQ, lskip);
    enc.cmp_ri(arm64::X10, '\t');
    enc.b_cond(arm64::EQ, lskip);
    enc.cmp_ri(arm64::X10, '\n');
    enc.b_cond(arm64::EQ, lskip);
    enc.cmp_ri(arm64::X10, '\r');
    enc.b_cond(arm64::EQ, lskip);
    enc.b(ldone);
    enc.bindLabel(lskip);
    enc.add_rri(arm64::X4, arm64::X4, 1);
    enc.b(lloop);

    enc.bindLabel(ldone);
    enc.str_imm(arm64::X4, arm64::FP, 32);      // start (index)
    enc.ldr_imm(arm64::X5, arm64::FP, 24);
    enc.sub_rri(arm64::X5, arm64::X5, 1);       // j = n - 1 (index)
    enc.bindLabel(rloop);
    enc.cmp_rr(arm64::X5, arm64::X4);
    enc.b_cond(arm64::LT, rdone);
    enc.add_rrr(arm64::X9, arm64::X6, arm64::X5);
    enc.ldrb_imm(arm64::X10, arm64::X9, 0);
    enc.cmp_ri(arm64::X10, ' ');
    enc.b_cond(arm64::EQ, rskip);
    enc.cmp_ri(arm64::X10, '\t');
    enc.b_cond(arm64::EQ, rskip);
    enc.cmp_ri(arm64::X10, '\n');
    enc.b_cond(arm64::EQ, rskip);
    enc.cmp_ri(arm64::X10, '\r');
    enc.b_cond(arm64::EQ, rskip);
    enc.b(rdone);
    enc.bindLabel(rskip);
    enc.sub_rri(arm64::X5, arm64::X5, 1);
    enc.b(rloop);

    enc.bindLabel(rdone);
    enc.sub_rrr(arm64::X2, arm64::X5, arm64::X4);
    enc.add_rri(arm64::X2, arm64::X2, 1);       // newLen
    enc.str_imm(arm64::X2, arm64::FP, 40);
    enc.add_rri(arm64::X0, arm64::X2, 1);
    enc.bl(lblMalloc);
    enc.str_imm(arm64::X0, arm64::FP, 48);

    enc.ldr_imm(arm64::X1, arm64::FP, 16);
    enc.ldr_imm(arm64::X9, arm64::FP, 32);
    enc.add_rrr(arm64::X1, arm64::X1, arm64::X9);
    enc.ldr_imm(arm64::X2, arm64::FP, 40);
    enc.ldr_imm(arm64::X0, arm64::FP, 48);
    enc.bl(lblMemCpy);
    enc.ldr_imm(arm64::X0, arm64::FP, 48);
    enc.ldr_imm(arm64::X2, arm64::FP, 40);
    enc.add_rrr(arm64::X3, arm64::X0, arm64::X2);
    enc.movz(arm64::X4, 0, 0);
    enc.strb_imm(arm64::X4, arm64::X3, 0);
    enc.ldp_post(arm64::FP, arm64::LR, arm64::SP, 96);
    enc.ret();

    enc.bindLabel(emptyLbl);
    enc.adr_data(arm64::X0, offEmptyStr);
    enc.ldp_post(arm64::FP, arm64::LR, arm64::SP, 96);
    enc.ret();
}

} // namespace arm64

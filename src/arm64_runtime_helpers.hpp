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

inline void emitStrTitle(Encoder& enc, size_t lblStrTitle, size_t lblStrLower, uint32_t offEmptyStr) {
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

inline void emitStrFind(Encoder& enc, size_t lblStrFind, size_t lblStrLen) {
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

inline void emitStrStartsWith(Encoder& enc, size_t lblStrStartsWith, size_t lblStrLen) {
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

inline void emitStrEndsWith(Encoder& enc, size_t lblStrEndsWith, size_t lblStrLen) {
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

inline void emitStrCount(Encoder& enc, size_t lblStrCount, size_t lblStrFind, size_t lblStrLen) {
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

inline void emitStrReplace(Encoder& enc, size_t lblStrReplace, size_t lblStrLen, size_t lblStrFind, size_t lblMalloc, size_t lblMemCpy, uint32_t offEmptyStr) {
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

inline void emitStrSplit(Encoder& enc, size_t lblStrSplit, size_t lblStrLen, size_t lblStrFind, size_t lblMalloc, size_t lblMemCpy, uint32_t offEmptyStr) {
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

inline void emitStrJoin(Encoder& enc, size_t lblStrJoin, size_t lblStrLen, size_t lblMalloc, size_t lblMemCpy, uint32_t offEmptyStr) {
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

inline void emitStrCmpEq(Encoder& enc, size_t lblStrCmpEq) {
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

inline void emitListIn(Encoder& enc, size_t lblListIn, size_t lblStrCmpEq) {
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

inline void emitRandChoice(Encoder& enc, size_t lblRandChoice, size_t lblRand, uint32_t offEmptyStr) {
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

} // namespace arm64

#pragma once

#include "arm64_encoder.hpp"
#include "unicode_case.hpp"

namespace arm64 {

inline void emitCaseConversion(Encoder& enc, size_t lowerEntry, size_t upperEntry,
                               size_t titleEntry, size_t length, size_t allocate,
                               uint32_t emptyString, uint32_t table) {
    const auto convert = enc.createLabel();
    enc.bindLabel(lowerEntry);
    enc.movz(X1, 0, 0);
    enc.b(convert);
    enc.bindLabel(upperEntry);
    enc.movz(X1, 1, 0);
    enc.b(convert);
    enc.bindLabel(titleEntry);
    enc.movz(X1, 2, 0);

    auto empty = enc.createLabel(), done = enc.createLabel(), next = enc.createLabel();
    auto sizeReady = enc.createLabel(), sizeTwo = enc.createLabel(), sizeThree = enc.createLabel();
    auto continuation = enc.createLabel(), lookup = enc.createLabel(), search = enc.createLabel();
    auto raiseLow = enc.createLabel(), lowerHigh = enc.createLabel(), unmatched = enc.createLabel();
    auto upper = enc.createLabel(), lower = enc.createLabel(), space = enc.createLabel();
    auto writeByte = enc.createLabel(), finish = enc.createLabel();

    enc.bindLabel(convert);
    enc.stp_pre(FP, LR, SP, -32);
    enc.mov_rr(FP, SP);
    enc.str_imm(X0, FP, 16);
    enc.str_imm(X1, FP, 24);
    enc.cbz(X0, empty);
    enc.bl(length);
    enc.lsl_ri(X0, X0, 2);
    enc.add_rri(X0, X0, 1);
    enc.bl(allocate);
    enc.cbz(X0, empty);
    enc.ldr_imm(X4, FP, 16); // input cursor
    enc.mov_rr(X5, X0);      // output base
    enc.mov_rr(X6, X0);      // output cursor
    enc.adr_data(X7, table);
    enc.ldr_imm(X8, FP, 24); // operation
    enc.movz(X9, 1, 0);      // next character starts a title word

    enc.bindLabel(next);
    enc.ldrb_imm(X10, X4, 0); // packed input
    enc.cbz(X10, finish);
    enc.add_rri(X4, X4, 1);
    enc.cmp_ri(X10, 0xc2);
    enc.b_cond(LT, lookup);
    enc.cmp_ri(X10, 0xf4);
    enc.b_cond(GT, lookup);
    enc.cmp_ri(X10, 0xe0);
    enc.b_cond(LT, sizeTwo);
    enc.cmp_ri(X10, 0xf0);
    enc.b_cond(LT, sizeThree);
    enc.movz(X13, 32, 0);
    enc.b(sizeReady);
    enc.bindLabel(sizeThree);
    enc.movz(X13, 24, 0);
    enc.b(sizeReady);
    enc.bindLabel(sizeTwo);
    enc.movz(X13, 16, 0);
    enc.bindLabel(sizeReady);
    enc.movz(X11, 8, 0);
    enc.bindLabel(continuation);
    enc.ldrb_imm(X12, X4, 0);
    enc.cmp_ri(X12, 0x80);
    enc.b_cond(LT, lookup);
    enc.cmp_ri(X12, 0xbf);
    enc.b_cond(GT, lookup);
    enc.lsl_rrr(X12, X12, X11);
    enc.orr_rrr(X10, X10, X12);
    enc.add_rri(X4, X4, 1);
    enc.add_rri(X11, X11, 8);
    enc.cmp_rr(X11, X13);
    enc.b_cond(LT, continuation);

    enc.bindLabel(lookup);
    enc.movz(X13, 0, 0); // lower bound
    enc.mov_ri64(X14, thava_unicode::caseMappingCount); // exclusive upper bound
    enc.bindLabel(search);
    enc.cmp_rr(X13, X14);
    enc.b_cond(GE, unmatched);
    enc.add_rrr(X15, X13, X14);
    enc.lsr_ri(X15, X15, 1);
    enc.lsl_ri(X16, X15, 5);
    enc.add_rrr(X16, X7, X16);
    enc.ldr_imm(X0, X16, 0);
    enc.cmp_rr(X0, X10);
    enc.b_cond(LT, raiseLow);
    enc.b_cond(GT, lowerHigh);
    enc.cbz(X8, lower);
    enc.cmp_ri(X8, 1);
    enc.b_cond(EQ, upper);
    enc.ldr_imm(X0, X16, 24);
    enc.cbz(X0, space);
    enc.cbz(X9, lower);
    enc.movz(X9, 0, 0);
    enc.b(writeByte);

    enc.bindLabel(lower);
    enc.ldr_imm(X0, X16, 8);
    enc.movz(X9, 0, 0);
    enc.b(writeByte);
    enc.bindLabel(upper);
    enc.ldr_imm(X0, X16, 16);
    enc.b(writeByte);
    enc.bindLabel(raiseLow);
    enc.add_rri(X13, X15, 1);
    enc.b(search);
    enc.bindLabel(lowerHigh);
    enc.mov_rr(X14, X15);
    enc.b(search);
    enc.bindLabel(space);
    enc.movz(X9, 1, 0);
    enc.mov_rr(X0, X10);
    enc.b(writeByte);
    enc.bindLabel(unmatched);
    enc.movz(X9, 0, 0);
    enc.mov_rr(X0, X10);

    enc.bindLabel(writeByte);
    enc.strb_imm(X0, X6, 0);
    enc.add_rri(X6, X6, 1);
    enc.lsr_ri(X0, X0, 8);
    enc.cbnz(X0, writeByte);
    enc.b(next);
    enc.bindLabel(finish);
    enc.strb_imm(X10, X6, 0);
    enc.mov_rr(X0, X5);
    enc.b(done);
    enc.bindLabel(empty);
    enc.adr_data(X0, emptyString);
    enc.bindLabel(done);
    enc.ldp_post(FP, LR, SP, 32);
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

inline void emitDictFind(Encoder& enc, size_t lblDictFind, size_t lblStrCmpEq) {
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


// X0 = list, X1 = weights, X2 = weights are stored as double bits.
inline void emitWeightedChoice(Encoder& enc, size_t entry, size_t rand,
                               size_t choice, size_t print, size_t exitLabel,
                               uint32_t mismatchMessage) {
    enc.bindLabel(entry);
    auto empty = enc.createLabel(), mismatch = enc.createLabel();
    auto totalLoop = enc.createLabel(), selectLoop = enc.createLabel();
    auto weightReady = enc.createLabel(), skipTotal = enc.createLabel();
    auto draw = enc.createLabel(), fallback = enc.createLabel();
    auto chosen = enc.createLabel(), selectReady = enc.createLabel();
    auto next = enc.createLabel();

    enc.ldr_imm(X3, X0, 0);
    enc.cbz(X3, empty);
    enc.ldr_imm(X4, X1, 0);
    enc.cmp_rr(X3, X4);
    enc.b_cond(NE, mismatch);

    enc.stp_pre(FP, LR, SP, -64);
    enc.mov_rr(FP, SP);
    enc.str_imm(X0, FP, 16);
    enc.str_imm(X1, FP, 24);
    enc.str_imm(X2, FP, 32);
    enc.str_imm(X3, FP, 40);
    enc.movz(X4, 0, 0);
    enc.fmov_dx(D2, X4);
    enc.fmov_dx(D3, X4);
    enc.add_rri(X1, X1, 8);
    enc.bindLabel(totalLoop);
    enc.ldr_reg(X5, X1, X4);
    enc.fmov_dx(D0, X5);
    enc.cbnz(X2, weightReady);
    enc.scvtf_dr(D0, X5);
    enc.bindLabel(weightReady);
    enc.fcmp_dd(D0, D3);
    enc.b_cond(LE, skipTotal);
    enc.b_cond(VS, skipTotal);
    enc.fadd_ddd(D2, D2, D0);
    enc.bindLabel(skipTotal);
    enc.add_rri(X4, X4, 8);
    enc.sub_rri(X3, X3, 1);
    enc.cbnz(X3, totalLoop);
    enc.fcmp_dd(D2, D3);
    enc.b_cond(GT, draw);
    enc.b(fallback);

    enc.bindLabel(draw);
    enc.fstr_imm(D2, FP, 48);
    enc.bl(rand);
    enc.scvtf_dr(D0, X0);
    enc.movz(X9, 32768, 0);
    enc.scvtf_dr(D1, X9);
    enc.fdiv_ddd(D0, D0, D1);
    enc.fldr_imm(D2, FP, 48);
    enc.fmul_ddd(D2, D2, D0); // target in [0,total)
    enc.movz(X4, 0, 0);
    enc.fmov_dx(D3, X4);
    enc.ldr_imm(X1, FP, 24);
    enc.add_rri(X1, X1, 8);
    enc.ldr_imm(X2, FP, 32);
    enc.ldr_imm(X3, FP, 40);
    enc.bindLabel(selectLoop);
    enc.ldr_reg(X5, X1, X4);
    enc.fmov_dx(D0, X5);
    enc.cbnz(X2, selectReady);
    enc.scvtf_dr(D0, X5);
    enc.bindLabel(selectReady);
    enc.fcmp_dd(D0, D3);
    enc.b_cond(LE, next);
    enc.b_cond(VS, next);
    enc.fcmp_dd(D2, D0);
    enc.b_cond(LT, chosen);
    enc.fsub_ddd(D2, D2, D0);
    enc.bindLabel(next);
    enc.add_rri(X4, X4, 8);
    enc.sub_rri(X3, X3, 1);
    enc.cbnz(X3, selectLoop);
    enc.bindLabel(fallback);
    enc.ldr_imm(X0, FP, 16);
    enc.ldp_post(FP, LR, SP, 64);
    enc.b(choice);

    enc.bindLabel(chosen);
    enc.ldr_imm(X0, FP, 16);
    enc.add_rri(X0, X0, 8);
    enc.ldr_reg(X0, X0, X4);
    enc.ldp_post(FP, LR, SP, 64);
    enc.ret();

    enc.bindLabel(empty);
    enc.b(choice);

    enc.bindLabel(mismatch);
    enc.stp_pre(FP, LR, SP, -16);
    enc.mov_rr(FP, SP);
    enc.adr_data(X0, mismatchMessage);
    enc.bl(print);
    enc.movz(X0, 1, 0);
    enc.bl(exitLabel);
}

} // namespace arm64

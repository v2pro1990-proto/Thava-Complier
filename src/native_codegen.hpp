#pragma once

#include "unicode_case.hpp"

#include "pe_emitter.hpp"
#include "elf_emitter.hpp"
#include "linux_runtime.hpp"
#include "x86_encoder.hpp"
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <iostream>
#include <sstream>
#include <type_traits>

// Forward declaration
ASTPtr parseThavaExpression(const std::string& src);
class OsMethodNode;
class TimeMethodNode;
class CLibMethodNode;

template<typename TEmitter = PeEmitter>
class NativeCodegen {
public:
    TEmitter& pe;
    x86::Encoder& enc;
    std::shared_ptr<Environment> globalEnv;

    struct VarInfo {
        int32_t offset;
        ThavaType type;
        std::string customType;
        // Collection parameters (<lst>/<tple>/<dict>) are passed by reference:
        // listOutSlot holds the address of the caller's slot for that variable.
        bool listOut = false;
        int32_t listOutSlot = 0;
    };

    static bool isCollectionType(ThavaType t) {
        return t == ThavaType::LIST || t == ThavaType::TUPLE || t == ThavaType::DICT;
    }

    struct FuncInfo {
        size_t labelId;
        ThavaType returnType;
        std::vector<std::pair<ThavaType, std::string>> params;
    };

    std::map<std::string, FuncInfo> functions;
    std::vector<std::map<std::string, VarInfo>> scopeStack;
    int32_t currentFrameOffset = -32;
    int32_t maxFrameOffset = -32;
    size_t currentFuncRetLabel = 0;
    ThavaType currentFuncRetType = ThavaType::UNKNOWN;
    std::string currentFuncRetWidth; // "" = int32, "int64", "int16"

    // Loop control
    std::vector<size_t> breakLabels;
    std::vector<size_t> continueLabels;

    // Standard string formats cached in .rdata (offset within .rdata)
    uint32_t offFmtIntNL = 0;      // "%lld\n"
    uint32_t offFmtInt = 0;        // "%lld"
    uint32_t offFmtFloatNL = 0;    // "%g\n"
    uint32_t offFmtFloat = 0;      // "%g"
    uint32_t offFmtFloat6 = 0;     // "%.6f" (double -> string, six decimals)
    uint32_t offFmtStrNL = 0;      // "%s\n"
    uint32_t offFmtStr = 0;        // "%s"
    uint32_t offFmtNL = 0;         // "\n"
    uint32_t offStrTrue = 0;       // "true"
    uint32_t offStrFalse = 0;      // "false"
    uint32_t offFmtScanInt = 0;    // "%lld"
    uint32_t offFmtScanDouble = 0; // "%lf"
    uint32_t offFmtScanFloat = 0;  // "%f"
    uint32_t offFmtScanLine = 0;   // " %1023[^\n]"
    uint32_t offEmptyStr = 0;      // ""
    uint32_t offSuccessMsg = 0;    // "\n[Thava Native] Compilation and execution successful!\n"
    uint32_t offListOpen = 0;      // "["
    uint32_t offListClose = 0;     // "]\n"
    uint32_t offListComma = 0;     // ", "

    // Runtime helper labels
    size_t lblStrConcat = 0;
    size_t lblStrMul = 0;
    size_t lblStrEquals = 0;
    size_t lblListIn = 0;
    size_t lblRandChoice = 0;
    size_t lblStrLower = 0;
    size_t lblStrUpper = 0;
    size_t lblStrTitle = 0;
    size_t lblStrStrip = 0;
    size_t lblStrReplace = 0;
    size_t lblStrFind = 0;
    size_t lblStrStartswith = 0;
    size_t lblStrEndswith = 0;
    size_t lblStrCount = 0;
    size_t lblStrJoin = 0;
    size_t lblStrSplit = 0;
    size_t lblInputLine = 0;
    size_t lblStrSplitLast = 0;
    size_t lblStrSubstring = 0;
    size_t lblFileReadAll = 0;
    size_t lblFileReadLine = 0;
    size_t lblFileReadLines = 0;
    size_t lblThrow = 0;

    std::map<std::string, ThavaType> listElemTypes;
    ThavaType lastCollectionElemType = ThavaType::UNKNOWN;

    int optLevel = 0;
    std::map<ASTNode*, int32_t> deferNodeToFlagSlot;

    void setOptLevel(int level) { optLevel = level; }

    // Truncates a 64-bit value in `r` to the declared integer width and
    // sign-extends it back: <int> = 32-bit, <int16> = 16-bit, <int64> = 64-bit.
    void narrowIntReg(x86::Reg r, const std::string& width) {
        if (width == "int64") return;
        uint8_t bits = (width == "int16") ? 16 : 32;
        // Shift the whole 64-bit register so the sign bit of the target width
        // lands in bit 63, then shift back to sign-extend.
        enc.shl_ri(r, 64 - bits);
        enc.sar_ri(r, 64 - bits);
    }

    // Top-level variables (globals) live in the program frame. A data cell holds
    // that frame pointer so functions and methods can reach them.
    uint32_t globalBaseCell = 0;
    std::map<std::string, int32_t> globalVarSlots;
    bool emittingProgramBody = false;

    void loadCollectionPtr(const std::string& name, VarInfo* local, x86::Reg dst) {
        if (local) enc.mov_rm_rbp(dst, local->offset);
        else {
            emitGlobalBase(dst, globalVarSlots.at(name));
            enc.mov_rm(dst, dst, 0);
        }
    }

    bool isGlobalName(const std::string& name) const {
        return globalVarSlots.count(name) > 0;
    }

    // First free frame slot of the program body (after the globals).
    int32_t programFrameBase = -32;

    // dst = address of a global slot: the program frame pointer plus the slot
    // offset (globals live in the frame of the program body).
    void emitGlobalBase(x86::Reg dst, int32_t slotOffset = 0) {
        enc.lea_rip_rdata(dst, globalBaseCell);
        enc.mov_rm(dst, dst, 0);
        if (slotOffset != 0) enc.add_ri(dst, slotOffset);
    }

    // Store an integer/pointer value into a variable slot, honouring the
    // variable's declared integer width.
    void storeVarFromReg(int32_t offset, x86::Reg reg, VarInfo* vi) {
        if (vi && vi->type == ThavaType::INT) narrowIntReg(reg, vi->customType);
        enc.mov_mr_rbp(offset, reg);
    }

    void invalidateRegCache() {}

    void collectDefers(ASTNode* node, std::vector<DeferNode*>& out) {
        if (!node) return;
        if (auto d = dynamic_cast<DeferNode*>(node)) {
            out.push_back(d);
            return;
        }
        if (auto b = dynamic_cast<BlockNode*>(node)) {
            for (const auto& s : b->statements) collectDefers(s.get(), out);
        } else if (auto i = dynamic_cast<IfNode*>(node)) {
            collectDefers(i->thenBranch.get(), out);
            if (i->elseBranch) collectDefers(i->elseBranch.get(), out);
        } else if (auto w = dynamic_cast<WhileNode*>(node)) {
            collectDefers(w->body.get(), out);
        } else if (auto f = dynamic_cast<ForNode*>(node)) {
            collectDefers(f->body.get(), out);
        } else if (auto r = dynamic_cast<RangeForNode*>(node)) {
            collectDefers(r->body.get(), out);
        }
    }

    NativeCodegen(TEmitter& p, x86::Encoder& e, std::shared_ptr<Environment> env)
        : pe(p), enc(e), globalEnv(env) {}

    // Thava is function-scoped: a declaration inside any block (if/while/for/try)
    // stays visible until the end of the enclosing function, so nested blocks do
    // not open their own scope. Only functions, methods and the top-level body do.
    void enterScope() {
        invalidateRegCache();
    }

    void exitScope() {
        invalidateRegCache();
    }

    void enterFunctionScope() {
        invalidateRegCache();
        scopeStack.push_back({});
    }

    void exitFunctionScope() {
        invalidateRegCache();
        if (!scopeStack.empty()) scopeStack.pop_back();
    }

    // Scopes below this index are invisible to the code being emitted: while a
    // function/method is emitted, the program scope (which holds the globals as
    // frame slots of main) must not resolve, because those slots belong to
    // another frame.
    size_t lookupFloor = 0;

    VarInfo* lookupVar(const std::string& name) {
        for (int i = (int)scopeStack.size() - 1; i >= (int)lookupFloor; --i) {
            auto it = scopeStack[i].find(name);
            if (it != scopeStack[i].end()) return &it->second;
        }
        return nullptr;
    }

    int32_t declareVar(const std::string& name, ThavaType type, const std::string& customType = "") {
        currentFrameOffset -= 8;
        if (currentFrameOffset < maxFrameOffset) maxFrameOffset = currentFrameOffset;
        VarInfo info{ currentFrameOffset, type, customType };
        scopeStack.back()[name] = info;
        return currentFrameOffset;
    }

    void initFormats() {
        offFmtIntNL    = pe.addStringLiteralOffset("%lld\n");
        offFmtInt      = pe.addStringLiteralOffset("%lld");
        offFmtFloatNL  = pe.addStringLiteralOffset("%g\n");
        offFmtFloat    = pe.addStringLiteralOffset("%g");
        offFmtFloat6   = pe.addStringLiteralOffset("%.6f");
        offFmtStrNL    = pe.addStringLiteralOffset("%s\n");
        offFmtStr      = pe.addStringLiteralOffset("%s");
        offFmtNL       = pe.addStringLiteralOffset("\n");
        offStrTrue     = pe.addStringLiteralOffset("true");
        offStrFalse    = pe.addStringLiteralOffset("false");
        offFmtScanInt  = pe.addStringLiteralOffset("%lld");
        offFmtScanDouble = pe.addStringLiteralOffset("%lf");
        offFmtScanFloat  = pe.addStringLiteralOffset("%f");
        offFmtScanLine = pe.addStringLiteralOffset(" %1023[^\n]");
        offEmptyStr    = pe.addStringLiteralOffset("");
        offSuccessMsg  = pe.addStringLiteralOffset("\n[Thava Native] Build and run successful!\n");
        offListOpen    = pe.addStringLiteralOffset("[");
        offListClose   = pe.addStringLiteralOffset("]\n");
        offListComma   = pe.addStringLiteralOffset(", ");
    }

    void emitCallPrintf(uint32_t fmtOffsetInRdata, x86::Reg arg2 = x86::RAX, bool hasArg2 = false) {
        if (hasArg2) {
            if (arg2 != x86::RDX) enc.mov_rr(x86::RDX, arg2);
        }
        enc.lea_rip_rdata(x86::RCX, fmtOffsetInRdata);
        enc.call_iat_func("printf");
    }

    void emitCallPrintfFloat(uint32_t fmtOffsetInRdata, x86::Xmm argXmm) {
        if (argXmm != x86::XMM1) enc.movsd_rr(x86::XMM1, argXmm);
        enc.movsd_mr_rbp(-8, x86::XMM1);
        enc.mov_rm_rbp(x86::RDX, -8);

        enc.lea_rip_rdata(x86::RCX, fmtOffsetInRdata);
        enc.call_iat_func("printf");
    }

    void emitUnicodeCaseHelpers() {
        using namespace x86;
        const auto table = thava_unicode::addCaseMappings(pe);
        const auto convert = enc.createLabel();
        lblStrLower = enc.createLabel();
        lblStrUpper = enc.createLabel();
        lblStrTitle = enc.createLabel();
        enc.bindLabel(lblStrLower);
        enc.mov_ri64(RDX, 0);
        enc.jmp(convert);
        enc.bindLabel(lblStrUpper);
        enc.mov_ri64(RDX, 1);
        enc.jmp(convert);
        enc.bindLabel(lblStrTitle);
        enc.mov_ri64(RDX, 2);

        auto empty = enc.createLabel(), done = enc.createLabel(), next = enc.createLabel();
        auto sizeReady = enc.createLabel(), sizeTwo = enc.createLabel(), sizeThree = enc.createLabel();
        auto continuation = enc.createLabel(), lookup = enc.createLabel(), search = enc.createLabel();
        auto raiseLow = enc.createLabel(), lowerHigh = enc.createLabel(), unmatched = enc.createLabel();
        auto upper = enc.createLabel(), lower = enc.createLabel(), space = enc.createLabel();
        auto writeByte = enc.createLabel(), finish = enc.createLabel();

        enc.bindLabel(convert);
        // Seven saved registers align the stack before calling the runtime.
        for (auto reg : {RBP, R12, RSI, RDI, RBX, R13, R14}) enc.push(reg);
        enc.sub_rsp(32);  // Windows call shadow space
        enc.mov_rr(RBX, RCX); // input cursor
        enc.mov_rr(R12, RDX); // operation
        enc.test_rr(RCX, RCX);
        enc.je(empty);
        enc.call_iat_func("strlen");
        enc.shl_ri(RAX, 2); // worst-case unconditional expansion: 4 bytes per input byte
        enc.inc_r(RAX);
        enc.mov_rr(RCX, RAX);
        enc.call_iat_func("malloc");
        enc.test_rr(RAX, RAX);
        enc.je(empty);
        enc.mov_rr(RDI, RAX); // buffer base
        enc.mov_rr(R13, RAX); // output cursor
        enc.lea_rip_rdata(R14, table);
        enc.mov_ri64(RSI, 1); // next character starts a title word

        enc.bindLabel(next);
        enc.movzx_rm8(RAX, RBX);
        enc.test_rr(RAX, RAX);
        enc.je(finish);
        enc.inc_r(RBX);
        enc.mov_rr(RDX, RAX); // packed input character
        enc.cmp_ri(RAX, 0xc2);
        enc.jl(lookup);
        enc.cmp_ri(RAX, 0xf4);
        enc.jg(lookup);
        enc.cmp_ri(RAX, 0xe0);
        enc.jl(sizeTwo);
        enc.cmp_ri(RAX, 0xf0);
        enc.jl(sizeThree);
        enc.mov_ri64(R11, 32);
        enc.jmp(sizeReady);
        enc.bindLabel(sizeThree);
        enc.mov_ri64(R11, 24);
        enc.jmp(sizeReady);
        enc.bindLabel(sizeTwo);
        enc.mov_ri64(R11, 16);
        enc.bindLabel(sizeReady);
        enc.mov_ri64(RCX, 8);
        enc.bindLabel(continuation);
        enc.movzx_rm8(RAX, RBX);
        enc.cmp_ri(RAX, 0x80);
        enc.jl(lookup);
        enc.cmp_ri(RAX, 0xbf);
        enc.jg(lookup);
        enc.shl_cl(RAX);
        enc.or_rr(RDX, RAX);
        enc.inc_r(RBX);
        enc.add_ri(RCX, 8);
        enc.cmp_rr(RCX, R11);
        enc.jl(continuation);

        enc.bindLabel(lookup);
        enc.xor_r32(R8, R8); // lower bound
        enc.mov_ri64(R9, thava_unicode::caseMappingCount); // exclusive upper bound
        enc.bindLabel(search);
        enc.cmp_rr(R8, R9);
        enc.jge(unmatched);
        enc.mov_rr(R10, R8);
        enc.add_rr(R10, R9);
        enc.shr_ri(R10, 1);
        enc.mov_rr(R11, R10);
        enc.shl_ri(R11, 5);
        enc.add_rr(R11, R14);
        enc.mov_rm(RAX, R11, 0);
        enc.cmp_rr(RAX, RDX);
        enc.jl(raiseLow);
        enc.jg(lowerHigh);
        enc.cmp_ri(R12, 0);
        enc.je(lower);
        enc.cmp_ri(R12, 1);
        enc.je(upper);
        enc.mov_rm(RAX, R11, 24);
        enc.test_rr(RAX, RAX);
        enc.je(space);
        enc.test_rr(RSI, RSI);
        enc.je(lower);
        enc.xor_r32(RSI, RSI);
        enc.jmp(writeByte);

        enc.bindLabel(lower);
        enc.mov_rm(RAX, R11, 8);
        enc.xor_r32(RSI, RSI);
        enc.jmp(writeByte);
        enc.bindLabel(upper);
        enc.mov_rm(RAX, R11, 16);
        enc.jmp(writeByte);
        enc.bindLabel(raiseLow);
        enc.mov_rr(R8, R10);
        enc.inc_r(R8);
        enc.jmp(search);
        enc.bindLabel(lowerHigh);
        enc.mov_rr(R9, R10);
        enc.jmp(search);
        enc.bindLabel(space);
        enc.mov_ri64(RSI, 1);
        enc.mov_rr(RAX, RDX);
        enc.jmp(writeByte);
        enc.bindLabel(unmatched);
        enc.xor_r32(RSI, RSI);
        enc.mov_rr(RAX, RDX);
        enc.bindLabel(writeByte);
        enc.mov_mr8(R13, 0, RAX);
        enc.inc_r(R13);
        enc.shr_ri(RAX, 8);
        enc.jne(writeByte);
        enc.jmp(next);
        enc.bindLabel(finish);
        enc.mov_mr8(R13, 0, RAX); // terminating zero
        enc.mov_rr(RAX, RDI);
        enc.jmp(done);
        enc.bindLabel(empty);
        enc.lea_rip_rdata(RAX, offEmptyStr);
        enc.bindLabel(done);
        enc.add_rsp(32);
        for (auto reg : {R14, R13, RBX, RDI, RSI, R12, RBP}) enc.pop(reg);
        enc.ret();
    }

    void emitRuntimeHelpers() {
        // --- 1. _thava_str_concat(s1 in RCX, s2 in RDX) -> RAX ---
        lblStrConcat = enc.createLabel();
        enc.bindLabel(lblStrConcat);
        enc.push(x86::RBP);
        enc.mov_rr(x86::RBP, x86::RSP);
        enc.sub_rsp(128);

        enc.mov_mr_rbp(-8, x86::RCX);  // s1
        enc.mov_mr_rbp(-16, x86::RDX); // s2

        enc.call_iat_func("strlen");
        enc.mov_mr_rbp(-24, x86::RAX); // len1

        enc.mov_rm_rbp(x86::RCX, -16); // s2
        enc.call_iat_func("strlen");
        enc.mov_rm_rbp(x86::R10, -24);
        enc.add_rr(x86::RAX, x86::R10);
        enc.mov_ri64(x86::R10, 1);
        enc.add_rr(x86::RAX, x86::R10);

        enc.mov_rr(x86::RCX, x86::RAX);
        enc.call_iat_func("malloc");
        enc.mov_mr_rbp(-32, x86::RAX); // outStr

        enc.mov_rr(x86::RCX, x86::RAX);
        enc.mov_rm_rbp(x86::RDX, -8);
        enc.call_iat_func("strcpy");

        enc.mov_rm_rbp(x86::RCX, -32);
        enc.mov_rm_rbp(x86::RDX, -16);
        enc.call_iat_func("strcat");

        enc.mov_rm_rbp(x86::RAX, -32);
        enc.add_rsp(128);
        enc.pop(x86::RBP);
        enc.ret();

        // --- _thava_str_mul(s in RCX, count in RDX) -> RAX ---
        lblStrMul = enc.createLabel();
        enc.bindLabel(lblStrMul);
        enc.push(x86::RBP);
        enc.mov_rr(x86::RBP, x86::RSP);
        enc.sub_rsp(128);

        enc.mov_mr_rbp(-8, x86::RCX);  // s
        enc.mov_mr_rbp(-16, x86::RDX); // count

        size_t emptyLbl = enc.createLabel();
        size_t doneMulLbl = enc.createLabel();

        // if count <= 0 -> return ""
        enc.mov_ri64(x86::R10, 0);
        enc.cmp_rr(x86::RDX, x86::R10);
        enc.jle(emptyLbl);

        // if s == NULL -> return ""
        enc.test_rr(x86::RCX, x86::RCX);
        enc.je(emptyLbl);

        // len = strlen(s)
        enc.call_iat_func("strlen");
        enc.mov_mr_rbp(-24, x86::RAX); // len

        // if len == 0 -> return ""
        enc.test_rr(x86::RAX, x86::RAX);
        enc.je(emptyLbl);

        // totalBytes = len * count + 1
        enc.mov_rm_rbp(x86::R10, -16); // count
        enc.imul_rr(x86::RAX, x86::R10);
        enc.mov_ri64(x86::R10, 1);
        enc.add_rr(x86::RAX, x86::R10);

        // malloc(totalBytes)
        enc.mov_rr(x86::RCX, x86::RAX);
        enc.call_iat_func("malloc");
        enc.mov_mr_rbp(-32, x86::RAX); // buf

        // i = 0
        enc.mov_ri64(x86::RAX, 0);
        enc.mov_mr_rbp(-40, x86::RAX);

        size_t loopMul = enc.createLabel();
        size_t endLoopMul = enc.createLabel();

        enc.bindLabel(loopMul);
        enc.mov_rm_rbp(x86::RAX, -40); // i
        enc.mov_rm_rbp(x86::R10, -16); // count
        enc.cmp_rr(x86::RAX, x86::R10);
        enc.jge(endLoopMul);

        // dst = buf + i * len
        enc.mov_rm_rbp(x86::RCX, -32); // buf
        enc.mov_rm_rbp(x86::R10, -24); // len
        enc.imul_rr(x86::R10, x86::RAX); // i * len
        enc.add_rr(x86::RCX, x86::R10); // dst = buf + i * len
        enc.mov_rm_rbp(x86::RDX, -8);  // src = s
        enc.mov_rm_rbp(x86::R8, -24);  // count = len
        enc.call_iat_func("memcpy");

        // i++
        enc.mov_rm_rbp(x86::RAX, -40);
        enc.mov_ri64(x86::R10, 1);
        enc.add_rr(x86::RAX, x86::R10);
        enc.mov_mr_rbp(-40, x86::RAX);
        enc.jmp(loopMul);

        enc.bindLabel(endLoopMul);
        // buf[len * count] = 0
        enc.mov_rm_rbp(x86::RAX, -32); // buf
        enc.mov_rm_rbp(x86::R10, -24); // len
        enc.mov_rm_rbp(x86::R11, -16); // count
        enc.imul_rr(x86::R10, x86::R11);
        enc.add_rr(x86::RAX, x86::R10);
        enc.xor_r32(x86::R10, x86::R10);
        enc.mov_mr8(x86::RAX, 0, x86::R10); // null terminate

        enc.mov_rm_rbp(x86::RAX, -32);
        enc.jmp(doneMulLbl);

        enc.bindLabel(emptyLbl);
        enc.lea_rip_rdata(x86::RAX, offEmptyStr);

        enc.bindLabel(doneMulLbl);
        enc.add_rsp(128);
        enc.pop(x86::RBP);
        enc.ret();

        // --- 2. _thava_str_equals(s1 in RCX, s2 in RDX) -> RAX ---
        lblStrEquals = enc.createLabel();
        enc.bindLabel(lblStrEquals);
        enc.push(x86::RBP);
        enc.mov_rr(x86::RBP, x86::RSP);
        enc.sub_rsp(64);

        enc.call_iat_func("strcmp");
        enc.test_rr(x86::RAX, x86::RAX);
        enc.setcc_to_rax(x86::Cond::E);

        enc.add_rsp(64);
        enc.pop(x86::RBP);
        enc.ret();

        // --- 3. _thava_list_in(item in RCX, list in RDX, isStr in R8) -> RAX ---
        lblListIn = enc.createLabel();
        enc.bindLabel(lblListIn);
        enc.push(x86::RBP);
        enc.mov_rr(x86::RBP, x86::RSP);
        enc.sub_rsp(128);

        enc.mov_mr_rbp(-8, x86::RCX);  // item
        enc.mov_mr_rbp(-16, x86::RDX); // list
        enc.mov_mr_rbp(-24, x86::R8);  // isStr

        size_t notFoundLbl = enc.createLabel();
        size_t foundLbl = enc.createLabel();
        size_t loopHeaderLbl = enc.createLabel();

        enc.test_rr(x86::RDX, x86::RDX);
        enc.je(notFoundLbl);

        enc.mov_rm(x86::R9, x86::RDX, 0); // count
        enc.mov_mr_rbp(-32, x86::R9);     // save count
        enc.mov_ri64(x86::RAX, 0);
        enc.mov_mr_rbp(-40, x86::RAX);     // i = 0

        enc.bindLabel(loopHeaderLbl);
        enc.mov_rm_rbp(x86::RAX, -40); // i
        enc.mov_rm_rbp(x86::R9, -32);  // count
        enc.cmp_rr(x86::RAX, x86::R9);
        enc.jge(notFoundLbl);

        // elem = [list + 8 + i * 8]
        enc.mov_rm_rbp(x86::RDX, -16); // list
        enc.mov_rm_rbp(x86::R10, -40); // i
        enc.mov_ri64(x86::RAX, 8);
        enc.imul_rr(x86::R10, x86::RAX);
        enc.add_rr(x86::RDX, x86::R10);
        enc.mov_rm(x86::RAX, x86::RDX, 8); // elem

        // check isStr
        enc.mov_rm_rbp(x86::R8, -24);
        enc.test_rr(x86::R8, x86::R8);
        size_t intCmpLbl = enc.createLabel();
        enc.je(intCmpLbl);

        // String cmp: strcmp(item, elem)
        enc.mov_rm_rbp(x86::RCX, -8);
        enc.mov_rr(x86::RDX, x86::RAX);
        enc.call_iat_func("strcmp");
        enc.test_rr(x86::RAX, x86::RAX);
        enc.je(foundLbl);
        size_t nextIterLbl = enc.createLabel();
        enc.jmp(nextIterLbl);

        // Int cmp: item == elem
        enc.bindLabel(intCmpLbl);
        enc.mov_rm_rbp(x86::R10, -8);
        enc.cmp_rr(x86::RAX, x86::R10);
        enc.je(foundLbl);

        enc.bindLabel(nextIterLbl);
        enc.mov_rm_rbp(x86::RAX, -40);
        enc.mov_ri64(x86::R10, 1);
        enc.add_rr(x86::RAX, x86::R10);
        enc.mov_mr_rbp(-40, x86::RAX);
        enc.jmp(loopHeaderLbl);

        enc.bindLabel(foundLbl);
        enc.mov_ri64(x86::RAX, 1);
        size_t exitInLbl = enc.createLabel();
        enc.jmp(exitInLbl);

        enc.bindLabel(notFoundLbl);
        enc.xor_r32(x86::RAX, x86::RAX);

        enc.bindLabel(exitInLbl);
        enc.add_rsp(128);
        enc.pop(x86::RBP);
        enc.ret();

        // --- 4. _thava_rand_choice(list in RCX) -> RAX ---
        lblRandChoice = enc.createLabel();
        enc.bindLabel(lblRandChoice);
        enc.push(x86::RBP);
        enc.mov_rr(x86::RBP, x86::RSP);
        enc.sub_rsp(64);

        enc.mov_mr_rbp(-8, x86::RCX);
        enc.test_rr(x86::RCX, x86::RCX);
        size_t emptyListLbl = enc.createLabel();
        enc.je(emptyListLbl);

        enc.mov_rm(x86::R10, x86::RCX, 0); // count
        enc.test_rr(x86::R10, x86::R10);
        enc.jle(emptyListLbl);
        enc.mov_mr_rbp(-16, x86::R10);

        enc.call_iat_func("rand");
        enc.mov_rm_rbp(x86::R10, -16);
        enc.cqo();
        enc.idiv_r(x86::R10); // RDX = rand() % count

        enc.mov_rm_rbp(x86::RCX, -8);
        enc.mov_ri64(x86::RAX, 8);
        enc.imul_rr(x86::RDX, x86::RAX);
        enc.add_rr(x86::RCX, x86::RDX);
        enc.mov_rm(x86::RAX, x86::RCX, 8); // elem
        size_t exitRandLbl = enc.createLabel();
        enc.jmp(exitRandLbl);

        enc.bindLabel(emptyListLbl);
        enc.xor_r32(x86::RAX, x86::RAX);

        enc.bindLabel(exitRandLbl);
        enc.add_rsp(64);
        enc.pop(x86::RBP);
        enc.ret();

        emitUnicodeCaseHelpers();

        // --- _thava_str_strip(s in RCX) -> RAX (heap string stripped of whitespace) ---
        lblStrStrip = enc.createLabel();
        enc.bindLabel(lblStrStrip);
        enc.push(x86::RBP);
        enc.mov_rr(x86::RBP, x86::RSP);
        enc.sub_rsp(128);

        enc.mov_mr_rbp(-8, x86::RCX); // s
        size_t emptyStrip = enc.createLabel();
        size_t exitStrip = enc.createLabel();

        enc.test_rr(x86::RCX, x86::RCX);
        enc.je(emptyStrip);

        enc.call_iat_func("strlen");
        enc.test_rr(x86::RAX, x86::RAX);
        enc.je(emptyStrip);
        enc.mov_mr_rbp(-16, x86::RAX); // len

        // p = s
        enc.mov_rm_rbp(x86::R10, -8);
        size_t lstripLoop = enc.createLabel();
        size_t lstripDone = enc.createLabel();
        enc.bindLabel(lstripLoop);
        enc.movzx_rm8(x86::RDX, x86::R10, 0);
        enc.test_rr(x86::RDX, x86::RDX);
        enc.je(emptyStrip);

        enc.mov_ri64(x86::RAX, ' ');
        enc.cmp_rr(x86::RDX, x86::RAX);
        size_t isWsL = enc.createLabel();
        enc.je(isWsL);
        enc.mov_ri64(x86::RAX, '\t');
        enc.cmp_rr(x86::RDX, x86::RAX);
        enc.je(isWsL);
        enc.mov_ri64(x86::RAX, '\n');
        enc.cmp_rr(x86::RDX, x86::RAX);
        enc.je(isWsL);
        enc.mov_ri64(x86::RAX, '\r');
        enc.cmp_rr(x86::RDX, x86::RAX);
        enc.je(isWsL);
        enc.jmp(lstripDone);

        enc.bindLabel(isWsL);
        enc.mov_ri64(x86::RAX, 1);
        enc.add_rr(x86::R10, x86::RAX);
        enc.jmp(lstripLoop);

        enc.bindLabel(lstripDone);
        enc.mov_mr_rbp(-24, x86::R10); // startPtr = p

        // q = s + len - 1
        enc.mov_rm_rbp(x86::R11, -8);
        enc.mov_rm_rbp(x86::RAX, -16);
        enc.add_rr(x86::R11, x86::RAX);
        enc.mov_ri64(x86::RAX, 1);
        enc.sub_rr(x86::R11, x86::RAX); // q

        size_t rstripLoop = enc.createLabel();
        size_t rstripDone = enc.createLabel();
        enc.bindLabel(rstripLoop);
        enc.mov_rm_rbp(x86::R10, -24); // p
        enc.cmp_rr(x86::R11, x86::R10);
        enc.jl(rstripDone);

        enc.movzx_rm8(x86::RDX, x86::R11, 0);
        enc.mov_ri64(x86::RAX, ' ');
        enc.cmp_rr(x86::RDX, x86::RAX);
        size_t isWsR = enc.createLabel();
        enc.je(isWsR);
        enc.mov_ri64(x86::RAX, '\t');
        enc.cmp_rr(x86::RDX, x86::RAX);
        enc.je(isWsR);
        enc.mov_ri64(x86::RAX, '\n');
        enc.cmp_rr(x86::RDX, x86::RAX);
        enc.je(isWsR);
        enc.mov_ri64(x86::RAX, '\r');
        enc.cmp_rr(x86::RDX, x86::RAX);
        enc.je(isWsR);
        enc.jmp(rstripDone);

        enc.bindLabel(isWsR);
        enc.mov_ri64(x86::RAX, 1);
        enc.sub_rr(x86::R11, x86::RAX);
        enc.jmp(rstripLoop);

        enc.bindLabel(rstripDone);
        // newLen = (q - p) + 1
        enc.mov_rm_rbp(x86::R10, -24); // p
        enc.mov_rr(x86::RAX, x86::R11); // q
        enc.sub_rr(x86::RAX, x86::R10); // q - p
        enc.mov_ri64(x86::R10, 1);
        enc.add_rr(x86::RAX, x86::R10); // newLen
        enc.mov_mr_rbp(-32, x86::RAX); // save newLen

        // malloc(newLen + 1)
        enc.mov_ri64(x86::R10, 1);
        enc.add_rr(x86::RAX, x86::R10);
        enc.mov_rr(x86::RCX, x86::RAX);
        enc.call_iat_func("malloc");
        enc.mov_mr_rbp(-40, x86::RAX); // outBuf

        // memcpy(outBuf, p, newLen)
        enc.mov_rr(x86::RCX, x86::RAX);
        enc.mov_rm_rbp(x86::RDX, -24); // p
        enc.mov_rm_rbp(x86::R8, -32);  // newLen
        enc.call_iat_func("memcpy");

        // outBuf[newLen] = 0
        enc.mov_rm_rbp(x86::RAX, -40); // outBuf
        enc.mov_rm_rbp(x86::R10, -32); // newLen
        enc.add_rr(x86::RAX, x86::R10);
        enc.xor_r32(x86::R10, x86::R10);
        enc.mov_mr8(x86::RAX, 0, x86::R10);

        enc.mov_rm_rbp(x86::RAX, -40);
        enc.jmp(exitStrip);

        enc.bindLabel(emptyStrip);
        enc.lea_rip_rdata(x86::RAX, offEmptyStr);

        enc.bindLabel(exitStrip);
        enc.add_rsp(128);
        enc.pop(x86::RBP);
        enc.ret();

        // --- _thava_str_find(s in RCX, sub in RDX) -> RAX (0-indexed pos or -1) ---
        lblStrFind = enc.createLabel();
        enc.bindLabel(lblStrFind);
        enc.push(x86::RBP);
        enc.mov_rr(x86::RBP, x86::RSP);
        enc.sub_rsp(64);

        enc.mov_mr_rbp(-8, x86::RCX);  // s
        enc.mov_mr_rbp(-16, x86::RDX); // sub

        size_t notFoundFind = enc.createLabel();
        size_t exitFind = enc.createLabel();

        enc.test_rr(x86::RCX, x86::RCX);
        enc.je(notFoundFind);
        enc.test_rr(x86::RDX, x86::RDX);
        enc.je(notFoundFind);

        enc.call_iat_func("strstr");
        enc.test_rr(x86::RAX, x86::RAX);
        enc.je(notFoundFind);

        // Found: RAX - s
        enc.mov_rm_rbp(x86::R10, -8);
        enc.sub_rr(x86::RAX, x86::R10);
        enc.jmp(exitFind);

        enc.bindLabel(notFoundFind);
        enc.mov_ri64(x86::RAX, -1);

        enc.bindLabel(exitFind);
        enc.add_rsp(64);
        enc.pop(x86::RBP);
        enc.ret();

        // --- _thava_str_startswith(s in RCX, prefix in RDX) -> RAX (bool) ---
        lblStrStartswith = enc.createLabel();
        enc.bindLabel(lblStrStartswith);
        enc.push(x86::RBP);
        enc.mov_rr(x86::RBP, x86::RSP);
        enc.sub_rsp(64);

        enc.mov_mr_rbp(-8, x86::RCX);  // s
        enc.mov_mr_rbp(-16, x86::RDX); // prefix

        size_t falseSw = enc.createLabel();
        size_t trueSw = enc.createLabel();
        size_t exitSw = enc.createLabel();

        enc.test_rr(x86::RCX, x86::RCX);
        enc.je(falseSw);
        enc.test_rr(x86::RDX, x86::RDX);
        enc.je(trueSw);

        enc.call_iat_func("strlen");
        enc.mov_mr_rbp(-24, x86::RAX); // lenS

        enc.mov_rm_rbp(x86::RCX, -16);
        enc.call_iat_func("strlen");
        enc.mov_mr_rbp(-32, x86::RAX); // lenPre

        enc.test_rr(x86::RAX, x86::RAX);
        enc.je(trueSw);

        enc.mov_rm_rbp(x86::R10, -24);
        enc.cmp_rr(x86::R10, x86::RAX);
        enc.jl(falseSw);

        enc.mov_rm_rbp(x86::RCX, -8);
        enc.mov_rm_rbp(x86::RDX, -16);
        enc.mov_rm_rbp(x86::R8, -32);
        enc.call_iat_func("strncmp");
        enc.test_rr(x86::RAX, x86::RAX);
        enc.jne(falseSw);

        enc.bindLabel(trueSw);
        enc.mov_ri64(x86::RAX, 1);
        enc.jmp(exitSw);

        enc.bindLabel(falseSw);
        enc.xor_r32(x86::RAX, x86::RAX);

        enc.bindLabel(exitSw);
        enc.add_rsp(64);
        enc.pop(x86::RBP);
        enc.ret();

        // --- _thava_str_endswith(s in RCX, suffix in RDX) -> RAX (bool) ---
        lblStrEndswith = enc.createLabel();
        enc.bindLabel(lblStrEndswith);
        enc.push(x86::RBP);
        enc.mov_rr(x86::RBP, x86::RSP);
        enc.sub_rsp(64);

        enc.mov_mr_rbp(-8, x86::RCX);  // s
        enc.mov_mr_rbp(-16, x86::RDX); // suffix

        size_t falseEw = enc.createLabel();
        size_t trueEw = enc.createLabel();
        size_t exitEw = enc.createLabel();

        enc.test_rr(x86::RCX, x86::RCX);
        enc.je(falseEw);
        enc.test_rr(x86::RDX, x86::RDX);
        enc.je(trueEw);

        enc.call_iat_func("strlen");
        enc.mov_mr_rbp(-24, x86::RAX); // lenS

        enc.mov_rm_rbp(x86::RCX, -16);
        enc.call_iat_func("strlen");
        enc.mov_mr_rbp(-32, x86::RAX); // lenSuf

        enc.test_rr(x86::RAX, x86::RAX);
        enc.je(trueEw);

        enc.mov_rm_rbp(x86::R10, -24); // lenS
        enc.cmp_rr(x86::R10, x86::RAX); // lenS < lenSuf
        enc.jl(falseEw);

        // s + lenS - lenSuf
        enc.mov_rm_rbp(x86::RCX, -8);
        enc.add_rr(x86::RCX, x86::R10);
        enc.mov_rm_rbp(x86::R10, -32);
        enc.sub_rr(x86::RCX, x86::R10);

        enc.mov_rm_rbp(x86::RDX, -16);
        enc.mov_rm_rbp(x86::R8, -32);
        enc.call_iat_func("strncmp");
        enc.test_rr(x86::RAX, x86::RAX);
        enc.jne(falseEw);

        enc.bindLabel(trueEw);
        enc.mov_ri64(x86::RAX, 1);
        enc.jmp(exitEw);

        enc.bindLabel(falseEw);
        enc.xor_r32(x86::RAX, x86::RAX);

        enc.bindLabel(exitEw);
        enc.add_rsp(64);
        enc.pop(x86::RBP);
        enc.ret();

        // --- _thava_str_count(s in RCX, sub in RDX) -> RAX (count) ---
        lblStrCount = enc.createLabel();
        enc.bindLabel(lblStrCount);
        enc.push(x86::RBP);
        enc.mov_rr(x86::RBP, x86::RSP);
        enc.sub_rsp(64);

        enc.mov_mr_rbp(-8, x86::RCX);  // s
        enc.mov_mr_rbp(-16, x86::RDX); // sub
        enc.mov_ri64(x86::RAX, 0);
        enc.mov_mr_rbp(-24, x86::RAX); // count = 0

        size_t exitCount = enc.createLabel();

        enc.test_rr(x86::RCX, x86::RCX);
        enc.je(exitCount);
        enc.test_rr(x86::RDX, x86::RDX);
        enc.je(exitCount);

        enc.mov_rr(x86::RCX, x86::RDX);
        enc.call_iat_func("strlen");
        enc.test_rr(x86::RAX, x86::RAX);
        enc.je(exitCount);
        enc.mov_mr_rbp(-32, x86::RAX); // lenSub

        // cur = s
        enc.mov_rm_rbp(x86::RAX, -8);
        enc.mov_mr_rbp(-40, x86::RAX); // cur

        size_t loopCnt = enc.createLabel();
        enc.bindLabel(loopCnt);
        enc.mov_rm_rbp(x86::RCX, -40); // cur
        enc.mov_rm_rbp(x86::RDX, -16); // sub
        enc.call_iat_func("strstr");
        enc.test_rr(x86::RAX, x86::RAX);
        enc.je(exitCount);

        // count++
        enc.mov_rm_rbp(x86::R10, -24);
        enc.mov_ri64(x86::R11, 1);
        enc.add_rr(x86::R10, x86::R11);
        enc.mov_mr_rbp(-24, x86::R10);

        // cur = RAX + lenSub
        enc.mov_rm_rbp(x86::R10, -32);
        enc.add_rr(x86::RAX, x86::R10);
        enc.mov_mr_rbp(-40, x86::RAX);
        enc.jmp(loopCnt);

        enc.bindLabel(exitCount);
        enc.mov_rm_rbp(x86::RAX, -24);
        enc.add_rsp(64);
        enc.pop(x86::RBP);
        enc.ret();

        // --- _thava_str_replace(s in RCX, oldSub in RDX, newSub in R8) -> RAX (heap string) ---
        lblStrReplace = enc.createLabel();
        enc.bindLabel(lblStrReplace);
        enc.push(x86::RBP);
        enc.mov_rr(x86::RBP, x86::RSP);
        enc.sub_rsp(128);

        enc.mov_mr_rbp(-8, x86::RCX);  // s
        enc.mov_mr_rbp(-16, x86::RDX); // oldSub
        enc.mov_mr_rbp(-24, x86::R8);  // newSub

        size_t exitRepl = enc.createLabel();
        size_t dupSRepl = enc.createLabel();

        enc.test_rr(x86::RCX, x86::RCX);
        size_t nullRepl = enc.createLabel();
        enc.je(nullRepl);

        enc.call_iat_func("strlen");
        enc.mov_mr_rbp(-32, x86::RAX); // lenS

        enc.mov_rm_rbp(x86::RCX, -16); // oldSub
        enc.test_rr(x86::RCX, x86::RCX);
        enc.je(dupSRepl);
        enc.call_iat_func("strlen");
        enc.mov_mr_rbp(-40, x86::RAX); // lenOld
        enc.test_rr(x86::RAX, x86::RAX);
        enc.je(dupSRepl);

        enc.mov_rm_rbp(x86::RCX, -24); // newSub
        size_t noNewSub = enc.createLabel();
        size_t haveNewSub = enc.createLabel();
        enc.test_rr(x86::RCX, x86::RCX);
        enc.je(noNewSub);
        enc.call_iat_func("strlen");
        enc.mov_mr_rbp(-48, x86::RAX); // lenNew
        enc.jmp(haveNewSub);
        enc.bindLabel(noNewSub);
        enc.mov_ri64(x86::RAX, 0);
        enc.mov_mr_rbp(-48, x86::RAX);
        enc.bindLabel(haveNewSub);

        // Count occurrences
        enc.mov_ri64(x86::RAX, 0);
        enc.mov_mr_rbp(-56, x86::RAX); // occ = 0
        enc.mov_rm_rbp(x86::RAX, -8);
        enc.mov_mr_rbp(-64, x86::RAX); // cur = s

        size_t occLoop = enc.createLabel();
        size_t occDone = enc.createLabel();
        enc.bindLabel(occLoop);
        enc.mov_rm_rbp(x86::RCX, -64);
        enc.mov_rm_rbp(x86::RDX, -16);
        enc.call_iat_func("strstr");
        enc.test_rr(x86::RAX, x86::RAX);
        enc.je(occDone);

        enc.mov_rm_rbp(x86::R10, -56);
        enc.mov_ri64(x86::R11, 1);
        enc.add_rr(x86::R10, x86::R11);
        enc.mov_mr_rbp(-56, x86::R10); // occ++

        enc.mov_rm_rbp(x86::R10, -40); // lenOld
        enc.add_rr(x86::RAX, x86::R10);
        enc.mov_mr_rbp(-64, x86::RAX); // cur = found + lenOld
        enc.jmp(occLoop);

        enc.bindLabel(occDone);
        // totalAlloc = lenS + occ * (lenNew - lenOld) + 1
        enc.mov_rm_rbp(x86::RAX, -48); // lenNew
        enc.mov_rm_rbp(x86::R10, -40); // lenOld
        enc.sub_rr(x86::RAX, x86::R10); // lenNew - lenOld
        enc.mov_rm_rbp(x86::R11, -56); // occ
        enc.imul_rr(x86::RAX, x86::R11);
        enc.mov_rm_rbp(x86::R10, -32); // lenS
        enc.add_rr(x86::RAX, x86::R10);
        enc.mov_ri64(x86::R10, 1);
        enc.add_rr(x86::RAX, x86::R10); // totalAlloc

        enc.mov_rr(x86::RCX, x86::RAX);
        enc.call_iat_func("malloc");
        enc.mov_mr_rbp(-72, x86::RAX); // outBuf

        // Copy and replace loop
        enc.mov_rm_rbp(x86::RAX, -8);
        enc.mov_mr_rbp(-80, x86::RAX); // src = s
        enc.mov_rm_rbp(x86::RAX, -72);
        enc.mov_mr_rbp(-88, x86::RAX); // dst = outBuf

        size_t replLoop = enc.createLabel();
        size_t replDone = enc.createLabel();
        enc.bindLabel(replLoop);
        enc.mov_rm_rbp(x86::RCX, -80); // src
        enc.mov_rm_rbp(x86::RDX, -16); // oldSub
        enc.call_iat_func("strstr");
        enc.test_rr(x86::RAX, x86::RAX);
        enc.je(replDone);

        enc.mov_mr_rbp(-96, x86::RAX); // found

        // chunkLen = found - src
        enc.mov_rm_rbp(x86::R10, -80); // src
        enc.sub_rr(x86::RAX, x86::R10); // chunkLen
        enc.mov_mr_rbp(-104, x86::RAX); // chunkLen
        enc.test_rr(x86::RAX, x86::RAX);
        size_t skipChunk = enc.createLabel();
        enc.jle(skipChunk);

        enc.mov_rm_rbp(x86::RCX, -88); // dst
        enc.mov_rm_rbp(x86::RDX, -80); // src
        enc.mov_rm_rbp(x86::R8, -104); // chunkLen
        enc.call_iat_func("memcpy");

        enc.mov_rm_rbp(x86::RAX, -88);
        enc.mov_rm_rbp(x86::R10, -104);
        enc.add_rr(x86::RAX, x86::R10);
        enc.mov_mr_rbp(-88, x86::RAX); // dst += chunkLen

        enc.bindLabel(skipChunk);
        // copy newSub if lenNew > 0
        enc.mov_rm_rbp(x86::R8, -48); // lenNew
        enc.test_rr(x86::R8, x86::R8);
        size_t skipNewCopy = enc.createLabel();
        enc.jle(skipNewCopy);

        enc.mov_rm_rbp(x86::RCX, -88); // dst
        enc.mov_rm_rbp(x86::RDX, -24); // newSub
        enc.call_iat_func("memcpy");

        enc.mov_rm_rbp(x86::RAX, -88);
        enc.mov_rm_rbp(x86::R10, -48);
        enc.add_rr(x86::RAX, x86::R10);
        enc.mov_mr_rbp(-88, x86::RAX); // dst += lenNew

        enc.bindLabel(skipNewCopy);
        // src = found + lenOld
        enc.mov_rm_rbp(x86::RAX, -96); // found
        enc.mov_rm_rbp(x86::R10, -40); // lenOld
        enc.add_rr(x86::RAX, x86::R10);
        enc.mov_mr_rbp(-80, x86::RAX); // src
        enc.jmp(replLoop);

        enc.bindLabel(replDone);
        // Copy remainder: strcpy(dst, src)
        enc.mov_rm_rbp(x86::RCX, -88);
        enc.mov_rm_rbp(x86::RDX, -80);
        enc.call_iat_func("strcpy");

        enc.mov_rm_rbp(x86::RAX, -72); // outBuf
        enc.jmp(exitRepl);

        enc.bindLabel(dupSRepl);
        enc.mov_rm_rbp(x86::RCX, -32); // lenS
        enc.mov_ri64(x86::R10, 1);
        enc.add_rr(x86::RCX, x86::R10);
        enc.call_iat_func("malloc");
        enc.mov_mr_rbp(-72, x86::RAX);
        enc.mov_rr(x86::RCX, x86::RAX);
        enc.mov_rm_rbp(x86::RDX, -8);
        enc.call_iat_func("strcpy");
        enc.mov_rm_rbp(x86::RAX, -72);
        enc.jmp(exitRepl);

        enc.bindLabel(nullRepl);
        enc.lea_rip_rdata(x86::RAX, offEmptyStr);

        enc.bindLabel(exitRepl);
        enc.add_rsp(128);
        enc.pop(x86::RBP);
        enc.ret();

        // --- _thava_str_join(delim in RCX, list in RDX) -> RAX (heap string) ---
        lblStrJoin = enc.createLabel();
        enc.bindLabel(lblStrJoin);
        enc.push(x86::RBP);
        enc.mov_rr(x86::RBP, x86::RSP);
        enc.sub_rsp(128);

        enc.mov_mr_rbp(-8, x86::RCX);  // delim
        enc.mov_mr_rbp(-16, x86::RDX); // list

        size_t emptyJoin = enc.createLabel();
        size_t exitJoin = enc.createLabel();

        enc.test_rr(x86::RDX, x86::RDX);
        enc.je(emptyJoin);

        enc.mov_rm(x86::RAX, x86::RDX, 0); // count
        enc.test_rr(x86::RAX, x86::RAX);
        enc.jle(emptyJoin);
        enc.mov_mr_rbp(-24, x86::RAX); // count

        // lenDelim = strlen(delim)
        enc.mov_rm_rbp(x86::RCX, -8);
        enc.test_rr(x86::RCX, x86::RCX);
        size_t delimNull = enc.createLabel();
        size_t delimHave = enc.createLabel();
        enc.je(delimNull);
        enc.call_iat_func("strlen");
        enc.mov_mr_rbp(-32, x86::RAX); // lenDelim
        enc.jmp(delimHave);
        enc.bindLabel(delimNull);
        enc.mov_ri64(x86::RAX, 0);
        enc.mov_mr_rbp(-32, x86::RAX);
        enc.bindLabel(delimHave);

        // totalLen = (count - 1) * lenDelim + 1
        enc.mov_rm_rbp(x86::RAX, -24); // count
        enc.mov_ri64(x86::R10, 1);
        enc.sub_rr(x86::RAX, x86::R10); // count - 1
        enc.mov_rm_rbp(x86::R10, -32); // lenDelim
        enc.imul_rr(x86::RAX, x86::R10);
        enc.mov_ri64(x86::R10, 1);
        enc.add_rr(x86::RAX, x86::R10);
        enc.mov_mr_rbp(-40, x86::RAX); // totalLen

        // loop i from 0 to count-1: totalLen += strlen(list[8 + i * 8])
        enc.mov_ri64(x86::RAX, 0);
        enc.mov_mr_rbp(-48, x86::RAX); // i = 0

        size_t lenLoop = enc.createLabel();
        size_t lenDone = enc.createLabel();
        enc.bindLabel(lenLoop);
        enc.mov_rm_rbp(x86::RAX, -48); // i
        enc.mov_rm_rbp(x86::R10, -24); // count
        enc.cmp_rr(x86::RAX, x86::R10);
        enc.jge(lenDone);

        // elem = list[8 + i * 8]
        enc.mov_rm_rbp(x86::RDX, -16);
        enc.mov_ri64(x86::R10, 8);
        enc.imul_rr(x86::RAX, x86::R10);
        enc.add_rr(x86::RDX, x86::RAX);
        enc.mov_rm(x86::RCX, x86::RDX, 8); // elem
        enc.test_rr(x86::RCX, x86::RCX);
        size_t skipElemLen = enc.createLabel();
        enc.je(skipElemLen);
        enc.call_iat_func("strlen");
        enc.mov_rm_rbp(x86::R10, -40); // totalLen
        enc.add_rr(x86::R10, x86::RAX);
        enc.mov_mr_rbp(-40, x86::R10);

        enc.bindLabel(skipElemLen);
        enc.mov_rm_rbp(x86::RAX, -48);
        enc.mov_ri64(x86::R10, 1);
        enc.add_rr(x86::RAX, x86::R10);
        enc.mov_mr_rbp(-48, x86::RAX);
        enc.jmp(lenLoop);

        enc.bindLabel(lenDone);
        // malloc(totalLen)
        enc.mov_rm_rbp(x86::RCX, -40);
        enc.call_iat_func("malloc");
        enc.mov_mr_rbp(-56, x86::RAX); // outBuf
        enc.xor_r32(x86::R10, x86::R10);
        enc.mov_mr8(x86::RAX, 0, x86::R10); // outBuf[0] = 0

        // Concatenation loop
        enc.mov_ri64(x86::RAX, 0);
        enc.mov_mr_rbp(-48, x86::RAX); // i = 0

        size_t catLoop = enc.createLabel();
        size_t catDone = enc.createLabel();
        enc.bindLabel(catLoop);
        enc.mov_rm_rbp(x86::RAX, -48);
        enc.mov_rm_rbp(x86::R10, -24);
        enc.cmp_rr(x86::RAX, x86::R10);
        enc.jge(catDone);

        // elem = list[8 + i * 8]
        enc.mov_rm_rbp(x86::RDX, -16);
        enc.mov_ri64(x86::R10, 8);
        enc.imul_rr(x86::RAX, x86::R10);
        enc.add_rr(x86::RDX, x86::RAX);
        enc.mov_rm(x86::RDX, x86::RDX, 8); // elem in RDX
        enc.test_rr(x86::RDX, x86::RDX);
        size_t skipCatElem = enc.createLabel();
        enc.je(skipCatElem);

        enc.mov_rm_rbp(x86::RCX, -56); // outBuf
        enc.call_iat_func("strcat");

        enc.bindLabel(skipCatElem);
        // if i + 1 < count: strcat(outBuf, delim)
        enc.mov_rm_rbp(x86::RAX, -48); // i
        enc.mov_ri64(x86::R10, 1);
        enc.add_rr(x86::RAX, x86::R10); // i + 1
        enc.mov_rm_rbp(x86::R10, -24); // count
        enc.cmp_rr(x86::RAX, x86::R10);
        size_t skipCatDelim = enc.createLabel();
        enc.jge(skipCatDelim);

        enc.mov_rm_rbp(x86::RDX, -8); // delim
        enc.test_rr(x86::RDX, x86::RDX);
        enc.je(skipCatDelim);
        enc.mov_rm_rbp(x86::RCX, -56); // outBuf
        enc.call_iat_func("strcat");

        enc.bindLabel(skipCatDelim);
        enc.mov_rm_rbp(x86::RAX, -48);
        enc.mov_ri64(x86::R10, 1);
        enc.add_rr(x86::RAX, x86::R10);
        enc.mov_mr_rbp(-48, x86::RAX);
        enc.jmp(catLoop);

        enc.bindLabel(catDone);
        enc.mov_rm_rbp(x86::RAX, -56);
        enc.jmp(exitJoin);

        enc.bindLabel(emptyJoin);
        enc.lea_rip_rdata(x86::RAX, offEmptyStr);

        enc.bindLabel(exitJoin);
        enc.add_rsp(128);
        enc.pop(x86::RBP);
        enc.ret();

        // --- _thava_str_split(s in RCX, delim in RDX) -> RAX (list buffer) ---
        lblStrSplit = enc.createLabel();
        enc.bindLabel(lblStrSplit);
        enc.push(x86::RBP);
        enc.mov_rr(x86::RBP, x86::RSP);
        enc.sub_rsp(128);

        enc.mov_mr_rbp(-8, x86::RCX);  // s
        enc.mov_mr_rbp(-16, x86::RDX); // delim

        size_t emptySplitAll = enc.createLabel();
        size_t exitSplitAll = enc.createLabel();

        enc.test_rr(x86::RCX, x86::RCX);
        enc.je(emptySplitAll);

        enc.call_iat_func("strlen");
        enc.mov_mr_rbp(-24, x86::RAX); // lenS

        enc.mov_rm_rbp(x86::RCX, -16); // delim
        enc.test_rr(x86::RCX, x86::RCX);
        size_t delimZero = enc.createLabel();
        size_t delimNonZero = enc.createLabel();
        enc.je(delimZero);
        enc.call_iat_func("strlen");
        enc.mov_mr_rbp(-32, x86::RAX); // lenDelim
        enc.test_rr(x86::RAX, x86::RAX);
        enc.je(delimZero);
        enc.jmp(delimNonZero);

        // If delim is empty/null, return list with 1 element [s]
        enc.bindLabel(delimZero);
        enc.mov_ri64(x86::RCX, 16);
        enc.call_iat_func("malloc");
        enc.mov_mr_rbp(-40, x86::RAX); // listPtr
        enc.mov_ri64(x86::R10, 1);
        enc.mov_mr(x86::RAX, 0, x86::R10); // listPtr[0] = 1

        enc.mov_rm_rbp(x86::RCX, -24); // lenS
        enc.mov_ri64(x86::R10, 1);
        enc.add_rr(x86::RCX, x86::R10);
        enc.call_iat_func("malloc");
        enc.mov_mr_rbp(-48, x86::RAX);
        enc.mov_rr(x86::RCX, x86::RAX);
        enc.mov_rm_rbp(x86::RDX, -8);
        enc.call_iat_func("strcpy");
        enc.mov_rm_rbp(x86::RAX, -40); // listPtr
        enc.mov_rm_rbp(x86::R10, -48);
        enc.mov_mr(x86::RAX, 8, x86::R10); // listPtr[1] = dupS
        enc.jmp(exitSplitAll);

        enc.bindLabel(delimNonZero);
        // Count pieces = 1 + count occurrences of delim
        enc.mov_ri64(x86::RAX, 1);
        enc.mov_mr_rbp(-40, x86::RAX); // pieces = 1
        enc.mov_rm_rbp(x86::RAX, -8);
        enc.mov_mr_rbp(-48, x86::RAX); // cur = s

        size_t countPiecesLoop = enc.createLabel();
        size_t countPiecesDone = enc.createLabel();
        enc.bindLabel(countPiecesLoop);
        enc.mov_rm_rbp(x86::RCX, -48); // cur
        enc.mov_rm_rbp(x86::RDX, -16); // delim
        enc.call_iat_func("strstr");
        enc.test_rr(x86::RAX, x86::RAX);
        enc.je(countPiecesDone);

        enc.mov_rm_rbp(x86::R10, -40);
        enc.mov_ri64(x86::R11, 1);
        enc.add_rr(x86::R10, x86::R11);
        enc.mov_mr_rbp(-40, x86::R10); // pieces++

        enc.mov_rm_rbp(x86::R10, -32); // lenDelim
        enc.add_rr(x86::RAX, x86::R10);
        enc.mov_mr_rbp(-48, x86::RAX); // cur = found + lenDelim
        enc.jmp(countPiecesLoop);

        enc.bindLabel(countPiecesDone);
        // Malloc list buffer: (pieces + 1) * 8
        enc.mov_rm_rbp(x86::RAX, -40); // pieces
        enc.mov_ri64(x86::R10, 1);
        enc.add_rr(x86::RAX, x86::R10); // pieces + 1
        enc.mov_ri64(x86::R10, 8);
        enc.imul_rr(x86::RAX, x86::R10);
        enc.mov_rr(x86::RCX, x86::RAX);
        enc.call_iat_func("malloc");
        enc.mov_mr_rbp(-56, x86::RAX); // listPtr

        // listPtr[0] = pieces
        enc.mov_rm_rbp(x86::R10, -40);
        enc.mov_mr(x86::RAX, 0, x86::R10);

        // Fill pieces
        enc.mov_rm_rbp(x86::RAX, -8);
        enc.mov_mr_rbp(-64, x86::RAX); // p = s
        enc.mov_ri64(x86::RAX, 0);
        enc.mov_mr_rbp(-72, x86::RAX); // idx = 0

        size_t splitFillLoop = enc.createLabel();
        size_t splitFillDone = enc.createLabel();
        enc.bindLabel(splitFillLoop);
        enc.mov_rm_rbp(x86::RCX, -64); // p
        enc.mov_rm_rbp(x86::RDX, -16); // delim
        enc.call_iat_func("strstr");
        enc.test_rr(x86::RAX, x86::RAX);
        enc.je(splitFillDone);

        enc.mov_mr_rbp(-80, x86::RAX); // found
        // pieceLen = found - p
        enc.mov_rm_rbp(x86::R10, -64); // p
        enc.sub_rr(x86::RAX, x86::R10); // pieceLen
        enc.mov_mr_rbp(-88, x86::RAX);

        // buf = malloc(pieceLen + 1)
        enc.mov_ri64(x86::R10, 1);
        enc.add_rr(x86::RAX, x86::R10);
        enc.mov_rr(x86::RCX, x86::RAX);
        enc.call_iat_func("malloc");
        enc.mov_mr_rbp(-96, x86::RAX); // buf

        // memcpy(buf, p, pieceLen)
        enc.mov_rr(x86::RCX, x86::RAX);
        enc.mov_rm_rbp(x86::RDX, -64); // p
        enc.mov_rm_rbp(x86::R8, -88);  // pieceLen
        enc.call_iat_func("memcpy");

        // buf[pieceLen] = 0
        enc.mov_rm_rbp(x86::RAX, -96);
        enc.mov_rm_rbp(x86::R10, -88);
        enc.add_rr(x86::RAX, x86::R10);
        enc.xor_r32(x86::R10, x86::R10);
        enc.mov_mr8(x86::RAX, 0, x86::R10);

        // listPtr[8 + idx * 8] = buf
        enc.mov_rm_rbp(x86::RAX, -56); // listPtr
        enc.mov_rm_rbp(x86::R10, -72); // idx
        enc.mov_ri64(x86::R11, 8);
        enc.imul_rr(x86::R10, x86::R11);
        enc.add_rr(x86::RAX, x86::R10);
        enc.mov_rm_rbp(x86::R11, -96); // buf
        enc.mov_mr(x86::RAX, 8, x86::R11);

        // idx++
        enc.mov_rm_rbp(x86::RAX, -72);
        enc.mov_ri64(x86::R10, 1);
        enc.add_rr(x86::RAX, x86::R10);
        enc.mov_mr_rbp(-72, x86::RAX);

        // p = found + lenDelim
        enc.mov_rm_rbp(x86::RAX, -80); // found
        enc.mov_rm_rbp(x86::R10, -32); // lenDelim
        enc.add_rr(x86::RAX, x86::R10);
        enc.mov_mr_rbp(-64, x86::RAX);
        enc.jmp(splitFillLoop);

        enc.bindLabel(splitFillDone);
        // Last piece: from p to end of string: pieceLen = strlen(p)
        enc.mov_rm_rbp(x86::RCX, -64); // p
        enc.call_iat_func("strlen");
        enc.mov_mr_rbp(-88, x86::RAX); // pieceLen

        enc.mov_ri64(x86::R10, 1);
        enc.add_rr(x86::RAX, x86::R10);
        enc.mov_rr(x86::RCX, x86::RAX);
        enc.call_iat_func("malloc");
        enc.mov_mr_rbp(-96, x86::RAX); // buf

        enc.mov_rr(x86::RCX, x86::RAX);
        enc.mov_rm_rbp(x86::RDX, -64); // p
        enc.call_iat_func("strcpy");

        // listPtr[8 + idx * 8] = buf
        enc.mov_rm_rbp(x86::RAX, -56); // listPtr
        enc.mov_rm_rbp(x86::R10, -72); // idx
        enc.mov_ri64(x86::R11, 8);
        enc.imul_rr(x86::R10, x86::R11);
        enc.add_rr(x86::RAX, x86::R10);
        enc.mov_rm_rbp(x86::R11, -96); // buf
        enc.mov_mr(x86::RAX, 8, x86::R11);

        enc.mov_rm_rbp(x86::RAX, -56);
        enc.jmp(exitSplitAll);

        enc.bindLabel(emptySplitAll);
        // empty list: malloc(8), list[0] = 0
        enc.mov_ri64(x86::RCX, 8);
        enc.call_iat_func("malloc");
        enc.xor_r32(x86::R10, x86::R10);
        enc.mov_mr(x86::RAX, 0, x86::R10);

        enc.bindLabel(exitSplitAll);
        enc.add_rsp(128);
        enc.pop(x86::RBP);
        enc.ret();

        // --- 7. _thava_input_line(prompt in RCX, mode in RDX: 0=none, 1=lower) -> RAX ---
        lblInputLine = enc.createLabel();
        enc.bindLabel(lblInputLine);
        enc.push(x86::RBP);
        enc.mov_rr(x86::RBP, x86::RSP);
        constexpr bool windowsInput = std::is_same_v<TEmitter, PeEmitter>;
        constexpr uint32_t inputFrame = windowsInput ? 2208 : 128;
        enc.sub_rsp(inputFrame);

        enc.mov_mr_rbp(-8, x86::RCX);  // prompt
        enc.mov_mr_rbp(-16, x86::RDX); // mode

        // Print prompt
        enc.test_rr(x86::RCX, x86::RCX);
        size_t skipPrompt = enc.createLabel();
        enc.je(skipPrompt);
        enc.mov_rm_rbp(x86::RDX, -8);
        emitCallPrintf(offFmtStr, x86::RDX, true);
        enc.xor_r32(x86::RCX, x86::RCX);
        enc.call_iat_func("fflush");

        enc.bindLabel(skipPrompt);
        // malloc(1024)
        enc.mov_ri64(x86::RCX, windowsInput ? 3073 : 1024);
        enc.call_iat_func("malloc");
        enc.mov_mr_rbp(-24, x86::RAX); // buf

        const auto inputEmpty = enc.createLabel();
        const auto inputUpper = enc.createLabel();
        const auto inputConverted = enc.createLabel();
        const auto inputDone = enc.createLabel();
        enc.test_rr(x86::RAX, x86::RAX);
        enc.je(inputEmpty);
        enc.xor_r32(x86::RDX, x86::RDX);
        enc.mov_mr8(x86::RAX, 0, x86::RDX);

        const auto lineRead = enc.createLabel();
        if constexpr (windowsInput) {
            using namespace x86;
            const auto redirectedInput = enc.createLabel();
            const auto trimEnd = enc.createLabel();
            const auto terminateLine = enc.createLabel();
            constexpr int32_t wideInput = -2112; // 1024 UTF-16 code units
            constexpr int32_t countSlot = -40;
            constexpr int32_t handleSlot = -48;

            enc.mov_ri64(RCX, -10); // STD_INPUT_HANDLE
            enc.call_iat_func("GetStdHandle");
            enc.mov_mr_rbp(handleSlot, RAX);
            enc.mov_rr(RCX, RAX);
            enc.lea_rm_rbp(RDX, countSlot);
            enc.call_iat_func("GetConsoleMode");
            enc.test_rr(RAX, RAX);
            enc.je(redirectedInput);

            enc.mov_rm_rbp(RCX, handleSlot);
            enc.lea_rm_rbp(RDX, wideInput);
            enc.mov_ri64(R8, 1024);
            enc.lea_rm_rbp(R9, countSlot);
            enc.xor_r32(RAX, RAX);
            enc.mov_mr_rbp(countSlot, RAX);
            enc.mov_mr_rsp(32, RAX);
            enc.call_iat_func("ReadConsoleW");
            enc.test_rr(RAX, RAX);
            enc.je(lineRead);

            enc.mov_ri64(RCX, 65001); // CP_UTF8
            enc.xor_r32(RDX, RDX);
            enc.lea_rm_rbp(R8, wideInput);
            enc.mov_rm_rbp(R9, countSlot);
            enc.mov_rm_rbp(R10, -24);
            enc.mov_mr_rsp(32, R10);
            enc.mov_ri64(R10, 3072);
            enc.mov_mr_rsp(40, R10);
            enc.mov_mr_rsp(48, RDX);
            enc.mov_mr_rsp(56, RDX);
            enc.call_iat_func("WideCharToMultiByte");

            // Trim the console's CR/LF after converting to UTF-8.
            enc.mov_rm_rbp(R10, -24);
            enc.bindLabel(trimEnd);
            enc.test_rr(RAX, RAX);
            enc.je(terminateLine);
            enc.mov_rr(R11, R10);
            enc.add_rr(R11, RAX);
            enc.movzx_rm8(RDX, R11, -1);
            enc.cmp_ri(RDX, '\r');
            const auto removeByte = enc.createLabel();
            enc.je(removeByte);
            enc.cmp_ri(RDX, '\n');
            enc.jne(terminateLine);
            enc.bindLabel(removeByte);
            enc.dec_r(RAX);
            enc.jmp(trimEnd);
            enc.bindLabel(terminateLine);
            enc.add_rr(R10, RAX);
            enc.xor_r32(RDX, RDX);
            enc.mov_mr8(R10, 0, RDX);
            enc.jmp(lineRead);
            enc.bindLabel(redirectedInput);
        }

        // Read a line into the initialized buffer.
        enc.lea_rip_rdata(x86::RCX, offFmtScanLine);
        enc.mov_rm_rbp(x86::RDX, -24);
        enc.call_iat_func("scanf");
        enc.bindLabel(lineRead);

        enc.mov_rm_rbp(x86::RDX, -16);
        enc.mov_rm_rbp(x86::RAX, -24);
        enc.test_rr(x86::RDX, x86::RDX);
        enc.je(inputDone);
        enc.mov_rr(x86::RCX, x86::RAX);
        enc.cmp_ri(x86::RDX, 2);
        enc.je(inputUpper);
        enc.call_label(lblStrLower);
        enc.jmp(inputConverted);
        enc.bindLabel(inputUpper);
        enc.call_label(lblStrUpper);
        enc.bindLabel(inputConverted);
        enc.mov_mr_rbp(-32, x86::RAX);
        enc.mov_rm_rbp(x86::RCX, -24);
        enc.call_iat_func("free");
        enc.mov_rm_rbp(x86::RAX, -32);
        enc.jmp(inputDone);
        enc.bindLabel(inputEmpty);
        enc.lea_rip_rdata(x86::RAX, offEmptyStr);
        enc.bindLabel(inputDone);
        enc.add_rsp(inputFrame);
        enc.pop(x86::RBP);
        enc.ret();

        // --- 8. _thava_str_split_last(s in RCX) -> RAX (last word after space) ---
        lblStrSplitLast = enc.createLabel();
        enc.bindLabel(lblStrSplitLast);
        enc.push(x86::RBP);
        enc.mov_rr(x86::RBP, x86::RSP);
        enc.sub_rsp(128);

        enc.mov_mr_rbp(-8, x86::RCX);  // s
        enc.call_iat_func("strlen");
        enc.mov_mr_rbp(-16, x86::RAX); // len

        // Find last space: walk backward from len-1 to 0
        enc.mov_rm_rbp(x86::R10, -16);
        size_t scanBack = enc.createLabel();
        size_t spaceFound = enc.createLabel();
        size_t noSpace = enc.createLabel();

        enc.bindLabel(scanBack);
        enc.mov_ri64(x86::RAX, 0);
        enc.cmp_rr(x86::R10, x86::RAX);
        enc.jle(noSpace);
        enc.mov_ri64(x86::RAX, 1);
        enc.sub_rr(x86::R10, x86::RAX);

        enc.mov_rm_rbp(x86::RCX, -8);
        enc.add_rr(x86::RCX, x86::R10);
        enc.movzx_rm8(x86::RAX, x86::RCX, 0); // single byte read
        enc.mov_ri64(x86::RDX, ' ');
        enc.cmp_rr(x86::RAX, x86::RDX);
        enc.je(spaceFound);
        enc.jmp(scanBack);

        enc.bindLabel(spaceFound);
        // ptr = s + R10 + 1
        enc.mov_rm_rbp(x86::RCX, -8);
        enc.add_rr(x86::RCX, x86::R10);
        enc.mov_ri64(x86::RAX, 1);
        enc.add_rr(x86::RCX, x86::RAX);
        enc.mov_mr_rbp(-24, x86::RCX); // token ptr
        enc.call_iat_func("strlen");
        enc.mov_ri64(x86::R10, 1);
        enc.add_rr(x86::RAX, x86::R10);
        enc.mov_rr(x86::RCX, x86::RAX);
        enc.call_iat_func("malloc");
        enc.mov_rr(x86::RCX, x86::RAX);
        enc.mov_rm_rbp(x86::RDX, -24);
        enc.call_iat_func("strcpy");
        size_t exitSplit = enc.createLabel();
        enc.jmp(exitSplit);

        enc.bindLabel(noSpace);
        enc.mov_rm_rbp(x86::RCX, -8);
        enc.call_iat_func("strlen");
        enc.mov_ri64(x86::R10, 1);
        enc.add_rr(x86::RAX, x86::R10);
        enc.mov_rr(x86::RCX, x86::RAX);
        enc.call_iat_func("malloc");
        enc.mov_rr(x86::RCX, x86::RAX);
        enc.mov_rm_rbp(x86::RDX, -8);
        enc.call_iat_func("strcpy");

        enc.bindLabel(exitSplit);
        enc.add_rsp(128);
        enc.pop(x86::RBP);
        enc.ret();

        // --- 8.5 _thava_str_substring(s in RCX, start in RDX, end in R8) -> RAX (heap string) ---
        lblStrSubstring = enc.createLabel();
        enc.bindLabel(lblStrSubstring);
        enc.push(x86::RBP);
        enc.mov_rr(x86::RBP, x86::RSP);
        enc.sub_rsp(128);

        enc.mov_mr_rbp(-8, x86::RCX);  // s
        enc.mov_mr_rbp(-16, x86::RDX); // start
        enc.mov_mr_rbp(-24, x86::R8);  // end

        size_t emptySubstr = enc.createLabel();
        size_t exitSubstr = enc.createLabel();

        // if (s == null) goto emptySubstr
        enc.test_rr(x86::RCX, x86::RCX);
        enc.je(emptySubstr);

        // n = strlen(s)
        enc.call_iat_func("strlen");
        enc.mov_mr_rbp(-32, x86::RAX); // n

        // if (n == 0) goto emptySubstr
        enc.test_rr(x86::RAX, x86::RAX);
        enc.je(emptySubstr);

        // a = start, b = end
        // if (a < 1) a = 1;
        enc.mov_rm_rbp(x86::RAX, -16);
        enc.mov_ri64(x86::R10, 1);
        enc.cmp_rr(x86::RAX, x86::R10);
        size_t aOk = enc.createLabel();
        enc.jge(aOk);
        enc.mov_mr_rbp(-16, x86::R10);
        enc.bindLabel(aOk);

        // if (b > n) b = n;
        enc.mov_rm_rbp(x86::RAX, -24);
        enc.mov_rm_rbp(x86::R10, -32);
        enc.cmp_rr(x86::RAX, x86::R10);
        size_t bOk = enc.createLabel();
        enc.jle(bOk);
        enc.mov_mr_rbp(-24, x86::R10);
        enc.bindLabel(bOk);

        // if (a > b) goto emptySubstr
        enc.mov_rm_rbp(x86::RAX, -16);
        enc.mov_rm_rbp(x86::R10, -24);
        enc.cmp_rr(x86::RAX, x86::R10);
        enc.jg(emptySubstr);

        // len = b - a + 1
        enc.mov_rm_rbp(x86::RAX, -24);
        enc.mov_rm_rbp(x86::R10, -16);
        enc.sub_rr(x86::RAX, x86::R10);
        enc.mov_ri64(x86::R10, 1);
        enc.add_rr(x86::RAX, x86::R10);
        enc.mov_mr_rbp(-40, x86::RAX); // len

        // malloc(len + 1)
        enc.mov_rm_rbp(x86::RCX, -40);
        enc.mov_ri64(x86::R10, 1);
        enc.add_rr(x86::RCX, x86::R10);
        enc.call_iat_func("malloc");
        enc.mov_mr_rbp(-48, x86::RAX); // dst

        // memcpy(dst, s + a - 1, len)
        enc.mov_rm_rbp(x86::RCX, -48); // dst
        enc.mov_rm_rbp(x86::RDX, -8);  // s
        enc.mov_rm_rbp(x86::R10, -16); // a
        enc.mov_ri64(x86::R11, 1);
        enc.sub_rr(x86::R10, x86::R11); // a - 1
        enc.add_rr(x86::RDX, x86::R10); // s + a - 1
        enc.mov_rm_rbp(x86::R8, -40);  // len
        enc.call_iat_func("memcpy");

        // dst[len] = 0
        enc.mov_rm_rbp(x86::RAX, -48);
        enc.mov_rm_rbp(x86::R10, -40);
        enc.add_rr(x86::RAX, x86::R10);
        enc.mov_ri64(x86::R11, 0);
        enc.mov_mr8(x86::RAX, 0, x86::R11);

        enc.mov_rm_rbp(x86::RAX, -48);
        enc.jmp(exitSubstr);

        enc.bindLabel(emptySubstr);
        enc.mov_ri64(x86::RCX, 1);
        enc.call_iat_func("malloc");
        enc.mov_ri64(x86::R10, 0);
        enc.mov_mr8(x86::RAX, 0, x86::R10);

        enc.bindLabel(exitSubstr);
        enc.add_rsp(128);
        enc.pop(x86::RBP);
        enc.ret();

        // --- 9. _thava_file_read_all(fileHandle in RCX) -> RAX (heap string) ---
        lblFileReadAll = enc.createLabel();
        enc.bindLabel(lblFileReadAll);
        enc.push(x86::RBP);
        enc.mov_rr(x86::RBP, x86::RSP);
        enc.sub_rsp(64);

        enc.mov_mr_rbp(-8, x86::RCX); // fileHandle

        // fseek(fileHandle, 0, SEEK_END=2)
        enc.mov_rm_rbp(x86::RCX, -8);
        enc.xor_r32(x86::RDX, x86::RDX);
        enc.mov_ri64(x86::R8, 2);
        enc.call_iat_func("fseek");

        // fileSize = ftell(fileHandle)
        enc.mov_rm_rbp(x86::RCX, -8);
        enc.call_iat_func("ftell");
        enc.mov_mr_rbp(-16, x86::RAX); // fileSize

        // fseek(fileHandle, 0, SEEK_SET=0)
        enc.mov_rm_rbp(x86::RCX, -8);
        enc.xor_r32(x86::RDX, x86::RDX);
        enc.xor_r32(x86::R8, x86::R8);
        enc.call_iat_func("fseek");

        // If fileSize < 0: fileSize = 0
        enc.mov_rm_rbp(x86::RAX, -16);
        enc.mov_ri64(x86::R10, 0);
        enc.cmp_rr(x86::RAX, x86::R10);
        size_t validSz = enc.createLabel();
        enc.jge(validSz);
        enc.mov_ri64(x86::RAX, 0);
        enc.mov_mr_rbp(-16, x86::RAX);
        enc.bindLabel(validSz);

        // malloc(fileSize + 1)
        enc.mov_rm_rbp(x86::RCX, -16);
        enc.inc_r(x86::RCX);
        enc.call_iat_func("malloc");
        enc.mov_mr_rbp(-24, x86::RAX); // buf

        // if fileSize > 0: fread(buf, 1, fileSize, fileHandle)
        enc.mov_rm_rbp(x86::RAX, -16);
        enc.test_rr(x86::RAX, x86::RAX);
        size_t noRead = enc.createLabel();
        enc.jle(noRead);

        enc.mov_rm_rbp(x86::RCX, -24); // buf
        enc.mov_ri64(x86::RDX, 1);     // size
        enc.mov_rm_rbp(x86::R8, -16);  // count = fileSize
        enc.mov_rm_rbp(x86::R9, -8);   // fileHandle
        enc.call_iat_func("fread");
        enc.mov_mr_rbp(-16, x86::RAX); // actual bytes read!

        enc.bindLabel(noRead);
        // buf[actualBytesRead] = 0 (null terminator)
        enc.mov_rm_rbp(x86::RCX, -24);
        enc.mov_rm_rbp(x86::RDX, -16);
        enc.add_rr(x86::RCX, x86::RDX);
        enc.xor_r32(x86::RAX, x86::RAX);
        enc.mov_mr8(x86::RCX, 0, x86::RAX);

        // Return buf in RAX
        enc.mov_rm_rbp(x86::RAX, -24);
        enc.add_rsp(64);
        enc.pop(x86::RBP);
        enc.ret();

        // --- 10. _thava_file_read_line(fileHandle in RCX) -> RAX (heap string) ---
        lblFileReadLine = enc.createLabel();
        enc.bindLabel(lblFileReadLine);
        enc.push(x86::RBP);
        enc.mov_rr(x86::RBP, x86::RSP);
        enc.sub_rsp(64);

        enc.mov_mr_rbp(-8, x86::RCX); // fileHandle

        // Allocate line buffer of 4096 bytes
        enc.mov_ri64(x86::RCX, 4096);
        enc.call_iat_func("malloc");
        enc.mov_mr_rbp(-16, x86::RAX); // lineBuf

        // len = 0
        enc.xor_r32(x86::RAX, x86::RAX);
        enc.mov_mr_rbp(-24, x86::RAX); // len

        size_t rlLoop = enc.createLabel();
        size_t rlDone = enc.createLabel();

        enc.bindLabel(rlLoop);
        // Read 1 char: fread(&ch, 1, 1, fileHandle)
        enc.lea_rm_rbp(x86::RCX, -32); // stack slot for 1 byte
        enc.mov_ri64(x86::RDX, 1);
        enc.mov_ri64(x86::R8, 1);
        enc.mov_rm_rbp(x86::R9, -8);
        enc.call_iat_func("fread");

        // If fread returned 0 (EOF): done
        enc.test_rr(x86::RAX, x86::RAX);
        enc.je(rlDone);

        enc.movzx_rm8(x86::RAX, x86::RBP, -32); // ch
        enc.cmp_ri32(x86::RAX, '\n');
        enc.je(rlDone);
        enc.cmp_ri32(x86::RAX, '\r');
        enc.je(rlLoop); // skip \r

        // lineBuf[len] = ch
        enc.mov_rm_rbp(x86::RCX, -16); // lineBuf
        enc.mov_rm_rbp(x86::RDX, -24); // len
        enc.add_rr(x86::RCX, x86::RDX);
        enc.mov_mr8(x86::RCX, 0, x86::RAX);

        // len++
        enc.mov_rm_rbp(x86::RAX, -24);
        enc.inc_r(x86::RAX);
        enc.mov_mr_rbp(-24, x86::RAX);

        // Max buffer check 4090
        enc.cmp_ri32(x86::RAX, 4090);
        enc.jge(rlDone);
        enc.jmp(rlLoop);

        enc.bindLabel(rlDone);
        // lineBuf[len] = '\0'
        enc.mov_rm_rbp(x86::RCX, -16);
        enc.mov_rm_rbp(x86::RDX, -24);
        enc.add_rr(x86::RCX, x86::RDX);
        enc.xor_r32(x86::RAX, x86::RAX);
        enc.mov_mr8(x86::RCX, 0, x86::RAX);

        // Return lineBuf in RAX
        enc.mov_rm_rbp(x86::RAX, -16);
        enc.add_rsp(64);
        enc.pop(x86::RBP);
        enc.ret();

        // --- 11. _thava_file_read_lines(fileHandle in RCX) -> RAX (list pointer) ---
        lblFileReadLines = enc.createLabel();
        enc.bindLabel(lblFileReadLines);
        enc.push(x86::RBP);
        enc.mov_rr(x86::RBP, x86::RSP);
        enc.sub_rsp(128);

        // Step 1: Read all content into a string
        enc.call_label(lblFileReadAll);
        enc.mov_mr_rbp(-8, x86::RAX); // fullText buf

        // Step 2: Pass 1 - Count number of lines
        enc.mov_rm_rbp(x86::RCX, -8); // p = fullText
        enc.xor_r32(x86::RDX, x86::RDX); // lineCount = 0
        enc.xor_r32(x86::R8, x86::R8);  // lineLen = 0

        size_t countLoop = enc.createLabel();
        size_t countDone = enc.createLabel();

        enc.bindLabel(countLoop);
        enc.movzx_rm8(x86::RAX, x86::RCX, 0); // char c = *p
        enc.test_rr(x86::RAX, x86::RAX);
        enc.je(countDone);

        enc.cmp_ri32(x86::RAX, '\n');
        size_t notNlCount = enc.createLabel();
        enc.jne(notNlCount);
        // Encountered '\n' -> lineCount++
        enc.inc_r(x86::RDX);
        enc.xor_r32(x86::R8, x86::R8);
        enc.inc_r(x86::RCX);
        enc.jmp(countLoop);

        enc.bindLabel(notNlCount);
        enc.inc_r(x86::R8);
        enc.inc_r(x86::RCX);
        enc.jmp(countLoop);

        enc.bindLabel(countDone);
        // If lineLen > 0, lineCount++
        enc.test_rr(x86::R8, x86::R8);
        size_t noLastLine = enc.createLabel();
        enc.je(noLastLine);
        enc.inc_r(x86::RDX);
        enc.bindLabel(noLastLine);

        enc.mov_mr_rbp(-16, x86::RDX); // lineCount

        // Step 3: Allocate Thava list: (lineCount + 1) * 8 bytes
        enc.mov_rr(x86::RCX, x86::RDX);
        enc.inc_r(x86::RCX);
        enc.mov_ri64(x86::R10, 8);
        enc.imul_rr(x86::RCX, x86::R10);
        enc.call_iat_func("malloc");
        enc.mov_mr_rbp(-24, x86::RAX); // listPtr

        // list[0] = lineCount
        enc.mov_rm_rbp(x86::RDX, -16);
        enc.mov_mr(x86::RAX, 0, x86::RDX);

        // Step 4: Pass 2 - Fill the list with line strings
        enc.mov_rm_rbp(x86::RSI, -8); // p = fullText
        enc.mov_rr(x86::RDI, x86::RSI); // lineStart = p
        enc.xor_r32(x86::RBX, x86::RBX); // curIndex = 0

        size_t fillLoop = enc.createLabel();
        size_t fillDone = enc.createLabel();
        size_t emitLine = enc.createLabel();

        enc.bindLabel(fillLoop);
        enc.movzx_rm8(x86::RAX, x86::RSI, 0); // c = *p
        enc.test_rr(x86::RAX, x86::RAX);
        size_t checkEOF = enc.createLabel();
        enc.je(checkEOF);

        enc.cmp_ri32(x86::RAX, '\n');
        enc.je(emitLine);
        enc.inc_r(x86::RSI);
        enc.jmp(fillLoop);

        enc.bindLabel(checkEOF);
        // EOF reached: if p > lineStart, emit final line
        enc.cmp_rr(x86::RSI, x86::RDI);
        enc.jle(fillDone);

        enc.bindLabel(emitLine);
        // Compute len = p - lineStart
        enc.mov_rr(x86::R8, x86::RSI);
        enc.sub_rr(x86::R8, x86::RDI); // len in R8

        // If len > 0 and lineStart[len - 1] == '\r', len--
        enc.test_rr(x86::R8, x86::R8);
        size_t noStripR = enc.createLabel();
        enc.je(noStripR);
        enc.mov_rr(x86::R10, x86::RDI);
        enc.add_rr(x86::R10, x86::R8);
        enc.dec_r(x86::R10);
        enc.movzx_rm8(x86::R11, x86::R10, 0);
        enc.cmp_ri32(x86::R11, '\r');
        enc.jne(noStripR);
        enc.dec_r(x86::R8);
        enc.bindLabel(noStripR);

        // Allocate (len + 1) for line string
        enc.mov_mr_rbp(-32, x86::RSI); // save RSI
        enc.mov_mr_rbp(-40, x86::RDI); // save RDI
        enc.mov_mr_rbp(-48, x86::R8);  // save len
        enc.mov_mr_rbp(-56, x86::RBX); // save curIndex

        enc.mov_rr(x86::RCX, x86::R8);
        enc.inc_r(x86::RCX);
        enc.call_iat_func("malloc");
        enc.mov_mr_rbp(-64, x86::RAX); // lineBuf

        // Copy len bytes: memcpy(lineBuf, lineStart, len)
        enc.mov_rr(x86::RCX, x86::RAX);
        enc.mov_rm_rbp(x86::RDX, -40); // lineStart
        enc.mov_rm_rbp(x86::R8, -48);  // len
        enc.call_iat_func("memcpy");

        // lineBuf[len] = '\0'
        enc.mov_rm_rbp(x86::RCX, -64);
        enc.mov_rm_rbp(x86::RDX, -48);
        enc.add_rr(x86::RCX, x86::RDX);
        enc.xor_r32(x86::RAX, x86::RAX);
        enc.mov_mr8(x86::RCX, 0, x86::RAX);

        // Store into list[1 + curIndex]
        enc.mov_rm_rbp(x86::R10, -24); // listPtr
        enc.mov_rm_rbp(x86::RBX, -56); // curIndex
        enc.mov_ri64(x86::R11, 8);
        enc.imul_rr(x86::R11, x86::RBX);
        enc.add_rr(x86::R10, x86::R11);
        enc.mov_rm_rbp(x86::RAX, -64); // lineBuf
        enc.mov_mr(x86::R10, 8, x86::RAX);

        // Restore RSI, RDI, RBX
        enc.mov_rm_rbp(x86::RSI, -32);
        enc.mov_rm_rbp(x86::RDI, -40);
        enc.mov_rm_rbp(x86::RBX, -56);
        enc.inc_r(x86::RBX);

        // If *p was '\n', p++
        enc.movzx_rm8(x86::RAX, x86::RSI, 0);
        enc.cmp_ri32(x86::RAX, '\n');
        size_t notWasNl = enc.createLabel();
        enc.jne(notWasNl);
        enc.inc_r(x86::RSI);
        enc.bindLabel(notWasNl);

        // lineStart = p
        enc.mov_rr(x86::RDI, x86::RSI);

        // Continue loop if not at end
        enc.movzx_rm8(x86::RAX, x86::RSI, 0);
        enc.test_rr(x86::RAX, x86::RAX);
        enc.jne(fillLoop);

        enc.bindLabel(fillDone);
        // Return listPtr in RAX
        enc.mov_rm_rbp(x86::RAX, -24);
        enc.add_rsp(128);
        enc.pop(x86::RBP);
        enc.ret();
    }

    // ==========================================
    // EXCEPTION RUNTIME
    // The current handler record lives in R15 (0 when no try block is active).
    // Record layout (48 bytes, allocated in the frame of the function that
    // owns the try block):
    //   +0  message    +8  type name    +16 previous handler
    //   +24 resume address              +32 saved RBP
    // ==========================================
    void emitThrowHelper() {
        lblThrow = enc.createLabel();
        enc.bindLabel(lblThrow);

        size_t fatal = enc.createLabel();
        enc.test_rr(x86::R15, x86::R15);
        enc.je(fatal);

        // Store the exception, unwind the stack and resume at the handler.
        enc.mov_mr(x86::R15, 0, x86::RAX);  // message
        enc.mov_mr(x86::R15, 8, x86::RDX);  // type name
        enc.mov_rm(x86::RCX, x86::R15, 32); // saved RBP
        enc.mov_rm(x86::R10, x86::R15, 24); // resume address
        enc.mov_rm(x86::R11, x86::R15, 16); // previous handler
        enc.mov_rr(x86::RBP, x86::RCX);
        enc.mov_rr(x86::RSP, x86::RBP);
        enc.sub_rsp(2048);
        enc.mov_rr(x86::R15, x86::R11);
        enc.jmp_r(x86::R10);

        // No active handler: report the exception and exit like before.
        enc.bindLabel(fatal);
        size_t hasMsg = enc.createLabel();
        enc.test_rr(x86::RAX, x86::RAX);
        enc.jne(hasMsg);
        enc.lea_rip_rdata(x86::RAX, pe.addStringLiteralOffset("Runtime Error!"));
        enc.bindLabel(hasMsg);
        enc.mov_rr(x86::RDX, x86::RAX);
        enc.lea_rip_rdata(x86::RCX, pe.addStringLiteralOffset("[Runtime Exception]: %s\n"));
        enc.call_iat_func("printf");
        enc.mov_ri64(x86::RCX, 1);
        enc.call_iat_func("exit");
    }

    // Emit `throw msg [as type]` into the exception runtime.
    void emitThrowCall(ASTNode* msgExpr, ASTNode* typeExpr, const std::string& defaultMsg) {
        int32_t msgSlot;
        currentFrameOffset -= 8;
        if (currentFrameOffset < maxFrameOffset) maxFrameOffset = currentFrameOffset;
        msgSlot = currentFrameOffset;
        if (msgExpr) {
            emitExpr(msgExpr);
        } else {
            enc.lea_rip_rdata(x86::RAX, pe.addStringLiteralOffset(defaultMsg));
        }
        enc.mov_mr_rbp(msgSlot, x86::RAX);

        if (typeExpr) {
            emitExpr(typeExpr);
            enc.mov_rr(x86::RDX, x86::RAX);
        } else {
            enc.lea_rip_rdata(x86::RDX, pe.addStringLiteralOffset("RuntimeError"));
        }
        enc.mov_rm_rbp(x86::RAX, msgSlot);
        enc.call_label(lblThrow);
    }

    // Element type of `base[i]`: known from the list variable's declaration, or
    // inferred from a list literal such as `[1.5, 2.5][i]`.
    ThavaType elemTypeOfIndexedList(ASTNode* base) {
        if (auto v = dynamic_cast<VarNode*>(base)) {
            auto it = listElemTypes.find(v->getName());
            if (it != listElemTypes.end()) return it->second;
            return ThavaType::UNKNOWN;
        }
        if (auto coll = dynamic_cast<CollectionNode*>(base)) {
            ThavaType found = ThavaType::INT;
            for (auto& el : coll->getElements()) {
                ThavaType t = getStaticExprType(el.get());
                if (t == ThavaType::DOUBLE || t == ThavaType::FLOAT) return ThavaType::DOUBLE;
                if (t == ThavaType::STRING) found = ThavaType::STRING;
            }
            return found;
        }
        return ThavaType::UNKNOWN;
    }

    // "1.500000" -> "1.5", "3.000000" -> "3": trims the decimals produced by
    // "%.6f" so a double renders like the other backends' short form.
    void emitTrimTrailingZeros(x86::Reg strPtr) {
        size_t scan = enc.createLabel();
        size_t back = enc.createLabel();
        size_t noDot = enc.createLabel();
        size_t done = enc.createLabel();
        enc.mov_rr(x86::RSI, strPtr);
        enc.bindLabel(scan);
        enc.movzx_rm8(x86::RAX, x86::RSI, 0);
        enc.test_rr(x86::RAX, x86::RAX);
        enc.je(back);
        enc.inc_r(x86::RSI);
        enc.jmp(scan);
        enc.bindLabel(back);
        enc.dec_r(x86::RSI);
        enc.movzx_rm8(x86::RAX, x86::RSI, 0);
        enc.cmp_ri32(x86::RAX, '0');
        enc.jne(noDot);
        enc.xor_r32(x86::RAX, x86::RAX);
        enc.mov_mr8(x86::RSI, 0, x86::RAX);
        enc.jmp(back);
        enc.bindLabel(noDot);
        enc.movzx_rm8(x86::RAX, x86::RSI, 0);
        enc.cmp_ri32(x86::RAX, '.');
        enc.jne(done);
        enc.xor_r32(x86::RAX, x86::RAX);
        enc.mov_mr8(x86::RSI, 0, x86::RAX);
        enc.bindLabel(done);
    }

    ThavaType getStaticExprType(ASTNode* n) {
        if (!n) return ThavaType::UNKNOWN;
        if (auto lit = dynamic_cast<LiteralNode*>(n)) return lit->getLitType();
        if (auto v = dynamic_cast<VarNode*>(n)) {
            VarInfo* vi = lookupVar(v->getName());
            if (vi) return vi->type;
            auto fit = functions.find(v->getName());
            if (fit != functions.end()) return fit->second.returnType;
        }
        if (auto call = dynamic_cast<CallNode*>(n)) {
            auto dlit = g_dlFunctions.find(call->name);
            if (dlit != g_dlFunctions.end()) return dlit->second.retType;
            auto fit = functions.find(call->name);
            if (fit != functions.end()) return fit->second.returnType;
        }
        if (auto bin = dynamic_cast<BinaryOpNode*>(n)) {
            const std::string& op = bin->op;
            if (op == "==" || op == "!=" || op == "<" || op == "<=" || op == ">" || op == ">=" || op == "and" || op == "or")
                return ThavaType::BOOL;
            ThavaType lt = getStaticExprType(bin->left.get());
            ThavaType rt = getStaticExprType(bin->right.get());
            if (lt == ThavaType::DOUBLE || rt == ThavaType::DOUBLE) return ThavaType::DOUBLE;
            if (lt == ThavaType::FLOAT || rt == ThavaType::FLOAT) return ThavaType::FLOAT;
            if (lt == ThavaType::STRING || rt == ThavaType::STRING) return ThavaType::STRING;
            return ThavaType::INT;
        }
        if (auto cast = dynamic_cast<CastNode*>(n)) return cast->targetType;
        if (auto idx = dynamic_cast<IndexNode*>(n)) {
            ThavaType et = elemTypeOfIndexedList(idx->base.get());
            if (et != ThavaType::UNKNOWN) return et;
        }
        if (auto cond = dynamic_cast<ConditionalExprNode*>(n)) {
            ThavaType tt = getStaticExprType(cond->trueExpr.get());
            ThavaType ft = getStaticExprType(cond->falseExpr.get());
            if (tt == ThavaType::DOUBLE || ft == ThavaType::DOUBLE) return ThavaType::DOUBLE;
            if (tt == ThavaType::FLOAT || ft == ThavaType::FLOAT) return ThavaType::FLOAT;
            return tt;
        }
        return ThavaType::UNKNOWN;
    }

    // ==========================================
    // EXPRESSION CODE GENERATION
    // ==========================================
    ThavaType emitExpr(ASTNode* node) {
        if (!node) return ThavaType::UNKNOWN;

        if (auto lit = dynamic_cast<LiteralNode*>(node)) {
            ThavaType t = lit->getLitType();
            const std::string& val = lit->getVal();
            if (t == ThavaType::NONE) {
                enc.mov_ri64(x86::RAX, 0);
                return ThavaType::NONE;
            }
            if (t == ThavaType::INT) {
                try {
                    int64_t iVal = std::stoll(val);
                    if (optLevel >= 1 && iVal == 0) {
                        enc.xor_r32(x86::RAX, x86::RAX);
                    } else {
                        enc.mov_ri64(x86::RAX, iVal);
                    }
                } catch (...) {
                    enc.xor_r32(x86::RAX, x86::RAX);
                }
                invalidateRegCache();
                return ThavaType::INT;
            }
            if (t == ThavaType::BOOL) {
                if (val == "true" || val == "1") {
                    enc.mov_ri64(x86::RAX, 1);
                } else {
                    enc.xor_r32(x86::RAX, x86::RAX);
                }
                invalidateRegCache();
                return ThavaType::BOOL;
            }
            if (t == ThavaType::FLOAT || t == ThavaType::DOUBLE) {
                double dVal = 0.0;
                try { dVal = std::stod(val); } catch (...) {}
                uint32_t off = pe.addDoubleLiteralOffset(dVal);
                enc.movsd_rip_rdata(x86::XMM0, off);
                return t;
            }
            if (t == ThavaType::STRING) {
                uint32_t off = pe.addStringLiteralOffset(val);
                enc.lea_rip_rdata(x86::RAX, off);
                return ThavaType::STRING;
            }
            return t;
        }

        if (auto cast = dynamic_cast<CastNode*>(node)) {
            ThavaType srcType = emitExpr(cast->expr.get());
            if (cast->targetType == srcType) return srcType;

            // A cast to a collection type is only a type annotation (e.g. <lst>[...]);
            // anything else would silently produce a bogus pointer.
            if (cast->targetType == ThavaType::LIST || cast->targetType == ThavaType::TUPLE ||
                cast->targetType == ThavaType::DICT) {
                std::cerr << "[Native Codegen Error]: cannot cast a "
                          << (srcType == ThavaType::INT ? "<int>" :
                              srcType == ThavaType::STRING ? "<str>" :
                              srcType == ThavaType::BOOL ? "<bl>" :
                              (srcType == ThavaType::DOUBLE || srcType == ThavaType::FLOAT) ? "<dbl>" : "value")
                          << " to a collection type (<lst>/<tple>/<dict>)\n";
                exit(1);
            }

            // Target: INT
            if (cast->targetType == ThavaType::INT) {
                if (srcType == ThavaType::NONE) {
                    enc.xor_r32(x86::RAX, x86::RAX);
                    return ThavaType::INT;
                }
                if (srcType == ThavaType::FLOAT || srcType == ThavaType::DOUBLE) {
                    enc.cvttsd2si(x86::RAX, x86::XMM0);
                    return ThavaType::INT;
                }
                if (srcType == ThavaType::STRING) {
                    enc.mov_rr(x86::RCX, x86::RAX);
                    enc.call_iat_func("_atoi64");
                    return ThavaType::INT;
                }
                if (srcType == ThavaType::BOOL) {
                    return ThavaType::INT;
                }
                return ThavaType::INT;
            }

            // Target: FLOAT or DOUBLE
            if (cast->targetType == ThavaType::FLOAT || cast->targetType == ThavaType::DOUBLE) {
                if (srcType == ThavaType::NONE) {
                    enc.xor_r32(x86::RAX, x86::RAX);
                    enc.cvtsi2sd(x86::XMM0, x86::RAX);
                    return cast->targetType;
                }
                if (srcType == ThavaType::INT) {
                    enc.cvtsi2sd(x86::XMM0, x86::RAX);
                    return cast->targetType;
                }
                if (srcType == ThavaType::STRING) {
                    enc.mov_rr(x86::RCX, x86::RAX);
                    enc.call_iat_func("atof");
                    return cast->targetType;
                }
                if (srcType == ThavaType::BOOL) {
                    enc.cvtsi2sd(x86::XMM0, x86::RAX);
                    return cast->targetType;
                }
                return cast->targetType;
            }

            // Target: STRING
            if (cast->targetType == ThavaType::STRING) {
                if (srcType == ThavaType::NONE) {
                    uint32_t offNone = pe.addStringLiteralOffset("none");
                    enc.lea_rip_rdata(x86::RAX, offNone);
                    return ThavaType::STRING;
                }
                currentFrameOffset -= 8;
                if (currentFrameOffset < maxFrameOffset) maxFrameOffset = currentFrameOffset;
                int32_t valSlot = currentFrameOffset;

                currentFrameOffset -= 8;
                if (currentFrameOffset < maxFrameOffset) maxFrameOffset = currentFrameOffset;
                int32_t bufSlot = currentFrameOffset;

                if (srcType == ThavaType::INT) {
                    enc.mov_mr_rbp(valSlot, x86::RAX);
                    enc.mov_ri64(x86::RCX, 64);
                    enc.call_iat_func("malloc");
                    enc.mov_mr_rbp(bufSlot, x86::RAX);

                    enc.mov_rr(x86::RCX, x86::RAX);
                    enc.lea_rip_rdata(x86::RDX, offFmtInt);
                    enc.mov_rm_rbp(x86::R8, valSlot);
                    enc.call_iat_func("sprintf");

                    enc.mov_rm_rbp(x86::RAX, bufSlot);
                    return ThavaType::STRING;
                }
                if (srcType == ThavaType::FLOAT || srcType == ThavaType::DOUBLE) {
                    enc.movsd_mr_rbp(valSlot, x86::XMM0);
                    enc.mov_ri64(x86::RCX, 64);
                    enc.call_iat_func("malloc");
                    enc.mov_mr_rbp(bufSlot, x86::RAX);

                    enc.mov_rr(x86::RCX, x86::RAX);
                    if constexpr (std::is_same_v<TEmitter, ElfEmitter>) {
                        // The zero-libc Linux runtime formats doubles itself.
                        enc.mov_rm_rbp(x86::R8, valSlot);
                        enc.call_iat_func("sprintf_d");
                    } else {
                        enc.lea_rip_rdata(x86::RDX, offFmtFloat6);
                        enc.movsd_rm_rbp(x86::XMM2, valSlot);
                        enc.mov_rm_rbp(x86::R8, valSlot);
                        enc.call_iat_func("sprintf");
                        enc.mov_rm_rbp(x86::RDI, bufSlot);
                        emitTrimTrailingZeros(x86::RDI);
                    }

                    enc.mov_rm_rbp(x86::RAX, bufSlot);
                    return ThavaType::STRING;
                }
                if (srcType == ThavaType::BOOL) {
                    size_t trueLbl = enc.createLabel();
                    size_t endLbl = enc.createLabel();
                    enc.test_rr(x86::RAX, x86::RAX);
                    enc.jne(trueLbl);
                    enc.lea_rip_rdata(x86::RAX, offStrFalse);
                    enc.jmp(endLbl);
                    enc.bindLabel(trueLbl);
                    enc.lea_rip_rdata(x86::RAX, offStrTrue);
                    enc.bindLabel(endLbl);
                    return ThavaType::STRING;
                }
                return ThavaType::STRING;
            }

            // Target: BOOL
            if (cast->targetType == ThavaType::BOOL) {
                if (srcType == ThavaType::INT) {
                    enc.test_rr(x86::RAX, x86::RAX);
                    enc.setcc_to_rax(x86::Cond::NE);
                    return ThavaType::BOOL;
                }
                if (srcType == ThavaType::FLOAT || srcType == ThavaType::DOUBLE) {
                    enc.xor_r32(x86::RAX, x86::RAX);
                    enc.cvtsi2sd(x86::XMM1, x86::RAX);
                    enc.ucomisd_rr(x86::XMM0, x86::XMM1);
                    enc.setcc_to_rax(x86::Cond::NE);
                    return ThavaType::BOOL;
                }
                if (srcType == ThavaType::STRING) {
                    enc.mov_rr(x86::RCX, x86::RAX);
                    enc.call_iat_func("strlen");
                    enc.test_rr(x86::RAX, x86::RAX);
                    enc.setcc_to_rax(x86::Cond::NE);
                    return ThavaType::BOOL;
                }
                return ThavaType::BOOL;
            }

            return cast->targetType;
        }

        if (auto var = dynamic_cast<VarNode*>(node)) {
            VarInfo* vi = lookupVar(var->getName());
            if (!vi && isGlobalName(var->getName())) {
                // Global read from inside a function/method.
                ThavaType gt = globalEnv ? globalEnv->get(var->getName()) : ThavaType::UNKNOWN;
                std::string gct = globalEnv ? globalEnv->getCustomType(var->getName()) : std::string();
                int32_t goff = globalVarSlots[var->getName()];
                emitGlobalBase(x86::R11, goff);
                if (gt == ThavaType::FLOAT || gt == ThavaType::DOUBLE) {
                    enc.movsd_rm(x86::XMM0, x86::R11, 0);
                } else {
                    enc.mov_rm(x86::RAX, x86::R11, 0);
                    if (gt == ThavaType::INT) narrowIntReg(x86::RAX, gct);
                }
                return gt;
            }
            if (!vi) {
                auto fit = functions.find(var->getName());
                if (fit != functions.end()) {
                    enc.lea_rip_label(x86::RAX, fit->second.labelId);
                    return ThavaType::FUNC;
                }
                std::cerr << "[Native Codegen Error]: Variable or function '" << var->getName() << "' is not declared!\n";
                exit(1);
            }
            if (vi->type == ThavaType::FLOAT || vi->type == ThavaType::DOUBLE) {
                enc.movsd_rm_rbp(x86::XMM0, vi->offset);
            } else {
                enc.mov_rm_rbp(x86::RAX, vi->offset);
                if (vi->type == ThavaType::INT) narrowIntReg(x86::RAX, vi->customType);
            }
            return vi->type;
        }

        if (auto coll = dynamic_cast<CollectionNode*>(node)) {
            size_t n = coll->getElements().size();
            enc.mov_ri64(x86::RCX, (n + 1) * 8);
            enc.call_iat_func("malloc");

            currentFrameOffset -= 8;
            if (currentFrameOffset < maxFrameOffset) maxFrameOffset = currentFrameOffset;
            int32_t listSlot = currentFrameOffset;
            enc.mov_mr_rbp(listSlot, x86::RAX);

            enc.mov_ri64(x86::R10, n);
            enc.mov_mr(x86::RAX, 0, x86::R10);

            ThavaType detectedElemType = ThavaType::INT;
            for (size_t i = 0; i < n; ++i) {
                ThavaType elemT = emitExpr(coll->getElements()[i].get());
                if (elemT == ThavaType::FLOAT || elemT == ThavaType::DOUBLE) {
                    detectedElemType = ThavaType::DOUBLE;
                } else if (elemT == ThavaType::STRING && detectedElemType != ThavaType::DOUBLE) {
                    detectedElemType = ThavaType::STRING;
                }
                enc.mov_rm_rbp(x86::R10, listSlot);
                if (elemT == ThavaType::FLOAT || elemT == ThavaType::DOUBLE) {
                    enc.movsd_mr(x86::R10, (int32_t)(8 + i * 8), x86::XMM0);
                } else {
                    enc.mov_mr(x86::R10, (int32_t)(8 + i * 8), x86::RAX);
                }
            }
            lastCollectionElemType = (n > 0) ? detectedElemType : ThavaType::UNKNOWN;

            enc.mov_rm_rbp(x86::RAX, listSlot);
            return coll->getIsTuple() ? ThavaType::TUPLE : ThavaType::LIST;
        }

        if (auto dictNode = dynamic_cast<DictNode*>(node)) {
            size_t n = dictNode->entries.size();
            size_t cap = n > 4 ? n : 4;
            enc.mov_ri64(x86::RCX, 16 + cap * 32);
            enc.call_iat_func("malloc");

            currentFrameOffset -= 8;
            if (currentFrameOffset < maxFrameOffset) maxFrameOffset = currentFrameOffset;
            int32_t dictSlot = currentFrameOffset;
            enc.mov_mr_rbp(dictSlot, x86::RAX);

            enc.mov_ri64(x86::R10, n);
            enc.mov_mr(x86::RAX, 0, x86::R10);
            enc.mov_ri64(x86::R10, cap);
            enc.mov_rm_rbp(x86::RAX, dictSlot);
            enc.mov_mr(x86::RAX, 8, x86::R10);

            for (size_t i = 0; i < n; ++i) {
                ThavaType kt = emitExpr(dictNode->entries[i].first.get());
                currentFrameOffset -= 8;
                if (currentFrameOffset < maxFrameOffset) maxFrameOffset = currentFrameOffset;
                int32_t keySlot = currentFrameOffset;
                enc.mov_mr_rbp(keySlot, x86::RAX);

                ThavaType vt = emitExpr(dictNode->entries[i].second.get());
                enc.mov_rm_rbp(x86::R10, dictSlot);
                int32_t entryOff = 16 + (int32_t)(i * 32);
                enc.mov_mr(x86::R10, entryOff + 8, x86::RAX); // val
                enc.mov_rm_rbp(x86::R11, keySlot);
                enc.mov_mr(x86::R10, entryOff + 0, x86::R11); // key
                enc.mov_ri64(x86::RAX, (int)kt);
                enc.mov_mr(x86::R10, entryOff + 16, x86::RAX); // keyType
                enc.mov_ri64(x86::RAX, (int)vt);
                enc.mov_mr(x86::R10, entryOff + 24, x86::RAX); // valType
            }

            enc.mov_rm_rbp(x86::RAX, dictSlot);
            return ThavaType::DICT;
        }

        if (auto dictM = dynamic_cast<DictMethodNode*>(node)) {
            ThavaType baseT = emitExpr(dictM->base.get());
            currentFrameOffset -= 8;
            if (currentFrameOffset < maxFrameOffset) maxFrameOffset = currentFrameOffset;
            int32_t dictSlot = currentFrameOffset;
            enc.mov_mr_rbp(dictSlot, x86::RAX);

            if (dictM->method == "len" || dictM->method == "size") {
                enc.mov_rm(x86::RAX, x86::RAX, 0); // count is at offset 0
                return ThavaType::INT;
            }
            if (dictM->method == "clear") {
                enc.mov_ri64(x86::R10, 0);
                enc.mov_mr(x86::RAX, 0, x86::R10); // set count to 0
                return ThavaType::VOID;
            }
            if (dictM->method == "keys") {
                enc.mov_rm(x86::RCX, x86::RAX, 0); // count
                currentFrameOffset -= 8;
                if (currentFrameOffset < maxFrameOffset) maxFrameOffset = currentFrameOffset;
                int32_t countSlot = currentFrameOffset;
                enc.mov_mr_rbp(countSlot, x86::RCX);

                enc.mov_ri64(x86::RAX, 1);
                enc.add_rr(x86::RCX, x86::RAX);
                enc.mov_ri64(x86::RAX, 8);
                enc.imul_rr(x86::RCX, x86::RAX);
                enc.call_iat_func("malloc");

                currentFrameOffset -= 8;
                if (currentFrameOffset < maxFrameOffset) maxFrameOffset = currentFrameOffset;
                int32_t listSlot = currentFrameOffset;
                enc.mov_mr_rbp(listSlot, x86::RAX);

                enc.mov_rm_rbp(x86::R10, countSlot);
                enc.mov_mr(x86::RAX, 0, x86::R10);

                size_t loopK = enc.createLabel();
                size_t endK = enc.createLabel();
                currentFrameOffset -= 8;
                if (currentFrameOffset < maxFrameOffset) maxFrameOffset = currentFrameOffset;
                int32_t iSlot = currentFrameOffset;
                enc.mov_ri64(x86::RAX, 0);
                enc.mov_mr_rbp(iSlot, x86::RAX);

                enc.bindLabel(loopK);
                enc.mov_rm_rbp(x86::RAX, iSlot);
                enc.mov_rm_rbp(x86::R10, countSlot);
                enc.cmp_rr(x86::RAX, x86::R10);
                enc.jge(endK);

                enc.mov_rm_rbp(x86::R10, dictSlot);
                enc.mov_ri64(x86::R11, 32);
                enc.imul_rr(x86::RAX, x86::R11);
                enc.add_rr(x86::R10, x86::RAX);
                enc.mov_rm(x86::R11, x86::R10, 16); // key

                enc.mov_rm_rbp(x86::RAX, iSlot);
                enc.mov_ri64(x86::RDX, 8);
                enc.imul_rr(x86::RAX, x86::RDX);
                enc.mov_rm_rbp(x86::R10, listSlot);
                enc.add_rr(x86::R10, x86::RAX);
                enc.mov_mr(x86::R10, 8, x86::R11);

                enc.mov_rm_rbp(x86::RAX, iSlot);
                enc.mov_ri64(x86::R11, 1);
                enc.add_rr(x86::RAX, x86::R11);
                enc.mov_mr_rbp(iSlot, x86::RAX);
                enc.jmp(loopK);

                enc.bindLabel(endK);
                enc.mov_rm_rbp(x86::RAX, listSlot);
                return ThavaType::LIST;
            }
            if (dictM->method == "values") {
                enc.mov_rm(x86::RCX, x86::RAX, 0); // count
                currentFrameOffset -= 8;
                if (currentFrameOffset < maxFrameOffset) maxFrameOffset = currentFrameOffset;
                int32_t countSlot = currentFrameOffset;
                enc.mov_mr_rbp(countSlot, x86::RCX);

                enc.mov_ri64(x86::RAX, 1);
                enc.add_rr(x86::RCX, x86::RAX);
                enc.mov_ri64(x86::RAX, 8);
                enc.imul_rr(x86::RCX, x86::RAX);
                enc.call_iat_func("malloc");

                currentFrameOffset -= 8;
                if (currentFrameOffset < maxFrameOffset) maxFrameOffset = currentFrameOffset;
                int32_t listSlot = currentFrameOffset;
                enc.mov_mr_rbp(listSlot, x86::RAX);

                enc.mov_rm_rbp(x86::R10, countSlot);
                enc.mov_mr(x86::RAX, 0, x86::R10);

                size_t loopV = enc.createLabel();
                size_t endV = enc.createLabel();
                currentFrameOffset -= 8;
                if (currentFrameOffset < maxFrameOffset) maxFrameOffset = currentFrameOffset;
                int32_t iSlot = currentFrameOffset;
                enc.mov_ri64(x86::RAX, 0);
                enc.mov_mr_rbp(iSlot, x86::RAX);

                enc.bindLabel(loopV);
                enc.mov_rm_rbp(x86::RAX, iSlot);
                enc.mov_rm_rbp(x86::R10, countSlot);
                enc.cmp_rr(x86::RAX, x86::R10);
                enc.jge(endV);

                enc.mov_rm_rbp(x86::R10, dictSlot);
                enc.mov_ri64(x86::R11, 32);
                enc.imul_rr(x86::RAX, x86::R11);
                enc.add_rr(x86::R10, x86::RAX);
                enc.mov_rm(x86::R11, x86::R10, 24); // val

                enc.mov_rm_rbp(x86::RAX, iSlot);
                enc.mov_ri64(x86::RDX, 8);
                enc.imul_rr(x86::RAX, x86::RDX);
                enc.mov_rm_rbp(x86::R10, listSlot);
                enc.add_rr(x86::R10, x86::RAX);
                enc.mov_mr(x86::R10, 8, x86::R11);

                enc.mov_rm_rbp(x86::RAX, iSlot);
                enc.mov_ri64(x86::R11, 1);
                enc.add_rr(x86::RAX, x86::R11);
                enc.mov_mr_rbp(iSlot, x86::RAX);
                enc.jmp(loopV);

                enc.bindLabel(endV);
                enc.mov_rm_rbp(x86::RAX, listSlot);
                return ThavaType::LIST;
            }
            if (dictM->method == "has" || dictM->method == "contains") {
                if (dictM->args.empty()) { enc.mov_ri64(x86::RAX, 0); return ThavaType::BOOL; }
                ThavaType kt = emitExpr(dictM->args[0].get());
                currentFrameOffset -= 8;
                if (currentFrameOffset < maxFrameOffset) maxFrameOffset = currentFrameOffset;
                int32_t keySlot = currentFrameOffset;
                enc.mov_mr_rbp(keySlot, x86::RAX);

                size_t searchLoop = enc.createLabel();
                size_t foundLbl = enc.createLabel();
                size_t notFoundLbl = enc.createLabel();
                size_t endHas = enc.createLabel();

                currentFrameOffset -= 8;
                if (currentFrameOffset < maxFrameOffset) maxFrameOffset = currentFrameOffset;
                int32_t iSlot = currentFrameOffset;
                enc.mov_ri64(x86::RAX, 0);
                enc.mov_mr_rbp(iSlot, x86::RAX);

                enc.bindLabel(searchLoop);
                enc.mov_rm_rbp(x86::RAX, iSlot);
                enc.mov_rm_rbp(x86::R10, dictSlot);
                enc.mov_rm(x86::R11, x86::R10, 0);
                enc.cmp_rr(x86::RAX, x86::R11);
                enc.jge(notFoundLbl);

                enc.mov_ri64(x86::R11, 32);
                enc.imul_rr(x86::RAX, x86::R11);
                enc.add_rr(x86::R10, x86::RAX);
                if (kt == ThavaType::STRING) {
                    enc.mov_rm(x86::RCX, x86::R10, 16);
                    enc.mov_rm_rbp(x86::RDX, keySlot);
                    enc.call_iat_func("strcmp");
                    enc.test_rr(x86::RAX, x86::RAX);
                    enc.je(foundLbl);
                } else {
                    enc.mov_rm(x86::RAX, x86::R10, 16);
                    enc.mov_rm_rbp(x86::R11, keySlot);
                    enc.cmp_rr(x86::RAX, x86::R11);
                    enc.je(foundLbl);
                }

                enc.mov_rm_rbp(x86::RAX, iSlot);
                enc.mov_ri64(x86::R11, 1);
                enc.add_rr(x86::RAX, x86::R11);
                enc.mov_mr_rbp(iSlot, x86::RAX);
                enc.jmp(searchLoop);

                enc.bindLabel(foundLbl);
                enc.mov_ri64(x86::RAX, 1);
                enc.jmp(endHas);

                enc.bindLabel(notFoundLbl);
                enc.mov_ri64(x86::RAX, 0);

                enc.bindLabel(endHas);
                return ThavaType::BOOL;
            }
            if (dictM->method == "get") {
                if (dictM->args.empty()) { enc.mov_ri64(x86::RAX, 0); return ThavaType::INT; }
                ThavaType kt = emitExpr(dictM->args[0].get());
                currentFrameOffset -= 8;
                if (currentFrameOffset < maxFrameOffset) maxFrameOffset = currentFrameOffset;
                int32_t keySlot = currentFrameOffset;
                enc.mov_mr_rbp(keySlot, x86::RAX);

                int32_t defSlot = 0;
                if (dictM->args.size() > 1) {
                    emitExpr(dictM->args[1].get());
                    currentFrameOffset -= 8;
                    if (currentFrameOffset < maxFrameOffset) maxFrameOffset = currentFrameOffset;
                    defSlot = currentFrameOffset;
                    enc.mov_mr_rbp(defSlot, x86::RAX);
                }

                size_t searchLoop = enc.createLabel();
                size_t foundLbl = enc.createLabel();
                size_t notFoundLbl = enc.createLabel();
                size_t endGet = enc.createLabel();

                currentFrameOffset -= 8;
                if (currentFrameOffset < maxFrameOffset) maxFrameOffset = currentFrameOffset;
                int32_t iSlot = currentFrameOffset;
                enc.mov_ri64(x86::RAX, 0);
                enc.mov_mr_rbp(iSlot, x86::RAX);

                enc.bindLabel(searchLoop);
                enc.mov_rm_rbp(x86::RAX, iSlot);
                enc.mov_rm_rbp(x86::R10, dictSlot);
                enc.mov_rm(x86::R11, x86::R10, 0);
                enc.cmp_rr(x86::RAX, x86::R11);
                enc.jge(notFoundLbl);

                enc.mov_ri64(x86::R11, 32);
                enc.imul_rr(x86::RAX, x86::R11);
                enc.add_rr(x86::R10, x86::RAX);
                if (kt == ThavaType::STRING) {
                    enc.mov_rm(x86::RCX, x86::R10, 16);
                    enc.mov_rm_rbp(x86::RDX, keySlot);
                    enc.call_iat_func("strcmp");
                    enc.test_rr(x86::RAX, x86::RAX);
                    enc.je(foundLbl);
                } else {
                    enc.mov_rm(x86::RAX, x86::R10, 16);
                    enc.mov_rm_rbp(x86::R11, keySlot);
                    enc.cmp_rr(x86::RAX, x86::R11);
                    enc.je(foundLbl);
                }

                enc.mov_rm_rbp(x86::RAX, iSlot);
                enc.mov_ri64(x86::R11, 1);
                enc.add_rr(x86::RAX, x86::R11);
                enc.mov_mr_rbp(iSlot, x86::RAX);
                enc.jmp(searchLoop);

                enc.bindLabel(foundLbl);
                enc.mov_rm_rbp(x86::RAX, iSlot);
                enc.mov_ri64(x86::R11, 32);
                enc.imul_rr(x86::RAX, x86::R11);
                enc.mov_rm_rbp(x86::R10, dictSlot);
                enc.add_rr(x86::R10, x86::RAX);
                enc.mov_rm(x86::RAX, x86::R10, 24);
                enc.jmp(endGet);

                enc.bindLabel(notFoundLbl);
                if (dictM->args.size() > 1) {
                    enc.mov_rm_rbp(x86::RAX, defSlot);
                } else {
                    enc.mov_ri64(x86::RAX, 0);
                }

                enc.bindLabel(endGet);
                return ThavaType::INT;
            }
            if (dictM->method == "remove" || dictM->method == "del") {
                if (dictM->args.empty()) { enc.mov_ri64(x86::RAX, 0); return ThavaType::VOID; }
                ThavaType kt = emitExpr(dictM->args[0].get());
                currentFrameOffset -= 8;
                if (currentFrameOffset < maxFrameOffset) maxFrameOffset = currentFrameOffset;
                int32_t keySlot = currentFrameOffset;
                enc.mov_mr_rbp(keySlot, x86::RAX);

                size_t searchLoop = enc.createLabel();
                size_t foundLbl = enc.createLabel();
                size_t notFoundLbl = enc.createLabel();
                size_t shiftLoop = enc.createLabel();
                size_t endShift = enc.createLabel();

                currentFrameOffset -= 8;
                if (currentFrameOffset < maxFrameOffset) maxFrameOffset = currentFrameOffset;
                int32_t iSlot = currentFrameOffset;
                enc.mov_ri64(x86::RAX, 0);
                enc.mov_mr_rbp(iSlot, x86::RAX);

                enc.bindLabel(searchLoop);
                enc.mov_rm_rbp(x86::RAX, iSlot);
                enc.mov_rm_rbp(x86::R10, dictSlot);
                enc.mov_rm(x86::R11, x86::R10, 0);
                enc.cmp_rr(x86::RAX, x86::R11);
                enc.jge(notFoundLbl);

                enc.mov_ri64(x86::R11, 32);
                enc.imul_rr(x86::RAX, x86::R11);
                enc.add_rr(x86::R10, x86::RAX);
                if (kt == ThavaType::STRING) {
                    enc.mov_rm(x86::RCX, x86::R10, 16);
                    enc.mov_rm_rbp(x86::RDX, keySlot);
                    enc.call_iat_func("strcmp");
                    enc.test_rr(x86::RAX, x86::RAX);
                    enc.je(foundLbl);
                } else {
                    enc.mov_rm(x86::RAX, x86::R10, 16);
                    enc.mov_rm_rbp(x86::R11, keySlot);
                    enc.cmp_rr(x86::RAX, x86::R11);
                    enc.je(foundLbl);
                }

                enc.mov_rm_rbp(x86::RAX, iSlot);
                enc.mov_ri64(x86::R11, 1);
                enc.add_rr(x86::RAX, x86::R11);
                enc.mov_mr_rbp(iSlot, x86::RAX);
                enc.jmp(searchLoop);

                enc.bindLabel(foundLbl);
                enc.bindLabel(shiftLoop);
                enc.mov_rm_rbp(x86::RAX, iSlot);
                enc.mov_ri64(x86::R11, 1);
                enc.add_rr(x86::RAX, x86::R11);
                enc.mov_rm_rbp(x86::R10, dictSlot);
                enc.mov_rm(x86::R11, x86::R10, 0);
                enc.cmp_rr(x86::RAX, x86::R11);
                enc.jge(endShift);

                enc.mov_rm_rbp(x86::RAX, iSlot);
                enc.mov_ri64(x86::R11, 32);
                enc.imul_rr(x86::RAX, x86::R11);
                enc.mov_rm_rbp(x86::RDX, dictSlot);
                enc.add_rr(x86::RDX, x86::RAX);

                enc.mov_rm(x86::R11, x86::RDX, 16 + 32);
                enc.mov_mr(x86::RDX, 16, x86::R11);
                enc.mov_rm(x86::R11, x86::RDX, 24 + 32);
                enc.mov_mr(x86::RDX, 24, x86::R11);
                enc.mov_rm(x86::R11, x86::RDX, 32 + 32);
                enc.mov_mr(x86::RDX, 32, x86::R11);
                enc.mov_rm(x86::R11, x86::RDX, 40 + 32);
                enc.mov_mr(x86::RDX, 40, x86::R11);

                enc.mov_rm_rbp(x86::RAX, iSlot);
                enc.mov_ri64(x86::R11, 1);
                enc.add_rr(x86::RAX, x86::R11);
                enc.mov_mr_rbp(iSlot, x86::RAX);
                enc.jmp(shiftLoop);

                enc.bindLabel(endShift);
                enc.mov_rm_rbp(x86::R10, dictSlot);
                enc.mov_rm(x86::RAX, x86::R10, 0);
                enc.mov_ri64(x86::R11, 1);
                enc.sub_rr(x86::RAX, x86::R11);
                enc.mov_mr(x86::R10, 0, x86::RAX);

                enc.bindLabel(notFoundLbl);
                return ThavaType::VOID;
            }
            enc.mov_rm_rbp(x86::RAX, dictSlot);
            return ThavaType::UNKNOWN;
        }

        if (auto inNode = dynamic_cast<InNode*>(node)) {
            ThavaType itemType = emitExpr(inNode->left.get());
            currentFrameOffset -= 8;
            if (currentFrameOffset < maxFrameOffset) maxFrameOffset = currentFrameOffset;
            int32_t itemSlot = currentFrameOffset;
            enc.mov_mr_rbp(itemSlot, x86::RAX);

            ThavaType contType = emitExpr(inNode->listExpr.get());
            if (contType == ThavaType::DICT) {
                currentFrameOffset -= 8;
                if (currentFrameOffset < maxFrameOffset) maxFrameOffset = currentFrameOffset;
                int32_t dictSlot = currentFrameOffset;
                enc.mov_mr_rbp(dictSlot, x86::RAX);

                size_t searchLoop = enc.createLabel();
                size_t foundLbl = enc.createLabel();
                size_t notFoundLbl = enc.createLabel();
                size_t endIn = enc.createLabel();

                currentFrameOffset -= 8;
                if (currentFrameOffset < maxFrameOffset) maxFrameOffset = currentFrameOffset;
                int32_t iSlot = currentFrameOffset;
                enc.mov_ri64(x86::RAX, 0);
                enc.mov_mr_rbp(iSlot, x86::RAX);

                enc.bindLabel(searchLoop);
                enc.mov_rm_rbp(x86::RAX, iSlot);
                enc.mov_rm_rbp(x86::R10, dictSlot);
                enc.mov_rm(x86::R11, x86::R10, 0);
                enc.cmp_rr(x86::RAX, x86::R11);
                enc.jge(notFoundLbl);

                enc.mov_ri64(x86::R11, 32);
                enc.imul_rr(x86::RAX, x86::R11);
                enc.add_rr(x86::R10, x86::RAX);
                if (itemType == ThavaType::STRING) {
                    enc.mov_rm(x86::RCX, x86::R10, 16);
                    enc.mov_rm_rbp(x86::RDX, itemSlot);
                    enc.call_iat_func("strcmp");
                    enc.test_rr(x86::RAX, x86::RAX);
                    enc.je(foundLbl);
                } else {
                    enc.mov_rm(x86::RAX, x86::R10, 16);
                    enc.mov_rm_rbp(x86::R11, itemSlot);
                    enc.cmp_rr(x86::RAX, x86::R11);
                    enc.je(foundLbl);
                }

                enc.mov_rm_rbp(x86::RAX, iSlot);
                enc.mov_ri64(x86::R11, 1);
                enc.add_rr(x86::RAX, x86::R11);
                enc.mov_mr_rbp(iSlot, x86::RAX);
                enc.jmp(searchLoop);

                enc.bindLabel(foundLbl);
                enc.mov_ri64(x86::RAX, 1);
                enc.jmp(endIn);

                enc.bindLabel(notFoundLbl);
                enc.mov_ri64(x86::RAX, 0);

                enc.bindLabel(endIn);
                return ThavaType::BOOL;
            }

            // Substring test: "needle" in "haystack"
            if (contType == ThavaType::STRING) {
                enc.mov_rr(x86::RCX, x86::RAX);      // haystack = container
                enc.mov_rm_rbp(x86::RDX, itemSlot);  // sub = item
                enc.call_label(lblStrFind);
                enc.cmp_ri32(x86::RAX, 0);
                enc.setcc_to_rax(x86::Cond::GE);
                return ThavaType::BOOL;
            }

            enc.mov_rr(x86::RDX, x86::RAX);
            enc.mov_rm_rbp(x86::RCX, itemSlot);
            enc.mov_ri64(x86::R8, itemType == ThavaType::STRING ? 1 : 0);
            enc.call_label(lblListIn);
            return ThavaType::BOOL;
        }

        if (auto inpCase = dynamic_cast<InputCaseNode*>(node)) {
            uint32_t promptOff = pe.addStringLiteralOffset(inpCase->prompt);
            enc.lea_rip_rdata(x86::RCX, promptOff);
            enc.mov_ri64(x86::RDX, inpCase->mode == "lower" ? 1 : (inpCase->mode == "upper" ? 2 : 0));
            enc.call_label(lblInputLine);
            return ThavaType::STRING;
        }

        if (auto strMethod = dynamic_cast<StrMethodNode*>(node)) {
            emitExpr(strMethod->base.get());
            enc.mov_rr(x86::RCX, x86::RAX);
            if (strMethod->method == "lower") {
                enc.call_label(lblStrLower);
            } else if (strMethod->method == "upper") {
                enc.call_label(lblStrUpper);
            } else if (strMethod->method == "title") {
                enc.call_label(lblStrTitle);
            } else if (strMethod->method == "strip") {
                enc.call_label(lblStrStrip);
            } else {
                enc.call_label(lblStrLower);
            }
            return ThavaType::STRING;
        }

        if (auto strSplit = dynamic_cast<StrSplitNode*>(node)) {
            emitExpr(strSplit->base.get());
            currentFrameOffset -= 8;
            if (currentFrameOffset < maxFrameOffset) maxFrameOffset = currentFrameOffset;
            int32_t baseSlot = currentFrameOffset;
            enc.mov_mr_rbp(baseSlot, x86::RAX);

            emitExpr(strSplit->delimExpr.get());
            enc.mov_rr(x86::RDX, x86::RAX);
            enc.mov_rm_rbp(x86::RCX, baseSlot);
            enc.call_label(lblStrSplit); // list buffer in RAX

            // Extract element at idxACode
            int32_t aIdx = 0;
            try { aIdx = std::stoi(strSplit->idxACode); } catch(...) { aIdx = 0; }
            enc.mov_rm(x86::RAX, x86::RAX, 8 + aIdx * 8);
            return ThavaType::STRING;
        }

        if (auto advStr = dynamic_cast<StrAdvMethodNode*>(node)) {
            if (advStr->method == "replace") {
                emitExpr(advStr->base.get());
                currentFrameOffset -= 8;
                if (currentFrameOffset < maxFrameOffset) maxFrameOffset = currentFrameOffset;
                int32_t baseSlot = currentFrameOffset;
                enc.mov_mr_rbp(baseSlot, x86::RAX);

                emitExpr(advStr->args[0].get());
                currentFrameOffset -= 8;
                if (currentFrameOffset < maxFrameOffset) maxFrameOffset = currentFrameOffset;
                int32_t oldSlot = currentFrameOffset;
                enc.mov_mr_rbp(oldSlot, x86::RAX);

                emitExpr(advStr->args[1].get());
                enc.mov_rr(x86::R8, x86::RAX);
                enc.mov_rm_rbp(x86::RDX, oldSlot);
                enc.mov_rm_rbp(x86::RCX, baseSlot);
                enc.call_label(lblStrReplace);
                return ThavaType::STRING;
            }
            if (advStr->method == "find" || advStr->method == "index") {
                emitExpr(advStr->base.get());
                currentFrameOffset -= 8;
                if (currentFrameOffset < maxFrameOffset) maxFrameOffset = currentFrameOffset;
                int32_t baseSlot = currentFrameOffset;
                enc.mov_mr_rbp(baseSlot, x86::RAX);

                emitExpr(advStr->args[0].get());
                enc.mov_rr(x86::RDX, x86::RAX);
                enc.mov_rm_rbp(x86::RCX, baseSlot);
                enc.call_label(lblStrFind);
                return ThavaType::INT;
            }
            if (advStr->method == "startswith") {
                emitExpr(advStr->base.get());
                currentFrameOffset -= 8;
                if (currentFrameOffset < maxFrameOffset) maxFrameOffset = currentFrameOffset;
                int32_t baseSlot = currentFrameOffset;
                enc.mov_mr_rbp(baseSlot, x86::RAX);

                emitExpr(advStr->args[0].get());
                enc.mov_rr(x86::RDX, x86::RAX);
                enc.mov_rm_rbp(x86::RCX, baseSlot);
                enc.call_label(lblStrStartswith);
                return ThavaType::BOOL;
            }
            if (advStr->method == "endswith") {
                emitExpr(advStr->base.get());
                currentFrameOffset -= 8;
                if (currentFrameOffset < maxFrameOffset) maxFrameOffset = currentFrameOffset;
                int32_t baseSlot = currentFrameOffset;
                enc.mov_mr_rbp(baseSlot, x86::RAX);

                emitExpr(advStr->args[0].get());
                enc.mov_rr(x86::RDX, x86::RAX);
                enc.mov_rm_rbp(x86::RCX, baseSlot);
                enc.call_label(lblStrEndswith);
                return ThavaType::BOOL;
            }
            if (advStr->method == "count") {
                emitExpr(advStr->base.get());
                currentFrameOffset -= 8;
                if (currentFrameOffset < maxFrameOffset) maxFrameOffset = currentFrameOffset;
                int32_t baseSlot = currentFrameOffset;
                enc.mov_mr_rbp(baseSlot, x86::RAX);

                emitExpr(advStr->args[0].get());
                enc.mov_rr(x86::RDX, x86::RAX);
                enc.mov_rm_rbp(x86::RCX, baseSlot);
                enc.call_label(lblStrCount);
                return ThavaType::INT;
            }
            if (advStr->method == "join") {
                emitExpr(advStr->base.get());
                currentFrameOffset -= 8;
                if (currentFrameOffset < maxFrameOffset) maxFrameOffset = currentFrameOffset;
                int32_t baseSlot = currentFrameOffset;
                enc.mov_mr_rbp(baseSlot, x86::RAX);

                emitExpr(advStr->args[0].get());
                enc.mov_rr(x86::RDX, x86::RAX);
                enc.mov_rm_rbp(x86::RCX, baseSlot);
                enc.call_label(lblStrJoin);
                return ThavaType::STRING;
            }
            if (advStr->method == "split") {
                emitExpr(advStr->base.get());
                currentFrameOffset -= 8;
                if (currentFrameOffset < maxFrameOffset) maxFrameOffset = currentFrameOffset;
                int32_t baseSlot = currentFrameOffset;
                enc.mov_mr_rbp(baseSlot, x86::RAX);

                if (!advStr->args.empty()) {
                    emitExpr(advStr->args[0].get());
                    enc.mov_rr(x86::RDX, x86::RAX);
                } else {
                    uint32_t spaceOff = pe.addStringLiteralOffset(" ");
                    enc.lea_rip_rdata(x86::RDX, spaceOff);
                }
                enc.mov_rm_rbp(x86::RCX, baseSlot);
                enc.call_label(lblStrSplit);
                return ThavaType::LIST;
            }
        }

        if (auto strSub = dynamic_cast<StrSubstringNode*>(node)) {
            int32_t savedOffset = currentFrameOffset;
            emitExpr(strSub->base.get());
            currentFrameOffset -= 8;
            if (currentFrameOffset < maxFrameOffset) maxFrameOffset = currentFrameOffset;
            int32_t baseSlot = currentFrameOffset;
            enc.mov_mr_rbp(baseSlot, x86::RAX);

            emitExpr(strSub->startExpr.get());
            currentFrameOffset -= 8;
            if (currentFrameOffset < maxFrameOffset) maxFrameOffset = currentFrameOffset;
            int32_t startSlot = currentFrameOffset;
            enc.mov_mr_rbp(startSlot, x86::RAX);

            emitExpr(strSub->endExpr.get());
            enc.mov_rr(x86::R8, x86::RAX); // end
            enc.mov_rm_rbp(x86::RDX, startSlot); // start
            enc.mov_rm_rbp(x86::RCX, baseSlot);  // s
            enc.call_label(lblStrSubstring);
            currentFrameOffset = savedOffset;
            return ThavaType::STRING;
        }

        if (auto uminus = dynamic_cast<UnaryMinusNode*>(node)) {
            ThavaType t = emitExpr(uminus->expr.get());
            if (t == ThavaType::FLOAT || t == ThavaType::DOUBLE) {
                uint32_t off = pe.addDoubleLiteralOffset(0.0);
                enc.movsd_rip_rdata(x86::XMM1, off);
                enc.subsd_rr(x86::XMM1, x86::XMM0);
                enc.movsd_rr(x86::XMM0, x86::XMM1);
            } else {
                enc.neg_r(x86::RAX);
            }
            return t;
        }

        if (auto unot = dynamic_cast<UnaryNotNode*>(node)) {
            emitExpr(unot->expr.get());
            enc.test_rr(x86::RAX, x86::RAX);
            enc.setcc_to_rax(x86::Cond::E);
            return ThavaType::BOOL;
        }

        if (auto unBN = dynamic_cast<UnaryBNotNode*>(node)) {
            emitExpr(unBN->expr.get());
            enc.not_r(x86::RAX);
            return ThavaType::INT;
        }

        if (auto condExpr = dynamic_cast<ConditionalExprNode*>(node)) {
            size_t falseLbl = enc.createLabel();
            size_t endLbl = enc.createLabel();

            ThavaType tType = getStaticExprType(condExpr->trueExpr.get());
            ThavaType fType = getStaticExprType(condExpr->falseExpr.get());
            bool isDouble = (tType == ThavaType::DOUBLE || tType == ThavaType::FLOAT ||
                             fType == ThavaType::DOUBLE || fType == ThavaType::FLOAT);

            emitExpr(condExpr->condition.get());
            enc.test_rr(x86::RAX, x86::RAX);
            enc.je(falseLbl);

            ThavaType tt = emitExpr(condExpr->trueExpr.get());
            if (isDouble && tt == ThavaType::INT) {
                enc.cvtsi2sd(x86::XMM0, x86::RAX);
            }
            enc.jmp(endLbl);

            enc.bindLabel(falseLbl);
            ThavaType ft = emitExpr(condExpr->falseExpr.get());
            if (isDouble && ft == ThavaType::INT) {
                enc.cvtsi2sd(x86::XMM0, x86::RAX);
            }

            enc.bindLabel(endLbl);
            return isDouble ? ThavaType::DOUBLE : tt;
        }

        if (auto bin = dynamic_cast<BinaryOpNode*>(node)) {
            std::string op = bin->op;

            // --- OPTIMIZATION (optLevel >= 1): Immediate & Direct Local Variable Operations ---
            if (optLevel >= 1) {
                // 1. Right operand is an integer literal
                if (auto litR = dynamic_cast<LiteralNode*>(bin->right.get())) {
                    if (litR->getLitType() == ThavaType::INT) {
                        int64_t imm = 0;
                        try { imm = std::stoll(litR->getVal()); } catch (...) {}

                        if (op == "+" || op == "-" || op == "*" || op == "/" || op == "//" || op == "%" ||
                            op == "==" || op == "!=" || op == "<" || op == "<=" || op == ">" || op == ">=") {
                            ThavaType lt = emitExpr(bin->left.get());
                            if (lt == ThavaType::INT) {
                                if (op == "+") {
                                    if (imm == 1) enc.inc_r(x86::RAX);
                                    else if (imm != 0) enc.add_ri(x86::RAX, (int32_t)imm);
                                    invalidateRegCache();
                                    return ThavaType::INT;
                                }
                                if (op == "-") {
                                    if (imm == 1) enc.dec_r(x86::RAX);
                                    else if (imm != 0) enc.sub_ri(x86::RAX, (int32_t)imm);
                                    invalidateRegCache();
                                    return ThavaType::INT;
                                }
                                if (op == "*") {
                                    if (imm == 0) {
                                        enc.xor_r32(x86::RAX, x86::RAX);
                                    } else if (imm == 1) {
                                        // no-op
                                    } else if (imm == 2) {
                                        enc.add_rr(x86::RAX, x86::RAX);
                                    } else if (imm > 0 && (imm & (imm - 1)) == 0 && imm <= (1LL << 30)) {
                                        int shift = 0;
                                        int64_t tmp = imm;
                                        while (tmp > 1) { tmp >>= 1; shift++; }
                                        enc.shl_ri(x86::RAX, (uint8_t)shift);
                                    } else {
                                        enc.imul_ri(x86::RAX, (int32_t)imm);
                                    }
                                    invalidateRegCache();
                                    return ThavaType::INT;
                                }
                                if ((op == "/" || op == "//") && imm == 1) {
                                    invalidateRegCache();
                                    return ThavaType::INT;
                                }
                                if (imm == 0) {
                                    if (op == "==" || op == "!=" || op == "<" || op == "<=" || op == ">" || op == ">=") {
                                        enc.test_rr(x86::RAX, x86::RAX);
                                        if (op == "==") enc.setcc_to_rax(x86::Cond::E);
                                        else if (op == "!=") enc.setcc_to_rax(x86::Cond::NE);
                                        else if (op == "<")  enc.setcc_to_rax(x86::Cond::L);
                                        else if (op == "<=") enc.setcc_to_rax(x86::Cond::LE);
                                        else if (op == ">")  enc.setcc_to_rax(x86::Cond::G);
                                        else if (op == ">=") enc.setcc_to_rax(x86::Cond::GE);
                                        invalidateRegCache();
                                        return ThavaType::BOOL;
                                    }
                                } else {
                                    if (op == "==" || op == "!=" || op == "<" || op == "<=" || op == ">" || op == ">=") {
                                        enc.cmp_ri(x86::RAX, (int32_t)imm);
                                        if (op == "==") enc.setcc_to_rax(x86::Cond::E);
                                        else if (op == "!=") enc.setcc_to_rax(x86::Cond::NE);
                                        else if (op == "<")  enc.setcc_to_rax(x86::Cond::L);
                                        else if (op == "<=") enc.setcc_to_rax(x86::Cond::LE);
                                        else if (op == ">")  enc.setcc_to_rax(x86::Cond::G);
                                        else if (op == ">=") enc.setcc_to_rax(x86::Cond::GE);
                                        invalidateRegCache();
                                        return ThavaType::BOOL;
                                    }
                                }
                            }
                        }
                    }
                }

                // 2. Right operand is a local int variable (direct memory arithmetic)
                if (auto varR = dynamic_cast<VarNode*>(bin->right.get())) {
                    VarInfo* viR = lookupVar(varR->getName());
                    if (viR && viR->type == ThavaType::INT) {
                        if (op == "+" || op == "-" || op == "*") {
                            ThavaType lt = emitExpr(bin->left.get());
                            if (lt == ThavaType::INT) {
                                enc.mov_rm_rbp(x86::R10, viR->offset);
                                if (op == "+") enc.add_rr(x86::RAX, x86::R10);
                                else if (op == "-") enc.sub_rr(x86::RAX, x86::R10);
                                else if (op == "*") enc.imul_rr(x86::RAX, x86::R10);
                                invalidateRegCache();
                                return ThavaType::INT;
                            }
                        }
                    }
                }

                // 3. Commutative swap: if left is LiteralNode(INT) and right is not (e.g. 1 + x or 2 * x)
                if (auto litL = dynamic_cast<LiteralNode*>(bin->left.get())) {
                    if (litL->getLitType() == ThavaType::INT && (op == "+" || op == "*")) {
                        int64_t imm = 0;
                        try { imm = std::stoll(litL->getVal()); } catch (...) {}
                        ThavaType rt = emitExpr(bin->right.get());
                        if (rt == ThavaType::INT) {
                            if (op == "+") {
                                if (imm == 1) enc.inc_r(x86::RAX);
                                else if (imm != 0) enc.add_ri(x86::RAX, (int32_t)imm);
                            } else if (op == "*") {
                                if (imm == 0) enc.xor_r32(x86::RAX, x86::RAX);
                                else if (imm == 1) { /* no-op */ }
                                else if (imm == 2) enc.add_rr(x86::RAX, x86::RAX);
                                else if (imm > 0 && (imm & (imm - 1)) == 0 && imm <= (1LL << 30)) {
                                    int shift = 0;
                                    int64_t tmp = imm;
                                    while (tmp > 1) { tmp >>= 1; shift++; }
                                    enc.shl_ri(x86::RAX, (uint8_t)shift);
                                } else {
                                    enc.imul_ri(x86::RAX, (int32_t)imm);
                                }
                            }
                            invalidateRegCache();
                            return ThavaType::INT;
                        }
                    }
                }
            }

            int32_t savedOffset = currentFrameOffset;
            ThavaType lt = emitExpr(bin->left.get());

            currentFrameOffset -= 8;
            if (currentFrameOffset < maxFrameOffset) maxFrameOffset = currentFrameOffset;
            int32_t leftSlot = currentFrameOffset;

            if (lt == ThavaType::FLOAT || lt == ThavaType::DOUBLE) {
                enc.movsd_mr_rbp(leftSlot, x86::XMM0);
            } else {
                enc.mov_mr_rbp(leftSlot, x86::RAX);
            }

            ThavaType rt = emitExpr(bin->right.get());

            // String concat
            if (op == "+" && lt == ThavaType::STRING && rt == ThavaType::STRING) {
                enc.mov_rr(x86::RDX, x86::RAX);
                enc.mov_rm_rbp(x86::RCX, leftSlot);
                currentFrameOffset = savedOffset;
                enc.call_label(lblStrConcat);
                return ThavaType::STRING;
            }

            // String repetition: <str> * <int> or <int> * <str>
            if (op == "*") {
                if (lt == ThavaType::STRING && rt == ThavaType::INT) {
                    enc.mov_rm_rbp(x86::RCX, leftSlot); // str
                    enc.mov_rr(x86::RDX, x86::RAX);     // count
                    currentFrameOffset = savedOffset;
                    enc.call_label(lblStrMul);
                    return ThavaType::STRING;
                }
                if (lt == ThavaType::INT && rt == ThavaType::STRING) {
                    enc.mov_rr(x86::RCX, x86::RAX);     // str
                    enc.mov_rm_rbp(x86::RDX, leftSlot); // count
                    currentFrameOffset = savedOffset;
                    enc.call_label(lblStrMul);
                    return ThavaType::STRING;
                }
            }

            // String comparison
            if ((op == "==" || op == "!=") && lt == ThavaType::STRING && rt == ThavaType::STRING) {
                enc.mov_rr(x86::RDX, x86::RAX);
                enc.mov_rm_rbp(x86::RCX, leftSlot);
                currentFrameOffset = savedOffset;
                enc.call_label(lblStrEquals);
                if (op == "!=") {
                    enc.test_rr(x86::RAX, x86::RAX);
                    enc.setcc_to_rax(x86::Cond::E);
                }
                return ThavaType::BOOL;
            }

            // Guard against incompatible string operations
            if (lt == ThavaType::STRING || rt == ThavaType::STRING) {
                std::cerr << "[Native Codegen Error]: Operation '" << op << "' is invalid between <str> and other types!\n";
                exit(1);
            }

            // Float operations
            if (lt == ThavaType::DOUBLE || rt == ThavaType::DOUBLE || lt == ThavaType::FLOAT || rt == ThavaType::FLOAT) {
                if (rt == ThavaType::INT) enc.cvtsi2sd(x86::XMM0, x86::RAX);
                enc.movsd_rr(x86::XMM1, x86::XMM0);

                if (lt == ThavaType::INT) {
                    enc.mov_rm_rbp(x86::RAX, leftSlot);
                    enc.cvtsi2sd(x86::XMM0, x86::RAX);
                } else {
                    enc.movsd_rm_rbp(x86::XMM0, leftSlot);
                }
                currentFrameOffset = savedOffset;

                if (op == "+") { enc.addsd_rr(x86::XMM0, x86::XMM1); return ThavaType::DOUBLE; }
                if (op == "-") { enc.subsd_rr(x86::XMM0, x86::XMM1); return ThavaType::DOUBLE; }
                if (op == "*") { enc.mulsd_rr(x86::XMM0, x86::XMM1); return ThavaType::DOUBLE; }
                if (op == "/") { enc.divsd_rr(x86::XMM0, x86::XMM1); return ThavaType::DOUBLE; }
                if (op == "**") {
                    enc.call_iat_func("pow");
                    return ThavaType::DOUBLE;
                }
                if (op == "==" || op == "!=") {
                    enc.ucomisd_rr(x86::XMM0, x86::XMM1);
                    if (op == "==") enc.setcc_to_rax(x86::Cond::E);
                    else enc.setcc_to_rax(x86::Cond::NE);
                    return ThavaType::BOOL;
                }
                if (op == "<" || op == "<=" || op == ">" || op == ">=") {
                    enc.ucomisd_rr(x86::XMM0, x86::XMM1);
                    if (op == "<")  enc.setcc_to_rax(x86::Cond::B);
                    else if (op == "<=") enc.setcc_to_rax(x86::Cond::BE);
                    else if (op == ">")  enc.setcc_to_rax(x86::Cond::A);
                    else if (op == ">=") enc.setcc_to_rax(x86::Cond::AE);
                    return ThavaType::BOOL;
                }
            }

            // Integer operations
            enc.mov_rr(x86::R10, x86::RAX);
            enc.mov_rm_rbp(x86::RAX, leftSlot);
            currentFrameOffset = savedOffset;

            if (op == "+") { enc.add_rr(x86::RAX, x86::R10); return ThavaType::INT; }
            if (op == "-") { enc.sub_rr(x86::RAX, x86::R10); return ThavaType::INT; }
            if (op == "*") { enc.imul_rr(x86::RAX, x86::R10); return ThavaType::INT; }
            if (op == "//" || op == "/") {
                enc.cqo();
                enc.idiv_r(x86::R10);
                return ThavaType::INT;
            }
            if (op == "%") {
                enc.cqo();
                enc.idiv_r(x86::R10);
                enc.mov_rr(x86::RAX, x86::RDX);
                return ThavaType::INT;
            }
            if (op == "band") { enc.and_rr(x86::RAX, x86::R10); return ThavaType::INT; }
            if (op == "bor")  { enc.or_rr(x86::RAX, x86::R10); return ThavaType::INT; }
            if (op == "bxor") { enc.xor_rr(x86::RAX, x86::R10); return ThavaType::INT; }
            if (op == "bshl") {
                enc.mov_rr(x86::RCX, x86::R10);
                enc.shl_cl(x86::RAX);
                return ThavaType::INT;
            }
            if (op == "bshr") {
                enc.mov_rr(x86::RCX, x86::R10);
                enc.sar_cl(x86::RAX);
                return ThavaType::INT;
            }
            if (op == "**") {
                size_t powLoop = enc.createLabel();
                size_t powDone = enc.createLabel();
                size_t powNext = enc.createLabel();

                currentFrameOffset -= 8;
                if (currentFrameOffset < maxFrameOffset) maxFrameOffset = currentFrameOffset;
                int32_t baseSlot = currentFrameOffset;
                enc.mov_mr_rbp(baseSlot, x86::RAX);

                currentFrameOffset -= 8;
                if (currentFrameOffset < maxFrameOffset) maxFrameOffset = currentFrameOffset;
                int32_t expSlot = currentFrameOffset;
                enc.mov_mr_rbp(expSlot, x86::R10);

                currentFrameOffset -= 8;
                if (currentFrameOffset < maxFrameOffset) maxFrameOffset = currentFrameOffset;
                int32_t resSlot = currentFrameOffset;
                enc.mov_ri64(x86::RAX, 1);
                enc.mov_mr_rbp(resSlot, x86::RAX);

                enc.bindLabel(powLoop);
                enc.mov_rm_rbp(x86::R10, expSlot);
                enc.mov_ri64(x86::R11, 0);
                enc.cmp_rr(x86::R10, x86::R11);
                enc.jle(powDone);

                enc.mov_rm_rbp(x86::R10, expSlot);
                enc.mov_ri64(x86::R11, 1);
                enc.and_rr(x86::R10, x86::R11);
                enc.test_rr(x86::R10, x86::R10);
                enc.je(powNext);

                enc.mov_rm_rbp(x86::RAX, resSlot);
                enc.mov_rm_rbp(x86::R11, baseSlot);
                enc.imul_rr(x86::RAX, x86::R11);
                enc.mov_mr_rbp(resSlot, x86::RAX);

                enc.bindLabel(powNext);
                enc.mov_rm_rbp(x86::RAX, baseSlot);
                enc.mov_rm_rbp(x86::R11, baseSlot);
                enc.imul_rr(x86::RAX, x86::R11);
                enc.mov_mr_rbp(baseSlot, x86::RAX);

                enc.mov_rm_rbp(x86::RAX, expSlot);
                enc.shr_ri(x86::RAX, 1);
                enc.mov_mr_rbp(expSlot, x86::RAX);
                enc.jmp(powLoop);

                enc.bindLabel(powDone);
                enc.mov_rm_rbp(x86::RAX, resSlot);
                return ThavaType::INT;
            }
            if (op == "==") { enc.cmp_rr(x86::RAX, x86::R10); enc.setcc_to_rax(x86::Cond::E); return ThavaType::BOOL; }
            if (op == "!=") { enc.cmp_rr(x86::RAX, x86::R10); enc.setcc_to_rax(x86::Cond::NE); return ThavaType::BOOL; }
            if (op == "<")  { enc.cmp_rr(x86::RAX, x86::R10); enc.setcc_to_rax(x86::Cond::L); return ThavaType::BOOL; }
            if (op == "<=") { enc.cmp_rr(x86::RAX, x86::R10); enc.setcc_to_rax(x86::Cond::LE); return ThavaType::BOOL; }
            if (op == ">")  { enc.cmp_rr(x86::RAX, x86::R10); enc.setcc_to_rax(x86::Cond::G); return ThavaType::BOOL; }
            if (op == ">=") { enc.cmp_rr(x86::RAX, x86::R10); enc.setcc_to_rax(x86::Cond::GE); return ThavaType::BOOL; }
            if (op == "and") {
                enc.test_rr(x86::RAX, x86::RAX);
                enc.setcc_to_rax(x86::Cond::NE);
                enc.push(x86::RAX);
                enc.test_rr(x86::R10, x86::R10);
                enc.setcc_to_rax(x86::Cond::NE);
                enc.pop(x86::R10);
                enc.and_rr(x86::RAX, x86::R10);
                return ThavaType::BOOL;
            }
            if (op == "or") {
                enc.or_rr(x86::RAX, x86::R10);
                enc.test_rr(x86::RAX, x86::RAX);
                enc.setcc_to_rax(x86::Cond::NE);
                return ThavaType::BOOL;
            }
            return ThavaType::INT;
        }

        if (auto randCall = dynamic_cast<RandCallNode*>(node)) {
            if (randCall->funcName == "randres" && !randCall->args.empty()) {
                emitExpr(randCall->args[0].get());
                enc.mov_rr(x86::RCX, x86::RAX);
                enc.call_label(lblRandChoice);
                return ThavaType::STRING;
            } else if (randCall->funcName == "randint") {
                if (randCall->args.size() == 2) {
                    emitExpr(randCall->args[0].get());
                    currentFrameOffset -= 8;
                    if (currentFrameOffset < maxFrameOffset) maxFrameOffset = currentFrameOffset;
                    int32_t aSlot = currentFrameOffset;
                    enc.mov_mr_rbp(aSlot, x86::RAX);

                    emitExpr(randCall->args[1].get());
                    enc.mov_rm_rbp(x86::R10, aSlot);
                    enc.sub_rr(x86::RAX, x86::R10);
                    enc.mov_ri64(x86::R11, 1);
                    enc.add_rr(x86::RAX, x86::R11);
                    currentFrameOffset -= 8;
                    if (currentFrameOffset < maxFrameOffset) maxFrameOffset = currentFrameOffset;
                    int32_t rangeSlot = currentFrameOffset;
                    enc.mov_mr_rbp(rangeSlot, x86::RAX);

                    enc.call_iat_func("rand");
                    enc.mov_rm_rbp(x86::R10, rangeSlot);
                    enc.cqo();
                    enc.idiv_r(x86::R10);
                    enc.mov_rm_rbp(x86::RAX, aSlot);
                    enc.add_rr(x86::RAX, x86::RDX);
                    return ThavaType::INT;
                } else {
                    enc.call_iat_func("rand");
                    enc.mov_ri64(x86::R10, 11);
                    enc.cqo();
                    enc.idiv_r(x86::R10);
                    enc.mov_rr(x86::RAX, x86::RDX);
                    return ThavaType::INT;
                }
            } else { // rand()
                enc.call_iat_func("rand");
                enc.cvtsi2sd(x86::XMM0, x86::RAX);
                uint32_t cOff = pe.addDoubleLiteralOffset(std::is_same_v<TEmitter, ElfEmitter> ? 2147483647.0 : 32767.0);
                enc.movsd_rip_rdata(x86::XMM1, cOff);
                enc.divsd_rr(x86::XMM0, x86::XMM1);
                return ThavaType::DOUBLE;
            }
        }

        if (auto call = dynamic_cast<CallNode*>(node)) {
            // Check built-in functions
            if (call->name == "randres" && !call->args.empty()) {
                emitExpr(call->args[0].get());
                enc.mov_rr(x86::RCX, x86::RAX);
                enc.call_label(lblRandChoice);
                return ThavaType::STRING; // Or dynamic type
            }
            if (call->name == "len" && !call->args.empty()) {
                ThavaType argT = emitExpr(call->args[0].get());
                if (argT == ThavaType::STRING) {
                    enc.mov_rr(x86::RCX, x86::RAX);
                    enc.call_iat_func("strlen");
                } else {
                    enc.mov_rm(x86::RAX, x86::RAX, 0); // count
                }
                return ThavaType::INT;
            }

            // Built-in min()/max() — unless the program defines its own function with that name
            if ((call->name == "min" || call->name == "max") && functions.find(call->name) == functions.end()) {
                if (call->args.size() != 2) {
                    std::cerr << "[Native Codegen Error]: " << call->name
                              << "() expects exactly 2 arguments\n";
                    exit(1);
                }
                bool wantMax = (call->name == "max");

                ThavaType at = emitExpr(call->args[0].get());
                currentFrameOffset -= 8;
                if (currentFrameOffset < maxFrameOffset) maxFrameOffset = currentFrameOffset;
                int32_t aSlot = currentFrameOffset;
                bool aFloat = (at == ThavaType::DOUBLE || at == ThavaType::FLOAT);
                if (aFloat) enc.movsd_mr_rbp(aSlot, x86::XMM0);
                else enc.mov_mr_rbp(aSlot, x86::RAX);

                ThavaType bt = emitExpr(call->args[1].get());
                currentFrameOffset -= 8;
                if (currentFrameOffset < maxFrameOffset) maxFrameOffset = currentFrameOffset;
                int32_t bSlot = currentFrameOffset;
                bool bFloat = (bt == ThavaType::DOUBLE || bt == ThavaType::FLOAT);
                if (bFloat) enc.movsd_mr_rbp(bSlot, x86::XMM0);
                else enc.mov_mr_rbp(bSlot, x86::RAX);

                if (aFloat || bFloat) {
                    if (aFloat) enc.movsd_rm_rbp(x86::XMM0, aSlot);
                    else { enc.mov_rm_rbp(x86::RAX, aSlot); enc.cvtsi2sd(x86::XMM0, x86::RAX); }
                    if (bFloat) enc.movsd_rm_rbp(x86::XMM1, bSlot);
                    else { enc.mov_rm_rbp(x86::RAX, bSlot); enc.cvtsi2sd(x86::XMM1, x86::RAX); }
                    // MINSD xmm0, xmm1 (F2 0F 5D C1) / MAXSD xmm0, xmm1 (F2 0F 5F C1)
                    enc.emit8(0xF2);
                    enc.emit8(0x0F);
                    enc.emit8(wantMax ? 0x5F : 0x5D);
                    enc.emit8(0xC1);
                    return ThavaType::DOUBLE;
                }

                enc.mov_rm_rbp(x86::RAX, aSlot);
                enc.mov_rm_rbp(x86::R10, bSlot);
                enc.cmp_rr(x86::RAX, x86::R10);
                size_t keepLeft = enc.createLabel();
                if (wantMax) enc.jge(keepLeft);
                else enc.jle(keepLeft);
                enc.mov_rr(x86::RAX, x86::R10);
                enc.bindLabel(keepLeft);
                return ThavaType::INT;
            }

            auto it = functions.find(call->name);
            if (it == functions.end()) {
                auto dlit = g_dlFunctions.find(call->name);
                if (dlit != g_dlFunctions.end()) {
                    int32_t savedOffset = currentFrameOffset;
                    const auto& dlinfo = dlit->second;
                    std::vector<int32_t> argSlots;
                    for (size_t i = 0; i < call->args.size(); ++i) {
                        ThavaType argType = emitExpr(call->args[i].get());
                        ThavaType pt = (i < dlinfo.paramTypes.size()) ? dlinfo.paramTypes[i] : ThavaType::INT;
                        if ((pt == ThavaType::FLOAT || pt == ThavaType::DOUBLE) && argType == ThavaType::INT) {
                            enc.cvtsi2sd(x86::XMM0, x86::RAX);
                            argType = ThavaType::DOUBLE;
                        } else if (pt == ThavaType::INT && (argType == ThavaType::FLOAT || argType == ThavaType::DOUBLE)) {
                            enc.cvttsd2si(x86::RAX, x86::XMM0);
                            argType = ThavaType::INT;
                        }
                        currentFrameOffset -= 8;
                        if (currentFrameOffset < maxFrameOffset) maxFrameOffset = currentFrameOffset;
                        if (argType == ThavaType::FLOAT || argType == ThavaType::DOUBLE) {
                            enc.movsd_mr_rbp(currentFrameOffset, x86::XMM0);
                        } else {
                            enc.mov_mr_rbp(currentFrameOffset, x86::RAX);
                        }
                        argSlots.push_back(currentFrameOffset);
                    }

                    x86::Reg intRegs[] = { x86::RCX, x86::RDX, x86::R8, x86::R9 };
                    x86::Xmm xmmRegs[] = { x86::XMM0, x86::XMM1, x86::XMM2, x86::XMM3 };

                    for (size_t i = 0; i < argSlots.size() && i < 4; ++i) {
                        ThavaType pt = (i < dlinfo.paramTypes.size()) ? dlinfo.paramTypes[i] : ThavaType::INT;
                        if (pt == ThavaType::FLOAT || pt == ThavaType::DOUBLE) {
                            enc.movsd_rm_rbp(xmmRegs[i], argSlots[i]);
                            enc.mov_rm_rbp(intRegs[i], argSlots[i]);
                        } else {
                            enc.mov_rm_rbp(intRegs[i], argSlots[i]);
                        }
                    }

                    // Pass stack arguments for >= 4 args
                    for (size_t i = 4; i < argSlots.size(); ++i) {
                        int32_t stackDisp = 32 + (int32_t)(i - 4) * 8;
                        ThavaType pt = (i < dlinfo.paramTypes.size()) ? dlinfo.paramTypes[i] : ThavaType::INT;
                        if (pt == ThavaType::FLOAT || pt == ThavaType::DOUBLE) {
                            enc.movsd_rm_rbp(x86::XMM4, argSlots[i]);
                            enc.movsd_mr_rsp(stackDisp, x86::XMM4);
                        } else {
                            enc.mov_rm_rbp(x86::R10, argSlots[i]);
                            enc.mov_mr_rsp(stackDisp, x86::R10);
                        }
                    }

                    if constexpr (std::is_same_v<TEmitter, ElfEmitter>) {
                        // No system dynamic linker on this target: the runtime loader
                        // mapped the library at its own fixed base, so call straight
                        // into base + text + export offset.
                        if (!dlinfo.hasOffset) {
                            std::cerr << "[Thava Compiler] Error: dynamic library '" << dlinfo.libPath
                                      << "' exports no function offsets; rebuild it with --lib using this compiler.\n";
                            exit(1);
                        }
                        enc.mov_ri64(x86::RAX, (int64_t)dlLoadBaseForPath(dlinfo.libPath) +
                                              (int64_t)dlinfo.libTextRva + (int64_t)dlinfo.funcOffset);
                        enc.call_r(x86::RAX);
                    } else {
                        enc.call_iat_func(call->name);
                    }
                    currentFrameOffset = savedOffset;
                    return dlinfo.retType;
                }

                // Check if call->name is a function pointer / callback variable!
                VarInfo* vi = lookupVar(call->name);
                if (vi) {
                    int32_t savedOffset = currentFrameOffset;
                    std::vector<int32_t> argSlots;
                    for (size_t i = 0; i < call->args.size(); ++i) {
                        ThavaType argType = emitExpr(call->args[i].get());
                        currentFrameOffset -= 8;
                        if (currentFrameOffset < maxFrameOffset) maxFrameOffset = currentFrameOffset;
                        if (argType == ThavaType::FLOAT || argType == ThavaType::DOUBLE) {
                            enc.movsd_mr_rbp(currentFrameOffset, x86::XMM0);
                        } else {
                            enc.mov_mr_rbp(currentFrameOffset, x86::RAX);
                        }
                        argSlots.push_back(currentFrameOffset);
                    }

                    x86::Reg intRegs[] = { x86::RCX, x86::RDX, x86::R8, x86::R9 };
                    x86::Xmm xmmRegs[] = { x86::XMM0, x86::XMM1, x86::XMM2, x86::XMM3 };

                    for (size_t i = 0; i < argSlots.size() && i < 4; ++i) {
                        enc.movsd_rm_rbp(xmmRegs[i], argSlots[i]);
                        enc.mov_rm_rbp(intRegs[i], argSlots[i]);
                    }

                    for (size_t i = 4; i < argSlots.size(); ++i) {
                        int32_t stackDisp = 32 + (int32_t)(i - 4) * 8;
                        enc.mov_rm_rbp(x86::R10, argSlots[i]);
                        enc.mov_mr_rsp(stackDisp, x86::R10);
                    }

                    // Load function pointer from variable into R11 and call indirect
                    enc.mov_rm_rbp(x86::R11, vi->offset);
                    enc.call_r(x86::R11);
                    currentFrameOffset = savedOffset;
                    return ThavaType::INT;
                }

                std::cerr << "[Native Codegen Error]: Function or callback variable '" << call->name << "' is not defined!\n";
                exit(1);
            }
            const FuncInfo& fi = it->second;

            int32_t savedOffset = currentFrameOffset;
            std::vector<int32_t> argSlots;
            for (size_t i = 0; i < call->args.size(); ++i) {
                ThavaType argType = emitExpr(call->args[i].get());
                ThavaType pt = (i < fi.params.size()) ? fi.params[i].first : ThavaType::UNKNOWN;
                if ((pt == ThavaType::FLOAT || pt == ThavaType::DOUBLE) && argType == ThavaType::INT) {
                    enc.cvtsi2sd(x86::XMM0, x86::RAX);
                    argType = ThavaType::DOUBLE;
                } else if (pt == ThavaType::INT && (argType == ThavaType::FLOAT || argType == ThavaType::DOUBLE)) {
                    enc.cvttsd2si(x86::RAX, x86::XMM0);
                    argType = ThavaType::INT;
                }
                currentFrameOffset -= 8;
                if (currentFrameOffset < maxFrameOffset) maxFrameOffset = currentFrameOffset;
                if (argType == ThavaType::FLOAT || argType == ThavaType::DOUBLE) {
                    enc.movsd_mr_rbp(currentFrameOffset, x86::XMM0);
                } else {
                    enc.mov_mr_rbp(currentFrameOffset, x86::RAX);
                }
                argSlots.push_back(currentFrameOffset);
            }

            x86::Reg intRegs[] = { x86::RCX, x86::RDX, x86::R8, x86::R9 };
            x86::Xmm xmmRegs[] = { x86::XMM0, x86::XMM1, x86::XMM2, x86::XMM3 };

            // For a collection parameter, the caller passes the address of the
            // variable itself when the argument is a plain variable, so mutations
            // inside the callee reach the caller's variable.
            auto argAddressOf = [&](size_t i, x86::Reg dst) {
                VarInfo* av = nullptr;
                if (i < call->args.size()) {
                    if (auto vn = dynamic_cast<VarNode*>(call->args[i].get()))
                        av = lookupVar(vn->getName());
                }
                enc.lea_rm_rbp(dst, av ? av->offset : argSlots[i]);
            };

            for (size_t i = 0; i < argSlots.size() && i < 4; ++i) {
                ThavaType pt = (i < fi.params.size()) ? fi.params[i].first : ThavaType::INT;
                if (isCollectionType(pt)) {
                    argAddressOf(i, intRegs[i]);
                } else if (pt == ThavaType::FLOAT || pt == ThavaType::DOUBLE) {
                    enc.movsd_rm_rbp(xmmRegs[i], argSlots[i]);
                    enc.mov_rm_rbp(intRegs[i], argSlots[i]);
                } else {
                    enc.mov_rm_rbp(intRegs[i], argSlots[i]);
                }
            }

            // Pass stack arguments for >= 4 args
            for (size_t i = 4; i < argSlots.size(); ++i) {
                int32_t stackDisp = 32 + (int32_t)(i - 4) * 8;
                ThavaType pt = (i < fi.params.size()) ? fi.params[i].first : ThavaType::INT;
                if (isCollectionType(pt)) {
                    argAddressOf(i, x86::R10);
                    enc.mov_mr_rsp(stackDisp, x86::R10);
                } else if (pt == ThavaType::FLOAT || pt == ThavaType::DOUBLE) {
                    enc.movsd_rm_rbp(x86::XMM4, argSlots[i]);
                    enc.movsd_mr_rsp(stackDisp, x86::XMM4);
                } else {
                    enc.mov_rm_rbp(x86::R10, argSlots[i]);
                    enc.mov_mr_rsp(stackDisp, x86::R10);
                }
            }

            enc.call_label(fi.labelId);
            currentFrameOffset = savedOffset;
            return fi.returnType;
        }

        if (auto inp = dynamic_cast<InputNode*>(node)) {
            if (inp->inputType == ThavaType::STRING) {
                uint32_t promptOff = pe.addStringLiteralOffset(inp->prompt);
                enc.lea_rip_rdata(x86::RCX, promptOff);
                enc.mov_ri64(x86::RDX, 0); // mode = 0 (none)
                enc.call_label(lblInputLine);
                return ThavaType::STRING;
            }

            // Read a whole line for every non-string type. Using scanf("%d")
            // left invalid input (e.g. "abc") in the stream, which made the
            // next key.input read the same garbage and never consume a line.
            if (!inp->prompt.empty()) {
                uint32_t off = pe.addStringLiteralOffset(inp->prompt);
                enc.lea_rip_rdata(x86::RCX, off);
            } else {
                enc.xor_r32(x86::RCX, x86::RCX);
            }
            enc.mov_ri64(x86::RDX, inp->inputType == ThavaType::BOOL ? 1 : 0);
            enc.call_label(lblInputLine);

            currentFrameOffset -= 8;
            if (currentFrameOffset < maxFrameOffset) maxFrameOffset = currentFrameOffset;
            int32_t scratchSlot = currentFrameOffset;
            enc.mov_mr_rbp(scratchSlot, x86::RAX); // line buffer

            if (inp->inputType == ThavaType::INT) {
                enc.mov_rr(x86::RCX, x86::RAX);
                enc.call_iat_func("_atoi64");
                return ThavaType::INT;
            } else if (inp->inputType == ThavaType::DOUBLE || inp->inputType == ThavaType::FLOAT) {
                enc.mov_rr(x86::RCX, x86::RAX);
                enc.call_iat_func("atof");
                return inp->inputType;
            } else if (inp->inputType == ThavaType::BOOL) {
                // The line was lowercased by lblInputLine (mode 1), so plain
                // strcmp against "true"/"yes" plus a numeric fallback is enough.
                size_t isTrue = enc.createLabel();
                size_t boolDone = enc.createLabel();
                enc.mov_rm_rbp(x86::RCX, scratchSlot);
                enc.lea_rip_rdata(x86::RDX, pe.addStringLiteralOffset("true"));
                enc.call_iat_func("strcmp");
                enc.test_rr(x86::RAX, x86::RAX);
                enc.je(isTrue);
                enc.mov_rm_rbp(x86::RCX, scratchSlot);
                enc.lea_rip_rdata(x86::RDX, pe.addStringLiteralOffset("yes"));
                enc.call_iat_func("strcmp");
                enc.test_rr(x86::RAX, x86::RAX);
                enc.je(isTrue);
                enc.mov_rm_rbp(x86::RCX, scratchSlot);
                enc.call_iat_func("_atoi64");
                enc.test_rr(x86::RAX, x86::RAX);
                enc.jne(isTrue);
                enc.mov_ri64(x86::RAX, 0);
                enc.jmp(boolDone);
                enc.bindLabel(isTrue);
                enc.mov_ri64(x86::RAX, 1);
                enc.bindLabel(boolDone);
                return ThavaType::BOOL;
            }
            return ThavaType::INT;
        }

        if (auto idxNode = dynamic_cast<IndexNode*>(node)) {
            ThavaType baseT = emitExpr(idxNode->base.get());
            currentFrameOffset -= 8;
            if (currentFrameOffset < maxFrameOffset) maxFrameOffset = currentFrameOffset;
            int32_t baseSlot = currentFrameOffset;
            enc.mov_mr_rbp(baseSlot, x86::RAX);

            if (baseT == ThavaType::STRING) {
                emitExpr(idxNode->indexExpr.get()); // idx in RAX (0-based)
                enc.mov_ri64(x86::R10, 1);
                enc.add_rr(x86::RAX, x86::R10); // 1-based start
                enc.mov_rr(x86::RDX, x86::RAX);
                enc.mov_rr(x86::R8, x86::RAX);  // 1-based end
                enc.mov_rm_rbp(x86::RCX, baseSlot);
                enc.call_label(lblStrSubstring);
                return ThavaType::STRING;
            }

            if (baseT == ThavaType::DICT) {
                ThavaType kt = emitExpr(idxNode->indexExpr.get());
                currentFrameOffset -= 8;
                if (currentFrameOffset < maxFrameOffset) maxFrameOffset = currentFrameOffset;
                int32_t keySlot = currentFrameOffset;
                enc.mov_mr_rbp(keySlot, x86::RAX);

                size_t searchLoop = enc.createLabel();
                size_t foundLbl = enc.createLabel();
                size_t notFoundLbl = enc.createLabel();
                size_t endIdx = enc.createLabel();

                currentFrameOffset -= 8;
                if (currentFrameOffset < maxFrameOffset) maxFrameOffset = currentFrameOffset;
                int32_t iSlot = currentFrameOffset;
                enc.mov_ri64(x86::RAX, 0);
                enc.mov_mr_rbp(iSlot, x86::RAX);

                enc.bindLabel(searchLoop);
                enc.mov_rm_rbp(x86::RAX, iSlot);
                enc.mov_rm_rbp(x86::R10, baseSlot);
                enc.mov_rm(x86::R11, x86::R10, 0);
                enc.cmp_rr(x86::RAX, x86::R11);
                enc.jge(notFoundLbl);

                enc.mov_ri64(x86::R11, 32);
                enc.imul_rr(x86::RAX, x86::R11);
                enc.add_rr(x86::R10, x86::RAX);
                if (kt == ThavaType::STRING) {
                    enc.mov_rm(x86::RCX, x86::R10, 16);
                    enc.mov_rm_rbp(x86::RDX, keySlot);
                    enc.call_iat_func("strcmp");
                    enc.test_rr(x86::RAX, x86::RAX);
                    enc.je(foundLbl);
                } else {
                    enc.mov_rm(x86::RAX, x86::R10, 16);
                    enc.mov_rm_rbp(x86::R11, keySlot);
                    enc.cmp_rr(x86::RAX, x86::R11);
                    enc.je(foundLbl);
                }

                enc.mov_rm_rbp(x86::RAX, iSlot);
                enc.mov_ri64(x86::R11, 1);
                enc.add_rr(x86::RAX, x86::R11);
                enc.mov_mr_rbp(iSlot, x86::RAX);
                enc.jmp(searchLoop);

                enc.bindLabel(foundLbl);
                enc.mov_rm_rbp(x86::RAX, iSlot);
                enc.mov_ri64(x86::R11, 32);
                enc.imul_rr(x86::RAX, x86::R11);
                enc.mov_rm_rbp(x86::R10, baseSlot);
                enc.add_rr(x86::R10, x86::RAX);
                enc.mov_rm(x86::RAX, x86::R10, 24);
                enc.jmp(endIdx);

                enc.bindLabel(notFoundLbl);
                enc.mov_ri64(x86::RAX, 0);

                enc.bindLabel(endIdx);
                return ThavaType::INT;
            }

            ThavaType elemType = elemTypeOfIndexedList(idxNode->base.get());
            if (elemType == ThavaType::UNKNOWN) elemType = ThavaType::INT;

            emitExpr(idxNode->indexExpr.get()); // idx in RAX
            enc.mov_rm_rbp(x86::R10, baseSlot); // base ptr in R10
            enc.mov_ri64(x86::R11, 8);
            enc.imul_rr(x86::RAX, x86::R11);   // idx * 8
            enc.add_rr(x86::R10, x86::RAX);     // base + idx * 8
            if (elemType == ThavaType::FLOAT || elemType == ThavaType::DOUBLE) {
                enc.movsd_rm(x86::XMM0, x86::R10, 8);
            } else {
                enc.mov_rm(x86::RAX, x86::R10, 8);  // element at offset 8 (after length qword)
            }
            return elemType;
        }

        if (auto lenNode = dynamic_cast<LenNode*>(node)) {
            ThavaType argT = emitExpr(lenNode->arg.get());
            if (argT == ThavaType::STRING) {
                enc.mov_rr(x86::RCX, x86::RAX);
                enc.call_iat_func("strlen");
            } else {
                enc.mov_rm(x86::RAX, x86::RAX, 0); // count is at offset 0
            }
            return ThavaType::INT;
        }

        if (auto fm = dynamic_cast<FileMethodNode*>(node)) {
            return emitFileMethod(fm);
        }

        if (auto lm = dynamic_cast<ListMethodNode*>(node)) {
            return emitListMethod(lm);
        }

        if (auto osNode = dynamic_cast<OsMethodNode*>(node)) {
            return emitOsMethod(osNode);
        }

        if (auto timeNode = dynamic_cast<TimeMethodNode*>(node)) {
            return emitTimeMethod(timeNode);
        }

        if (auto clibNode = dynamic_cast<CLibMethodNode*>(node)) {
            return emitCLibMethod(clibNode);
        }

        // --- LocVar / ParVar: the current object / the parent sub-object ---
        if (dynamic_cast<LocVarNode*>(node) || dynamic_cast<ParVarNode*>(node)) {
            VarInfo* thi = lookupVar("@this");
            if (!thi) {
                std::cerr << "[Native Codegen Error]: 'locvar'/'parvar' used outside a class method.\n";
                exit(1);
            }
            enc.mov_rm_rbp(x86::RAX, thi->offset);
            return ThavaType::CUSTOM;
        }

        // --- ClassInstantiateNode: ClassName(args...) ---
        if (auto ci = dynamic_cast<ClassInstantiateNode*>(node)) {
            auto lit = classLayouts.find(ci->className);
            if (lit == classLayouts.end()) {
                std::cerr << "[Native Codegen Error]: Class '" << ci->className << "' is not declared!\n";
                exit(1);
            }
            int32_t size = lit->second.totalSize > 0 ? lit->second.totalSize : 8;

            enc.mov_ri64(x86::RCX, size);
            enc.call_iat_func("malloc");
            currentFrameOffset -= 8;
            if (currentFrameOffset < maxFrameOffset) maxFrameOffset = currentFrameOffset;
            int32_t ptrSlot = currentFrameOffset;
            enc.mov_mr_rbp(ptrSlot, x86::RAX);

            // Zero the block so unset fields read as 0
            enc.mov_rr(x86::RCX, x86::RAX);
            enc.xor_r32(x86::RDX, x86::RDX);
            enc.mov_ri64(x86::R8, size);
            enc.call_iat_func("memset");

            // Field default values, base class first
            std::vector<std::string> chain;
            for (std::string c = ci->className; !c.empty();) {
                chain.push_back(c);
                auto b = classBase.find(c);
                c = (b != classBase.end()) ? b->second : std::string();
            }
            for (int ci2 = (int)chain.size() - 1; ci2 >= 0; --ci2) {
                auto dit = classFieldDefaults.find(chain[ci2]);
                if (dit == classFieldDefaults.end()) continue;
                auto lay = classLayouts.find(chain[ci2]);
                for (auto& d : dit->second) {
                    const StructFieldInfo* fld = nullptr;
                    if (lay != classLayouts.end()) {
                        for (const auto& f : lay->second.fields)
                            if (f.offset == d.first) { fld = &f; break; }
                    }
                    if (!fld) continue;
                    ThavaType dt = emitExpr(d.second);
                    enc.mov_rm_rbp(x86::R10, ptrSlot);
                    if (fld->type == ThavaType::DOUBLE) {
                        if (dt == ThavaType::INT) enc.cvtsi2sd(x86::XMM0, x86::RAX);
                        enc.movsd_mr(x86::R10, fld->offset, x86::XMM0);
                    } else if (fld->type == ThavaType::FLOAT) {
                        if (dt == ThavaType::INT) enc.cvtsi2sd(x86::XMM0, x86::RAX);
                        enc.cvtsd2ss(x86::XMM0, x86::XMM0);
                        enc.movss_mr(x86::R10, fld->offset, x86::XMM0);
                    } else {
                        if (fld->type == ThavaType::INT) narrowIntReg(x86::RAX, fld->customType);
                        enc.mov_mr(x86::R10, fld->offset, x86::RAX);
                    }
                }
            }

            // Constructor call (both 'func init' and 'func ClassName' map here)
            ThavaType ctorRet = ThavaType::VOID;
            std::string ctorKey = ci->className + "::init";
            size_t ctorLbl = findMethodLabel(ci->className, "init", &ctorRet);
            std::vector<std::pair<ThavaType, std::string>> ctorParams;
            auto cpit = classMethodParams.find(ctorKey);
            if (cpit != classMethodParams.end()) ctorParams = cpit->second;

            if (ctorLbl != 0 && ctorParams.size() == ci->args.size()) {
                std::vector<int32_t> argSlots;
                std::vector<ThavaType> argTypes;
                for (auto& a : ci->args) {
                    ThavaType at = emitExpr(a.get());
                    currentFrameOffset -= 8;
                    if (currentFrameOffset < maxFrameOffset) maxFrameOffset = currentFrameOffset;
                    int32_t slot = currentFrameOffset;
                    if (at == ThavaType::FLOAT || at == ThavaType::DOUBLE) enc.movsd_mr_rbp(slot, x86::XMM0);
                    else enc.mov_mr_rbp(slot, x86::RAX);
                    argSlots.push_back(slot);
                    argTypes.push_back(at);
                }
                emitClassCallArgs(ctorLbl, ctorKey, ci->args, ptrSlot, argSlots, argTypes);
            }

            enc.mov_rm_rbp(x86::RAX, ptrSlot);
            return ThavaType::CUSTOM;
        }

        // --- MethodCallNode on a class instance: obj.method(args...) ---
        if (auto mc = dynamic_cast<MethodCallNode*>(node)) {
            std::string cls = structNameOfTarget(mc->target.get());
            if (classLayouts.count(cls)) {
                ThavaType ret = ThavaType::VOID;
                std::string key = cls + "::" + mc->methodName;
                size_t lbl = findMethodLabel(cls, mc->methodName, &ret);
                if (lbl == 0) {
                    std::cerr << "[Native Codegen Error]: class '" << cls << "' has no method '"
                              << mc->methodName << "'.\n";
                    exit(1);
                }
                emitExpr(mc->target.get());
                currentFrameOffset -= 8;
                if (currentFrameOffset < maxFrameOffset) maxFrameOffset = currentFrameOffset;
                int32_t thisSlot = currentFrameOffset;
                enc.mov_mr_rbp(thisSlot, x86::RAX);

                std::vector<int32_t> argSlots;
                std::vector<ThavaType> argTypes;
                for (auto& a : mc->args) {
                    ThavaType at = emitExpr(a.get());
                    currentFrameOffset -= 8;
                    if (currentFrameOffset < maxFrameOffset) maxFrameOffset = currentFrameOffset;
                    int32_t slot = currentFrameOffset;
                    if (at == ThavaType::FLOAT || at == ThavaType::DOUBLE) enc.movsd_mr_rbp(slot, x86::XMM0);
                    else enc.mov_mr_rbp(slot, x86::RAX);
                    argSlots.push_back(slot);
                    argTypes.push_back(at);
                }
                emitClassCallArgs(lbl, key, mc->args, thisSlot, argSlots, argTypes);
                if (ret == ThavaType::INT) narrowIntReg(x86::RAX, currentFuncRetWidth);
                return ret;
            }
            std::cerr << "[Native Codegen Error]: cannot resolve the class of method call '"
                      << mc->methodName << "'.\n";
            exit(1);
        }

        // --- MemberAccessNode (Read struct/class field: obj.field) ---
        if (auto mem = dynamic_cast<MemberAccessNode*>(node)) {
            std::string structName = structNameOfTarget(mem->target.get());

            auto sit = g_structs.find(structName);
            if (sit == g_structs.end()) {
                if (auto v = dynamic_cast<VarNode*>(mem->target.get())) {
                    auto eit = g_enums.find(v->getName());
                    if (eit != g_enums.end()) {
                        auto mit = eit->second.members.find(mem->memberName);
                        if (mit != eit->second.members.end()) {
                            enc.mov_ri64(x86::RAX, mit->second);
                            return ThavaType::INT;
                        }
                    }
                }
                std::cerr << "[Native Codegen Error]: Struct or enum not found for '" << mem->memberName << "'!\n";
                exit(1);
            }

            const auto& sinfo = sit->second;
            auto fit = sinfo.fieldIndex.find(mem->memberName);
            if (fit == sinfo.fieldIndex.end()) {
                std::cerr << "[Native Codegen Error]: Struct '" << structName << "' has no member '" << mem->memberName << "'!\n";
                exit(1);
            }
            const auto& fld = sinfo.fields[fit->second];

            emitExpr(mem->target.get());
            if (fld.type == ThavaType::DOUBLE) {
                enc.movsd_rm(x86::XMM0, x86::RAX, fld.offset);
                return ThavaType::DOUBLE;
            } else if (fld.type == ThavaType::FLOAT) {
                enc.movss_rm(x86::XMM0, x86::RAX, fld.offset);
                enc.cvtss2sd(x86::XMM0, x86::XMM0);
                return ThavaType::FLOAT;
            } else {
                enc.mov_rm(x86::RAX, x86::RAX, fld.offset);
                return fld.type;
            }
        }

        // --- StructInstantiateNode (Struct instantiation: StructName(args...)) ---
        if (auto sinst = dynamic_cast<StructInstantiateNode*>(node)) {
            auto sit = g_structs.find(sinst->structName);
            if (sit == g_structs.end()) {
                std::cerr << "[Native Codegen Error]: Struct not found '" << sinst->structName << "'!\n";
                exit(1);
            }
            const auto& sinfo = sit->second;

            // 1. Evaluate constructor arguments first
            std::vector<int32_t> argSlots;
            std::vector<ThavaType> argTypes;
            for (size_t i = 0; i < sinst->args.size() && i < sinfo.fields.size(); ++i) {
                ThavaType at = emitExpr(sinst->args[i].get());
                currentFrameOffset -= 8;
                if (currentFrameOffset < maxFrameOffset) maxFrameOffset = currentFrameOffset;
                int32_t slot = currentFrameOffset;
                if (at == ThavaType::FLOAT || at == ThavaType::DOUBLE) {
                    enc.movsd_mr_rbp(slot, x86::XMM0);
                } else {
                    enc.mov_mr_rbp(slot, x86::RAX);
                }
                argSlots.push_back(slot);
                argTypes.push_back(at);
            }

            // 2. Allocate heap memory for struct via malloc
            enc.mov_ri64(x86::RCX, sinfo.totalSize);
            enc.call_iat_func("malloc");
            currentFrameOffset -= 8;
            if (currentFrameOffset < maxFrameOffset) maxFrameOffset = currentFrameOffset;
            int32_t ptrSlot = currentFrameOffset;
            enc.mov_mr_rbp(ptrSlot, x86::RAX);

            // 3. Zero out struct memory
            enc.mov_rr(x86::RCX, x86::RAX);
            enc.xor_r32(x86::RDX, x86::RDX);
            enc.mov_ri64(x86::R8, sinfo.totalSize);
            enc.call_iat_func("memset");

            // 4. Assign initial values to fields
            for (size_t i = 0; i < argSlots.size(); ++i) {
                const auto& f = sinfo.fields[i];
                enc.mov_rm_rbp(x86::RCX, ptrSlot);
                if (f.type == ThavaType::FLOAT) {
                    enc.movsd_rm_rbp(x86::XMM0, argSlots[i]);
                    if (argTypes[i] == ThavaType::INT) enc.cvtsi2sd(x86::XMM0, x86::RAX);
                    enc.cvtsd2ss(x86::XMM0, x86::XMM0);
                    enc.movss_mr(x86::RCX, f.offset, x86::XMM0);
                } else if (f.type == ThavaType::DOUBLE) {
                    enc.movsd_rm_rbp(x86::XMM0, argSlots[i]);
                    if (argTypes[i] == ThavaType::INT) enc.cvtsi2sd(x86::XMM0, x86::RAX);
                    enc.movsd_mr(x86::RCX, f.offset, x86::XMM0);
                } else {
                    enc.mov_rm_rbp(x86::RAX, argSlots[i]);
                    if (f.type == ThavaType::INT) narrowIntReg(x86::RAX, f.customType);
                    enc.mov_mr(x86::RCX, f.offset, x86::RAX);
                }
            }

            enc.mov_rm_rbp(x86::RAX, ptrSlot);
            return ThavaType::CUSTOM;
        }

        return ThavaType::UNKNOWN;
    }

    ThavaType emitFileMethod(FileMethodNode* fm) {
        VarInfo* vi = lookupVar(fm->varName);
        if (!vi) {
            std::cerr << "[Native Codegen Error]: File variable '" << fm->varName << "' is not declared!\n";
            exit(1);
        }
        int32_t fileVarOffset = vi->offset;
        std::string m = fm->method;

        if (m == "write") {
            if (fm->args.empty()) return ThavaType::VOID;
            ThavaType t = emitExpr(fm->args[0].get());

            if (t == ThavaType::STRING) {
                enc.mov_rr(x86::RCX, x86::RAX);          // str
                enc.mov_rm_rbp(x86::RDX, fileVarOffset); // file
                enc.call_iat_func("fputs");
            } else if (t == ThavaType::INT) {
                currentFrameOffset -= 8;
                if (currentFrameOffset < maxFrameOffset) maxFrameOffset = currentFrameOffset;
                int32_t valSlot = currentFrameOffset;
                enc.mov_mr_rbp(valSlot, x86::RAX);

                currentFrameOffset -= 64;
                if (currentFrameOffset < maxFrameOffset) maxFrameOffset = currentFrameOffset;
                int32_t bufSlot = currentFrameOffset;

                enc.lea_rm_rbp(x86::RCX, bufSlot);
                enc.lea_rip_rdata(x86::RDX, offFmtInt);
                enc.mov_rm_rbp(x86::R8, valSlot);
                enc.call_iat_func("sprintf");

                enc.lea_rm_rbp(x86::RCX, bufSlot);
                enc.mov_rm_rbp(x86::RDX, fileVarOffset);
                enc.call_iat_func("fputs");
            } else if (t == ThavaType::FLOAT || t == ThavaType::DOUBLE) {
                currentFrameOffset -= 8;
                if (currentFrameOffset < maxFrameOffset) maxFrameOffset = currentFrameOffset;
                int32_t valSlot = currentFrameOffset;
                enc.movsd_mr_rbp(valSlot, x86::XMM0);

                currentFrameOffset -= 64;
                if (currentFrameOffset < maxFrameOffset) maxFrameOffset = currentFrameOffset;
                int32_t bufSlot = currentFrameOffset;

                enc.lea_rm_rbp(x86::RCX, bufSlot);
                if constexpr (std::is_same_v<TEmitter, ElfEmitter>) {
                    // The zero-libc Linux runtime has no %g formatting.
                    enc.mov_rm_rbp(x86::R8, valSlot);
                    enc.call_iat_func("sprintf_d");
                } else {
                    enc.lea_rip_rdata(x86::RDX, offFmtFloat6);
                    enc.movsd_rm_rbp(x86::XMM2, valSlot);
                    enc.mov_rm_rbp(x86::R8, valSlot);
                    enc.call_iat_func("sprintf");
                    enc.lea_rm_rbp(x86::RDI, bufSlot);
                    emitTrimTrailingZeros(x86::RDI);
                }

                enc.lea_rm_rbp(x86::RCX, bufSlot);
                enc.mov_rm_rbp(x86::RDX, fileVarOffset);
                enc.call_iat_func("fputs");
            } else if (t == ThavaType::BOOL) {
                size_t trueLbl = enc.createLabel();
                size_t endLbl = enc.createLabel();
                enc.test_rr(x86::RAX, x86::RAX);
                enc.jne(trueLbl);
                enc.lea_rip_rdata(x86::RCX, offStrFalse);
                enc.jmp(endLbl);
                enc.bindLabel(trueLbl);
                enc.lea_rip_rdata(x86::RCX, offStrTrue);
                enc.bindLabel(endLbl);
                enc.mov_rm_rbp(x86::RDX, fileVarOffset);
                enc.call_iat_func("fputs");
            }
            return ThavaType::VOID;
        }

        if (m == "write_line" || m == "writeline") {
            if (!fm->args.empty()) {
                ThavaType t = emitExpr(fm->args[0].get());

                if (t == ThavaType::STRING) {
                    enc.mov_rr(x86::RCX, x86::RAX);          // str
                    enc.mov_rm_rbp(x86::RDX, fileVarOffset); // file
                    enc.call_iat_func("fputs");
                } else if (t == ThavaType::INT) {
                    currentFrameOffset -= 8;
                    if (currentFrameOffset < maxFrameOffset) maxFrameOffset = currentFrameOffset;
                    int32_t valSlot = currentFrameOffset;
                    enc.mov_mr_rbp(valSlot, x86::RAX);

                    currentFrameOffset -= 64;
                    if (currentFrameOffset < maxFrameOffset) maxFrameOffset = currentFrameOffset;
                    int32_t bufSlot = currentFrameOffset;

                    enc.lea_rm_rbp(x86::RCX, bufSlot);
                    enc.lea_rip_rdata(x86::RDX, offFmtInt);
                    enc.mov_rm_rbp(x86::R8, valSlot);
                    enc.call_iat_func("sprintf");

                    enc.lea_rm_rbp(x86::RCX, bufSlot);
                    enc.mov_rm_rbp(x86::RDX, fileVarOffset);
                    enc.call_iat_func("fputs");
                } else if (t == ThavaType::FLOAT || t == ThavaType::DOUBLE) {
                    currentFrameOffset -= 8;
                    if (currentFrameOffset < maxFrameOffset) maxFrameOffset = currentFrameOffset;
                    int32_t valSlot = currentFrameOffset;
                    enc.movsd_mr_rbp(valSlot, x86::XMM0);

                    currentFrameOffset -= 64;
                    if (currentFrameOffset < maxFrameOffset) maxFrameOffset = currentFrameOffset;
                    int32_t bufSlot = currentFrameOffset;

                    enc.lea_rm_rbp(x86::RCX, bufSlot);
                    if constexpr (std::is_same_v<TEmitter, ElfEmitter>) {
                        enc.mov_rm_rbp(x86::R8, valSlot);
                        enc.call_iat_func("sprintf_d");
                    } else {
                        enc.lea_rip_rdata(x86::RDX, offFmtFloat6);
                        enc.movsd_rm_rbp(x86::XMM2, valSlot);
                        enc.mov_rm_rbp(x86::R8, valSlot);
                        enc.call_iat_func("sprintf");
                        enc.lea_rm_rbp(x86::RDI, bufSlot);
                        emitTrimTrailingZeros(x86::RDI);
                    }

                    enc.lea_rm_rbp(x86::RCX, bufSlot);
                    enc.mov_rm_rbp(x86::RDX, fileVarOffset);
                    enc.call_iat_func("fputs");
                } else if (t == ThavaType::BOOL) {
                    size_t trueLbl = enc.createLabel();
                    size_t endLbl = enc.createLabel();
                    enc.test_rr(x86::RAX, x86::RAX);
                    enc.jne(trueLbl);
                    enc.lea_rip_rdata(x86::RCX, offStrFalse);
                    enc.jmp(endLbl);
                    enc.bindLabel(trueLbl);
                    enc.lea_rip_rdata(x86::RCX, offStrTrue);
                    enc.bindLabel(endLbl);
                    enc.mov_rm_rbp(x86::RDX, fileVarOffset);
                    enc.call_iat_func("fputs");
                }
            }
            // Newline "\n"
            enc.lea_rip_rdata(x86::RCX, offFmtNL);
            enc.mov_rm_rbp(x86::RDX, fileVarOffset);
            enc.call_iat_func("fputs");
            return ThavaType::VOID;
        }

        if (m == "read") {
            enc.mov_rm_rbp(x86::RCX, fileVarOffset);
            enc.call_label(lblFileReadAll);
            return ThavaType::STRING;
        }

        if (m == "read_line" || m == "readline") {
            enc.mov_rm_rbp(x86::RCX, fileVarOffset);
            enc.call_label(lblFileReadLine);
            return ThavaType::STRING;
        }

        if (m == "read_lines" || m == "readlines") {
            enc.mov_rm_rbp(x86::RCX, fileVarOffset);
            enc.call_label(lblFileReadLines);
            return ThavaType::LIST;
        }

        return ThavaType::VOID;
    }

    ThavaType emitListMethod(ListMethodNode* lm) {
        if (!lookupVar(lm->varName) && isGlobalName(lm->varName) &&
            (lm->method != "addval" || lm->args.size() != 1)) {
            std::cerr << "[Codegen Error]: Unsupported native global list operation: " << lm->method << "\n";
            exit(1);
        }
        if (lm->method == "addval") {
            VarInfo* vi = lookupVar(lm->varName);
            if (!vi && !isGlobalName(lm->varName)) {
                std::cerr << "[Native Codegen Error]: List '" << lm->varName << "' is not declared!\n";
                exit(1);
            }

            // Evaluate valExpr (args[0] if 1 arg, args[1] if 2 args)
            ASTNode* valExpr = lm->args.size() == 1 ? lm->args[0].get() : lm->args[1].get();
            ThavaType vt = emitExpr(valExpr);
            currentFrameOffset -= 8;
            if (currentFrameOffset < maxFrameOffset) maxFrameOffset = currentFrameOffset;
            int32_t valSlot = currentFrameOffset;
            if (vt == ThavaType::FLOAT || vt == ThavaType::DOUBLE) {
                enc.movsd_mr_rbp(valSlot, x86::XMM0);
                listElemTypes[lm->varName] = ThavaType::DOUBLE;
            } else {
                enc.mov_mr_rbp(valSlot, x86::RAX);
            }

            // Load old list ptr
            loadCollectionPtr(lm->varName, vi, x86::R10); // oldPtr
            // old count n = [R10 + 0]
            enc.mov_rm(x86::R11, x86::R10, 0);   // n
            currentFrameOffset -= 8;
            if (currentFrameOffset < maxFrameOffset) maxFrameOffset = currentFrameOffset;
            int32_t countSlot = currentFrameOffset;
            enc.mov_mr_rbp(countSlot, x86::R11);

            // newCount = n + 1
            // newBytes = (newCount + 1) * 8 = (n + 2) * 8
            enc.mov_rr(x86::RCX, x86::R11);
            enc.mov_ri64(x86::RAX, 2);
            enc.add_rr(x86::RCX, x86::RAX);
            enc.mov_ri64(x86::RAX, 8);
            enc.imul_rr(x86::RCX, x86::RAX);
            enc.call_iat_func("malloc"); // RAX = newPtr

            currentFrameOffset -= 8;
            if (currentFrameOffset < maxFrameOffset) maxFrameOffset = currentFrameOffset;
            int32_t newPtrSlot = currentFrameOffset;
            enc.mov_mr_rbp(newPtrSlot, x86::RAX);

            // newPtr[0] = n + 1
            enc.mov_rm_rbp(x86::R11, countSlot);
            enc.inc_r(x86::R11);
            enc.mov_mr(x86::RAX, 0, x86::R11);

            // Copy elements: 0 .. n - 1 from oldPtr to newPtr
            // if n > 0: memcpy(newPtr + 8, oldPtr + 8, n * 8)
            enc.mov_rm_rbp(x86::R11, countSlot);
            enc.test_rr(x86::R11, x86::R11);
            size_t noCopy = enc.createLabel();
            enc.jle(noCopy);

            enc.mov_rm_rbp(x86::RCX, newPtrSlot);
            enc.mov_ri64(x86::RAX, 8);
            enc.add_rr(x86::RCX, x86::RAX); // dst = newPtr + 8

            loadCollectionPtr(lm->varName, vi, x86::RDX);
            enc.add_rr(x86::RDX, x86::RAX); // src = oldPtr + 8

            enc.mov_rm_rbp(x86::R8, countSlot);
            enc.imul_rr(x86::R8, x86::RAX); // count * 8 bytes
            enc.call_iat_func("memcpy");

            enc.bindLabel(noCopy);
            // newPtr[1 + n] = val
            enc.mov_rm_rbp(x86::R10, newPtrSlot);
            enc.mov_rm_rbp(x86::R11, countSlot);
            enc.mov_ri64(x86::RAX, 8);
            enc.imul_rr(x86::R11, x86::RAX);
            enc.add_rr(x86::R10, x86::R11);
            enc.mov_rm_rbp(x86::RAX, valSlot);
            enc.mov_mr(x86::R10, 8, x86::RAX);

            // update list variable
            enc.mov_rm_rbp(x86::RAX, newPtrSlot);
            if (vi) enc.mov_mr_rbp(vi->offset, x86::RAX);
            else {
                emitGlobalBase(x86::R11, globalVarSlots.at(lm->varName));
                enc.mov_mr(x86::R11, 0, x86::RAX);
            }
            return ThavaType::INT;
        }
        return ThavaType::VOID;
    }

    void emitFileOpenBlock(FileOpenBlockNode* fileBlock) {
        // 1. Evaluate pathExpr
        emitExpr(fileBlock->pathExpr.get());
        currentFrameOffset -= 8;
        if (currentFrameOffset < maxFrameOffset) maxFrameOffset = currentFrameOffset;
        int32_t pathSlot = currentFrameOffset;
        enc.mov_mr_rbp(pathSlot, x86::RAX);

        // 2. Evaluate modeExpr (or default "rb")
        currentFrameOffset -= 8;
        if (currentFrameOffset < maxFrameOffset) maxFrameOffset = currentFrameOffset;
        int32_t modeSlot = currentFrameOffset;

        uint32_t offModeRb = pe.addStringLiteralOffset("rb");
        uint32_t offModeWb = pe.addStringLiteralOffset("wb");
        uint32_t offModeAb = pe.addStringLiteralOffset("ab");

        if (fileBlock->modeExpr) {
            emitExpr(fileBlock->modeExpr.get()); // RAX = user mode str
            enc.movzx_rm8(x86::RDX, x86::RAX, 0);
            enc.cmp_ri32(x86::RDX, 'w');
            size_t notW = enc.createLabel();
            enc.jne(notW);
            enc.lea_rip_rdata(x86::RAX, offModeWb);
            enc.mov_mr_rbp(modeSlot, x86::RAX);
            size_t modeDone = enc.createLabel();
            enc.jmp(modeDone);

            enc.bindLabel(notW);
            enc.cmp_ri32(x86::RDX, 'a');
            size_t notA = enc.createLabel();
            enc.jne(notA);
            enc.lea_rip_rdata(x86::RAX, offModeAb);
            enc.mov_mr_rbp(modeSlot, x86::RAX);
            enc.jmp(modeDone);

            enc.bindLabel(notA);
            enc.lea_rip_rdata(x86::RAX, offModeRb);
            enc.mov_mr_rbp(modeSlot, x86::RAX);

            enc.bindLabel(modeDone);
        } else {
            enc.lea_rip_rdata(x86::RAX, offModeRb);
            enc.mov_mr_rbp(modeSlot, x86::RAX);
        }

        // 3. Call fopen(path, mode)
        enc.mov_rm_rbp(x86::RCX, pathSlot);
        enc.mov_rm_rbp(x86::RDX, modeSlot);
        enc.call_iat_func("fopen");

        // 4. Check if fopen failed (RAX == 0)
        size_t openSuccessLbl = enc.createLabel();
        enc.test_rr(x86::RAX, x86::RAX);
        enc.jne(openSuccessLbl);

        // Open failed: print runtime error and exit(1)
        uint32_t offErr1 = pe.addStringLiteralOffset("[Runtime Error (RuntimeError)]: Cannot open file '");
        enc.lea_rip_rdata(x86::RCX, offErr1);
        enc.call_iat_func("printf");

        enc.mov_rm_rbp(x86::RDX, pathSlot);
        emitCallPrintf(offFmtStr, x86::RDX, true);

        uint32_t offErr2 = pe.addStringLiteralOffset("' in mode '");
        enc.lea_rip_rdata(x86::RCX, offErr2);
        enc.call_iat_func("printf");

        enc.mov_rm_rbp(x86::RDX, modeSlot);
        emitCallPrintf(offFmtStr, x86::RDX, true);

        uint32_t offErr3 = pe.addStringLiteralOffset("' (File does not exist or permission denied)!\n");
        enc.lea_rip_rdata(x86::RCX, offErr3);
        enc.call_iat_func("printf");

        enc.mov_ri64(x86::RCX, 1);
        enc.call_iat_func("exit");

        // 5. Open succeeded: declare file variable in new scope
        enc.bindLabel(openSuccessLbl);
        enterScope();
        int32_t fileVarOffset = declareVar(fileBlock->varName, ThavaType::FILE);
        enc.mov_mr_rbp(fileVarOffset, x86::RAX); // store FILE*

        // 6. Execute body statements
        for (const auto& stmt : fileBlock->body) {
            emitStmt(stmt.get());
        }

        // 7. Auto close file (RAII)
        enc.mov_rm_rbp(x86::RCX, fileVarOffset);
        enc.call_iat_func("fclose");
        exitScope();
    }

    ThavaType emitOsMethod(OsMethodNode* osNode) {
        if (osNode->method == "name") {
            if constexpr (std::is_same_v<TEmitter, ElfEmitter>) {
                uint32_t off = pe.addStringLiteralOffset("linux");
                enc.lea_rip_rdata(x86::RAX, off);
            } else {
                uint32_t off = pe.addStringLiteralOffset("windows");
                enc.lea_rip_rdata(x86::RAX, off);
            }
            return ThavaType::STRING;
        }

        if (osNode->method == "getpid") {
            enc.call_iat_func("GetCurrentProcessId");
            return ThavaType::INT;
        }

        if (osNode->method == "system") {
            if (!osNode->args.empty()) {
                emitExpr(osNode->args[0].get());
                enc.mov_rr(x86::RCX, x86::RAX);
                enc.call_iat_func("system");
            } else {
                enc.xor_r32(x86::RAX, x86::RAX);
            }
            return ThavaType::INT;
        }

        if (osNode->method == "sleep") {
            if (!osNode->args.empty()) {
                emitExpr(osNode->args[0].get());
                enc.mov_rr(x86::RCX, x86::RAX);
                enc.call_iat_func("Sleep");
            }
            enc.xor_r32(x86::RAX, x86::RAX);
            return ThavaType::INT;
        }

        if (osNode->method == "cpu_count") {
            uint32_t offEnv = pe.addStringLiteralOffset("NUMBER_OF_PROCESSORS");
            enc.lea_rip_rdata(x86::RCX, offEnv);
            enc.call_iat_func("getenv");
            size_t lblHasEnv = enc.createLabel();
            size_t lblDone = enc.createLabel();
            enc.cmp_ri32(x86::RAX, 0);
            enc.jne(lblHasEnv);
            enc.mov_ri64(x86::RAX, 4);
            enc.jmp(lblDone);
            enc.bindLabel(lblHasEnv);
            enc.mov_rr(x86::RCX, x86::RAX);
            enc.call_iat_func("atoi");
            enc.bindLabel(lblDone);
            return ThavaType::INT;
        }

        if (osNode->method == "remove") {
            if (!osNode->args.empty()) {
                emitExpr(osNode->args[0].get());
                enc.mov_rr(x86::RCX, x86::RAX);
                enc.call_iat_func("remove");
                enc.cmp_ri32(x86::RAX, 0);
                enc.setcc_to_rax(x86::Cond::E);
            }
            return ThavaType::BOOL;
        }

        if (osNode->method == "rename") {
            if (osNode->args.size() >= 2) {
                emitExpr(osNode->args[0].get());
                currentFrameOffset -= 8;
                if (currentFrameOffset < maxFrameOffset) maxFrameOffset = currentFrameOffset;
                enc.mov_mr_rbp(currentFrameOffset, x86::RAX);
                int32_t slotOld = currentFrameOffset;

                emitExpr(osNode->args[1].get());
                enc.mov_rr(x86::RDX, x86::RAX);
                enc.mov_rm_rbp(x86::RCX, slotOld);
                enc.call_iat_func("rename");
                enc.cmp_ri32(x86::RAX, 0);
                enc.setcc_to_rax(x86::Cond::E);
            }
            return ThavaType::BOOL;
        }

        if (osNode->method == "exists") {
            if (!osNode->args.empty()) {
                emitExpr(osNode->args[0].get());
                enc.mov_rr(x86::RCX, x86::RAX);
                enc.xor_r32(x86::RDX, x86::RDX);
                enc.call_iat_func("_access");
                enc.cmp_ri32(x86::RAX, 0);
                enc.setcc_to_rax(x86::Cond::E);
            }
            return ThavaType::BOOL;
        }

        if (osNode->method == "mkdir") {
            if (!osNode->args.empty()) {
                emitExpr(osNode->args[0].get());
                enc.mov_rr(x86::RCX, x86::RAX);
                enc.call_iat_func("_mkdir");
                enc.cmp_ri32(x86::RAX, 0);
                enc.setcc_to_rax(x86::Cond::E);
            }
            return ThavaType::BOOL;
        }

        if (osNode->method == "rmdir") {
            if (!osNode->args.empty()) {
                emitExpr(osNode->args[0].get());
                enc.mov_rr(x86::RCX, x86::RAX);
                enc.call_iat_func("_rmdir");
                enc.cmp_ri32(x86::RAX, 0);
                enc.setcc_to_rax(x86::Cond::E);
            }
            return ThavaType::BOOL;
        }

        if (osNode->method == "chdir") {
            if (!osNode->args.empty()) {
                emitExpr(osNode->args[0].get());
                enc.mov_rr(x86::RCX, x86::RAX);
                enc.call_iat_func("_chdir");
                enc.cmp_ri32(x86::RAX, 0);
                enc.setcc_to_rax(x86::Cond::E);
            }
            return ThavaType::BOOL;
        }

        if (osNode->method == "getenv") {
            if (!osNode->args.empty()) {
                emitExpr(osNode->args[0].get());
                enc.mov_rr(x86::RCX, x86::RAX);
                enc.call_iat_func("getenv");
                size_t lblFound = enc.createLabel();
                enc.cmp_ri32(x86::RAX, 0);
                enc.jne(lblFound);
                enc.lea_rip_rdata(x86::RAX, offEmptyStr);
                enc.bindLabel(lblFound);
            }
            return ThavaType::STRING;
        }

        if (osNode->method == "getcwd") {
            enc.xor_r32(x86::RCX, x86::RCX);
            enc.xor_r32(x86::RDX, x86::RDX);
            enc.call_iat_func("_getcwd");
            size_t lblOk = enc.createLabel();
            enc.cmp_ri32(x86::RAX, 0);
            enc.jne(lblOk);
            enc.lea_rip_rdata(x86::RAX, offEmptyStr);
            enc.bindLabel(lblOk);
            return ThavaType::STRING;
        }

        if (osNode->method == "getsize") {
            if (!osNode->args.empty()) {
                emitExpr(osNode->args[0].get());
                currentFrameOffset -= 8;
                if (currentFrameOffset < maxFrameOffset) maxFrameOffset = currentFrameOffset;
                enc.mov_mr_rbp(currentFrameOffset, x86::RAX);
                int32_t slotPath = currentFrameOffset;

                uint32_t offMode = pe.addStringLiteralOffset("rb");
                enc.lea_rip_rdata(x86::RDX, offMode);
                enc.mov_rm_rbp(x86::RCX, slotPath);
                enc.call_iat_func("fopen");
                size_t lblOpenFail = enc.createLabel();
                size_t lblDoneSize = enc.createLabel();
                enc.cmp_ri32(x86::RAX, 0);
                enc.je(lblOpenFail);

                // FILE* in RAX
                currentFrameOffset -= 8;
                if (currentFrameOffset < maxFrameOffset) maxFrameOffset = currentFrameOffset;
                enc.mov_mr_rbp(currentFrameOffset, x86::RAX);
                int32_t slotFp = currentFrameOffset;

                // fseek(fp, 0, SEEK_END=2)
                enc.mov_rm_rbp(x86::RCX, slotFp);
                enc.xor_r32(x86::RDX, x86::RDX);
                enc.mov_ri64(x86::R8, 2);
                enc.call_iat_func("fseek");

                // ftell(fp) -> RAX
                enc.mov_rm_rbp(x86::RCX, slotFp);
                enc.call_iat_func("ftell");
                enc.mov_mr_rbp(slotPath, x86::RAX);

                // fclose(fp)
                enc.mov_rm_rbp(x86::RCX, slotFp);
                enc.call_iat_func("fclose");

                enc.mov_rm_rbp(x86::RAX, slotPath);
                enc.jmp(lblDoneSize);

                enc.bindLabel(lblOpenFail);
                enc.mov_ri64(x86::RAX, -1);
                enc.bindLabel(lblDoneSize);
            }
            return ThavaType::INT;
        }

        if (osNode->method == "showmessagebox") {
            if (osNode->args.size() >= 4) {
                // Title (args[1])
                emitExpr(osNode->args[1].get());
                currentFrameOffset -= 8;
                if (currentFrameOffset < maxFrameOffset) maxFrameOffset = currentFrameOffset;
                enc.mov_mr_rbp(currentFrameOffset, x86::RAX);
                int32_t slotTitle = currentFrameOffset;

                // Content (args[2])
                emitExpr(osNode->args[2].get());
                currentFrameOffset -= 8;
                if (currentFrameOffset < maxFrameOffset) maxFrameOffset = currentFrameOffset;
                enc.mov_mr_rbp(currentFrameOffset, x86::RAX);
                int32_t slotContent = currentFrameOffset;

                auto parseIconFlag = [](ASTNode* n) -> int {
                    if (auto lit = dynamic_cast<LiteralNode*>(n)) {
                        if (lit->getLitType() == ThavaType::INT) return std::stoi(lit->getVal());
                        if (lit->getLitType() == ThavaType::STRING) {
                            std::string s = lit->getVal();
                            for (char& c : s) c = (char)tolower((unsigned char)c);
                            if (s == "warning") return 0x30;
                            if (s == "information" || s == "infomation" || s == "info") return 0x40;
                            if (s == "question") return 0x20;
                            if (s == "error") return 0x10;
                            return 0;
                        }
                    }
                    return -1;
                };

                auto parseButtonFlag = [](ASTNode* n) -> int {
                    if (auto lit = dynamic_cast<LiteralNode*>(n)) {
                        if (lit->getLitType() == ThavaType::INT) return std::stoi(lit->getVal());
                        if (lit->getLitType() == ThavaType::STRING) {
                            std::string s = lit->getVal();
                            for (char& c : s) c = (char)tolower((unsigned char)c);
                            if (s == "ok") return 0;
                            if (s == "ok/cancel" || s == "okcancel") return 1;
                            if (s == "yes/no/cancel" || s == "yesnocancel") return 3;
                            if (s == "yes/no" || s == "yesno") return 4;
                            if (s == "retry/cancel" || s == "retrycancel") return 5;
                            return 0;
                        }
                    }
                    return -1;
                };

                int iconFlag = parseIconFlag(osNode->args[0].get());
                int btnFlag = parseButtonFlag(osNode->args[3].get());

                if (iconFlag >= 0 && btnFlag >= 0) {
                    uint32_t uType = (uint32_t)(iconFlag | btnFlag | 0x40000 /* MB_TOPMOST */);
                    enc.mov_ri64(x86::R9, uType);
                } else {
                    emitExpr(osNode->args[0].get());
                    currentFrameOffset -= 8;
                    if (currentFrameOffset < maxFrameOffset) maxFrameOffset = currentFrameOffset;
                    enc.mov_mr_rbp(currentFrameOffset, x86::RAX);
                    int32_t slotIcon = currentFrameOffset;

                    emitExpr(osNode->args[3].get());
                    enc.mov_rr(x86::R9, x86::RAX);
                    enc.mov_rm_rbp(x86::R10, slotIcon);
                    enc.or_rr(x86::R9, x86::R10);
                    enc.mov_ri64(x86::R10, 0x40000);
                    enc.or_rr(x86::R9, x86::R10);
                }

                // RCX = 0 (hWnd), RDX = content, R8 = title, R9 = uType
                enc.xor_r32(x86::RCX, x86::RCX);
                enc.mov_rm_rbp(x86::RDX, slotContent);
                enc.mov_rm_rbp(x86::R8, slotTitle);
                enc.call_iat_func("MessageBoxA");
            }
            return ThavaType::INT;
        }

        return ThavaType::UNKNOWN;
    }

    ThavaType emitTimeMethod(TimeMethodNode* timeNode) {
        if (timeNode->method == "timestamp") {
            enc.xor_r32(x86::RCX, x86::RCX);
            enc.call_iat_func("time");
            return ThavaType::INT;
        }

        if (timeNode->method == "sleep") {
            if (!timeNode->args.empty()) {
                emitExpr(timeNode->args[0].get());
                enc.mov_rr(x86::RCX, x86::RAX);
                enc.call_iat_func("Sleep");
            }
            enc.xor_r32(x86::RAX, x86::RAX);
            return ThavaType::INT;
        }

        // Component getters: year, month, day, hour, minute, second, millis
        if (timeNode->method == "year" || timeNode->method == "month" || timeNode->method == "day" ||
            timeNode->method == "hour" || timeNode->method == "minute" || timeNode->method == "second" ||
            timeNode->method == "millis") {
            currentFrameOffset -= 16;
            if (currentFrameOffset < maxFrameOffset) maxFrameOffset = currentFrameOffset;
            int32_t stSlot = currentFrameOffset;

            enc.lea_rm_rbp(x86::RCX, stSlot);
            enc.call_iat_func("GetLocalTime");

            int32_t off = 0;
            if (timeNode->method == "year") off = 0;
            else if (timeNode->method == "month") off = 2;
            else if (timeNode->method == "day") off = 6;
            else if (timeNode->method == "hour") off = 8;
            else if (timeNode->method == "minute") off = 10;
            else if (timeNode->method == "second") off = 12;
            else if (timeNode->method == "millis") off = 14;

            enc.movzx_rm16(x86::RAX, x86::RBP, stSlot + off);
            currentFrameOffset += 16;
            return ThavaType::INT;
        }

        if (timeNode->method == "now" || timeNode->method == "date") {
            currentFrameOffset -= 16;
            if (currentFrameOffset < maxFrameOffset) maxFrameOffset = currentFrameOffset;
            int32_t stSlot = currentFrameOffset;

            enc.lea_rm_rbp(x86::RCX, stSlot);
            enc.call_iat_func("GetLocalTime");

            // Allocate 128 bytes buffer
            enc.mov_ri64(x86::RCX, 128);
            enc.call_iat_func("malloc");
            currentFrameOffset -= 8;
            if (currentFrameOffset < maxFrameOffset) maxFrameOffset = currentFrameOffset;
            int32_t bufSlot = currentFrameOffset;
            enc.mov_mr_rbp(bufSlot, x86::RAX);
            enc.mov_rr(x86::RDI, x86::RAX); // RDI = output buffer

            // Determine format string
            std::string rawFmt = (timeNode->method == "date") ? "%Y-%m-%d" : "%Y-%m-%d %H:%M:%S";
            bool isDynamicFmt = false;
            if (!timeNode->args.empty()) {
                if (auto lit = dynamic_cast<LiteralNode*>(timeNode->args[0].get())) {
                    rawFmt = lit->getVal();
                } else {
                    isDynamicFmt = true;
                }
            }

            if (!isDynamicFmt) {
                // Normalize keywords and patterns to strftime % tokens
                std::string f = rawFmt;
                if (f.empty() || f == "datetime") f = "%Y-%m-%d %H:%M:%S";
                else if (f == "date") f = "%Y-%m-%d";
                else if (f == "time") f = "%H:%M:%S";
                else {
                    auto replaceAll = [](std::string& str, const std::string& from, const std::string& to) {
                        size_t start_pos = 0;
                        while((start_pos = str.find(from, start_pos)) != std::string::npos) {
                            str.replace(start_pos, from.length(), to);
                            start_pos += to.length();
                        }
                    };
                    replaceAll(f, "yyyy", "%Y"); replaceAll(f, "YYYY", "%Y");
                    replaceAll(f, ":mm", ":%M");  replaceAll(f, ":MM", ":%M");
                    replaceAll(f, "mm:", "%M:");  replaceAll(f, "MM:", "%M:");
                    replaceAll(f, "mm", "%m");   replaceAll(f, "MM", "%m");
                    replaceAll(f, "dd", "%d");   replaceAll(f, "DD", "%d");
                    replaceAll(f, "hh", "%H");   replaceAll(f, "HH", "%H");
                    replaceAll(f, "ss", "%S");   replaceAll(f, "SS", "%S");
                }
                uint32_t offFmt = pe.addStringLiteralOffset(f);
                enc.lea_rip_rdata(x86::RSI, offFmt);
            } else {
                emitExpr(timeNode->args[0].get());
                enc.mov_rr(x86::RSI, x86::RAX);
            }

            // Assembly format loop:
            // RSI: fmt, RDI: dest, [RBP + stSlot]: SYSTEMTIME
            size_t lblFmtLoop = enc.createLabel();
            size_t lblFmtDone = enc.createLabel();
            size_t lblNotToken = enc.createLabel();
            size_t lblTokY = enc.createLabel();
            size_t lblTokm = enc.createLabel();
            size_t lblTokd = enc.createLabel();
            size_t lblTokH = enc.createLabel();
            size_t lblTokM = enc.createLabel();
            size_t lblTokS = enc.createLabel();
            size_t lblTokPct = enc.createLabel();

            enc.bindLabel(lblFmtLoop);
            enc.movzx_rm8(x86::RAX, x86::RSI, 0);
            enc.test_rr(x86::RAX, x86::RAX);
            enc.je(lblFmtDone);

            enc.cmp_ri32(x86::RAX, '%');
            enc.jne(lblNotToken);

            enc.movzx_rm8(x86::RDX, x86::RSI, 1);
            enc.test_rr(x86::RDX, x86::RDX);
            enc.je(lblNotToken);

            enc.cmp_ri32(x86::RDX, 'Y');
            enc.je(lblTokY);
            enc.cmp_ri32(x86::RDX, 'm');
            enc.je(lblTokm);
            enc.cmp_ri32(x86::RDX, 'd');
            enc.je(lblTokd);
            enc.cmp_ri32(x86::RDX, 'H');
            enc.je(lblTokH);
            enc.cmp_ri32(x86::RDX, 'M');
            enc.je(lblTokM);
            enc.cmp_ri32(x86::RDX, 'S');
            enc.je(lblTokS);
            enc.cmp_ri32(x86::RDX, '%');
            enc.je(lblTokPct);
            enc.jmp(lblNotToken);

            // %Y (4 digits)
            enc.bindLabel(lblTokY);
            enc.movzx_rm16(x86::RAX, x86::RBP, stSlot + 0);
            enc.xor_r32(x86::RDX, x86::RDX);
            enc.mov_ri64(x86::R10, 100);
            enc.div_r(x86::R10);
            enc.push(x86::RDX); // year in century

            // Century
            enc.xor_r32(x86::RDX, x86::RDX);
            enc.mov_ri64(x86::R10, 10);
            enc.div_r(x86::R10);
            enc.add_ri32(x86::RAX, '0');
            enc.add_ri32(x86::RDX, '0');
            enc.mov_mr8(x86::RDI, 0, x86::RAX);
            enc.mov_mr8(x86::RDI, 1, x86::RDX);
            enc.add_ri32(x86::RDI, 2);

            // Year in century
            enc.pop(x86::RAX);
            enc.xor_r32(x86::RDX, x86::RDX);
            enc.mov_ri64(x86::R10, 10);
            enc.div_r(x86::R10);
            enc.add_ri32(x86::RAX, '0');
            enc.add_ri32(x86::RDX, '0');
            enc.mov_mr8(x86::RDI, 0, x86::RAX);
            enc.mov_mr8(x86::RDI, 1, x86::RDX);
            enc.add_ri32(x86::RDI, 2);
            enc.add_ri32(x86::RSI, 2);
            enc.jmp(lblFmtLoop);

            // Helper to emit 2 digits
            auto emit2Digits = [&](size_t lbl, int stOffset) {
                enc.bindLabel(lbl);
                enc.movzx_rm16(x86::RAX, x86::RBP, stSlot + stOffset);
                enc.xor_r32(x86::RDX, x86::RDX);
                enc.mov_ri64(x86::R10, 10);
                enc.div_r(x86::R10);
                enc.add_ri32(x86::RAX, '0');
                enc.add_ri32(x86::RDX, '0');
                enc.mov_mr8(x86::RDI, 0, x86::RAX);
                enc.mov_mr8(x86::RDI, 1, x86::RDX);
                enc.add_ri32(x86::RDI, 2);
                enc.add_ri32(x86::RSI, 2);
                enc.jmp(lblFmtLoop);
            };

            emit2Digits(lblTokm, 2);   // %m -> month (+2)
            emit2Digits(lblTokd, 6);   // %d -> day (+6)
            emit2Digits(lblTokH, 8);   // %H -> hour (+8)
            emit2Digits(lblTokM, 10);  // %M -> minute (+10)
            emit2Digits(lblTokS, 12);  // %S -> second (+12)

            // %%
            enc.bindLabel(lblTokPct);
            enc.mov_ri64(x86::RAX, '%');
            enc.mov_mr8(x86::RDI, 0, x86::RAX);
            enc.inc_r(x86::RDI);
            enc.add_ri32(x86::RSI, 2);
            enc.jmp(lblFmtLoop);

            // Literal character
            enc.bindLabel(lblNotToken);
            enc.mov_mr8(x86::RDI, 0, x86::RAX);
            enc.inc_r(x86::RDI);
            enc.inc_r(x86::RSI);
            enc.jmp(lblFmtLoop);

            // End of format string
            enc.bindLabel(lblFmtDone);
            enc.xor_r32(x86::RAX, x86::RAX);
            enc.mov_mr8(x86::RDI, 0, x86::RAX); // null terminate

            // Return pointer to allocated string in RAX
            enc.mov_rm_rbp(x86::RAX, bufSlot);
            currentFrameOffset += 24;
            return ThavaType::STRING;
        }

        return ThavaType::UNKNOWN;
    }

    ThavaType emitCLibMethod(CLibMethodNode* clibNode) {
        if (!clibNode) return ThavaType::UNKNOWN;

        if (clibNode->method == "load") {
            if (clibNode->args.empty()) {
                std::cerr << "Error: clib.load() requires library path!\n";
                exit(1);
            }
            emitExpr(clibNode->args[0].get());
            enc.mov_rr(x86::RCX, x86::RAX);
            enc.call_iat_func("LoadLibraryA");
            return ThavaType::INT;
        }

        if (clibNode->method == "free") {
            if (clibNode->target) {
                emitExpr(clibNode->target.get());
            } else if (!clibNode->args.empty()) {
                emitExpr(clibNode->args[0].get());
            } else {
                std::cerr << "Error: clib.free() requires library handle!\n";
                exit(1);
            }
            enc.mov_rr(x86::RCX, x86::RAX);
            enc.call_iat_func("FreeLibrary");
            return ThavaType::INT;
        }

        if (clibNode->method == "has") {
            int32_t savedOffset = currentFrameOffset;
            if (clibNode->target) {
                // lib.has("func_name")
                emitExpr(clibNode->target.get());
                currentFrameOffset -= 8;
                if (currentFrameOffset < maxFrameOffset) maxFrameOffset = currentFrameOffset;
                int32_t slotH = currentFrameOffset;
                enc.mov_mr_rbp(slotH, x86::RAX);

                if (clibNode->args.empty()) {
                    std::cerr << "Error: lib.has() requires function name!\n";
                    exit(1);
                }
                emitExpr(clibNode->args[0].get());
                enc.mov_rr(x86::RDX, x86::RAX);
                enc.mov_rm_rbp(x86::RCX, slotH);
                enc.call_iat_func("GetProcAddress");
                enc.test_rr(x86::RAX, x86::RAX);
                enc.setcc_to_rax(x86::Cond::NE);
                currentFrameOffset = savedOffset;
                return ThavaType::BOOL;
            } else {
                // clib.has("lib_name", "func_name")
                if (clibNode->args.size() < 2) {
                    std::cerr << "Error: clib.has() requires at least 2 arguments (lib, func)!\n";
                    exit(1);
                }
                emitExpr(clibNode->args[0].get());
                enc.mov_rr(x86::RCX, x86::RAX);
                enc.call_iat_func("LoadLibraryA");

                currentFrameOffset -= 8;
                if (currentFrameOffset < maxFrameOffset) maxFrameOffset = currentFrameOffset;
                int32_t slotH = currentFrameOffset;
                enc.mov_mr_rbp(slotH, x86::RAX);

                size_t lblDone = enc.createLabel();
                enc.test_rr(x86::RAX, x86::RAX);
                enc.je(lblDone);

                emitExpr(clibNode->args[1].get());
                enc.mov_rr(x86::RDX, x86::RAX);
                enc.mov_rm_rbp(x86::RCX, slotH);
                enc.call_iat_func("GetProcAddress");
                enc.test_rr(x86::RAX, x86::RAX);
                enc.setcc_to_rax(x86::Cond::NE);

                enc.bindLabel(lblDone);
                currentFrameOffset = savedOffset;
                return ThavaType::BOOL;
            }
        }

        if (clibNode->method == "call" || clibNode->method == "call_str" || clibNode->method == "call_dbl") {
            int32_t savedOffset = currentFrameOffset;
            int32_t slotH = 0;
            int32_t slotFunc = 0;
            std::vector<ASTNode*> actualArgs;

            if (clibNode->target) {
                // lib.call("func_name", ...)
                emitExpr(clibNode->target.get());
                currentFrameOffset -= 8;
                if (currentFrameOffset < maxFrameOffset) maxFrameOffset = currentFrameOffset;
                slotH = currentFrameOffset;
                enc.mov_mr_rbp(slotH, x86::RAX);

                if (clibNode->args.empty()) {
                    std::cerr << "Error: lib.call() requires function name!\n";
                    exit(1);
                }
                emitExpr(clibNode->args[0].get()); // funcName
                enc.mov_rr(x86::RDX, x86::RAX);
                enc.mov_rm_rbp(x86::RCX, slotH);
                enc.call_iat_func("GetProcAddress");

                currentFrameOffset -= 8;
                if (currentFrameOffset < maxFrameOffset) maxFrameOffset = currentFrameOffset;
                slotFunc = currentFrameOffset;
                enc.mov_mr_rbp(slotFunc, x86::RAX);

                for (size_t i = 1; i < clibNode->args.size(); ++i) {
                    actualArgs.push_back(clibNode->args[i].get());
                }
            } else {
                // clib.call("lib_name", "func_name", ...)
                if (clibNode->args.size() < 2) {
                    std::cerr << "Error: clib.call() requires at least 2 arguments (lib, func)!\n";
                    exit(1);
                }
                emitExpr(clibNode->args[0].get()); // libName
                enc.mov_rr(x86::RCX, x86::RAX);
                enc.call_iat_func("LoadLibraryA");

                currentFrameOffset -= 8;
                if (currentFrameOffset < maxFrameOffset) maxFrameOffset = currentFrameOffset;
                slotH = currentFrameOffset;
                enc.mov_mr_rbp(slotH, x86::RAX);

                emitExpr(clibNode->args[1].get()); // funcName
                enc.mov_rr(x86::RDX, x86::RAX);
                enc.mov_rm_rbp(x86::RCX, slotH);
                enc.call_iat_func("GetProcAddress");

                currentFrameOffset -= 8;
                if (currentFrameOffset < maxFrameOffset) maxFrameOffset = currentFrameOffset;
                slotFunc = currentFrameOffset;
                enc.mov_mr_rbp(slotFunc, x86::RAX);

                for (size_t i = 2; i < clibNode->args.size(); ++i) {
                    actualArgs.push_back(clibNode->args[i].get());
                }
            }

            // Check if proc == 0
            size_t lblCallProc = enc.createLabel();
            size_t lblCallDone = enc.createLabel();
            enc.mov_rm_rbp(x86::RAX, slotFunc);
            enc.test_rr(x86::RAX, x86::RAX);
            enc.jne(lblCallProc);

            // Fail: return 0 / null / 0.0
            enc.xor_r32(x86::RAX, x86::RAX);
            if (clibNode->method == "call_dbl") {
                enc.cvtsi2sd(x86::XMM0, x86::RAX);
            }
            enc.jmp(lblCallDone);

            enc.bindLabel(lblCallProc);
            // Evaluate arguments
            std::vector<int32_t> argSlots;
            std::vector<ThavaType> argTypes;

            for (size_t i = 0; i < actualArgs.size(); ++i) {
                ThavaType argType = emitExpr(actualArgs[i]);
                currentFrameOffset -= 8;
                if (currentFrameOffset < maxFrameOffset) maxFrameOffset = currentFrameOffset;
                if (argType == ThavaType::FLOAT || argType == ThavaType::DOUBLE) {
                    enc.movsd_mr_rbp(currentFrameOffset, x86::XMM0);
                } else {
                    enc.mov_mr_rbp(currentFrameOffset, x86::RAX);
                }
                argSlots.push_back(currentFrameOffset);
                argTypes.push_back(argType);
            }

            // Pass first 4 args in registers RCX, RDX, R8, R9 / XMM0..3
            x86::Reg intRegs[] = { x86::RCX, x86::RDX, x86::R8, x86::R9 };
            x86::Xmm xmmRegs[] = { x86::XMM0, x86::XMM1, x86::XMM2, x86::XMM3 };

            for (size_t i = 0; i < argSlots.size() && i < 4; ++i) {
                if (argTypes[i] == ThavaType::FLOAT || argTypes[i] == ThavaType::DOUBLE) {
                    enc.movsd_rm_rbp(xmmRegs[i], argSlots[i]);
                    enc.mov_rm_rbp(intRegs[i], argSlots[i]);
                } else {
                    enc.mov_rm_rbp(intRegs[i], argSlots[i]);
                }
            }

            // Pass stack arguments for >= 4 args
            for (size_t i = 4; i < argSlots.size(); ++i) {
                int32_t stackDisp = 32 + (int32_t)(i - 4) * 8;
                if (argTypes[i] == ThavaType::FLOAT || argTypes[i] == ThavaType::DOUBLE) {
                    enc.movsd_rm_rbp(x86::XMM4, argSlots[i]);
                    enc.movsd_mr_rsp(stackDisp, x86::XMM4);
                } else {
                    enc.mov_rm_rbp(x86::R10, argSlots[i]);
                    enc.mov_mr_rsp(stackDisp, x86::R10);
                }
            }

            // Call function pointer
            enc.mov_rm_rbp(x86::RAX, slotFunc);
            enc.call_r(x86::RAX);

            enc.bindLabel(lblCallDone);
            currentFrameOffset = savedOffset;

            if (clibNode->method == "call_str") return ThavaType::STRING;
            if (clibNode->method == "call_dbl") return ThavaType::DOUBLE;
            return ThavaType::INT;
        }

        if (clibNode->method == "alloc") {
            if (clibNode->args.empty()) {
                std::cerr << "Error: clib.alloc() requires byte size!\n";
                exit(1);
            }
            emitExpr(clibNode->args[0].get());
            enc.mov_rr(x86::RCX, x86::RAX);
            enc.call_iat_func("malloc");
            return ThavaType::INT;
        }

        if (clibNode->method == "free_mem") {
            if (clibNode->args.empty()) {
                std::cerr << "Error: clib.free_mem() requires pointer address!\n";
                exit(1);
            }
            emitExpr(clibNode->args[0].get());
            enc.mov_rr(x86::RCX, x86::RAX);
            enc.call_iat_func("free");
            return ThavaType::VOID;
        }

        if (clibNode->method == "peek" || clibNode->method == "peek32") {
            if (clibNode->args.empty()) {
                std::cerr << "Error: clib." << clibNode->method << "() requires pointer address!\n";
                exit(1);
            }
            int32_t savedOffset = currentFrameOffset;
            emitExpr(clibNode->args[0].get()); // ptr
            currentFrameOffset -= 8;
            if (currentFrameOffset < maxFrameOffset) maxFrameOffset = currentFrameOffset;
            int32_t slotPtr = currentFrameOffset;
            enc.mov_mr_rbp(slotPtr, x86::RAX);

            if (clibNode->args.size() > 1) {
                emitExpr(clibNode->args[1].get()); // offset
            } else {
                enc.xor_r32(x86::RAX, x86::RAX);
            }
            enc.mov_rm_rbp(x86::R10, slotPtr);
            enc.add_rr(x86::R10, x86::RAX);
            enc.movsxd_rm(x86::RAX, x86::R10, 0);
            currentFrameOffset = savedOffset;
            return ThavaType::INT;
        }

        if (clibNode->method == "peek_u8") {
            if (clibNode->args.empty()) {
                std::cerr << "Error: clib.peek_u8() requires pointer address!\n";
                exit(1);
            }
            int32_t savedOffset = currentFrameOffset;
            emitExpr(clibNode->args[0].get()); // ptr
            currentFrameOffset -= 8;
            if (currentFrameOffset < maxFrameOffset) maxFrameOffset = currentFrameOffset;
            int32_t slotPtr = currentFrameOffset;
            enc.mov_mr_rbp(slotPtr, x86::RAX);

            if (clibNode->args.size() > 1) {
                emitExpr(clibNode->args[1].get()); // offset
            } else {
                enc.xor_r32(x86::RAX, x86::RAX);
            }
            enc.mov_rm_rbp(x86::R10, slotPtr);
            enc.add_rr(x86::R10, x86::RAX);
            enc.movzx_rm8(x86::RAX, x86::R10, 0);
            currentFrameOffset = savedOffset;
            return ThavaType::INT;
        }

        if (clibNode->method == "peek64") {
            if (clibNode->args.empty()) {
                std::cerr << "Error: clib.peek64() requires pointer address!\n";
                exit(1);
            }
            int32_t savedOffset = currentFrameOffset;
            emitExpr(clibNode->args[0].get()); // ptr
            currentFrameOffset -= 8;
            if (currentFrameOffset < maxFrameOffset) maxFrameOffset = currentFrameOffset;
            int32_t slotPtr = currentFrameOffset;
            enc.mov_mr_rbp(slotPtr, x86::RAX);

            if (clibNode->args.size() > 1) {
                emitExpr(clibNode->args[1].get()); // offset
            } else {
                enc.xor_r32(x86::RAX, x86::RAX);
            }
            enc.mov_rm_rbp(x86::R10, slotPtr);
            enc.add_rr(x86::R10, x86::RAX);
            enc.mov_rm(x86::RAX, x86::R10, 0);
            currentFrameOffset = savedOffset;
            return ThavaType::INT;
        }

        if (clibNode->method == "peek_f") {
            if (clibNode->args.empty()) {
                std::cerr << "Error: clib.peek_f() requires pointer address!\n";
                exit(1);
            }
            int32_t savedOffset = currentFrameOffset;
            emitExpr(clibNode->args[0].get()); // ptr
            currentFrameOffset -= 8;
            if (currentFrameOffset < maxFrameOffset) maxFrameOffset = currentFrameOffset;
            int32_t slotPtr = currentFrameOffset;
            enc.mov_mr_rbp(slotPtr, x86::RAX);

            if (clibNode->args.size() > 1) {
                emitExpr(clibNode->args[1].get()); // offset
            } else {
                enc.xor_r32(x86::RAX, x86::RAX);
            }
            enc.mov_rm_rbp(x86::R10, slotPtr);
            enc.add_rr(x86::R10, x86::RAX);
            enc.movss_rm(x86::XMM0, x86::R10, 0);
            enc.cvtss2sd(x86::XMM0, x86::XMM0);
            currentFrameOffset = savedOffset;
            return ThavaType::DOUBLE;
        }

        if (clibNode->method == "poke" || clibNode->method == "poke32") {
            if (clibNode->args.size() < 3) {
                std::cerr << "Error: clib." << clibNode->method << "() requires (ptr, offset, val)!\n";
                exit(1);
            }
            int32_t savedOffset = currentFrameOffset;
            emitExpr(clibNode->args[0].get()); // ptr
            currentFrameOffset -= 8;
            if (currentFrameOffset < maxFrameOffset) maxFrameOffset = currentFrameOffset;
            int32_t slotPtr = currentFrameOffset;
            enc.mov_mr_rbp(slotPtr, x86::RAX);

            emitExpr(clibNode->args[1].get()); // offset
            currentFrameOffset -= 8;
            if (currentFrameOffset < maxFrameOffset) maxFrameOffset = currentFrameOffset;
            int32_t slotOff = currentFrameOffset;
            enc.mov_mr_rbp(slotOff, x86::RAX);

            emitExpr(clibNode->args[2].get()); // val in RAX
            enc.mov_rm_rbp(x86::R10, slotPtr);
            enc.mov_rm_rbp(x86::R11, slotOff);
            enc.add_rr(x86::R10, x86::R11);
            enc.mov_mr32(x86::R10, 0, x86::RAX);
            currentFrameOffset = savedOffset;
            return ThavaType::INT;
        }

        if (clibNode->method == "poke64") {
            if (clibNode->args.size() < 3) {
                std::cerr << "Error: clib.poke64() requires (ptr, offset, val)!\n";
                exit(1);
            }
            int32_t savedOffset = currentFrameOffset;
            emitExpr(clibNode->args[0].get()); // ptr
            currentFrameOffset -= 8;
            if (currentFrameOffset < maxFrameOffset) maxFrameOffset = currentFrameOffset;
            int32_t slotPtr = currentFrameOffset;
            enc.mov_mr_rbp(slotPtr, x86::RAX);

            emitExpr(clibNode->args[1].get()); // offset
            currentFrameOffset -= 8;
            if (currentFrameOffset < maxFrameOffset) maxFrameOffset = currentFrameOffset;
            int32_t slotOff = currentFrameOffset;
            enc.mov_mr_rbp(slotOff, x86::RAX);

            emitExpr(clibNode->args[2].get()); // val in RAX
            enc.mov_rm_rbp(x86::R10, slotPtr);
            enc.mov_rm_rbp(x86::R11, slotOff);
            enc.add_rr(x86::R10, x86::R11);
            enc.mov_mr(x86::R10, 0, x86::RAX);
            currentFrameOffset = savedOffset;
            return ThavaType::INT;
        }

        if (clibNode->method == "poke_f") {
            if (clibNode->args.size() < 3) {
                std::cerr << "Error: clib.poke_f() requires (ptr, offset, val)!\n";
                exit(1);
            }
            int32_t savedOffset = currentFrameOffset;
            emitExpr(clibNode->args[0].get()); // ptr
            currentFrameOffset -= 8;
            if (currentFrameOffset < maxFrameOffset) maxFrameOffset = currentFrameOffset;
            int32_t slotPtr = currentFrameOffset;
            enc.mov_mr_rbp(slotPtr, x86::RAX);

            emitExpr(clibNode->args[1].get()); // offset
            currentFrameOffset -= 8;
            if (currentFrameOffset < maxFrameOffset) maxFrameOffset = currentFrameOffset;
            int32_t slotOff = currentFrameOffset;
            enc.mov_mr_rbp(slotOff, x86::RAX);

            ThavaType valType = emitExpr(clibNode->args[2].get()); // val
            if (valType == ThavaType::INT) {
                enc.cvtsi2sd(x86::XMM0, x86::RAX);
            }
            enc.cvtsd2ss(x86::XMM0, x86::XMM0);
            enc.mov_rm_rbp(x86::R10, slotPtr);
            enc.mov_rm_rbp(x86::R11, slotOff);
            enc.add_rr(x86::R10, x86::R11);
            enc.movss_mr(x86::R10, 0, x86::XMM0);
            currentFrameOffset = savedOffset;
            return ThavaType::INT;
        }

        return ThavaType::UNKNOWN;
    }


    // ==========================================
    // STATEMENT CODE GENERATION
    // ==========================================
    // ==========================================
    // CLASS SUPPORT (single inheritance, static dispatch)
    // An instance is a flat heap block: inherited fields first, then own
    // fields, every field in an 8-byte slot. Methods are ordinary functions
    // whose first argument is the implicit 'this' pointer.
    // ==========================================
    std::map<std::string, StructInfo> classLayouts;
    std::map<std::string, std::string> classBase;
    std::map<std::string, std::vector<std::pair<int32_t, ASTNode*>>> classFieldDefaults;
    std::map<std::string, size_t> classMethodLabels;   // "Class::method" -> label
    std::map<std::string, ThavaType> classMethodRet;    // "Class::method" -> return type
    std::map<std::string, std::vector<std::pair<ThavaType, std::string>>> classMethodParams;
    std::string currentClassName;
    std::string currentBaseClassName;

    // Resolve the struct/class name a member-access target refers to.
    std::string structNameOfTarget(ASTNode* n) {
        if (!n) return "";
        if (auto v = dynamic_cast<VarNode*>(n)) {
            VarInfo* vi = lookupVar(v->getName());
            if (vi) return vi->customType;
            return "";
        }
        if (dynamic_cast<LocVarNode*>(n)) return currentClassName;
        if (auto p = dynamic_cast<ParVarNode*>(n)) return p->baseClassName;
        if (auto m = dynamic_cast<MemberAccessNode*>(n)) {
            std::string parent = structNameOfTarget(m->target.get());
            auto it = g_structs.find(parent);
            if (it != g_structs.end()) {
                auto fi = it->second.fieldIndex.find(m->memberName);
                if (fi != it->second.fieldIndex.end()) return it->second.fields[fi->second].customType;
            }
        }
        return "";
    }

    size_t findMethodLabel(const std::string& cls, const std::string& method, ThavaType* outRet) {
        std::string c = cls;
        while (!c.empty()) {
            std::string key = c + "::" + method;
            auto it = classMethodLabels.find(key);
            if (it != classMethodLabels.end()) {
                if (outRet) {
                    auto rt = classMethodRet.find(key);
                    *outRet = (rt != classMethodRet.end()) ? rt->second : ThavaType::VOID;
                }
                return it->second;
            }
            auto b = classBase.find(c);
            c = (b != classBase.end()) ? b->second : std::string();
        }
        return 0;
    }

    void registerNativeClass(ClassDeclNode* cd) {
        StructInfo layout;
        int32_t cur = 0;
        if (!cd->baseClassName.empty()) {
            auto bit = classLayouts.find(cd->baseClassName);
            if (bit == classLayouts.end()) {
                std::cerr << "[Native Codegen Error]: base class '" << cd->baseClassName
                          << "' of '" << cd->className << "' is not declared before it.\n";
                exit(1);
            }
            layout = bit->second; // inherited fields keep their offsets
            cur = layout.totalSize;
            classBase[cd->className] = cd->baseClassName;
        }
        layout.name = cd->className;
        for (auto& f : cd->fields) {
            StructFieldInfo sfi;
            sfi.name = f.name;
            sfi.type = f.type;
            sfi.customType = f.customTypeName;
            sfi.offset = cur;
            layout.fieldIndex[f.name] = layout.fields.size();
            layout.fields.push_back(sfi);
            if (f.initExpr) classFieldDefaults[cd->className].push_back({ cur, f.initExpr.get() });
            cur += 8;
        }
        layout.totalSize = cur > 0 ? cur : 8;
        classLayouts[cd->className] = layout;
        g_structs[cd->className] = layout; // reuse struct field read/write codegen

        for (auto& m : cd->methods) {
            std::string key = cd->className + "::" + m.name;
            if (!classMethodLabels.count(key)) {
                classMethodLabels[key] = enc.createLabel();
                classMethodRet[key] = m.returnType;
                classMethodParams[key] = m.params;
            }
        }
        for (auto& c : cd->constructors) {
            std::string key = cd->className + "::init";
            if (!classMethodLabels.count(key)) {
                classMethodLabels[key] = enc.createLabel();
                classMethodRet[key] = ThavaType::VOID;
                classMethodParams[key] = c.params;
            }
            std::string alias = cd->className + "::" + cd->className;
            if (!classMethodLabels.count(alias)) {
                classMethodLabels[alias] = classMethodLabels[key];
                classMethodRet[alias] = ThavaType::VOID;
                classMethodParams[alias] = c.params;
            }
        }
    }

    void emitClassMethod(const std::string& className, size_t labelId, ThavaType retType,
                         const std::string& retWidth,
                         const std::vector<std::pair<ThavaType, std::string>>& params,
                         const std::vector<std::string>& paramCustoms,
                         const std::vector<ASTPtr>& body,
                         const std::string& methodName) {
        std::string prevClass = currentClassName;
        std::string prevBase = currentBaseClassName;
        currentClassName = className;
        auto bit = classBase.find(className);
        currentBaseClassName = (bit != classBase.end()) ? bit->second : std::string();

        enc.bindLabel(labelId);
        enc.push(x86::RBP);
        enc.mov_rr(x86::RBP, x86::RSP);
        enc.sub_rsp(2048);

        currentFrameOffset = -32;
        maxFrameOffset = -32;
        currentFuncRetLabel = enc.createLabel();
        currentFuncRetType = retType;
        currentFuncRetWidth = retWidth;

        enterFunctionScope();

        x86::Reg intRegs[] = { x86::RCX, x86::RDX, x86::R8, x86::R9 };
        x86::Xmm xmmRegs[] = { x86::XMM0, x86::XMM1, x86::XMM2, x86::XMM3 };

        int32_t thisOff = declareVar("@this", ThavaType::CUSTOM, className);
        enc.mov_mr_rbp(thisOff, x86::RCX);

        for (size_t i = 0; i < params.size(); ++i) {
            ThavaType pt = params[i].first;
            std::string pct = (i < paramCustoms.size()) ? paramCustoms[i] : "";
            int32_t off = declareVar(params[i].second, pt, pct);
            if (isCollectionType(pt)) {
                // Element type comes from the call sites of this method.
                auto eit = g_funcListParamElem.find(className + "::" + methodName);
                ThavaType et = (eit != g_funcListParamElem.end() && i < eit->second.size())
                                   ? eit->second[i] : ThavaType::UNKNOWN;
                if (et != ThavaType::UNKNOWN) listElemTypes[params[i].second] = et;
                else listElemTypes.erase(params[i].second);
                int32_t refSlot = declareVar("@ref" + std::to_string(i), ThavaType::INT);
                VarInfo* pv = lookupVar(params[i].second);
                if (pv) { pv->listOut = true; pv->listOutSlot = refSlot; }
                if (i < 3) {
                    enc.mov_mr_rbp(refSlot, intRegs[i + 1]);
                } else {
                    int32_t callerOff = 48 + (int32_t)(i - 3) * 8;
                    enc.mov_rm_rbp(x86::RAX, callerOff);
                    enc.mov_mr_rbp(refSlot, x86::RAX);
                }
                enc.mov_rm_rbp(x86::RAX, refSlot);
                enc.mov_rm(x86::RAX, x86::RAX, 0);
                enc.mov_mr_rbp(off, x86::RAX);
            } else if (i < 3) {
                if (pt == ThavaType::FLOAT || pt == ThavaType::DOUBLE) {
                    enc.movsd_mr_rbp(off, xmmRegs[i]);
                } else {
                    if (pt == ThavaType::INT) narrowIntReg(intRegs[i + 1], pct);
                    enc.mov_mr_rbp(off, intRegs[i + 1]);
                }
            } else {
                // Extra parameters arrive on the caller's stack (Win64 slots).
                int32_t callerOff = 48 + (int32_t)(i - 3) * 8;
                if (pt == ThavaType::FLOAT || pt == ThavaType::DOUBLE) {
                    enc.movsd_rm_rbp(x86::XMM0, callerOff);
                    enc.movsd_mr_rbp(off, x86::XMM0);
                } else {
                    enc.mov_rm_rbp(x86::RAX, callerOff);
                    if (pt == ThavaType::INT) narrowIntReg(x86::RAX, pct);
                    enc.mov_mr_rbp(off, x86::RAX);
                }
            }
        }

        for (const auto& s : body) emitStmt(s.get());

        enc.bindLabel(currentFuncRetLabel);

        // Copy collection parameters back to the caller (same rule as functions).
        for (size_t i = 0; i < params.size(); ++i) {
            if (!isCollectionType(params[i].first)) continue;
            VarInfo* pv = lookupVar(params[i].second);
            if (!pv || !pv->listOut) continue;
            enc.mov_rm_rbp(x86::R10, pv->offset);
            enc.mov_rm_rbp(x86::R11, pv->listOutSlot);
            enc.mov_mr(x86::R11, 0, x86::R10);
        }

        exitFunctionScope();
        enc.add_rsp(2048);
        enc.pop(x86::RBP);
        enc.ret();

        currentClassName = prevClass;
        currentBaseClassName = prevBase;
    }

    // Push 'this' + call arguments into the method's register parameters.
    void emitClassCallArgs(size_t labelKeySlot, const std::string& key,
                           const std::vector<ASTPtr>& args, int32_t thisSlot,
                           const std::vector<int32_t>& argSlots,
                           const std::vector<ThavaType>& argTypes) {
        x86::Reg intRegs[] = { x86::RCX, x86::RDX, x86::R8, x86::R9 };
        x86::Xmm xmmRegs[] = { x86::XMM0, x86::XMM1, x86::XMM2, x86::XMM3 };
        std::vector<std::pair<ThavaType, std::string>> decl;
        auto pit = classMethodParams.find(key);
        if (pit != classMethodParams.end()) decl = pit->second;

        enc.mov_rm_rbp(x86::RCX, thisSlot);
        for (size_t i = 0; i < args.size(); ++i) {
            ThavaType want = (i < decl.size()) ? decl[i].first : argTypes[i];
            bool isFloat = (want == ThavaType::FLOAT || want == ThavaType::DOUBLE);
            if (isCollectionType(want)) {
                // Collections are passed as the address of the caller's variable.
                VarInfo* av = nullptr;
                if (i < args.size()) {
                    if (auto vn = dynamic_cast<VarNode*>(args[i].get()))
                        av = lookupVar(vn->getName());
                }
                int32_t addrSlot = av ? av->offset : argSlots[i];
                if (i < 3) {
                    enc.lea_rm_rbp(intRegs[i + 1], addrSlot);
                } else {
                    int32_t stackDisp = 32 + (int32_t)(i - 3) * 8;
                    enc.lea_rm_rbp(x86::R10, addrSlot);
                    enc.mov_mr_rsp(stackDisp, x86::R10);
                }
            } else if (i < 3) {
                if (isFloat) {
                    if (argTypes[i] == ThavaType::INT || argTypes[i] == ThavaType::BOOL) {
                        enc.mov_rm_rbp(x86::RAX, argSlots[i]);
                        enc.cvtsi2sd(xmmRegs[i], x86::RAX);
                    } else {
                        enc.movsd_rm_rbp(xmmRegs[i], argSlots[i]);
                    }
                } else {
                    enc.mov_rm_rbp(intRegs[i + 1], argSlots[i]);
                }
            } else {
                int32_t stackDisp = 32 + (int32_t)(i - 3) * 8;
                if (isFloat) {
                    enc.movsd_rm_rbp(x86::XMM4, argSlots[i]);
                    enc.movsd_mr_rsp(stackDisp, x86::XMM4);
                } else {
                    enc.mov_rm_rbp(x86::R10, argSlots[i]);
                    enc.mov_mr_rsp(stackDisp, x86::R10);
                }
            }
        }
        enc.call_label(labelKeySlot);
    }

    void emitStmt(ASTNode* node) {
        if (!node) return;

        // --- MemberAssignNode (Gan thuoc tinh struct: obj.field = val / += / -= ...) ---
        if (auto massign = dynamic_cast<MemberAssignNode*>(node)) {
            std::string structName = structNameOfTarget(massign->target.get());

            auto sit = g_structs.find(structName);
            if (sit == g_structs.end()) {
                std::cerr << "[Native Codegen Error]: Struct not found '" << structName << "' for assignment '" << massign->memberName << "'!\n";
                exit(1);
            }
            const auto& sinfo = sit->second;
            auto fit = sinfo.fieldIndex.find(massign->memberName);
            if (fit == sinfo.fieldIndex.end()) {
                std::cerr << "[Native Codegen Error]: Struct '" << structName << "' has no member '" << massign->memberName << "'!\n";
                exit(1);
            }
            const auto& fld = sinfo.fields[fit->second];

            // 1. Evaluate target pointer and store in temporary slot
            emitExpr(massign->target.get());
            currentFrameOffset -= 8;
            if (currentFrameOffset < maxFrameOffset) maxFrameOffset = currentFrameOffset;
            int32_t targetSlot = currentFrameOffset;
            enc.mov_mr_rbp(targetSlot, x86::RAX);

            // 2. Evaluate right-hand side expression
            ThavaType exprType = emitExpr(massign->expr.get());

            // 3. Reload target pointer back into RCX
            enc.mov_rm_rbp(x86::RCX, targetSlot);

            // 4. Perform assignment
            if (fld.type == ThavaType::DOUBLE || fld.type == ThavaType::FLOAT) {
                if (exprType == ThavaType::INT) enc.cvtsi2sd(x86::XMM0, x86::RAX);
                if (massign->op == "=") {
                    enc.movsd_mr(x86::RCX, fld.offset, x86::XMM0);
                } else if (massign->op == "+=") {
                    enc.movsd_rm(x86::XMM1, x86::RCX, fld.offset);
                    enc.addsd_rr(x86::XMM1, x86::XMM0);
                    enc.movsd_mr(x86::RCX, fld.offset, x86::XMM1);
                } else if (massign->op == "-=") {
                    enc.movsd_rm(x86::XMM1, x86::RCX, fld.offset);
                    enc.subsd_rr(x86::XMM1, x86::XMM0);
                    enc.movsd_mr(x86::RCX, fld.offset, x86::XMM1);
                } else if (massign->op == "*=") {
                    enc.movsd_rm(x86::XMM1, x86::RCX, fld.offset);
                    enc.mulsd_rr(x86::XMM1, x86::XMM0);
                    enc.movsd_mr(x86::RCX, fld.offset, x86::XMM1);
                } else if (massign->op == "/=") {
                    enc.movsd_rm(x86::XMM1, x86::RCX, fld.offset);
                    enc.divsd_rr(x86::XMM1, x86::XMM0);
                    enc.movsd_mr(x86::RCX, fld.offset, x86::XMM1);
                }
            } else {
                if (massign->op == "=") {
                    if (fld.type == ThavaType::INT) narrowIntReg(x86::RAX, fld.customType);
                    enc.mov_mr(x86::RCX, fld.offset, x86::RAX);
                } else if (massign->op == "+=") {
                    enc.mov_rm(x86::RDX, x86::RCX, fld.offset);
                    enc.add_rr(x86::RDX, x86::RAX);
                    if (fld.type == ThavaType::INT) narrowIntReg(x86::RDX, fld.customType);
                    enc.mov_mr(x86::RCX, fld.offset, x86::RDX);
                } else if (massign->op == "-=") {
                    enc.mov_rm(x86::RDX, x86::RCX, fld.offset);
                    enc.sub_rr(x86::RDX, x86::RAX);
                    if (fld.type == ThavaType::INT) narrowIntReg(x86::RDX, fld.customType);
                    enc.mov_mr(x86::RCX, fld.offset, x86::RDX);
                } else if (massign->op == "*=") {
                    enc.mov_rm(x86::RDX, x86::RCX, fld.offset);
                    enc.imul_rr(x86::RDX, x86::RAX);
                    if (fld.type == ThavaType::INT) narrowIntReg(x86::RDX, fld.customType);
                    enc.mov_mr(x86::RCX, fld.offset, x86::RDX);
                }
            }
            return;
        }

        if (auto vdecl = dynamic_cast<VarDeclNode*>(node)) {
            int32_t offset;
            if (emittingProgramBody && globalVarSlots.count(vdecl->varName)) {
                // Global variable: its slot was reserved in the program scope. A
                // local declaration with the same name inside a function shadows it.
                offset = globalVarSlots[vdecl->varName];
            } else {
                offset = declareVar(vdecl->varName, vdecl->varType, vdecl->customTypeName);
            }
            if (vdecl->initExpr) {
                if (auto fm = dynamic_cast<FileMethodNode*>(vdecl->initExpr.get())) {
                    if (fm->method == "read_lines" || fm->method == "readlines") {
                        listElemTypes[vdecl->varName] = ThavaType::STRING;
                    }
                }
                if (auto sam = dynamic_cast<StrAdvMethodNode*>(vdecl->initExpr.get())) {
                    if (sam->method == "split") {
                        listElemTypes[vdecl->varName] = ThavaType::STRING;
                    }
                }
                ThavaType t = emitExpr(vdecl->initExpr.get());
                if (vdecl->varType == ThavaType::LIST || vdecl->varType == ThavaType::TUPLE) {
                    if (lastCollectionElemType != ThavaType::UNKNOWN) {
                        listElemTypes[vdecl->varName] = lastCollectionElemType;
                        lastCollectionElemType = ThavaType::UNKNOWN;
                    }
                }
                if (vdecl->varType == ThavaType::FLOAT || vdecl->varType == ThavaType::DOUBLE) {
                    if (t == ThavaType::INT) enc.cvtsi2sd(x86::XMM0, x86::RAX);
                    enc.movsd_mr_rbp(offset, x86::XMM0);
                    invalidateRegCache();
                } else {
                    if (vdecl->varType == ThavaType::INT && (t == ThavaType::FLOAT || t == ThavaType::DOUBLE)) {
                        enc.cvttsd2si(x86::RAX, x86::XMM0);
                    }
                    if (vdecl->varType == ThavaType::INT) narrowIntReg(x86::RAX, vdecl->customTypeName);
                    enc.mov_mr_rbp(offset, x86::RAX);
                }
            } else if (g_structs.count(vdecl->customTypeName)) {
                const auto& sinfo = g_structs[vdecl->customTypeName];
                enc.mov_ri64(x86::RCX, sinfo.totalSize);
                enc.call_iat_func("malloc");
                currentFrameOffset -= 8;
                if (currentFrameOffset < maxFrameOffset) maxFrameOffset = currentFrameOffset;
                int32_t structPtrSlot = currentFrameOffset;
                enc.mov_mr_rbp(structPtrSlot, x86::RAX);
                enc.mov_rr(x86::RCX, x86::RAX);
                enc.xor_r32(x86::RDX, x86::RDX);
                enc.mov_ri64(x86::R8, sinfo.totalSize);
                enc.call_iat_func("memset");
                enc.mov_rm_rbp(x86::RAX, structPtrSlot);
                enc.mov_mr_rbp(offset, x86::RAX);
                invalidateRegCache();
            }
            return;
        }

        if (auto assign = dynamic_cast<AssignNode*>(node)) {
            VarInfo* vi = lookupVar(assign->name);
            if (!vi && isGlobalName(assign->name)) {
                ThavaType gt = globalEnv->get(assign->name);
                std::string gct = globalEnv->getCustomType(assign->name);
                ThavaType t = emitExpr(assign->expr.get());
                emitGlobalBase(x86::R11, globalVarSlots.at(assign->name));
                if (gt == ThavaType::FLOAT || gt == ThavaType::DOUBLE) {
                    if (t == ThavaType::INT || t == ThavaType::BOOL) enc.cvtsi2sd(x86::XMM0, x86::RAX);
                    enc.movsd_mr(x86::R11, 0, x86::XMM0);
                } else {
                    if (gt == ThavaType::INT && (t == ThavaType::FLOAT || t == ThavaType::DOUBLE))
                        enc.cvttsd2si(x86::RAX, x86::XMM0);
                    if (gt == ThavaType::INT) narrowIntReg(x86::RAX, gct);
                    enc.mov_mr(x86::R11, 0, x86::RAX);
                }
                invalidateRegCache();
                return;
            }
            if (!vi) {
                std::cerr << "[Native Codegen Error]: Variable '" << assign->name << "' is not declared!\n";
                exit(1);
            }
            if (auto fm = dynamic_cast<FileMethodNode*>(assign->expr.get())) {
                if (fm->method == "read_lines" || fm->method == "readlines") {
                    listElemTypes[assign->name] = ThavaType::STRING;
                }
            }
            if (auto sam = dynamic_cast<StrAdvMethodNode*>(assign->expr.get())) {
                if (sam->method == "split") {
                    listElemTypes[assign->name] = ThavaType::STRING;
                }
            }
            ThavaType t = emitExpr(assign->expr.get());
            if (vi && (vi->type == ThavaType::LIST || vi->type == ThavaType::TUPLE)) {
                if (lastCollectionElemType != ThavaType::UNKNOWN) {
                    listElemTypes[assign->name] = lastCollectionElemType;
                    lastCollectionElemType = ThavaType::UNKNOWN;
                }
            }
            if (assign->op == "=") {
                if (vi->type == ThavaType::FLOAT || vi->type == ThavaType::DOUBLE) {
                    if (t == ThavaType::INT) enc.cvtsi2sd(x86::XMM0, x86::RAX);
                    enc.movsd_mr_rbp(vi->offset, x86::XMM0);
                    invalidateRegCache();
                } else {
                    if (vi->type == ThavaType::INT && (t == ThavaType::FLOAT || t == ThavaType::DOUBLE)) {
                        enc.cvttsd2si(x86::RAX, x86::XMM0);
                    }
                    storeVarFromReg(vi->offset, x86::RAX, vi);
                }
            } else if (assign->op == "+=") {
                if (vi->type == ThavaType::STRING) {
                    // `s += x` on a string concatenates; treating the pointers as
                    // integers used to store a bogus pointer and crash on print.
                    if (t != ThavaType::STRING) {
                        std::cerr << "[Native Codegen Error]: '+=' on <str> variable '"
                                  << assign->name << "' needs a <str> value.\n";
                        exit(1);
                    }
                    enc.mov_rr(x86::RDX, x86::RAX);
                    enc.mov_rm_rbp(x86::RCX, vi->offset);
                    enc.call_label(lblStrConcat);
                    storeVarFromReg(vi->offset, x86::RAX, vi);
                    invalidateRegCache();
                } else if (vi->type == ThavaType::FLOAT || vi->type == ThavaType::DOUBLE) {
                    if (t == ThavaType::INT) enc.cvtsi2sd(x86::XMM0, x86::RAX);
                    enc.movsd_rm_rbp(x86::XMM1, vi->offset);
                    enc.addsd_rr(x86::XMM1, x86::XMM0);
                    enc.movsd_mr_rbp(vi->offset, x86::XMM1);
                    invalidateRegCache();
                } else {
                    enc.mov_rm_rbp(x86::RCX, vi->offset);
                    enc.add_rr(x86::RCX, x86::RAX);
                    storeVarFromReg(vi->offset, x86::RCX, vi);
                    invalidateRegCache();
                }
            } else if (assign->op == "-=") {
                if (vi->type == ThavaType::STRING) {
                    std::cerr << "[Native Codegen Error]: operator '-=' is not defined on <str> variable '"
                              << assign->name << "'.\n";
                    exit(1);
                }
                if (vi->type == ThavaType::FLOAT || vi->type == ThavaType::DOUBLE) {
                    if (t == ThavaType::INT) enc.cvtsi2sd(x86::XMM0, x86::RAX);
                    enc.movsd_rm_rbp(x86::XMM1, vi->offset);
                    enc.subsd_rr(x86::XMM1, x86::XMM0);
                    enc.movsd_mr_rbp(vi->offset, x86::XMM1);
                    invalidateRegCache();
                } else {
                    enc.mov_rm_rbp(x86::RCX, vi->offset);
                    enc.sub_rr(x86::RCX, x86::RAX);
                    storeVarFromReg(vi->offset, x86::RCX, vi);
                    invalidateRegCache();
                }
            } else if (assign->op == "*=") {
                if (vi->type == ThavaType::STRING) {
                    // String repetition: `s *= 3`, same helper as `s * 3`.
                    if (t != ThavaType::INT) {
                        std::cerr << "[Native Codegen Error]: '*=' on <str> variable '"
                                  << assign->name << "' needs an <int> count.\n";
                        exit(1);
                    }
                    enc.mov_rr(x86::RDX, x86::RAX);
                    enc.mov_rm_rbp(x86::RCX, vi->offset);
                    enc.call_label(lblStrMul);
                    storeVarFromReg(vi->offset, x86::RAX, vi);
                    invalidateRegCache();
                } else if (vi->type == ThavaType::FLOAT || vi->type == ThavaType::DOUBLE) {
                    if (t == ThavaType::INT) enc.cvtsi2sd(x86::XMM0, x86::RAX);
                    enc.movsd_rm_rbp(x86::XMM1, vi->offset);
                    enc.mulsd_rr(x86::XMM1, x86::XMM0);
                    enc.movsd_mr_rbp(vi->offset, x86::XMM1);
                    invalidateRegCache();
                } else {
                    enc.mov_rm_rbp(x86::RCX, vi->offset);
                    enc.imul_rr(x86::RCX, x86::RAX);
                    storeVarFromReg(vi->offset, x86::RCX, vi);
                    invalidateRegCache();
                }
            } else if (assign->op == "/=") {
                if (vi->type == ThavaType::STRING) {
                    std::cerr << "[Native Codegen Error]: operator '/=' is not defined on <str> variable '"
                              << assign->name << "'.\n";
                    exit(1);
                }
                if (vi->type == ThavaType::FLOAT || vi->type == ThavaType::DOUBLE) {
                    if (t == ThavaType::INT) enc.cvtsi2sd(x86::XMM0, x86::RAX);
                    enc.movsd_rm_rbp(x86::XMM1, vi->offset);
                    enc.divsd_rr(x86::XMM1, x86::XMM0);
                    enc.movsd_mr_rbp(vi->offset, x86::XMM1);
                    invalidateRegCache();
                } else {
                    enc.mov_rr(x86::R10, x86::RAX);
                    enc.mov_rm_rbp(x86::RAX, vi->offset);
                    enc.cqo();
                    enc.idiv_r(x86::R10);
                    storeVarFromReg(vi->offset, x86::RAX, vi);
                    invalidateRegCache();
                }
            } else if (assign->op == "%=") {
                if (vi->type == ThavaType::STRING) {
                    std::cerr << "[Native Codegen Error]: operator '%=' is not defined on <str> variable '"
                              << assign->name << "'.\n";
                    exit(1);
                }
                enc.mov_rr(x86::R10, x86::RAX);
                enc.mov_rm_rbp(x86::RAX, vi->offset);
                enc.cqo();
                enc.idiv_r(x86::R10);
                storeVarFromReg(vi->offset, x86::RDX, vi);
                invalidateRegCache();
            } else if (assign->op == "**=") {
                enc.mov_rr(x86::R10, x86::RAX);
                enc.mov_rm_rbp(x86::RAX, vi->offset);

                size_t powLoop = enc.createLabel();
                size_t powDone = enc.createLabel();
                size_t powNext = enc.createLabel();

                currentFrameOffset -= 8;
                if (currentFrameOffset < maxFrameOffset) maxFrameOffset = currentFrameOffset;
                int32_t baseSlot = currentFrameOffset;
                enc.mov_mr_rbp(baseSlot, x86::RAX);

                currentFrameOffset -= 8;
                if (currentFrameOffset < maxFrameOffset) maxFrameOffset = currentFrameOffset;
                int32_t expSlot = currentFrameOffset;
                enc.mov_mr_rbp(expSlot, x86::R10);

                currentFrameOffset -= 8;
                if (currentFrameOffset < maxFrameOffset) maxFrameOffset = currentFrameOffset;
                int32_t resSlot = currentFrameOffset;
                enc.mov_ri64(x86::RAX, 1);
                enc.mov_mr_rbp(resSlot, x86::RAX);

                enc.bindLabel(powLoop);
                enc.mov_rm_rbp(x86::R10, expSlot);
                enc.mov_ri64(x86::R11, 0);
                enc.cmp_rr(x86::R10, x86::R11);
                enc.jle(powDone);

                enc.mov_rm_rbp(x86::R10, expSlot);
                enc.mov_ri64(x86::R11, 1);
                enc.and_rr(x86::R10, x86::R11);
                enc.test_rr(x86::R10, x86::R10);
                enc.je(powNext);

                enc.mov_rm_rbp(x86::RAX, resSlot);
                enc.mov_rm_rbp(x86::R11, baseSlot);
                enc.imul_rr(x86::RAX, x86::R11);
                enc.mov_mr_rbp(resSlot, x86::RAX);

                enc.bindLabel(powNext);
                enc.mov_rm_rbp(x86::RAX, baseSlot);
                enc.mov_rm_rbp(x86::R11, baseSlot);
                enc.imul_rr(x86::RAX, x86::R11);
                enc.mov_mr_rbp(baseSlot, x86::RAX);

                enc.mov_rm_rbp(x86::RAX, expSlot);
                enc.shr_ri(x86::RAX, 1);
                enc.mov_mr_rbp(expSlot, x86::RAX);
                enc.jmp(powLoop);

                enc.bindLabel(powDone);
                enc.mov_rm_rbp(x86::RAX, resSlot);
                storeVarFromReg(vi->offset, x86::RAX, vi);
            } else {
                storeVarFromReg(vi->offset, x86::RAX, vi);
            }
            return;
        }

        if (auto idxAssign = dynamic_cast<IndexAssignNode*>(node)) {
            VarInfo* vi = lookupVar(idxAssign->varName);
            if (!vi && !isGlobalName(idxAssign->varName)) {
                std::cerr << "[Native Codegen Error]: Variable '" << idxAssign->varName << "' is not declared!\n";
                exit(1);
            }
            if ((vi ? vi->type : globalEnv->get(idxAssign->varName)) == ThavaType::DICT) {
                ThavaType kt = emitExpr(idxAssign->indexExpr.get());
                currentFrameOffset -= 8;
                if (currentFrameOffset < maxFrameOffset) maxFrameOffset = currentFrameOffset;
                int32_t keySlot = currentFrameOffset;
                enc.mov_mr_rbp(keySlot, x86::RAX);

                ThavaType vt = emitExpr(idxAssign->valExpr.get());
                currentFrameOffset -= 8;
                if (currentFrameOffset < maxFrameOffset) maxFrameOffset = currentFrameOffset;
                int32_t valSlot = currentFrameOffset;
                enc.mov_mr_rbp(valSlot, x86::RAX);

                loadCollectionPtr(idxAssign->varName, vi, x86::R10);
                size_t searchLoop = enc.createLabel();
                size_t foundLbl = enc.createLabel();
                size_t notFoundLbl = enc.createLabel();
                size_t endAssign = enc.createLabel();

                currentFrameOffset -= 8;
                if (currentFrameOffset < maxFrameOffset) maxFrameOffset = currentFrameOffset;
                int32_t iSlot = currentFrameOffset;
                enc.mov_ri64(x86::RAX, 0);
                enc.mov_mr_rbp(iSlot, x86::RAX);

                enc.bindLabel(searchLoop);
                enc.mov_rm_rbp(x86::RAX, iSlot);
                loadCollectionPtr(idxAssign->varName, vi, x86::R10);
                enc.mov_rm(x86::R11, x86::R10, 0);
                enc.cmp_rr(x86::RAX, x86::R11);
                enc.jge(notFoundLbl);

                enc.mov_ri64(x86::R11, 32);
                enc.imul_rr(x86::RAX, x86::R11);
                enc.add_rr(x86::R10, x86::RAX);
                if (kt == ThavaType::STRING) {
                    enc.mov_rm(x86::RCX, x86::R10, 16);
                    enc.mov_rm_rbp(x86::RDX, keySlot);
                    enc.call_iat_func("strcmp");
                    enc.test_rr(x86::RAX, x86::RAX);
                    enc.je(foundLbl);
                } else {
                    enc.mov_rm(x86::RAX, x86::R10, 16);
                    enc.mov_rm_rbp(x86::R11, keySlot);
                    enc.cmp_rr(x86::RAX, x86::R11);
                    enc.je(foundLbl);
                }

                enc.mov_rm_rbp(x86::RAX, iSlot);
                enc.mov_ri64(x86::R11, 1);
                enc.add_rr(x86::RAX, x86::R11);
                enc.mov_mr_rbp(iSlot, x86::RAX);
                enc.jmp(searchLoop);

                enc.bindLabel(foundLbl);
                enc.mov_rm_rbp(x86::RAX, iSlot);
                enc.mov_ri64(x86::R11, 32);
                enc.imul_rr(x86::RAX, x86::R11);
                loadCollectionPtr(idxAssign->varName, vi, x86::R10);
                enc.add_rr(x86::R10, x86::RAX);
                enc.mov_rm_rbp(x86::RAX, valSlot);
                if (idxAssign->op == "+=") {
                    enc.mov_rm(x86::RCX, x86::R10, 24);
                    enc.add_rr(x86::RCX, x86::RAX);
                    enc.mov_mr(x86::R10, 24, x86::RCX);
                } else if (idxAssign->op == "-=") {
                    enc.mov_rm(x86::RCX, x86::R10, 24);
                    enc.sub_rr(x86::RCX, x86::RAX);
                    enc.mov_mr(x86::R10, 24, x86::RCX);
                } else {
                    enc.mov_mr(x86::R10, 24, x86::RAX);
                }
                enc.jmp(endAssign);

                enc.bindLabel(notFoundLbl);
                loadCollectionPtr(idxAssign->varName, vi, x86::R10);
                enc.mov_rm(x86::RAX, x86::R10, 0); // count
                enc.mov_ri64(x86::R11, 32);
                enc.imul_rr(x86::RAX, x86::R11);
                enc.add_rr(x86::R10, x86::RAX);
                enc.mov_rm_rbp(x86::R11, keySlot);
                enc.mov_mr(x86::R10, 16, x86::R11); // key
                enc.mov_rm_rbp(x86::RAX, valSlot);
                enc.mov_mr(x86::R10, 24, x86::RAX); // val
                enc.mov_ri64(x86::RAX, (int)kt);
                enc.mov_mr(x86::R10, 32, x86::RAX); // keyType
                enc.mov_ri64(x86::RAX, (int)vt);
                enc.mov_mr(x86::R10, 40, x86::RAX); // valType

                // count++
                loadCollectionPtr(idxAssign->varName, vi, x86::R10);
                enc.mov_rm(x86::RAX, x86::R10, 0);
                enc.mov_ri64(x86::R11, 1);
                enc.add_rr(x86::RAX, x86::R11);
                enc.mov_mr(x86::R10, 0, x86::RAX);

                enc.bindLabel(endAssign);
                return;
            }

            ThavaType elemType = ThavaType::INT;
            auto it = listElemTypes.find(idxAssign->varName);
            if (it != listElemTypes.end()) elemType = it->second;

            emitExpr(idxAssign->indexExpr.get());
            currentFrameOffset -= 8;
            if (currentFrameOffset < maxFrameOffset) maxFrameOffset = currentFrameOffset;
            int32_t idxSlot = currentFrameOffset;
            enc.mov_mr_rbp(idxSlot, x86::RAX);

            ThavaType vt = emitExpr(idxAssign->valExpr.get());
            if (elemType == ThavaType::INT && (vt == ThavaType::FLOAT || vt == ThavaType::DOUBLE)) {
                elemType = ThavaType::DOUBLE;
                listElemTypes[idxAssign->varName] = ThavaType::DOUBLE;
            }

            loadCollectionPtr(idxAssign->varName, vi, x86::R10); // list ptr in R10
            enc.mov_rm_rbp(x86::R11, idxSlot);    // index in R11
            enc.mov_ri64(x86::RDX, 8);
            enc.imul_rr(x86::R11, x86::RDX);      // idx * 8
            enc.add_rr(x86::R10, x86::R11);       // list + idx * 8

            if (elemType == ThavaType::FLOAT || elemType == ThavaType::DOUBLE) {
                if (vt == ThavaType::INT) enc.cvtsi2sd(x86::XMM0, x86::RAX);
                if (idxAssign->op == "+=") {
                    enc.movsd_rm(x86::XMM1, x86::R10, 8);
                    enc.addsd_rr(x86::XMM1, x86::XMM0);
                    enc.movsd_mr(x86::R10, 8, x86::XMM1);
                } else if (idxAssign->op == "-=") {
                    enc.movsd_rm(x86::XMM1, x86::R10, 8);
                    enc.subsd_rr(x86::XMM1, x86::XMM0);
                    enc.movsd_mr(x86::R10, 8, x86::XMM1);
                } else if (idxAssign->op == "*=") {
                    enc.movsd_rm(x86::XMM1, x86::R10, 8);
                    enc.mulsd_rr(x86::XMM1, x86::XMM0);
                    enc.movsd_mr(x86::R10, 8, x86::XMM1);
                } else if (idxAssign->op == "/=") {
                    enc.movsd_rm(x86::XMM1, x86::R10, 8);
                    enc.divsd_rr(x86::XMM1, x86::XMM0);
                    enc.movsd_mr(x86::R10, 8, x86::XMM1);
                } else {
                    enc.movsd_mr(x86::R10, 8, x86::XMM0);
                }
            } else {
                if (idxAssign->op == "+=") {
                    enc.mov_rm(x86::RCX, x86::R10, 8);
                    enc.add_rr(x86::RCX, x86::RAX);
                    enc.mov_mr(x86::R10, 8, x86::RCX);
                } else if (idxAssign->op == "-=") {
                    enc.mov_rm(x86::RCX, x86::R10, 8);
                    enc.sub_rr(x86::RCX, x86::RAX);
                    enc.mov_mr(x86::R10, 8, x86::RCX);
                } else if (idxAssign->op == "*=") {
                    enc.mov_rm(x86::RCX, x86::R10, 8);
                    enc.imul_rr(x86::RCX, x86::RAX);
                    enc.mov_mr(x86::R10, 8, x86::RCX);
                } else if (idxAssign->op == "/=") {
                    enc.mov_rr(x86::R11, x86::RAX);
                    enc.mov_rm(x86::RAX, x86::R10, 8);
                    enc.cqo();
                    enc.idiv_r(x86::R11);
                    enc.mov_mr(x86::R10, 8, x86::RAX);
                } else if (idxAssign->op == "%=") {
                    enc.mov_rr(x86::R11, x86::RAX);
                    enc.mov_rm(x86::RAX, x86::R10, 8);
                    enc.cqo();
                    enc.idiv_r(x86::R11);
                    enc.mov_mr(x86::R10, 8, x86::RDX);
                } else {
                    enc.mov_mr(x86::R10, 8, x86::RAX);    // [list + 8 + idx * 8] = val
                }
            }
            return;
        }

        if (auto block = dynamic_cast<BlockNode*>(node)) {
            enterScope();
            for (const auto& s : block->statements) emitStmt(s.get());
            exitScope();
            return;
        }

        if (auto ifNode = dynamic_cast<IfNode*>(node)) {
            size_t elseLbl = enc.createLabel();
            size_t endLbl = enc.createLabel();

            emitExpr(ifNode->cond.get());
            enc.test_rr(x86::RAX, x86::RAX);
            enc.je(elseLbl);

            emitStmt(ifNode->thenBranch.get());
            enc.jmp(endLbl);

            enc.bindLabel(elseLbl);
            if (ifNode->elseBranch) {
                emitStmt(ifNode->elseBranch.get());
            }
            enc.bindLabel(endLbl);
            return;
        }

        if (auto whileNode = dynamic_cast<WhileNode*>(node)) {
            size_t startLbl = enc.createLabel();
            size_t endLbl = enc.createLabel();

            breakLabels.push_back(endLbl);
            continueLabels.push_back(startLbl);

            enc.bindLabel(startLbl);
            emitExpr(whileNode->cond.get());
            enc.test_rr(x86::RAX, x86::RAX);
            enc.je(endLbl);

            emitStmt(whileNode->body.get());
            enc.jmp(startLbl);

            enc.bindLabel(endLbl);
            breakLabels.pop_back();
            continueLabels.pop_back();
            return;
        }

        if (auto forNode = dynamic_cast<ForNode*>(node)) {
            enterScope();
            if (forNode->init) emitStmt(forNode->init.get());

            size_t startLbl = enc.createLabel();
            size_t stepLbl = enc.createLabel();
            size_t endLbl = enc.createLabel();

            breakLabels.push_back(endLbl);
            continueLabels.push_back(stepLbl);

            enc.bindLabel(startLbl);
            if (forNode->cond) {
                emitExpr(forNode->cond.get());
                enc.test_rr(x86::RAX, x86::RAX);
                enc.je(endLbl);
            }

            emitStmt(forNode->body.get());

            enc.bindLabel(stepLbl);
            if (forNode->update) emitStmt(forNode->update.get());
            enc.jmp(startLbl);

            enc.bindLabel(endLbl);
            breakLabels.pop_back();
            continueLabels.pop_back();
            exitScope();
            return;
        }

        if (auto rangeFor = dynamic_cast<RangeForNode*>(node)) {
            enterScope();
            int32_t varOff = declareVar(rangeFor->varName, rangeFor->varType);
            int32_t stepOff = declareVar("_thava_step_" + rangeFor->varName, ThavaType::INT);
            int32_t stopOff = declareVar("_thava_stop_" + rangeFor->varName, ThavaType::INT);

            emitExpr(rangeFor->startExpr.get());
            enc.mov_mr_rbp(varOff, x86::RAX);

            emitExpr(rangeFor->stepExpr.get());
            enc.mov_mr_rbp(stepOff, x86::RAX);

            emitExpr(rangeFor->stopExpr.get());
            enc.mov_mr_rbp(stopOff, x86::RAX);

            size_t startLbl = enc.createLabel();
            size_t stepLbl  = enc.createLabel();
            size_t endLbl   = enc.createLabel();
            size_t negBranch = enc.createLabel();

            breakLabels.push_back(endLbl);
            continueLabels.push_back(stepLbl);

            enc.bindLabel(startLbl);
            enc.mov_rm_rbp(x86::RAX, stepOff);
            enc.mov_ri64(x86::R10, 0);
            enc.cmp_rr(x86::RAX, x86::R10);
            enc.jle(negBranch);

            // step > 0: var < stop
            enc.mov_rm_rbp(x86::RAX, varOff);
            enc.mov_rm_rbp(x86::R10, stopOff);
            enc.cmp_rr(x86::RAX, x86::R10);
            enc.jge(endLbl);
            size_t bodyLbl = enc.createLabel();
            enc.jmp(bodyLbl);

            // step <= 0: var > stop
            enc.bindLabel(negBranch);
            enc.mov_rm_rbp(x86::RAX, varOff);
            enc.mov_rm_rbp(x86::R10, stopOff);
            enc.cmp_rr(x86::RAX, x86::R10);
            enc.jle(endLbl);

            enc.bindLabel(bodyLbl);
            emitStmt(rangeFor->body.get());

            enc.bindLabel(stepLbl);
            enc.mov_rm_rbp(x86::RAX, varOff);
            enc.mov_rm_rbp(x86::R10, stepOff);
            enc.add_rr(x86::RAX, x86::R10);
            enc.mov_mr_rbp(varOff, x86::RAX);
            enc.jmp(startLbl);

            enc.bindLabel(endLbl);
            breakLabels.pop_back();
            continueLabels.pop_back();
            exitScope();
            return;
        }

        if (auto b = dynamic_cast<BreakNode*>(node)) {
            if (breakLabels.empty()) {
                std::cerr << "Native Compilation Error: 'break' can only be used inside loops!\n";
                exit(1);
            }
            enc.jmp(breakLabels.back());
            return;
        }

        if (auto c = dynamic_cast<ContinueNode*>(node)) {
            if (continueLabels.empty()) {
                std::cerr << "Native Compilation Error: 'continue' can only be used inside loops!\n";
                exit(1);
            }
            enc.jmp(continueLabels.back());
            return;
        }

        if (auto forEach = dynamic_cast<ForEachNode*>(node)) {
            enterScope();
            ThavaType collType = emitExpr(forEach->collectionExpr.get());

            int32_t collSlot = declareVar("_thava_coll_" + forEach->varName, ThavaType::INT);
            enc.mov_mr_rbp(collSlot, x86::RAX);

            int32_t lenSlot = declareVar("_thava_len_" + forEach->varName, ThavaType::INT);
            if (collType == ThavaType::STRING) {
                enc.mov_rr(x86::RCX, x86::RAX);
                enc.call_iat_func("strlen");
            } else {
                enc.mov_rm(x86::RAX, x86::RAX, 0); // Count o offset 0
            }
            enc.mov_mr_rbp(lenSlot, x86::RAX);

            int32_t idxSlot = declareVar("_thava_idx_" + forEach->varName, ThavaType::INT);
            enc.mov_ri64(x86::RAX, 0);
            enc.mov_mr_rbp(idxSlot, x86::RAX);

            ThavaType itemType = forEach->varType;
            if (auto v = dynamic_cast<VarNode*>(forEach->collectionExpr.get())) {
                auto it = listElemTypes.find(v->getName());
                if (it != listElemTypes.end()) itemType = it->second;
            } else if (auto sam = dynamic_cast<StrAdvMethodNode*>(forEach->collectionExpr.get())) {
                if (sam->method == "split") itemType = ThavaType::STRING;
            }
            if (itemType == ThavaType::UNKNOWN) {
                itemType = (collType == ThavaType::STRING ? ThavaType::STRING : ThavaType::INT);
            }
            int32_t varSlot = declareVar(forEach->varName, itemType);

            size_t startLbl = enc.createLabel();
            size_t stepLbl  = enc.createLabel();
            size_t endLbl   = enc.createLabel();

            breakLabels.push_back(endLbl);
            continueLabels.push_back(stepLbl);

            enc.bindLabel(startLbl);
            enc.mov_rm_rbp(x86::RAX, idxSlot);
            enc.mov_rm_rbp(x86::R10, lenSlot);
            enc.cmp_rr(x86::RAX, x86::R10);
            enc.jge(endLbl);

            enc.mov_rm_rbp(x86::R10, collSlot);
            if (collType == ThavaType::STRING) {
                enc.mov_rm_rbp(x86::RAX, idxSlot);
                enc.add_rr(x86::R10, x86::RAX);
                enc.movzx_rm8(x86::RAX, x86::R10, 0);
                if (itemType == ThavaType::STRING) {
                    enc.push(x86::RAX);
                    enc.mov_ri64(x86::RCX, 2);
                    enc.call_iat_func("malloc");
                    enc.pop(x86::R10);
                    enc.mov_mr8(x86::RAX, 0, x86::R10);
                    enc.mov_ri64(x86::R11, 0);
                    enc.mov_mr8(x86::RAX, 1, x86::R11);
                }
            } else if (collType == ThavaType::DICT) {
                enc.mov_rm_rbp(x86::RAX, idxSlot);
                enc.mov_ri64(x86::R11, 32);
                enc.imul_rr(x86::RAX, x86::R11);
                enc.add_rr(x86::R10, x86::RAX);
                enc.mov_rm(x86::RAX, x86::R10, 16); // load key from entry
            } else {
                enc.mov_rm_rbp(x86::RAX, idxSlot);
                enc.mov_ri64(x86::R11, 8);
                enc.imul_rr(x86::RAX, x86::R11);
                enc.add_rr(x86::R10, x86::RAX);
                enc.mov_rm(x86::RAX, x86::R10, 8); // [R10 + 8]
            }
            enc.mov_mr_rbp(varSlot, x86::RAX);

            emitStmt(forEach->body.get());

            enc.bindLabel(stepLbl);
            enc.mov_rm_rbp(x86::RAX, idxSlot);
            enc.mov_ri64(x86::R10, 1);
            enc.add_rr(x86::RAX, x86::R10);
            enc.mov_mr_rbp(idxSlot, x86::RAX);
            enc.jmp(startLbl);

            enc.bindLabel(endLbl);
            breakLabels.pop_back();
            continueLabels.pop_back();
            exitScope();
            return;
        }

        if (dynamic_cast<ConsClearNode*>(node)) {
            if constexpr (std::is_same_v<TEmitter, ElfEmitter>) {
                // ANSI clear + home: works in every Linux terminal
                uint32_t offClear = pe.addStringLiteralOffset("\x1b[2J\x1b[H");
                enc.lea_rip_rdata(x86::RDX, offClear);
                emitCallPrintf(offFmtStr, x86::RDX, true);
            } else {
                uint32_t offCls = pe.addStringLiteralOffset("cls");
                enc.lea_rip_rdata(x86::RCX, offCls);
                enc.call_iat_func("system");
            }
            return;
        }

        if (auto consOut = dynamic_cast<ConsOutNode*>(node)) {
            auto lit = dynamic_cast<LiteralNode*>(consOut->getExpr());
            if (lit && lit->getLitType() == ThavaType::STRING) {
                const std::string& text = lit->getVal();
                size_t pos = 0;
                while (pos < text.length()) {
                    size_t startPlaceholder = text.find("@{", pos);
                    if (startPlaceholder == std::string::npos) {
                        std::string part = text.substr(pos);
                        if (!part.empty()) {
                            uint32_t off = pe.addStringLiteralOffset(part);
                            enc.lea_rip_rdata(x86::RDX, off);
                            emitCallPrintf(offFmtStr, x86::RDX, true);
                        }
                        break;
                    }
                    std::string part = text.substr(pos, startPlaceholder - pos);
                    if (!part.empty()) {
                        uint32_t off = pe.addStringLiteralOffset(part);
                        enc.lea_rip_rdata(x86::RDX, off);
                        emitCallPrintf(offFmtStr, x86::RDX, true);
                    }
                    size_t endPlaceholder = text.find('}', startPlaceholder);
                    if (endPlaceholder == std::string::npos) break;
                    std::string exprSrc = text.substr(startPlaceholder + 2, endPlaceholder - startPlaceholder - 2);

                    try {
                        auto exprAst = parseThavaExpression(exprSrc);
                        ThavaType et = emitExpr(exprAst.get());
                        if (et == ThavaType::INT) {
                            emitCallPrintf(offFmtInt, x86::RAX, true);
                        } else if (et == ThavaType::BOOL) {
                            size_t trueLbl = enc.createLabel();
                            size_t endLbl = enc.createLabel();
                            enc.test_rr(x86::RAX, x86::RAX);
                            enc.jne(trueLbl);
                            enc.lea_rip_rdata(x86::RDX, offStrFalse);
                            enc.jmp(endLbl);
                            enc.bindLabel(trueLbl);
                            enc.lea_rip_rdata(x86::RDX, offStrTrue);
                            enc.bindLabel(endLbl);
                            emitCallPrintf(offFmtStr, x86::RDX, true);
                        } else if (et == ThavaType::FLOAT || et == ThavaType::DOUBLE) {
                            emitCallPrintfFloat(offFmtFloat, x86::XMM0);
                        } else if (et == ThavaType::STRING) {
                            emitCallPrintf(offFmtStr, x86::RAX, true);
                        } else if (et == ThavaType::NONE) {
                            uint32_t off = pe.addStringLiteralOffset("none");
                            enc.lea_rip_rdata(x86::RDX, off);
                            emitCallPrintf(offFmtStr, x86::RDX, true);
                        } else if (et == ThavaType::CUSTOM) {
                            uint32_t off = pe.addStringLiteralOffset("<struct object>");
                            enc.lea_rip_rdata(x86::RDX, off);
                            emitCallPrintf(offFmtStr, x86::RDX, true);
                        }
                    } catch (...) {}
                    pos = endPlaceholder + 1;
                }
                emitCallPrintf(offFmtNL);
            } else {
                ThavaType et = emitExpr(consOut->getExpr());
                if (et == ThavaType::INT) {
                    emitCallPrintf(offFmtIntNL, x86::RAX, true);
                } else if (et == ThavaType::BOOL) {
                    size_t trueLbl = enc.createLabel();
                    size_t endLbl = enc.createLabel();
                    enc.test_rr(x86::RAX, x86::RAX);
                    enc.jne(trueLbl);
                    enc.lea_rip_rdata(x86::RDX, offStrFalse);
                    enc.jmp(endLbl);
                    enc.bindLabel(trueLbl);
                    enc.lea_rip_rdata(x86::RDX, offStrTrue);
                    enc.bindLabel(endLbl);
                    emitCallPrintf(offFmtStrNL, x86::RDX, true);
                } else if (et == ThavaType::FLOAT || et == ThavaType::DOUBLE) {
                    emitCallPrintfFloat(offFmtFloatNL, x86::XMM0);
                } else if (et == ThavaType::STRING) {
                    emitCallPrintf(offFmtStrNL, x86::RAX, true);
                } else if (et == ThavaType::NONE) {
                    uint32_t off = pe.addStringLiteralOffset("none\n");
                    enc.lea_rip_rdata(x86::RDX, off);
                    emitCallPrintf(offFmtStr, x86::RDX, true);
                } else if (et == ThavaType::CUSTOM) {
                    uint32_t off = pe.addStringLiteralOffset("<struct object>\n");
                    enc.lea_rip_rdata(x86::RDX, off);
                    emitCallPrintf(offFmtStr, x86::RDX, true);
                } else if (et == ThavaType::LIST || et == ThavaType::TUPLE) {
                    // Determine the element type so each item is printed with the
                    // correct format (double bit patterns / string pointers would
                    // otherwise be printed as raw integers -> garbage output).
                    ThavaType printElemType = ThavaType::INT;
                    if (auto pv = dynamic_cast<VarNode*>(consOut->getExpr())) {
                        auto leit = listElemTypes.find(pv->getName());
                        if (leit != listElemTypes.end()) printElemType = leit->second;
                    } else {
                        printElemType = lastCollectionElemType;
                    }

                    currentFrameOffset -= 8;
                    if (currentFrameOffset < maxFrameOffset) maxFrameOffset = currentFrameOffset;
                    int32_t listPtrSlot = currentFrameOffset;
                    enc.mov_mr_rbp(listPtrSlot, x86::RAX);

                    currentFrameOffset -= 8;
                    if (currentFrameOffset < maxFrameOffset) maxFrameOffset = currentFrameOffset;
                    int32_t idxSlot = currentFrameOffset;
                    enc.mov_ri64(x86::R10, 0);
                    enc.mov_mr_rbp(idxSlot, x86::R10);

                    enc.lea_rip_rdata(x86::RDX, offListOpen);
                    emitCallPrintf(offFmtStr, x86::RDX, true);

                    size_t loopLbl = enc.createLabel();
                    size_t doneLbl = enc.createLabel();

                    enc.bindLabel(loopLbl);
                    enc.mov_rm_rbp(x86::RAX, idxSlot);
                    enc.mov_rm_rbp(x86::R10, listPtrSlot);
                    enc.mov_rm(x86::R11, x86::R10, 0); // count
                    enc.cmp_rr(x86::RAX, x86::R11);
                    enc.jge(doneLbl);

                    enc.mov_rm_rbp(x86::RAX, idxSlot);
                    enc.mov_ri64(x86::R11, 8);
                    enc.imul_rr(x86::RAX, x86::R11);
                    enc.mov_rm_rbp(x86::R10, listPtrSlot);
                    enc.add_rr(x86::R10, x86::RAX);
                    if (printElemType == ThavaType::FLOAT || printElemType == ThavaType::DOUBLE) {
                        enc.movsd_rm(x86::XMM0, x86::R10, 8); // elem as double
                        emitCallPrintfFloat(offFmtFloat, x86::XMM0);
                    } else if (printElemType == ThavaType::STRING) {
                        enc.mov_rm(x86::RAX, x86::R10, 8); // elem as char*
                        emitCallPrintf(offFmtStr, x86::RAX, true);
                    } else {
                        enc.mov_rm(x86::RAX, x86::R10, 8); // elem as int64
                        emitCallPrintf(offFmtInt, x86::RAX, true);
                    }

                    size_t noCommaLbl = enc.createLabel();
                    enc.mov_rm_rbp(x86::RAX, idxSlot);
                    enc.mov_ri64(x86::R11, 1);
                    enc.add_rr(x86::RAX, x86::R11);
                    enc.mov_rm_rbp(x86::R10, listPtrSlot);
                    enc.mov_rm(x86::R11, x86::R10, 0);
                    enc.cmp_rr(x86::RAX, x86::R11);
                    enc.jge(noCommaLbl);
                    enc.lea_rip_rdata(x86::RDX, offListComma);
                    emitCallPrintf(offFmtStr, x86::RDX, true);

                    enc.bindLabel(noCommaLbl);
                    enc.mov_rm_rbp(x86::RAX, idxSlot);
                    enc.mov_ri64(x86::R11, 1);
                    enc.add_rr(x86::RAX, x86::R11);
                    enc.mov_mr_rbp(idxSlot, x86::RAX);
                    enc.jmp(loopLbl);

                    enc.bindLabel(doneLbl);
                    enc.lea_rip_rdata(x86::RDX, offListClose);
                    emitCallPrintf(offFmtStr, x86::RDX, true);
                } else if (et == ThavaType::DICT) {
                    currentFrameOffset -= 8;
                    if (currentFrameOffset < maxFrameOffset) maxFrameOffset = currentFrameOffset;
                    int32_t dictPtrSlot = currentFrameOffset;
                    enc.mov_mr_rbp(dictPtrSlot, x86::RAX);

                    currentFrameOffset -= 8;
                    if (currentFrameOffset < maxFrameOffset) maxFrameOffset = currentFrameOffset;
                    int32_t idxSlot = currentFrameOffset;
                    enc.mov_ri64(x86::R10, 0);
                    enc.mov_mr_rbp(idxSlot, x86::R10);

                    uint32_t offDictOpen = pe.addStringLiteralOffset("{");
                    uint32_t offDictClose = pe.addStringLiteralOffset("}\n");
                    uint32_t offDictColon = pe.addStringLiteralOffset(": ");
                    uint32_t offQuote = pe.addStringLiteralOffset("\"%s\"");

                    enc.lea_rip_rdata(x86::RDX, offDictOpen);
                    emitCallPrintf(offFmtStr, x86::RDX, true);

                    size_t loopLbl = enc.createLabel();
                    size_t doneLbl = enc.createLabel();

                    enc.bindLabel(loopLbl);
                    enc.mov_rm_rbp(x86::RAX, idxSlot);
                    enc.mov_rm_rbp(x86::R10, dictPtrSlot);
                    enc.mov_rm(x86::R11, x86::R10, 0); // count
                    enc.cmp_rr(x86::RAX, x86::R11);
                    enc.jge(doneLbl);

                    // key at [dict + 16 + i * 32]
                    enc.mov_rm_rbp(x86::RAX, idxSlot);
                    enc.mov_ri64(x86::R11, 32);
                    enc.imul_rr(x86::RAX, x86::R11);
                    enc.mov_rm_rbp(x86::R10, dictPtrSlot);
                    enc.add_rr(x86::R10, x86::RAX);
                    enc.mov_rm(x86::R11, x86::R10, 32); // keyType
                    enc.mov_ri64(x86::RAX, (int)ThavaType::STRING);
                    size_t notStrKey = enc.createLabel();
                    size_t afterKey = enc.createLabel();
                    enc.cmp_rr(x86::R11, x86::RAX);
                    enc.jne(notStrKey);
                    enc.mov_rm(x86::RDX, x86::R10, 16);
                    emitCallPrintf(offQuote, x86::RDX, true);
                    enc.jmp(afterKey);

                    enc.bindLabel(notStrKey);
                    enc.mov_rm(x86::RAX, x86::R10, 16);
                    emitCallPrintf(offFmtInt, x86::RAX, true);

                    enc.bindLabel(afterKey);
                    enc.lea_rip_rdata(x86::RDX, offDictColon);
                    emitCallPrintf(offFmtStr, x86::RDX, true);

                    // val is at [dict + 16 + i * 32 + 8] = [dict + 24 + i * 32]
                    enc.mov_rm_rbp(x86::RAX, idxSlot);
                    enc.mov_ri64(x86::R11, 32);
                    enc.imul_rr(x86::RAX, x86::R11);
                    enc.mov_rm_rbp(x86::R10, dictPtrSlot);
                    enc.add_rr(x86::R10, x86::RAX);

                    enc.mov_rm(x86::R11, x86::R10, 40); // valType
                    enc.mov_ri64(x86::RAX, (int)ThavaType::STRING);
                    size_t notStrVal = enc.createLabel();
                    size_t afterVal = enc.createLabel();
                    enc.cmp_rr(x86::R11, x86::RAX);
                    enc.jne(notStrVal);
                    enc.mov_rm(x86::RDX, x86::R10, 24);
                    emitCallPrintf(offQuote, x86::RDX, true);
                    enc.jmp(afterVal);

                    enc.bindLabel(notStrVal);
                    enc.mov_rm(x86::RAX, x86::R10, 24);
                    emitCallPrintf(offFmtInt, x86::RAX, true);

                    enc.bindLabel(afterVal);

                    // comma if not last
                    size_t noCommaLbl = enc.createLabel();
                    enc.mov_rm_rbp(x86::RAX, idxSlot);
                    enc.mov_ri64(x86::R11, 1);
                    enc.add_rr(x86::RAX, x86::R11);
                    enc.mov_rm_rbp(x86::R10, dictPtrSlot);
                    enc.mov_rm(x86::R11, x86::R10, 0);
                    enc.cmp_rr(x86::RAX, x86::R11);
                    enc.jge(noCommaLbl);
                    enc.lea_rip_rdata(x86::RDX, offListComma);
                    emitCallPrintf(offFmtStr, x86::RDX, true);

                    enc.bindLabel(noCommaLbl);
                    enc.mov_rm_rbp(x86::RAX, idxSlot);
                    enc.mov_ri64(x86::R11, 1);
                    enc.add_rr(x86::RAX, x86::R11);
                    enc.mov_mr_rbp(idxSlot, x86::RAX);
                    enc.jmp(loopLbl);

                    enc.bindLabel(doneLbl);
                    enc.lea_rip_rdata(x86::RDX, offDictClose);
                    emitCallPrintf(offFmtStr, x86::RDX, true);
                } else {
                    emitCallPrintf(offFmtNL);
                }
            }
            return;
        }

        if (auto retNode = dynamic_cast<ReturnNode*>(node)) {
            if (retNode->expr) {
                ThavaType t = emitExpr(retNode->expr.get());
                if ((currentFuncRetType == ThavaType::FLOAT || currentFuncRetType == ThavaType::DOUBLE) && t == ThavaType::INT) {
                    enc.cvtsi2sd(x86::XMM0, x86::RAX);
                } else if (currentFuncRetType == ThavaType::INT && (t == ThavaType::FLOAT || t == ThavaType::DOUBLE)) {
                    enc.cvttsd2si(x86::RAX, x86::XMM0);
                }
                if (currentFuncRetType == ThavaType::INT) narrowIntReg(x86::RAX, currentFuncRetWidth);
            }
            enc.jmp(currentFuncRetLabel);
            return;
        }

        if (auto fileBlock = dynamic_cast<FileOpenBlockNode*>(node)) {
            emitFileOpenBlock(fileBlock);
            return;
        }

        if (auto exprStmt = dynamic_cast<ExpressionStatementNode*>(node)) {
            emitExpr(exprStmt->expr.get());
            return;
        }

        if (auto defNode = dynamic_cast<DeferNode*>(node)) {
            auto it = deferNodeToFlagSlot.find(defNode);
            if (it != deferNodeToFlagSlot.end()) {
                enc.mov_ri64(x86::R10, 1);
                enc.mov_mr_rbp(it->second, x86::R10);
            }
            return;
        }

        if (auto raiseNode = dynamic_cast<RaiseErrorNode*>(node)) {
            if (raiseNode->isCompileTime) return;
            emitThrowCall(raiseNode->msgExpr.get(), raiseNode->typeExpr.get(), "Runtime Error!");
            return;
        }

        if (auto throwNode = dynamic_cast<ThrowNode*>(node)) {
            emitThrowCall(throwNode->expr.get(), nullptr, "Runtime Exception occurred!");
            return;
        }

        if (auto tryNode = dynamic_cast<TryExceptNode*>(node)) {
            // Handler record lives in this frame; R15 links active handlers.
            currentFrameOffset -= 48;
            if (currentFrameOffset < maxFrameOffset) maxFrameOffset = currentFrameOffset;
            int32_t rec = currentFrameOffset;

            size_t resumeLbl = enc.createLabel();
            size_t afterLbl = enc.createLabel();

            enc.xor_r32(x86::R10, x86::R10);
            enc.mov_mr_rbp(rec + 0, x86::R10);            // message
            enc.mov_mr_rbp(rec + 8, x86::R10);            // type name
            enc.mov_mr_rbp(rec + 16, x86::R15);           // previous handler
            enc.lea_rip_label(x86::R10, resumeLbl);
            enc.mov_mr_rbp(rec + 24, x86::R10);           // resume address
            enc.mov_mr_rbp(rec + 32, x86::RBP);           // saved RBP
            enc.lea_rm_rbp(x86::R15, rec);                // install handler

            enterScope();
            for (const auto& s : tryNode->tryBody) emitStmt(s.get());
            exitScope();

            // Normal completion: pop the handler and skip the catch code.
            enc.mov_rm_rbp(x86::R15, rec + 16);
            enc.jmp(afterLbl);

            // A throw jumped back here.
            enc.bindLabel(resumeLbl);
            enc.mov_rm_rbp(x86::R15, rec + 16);

            const ExceptClause* catchAll = nullptr;
            for (const auto& clause : tryNode->exceptClauses) {
                bool isCatchAll = clause.exceptionType.empty() || clause.exceptionType == "Exception" ||
                                  clause.exceptionType == "RuntimeError";

                size_t nextClause = 0;
                if (!isCatchAll) {
                    nextClause = enc.createLabel();
                    enc.mov_rm_rbp(x86::RCX, rec + 8);
                    enc.lea_rip_rdata(x86::RDX, pe.addStringLiteralOffset(clause.exceptionType));
                    enc.call_iat_func("strcmp");
                    enc.test_rr(x86::RAX, x86::RAX);
                    enc.jne(nextClause);
                }

                enterScope();
                if (!clause.varName.empty()) {
                    int32_t varOff = declareVar(clause.varName, ThavaType::STRING);
                    enc.mov_rm_rbp(x86::RAX, rec + 0);
                    enc.mov_mr_rbp(varOff, x86::RAX);
                }
                for (const auto& s : clause.body) emitStmt(s.get());
                exitScope();
                enc.jmp(afterLbl);

                if (!isCatchAll) enc.bindLabel(nextClause);
                else { catchAll = &clause; break; }
            }
            (void)catchAll;

            // Nothing matched: keep unwinding outwards with the same payload.
            enc.mov_rm_rbp(x86::RAX, rec + 0);
            enc.mov_rm_rbp(x86::RDX, rec + 8);
            enc.call_label(lblThrow);

            enc.bindLabel(afterLbl);
            return;
        }
    }

    void emitUserFunction(FunctionDeclNode* func) {
        auto it = functions.find(func->name);
        size_t labelId = it->second.labelId;

        enc.bindLabel(labelId);

        // Function Prologue
        enc.push(x86::RBP);
        enc.mov_rr(x86::RBP, x86::RSP);
        enc.sub_rsp(2048);

        currentFrameOffset = -32;
        maxFrameOffset = -32;
        currentFuncRetLabel = enc.createLabel();
        currentFuncRetType = func->returnType;
        currentFuncRetWidth = func->customReturnType;

        enterFunctionScope();

        // Setup defers inside this function
        std::vector<DeferNode*> defersInFunc;
        for (const auto& s : func->body) {
            collectDefers(s.get(), defersInFunc);
        }

        struct FuncDeferInfo {
            int32_t flagSlot;
            DeferNode* node;
        };
        std::vector<FuncDeferInfo> funcDefers;
        for (auto* dn : defersInFunc) {
            currentFrameOffset -= 8;
            if (currentFrameOffset < maxFrameOffset) maxFrameOffset = currentFrameOffset;
            int32_t fSlot = currentFrameOffset;
            funcDefers.push_back({ fSlot, dn });
            deferNodeToFlagSlot[dn] = fSlot;
            enc.xor_r32(x86::R10, x86::R10);
            enc.mov_mr_rbp(fSlot, x86::R10);
        }

        x86::Reg intRegs[] = { x86::RCX, x86::RDX, x86::R8, x86::R9 };
        x86::Xmm xmmRegs[] = { x86::XMM0, x86::XMM1, x86::XMM2, x86::XMM3 };

        for (size_t i = 0; i < func->params.size(); ++i) {
            ThavaType pt = func->params[i].first;
            const std::string& pname = func->params[i].second;
            std::string pct = (i < func->paramCustomTypes.size()) ? func->paramCustomTypes[i] : "";
            int32_t off = declareVar(pname, pt, pct);

            if (isCollectionType(pt)) {
                // Element type comes from the call sites (g_funcListParamElem); the
                // parameter shadows a same-named global, so never inherit its type.
                auto eit = g_funcListParamElem.find(func->name);
                ThavaType et = (eit != g_funcListParamElem.end() && i < eit->second.size())
                                   ? eit->second[i] : ThavaType::UNKNOWN;
                if (et != ThavaType::UNKNOWN) listElemTypes[pname] = et;
                else listElemTypes.erase(pname);
            }

            if (isCollectionType(pt)) {
                // The caller passes the address of its own slot. Keep that address
                // (to copy the possibly reallocated pointer back on return) and use
                // the current pointer value as this function's variable.
                int32_t refSlot = declareVar("@ref" + std::to_string(i), ThavaType::INT);
                VarInfo* pv = lookupVar(pname);
                if (pv) { pv->listOut = true; pv->listOutSlot = refSlot; }
                if (i < 4) {
                    enc.mov_mr_rbp(refSlot, intRegs[i]);
                } else {
                    int32_t callerOff = 48 + (int32_t)(i - 4) * 8;
                    enc.mov_rm_rbp(x86::RAX, callerOff);
                    enc.mov_mr_rbp(refSlot, x86::RAX);
                }
                enc.mov_rm_rbp(x86::RAX, refSlot);
                enc.mov_rm(x86::RAX, x86::RAX, 0);
                enc.mov_mr_rbp(off, x86::RAX);
            } else if (i < 4) {
                if (pt == ThavaType::FLOAT || pt == ThavaType::DOUBLE) {
                    enc.movsd_mr_rbp(off, xmmRegs[i]);
                } else {
                    if (pt == ThavaType::INT) narrowIntReg(intRegs[i], pct);
                    enc.mov_mr_rbp(off, intRegs[i]);
                }
            } else {
                int32_t callerOff = 48 + (int32_t)(i - 4) * 8;
                if (pt == ThavaType::FLOAT || pt == ThavaType::DOUBLE) {
                    enc.movsd_rm_rbp(x86::XMM0, callerOff);
                    enc.movsd_mr_rbp(off, x86::XMM0);
                } else {
                    enc.mov_rm_rbp(x86::RAX, callerOff);
                    enc.mov_mr_rbp(off, x86::RAX);
                }
            }
        }

        for (const auto& s : func->body) {
            emitStmt(s.get());
        }

        enc.bindLabel(currentFuncRetLabel);

        // Execute active defers in REVERSE order (LIFO)
        if (!funcDefers.empty()) {
            currentFrameOffset -= 16;
            if (currentFrameOffset < maxFrameOffset) maxFrameOffset = currentFrameOffset;
            int32_t retSaveSlot = currentFrameOffset;
            int32_t retFloatSlot = currentFrameOffset + 8;

            if (func->returnType == ThavaType::FLOAT || func->returnType == ThavaType::DOUBLE) {
                enc.movsd_mr_rbp(retFloatSlot, x86::XMM0);
            } else if (func->returnType != ThavaType::VOID) {
                enc.mov_mr_rbp(retSaveSlot, x86::RAX);
            }

            for (int i = (int)funcDefers.size() - 1; i >= 0; --i) {
                size_t skipLbl = enc.createLabel();
                enc.mov_rm_rbp(x86::R10, funcDefers[i].flagSlot);
                enc.cmp_ri(x86::R10, 1);
                enc.jne(skipLbl);
                emitStmt(funcDefers[i].node->stmt.get());
                enc.bindLabel(skipLbl);
            }

            if (func->returnType == ThavaType::FLOAT || func->returnType == ThavaType::DOUBLE) {
                enc.movsd_rm_rbp(x86::XMM0, retFloatSlot);
            } else if (func->returnType != ThavaType::VOID) {
                enc.mov_rm_rbp(x86::RAX, retSaveSlot);
            }
        }

        // Copy list/dict/tuple parameters back into the caller's variable, so that
        // addval/removeval/assign inside this function stay visible to the caller.
        for (size_t i = 0; i < func->params.size(); ++i) {
            if (!isCollectionType(func->params[i].first)) continue;
            VarInfo* pv = lookupVar(func->params[i].second);
            if (!pv || !pv->listOut) continue;
            enc.mov_rm_rbp(x86::R10, pv->offset);
            enc.mov_rm_rbp(x86::R11, pv->listOutSlot);
            enc.mov_mr(x86::R11, 0, x86::R10);
        }

        exitFunctionScope();

        enc.add_rsp(2048);
        enc.pop(x86::RBP);
        enc.ret();
    }

    // ==========================================
    // PROGRAM ENTRY POINT & ORCHESTRATION
    // ==========================================
    // Maps every @dlattach library into the running process and relocates its
    // absolute data words. Emitted into the program entry, before user code, so a
    // missing library fails with a clear message instead of a bad jump.
    void emitDLAttachLoader() {
        if (!g_hasDLAttach) return;

        for (const auto& libPath : g_dlLibraries) {
            auto infoIt = g_dlLibraryInfos.find(libPath);
            if (infoIt == g_dlLibraryInfos.end() || !infoIt->second.hasOffsets) {
                std::cerr << "[Thava Compiler] Error: dynamic library '" << libPath
                          << "' exports no offsets; rebuild it with --lib using this compiler.\n";
                exit(1);
            }
            const DLLibraryInfo& lib = infoIt->second;
            const char* expectedMachine = std::is_same_v<TEmitter, PeEmitter> ? "x86-64-pe" : "x86-64-elf";
            if (!lib.machine.empty() && lib.machine != expectedMachine) {
                std::cerr << "[Thava Compiler] Error: dynamic library '" << libPath << "' was built for '"
                          << lib.machine << "', but this target is '" << expectedMachine << "'.\n";
                exit(1);
            }
            const int64_t base = (int64_t)dlLoadBaseForPath(libPath);
            const int64_t delta = base - (int64_t)lib.imageBase;

            uint32_t pathStr = pe.addStringLiteralOffset(libPath);
            uint32_t baseNameStr = pe.addStringLiteralOffset(lib.basename.empty() ? libPath : lib.basename);
            const std::string errMsg =
                "[Thava Runtime] cannot load library '" + libPath + "'\n";
            uint32_t errStr = pe.addStringLiteralOffset(errMsg);
            size_t errLbl = enc.createLabel();
            size_t okLbl = enc.createLabel();
            size_t altLbl = enc.createLabel();
            size_t fdLbl = enc.createLabel();

            // fd = open(path, O_RDONLY); jumps to failLbl on error, leaves the fd in R12.
            auto emitOpen = [&](uint32_t strOff, size_t failLbl) {
                enc.lea_rip_rdata(x86::RDI, strOff);
                enc.xor_r32(x86::RSI, x86::RSI);
                enc.xor_r32(x86::RDX, x86::RDX);
                enc.mov_ri64(x86::RAX, 2);        // sys_open
                enc.syscall();
                enc.test_rr(x86::RAX, x86::RAX);
                enc.jl(failLbl);
                enc.mov_rr(x86::R12, x86::RAX);   // fd
            };

            emitOpen(pathStr, altLbl);
            enc.jmp(fdLbl);

            // The recorded path can belong to the build machine (cross-compiling), so
            // fall back to the plain file name in the current directory.
            enc.bindLabel(altLbl);
            emitOpen(baseNameStr, errLbl);
            enc.bindLabel(fdLbl);

            // size = lseek(fd, 0, SEEK_END)
            enc.mov_rr(x86::RDI, x86::R12);
            enc.xor_r32(x86::RSI, x86::RSI);
            enc.mov_ri64(x86::RDX, 2);
            enc.mov_ri64(x86::RAX, 8);
            enc.syscall();
            enc.test_rr(x86::RAX, x86::RAX);
            enc.jle(errLbl);
            enc.mov_rr(x86::R13, x86::RAX);      // file size
            enc.mov_rr(x86::R14, x86::R13);
            enc.mov_ri64(x86::R10, kDLHeapSlack);
            enc.add_rr(x86::R14, x86::R10);      // reservation = size + heap slack

            // Reserve the whole window anonymously first: the part of the file that
            // is not page aligned stays zero-filled, which gives the module its heap.
            enc.mov_ri64(x86::RDI, base);
            enc.mov_rr(x86::RSI, x86::R14);
            enc.mov_ri64(x86::RDX, 7);           // PROT_READ|PROT_WRITE|PROT_EXEC
            enc.mov_ri64(x86::R10, 0x22);        // MAP_PRIVATE|MAP_ANONYMOUS
            enc.mov_ri64(x86::R8, -1);
            enc.xor_r32(x86::R9, x86::R9);
            enc.mov_ri64(x86::RAX, 9);           // mmap
            enc.syscall();
            enc.mov_ri64(x86::R10, base);
            enc.cmp_rr(x86::RAX, x86::R10);
            enc.jne(errLbl);

            // Map the library file itself over the reservation (MAP_FIXED).
            enc.mov_ri64(x86::RDI, base);
            enc.mov_rr(x86::RSI, x86::R13);
            enc.mov_ri64(x86::RDX, 7);
            enc.mov_ri64(x86::R10, 0x12);        // MAP_PRIVATE|MAP_FIXED
            enc.mov_rr(x86::R8, x86::R12);
            enc.xor_r32(x86::R9, x86::R9);
            enc.mov_ri64(x86::RAX, 9);
            enc.syscall();
            enc.mov_ri64(x86::R10, base);
            enc.cmp_rr(x86::RAX, x86::R10);
            enc.jne(errLbl);

            // close(fd)
            enc.mov_rr(x86::RDI, x86::R12);
            enc.mov_ri64(x86::RAX, 3);
            enc.syscall();

            // Relocate the module's absolute function-pointer words.
            for (uint32_t relocOff : lib.relocs) {
                enc.mov_ri64(x86::R10, base + (int64_t)relocOff);
                enc.mov_rm(x86::R11, x86::R10, 0);
                enc.mov_ri64(x86::RAX, delta);
                enc.add_rr(x86::R11, x86::RAX);
                enc.mov_mr(x86::R10, 0, x86::R11);
            }

            // Run the module's top-level initializer, like DllMain on PE. RDX = 1
            // passes the library's DllMain reason guard; the entry sets up its own
            // frame, so it must be entered at its first instruction.
            if (lib.hasInit) {
                enc.mov_ri64(x86::RDX, 1);
                enc.mov_ri64(x86::RAX, base + (int64_t)lib.textRva + (int64_t)lib.initOffset);
                enc.call_r(x86::RAX);
            }
            enc.jmp(okLbl);

            // Report on stderr and exit(1) with raw syscalls only: the loader must
            // not depend on the program's runtime helpers being initialised.
            enc.bindLabel(errLbl);
            enc.mov_ri64(x86::RDI, 2);
            enc.lea_rip_rdata(x86::RSI, errStr);
            enc.mov_ri64(x86::RDX, (int64_t)errMsg.size());
            enc.mov_ri64(x86::RAX, 1);
            enc.syscall();
            enc.mov_ri64(x86::RDI, 1);
            enc.mov_ri64(x86::RAX, 60);
            enc.syscall();

            enc.bindLabel(okLbl);
        }
    }

    bool compileProgram(const std::vector<ASTPtr>& ast, const std::string& outputFile) {
        initFormats();
        size_t libInitLabel = 0;

        if (g_isLibraryMode) {
            if constexpr (std::is_same_v<TEmitter, PeEmitter>) {
                pe.setDll(true);
                pe.setDllExportName(outputFile);
            }
        }

        // 1. Jump over functions directly to main entry point
        size_t mainLabel = enc.createLabel();
        enc.jmp(mainLabel);

        // A library's initializer is its entry point: the ELF loader calls it
        // directly, with RDX = 1 to pass the DllMain reason guard below.
        if (g_isLibraryMode) libInitLabel = mainLabel;

        // 1.5. Globals: reserve one data cell for the program frame pointer and
        // give every top-level variable a slot in the program frame, so functions
        // and the top-level body agree on the same addresses.
        // Globals live in the frame of the program body; a data cell stores that
        // frame pointer so functions and methods can reach them.
        currentFrameOffset = -32;
        maxFrameOffset = -32;
        enterFunctionScope();
        globalBaseCell = pe.addDataSlot(8);
        if (globalEnv) {
            for (const auto& gv : globalEnv->globals) {
                globalVarSlots[gv.first] = declareVar(
                    gv.first, globalEnv->get(gv.first), globalEnv->getCustomType(gv.first));
                // Function bodies are emitted before the program body, so the element
                // type of a global list must be known now: without it `g_list[i]`
                // would be unboxed as an int (raw bits for doubles).
                ThavaType gElem = globalEnv->getListElem(gv.first);
                if (gElem != ThavaType::UNKNOWN && gElem != ThavaType::NONE)
                    listElemTypes[gv.first] = gElem;
            }
        }
        // Everything emitted from here on (functions, methods) must not resolve
        // these frame slots.
        lookupFloor = scopeStack.size();
        // Functions reset the frame counter while they are emitted; remember where
        // the program body's locals start.
        programFrameBase = currentFrameOffset;

        // 1.5. In Linux ELF mode, emit helpers calling Linux syscalls directly
        if constexpr (std::is_same_v<TEmitter, ElfEmitter>) {
            LinuxNativeRuntime linuxRt(enc, pe);
            linuxRt.emitAndRegister();
        }

        // 2. Emit runtime string & collection helpers
        emitRuntimeHelpers();
        emitThrowHelper();

        // 2.5. Register all dynamic library imported functions in PE
        if constexpr (std::is_same_v<TEmitter, PeEmitter>) {
            for (const auto& [fname, info] : g_dlFunctions) {
                pe.addImport(info.libBasename, fname);
            }
        }

        // 3. Pre-register classes (layouts + method labels)
        for (const auto& node : ast) {
            if (auto cd = dynamic_cast<ClassDeclNode*>(node.get())) registerNativeClass(cd);
        }

        // 3.1. Pre-register all user functions & forward declarations
        for (const auto& node : ast) {
            if (auto func = dynamic_cast<FunctionDeclNode*>(node.get())) {
                if (functions.find(func->name) == functions.end()) {
                    FuncInfo fi;
                    fi.labelId = enc.createLabel();
                    fi.returnType = func->returnType;
                    fi.params = func->params;
                    functions[func->name] = fi;
                }
            } else if (auto fwd = dynamic_cast<ForwardDeclNode*>(node.get())) {
                if (functions.find(fwd->name) == functions.end()) {
                    FuncInfo fi;
                    fi.labelId = enc.createLabel();
                    fi.returnType = fwd->returnType;
                    fi.params = fwd->params;
                    functions[fwd->name] = fi;
                }
            }
        }

        // 4. Emit bodies for all user functions
        for (const auto& node : ast) {
            if (auto func = dynamic_cast<FunctionDeclNode*>(node.get())) {
                emitUserFunction(func);
            }
        }

        // 4.1. Emit bodies for all class methods and constructors
        for (const auto& node : ast) {
            auto cd = dynamic_cast<ClassDeclNode*>(node.get());
            if (!cd) continue;
            for (const auto& m : cd->methods) {
                std::string key = cd->className + "::" + m.name;
                auto lit = classMethodLabels.find(key);
                if (lit == classMethodLabels.end()) continue;
                emitClassMethod(cd->className, lit->second, m.returnType, m.customReturnType,
                                m.params, m.paramCustomTypes, m.body, m.name);
            }
            for (const auto& c : cd->constructors) {
                std::string key = cd->className + "::init";
                auto lit = classMethodLabels.find(key);
                if (lit == classMethodLabels.end()) continue;
                emitClassMethod(cd->className, lit->second, ThavaType::VOID, "",
                                c.params, c.paramCustomTypes, c.body, "init");
            }
        }

        // 5. Main Entry Point
        enc.bindLabel(mainLabel);
        enc.push(x86::RBP);
        enc.mov_rr(x86::RBP, x86::RSP);
        enc.sub_rsp(2048);

        // Globals already reserved the first slots of this frame (step 1.5); locals
        // continue after them. The program body resolves globals as its own frame
        // slots again.
        lookupFloor = 0;
        currentFrameOffset = programFrameBase;
        enterFunctionScope();

        // Publish this frame pointer so functions can reach globals.
        enc.lea_rip_rdata(x86::R10, globalBaseCell);
        enc.mov_mr(x86::R10, 0, x86::RBP);

        // Map and relocate every @dlattach library before any of its code runs (a
        // library itself never runs the loader: its exports are entered directly).
        if constexpr (std::is_same_v<TEmitter, ElfEmitter>) {
            if (!g_isLibraryMode) emitDLAttachLoader();
        }

        // No handler installed at program start.
        enc.xor_r32(x86::R15, x86::R15);

        if (g_isLibraryMode) {
            // Dynamic Library Entry Point (DllMain for Windows PE)
            // RDX = fdwReason (1 = DLL_PROCESS_ATTACH)
            size_t skipInitLabel = enc.createLabel();
            enc.cmp_ri32(x86::RDX, 1);
            enc.jne(skipInitLabel);

            // Execute top-level initialization statements if any
            for (const auto& node : ast) {
                if (dynamic_cast<FunctionDeclNode*>(node.get())) continue;
                if (dynamic_cast<ForwardDeclNode*>(node.get())) continue;
                if (dynamic_cast<CFuncDeclNode*>(node.get())) continue;
                if (dynamic_cast<ClassDeclNode*>(node.get())) continue;
                if (dynamic_cast<StructDeclNode*>(node.get())) continue;
                if (dynamic_cast<EnumDeclNode*>(node.get())) continue;
                emitStmt(node.get());
            }

            enc.bindLabel(skipInitLabel);
            enc.add_rsp(2048);
            enc.pop(x86::RBP);
            enc.mov_ri64(x86::RAX, 1); // return TRUE (success)
            enc.ret();
            exitFunctionScope();
        } else {
            emittingProgramBody = true;

            // Set console to UTF-8
            if constexpr (std::is_same_v<TEmitter, PeEmitter>) {
                enc.mov_ri64(x86::RCX, 65001);
                enc.call_iat_func("SetConsoleCP");
                enc.mov_ri64(x86::RCX, 65001);
                enc.call_iat_func("SetConsoleOutputCP");
            }

            // Seed RNG: srand(time(NULL))
            enc.xor_r32(x86::RCX, x86::RCX);
            enc.call_iat_func("time");
            enc.mov_rr(x86::RCX, x86::RAX);
            enc.call_iat_func("srand");

            // Setup top-level defers
            std::vector<DeferNode*> topDefers;
            for (const auto& node : ast) {
                if (dynamic_cast<FunctionDeclNode*>(node.get())) continue;
                if (dynamic_cast<ForwardDeclNode*>(node.get())) continue;
                if (dynamic_cast<CFuncDeclNode*>(node.get())) continue;
                if (dynamic_cast<ClassDeclNode*>(node.get())) continue;
                if (dynamic_cast<StructDeclNode*>(node.get())) continue;
                if (dynamic_cast<EnumDeclNode*>(node.get())) continue;
                collectDefers(node.get(), topDefers);
            }

            struct TopDeferInfo {
                int32_t flagSlot;
                DeferNode* node;
            };
            std::vector<TopDeferInfo> topDeferInfos;
            for (auto* dn : topDefers) {
                currentFrameOffset -= 8;
                if (currentFrameOffset < maxFrameOffset) maxFrameOffset = currentFrameOffset;
                int32_t fSlot = currentFrameOffset;
                topDeferInfos.push_back({ fSlot, dn });
                deferNodeToFlagSlot[dn] = fSlot;
                enc.xor_r32(x86::R10, x86::R10);
                enc.mov_mr_rbp(fSlot, x86::R10);
            }

            // Execute all top-level statements
            for (const auto& node : ast) {
                if (dynamic_cast<FunctionDeclNode*>(node.get())) continue;
                if (dynamic_cast<ForwardDeclNode*>(node.get())) continue;
                if (dynamic_cast<CFuncDeclNode*>(node.get())) continue;
                if (dynamic_cast<ClassDeclNode*>(node.get())) continue;
                if (dynamic_cast<StructDeclNode*>(node.get())) continue;
                if (dynamic_cast<EnumDeclNode*>(node.get())) continue;
                emitStmt(node.get());

        }

        {
            // Execute active top-level defers in REVERSE order (LIFO)
            for (int i = (int)topDeferInfos.size() - 1; i >= 0; --i) {
                size_t skipLbl = enc.createLabel();
                enc.mov_rm_rbp(x86::R10, topDeferInfos[i].flagSlot);
                enc.cmp_ri(x86::R10, 1);
                enc.jne(skipLbl);
                emitStmt(topDeferInfos[i].node->stmt.get());
                enc.bindLabel(skipLbl);
            }

            // Print final success message
            enc.lea_rip_rdata(x86::RDX, offSuccessMsg);
            emitCallPrintf(offFmtStr, x86::RDX, true);

            // Exit process: exit(0)
            enc.xor_r32(x86::RCX, x86::RCX);
            enc.call_iat_func("exit");

            // Cleanup
            enc.add_rsp(2048);
            enc.pop(x86::RBP);
            enc.ret();
            exitFunctionScope();
        }
        }

        // 5.5. Dynamic Library Export & Metadata registration
        // The metadata carries image offsets of the exported functions, so it can
        // only be built once .text is final; it is therefore reached through a data
        // slot that is patched together with the string itself.
        uint32_t metaSlot = 0;
        if (g_isLibraryMode) {
            metaSlot = pe.addDataSlot(8);

            // Emit _thava_get_metadata in .text
            uint32_t metaFuncTextOffset = (uint32_t)enc.code.size();
            enc.lea_rip_rdata(x86::R10, metaSlot);
            enc.mov_rm(x86::RAX, x86::R10, 0);
            enc.ret();

            if constexpr (std::is_same_v<TEmitter, PeEmitter>) {
                pe.addExport("_thava_get_metadata", metaFuncTextOffset);

                // Register all user functions as exports
                for (const auto& node : ast) {
                    if (auto func = dynamic_cast<FunctionDeclNode*>(node.get())) {
                        auto it = functions.find(func->name);
                        if (it != functions.end()) {
                            int boundOff = enc.getLabelOffset(it->second.labelId);
                            if (boundOff >= 0) {
                                pe.addExport(func->name, (uint32_t)boundOff);
                            }
                        }
                    }
                }
            }
        }

        // 6. Layout .rdata, resolve fixups, and write PE/ELF binary
        pe.setTextBytes(enc.code);
        if (g_isLibraryMode) {
            uint32_t dataRva = 0;
            if constexpr (std::is_same_v<TEmitter, PeEmitter>) dataRva = pe.getRdataRva();
            else dataRva = pe.getDataRva();

            auto hexVal = [](uint64_t v) {
                static const char* digits = "0123456789abcdef";
                std::string s;
                do { s.insert(s.begin(), digits[v & 0xF]); v >>= 4; } while (v);
                return "0x" + s;
            };

            std::string metaStr = "__THAVA_METADATA_START__\n# THAVA DYNAMIC LIBRARY METADATA\n";
            metaStr += "machine:";
            metaStr += std::is_same_v<TEmitter, PeEmitter> ? "x86-64-pe" : "x86-64-elf";
            metaStr += "\n";
            metaStr += "image:" + hexVal(0x400000) + "\n";
            metaStr += "text:" + hexVal(pe.textRva) + "\n";
            metaStr += "data:" + hexVal(dataRva) + "\n";
            if (libInitLabel != 0) {
                int initOff = enc.getLabelOffset(libInitLabel);
                if (initOff >= 0) metaStr += "init:" + hexVal((uint64_t)initOff) + "\n";
            }
            for (const auto& node : ast) {
                if (auto func = dynamic_cast<FunctionDeclNode*>(node.get())) {
                    metaStr += "func:" + func->name + "|ret:" + typeToString(func->returnType) + "|params:";
                    for (size_t i = 0; i < func->params.size(); ++i) {
                        metaStr += typeToString(func->params[i].first) + " " + func->params[i].second;
                        if (i + 1 < func->params.size()) metaStr += ",";
                    }
                    auto fit = functions.find(func->name);
                    if (fit != functions.end()) {
                        int fOff = enc.getLabelOffset(fit->second.labelId);
                        if (fOff >= 0) metaStr += "|off:" + hexVal((uint64_t)fOff);
                    }
                    metaStr += "\n";
                }
            }
            if constexpr (!std::is_same_v<TEmitter, PeEmitter>) {
                metaStr += "reloc:";
                bool firstReloc = true;
                for (const auto& slot : pe.getFuncSlotOffsets()) {
                    if (!firstReloc) metaStr += ",";
                    firstReloc = false;
                    metaStr += hexVal((uint64_t)dataRva + slot.second);
                }
                metaStr += "\n";
            }
            metaStr += "__THAVA_METADATA_END__\n";
            uint32_t metaStrOffset = pe.addStringLiteralOffset(metaStr);
            pe.setDataSlotValue(metaSlot, 0x400000 + dataRva + metaStrOffset);
        }
        pe.layoutRdata();
        enc.resolveFixups(pe.textRva, pe.getRdataRva(), pe.getFuncToIatMap());
        pe.setTextBytes(enc.code);

        return pe.buildAndWrite(outputFile);
    }
};

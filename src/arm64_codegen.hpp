#pragma once

#include "elf_arm64_emitter.hpp"
#include "pe_emitter.hpp"
#include "arm64_encoder.hpp"
#include "arm64_runtime.hpp"
#include "arm64_win_runtime.hpp"
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <iostream>
#include <cstdint>
#include <type_traits>

template<typename TEmitter = ElfArm64Emitter, typename TRuntime = Arm64NativeRuntime>
class Arm64Codegen {
public:
    TEmitter& emitter;
    arm64::Encoder& enc;
    TRuntime runtime;
    std::shared_ptr<Environment> globalEnv;

    struct VarInfo {
        int32_t offset;
        ThavaType type;
        std::string customType;
    };

    struct FuncInfo {
        size_t labelId;
        ThavaType returnType;
        std::vector<std::pair<ThavaType, std::string>> params;
    };

    std::map<std::string, FuncInfo> functions;
    std::vector<std::map<std::string, VarInfo>> scopeStack;
    int32_t currentFrameOffset = 0;
    size_t currentFuncRetLabel = 0;
    ThavaType currentFuncRetType = ThavaType::UNKNOWN;

    // Loop control
    std::vector<size_t> breakLabels;
    std::vector<size_t> continueLabels;

    // Defer control
    std::vector<DeferNode*> functionDefers;
    std::vector<DeferNode*> topLevelDefers;

    int optLevel = 0;
    void setOptLevel(int level) { optLevel = level; }

    Arm64Codegen(TEmitter& em, arm64::Encoder& e, std::shared_ptr<Environment> env)
        : emitter(em), enc(e), runtime(e, em), globalEnv(env) {}

    void enterScope() {
        scopeStack.push_back({});
    }

    void exitScope() {
        if (!scopeStack.empty()) scopeStack.pop_back();
    }

    VarInfo* lookupVar(const std::string& name) {
        for (int i = static_cast<int>(scopeStack.size()) - 1; i >= 0; --i) {
            auto it = scopeStack[i].find(name);
            if (it != scopeStack[i].end()) return &it->second;
        }
        return nullptr;
    }

    int32_t declareVar(const std::string& name, ThavaType type, const std::string& customType = "") {
        currentFrameOffset -= 8;
        VarInfo info{ currentFrameOffset, type, customType };
        scopeStack.back()[name] = info;
        return currentFrameOffset;
    }

    // ==========================================
    // EXPRESSION CODE GENERATION
    // Evaluates expression and leaves result in X0 (integer/string/bool/ptr)
    // Returns the static ThavaType of the expression
    // ==========================================
    ThavaType compileExpression(ASTNode* node) {
        if (!node) return ThavaType::UNKNOWN;

        // 1. LiteralNode
        if (auto lit = dynamic_cast<LiteralNode*>(node)) {
            ThavaType lt = lit->getLitType();
            std::string valStr = lit->getVal();

            if (lt == ThavaType::INT) {
                int64_t val = 0;
                try { val = std::stoll(valStr); } catch (...) {}
                enc.mov_ri64(arm64::X0, val);
                return ThavaType::INT;
            } else if (lt == ThavaType::BOOL) {
                bool b = (valStr == "true");
                enc.movz(arm64::X0, b ? 1 : 0, 0);
                return ThavaType::BOOL;
            } else if (lt == ThavaType::STRING) {
                uint32_t strOff = emitter.addStringLiteralOffset(valStr);
                enc.adr_data(arm64::X0, strOff);
                return ThavaType::STRING;
            } else if (lt == ThavaType::FLOAT || lt == ThavaType::DOUBLE) {
                double d = 0.0;
                try { d = std::stod(valStr); } catch (...) {}
                uint32_t dOff = emitter.addDoubleLiteralOffset(d);
                enc.adr_data(arm64::X16, dOff);
                enc.ldr_imm(arm64::X0, arm64::X16, 0);
                return ThavaType::DOUBLE;
            } else if (lt == ThavaType::NONE) {
                enc.movz(arm64::X0, 0, 0);
                return ThavaType::NONE;
            }
        }

        // 2. VarNode
        if (auto vn = dynamic_cast<VarNode*>(node)) {
            std::string vname = vn->getName();
            VarInfo* v = lookupVar(vname);
            if (v) {
                enc.ldr_imm(arm64::X0, arm64::FP, v->offset);
                return v->type;
            }
            // Check function pointer
            auto fit = functions.find(vname);
            if (fit != functions.end()) {
                enc.adr(arm64::X0, fit->second.labelId);
                return ThavaType::FUNC;
            }
            std::cerr << "[ARM64 Codegen Error]: Undefined variable '" << vname << "'\n";
            return ThavaType::UNKNOWN;
        }

        // 3. BinaryOpNode
        if (auto bin = dynamic_cast<BinaryOpNode*>(node)) {
            // Evaluate left -> X0
            ThavaType lt = compileExpression(bin->left.get());

            // Save X0 on temporary stack slot (below SP)
            enc.str_imm(arm64::X0, arm64::SP, -16);
            enc.sub_rri(arm64::SP, arm64::SP, 16);

            // Evaluate right -> X0
            ThavaType rt = compileExpression(bin->right.get());

            // Restore left -> X1
            enc.ldr_imm(arm64::X1, arm64::SP, 0);
            enc.add_rri(arm64::SP, arm64::SP, 16);

            // X1 is left, X0 is right
            std::string op = bin->op;

            if (op == "+") {
                enc.add_rrr(arm64::X0, arm64::X1, arm64::X0);
                return ThavaType::INT;
            } else if (op == "-") {
                enc.sub_rrr(arm64::X0, arm64::X1, arm64::X0);
                return ThavaType::INT;
            } else if (op == "*") {
                enc.mul_rrr(arm64::X0, arm64::X1, arm64::X0);
                return ThavaType::INT;
            } else if (op == "/" || op == "//") {
                enc.sdiv_rrr(arm64::X0, arm64::X1, arm64::X0);
                return ThavaType::INT;
            } else if (op == "%") {
                enc.sdiv_rrr(arm64::X2, arm64::X1, arm64::X0);
                enc.mul_rrr(arm64::X2, arm64::X2, arm64::X0);
                enc.sub_rrr(arm64::X0, arm64::X1, arm64::X2);
                return ThavaType::INT;
            } else if (op == "band") {
                enc.and_rrr(arm64::X0, arm64::X1, arm64::X0);
                return ThavaType::INT;
            } else if (op == "bor") {
                enc.orr_rrr(arm64::X0, arm64::X1, arm64::X0);
                return ThavaType::INT;
            } else if (op == "bxor") {
                enc.eor_rrr(arm64::X0, arm64::X1, arm64::X0);
                return ThavaType::INT;
            } else if (op == "==") {
                enc.cmp_rr(arm64::X1, arm64::X0);
                enc.cset(arm64::X0, arm64::EQ);
                return ThavaType::BOOL;
            } else if (op == "!=") {
                enc.cmp_rr(arm64::X1, arm64::X0);
                enc.cset(arm64::X0, arm64::NE);
                return ThavaType::BOOL;
            } else if (op == "<") {
                enc.cmp_rr(arm64::X1, arm64::X0);
                enc.cset(arm64::X0, arm64::LT);
                return ThavaType::BOOL;
            } else if (op == "<=") {
                enc.cmp_rr(arm64::X1, arm64::X0);
                enc.cset(arm64::X0, arm64::LE);
                return ThavaType::BOOL;
            } else if (op == ">") {
                enc.cmp_rr(arm64::X1, arm64::X0);
                enc.cset(arm64::X0, arm64::GT);
                return ThavaType::BOOL;
            } else if (op == ">=") {
                enc.cmp_rr(arm64::X1, arm64::X0);
                enc.cset(arm64::X0, arm64::GE);
                return ThavaType::BOOL;
            } else if (op == "and") {
                enc.cmp_ri(arm64::X1, 0);
                enc.cset(arm64::X1, arm64::NE);
                enc.cmp_ri(arm64::X0, 0);
                enc.cset(arm64::X0, arm64::NE);
                enc.and_rrr(arm64::X0, arm64::X1, arm64::X0);
                return ThavaType::BOOL;
            } else if (op == "or") {
                enc.cmp_ri(arm64::X1, 0);
                enc.cset(arm64::X1, arm64::NE);
                enc.cmp_ri(arm64::X0, 0);
                enc.cset(arm64::X0, arm64::NE);
                enc.orr_rrr(arm64::X0, arm64::X1, arm64::X0);
                return ThavaType::BOOL;
            }
        }

        // 4. UnaryMinusNode (-x)
        if (auto un = dynamic_cast<UnaryMinusNode*>(node)) {
            ThavaType t = compileExpression(un->expr.get());
            enc.sub_rrr(arm64::X0, arm64::XZR, arm64::X0);
            return t;
        }

        // 5. UnaryNotNode (not x)
        if (auto un = dynamic_cast<UnaryNotNode*>(node)) {
            compileExpression(un->expr.get());
            enc.cmp_ri(arm64::X0, 0);
            enc.cset(arm64::X0, arm64::EQ);
            return ThavaType::BOOL;
        }

        // 6. ConditionalExprNode (cond ? trueExpr : falseExpr)
        if (auto cond = dynamic_cast<ConditionalExprNode*>(node)) {
            size_t elseLbl = enc.createLabel();
            size_t endLbl  = enc.createLabel();

            compileExpression(cond->condition.get());
            enc.cbz(arm64::X0, elseLbl);

            ThavaType t = compileExpression(cond->trueExpr.get());
            enc.b(endLbl);

            enc.bindLabel(elseLbl);
            compileExpression(cond->falseExpr.get());

            enc.bindLabel(endLbl);
            return t;
        }

        // 7. CallNode (Function invocation)
        if (auto call = dynamic_cast<CallNode*>(node)) {
            auto fit = functions.find(call->name);
            if (fit != functions.end()) {
                for (size_t a = 0; a < call->args.size(); ++a) {
                    compileExpression(call->args[a].get());
                    enc.str_imm(arm64::X0, arm64::SP, -16);
                    enc.sub_rri(arm64::SP, arm64::SP, 16);
                }
                for (int a = static_cast<int>(call->args.size()) - 1; a >= 0; --a) {
                    arm64::Reg targetReg = static_cast<arm64::Reg>(a);
                    enc.ldr_imm(targetReg, arm64::SP, 0);
                    enc.add_rri(arm64::SP, arm64::SP, 16);
                }
                enc.bl(fit->second.labelId);
                return fit->second.returnType;
            }

            // Check indirect function pointer call
            VarInfo* v = lookupVar(call->name);
            if (v && v->type == ThavaType::FUNC) {
                for (size_t a = 0; a < call->args.size(); ++a) {
                    compileExpression(call->args[a].get());
                    enc.str_imm(arm64::X0, arm64::SP, -16);
                    enc.sub_rri(arm64::SP, arm64::SP, 16);
                }
                for (int a = static_cast<int>(call->args.size()) - 1; a >= 0; --a) {
                    arm64::Reg targetReg = static_cast<arm64::Reg>(a);
                    enc.ldr_imm(targetReg, arm64::SP, 0);
                    enc.add_rri(arm64::SP, arm64::SP, 16);
                }
                enc.ldr_imm(arm64::X16, arm64::FP, v->offset);
                enc.blr(arm64::X16);
                return ThavaType::INT;
            }
        }

        // 8. CastNode (<int>(x), <str>(x), etc.)
        if (auto c = dynamic_cast<CastNode*>(node)) {
            compileExpression(c->expr.get());
            return c->targetType;
        }

        return ThavaType::UNKNOWN;
    }

    // ==========================================
    // STATEMENT CODE GENERATION
    // ==========================================
    void compileStatement(ASTNode* node) {
        if (!node) return;

        // 1. VarDeclNode (<int> x = 10)
        if (auto vdecl = dynamic_cast<VarDeclNode*>(node)) {
            ThavaType t = vdecl->varType;
            if (vdecl->initExpr) {
                t = compileExpression(vdecl->initExpr.get());
            } else {
                enc.movz(arm64::X0, 0, 0);
            }
            int32_t off = declareVar(vdecl->varName, t, vdecl->customTypeName);
            enc.str_imm(arm64::X0, arm64::FP, off);
            return;
        }

        // 2. AssignNode (x = 20, x += 5, etc.)
        if (auto assign = dynamic_cast<AssignNode*>(node)) {
            VarInfo* v = lookupVar(assign->name);
            if (!v) {
                std::cerr << "[ARM64 Codegen Error]: Assigning to undeclared variable '" << assign->name << "'\n";
                return;
            }
            compileExpression(assign->expr.get());
            if (assign->op == "=") {
                enc.str_imm(arm64::X0, arm64::FP, v->offset);
            } else if (assign->op == "+=") {
                enc.ldr_imm(arm64::X1, arm64::FP, v->offset);
                enc.add_rrr(arm64::X0, arm64::X1, arm64::X0);
                enc.str_imm(arm64::X0, arm64::FP, v->offset);
            } else if (assign->op == "-=") {
                enc.ldr_imm(arm64::X1, arm64::FP, v->offset);
                enc.sub_rrr(arm64::X0, arm64::X1, arm64::X0);
                enc.str_imm(arm64::X0, arm64::FP, v->offset);
            } else if (assign->op == "*=") {
                enc.ldr_imm(arm64::X1, arm64::FP, v->offset);
                enc.mul_rrr(arm64::X0, arm64::X1, arm64::X0);
                enc.str_imm(arm64::X0, arm64::FP, v->offset);
            } else if (assign->op == "/=") {
                enc.ldr_imm(arm64::X1, arm64::FP, v->offset);
                enc.sdiv_rrr(arm64::X0, arm64::X1, arm64::X0);
                enc.str_imm(arm64::X0, arm64::FP, v->offset);
            } else {
                enc.str_imm(arm64::X0, arm64::FP, v->offset);
            }
            return;
        }

        // 3. ConsOutNode (cons.out(expr))
        if (auto cout = dynamic_cast<ConsOutNode*>(node)) {
            ThavaType t = compileExpression(cout->getExpr());
            if (t == ThavaType::STRING) {
                enc.bl(runtime.lblPrintStrNL);
            } else if (t == ThavaType::BOOL) {
                enc.bl(runtime.lblPrintBoolNL);
            } else {
                enc.bl(runtime.lblPrintIntNL);
            }
            return;
        }

        // 4. BlockNode ({ ... })
        if (auto blk = dynamic_cast<BlockNode*>(node)) {
            enterScope();
            for (const auto& s : blk->statements) {
                compileStatement(s.get());
            }
            exitScope();
            return;
        }

        // 5. IfNode (if cond { ... } elif ... else { ... })
        if (auto ifn = dynamic_cast<IfNode*>(node)) {
            size_t elseLbl = enc.createLabel();
            size_t endLbl  = enc.createLabel();

            compileExpression(ifn->cond.get());
            enc.cbz(arm64::X0, elseLbl);

            compileStatement(ifn->thenBranch.get());
            enc.b(endLbl);

            enc.bindLabel(elseLbl);
            if (ifn->elseBranch) {
                compileStatement(ifn->elseBranch.get());
            }

            enc.bindLabel(endLbl);
            return;
        }

        // 6. WhileNode (while cond { ... })
        if (auto wh = dynamic_cast<WhileNode*>(node)) {
            size_t loopLbl  = enc.createLabel();
            size_t breakLbl = enc.createLabel();

            breakLabels.push_back(breakLbl);
            continueLabels.push_back(loopLbl);

            enc.bindLabel(loopLbl);
            compileExpression(wh->cond.get());
            enc.cbz(arm64::X0, breakLbl);

            compileStatement(wh->body.get());
            enc.b(loopLbl);

            enc.bindLabel(breakLbl);
            breakLabels.pop_back();
            continueLabels.pop_back();
            return;
        }

        // 7. RangeForNode (for <int> i in range(start, stop, step) { ... })
        if (auto rf = dynamic_cast<RangeForNode*>(node)) {
            enterScope();
            int32_t varOff = declareVar(rf->varName, ThavaType::INT);

            // Initialize variable with start
            compileExpression(rf->startExpr.get());
            enc.str_imm(arm64::X0, arm64::FP, varOff);

            size_t loopLbl  = enc.createLabel();
            size_t contLbl  = enc.createLabel();
            size_t breakLbl = enc.createLabel();

            breakLabels.push_back(breakLbl);
            continueLabels.push_back(contLbl);

            enc.bindLabel(loopLbl);

            // Check condition: i < stop
            enc.ldr_imm(arm64::X1, arm64::FP, varOff);
            enc.str_imm(arm64::X1, arm64::SP, -16);
            enc.sub_rri(arm64::SP, arm64::SP, 16);

            compileExpression(rf->stopExpr.get()); // X0 = stop
            enc.ldr_imm(arm64::X1, arm64::SP, 0);  // X1 = i
            enc.add_rri(arm64::SP, arm64::SP, 16);

            enc.cmp_rr(arm64::X1, arm64::X0);
            enc.b_cond(arm64::GE, breakLbl);

            // Body
            compileStatement(rf->body.get());

            // Continue label: increment i by step
            enc.bindLabel(contLbl);
            if (rf->stepExpr) {
                compileExpression(rf->stepExpr.get());
            } else {
                enc.movz(arm64::X0, 1, 0); // default step = 1
            }
            enc.ldr_imm(arm64::X1, arm64::FP, varOff);
            enc.add_rrr(arm64::X1, arm64::X1, arm64::X0);
            enc.str_imm(arm64::X1, arm64::FP, varOff);
            enc.b(loopLbl);

            enc.bindLabel(breakLbl);
            breakLabels.pop_back();
            continueLabels.pop_back();
            exitScope();
            return;
        }

        // 8. BreakNode & ContinueNode
        if (dynamic_cast<BreakNode*>(node)) {
            if (!breakLabels.empty()) {
                enc.b(breakLabels.back());
            }
            return;
        }
        if (dynamic_cast<ContinueNode*>(node)) {
            if (!continueLabels.empty()) {
                enc.b(continueLabels.back());
            }
            return;
        }

        // 9. ReturnNode (return expr)
        if (auto ret = dynamic_cast<ReturnNode*>(node)) {
            if (ret->expr) {
                compileExpression(ret->expr.get());
            } else {
                enc.movz(arm64::X0, 0, 0);
            }

            // Save return value on stack across defers
            enc.str_imm(arm64::X0, arm64::SP, -16);
            enc.sub_rri(arm64::SP, arm64::SP, 16);

            // Execute function defers in reverse LIFO order
            for (int d = static_cast<int>(functionDefers.size()) - 1; d >= 0; --d) {
                compileStatement(functionDefers[d]->stmt.get());
            }

            // Restore return value
            enc.ldr_imm(arm64::X0, arm64::SP, 0);
            enc.add_rri(arm64::SP, arm64::SP, 16);

            enc.b(currentFuncRetLabel);
            return;
        }

        // 10. DeferNode (defer stmt)
        if (auto def = dynamic_cast<DeferNode*>(node)) {
            functionDefers.push_back(def);
            return;
        }

        // 11. ExpressionStatementNode
        if (auto es = dynamic_cast<ExpressionStatementNode*>(node)) {
            compileExpression(es->expr.get());
            return;
        }
    }

    // ==========================================
    // FUNCTION CODE GENERATION
    // ==========================================
    void emitUserFunction(FunctionDeclNode* func) {
        FuncInfo& fi = functions[func->name];
        enc.bindLabel(fi.labelId);

        // Standard AAPCS64 Prologue:
        // Save FP and LR, establish frame pointer, allocate 512 bytes for locals
        enc.stp_pre(arm64::FP, arm64::LR, arm64::SP, -16);
        enc.mov_rr(arm64::FP, arm64::SP);
        enc.sub_rri(arm64::SP, arm64::SP, 512);

        enterScope();
        currentFrameOffset = 0;
        currentFuncRetLabel = enc.createLabel();
        currentFuncRetType = func->returnType;

        std::vector<DeferNode*> prevDefers = functionDefers;
        functionDefers.clear();

        // Map parameters from X0..X7 to local stack slots at [FP - offset]
        for (size_t p = 0; p < func->params.size(); ++p) {
            int32_t off = declareVar(func->params[p].second, func->params[p].first);
            if (p < 8) {
                arm64::Reg pReg = static_cast<arm64::Reg>(p);
                enc.str_imm(pReg, arm64::FP, off);
            }
        }

        // Compile function body statements
        for (const auto& s : func->body) {
            compileStatement(s.get());
        }

        // If execution falls through without return, run defers and zero X0
        for (int d = static_cast<int>(functionDefers.size()) - 1; d >= 0; --d) {
            compileStatement(functionDefers[d]->stmt.get());
        }
        enc.movz(arm64::X0, 0, 0);

        // Epilogue label (jumped to by ReturnNode)
        enc.bindLabel(currentFuncRetLabel);

        exitScope();
        functionDefers = prevDefers;

        // Epilogue: restore SP, restore FP, LR and return
        enc.mov_rr(arm64::SP, arm64::FP);
        enc.ldp_post(arm64::FP, arm64::LR, arm64::SP, 16);
        enc.ret();
    }

    // ==========================================
    // PROGRAM COMPILATION ENTRY POINT
    // ==========================================
    bool compileProgram(const std::vector<ASTPtr>& ast, const std::string& outputFile) {
        runtime.init();

        // 1. Jump over functions to program entry point (_start)
        size_t entryLabel = enc.createLabel();
        enc.b(entryLabel);

        // 2. Pre-register all user functions & forward declarations
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

        // 3. Emit bodies for all user functions
        for (const auto& node : ast) {
            if (auto func = dynamic_cast<FunctionDeclNode*>(node.get())) {
                emitUserFunction(func);
            }
        }

        // 4. Main Entry Point (_start)
        enc.bindLabel(entryLabel);
        // Setup main frame
        enc.stp_pre(arm64::FP, arm64::LR, arm64::SP, -16);
        enc.mov_rr(arm64::FP, arm64::SP);
        enc.sub_rri(arm64::SP, arm64::SP, 512);

        enterScope();
        currentFrameOffset = 0;

        topLevelDefers.clear();

        // Compile top-level statements
        for (const auto& node : ast) {
            if (dynamic_cast<FunctionDeclNode*>(node.get())) continue;
            if (dynamic_cast<ForwardDeclNode*>(node.get())) continue;
            if (dynamic_cast<CFuncDeclNode*>(node.get())) continue;
            if (dynamic_cast<ClassDeclNode*>(node.get())) continue;
            if (dynamic_cast<StructDeclNode*>(node.get())) continue;
            if (dynamic_cast<EnumDeclNode*>(node.get())) continue;

            if (auto def = dynamic_cast<DeferNode*>(node.get())) {
                topLevelDefers.push_back(def);
                continue;
            }

            compileStatement(node.get());
        }

        // Execute top-level defers in reverse LIFO order
        for (int d = static_cast<int>(topLevelDefers.size()) - 1; d >= 0; --d) {
            compileStatement(topLevelDefers[d]->stmt.get());
        }

        exitScope();

        // Exit with status 0
        enc.movz(arm64::X0, 0, 0);
        enc.bl(runtime.lblExit);

        // 5. Emit runtime helper routines
        runtime.emitHelpers();

        // 6. Resolve all branch and data offset fixups
        uint32_t dataRva = 0;
        if constexpr (std::is_same_v<TEmitter, ElfArm64Emitter>) {
            dataRva = emitter.getDataRva();
        } else {
            dataRva = emitter.getRdataRva();
        }
        enc.applyFixups(emitter.textRva, dataRva);

        // 7. Hand machine code to emitter and write to disk
        emitter.setTextCode(enc.code);
        return emitter.buildAndWrite(outputFile);
    }
};

using Arm64ElfCodegen = Arm64Codegen<ElfArm64Emitter, Arm64NativeRuntime>;
using Arm64PeCodegen  = Arm64Codegen<PeEmitter, Arm64WinRuntime>;

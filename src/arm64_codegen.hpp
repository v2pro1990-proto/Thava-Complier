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
        // Collection parameters (<lst>/<tple>/<dict>) are passed by reference:
        // listOutSlot holds the address of the caller's slot for that variable.
        bool listOut = false;
        int32_t listOutSlot = 0;
    };

    static bool isCollectionType(ThavaType t) {
        return t == ThavaType::LIST || t == ThavaType::TUPLE || t == ThavaType::DICT || t == ThavaType::MULTYPE;
    }

    struct FuncInfo {
        size_t labelId;
        ThavaType returnType;
        std::vector<std::pair<ThavaType, std::string>> params;
    };

    std::map<std::string, FuncInfo> functions;
    std::map<std::string, ThavaType> listElemTypes;
    ThavaType lastCollectionElemType = ThavaType::INT;
    std::vector<std::map<std::string, VarInfo>> scopeStack;
    int32_t currentFrameOffset = 0;
    size_t currentFuncRetLabel = 0;
    ThavaType currentFuncRetType = ThavaType::UNKNOWN;
    std::string currentFuncRetWidth; // "" = int32, "int64", "int16"

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

    // Top-level variables (globals) live in the program frame. A data cell holds
    // that frame pointer so functions and methods can reach them.
    uint32_t globalBaseCell = 0;
    std::map<std::string, int32_t> globalVarSlots;
    bool emittingProgramBody = false;
    static constexpr const char* peInitializerName = "_thava_init";
    static constexpr const char* peMetadataName = "_thava_get_metadata";
    std::map<std::pair<std::string, std::string>, size_t> peLibraryThunks;
    std::vector<size_t> peLibraryInitializers;


    void loadCollectionPtr(const std::string& name, VarInfo* local, arm64::Reg dst) {
        if (local) enc.ldr_imm(dst, arm64::FP, local->offset);
        else {
            emitGlobalBase(dst);
            enc.ldr_imm(dst, dst, globalVarSlots.at(name));
        }
    }

    bool isGlobalName(const std::string& name) const {
        return globalVarSlots.count(name) > 0;
    }

    // First free frame slot of the program body (after the globals).
    int32_t programFrameBase = 0;

    // dst = frame pointer of the program body (globals base); the slot offset is
    // applied by the caller as the load/store displacement.
    void emitGlobalBase(arm64::Reg dst) {
        enc.adr_data(dst, globalBaseCell);
        enc.ldr_imm(dst, dst, 0);
    }

    // Thava is function-scoped: a declaration inside any block (if/while/for/try)
    // stays visible until the end of the enclosing function, so nested blocks do
    // not open their own scope. Only functions, methods and the top-level body do.
    void enterScope() {}

    void exitScope() {}

    void enterFunctionScope() {
        scopeStack.push_back({});
    }

    void exitFunctionScope() {
        if (!scopeStack.empty()) scopeStack.pop_back();
    }

    // Scopes below this index are invisible: while a function/method is emitted,
    // the program scope (globals as frame slots of main) must not resolve.
    size_t lookupFloor = 0;

    VarInfo* lookupVar(const std::string& name) {
        for (int i = static_cast<int>(scopeStack.size()) - 1; i >= static_cast<int>(lookupFloor); --i) {
            auto it = scopeStack[i].find(name);
            if (it != scopeStack[i].end()) return &it->second;
        }
        return nullptr;
    }

    // Truncates X0 to the declared integer width and sign-extends it back:
    // <int> = 32-bit, <int16> = 16-bit, <int64> = 64-bit.
    void narrowIntX0(const std::string& width) {
        if (width == "int64") return;
        uint32_t bits = (width == "int16") ? 16 : 32;
        // Shift the whole 64-bit register so the sign bit of the target width
        // lands in bit 63, then shift back to sign-extend.
        uint32_t shift = 64 - bits;
        enc.lsl_ri(arm64::X0, arm64::X0, shift);
        enc.asr_ri(arm64::X0, arm64::X0, shift);
    }

    void storeIntVar(int32_t off, VarInfo* v) {
        if (v && v->type == ThavaType::INT) narrowIntX0(v->customType);
        enc.str_imm(arm64::X0, arm64::FP, off);
    }

    int32_t declareVar(const std::string& name, ThavaType type, const std::string& customType = "") {
        currentFrameOffset -= 8;
        VarInfo info{ currentFrameOffset, type, customType };
        scopeStack.back()[name] = info;
        return currentFrameOffset;
    }

    // ==========================================
    // Double helpers. Doubles are carried as raw 64-bit patterns in X0,
    // exactly like the x86-64 backend keeps them as 8-byte slots.
    // ==========================================
    void x0BitsToD0() {
        enc.sub_rri(arm64::SP, arm64::SP, 16);
        enc.str_imm(arm64::X0, arm64::SP, 0);
        enc.fldr_imm(arm64::D0, arm64::SP, 0);
        enc.add_rri(arm64::SP, arm64::SP, 16);
    }

    void d0BitsToX0() {
        enc.sub_rri(arm64::SP, arm64::SP, 16);
        enc.fstr_imm(arm64::D0, arm64::SP, 0);
        enc.ldr_imm(arm64::X0, arm64::SP, 0);
        enc.add_rri(arm64::SP, arm64::SP, 16);
    }

    // X0 (integer) -> X0 (double bits)
    void intToDoubleBits() {
        enc.scvtf_dr(arm64::D0, arm64::X0);
        d0BitsToX0();
    }

    // X0 (double bits) -> X0 (integer, truncation)
    // Exponentiation by squaring. X1 holds the base and X0 the signed exponent.
    void emitIntegerPower() {
        auto loop = enc.createLabel(), square = enc.createLabel(), done = enc.createLabel();
        enc.movz(arm64::X2, 0, 0);
        enc.cmp_ri(arm64::X0, 0);
        enc.b_cond(arm64::LT, done);
        enc.movz(arm64::X2, 1, 0);
        enc.movz(arm64::X3, 1, 0);
        enc.bindLabel(loop);
        enc.cbz(arm64::X0, done);
        enc.and_rrr(arm64::X4, arm64::X0, arm64::X3);
        enc.cbz(arm64::X4, square);
        enc.mul_rrr(arm64::X2, arm64::X2, arm64::X1);
        enc.bindLabel(square);
        enc.mul_rrr(arm64::X1, arm64::X1, arm64::X1);
        enc.lsr_ri(arm64::X0, arm64::X0, 1);
        enc.b(loop);
        enc.bindLabel(done);
        enc.mov_rr(arm64::X0, arm64::X2);
    }

    void doubleBitsToInt() {
        x0BitsToD0();
        enc.fcvtzs_rd(arm64::X0, arm64::D0);
    }

    // Load left (X1) and right (X0) operands as doubles into D0 and D1
    void loadOperandsAsDoubles(ThavaType lt, ThavaType rt) {
        bool lf = (lt == ThavaType::DOUBLE || lt == ThavaType::FLOAT);
        bool rf = (rt == ThavaType::DOUBLE || rt == ThavaType::FLOAT);

        enc.sub_rri(arm64::SP, arm64::SP, 16);
        enc.str_imm(arm64::X0, arm64::SP, 0); // save right
        if (!lf) {
            enc.mov_rr(arm64::X0, arm64::X1);
            intToDoubleBits();
            enc.mov_rr(arm64::X1, arm64::X0);
        }
        enc.ldr_imm(arm64::X0, arm64::SP, 0);
        enc.add_rri(arm64::SP, arm64::SP, 16);
        if (!rf) intToDoubleBits();

        enc.sub_rri(arm64::SP, arm64::SP, 16);
        enc.str_imm(arm64::X0, arm64::SP, 0);
        enc.fldr_imm(arm64::D1, arm64::SP, 0); // D1 = right
        enc.add_rri(arm64::SP, arm64::SP, 16);

        enc.sub_rri(arm64::SP, arm64::SP, 16);
        enc.str_imm(arm64::X1, arm64::SP, 0);
        enc.fldr_imm(arm64::D0, arm64::SP, 0); // D0 = left
        enc.add_rri(arm64::SP, arm64::SP, 16);
    }

    static bool isListDir(ASTNode* n) {
        const auto* os = dynamic_cast<OsMethodNode*>(n);
        return os && os->method == "listdir";
    }

    // Element type of `base[i]`: known from the list variable's declaration, or
    // inferred from a list literal such as `[1.5, 2.5][i]`.
    ThavaType elemTypeOfIndexedList(ASTNode* base) {
        if (isListDir(base)) return ThavaType::STRING;
        if (auto v = dynamic_cast<VarNode*>(base)) {
            auto it = listElemTypes.find(v->getName());
            if (it != listElemTypes.end()) return it->second;
            return ThavaType::UNKNOWN;
        }
        if (auto coll = dynamic_cast<CollectionNode*>(base)) {
            ThavaType found = ThavaType::INT;
            for (auto& el : coll->getElements()) {
                ThavaType t = staticExprType(el.get());
                if (t == ThavaType::DOUBLE || t == ThavaType::FLOAT) return ThavaType::DOUBLE;
                if (t == ThavaType::STRING) found = ThavaType::STRING;
            }
            return found;
        }
        return ThavaType::UNKNOWN;
    }

    // Static (compile-time) type inference for expressions
    ThavaType staticExprType(ASTNode* n) {
        if (dynamic_cast<DecimalParseNode*>(n)) return ThavaType::DECIMAL;
        if (auto minus = dynamic_cast<UnaryMinusNode*>(n); minus && minus->decimalArithmetic)
            return ThavaType::DECIMAL;
        if (auto binary = dynamic_cast<BinaryOpNode*>(n); binary && binary->decimalOp >= 0)
            return decimalComparison(binary->decimalOp) ? ThavaType::BOOL : ThavaType::DECIMAL;
        if (dynamic_cast<InterpolatedStringNode*>(n)) return ThavaType::STRING;
        if (!n) return ThavaType::UNKNOWN;
        if (auto lit = dynamic_cast<LiteralNode*>(n)) return lit->getLitType();
        if (auto v = dynamic_cast<VarNode*>(n)) {
            VarInfo* vi = lookupVar(v->getName());
            if (vi) return vi->type;
            auto f = functions.find(v->getName());
            if (f != functions.end()) return ThavaType::FUNC;
            return ThavaType::UNKNOWN;
        }
        if (auto c = dynamic_cast<CastNode*>(n)) return c->targetType;
        if (auto bin = dynamic_cast<BinaryOpNode*>(n)) {
            const std::string& op = bin->op;
            if (op == "==" || op == "!=" || op == "<" || op == "<=" ||
                op == ">" || op == ">=" || op == "and" || op == "or") {
                return ThavaType::BOOL;
            }
            ThavaType lt = staticExprType(bin->left.get());
            ThavaType rt = staticExprType(bin->right.get());
            if (lt == ThavaType::DOUBLE || rt == ThavaType::DOUBLE ||
                lt == ThavaType::FLOAT || rt == ThavaType::FLOAT) {
                return ThavaType::DOUBLE;
            }
            return ThavaType::INT;
        }
        if (auto un = dynamic_cast<UnaryMinusNode*>(n)) return staticExprType(un->expr.get());
        if (dynamic_cast<UnaryNotNode*>(n)) return ThavaType::BOOL;
        if (auto cond = dynamic_cast<ConditionalExprNode*>(n)) {
            ThavaType tt = staticExprType(cond->trueExpr.get());
            ThavaType ft = staticExprType(cond->falseExpr.get());
            if (tt == ThavaType::DOUBLE || ft == ThavaType::DOUBLE) return ThavaType::DOUBLE;
            if (tt == ThavaType::FLOAT || ft == ThavaType::FLOAT) return ThavaType::FLOAT;
            return tt;
        }
        if (auto call = dynamic_cast<CallNode*>(n)) {
            auto f = functions.find(call->name);
            if (f != functions.end()) return f->second.returnType;
            auto imported = g_dlFunctions.find(call->name);
            if (imported != g_dlFunctions.end()) return imported->second.retType;
            VarInfo* vi = lookupVar(call->name);
            if (vi && vi->type == ThavaType::FUNC) return ThavaType::INT;
            return ThavaType::INT;
        }
        if (auto coll = dynamic_cast<CollectionNode*>(n)) {
            return coll->getIsTuple() ? ThavaType::TUPLE : ThavaType::LIST;
        }
        if (auto idx = dynamic_cast<IndexNode*>(n)) {
            ThavaType et = elemTypeOfIndexedList(idx->base.get());
            if (et != ThavaType::UNKNOWN) return et;
            return ThavaType::INT;
        }
        if (auto lm = dynamic_cast<ListMethodNode*>(n)) {
            if (lm->method == "len" || lm->method == "size") return ThavaType::INT;
            return ThavaType::UNKNOWN;
        }
        if (dynamic_cast<LenNode*>(n)) return ThavaType::INT;
        if (dynamic_cast<DictNode*>(n)) return ThavaType::DICT;
        if (auto dm = dynamic_cast<DictMethodNode*>(n)) {
            if (dm->method == "len" || dm->method == "size") return ThavaType::INT;
            if (dm->method == "keys" || dm->method == "values") return ThavaType::LIST;
            if (dm->method == "has" || dm->method == "contains") return ThavaType::BOOL;
            return ThavaType::UNKNOWN;
        }
        if (dynamic_cast<StrSubstringNode*>(n)) return ThavaType::STRING;
        return ThavaType::UNKNOWN;
    }

    // ==========================================
    // Unsupported constructs: fail loudly instead of emitting a wrong binary
    // ==========================================
    static bool unsupportedFeatureName(ASTNode* n, std::string& what) {
        if (dynamic_cast<CLibMethodNode*>(n))      { what = "clib.* (C library calls)"; return true; }
        if (dynamic_cast<CCallNode*>(n))           { what = "C function calls"; return true; }
        return false;
    }

    static bool hasWindowsOsMethod(const std::string& name) {
        static const std::set<std::string> methods = {
            "name", "system", "getpid", "cpu_count", "getcwd", "chdir", "mkdir",
            "rmdir", "remove", "rename", "exists", "is_file", "is_dir", "getsize",
            "getenv", "sleep", "listdir", "setenv", "showmessagebox"
        };
        return methods.count(name) != 0;
    }

    static int windowsTimeField(const std::string& name) {
        // Byte offsets of the WORD fields in SYSTEMTIME.
        static const std::map<std::string, int> offsets = {
            {"year", 0}, {"month", 2}, {"day", 6}, {"hour", 8}, {"minute", 10},
            {"second", 12}, {"millis", 14}
        };
        auto field = offsets.find(name);
        return field == offsets.end() ? -1 : field->second;
    }

    // Features that only the Linux ARM64 runtime can service (syscalls/helpers
    // that the Windows ARM64 runtime does not provide yet).
    static const char* linuxOnlyFeature(ASTNode* n) {
        if (auto os = dynamic_cast<OsMethodNode*>(n); os && !hasWindowsOsMethod(os->method))
            return "this os.* method";
        if (auto time = dynamic_cast<TimeMethodNode*>(n); time && time->method != "timestamp" &&
            time->method != "sleep" && time->method != "now" && time->method != "date" &&
            windowsTimeField(time->method) < 0)
            return "this time.* method";
        return nullptr;
    }

    // Resolves the declared struct type of a member-access chain such as
    // `p`, `p.inner` (used to look up field offsets).
    std::string resolveStructName(ASTNode* target) {
        if (auto v = dynamic_cast<VarNode*>(target)) {
            VarInfo* vi = lookupVar(v->getName());
            return vi ? vi->customType : std::string();
        }
        if (dynamic_cast<LocVarNode*>(target)) return currentClassName;
        if (auto p = dynamic_cast<ParVarNode*>(target)) return p->baseClassName;
        if (auto sm = dynamic_cast<MemberAccessNode*>(target)) {
            std::string parent = resolveStructName(sm->target.get());
            auto it = g_structs.find(parent);
            if (it != g_structs.end()) {
                auto fit = it->second.fieldIndex.find(sm->memberName);
                if (fit != it->second.fieldIndex.end())
                    return it->second.fields[fit->second].customType;
            }
        }
        return std::string();
    }

    // ==========================================
    // CLASSES (single inheritance, static dispatch) + EXCEPTIONS
    // Instance = flat heap block (inherited fields first, 8-byte slots).
    // Methods take the implicit 'this' as X0, then the declared parameters.
    // The active exception handler record pointer lives in X28 (0 = none).
    // ==========================================
    std::map<std::string, StructInfo> classLayouts;
    std::map<std::string, std::string> classBase;
    std::map<std::string, std::vector<std::pair<int32_t, ASTNode*>>> classFieldDefaults;
    std::map<std::string, size_t> classMethodLabels;
    std::map<std::string, ThavaType> classMethodRet;
    std::map<std::string, std::vector<std::pair<ThavaType, std::string>>> classMethodParams;
    std::string currentClassName;
    std::string currentBaseClassName;
    size_t lblThrow = 0;
    std::optional<size_t> decimalEntry;
    std::optional<size_t> mutationEntry;
    size_t mutationAllocator = 0;
    size_t lblBoxedToString = 0;
    size_t lblMultypeToString = 0;
    bool usesMultypeFormatting = false;

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
                std::cerr << "[ARM64 Codegen Error]: base class '" << cd->baseClassName
                          << "' of '" << cd->className << "' is not declared before it.\n";
                exit(1);
            }
            layout = bit->second;
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
        g_structs[cd->className] = layout;

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
        enc.stp_pre(arm64::FP, arm64::LR, arm64::SP, -16);
        enc.mov_rr(arm64::FP, arm64::SP);
        const auto localFrame = reserveLocalFrame();

        enterFunctionScope();
        currentFrameOffset = 0;
        currentFuncRetLabel = enc.createLabel();
        currentFuncRetType = retType;
        currentFuncRetWidth = retWidth;

        std::vector<DeferNode*> prevDefers = functionDefers;
        functionDefers.clear();

        int32_t thisOff = declareVar("@this", ThavaType::CUSTOM, className);
        enc.str_imm(arm64::X0, arm64::FP, thisOff);

        for (size_t p = 0; p < params.size(); ++p) {
            std::string pct = (p < paramCustoms.size()) ? paramCustoms[p] : std::string();
            int32_t off = declareVar(params[p].second, params[p].first, pct);
            if (isCollectionType(params[p].first)) {
                // Element type comes from the call sites of this method.
                auto eit = g_funcListParamElem.find(className + "::" + methodName);
                ThavaType et = (eit != g_funcListParamElem.end() && p < eit->second.size())
                                   ? eit->second[p] : ThavaType::UNKNOWN;
                if (et != ThavaType::UNKNOWN) listElemTypes[params[p].second] = et;
                else listElemTypes.erase(params[p].second);
                // Collection parameters arrive as the address of the caller's slot.
                int32_t refSlot = declareVar("@ref" + std::to_string(p), ThavaType::INT);
                VarInfo* pv = lookupVar(params[p].second);
                if (pv) { pv->listOut = true; pv->listOutSlot = refSlot; }
                if (p < 7) {
                    enc.str_imm(static_cast<arm64::Reg>(p + 1), arm64::FP, refSlot);
                } else {
                    enc.ldr_imm(arm64::X9, arm64::FP, (int32_t)(16 + 8 * (int)(p - 7)));
                    enc.str_imm(arm64::X9, arm64::FP, refSlot);
                }
                enc.ldr_imm(arm64::X9, arm64::FP, refSlot);
                enc.ldr_imm(arm64::X9, arm64::X9, 0);
                enc.str_imm(arm64::X9, arm64::FP, off);
            } else if (p < 7) {
                arm64::Reg pReg = static_cast<arm64::Reg>(p + 1);
                if (params[p].first == ThavaType::INT && pct != "int64") {
                    uint32_t bits = (pct == "int16") ? 16 : 32;
                    enc.lsl_ri(pReg, pReg, 64 - bits);
                    enc.asr_ri(pReg, pReg, 64 - bits);
                }
                enc.str_imm(pReg, arm64::FP, off);
            } else {
                // Extra parameters arrive on the caller's stack (AAPCS64: [SP] at entry,
                // which is [FP + 16] after our stp fp, lr frame).
                enc.ldr_imm(arm64::X9, arm64::FP, (int32_t)(16 + 8 * (int)(p - 7)));
                if (params[p].first == ThavaType::INT && pct != "int64") {
                    uint32_t bits = (pct == "int16") ? 16 : 32;
                    enc.lsl_ri(arm64::X9, arm64::X9, 64 - bits);
                    enc.asr_ri(arm64::X9, arm64::X9, 64 - bits);
                }
                enc.str_imm(arm64::X9, arm64::FP, off);
            }
        }

        for (const auto& s : body) compileStatement(s.get());

        enc.movz(arm64::X0, 0, 0);
        enc.bindLabel(currentFuncRetLabel);

        for (size_t p = 0; p < params.size(); ++p) {
            if (!isCollectionType(params[p].first)) continue;
            VarInfo* pv = lookupVar(params[p].second);
            if (!pv || !pv->listOut) continue;
            enc.ldr_imm(arm64::X9, arm64::FP, pv->offset);
            enc.ldr_imm(arm64::X10, arm64::FP, pv->listOutSlot);
            enc.str_imm(arm64::X9, arm64::X10, 0);
        }

        finalizeLocalFrame(localFrame);
        exitFunctionScope();
        functionDefers = prevDefers;

        enc.mov_rr(arm64::SP, arm64::FP);
        enc.ldp_post(arm64::FP, arm64::LR, arm64::SP, 16);
        enc.ret();

        currentClassName = prevClass;
        currentBaseClassName = prevBase;
    }

    // Put the address of an argument's storage into dst: the caller's variable
    // slot when the argument is a plain variable, otherwise the temporary slot at
    // [SP + fallbackSpOffset].
    void emitArgAddress(arm64::Reg dst, ASTNode* argNode, int32_t fallbackSpOffset) {
        if (auto variable = dynamic_cast<VarNode*>(argNode)) {
            const auto& name = variable->getName();
            if (auto local = lookupVar(name)) {
                enc.sub_rri(dst, arm64::FP, static_cast<uint32_t>(-local->offset));
                return;
            }
            if (auto global = globalVarSlots.find(name); global != globalVarSlots.end()) {
                emitGlobalBase(dst);
                enc.sub_rri(dst, dst, static_cast<uint32_t>(-global->second));
                return;
            }
        }
        enc.add_rri(dst, arm64::SP, static_cast<uint32_t>(fallbackSpOffset));
    }

    static uint32_t alignedStackBytes(size_t slots) {
        return static_cast<uint32_t>((slots * 8 + 15) & ~size_t(15));
    }

    template<class ParameterType>
    uint32_t emitThavaCallArguments(const std::vector<ASTPtr>& args, ParameterType parameterType) {
        using namespace arm64;
        const size_t count = args.size();
        if (count > 240) {
            std::cerr << "[ARM64 Codegen Error]: Too many function arguments.\n";
            exit(1);
        }
        const uint32_t outgoing = alignedStackBytes(count > 8 ? count - 8 : 0);
        const uint32_t frame = outgoing + alignedStackBytes(count);
        if (frame) enc.sub_rri(SP, SP, frame);
        for (size_t i = 0; i < count; ++i) {
            const auto formal = parameterType(i);
            const auto actual = compileExpression(args[i].get(), formal);
            if ((formal == ThavaType::DOUBLE || formal == ThavaType::FLOAT) && actual == ThavaType::INT)
                intToDoubleBits();
            else if (formal == ThavaType::INT && (actual == ThavaType::DOUBLE || actual == ThavaType::FLOAT))
                doubleBitsToInt();
            enc.str_imm(X0, SP, outgoing + 8 * i);
        }
        for (size_t i = 0; i < count; ++i) {
            const int32_t slot = outgoing + 8 * i;
            const auto destination = i < 8 ? static_cast<Reg>(i) : X9;
            if (isCollectionType(parameterType(i))) emitArgAddress(destination, args[i].get(), slot);
            else enc.ldr_imm(destination, SP, slot);
            if (i >= 8) enc.str_imm(destination, SP, 8 * (i - 8));
        }
        return frame;
    }

    size_t reserveLocalFrame() {
        const size_t offset = enc.code.size();
        enc.sub_rri(arm64::SP, arm64::SP, 0);
        return offset;
    }

    void finalizeLocalFrame(size_t offset) {
        const uint32_t bytes = alignedStackBytes(static_cast<size_t>(-currentFrameOffset) / 8);
        // A single allocation below one page keeps Windows guard-page probing unnecessary.
        if (bytes > 4080) {
            std::cerr << "[ARM64 Codegen Error]: Local variables require more than 4080 stack bytes.\n";
            exit(1);
        }
        enc.code[offset + 1] |= static_cast<uint8_t>(bytes << 2);
        enc.code[offset + 2] |= static_cast<uint8_t>(bytes >> 6);
    }

    // Emit a class method / constructor call: this in X0, args in X1..X7,
    // further args on the caller stack (AAPCS64).
    void emitClassCall(size_t lbl, const std::vector<ASTPtr>& args, ASTNode* target, bool thisOnStack,
                       const std::vector<std::pair<ThavaType, std::string>>* formal = nullptr) {
        const size_t n = args.size();
        if (n > 160) {
            std::cerr << "[ARM64 Codegen Error]: Too many method arguments.\n";
            exit(1);
        }
        const uint32_t reserve = static_cast<uint32_t>(16 * (n + 1));

        if (thisOnStack) {
            enc.ldr_imm(arm64::X9, arm64::SP, 0);
            enc.sub_rri(arm64::SP, arm64::SP, reserve);
            enc.str_imm(arm64::X9, arm64::SP, (int32_t)(16 * n));
        } else {
            enc.sub_rri(arm64::SP, arm64::SP, reserve);
            compileExpression(target);
            enc.str_imm(arm64::X0, arm64::SP, (int32_t)(16 * n));
        }

        for (size_t i = 0; i < n; ++i) {
            const auto expected = formal && i < formal->size() ? (*formal)[i].first : ThavaType::UNKNOWN;
            compileExpression(args[i].get(), expected);
            enc.str_imm(arm64::X0, arm64::SP, (int32_t)(16 * (n - 1 - i)));
        }

        enc.ldr_imm(arm64::X0, arm64::SP, (int32_t)(16 * n)); // this

        const size_t regCount = (n < 7) ? n : 7;
        for (size_t i = 0; i < regCount; ++i) {
            arm64::Reg r = static_cast<arm64::Reg>(i + 1);
            bool byRef = formal && (i < formal->size()) && isCollectionType((*formal)[i].first);
            if (byRef) {
                emitArgAddress(r, args[i].get(), (int32_t)(16 * (n - 1 - i)));
            } else {
                enc.ldr_imm(r, arm64::SP, (int32_t)(16 * (n - 1 - i)));
            }
        }

        const size_t extras = (n > 7) ? (n - 7) : 0;
        const uint32_t outgoing = alignedStackBytes(extras);
        if (outgoing) {
            enc.sub_rri(arm64::SP, arm64::SP, outgoing);
            for (size_t k = 0; k < extras; ++k) {
                bool byRef = formal && ((7 + k) < formal->size()) &&
                             isCollectionType((*formal)[7 + k].first);
                if (byRef) {
                    emitArgAddress(arm64::X9, args[7 + k].get(),
                                   (int32_t)(outgoing + 16 * (n - 1 - (7 + k))));
                } else {
                    enc.ldr_imm(arm64::X9, arm64::SP,
                                (int32_t)(outgoing + 16 * (n - 1 - (7 + k))));
                }
                enc.str_imm(arm64::X9, arm64::SP, (int32_t)(8 * k));
            }
        }

        enc.bl(lbl);
        enc.add_rri(arm64::SP, arm64::SP, reserve + outgoing);
    }

    // X0 = message, X1 = type name. Never returns.
    void emitThrowHelper() {
        lblThrow = enc.createLabel();
        enc.bindLabel(lblThrow);

        size_t fatal = enc.createLabel();
        enc.cbz(arm64::X28, fatal);

        enc.str_imm(arm64::X0, arm64::X28, 0);
        enc.str_imm(arm64::X1, arm64::X28, 8);
        enc.ldr_imm(arm64::X9, arm64::X28, 32);  // saved FP
        enc.ldr_imm(arm64::X10, arm64::X28, 24); // resume address
        enc.ldr_imm(arm64::X11, arm64::X28, 16); // previous handler
        enc.mov_rr(arm64::FP, arm64::X9);
        enc.ldr_imm(arm64::X12, arm64::X28, 40); // saved SP
        enc.mov_rr(arm64::SP, arm64::X12);
        enc.mov_rr(arm64::X28, arm64::X11);
        enc.br(arm64::X10);

        enc.bindLabel(fatal);
        enc.mov_rr(arm64::X20, arm64::X0);
        enc.adr_data(arm64::X0, emitter.addStringLiteralOffset("[Runtime Exception]: "));
        enc.bl(runtime.lblPrintStr);
        size_t haveMsg = enc.createLabel();
        enc.cmp_ri(arm64::X20, 0);
        enc.b_cond(arm64::NE, haveMsg);
        enc.adr_data(arm64::X20, emitter.addStringLiteralOffset("Runtime Error!"));
        enc.bindLabel(haveMsg);
        enc.mov_rr(arm64::X0, arm64::X20);
        enc.bl(runtime.lblPrintStrNL);
        enc.movz(arm64::X0, 1, 0);
        enc.bl(runtime.lblExit);
    }

    void emitThrowCall(ASTNode* msgExpr, ASTNode* typeExpr, const std::string& defaultMsg) {
        if (msgExpr) compileExpression(msgExpr);
        else enc.adr_data(arm64::X0, emitter.addStringLiteralOffset(defaultMsg));
        enc.str_imm(arm64::X0, arm64::SP, -16);
        enc.sub_rri(arm64::SP, arm64::SP, 16);

        if (typeExpr) compileExpression(typeExpr);
        else enc.adr_data(arm64::X0, emitter.addStringLiteralOffset("RuntimeError"));
        enc.mov_rr(arm64::X1, arm64::X0);

        enc.ldr_imm(arm64::X0, arm64::SP, 0);
        enc.add_rri(arm64::SP, arm64::SP, 16);
        enc.bl(lblThrow);
    }

    void checkRuntimeSupport(ASTNode* n) {
        if constexpr (std::is_same_v<TRuntime, Arm64NativeRuntime>) {
            (void)n;
            return;
        } else {
            const char* f = linuxOnlyFeature(n);
            if (f) {
                std::cerr << "[ARM64 Codegen Error]: " << f
                          << " is implemented only by the Linux ARM64 runtime.\n"
                          << "                    Use --backend arm64 (Linux) or"
                          << " --backend native/elf (x86-64).\n";
                exit(1);
            }
        }
    }

    void checkUnsupported(ASTNode* n) {
        if constexpr (std::is_same_v<TRuntime, Arm64WinRuntime>) return;
        std::string what;
        if (unsupportedFeatureName(n, what)) {
            std::cerr << "[ARM64 Codegen Error]: " << what
                      << " is not implemented by the ARM64 backend yet.\n"
                      << "                    Compile this program with --arch x86_64"
                      << " (or --backend native/elf) instead.\n";
            exit(1);
        }
    }

    // ==========================================
    // EXPRESSION CODE GENERATION
    // Evaluates expression and leaves result in X0 (integer/string/bool/ptr)
    // Returns the static ThavaType of the expression
    // ==========================================
    void emitStringCast(ThavaType srcT) {
        if (srcT == ThavaType::STRING || srcT == ThavaType::DECIMAL) return;
        // value -> heap string
        if (srcT == ThavaType::DOUBLE || srcT == ThavaType::FLOAT) {
            enc.bl(runtime.lblFileFmtDouble);
        } else if (srcT == ThavaType::BOOL) {
            size_t trueLbl = enc.createLabel();
            size_t endLbl = enc.createLabel();
            enc.cbnz(arm64::X0, trueLbl);
            enc.adr_data(arm64::X0, runtime.offStrFalse);
            enc.b(endLbl);
            enc.bindLabel(trueLbl);
            enc.adr_data(arm64::X0, runtime.offStrTrue);
            enc.bindLabel(endLbl);
        } else {
            enc.bl(runtime.lblIntToStr);
        }
    }

    void emitInterpolationPart(ASTNode* part) {
        const auto type = compileExpression(part);
        if (type == ThavaType::BOXED) {
            emitBoxedString();
        } else if (type == ThavaType::MULTYPE) {
            emitMultypeString();
        } else if (type == ThavaType::NONE) {
            enc.adr_data(arm64::X0, emitter.addStringLiteralOffset("none"));
        } else if (type == ThavaType::INT || type == ThavaType::FLOAT || type == ThavaType::DOUBLE ||
                   type == ThavaType::BOOL || type == ThavaType::STRING || type == ThavaType::DECIMAL) {
            emitStringCast(type);
        } else {
            std::cerr << "[ARM64 Codegen Error]: This value cannot be interpolated as a string on ARM64.\n";
            exit(1);
        }
    }

    void loadMultypeBlock(arm64::Reg reg) {
        using namespace arm64;
        const auto ready = enc.createLabel();
        enc.cbz(reg, ready);
        enc.ldr_imm(X14, reg, 0);
        enc.mov_ri64(X15, THAVA_SEQUENCE_HANDLE);
        enc.cmp_rr(X14, X15);
        enc.b_cond(NE, ready);
        enc.ldr_imm(reg, reg, 8);
        enc.bindLabel(ready);
    }

    void initMultypeHandle(arm64::Reg handle, arm64::Reg capacity) {
        using namespace arm64;
        enc.mov_ri64(X14, THAVA_SEQUENCE_HANDLE);
        enc.str_imm(X14, handle, 0);
        enc.add_rri(X14, handle, native_multype::handleSize);
        enc.str_imm(X14, handle, 8);
        enc.str_imm(capacity, handle, 16);
    }

    void mutateMultype(const std::string& name, ListMutation mutation) {
        using namespace arm64;
        if (!mutationEntry) {
            mutationEntry = enc.createLabel();
            mutationAllocator = enc.createLabel();
        }
        enc.sub_rri(SP, SP, 64);
        loadCollectionPtr(name, lookupVar(name), X0);
        enc.str_imm(X0, SP, 0);
        if (mutation.index) {
            if (compileExpression(mutation.index, ThavaType::INT) != ThavaType::INT)
                diag::fail("A <multype> list index must be an integer.", mutation.index->span);
        }
        else enc.movz(X0, 0, 0);
        enc.str_imm(X0, SP, 16);
        if (mutation.op >= THAVA_LIST_ADD) emitMultypeMutationCall(THAVA_LIST_READ);
        if (mutation.value) {
            const auto type = compileExpression(mutation.value, dynamic_cast<CollectionNode*>(mutation.value)
                                                ? ThavaType::MULTYPE : ThavaType::UNKNOWN);
            if (type == ThavaType::VOID || type == ThavaType::UNKNOWN || type == ThavaType::FILE)
                diag::fail("This value cannot be stored in a <multype> list.", mutation.value->span);
            if (type == ThavaType::BOXED) {
                enc.ldr_imm(X10, X0, 8);
                enc.ldr_imm(X0, X0, 0);
            } else {
                if (type == ThavaType::LIST || type == ThavaType::TUPLE) {
                    const auto element = elemTypeOfIndexedList(mutation.value);
                    boxPlainSequence(element == ThavaType::UNKNOWN ? ThavaType::INT : element);
                }
                enc.movz(X10, native_multype::tagFor(type == ThavaType::LIST || type == ThavaType::TUPLE
                                                     ? ThavaType::MULTYPE : type), 0);
            }
            enc.str_imm(X0, SP, 24);
            enc.str_imm(X10, SP, 32);
        } else {
            enc.str_imm(XZR, SP, 24);
            enc.str_imm(XZR, SP, 32);
        }
        emitMultypeMutationCall(mutation.op);
        enc.add_rri(SP, SP, 64);
    }

    void emitMultypeMutationCall(int op) {
        using namespace arm64;
        enc.mov_ri64(X0, op);
        enc.str_imm(X0, SP, 8);
        enc.adr(X0, mutationAllocator);
        enc.str_imm(X0, SP, 40);
        enc.add_rri(X0, SP, 0);
        enc.bl(*mutationEntry);
        const auto done = enc.createLabel();
        enc.cbz(X0, done);
        for (size_t i = 1; i < std::size(native_multype::mutationErrors); ++i) {
            const auto next = enc.createLabel();
            enc.cmp_ri(X0, i);
            enc.b_cond(NE, next);
            enc.add_rri(SP, SP, 64);
            emitThrowCall(nullptr, nullptr, native_multype::mutationErrors[i]);
            enc.bindLabel(next);
        }
        enc.bindLabel(done);
    }

    void emitBoxedString() {
        usesMultypeFormatting = true;
        enc.bl(lblBoxedToString);
    }

    void emitMultypeString() {
        usesMultypeFormatting = true;
        enc.bl(lblMultypeToString);
    }

    void emitMultypeHelpers() {
        if (!usesMultypeFormatting) return;
        using namespace arm64;
        using namespace native_multype;
        const auto done = enc.createLabel();
        const auto text = enc.createLabel();
        const auto whole = enc.createLabel();
        const auto real = enc.createLabel();
        const auto boolean = enc.createLabel();
        const auto sequence = enc.createLabel();
        const auto dictionary = enc.createLabel();
        const auto function = enc.createLabel();
        const auto object = enc.createLabel();
        auto literal = [&](const char* value) { enc.adr_data(X0, emitter.addStringLiteralOffset(value)); };

        enc.bindLabel(lblBoxedToString);
        enc.stp_pre(FP, LR, SP, -16);
        enc.mov_rr(FP, SP);
        enc.ldr_imm(X10, X0, tagOffset);
        for (const auto& [tag, label] : {
                 std::pair<Tag, size_t>{String, text}, {Integer, whole}, {Real, real},
                 {Boolean, boolean}, {Decimal, text}, {Sequence, sequence}, {Dictionary, dictionary},
                 {Function, function}, {Object, object}}) {
            enc.cmp_ri(X10, tag);
            enc.b_cond(EQ, label);
        }
        literal("none");
        enc.b(done);
        enc.bindLabel(dictionary);
        literal("<dict>");
        enc.b(done);
        enc.bindLabel(function);
        literal("<func>");
        enc.b(done);
        enc.bindLabel(object);
        literal("<object>");
        enc.b(done);
        enc.bindLabel(text);
        enc.ldr_imm(X0, X0, 0);
        enc.b(done);
        enc.bindLabel(whole);
        enc.ldr_imm(X0, X0, 0);
        emitStringCast(ThavaType::INT);
        enc.b(done);
        enc.bindLabel(real);
        const auto finite = enc.createLabel();
        const auto infinity = enc.createLabel();
        const auto negativeInfinity = enc.createLabel();
        enc.ldr_imm(X0, X0, 0);
        enc.mov_ri64(X10, 0x7FFFFFFFFFFFFFFFLL);
        enc.and_rrr(X10, X0, X10);
        enc.mov_ri64(X11, 0x7FF0000000000000LL);
        enc.cmp_rr(X10, X11);
        enc.b_cond(CC, finite);
        enc.b_cond(EQ, infinity);
        literal("nan");
        enc.b(done);
        enc.bindLabel(infinity);
        enc.cmp_ri(X0, 0);
        enc.b_cond(LT, negativeInfinity);
        literal("inf");
        enc.b(done);
        enc.bindLabel(negativeInfinity);
        literal("-inf");
        enc.b(done);
        enc.bindLabel(finite);
        emitStringCast(ThavaType::DOUBLE);
        enc.b(done);
        enc.bindLabel(boolean);
        enc.ldr_imm(X0, X0, 0);
        emitStringCast(ThavaType::BOOL);
        enc.b(done);
        enc.bindLabel(sequence);
        enc.ldr_imm(X0, X0, 0);
        enc.bl(lblMultypeToString);
        enc.bindLabel(done);
        enc.ldp_post(FP, LR, SP, 16);
        enc.ret();

        enc.bindLabel(lblMultypeToString);
        loadMultypeBlock(X0);
        enc.stp_pre(FP, LR, SP, -48);
        enc.mov_rr(FP, SP);
        enc.str_imm(X0, FP, 16); // source
        const auto loop = enc.createLabel();
        const auto append = enc.createLabel();
        const auto close = enc.createLabel();
        const auto empty = enc.createLabel();
        const auto finish = enc.createLabel();
        enc.cbz(X0, empty);
        enc.ldr_imm(X10, X0, 0);
        const auto collection = enc.createLabel();
        enc.mov_ri64(X11, scalarHeader);
        enc.cmp_rr(X10, X11);
        enc.b_cond(NE, collection);
        enc.add_rri(X0, X0, countSize);
        enc.bl(lblBoxedToString);
        enc.b(finish);
        enc.bindLabel(collection);
        enc.str_imm(XZR, FP, 24); // index
        literal("[");
        enc.str_imm(X0, FP, 32); // output
        enc.bindLabel(loop);
        enc.ldr_imm(X10, FP, 16);
        enc.ldr_imm(X11, X10, 0);
        enc.ldr_imm(X0, FP, 24);
        enc.cmp_rr(X0, X11);
        enc.b_cond(GE, close);
        enc.cbz(X0, append);
        enc.ldr_imm(X0, FP, 32);
        enc.adr_data(X1, emitter.addStringLiteralOffset(", "));
        enc.bl(runtime.lblStrConcat);
        enc.str_imm(X0, FP, 32);
        enc.bindLabel(append);
        enc.ldr_imm(X0, FP, 24);
        enc.movz(X11, entrySize, 0);
        enc.mul_rrr(X0, X0, X11);
        enc.ldr_imm(X10, FP, 16);
        enc.add_rrr(X0, X0, X10);
        enc.add_rri(X0, X0, countSize);
        enc.bl(lblBoxedToString);
        enc.mov_rr(X1, X0);
        enc.ldr_imm(X0, FP, 32);
        enc.bl(runtime.lblStrConcat);
        enc.str_imm(X0, FP, 32);
        enc.ldr_imm(X0, FP, 24);
        enc.add_rri(X0, X0, 1);
        enc.str_imm(X0, FP, 24);
        enc.b(loop);
        enc.bindLabel(close);
        enc.ldr_imm(X0, FP, 32);
        enc.adr_data(X1, emitter.addStringLiteralOffset("]"));
        enc.bl(runtime.lblStrConcat);
        enc.b(finish);
        enc.bindLabel(empty);
        literal("[]");
        enc.bindLabel(finish);
        enc.ldp_post(FP, LR, SP, 48);
        enc.ret();
    }

    [[noreturn]] static void rejectMixedOperand() {
        std::cerr << "[ARM64 Codegen Error]: Cast a <multype> element before using it in this operation.\n";
        exit(1);
    }

    ThavaType emitUnbox(ThavaType expected, bool strictString = false) {
        // A pointer tag alone cannot establish a class/struct's nominal type
        // or turn tagged entries into the plain list/tuple representation.
        if (expected == ThavaType::CUSTOM || expected == ThavaType::LIST ||
            expected == ThavaType::TUPLE || expected == ThavaType::FILE ||
            expected == ThavaType::VOID) {
            std::cerr << "Codegen Error: Cannot extract <" << typeToString(expected)
                      << "> directly from a <multype> element.\n";
            exit(1);
        }
        using namespace arm64;
        using namespace native_multype;
        if (expected == ThavaType::STRING && !strictString) {
            emitBoxedString();
            return expected;
        }
        const auto payload = enc.createLabel();
        const auto failure = enc.createLabel();
        const auto done = enc.createLabel();
        enc.ldr_imm(X10, X0, tagOffset);
        enc.cmp_ri(X10, tagFor(expected));
        enc.b_cond(EQ, payload);
        if (expected == ThavaType::FLOAT || expected == ThavaType::DOUBLE || expected == ThavaType::INT) {
            enc.cmp_ri(X10, expected == ThavaType::INT ? Real : Integer);
            enc.b_cond(NE, failure);
            enc.ldr_imm(X0, X0, 0);
            if (expected == ThavaType::INT) doubleBitsToInt();
            else intToDoubleBits();
            enc.b(done);
        } else {
            enc.b(failure);
        }
        enc.bindLabel(failure);
        emitThrowCall(nullptr, nullptr, "Incompatible <multype> element type");
        enc.bindLabel(payload);
        enc.ldr_imm(X0, X0, 0);
        enc.bindLabel(done);
        return expected;
    }

    ThavaType emitMultypeLiteral(CollectionNode& collection) {
        using namespace arm64;
        using namespace native_multype;
        const auto& elements = collection.getElements();
        const bool sequence = collection.scalarPayload == ThavaType::UNKNOWN;
        enc.mov_ri64(X0, (sequence ? handleSize : 0) + countSize + elements.size() * entrySize);
        enc.bl(runtime.lblMalloc);
        enc.sub_rri(SP, SP, 16);
        enc.str_imm(X0, SP, 0);
        enc.mov_ri64(X10, sequence ? int64_t(elements.size()) : scalarHeader);
        if (sequence) {
            initMultypeHandle(X0, X10);
            enc.add_rri(X0, X0, handleSize);
        }
        enc.str_imm(X10, X0, 0);
        for (size_t i = 0; i < elements.size(); ++i) {
            const auto type = compileExpression(elements[i].get(),
                dynamic_cast<CollectionNode*>(elements[i].get()) ? ThavaType::MULTYPE : ThavaType::UNKNOWN);
            const auto offset = static_cast<int32_t>((sequence ? handleSize : 0) + countSize + i * entrySize);
            enc.ldr_imm(X11, SP, 0);
            if (type == ThavaType::BOXED) {
                enc.ldr_imm(X10, X0, 0);
                enc.str_imm(X10, X11, offset);
                enc.ldr_imm(X10, X0, tagOffset);
            } else {
                if (type == ThavaType::UNKNOWN || type == ThavaType::VOID ||
                    type == ThavaType::LIST || type == ThavaType::TUPLE) {
                    std::cerr << "[ARM64 Codegen Error]: Use a <multype> value or literal for a nested collection.\n";
                    exit(1);
                }
                enc.str_imm(X0, X11, offset);
                enc.movz(X10, tagFor(type), 0);
            }
            enc.str_imm(X10, X11, offset + tagOffset);
        }
        enc.ldr_imm(X0, SP, 0);
        enc.add_rri(SP, SP, 16);
        return ThavaType::MULTYPE;
    }

    void boxPlainSequence(ThavaType element) {
        using namespace arm64;
        using namespace native_multype;
        const auto empty = enc.createLabel();
        const auto loop = enc.createLabel();
        const auto done = enc.createLabel();
        enc.cbz(X0, empty);
        enc.sub_rri(SP, SP, 16);
        enc.str_imm(X0, SP, 0);
        enc.ldr_imm(X9, X0, 0);
        enc.movz(X10, entrySize, 0);
        enc.mul_rrr(X0, X9, X10);
        enc.add_rri(X0, X0, handleSize + countSize);
        enc.bl(runtime.lblMalloc);
        enc.ldr_imm(X9, SP, 0);
        enc.add_rri(SP, SP, 16);
        enc.ldr_imm(X10, X9, 0);
        initMultypeHandle(X0, X10);
        enc.str_imm(X10, X0, handleSize);
        enc.add_rri(X9, X9, 8);
        enc.add_rri(X11, X0, handleSize + countSize);
        enc.movz(X12, tagFor(element), 0);
        enc.bindLabel(loop);
        enc.cbz(X10, done);
        enc.ldr_imm(X13, X9, 0);
        enc.str_imm(X13, X11, 0);
        enc.str_imm(X12, X11, tagOffset);
        enc.add_rri(X9, X9, 8);
        enc.add_rri(X11, X11, entrySize);
        enc.sub_rri(X10, X10, 1);
        enc.b(loop);
        enc.bindLabel(empty);
        enc.bindLabel(done);
    }

    ThavaType compileExpression(ASTNode* node, ThavaType expected = ThavaType::UNKNOWN) {
        diag::Scope location(node ? node->span : SourceSpan{});
        const auto actual = compileExpressionRaw(node, expected);
        if (actual == ThavaType::BOXED && expected != ThavaType::UNKNOWN) return emitUnbox(expected);
        if (expected == ThavaType::MULTYPE && (actual == ThavaType::LIST || actual == ThavaType::TUPLE)) {
            const auto element = elemTypeOfIndexedList(node);
            boxPlainSequence(element == ThavaType::UNKNOWN ? ThavaType::INT : element);
            return ThavaType::MULTYPE;
        }
        if ((expected == ThavaType::MULTYPE && actual != ThavaType::MULTYPE) ||
            (actual == ThavaType::MULTYPE && (expected == ThavaType::LIST || expected == ThavaType::TUPLE))) {
            std::cerr << "[ARM64 Codegen Error]: Keep mixed collections declared as <multype>.\n";
            exit(1);
        }
        return actual;
    }

    ThavaType emitDecimalOperation(int op, ASTNode* left, ASTNode* right = nullptr) {
        using namespace arm64;
        if (!decimalEntry) decimalEntry = enc.createLabel();
        const bool comparison = decimalComparison(op);
        enc.sub_rri(SP, SP, 32);
        compileExpression(left);
        enc.str_imm(X0, SP, 0);
        if (right) {
            compileExpression(right);
            enc.str_imm(X0, SP, 8);
        }
        if (!comparison) {
            enc.mov_ri64(X0, THAVA_DECIMAL_BUFFER);
            enc.bl(runtime.lblMalloc);
            enc.str_imm(X0, SP, 16);
        }

        enc.mov_ri64(X0, op);
        enc.ldr_imm(X1, SP, 0);
        if (right) enc.ldr_imm(X2, SP, 8);
        else enc.mov_ri64(X2, 0);
        if (comparison) enc.mov_ri64(X3, 0);
        else enc.ldr_imm(X3, SP, 16);
        enc.bl(*decimalEntry);

        const auto done = enc.createLabel();
        enc.mov_ri64(X9, 0xffffffff);
        enc.and_rrr(X0, X0, X9);
        enc.cbz(X0, done);
        if (comparison) {
            enc.cmp_ri(X0, 1);
            enc.b_cond(EQ, done);
        }
        for (const auto& error : decimalErrors) {
            const auto next = enc.createLabel();
            enc.mov_ri64(X9, static_cast<uint32_t>(error.first));
            enc.cmp_rr(X0, X9);
            enc.b_cond(NE, next);
            enc.add_rri(SP, SP, 32);
            emitThrowCall(nullptr, nullptr, error.second);
            enc.bindLabel(next);
        }
        enc.bindLabel(done);
        if (!comparison) enc.ldr_imm(X0, SP, 16);
        enc.add_rri(SP, SP, 32);
        return comparison ? ThavaType::BOOL : ThavaType::DECIMAL;
    }

    ThavaType compileExpressionRaw(ASTNode* node, ThavaType expected) {
        if (auto literal = dynamic_cast<DecimalParseNode*>(node))
            return emitDecimalOperation(THAVA_DECIMAL_PARSE, literal->text.get());
        if (auto minus = dynamic_cast<UnaryMinusNode*>(node); minus && minus->decimalArithmetic)
            return emitDecimalOperation(THAVA_DECIMAL_NEGATE, minus->expr.get());
        if (auto binary = dynamic_cast<BinaryOpNode*>(node); binary && binary->decimalOp >= 0)
            return emitDecimalOperation(binary->decimalOp, binary->left.get(), binary->right.get());
        if (auto interpolation = dynamic_cast<InterpolatedStringNode*>(node)) {
            emitInterpolationPart(interpolation->parts.front().get());
            if (interpolation->parts.size() == 1) return ThavaType::STRING;
            enc.sub_rri(arm64::SP, arm64::SP, 16);
            for (size_t i = 1; i < interpolation->parts.size(); ++i) {
                enc.str_imm(arm64::X0, arm64::SP, 0);
                emitInterpolationPart(interpolation->parts[i].get());
                enc.mov_rr(arm64::X1, arm64::X0);
                enc.ldr_imm(arm64::X0, arm64::SP, 0);
                enc.bl(runtime.lblStrConcat);
            }
            enc.add_rri(arm64::SP, arm64::SP, 16);
            return ThavaType::STRING;
        }

        if (!node) return ThavaType::UNKNOWN;
        checkUnsupported(node);
        checkRuntimeSupport(node);

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
                if (v->type == ThavaType::INT) narrowIntX0(v->customType);
                return v->type;
            }
            if (isGlobalName(vname)) {
                // Global read (static storage shared with main and other funcs).
                ThavaType gt = globalEnv ? globalEnv->get(vname) : ThavaType::UNKNOWN;
                std::string gct = globalEnv ? globalEnv->getCustomType(vname) : std::string();
                emitGlobalBase(arm64::X9);
                enc.ldr_imm(arm64::X0, arm64::X9, globalVarSlots[vname]);
                if (gt == ThavaType::INT) narrowIntX0(gct);
                return gt;
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
            if (lt == ThavaType::BOXED || lt == ThavaType::MULTYPE) rejectMixedOperand();

            // Save X0 on temporary stack slot (below SP)
            enc.str_imm(arm64::X0, arm64::SP, -16);
            enc.sub_rri(arm64::SP, arm64::SP, 16);

            // Evaluate right -> X0
            ThavaType rt = compileExpression(bin->right.get());
            if (rt == ThavaType::BOXED || rt == ThavaType::MULTYPE) rejectMixedOperand();

            // Restore left -> X1
            enc.ldr_imm(arm64::X1, arm64::SP, 0);
            enc.add_rri(arm64::SP, arm64::SP, 16);

            // X1 is left, X0 is right
            std::string op = bin->op;

            bool cmpOp = (op == "==" || op == "!=" || op == "<" || op == "<=" ||
                          op == ">" || op == ">=");
            bool anyDouble = (lt == ThavaType::FLOAT || lt == ThavaType::DOUBLE ||
                              rt == ThavaType::FLOAT || rt == ThavaType::DOUBLE);

            // Exponent on doubles: constant integer exponent, unrolled squaring.
            if (op == "**" && !anyDouble) {
                emitIntegerPower();
                return ThavaType::INT;
            }
            if (op == "**" && anyDouble) {
                loadOperandsAsDoubles(lt, rt); // D0 = base
                bool haveExp = false;
                long long expv = 0;
                if (auto rlit = dynamic_cast<LiteralNode*>(bin->right.get())) {
                    ThavaType rlt = rlit->getLitType();
                    try {
                        if (rlt == ThavaType::INT) {
                            expv = std::stoll(rlit->getVal());
                            haveExp = true;
                        } else if (rlt == ThavaType::DOUBLE || rlt == ThavaType::FLOAT) {
                            double d = std::stod(rlit->getVal());
                            long long iv = (long long)d;
                            if ((double)iv == d) { expv = iv; haveExp = true; }
                        }
                    } catch (...) {}
                }
                if (!haveExp || expv > 64 || expv < -64) {
                    // General case: x^y = exp2(y * log2(x)) via the runtime helper
                    enc.bl(runtime.lblPow);
                    d0BitsToX0();
                    return ThavaType::DOUBLE;
                }
                bool negExp = expv < 0;
                unsigned long long m = negExp ? (unsigned long long)(-expv)
                                              : (unsigned long long)expv;

                // Save base, start accumulator at 1.0
                enc.sub_rri(arm64::SP, arm64::SP, 16);
                enc.fstr_imm(arm64::D0, arm64::SP, 0);
                enc.movz(arm64::X0, 1, 0);
                intToDoubleBits();
                enc.fldr_imm(arm64::D3, arm64::SP, 0); // D3 = base
                enc.add_rri(arm64::SP, arm64::SP, 16);

                for (int b = 0; b < 64; ++b) {
                    if ((m >> b) & 1ULL) enc.fmul_ddd(arm64::D0, arm64::D0, arm64::D3);
                    if ((m >> (b + 1)) != 0) enc.fmul_ddd(arm64::D3, arm64::D3, arm64::D3);
                }

                if (negExp) {
                    enc.sub_rri(arm64::SP, arm64::SP, 16);
                    enc.fstr_imm(arm64::D0, arm64::SP, 0);   // result
                    enc.movz(arm64::X0, 1, 0);
                    intToDoubleBits();                        // D0 = 1.0
                    enc.fldr_imm(arm64::D1, arm64::SP, 0);
                    enc.add_rri(arm64::SP, arm64::SP, 16);
                    enc.fdiv_ddd(arm64::D0, arm64::D0, arm64::D1);
                }
                d0BitsToX0();
                return ThavaType::DOUBLE;
            }

            if (anyDouble && (cmpOp || op == "+" || op == "-" || op == "*" || op == "/" || op == "//")) {
                loadOperandsAsDoubles(lt, rt);
                if (cmpOp) {
                    enc.fcmp_dd(arm64::D0, arm64::D1);
                    if (op == "==") enc.cset(arm64::X0, arm64::EQ);
                    else if (op == "!=") enc.cset(arm64::X0, arm64::NE);
                    else if (op == "<") enc.cset(arm64::X0, arm64::LT);
                    else if (op == "<=") enc.cset(arm64::X0, arm64::LE);
                    else if (op == ">") enc.cset(arm64::X0, arm64::GT);
                    else enc.cset(arm64::X0, arm64::GE);
                    return ThavaType::BOOL;
                }
                if (op == "+") enc.fadd_ddd(arm64::D0, arm64::D0, arm64::D1);
                else if (op == "-") enc.fsub_ddd(arm64::D0, arm64::D0, arm64::D1);
                else if (op == "*") enc.fmul_ddd(arm64::D0, arm64::D0, arm64::D1);
                else enc.fdiv_ddd(arm64::D0, arm64::D0, arm64::D1);
                d0BitsToX0();
                return ThavaType::DOUBLE;
            }

            if ((op == "==" || op == "!=") && lt == ThavaType::STRING && rt == ThavaType::STRING) {
                enc.bl(runtime.lblStrCmpEq);
                if (op == "!=") {
                    enc.cmp_ri(arm64::X0, 0);
                    enc.cset(arm64::X0, arm64::EQ);
                }
                return ThavaType::BOOL;
            }

            // String concatenation: <str> + <str>
            if (op == "+" && lt == ThavaType::STRING && rt == ThavaType::STRING) {
                enc.mov_rr(arm64::X2, arm64::X0); // s2
                enc.mov_rr(arm64::X0, arm64::X1); // s1
                enc.mov_rr(arm64::X1, arm64::X2); // s2
                enc.bl(runtime.lblStrConcat);
                return ThavaType::STRING;
            }

            // String repetition: <str> * <int> or <int> * <str>
            if (op == "*") {
                if (lt == ThavaType::STRING && rt == ThavaType::INT) {
                    enc.mov_rr(arm64::X2, arm64::X1); // s
                    enc.mov_rr(arm64::X1, arm64::X0); // count
                    enc.mov_rr(arm64::X0, arm64::X2); // s
                    enc.bl(runtime.lblStrMul);
                    return ThavaType::STRING;
                }
                if (lt == ThavaType::INT && rt == ThavaType::STRING) {
                    enc.bl(runtime.lblStrMul); // X0 = s, X1 = count
                    return ThavaType::STRING;
                }
            }

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
            if (t == ThavaType::DOUBLE || t == ThavaType::FLOAT) {
                // Doubles live as raw bit patterns in X0: flip the sign bit
                enc.movz(arm64::X1, 0, 0);
                enc.movk(arm64::X1, 0x8000, 48);
                enc.eor_rrr(arm64::X0, arm64::X0, arm64::X1);
            } else {
                enc.sub_rrr(arm64::X0, arm64::XZR, arm64::X0);
            }
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

            ThavaType tType = staticExprType(cond->trueExpr.get());
            ThavaType fType = staticExprType(cond->falseExpr.get());
            bool anyDouble = (tType == ThavaType::DOUBLE || tType == ThavaType::FLOAT ||
                              fType == ThavaType::DOUBLE || fType == ThavaType::FLOAT);

            compileExpression(cond->condition.get(), ThavaType::BOOL);
            enc.cbz(arm64::X0, elseLbl);

            ThavaType t = compileExpression(cond->trueExpr.get(), expected);
            if (anyDouble && t == ThavaType::INT) {
                intToDoubleBits();
                t = ThavaType::DOUBLE;
            }
            enc.b(endLbl);

            enc.bindLabel(elseLbl);
            ThavaType f = compileExpression(cond->falseExpr.get(), expected);
            if (anyDouble && f == ThavaType::INT) {
                intToDoubleBits();
                f = ThavaType::DOUBLE;
            }

            enc.bindLabel(endLbl);
            return anyDouble ? ThavaType::DOUBLE : t;
        }

        // 7. CallNode (Function invocation)
        if constexpr (std::is_same_v<TRuntime, Arm64WinRuntime>) {
            if (auto call = dynamic_cast<CCallNode*>(node))
                return emitWindowsCFunction(call->funcName, call->args);
            if (auto clib = dynamic_cast<CLibMethodNode*>(node))
                return emitWindowsCLib(clib);
            if (dynamic_cast<CVarNode*>(node)) {
                std::cerr << "[ARM64 Codegen Error]: C globals require the C++ backend.\n";
                exit(1);
            }
        }

        if (auto call = dynamic_cast<CallNode*>(node)) {
            if constexpr (std::is_same_v<TRuntime, Arm64WinRuntime>) {
                if (g_isCFunc.count(call->name))
                    return emitWindowsCFunction(call->name, call->args);
            }
            // Built-in min()/max() — unless the program defines its own function with that name
            if ((call->name == "min" || call->name == "max") && functions.find(call->name) == functions.end()) {
                if (call->args.size() != 2) {
                    std::cerr << "[ARM64 Codegen Error]: " << call->name
                              << "() expects exactly 2 arguments\n";
                    exit(1);
                }
                bool wantMax = (call->name == "max");

                ThavaType at = compileExpression(call->args[0].get());
                enc.str_imm(arm64::X0, arm64::SP, -16);
                enc.sub_rri(arm64::SP, arm64::SP, 16);
                ThavaType bt = compileExpression(call->args[1].get());
                enc.ldr_imm(arm64::X1, arm64::SP, 0); // X1 = left
                enc.add_rri(arm64::SP, arm64::SP, 16);

                bool anyD = (at == ThavaType::DOUBLE || at == ThavaType::FLOAT ||
                             bt == ThavaType::DOUBLE || bt == ThavaType::FLOAT);
                if (anyD) {
                    loadOperandsAsDoubles(at, bt); // D0 = left, D1 = right
                    enc.fcmp_dd(arm64::D0, arm64::D1);
                    size_t keepLeft = enc.createLabel();
                    size_t doneLbl = enc.createLabel();
                    if (wantMax) enc.b_cond(arm64::GE, keepLeft);
                    else enc.b_cond(arm64::LE, keepLeft);
                    // take right operand
                    enc.sub_rri(arm64::SP, arm64::SP, 16);
                    enc.fstr_imm(arm64::D1, arm64::SP, 0);
                    enc.fldr_imm(arm64::D0, arm64::SP, 0);
                    enc.add_rri(arm64::SP, arm64::SP, 16);
                    enc.b(doneLbl);
                    enc.bindLabel(keepLeft);
                    enc.bindLabel(doneLbl);
                    d0BitsToX0();
                    return ThavaType::DOUBLE;
                }

                enc.cmp_rr(arm64::X1, arm64::X0);
                size_t keepLeftI = enc.createLabel();
                size_t doneI = enc.createLabel();
                if (wantMax) enc.b_cond(arm64::GE, keepLeftI);
                else enc.b_cond(arm64::LE, keepLeftI);
                enc.b(doneI);
                enc.bindLabel(keepLeftI);
                enc.mov_rr(arm64::X0, arm64::X1);
                enc.bindLabel(doneI);
                return ThavaType::INT;
            }

            auto fit = functions.find(call->name);
            if (fit != functions.end()) {
                const auto& params = fit->second.params;
                const auto frame = emitThavaCallArguments(call->args, [&](size_t index) {
                    return index < params.size() ? params[index].first : ThavaType::UNKNOWN;
                });
                enc.bl(fit->second.labelId);
                if (frame) enc.add_rri(arm64::SP, arm64::SP, frame);
                return fit->second.returnType;
            }

            // Imported from a @dlattach library: call its slot in the mapped module.
            auto dlit = g_dlFunctions.find(call->name);
            if (dlit != g_dlFunctions.end()) {
                const auto& dlinfo = dlit->second;
                if (!dlinfo.hasOffset) {
                    std::cerr << "[Thava Compiler] Error: dynamic library '" << dlinfo.libPath
                              << "' exports no function offsets; rebuild it with --lib using this compiler.\n";
                    exit(1);
                }
                const auto frame = emitThavaCallArguments(call->args, [&](size_t index) {
                    return index < dlinfo.paramTypes.size() ? dlinfo.paramTypes[index] : ThavaType::UNKNOWN;
                });
                if constexpr (std::is_same_v<TEmitter, PeEmitter>) {
                    enc.bl(peLibraryThunks.at({dlinfo.libBasename, call->name}));
                } else {
                    const auto libBase = dlLoadBaseForPath(dlinfo.libPath);
                    enc.mov_ri64(arm64::X16, libBase + dlinfo.libTextRva + dlinfo.funcOffset);
                    enc.blr(arm64::X16);
                }
                if (frame) enc.add_rri(arm64::SP, arm64::SP, frame);
                return dlinfo.retType;
            }

            // Check indirect function pointer call
            VarInfo* v = lookupVar(call->name);
            if (v && v->type == ThavaType::FUNC) {
                const auto frame = emitThavaCallArguments(call->args, [](size_t) { return ThavaType::UNKNOWN; });
                enc.ldr_imm(arm64::X16, arm64::FP, v->offset);
                enc.blr(arm64::X16);
                if (frame) enc.add_rri(arm64::SP, arm64::SP, frame);
                return ThavaType::INT;
            }
        }

        // 8. CastNode (<int>(x), <str>(x), etc.)
        if (auto c = dynamic_cast<CastNode*>(node)) {
            if (c->targetType == ThavaType::MULTYPE)
                return compileExpression(c->expr.get(), ThavaType::MULTYPE);
            ThavaType srcT = compileExpression(c->expr.get());
            if (c->targetType == ThavaType::STRING && srcT == ThavaType::MULTYPE) {
                emitMultypeString();
                return ThavaType::STRING;
            }
            if (srcT == ThavaType::BOXED) {
                auto type = emitUnbox(c->targetType);
                if (type == ThavaType::INT) narrowIntX0(c->customTarget);
                return type;
            }
            ThavaType dstT = c->targetType;

            if ((dstT == ThavaType::DOUBLE || dstT == ThavaType::FLOAT) &&
                srcT != ThavaType::DOUBLE && srcT != ThavaType::FLOAT) {
                // int/ptr -> double (doubles are stored as raw 64-bit patterns in X0)
                enc.scvtf_dr(arm64::D0, arm64::X0);
                enc.sub_rri(arm64::SP, arm64::SP, 16);
                enc.fstr_imm(arm64::D0, arm64::SP, 0);
                enc.ldr_imm(arm64::X0, arm64::SP, 0);
                enc.add_rri(arm64::SP, arm64::SP, 16);
                return dstT;
            }
            if (dstT == ThavaType::INT &&
                (srcT == ThavaType::DOUBLE || srcT == ThavaType::FLOAT)) {
                // double -> int (truncate toward zero)
                enc.sub_rri(arm64::SP, arm64::SP, 16);
                enc.str_imm(arm64::X0, arm64::SP, 0);
                enc.fldr_imm(arm64::D0, arm64::SP, 0);
                enc.add_rri(arm64::SP, arm64::SP, 16);
                enc.fcvtzs_rd(arm64::X0, arm64::D0);
                return ThavaType::INT;
            }
            if (dstT == ThavaType::STRING && srcT != ThavaType::STRING) {
                emitStringCast(srcT);
                return ThavaType::STRING;
            }
            // A cast to a collection type is only a type annotation (e.g. <lst>[...]);
            // anything else would silently produce a bogus pointer.
            if ((dstT == ThavaType::LIST || dstT == ThavaType::TUPLE || dstT == ThavaType::DICT) &&
                srcT != dstT) {
                std::cerr << "[ARM64 Codegen Error]: cannot cast a non-collection value"
                          << " to a collection type (<lst>/<tple>/<dict>)\n";
                exit(1);
            }
            return dstT;
        }

        // 9. CollectionNode (list/tuple literal: [a, b, c])
        if (auto coll = dynamic_cast<CollectionNode*>(node)) {
            if (expected == ThavaType::MULTYPE) return emitMultypeLiteral(*coll);
            size_t n = coll->getElements().size();

            // malloc((n + 1) * 8) -> X0
            enc.mov_ri64(arm64::X0, (int64_t)((n + 1) * 8));
            enc.bl(runtime.lblMalloc);

            // [SP+0] = list pointer
            enc.sub_rri(arm64::SP, arm64::SP, 16);
            enc.str_imm(arm64::X0, arm64::SP, 0);

            // list[0] = element count
            enc.mov_ri64(arm64::X1, (int64_t)n);
            enc.ldr_imm(arm64::X0, arm64::SP, 0);
            enc.str_imm(arm64::X1, arm64::X0, 0);

            ThavaType detectedElemType = (n > 0) ? ThavaType::INT : ThavaType::UNKNOWN;
            for (size_t i = 0; i < n; ++i) {
                ThavaType elemT = compileExpression(coll->getElements()[i].get());
                if (elemT == ThavaType::FLOAT || elemT == ThavaType::DOUBLE) {
                    detectedElemType = ThavaType::DOUBLE;
                } else if (elemT == ThavaType::STRING && detectedElemType != ThavaType::DOUBLE) {
                    detectedElemType = ThavaType::STRING;
                }
                // Raw 8-byte slot: int, pointer, or double bit pattern (same layout as x86 backend)
                enc.ldr_imm(arm64::X1, arm64::SP, 0);
                enc.str_imm(arm64::X0, arm64::X1, (int32_t)(8 + i * 8));
            }
            lastCollectionElemType = detectedElemType;

            enc.ldr_imm(arm64::X0, arm64::SP, 0);
            enc.add_rri(arm64::SP, arm64::SP, 16);
            return coll->getIsTuple() ? ThavaType::TUPLE : ThavaType::LIST;
        }

        // StrSubstringNode (s.getsubstring(start, end))
        // Same semantics as x86: 1-based, inclusive, clamped.
        if (auto ss = dynamic_cast<StrSubstringNode*>(node)) {
            compileExpression(ss->base.get()); // X0 = s
            enc.sub_rri(arm64::SP, arm64::SP, 16);
            enc.str_imm(arm64::X0, arm64::SP, 0);

            compileExpression(ss->startExpr.get()); // X0 = start
            enc.sub_rri(arm64::SP, arm64::SP, 16);
            enc.str_imm(arm64::X0, arm64::SP, 0);

            compileExpression(ss->endExpr.get()); // X0 = end
            enc.mov_rr(arm64::X2, arm64::X0);      // b = end
            enc.ldr_imm(arm64::X1, arm64::SP, 0);  // a = start
            enc.add_rri(arm64::SP, arm64::SP, 16);
            enc.ldr_imm(arm64::X0, arm64::SP, 0);  // s
            enc.add_rri(arm64::SP, arm64::SP, 16);
            enc.bl(runtime.lblSubstrRange);
            return ThavaType::STRING;
        }

        // 10. IndexNode (list[i])
        if (auto idxn = dynamic_cast<IndexNode*>(node)) {
            using namespace arm64;
            using namespace native_multype;
            ThavaType baseT = compileExpression(idxn->base.get());
            if (baseT == ThavaType::BOXED) baseT = emitUnbox(ThavaType::MULTYPE);
            if (baseT == ThavaType::MULTYPE) {
                loadMultypeBlock(X0);
                if (idxn->singletonType != ThavaType::UNKNOWN) {
                    const auto invalid = enc.createLabel();
                    const auto single = enc.createLabel();
                    enc.cbz(X0, invalid);
                    enc.ldr_imm(X10, X0, 0);
                    enc.cmp_ri(X10, 1);
                    enc.b_cond(EQ, single);
                    enc.mov_ri64(X11, scalarHeader);
                    enc.cmp_rr(X10, X11);
                    enc.b_cond(EQ, single);
                    enc.bindLabel(invalid);
                    emitThrowCall(nullptr, nullptr, "Expected exactly one <multype> element");
                    enc.bindLabel(single);
                    enc.add_rri(X0, X0, countSize);
                    return emitUnbox(idxn->singletonType, true);
                }
                enc.sub_rri(SP, SP, 16);
                enc.str_imm(X0, SP, 0);
                if (compileExpression(idxn->indexExpr.get(), ThavaType::INT) != ThavaType::INT) {
                    std::cerr << "[ARM64 Codegen Error]: A <multype> index must be an integer.\n";
                    exit(1);
                }
                enc.ldr_imm(X10, SP, 0);
                enc.add_rri(SP, SP, 16);
                const auto valid = enc.createLabel();
                const auto nonnegative = enc.createLabel();
                const auto outOfRange = enc.createLabel();
                enc.cbz(X10, outOfRange);
                enc.ldr_imm(X11, X10, 0);
                enc.cmp_ri(X11, 0);
                enc.b_cond(LT, outOfRange);
                enc.cmp_ri(X0, 0);
                enc.b_cond(GE, nonnegative);
                enc.add_rrr(X0, X0, X11);
                enc.bindLabel(nonnegative);
                enc.cmp_rr(X0, X11);
                enc.b_cond(CC, valid);
                enc.bindLabel(outOfRange);
                emitThrowCall(nullptr, nullptr, "<multype> index out of range");
                enc.bindLabel(valid);
                enc.movz(X11, entrySize, 0);
                enc.mul_rrr(X0, X0, X11);
                enc.add_rrr(X0, X0, X10);
                enc.add_rri(X0, X0, countSize);
                return ThavaType::BOXED;
            }
            if (baseT == ThavaType::STRING) {
                // s[i] -> 1-character string (x86 passes 1-based i+1 .. i+1)
                enc.sub_rri(arm64::SP, arm64::SP, 16);
                enc.str_imm(arm64::X0, arm64::SP, 0);
                compileExpression(idxn->indexExpr.get()); // X0 = index
                enc.add_rri(arm64::X1, arm64::X0, 1);     // a = index + 1
                enc.mov_rr(arm64::X2, arm64::X1);         // b = index + 1
                enc.ldr_imm(arm64::X0, arm64::SP, 0);     // s
                enc.add_rri(arm64::SP, arm64::SP, 16);
                enc.bl(runtime.lblSubstrRange);
                return ThavaType::STRING;
            }
            if (baseT == ThavaType::DICT) {
                enc.sub_rri(arm64::SP, arm64::SP, 32);
                enc.str_imm(arm64::X0, arm64::SP, 0); // dict
                ThavaType kt = compileExpression(idxn->indexExpr.get());
                enc.str_imm(arm64::X0, arm64::SP, 8); // key
                enc.ldr_imm(arm64::X0, arm64::SP, 0);
                enc.ldr_imm(arm64::X1, arm64::SP, 8);
                enc.movz(arm64::X2, kt == ThavaType::STRING ? 1 : 0, 0);
                enc.bl(runtime.lblDictFind);
                size_t foundLbl = enc.createLabel();
                size_t endLbl = enc.createLabel();
                enc.cmp_ri(arm64::X0, 0);
                enc.b_cond(arm64::GE, foundLbl);
                enc.movz(arm64::X0, 0, 0);
                enc.b(endLbl);
                enc.bindLabel(foundLbl);
                enc.movz(arm64::X2, 32, 0);
                enc.mul_rrr(arm64::X3, arm64::X0, arm64::X2);
                enc.ldr_imm(arm64::X4, arm64::SP, 0);
                enc.add_rrr(arm64::X4, arm64::X4, arm64::X3);
                enc.ldr_imm(arm64::X0, arm64::X4, 24);
                enc.bindLabel(endLbl);
                enc.add_rri(arm64::SP, arm64::SP, 32);
                return ThavaType::INT;
            }
            if (baseT != ThavaType::LIST && baseT != ThavaType::TUPLE) {
                return ThavaType::UNKNOWN;
            }
            // [SP+0] = base pointer
            enc.sub_rri(arm64::SP, arm64::SP, 16);
            enc.str_imm(arm64::X0, arm64::SP, 0);

            compileExpression(idxn->indexExpr.get()); // X0 = index
            enc.ldr_imm(arm64::X1, arm64::SP, 0);    // X1 = base
            enc.add_rri(arm64::SP, arm64::SP, 16);

            enc.movz(arm64::X3, 8, 0);
            enc.mul_rrr(arm64::X0, arm64::X0, arm64::X3);
            enc.add_rrr(arm64::X1, arm64::X1, arm64::X0);
            enc.ldr_imm(arm64::X0, arm64::X1, 8);        // element (bits/ptr/int)

            ThavaType elemT = elemTypeOfIndexedList(idxn->base.get());
            if (elemT == ThavaType::UNKNOWN) elemT = ThavaType::INT;
            return elemT;
        }

        // 11. LenNode (len(list) / len(str) / s.len())
        if (auto lenN = dynamic_cast<LenNode*>(node)) {
            ThavaType at = compileExpression(lenN->arg.get());
            if (at == ThavaType::BOXED) at = emitUnbox(ThavaType::MULTYPE);
            if (at == ThavaType::STRING) {
                enc.bl(runtime.lblStrLen);
                return ThavaType::INT;
            }
            if (at == ThavaType::MULTYPE) {
                loadMultypeBlock(arm64::X0);
                const auto zero = enc.createLabel();
                enc.cbz(arm64::X0, zero);
                enc.ldr_imm(arm64::X0, arm64::X0, 0);
                enc.cmp_ri(arm64::X0, 0);
                enc.b_cond(arm64::GE, zero);
                emitThrowCall(nullptr, nullptr, "Cannot take the length of a scalar <multype>");
                enc.bindLabel(zero);
                return ThavaType::INT;
            }
            if (isCollectionType(at)) {
                enc.ldr_imm(arm64::X0, arm64::X0, 0); // count at offset 0
                return ThavaType::INT;
            }
            return ThavaType::UNKNOWN;
        }

        // 12. ListMethodNode (list.len(), list.addval(x))
        if (auto lm = dynamic_cast<ListMethodNode*>(node)) {
            const auto* local = lookupVar(lm->varName);
            const auto type = local ? local->type : globalEnv->get(lm->varName);
            if (type == ThavaType::MULTYPE) {
                mutateMultype(lm->varName, listMutation(*lm));
                return ThavaType::VOID;
            }
            if (!lookupVar(lm->varName) && isGlobalName(lm->varName) &&
                (lm->method != "addval" || lm->args.size() != 1)) {
                std::cerr << "[Codegen Error]: Unsupported native global list operation: " << lm->method << "\n";
                exit(1);
            }
            if (lm->method == "len" || lm->method == "size") {
                VarInfo* vi = lookupVar(lm->varName);
                if (!vi && !isGlobalName(lm->varName)) return ThavaType::UNKNOWN;
                loadCollectionPtr(lm->varName, vi, arm64::X0);
                enc.ldr_imm(arm64::X0, arm64::X0, 0);
                return ThavaType::INT;
            }
            if (lm->method == "addval") {
                VarInfo* vi = lookupVar(lm->varName);
                if ((!vi && !isGlobalName(lm->varName)) || lm->args.empty()) return ThavaType::UNKNOWN;
                ASTNode* valExpr = lm->args.size() == 1 ? lm->args[0].get() : lm->args[1].get();

                // Scratch slots: [SP+0]=value [SP+8]=old [SP+16]=n [SP+24]=new [SP+32]=i
                enc.sub_rri(arm64::SP, arm64::SP, 48);
                ThavaType vt = compileExpression(valExpr);
                enc.str_imm(arm64::X0, arm64::SP, 0);
                if (vt == ThavaType::FLOAT || vt == ThavaType::DOUBLE) {
                    listElemTypes[lm->varName] = ThavaType::DOUBLE;
                } else if (vt == ThavaType::STRING) {
                    listElemTypes[lm->varName] = ThavaType::STRING;
                }

                loadCollectionPtr(lm->varName, vi, arm64::X1);
                enc.str_imm(arm64::X1, arm64::SP, 8);
                enc.ldr_imm(arm64::X2, arm64::X1, 0);   // n
                enc.str_imm(arm64::X2, arm64::SP, 16);

                // newBytes = (n + 2) * 8
                enc.add_rri(arm64::X0, arm64::X2, 2);
                enc.movz(arm64::X3, 8, 0);
                enc.mul_rrr(arm64::X0, arm64::X0, arm64::X3);
                enc.bl(runtime.lblMalloc);
                enc.str_imm(arm64::X0, arm64::SP, 24);  // new ptr

                // new[0] = n + 1
                enc.ldr_imm(arm64::X2, arm64::SP, 16);
                enc.add_rri(arm64::X3, arm64::X2, 1);
                enc.str_imm(arm64::X3, arm64::X0, 0);

                // Copy old elements
                enc.movz(arm64::X4, 0, 0);
                enc.str_imm(arm64::X4, arm64::SP, 32);  // i = 0
                size_t copyLoop = enc.createLabel();
                size_t copyDone = enc.createLabel();
                enc.bindLabel(copyLoop);
                enc.ldr_imm(arm64::X4, arm64::SP, 32);
                enc.ldr_imm(arm64::X2, arm64::SP, 16);
                enc.cmp_rr(arm64::X4, arm64::X2);
                enc.b_cond(arm64::GE, copyDone);
                enc.movz(arm64::X5, 8, 0);
                enc.mul_rrr(arm64::X6, arm64::X4, arm64::X5);
                enc.ldr_imm(arm64::X0, arm64::SP, 8);
                enc.add_rrr(arm64::X0, arm64::X0, arm64::X6);
                enc.ldr_imm(arm64::X7, arm64::X0, 8);
                enc.ldr_imm(arm64::X0, arm64::SP, 24);
                enc.add_rrr(arm64::X0, arm64::X0, arm64::X6);
                enc.str_imm(arm64::X7, arm64::X0, 8);
                enc.add_rri(arm64::X4, arm64::X4, 1);
                enc.str_imm(arm64::X4, arm64::SP, 32);
                enc.b(copyLoop);
                enc.bindLabel(copyDone);

                // new[8 + n*8] = value
                enc.ldr_imm(arm64::X2, arm64::SP, 16);
                enc.movz(arm64::X5, 8, 0);
                enc.mul_rrr(arm64::X6, arm64::X2, arm64::X5);
                enc.ldr_imm(arm64::X0, arm64::SP, 24);
                enc.add_rrr(arm64::X0, arm64::X0, arm64::X6);
                enc.ldr_imm(arm64::X7, arm64::SP, 0);
                enc.str_imm(arm64::X7, arm64::X0, 8);

                // Store new pointer back into the list variable
                enc.ldr_imm(arm64::X0, arm64::SP, 24);
                if (vi) enc.str_imm(arm64::X0, arm64::FP, vi->offset);
                else {
                    emitGlobalBase(arm64::X9);
                    enc.str_imm(arm64::X0, arm64::X9, globalVarSlots.at(lm->varName));
                }
                enc.add_rri(arm64::SP, arm64::SP, 48);
                return ThavaType::VOID;
            }
            return ThavaType::UNKNOWN;
        }

        // 12e. DictNode literal: [count][cap] + 32-byte entries
        if (auto dn = dynamic_cast<DictNode*>(node)) {
            size_t n = dn->entries.size();
            size_t cap = n > 4 ? n : 4;
            enc.mov_ri64(arm64::X0, (int64_t)(16 + cap * 32));
            enc.bl(runtime.lblMalloc);
            enc.sub_rri(arm64::SP, arm64::SP, 32);
            enc.str_imm(arm64::X0, arm64::SP, 0);
            enc.mov_ri64(arm64::X1, (int64_t)n);
            enc.str_imm(arm64::X1, arm64::X0, 0);
            enc.mov_ri64(arm64::X1, (int64_t)cap);
            enc.str_imm(arm64::X1, arm64::X0, 8);

            for (size_t i = 0; i < n; ++i) {
                ThavaType kt = compileExpression(dn->entries[i].first.get());
                enc.str_imm(arm64::X0, arm64::SP, 8);
                ThavaType vt = compileExpression(dn->entries[i].second.get());
                enc.str_imm(arm64::X0, arm64::SP, 16);
                enc.ldr_imm(arm64::X0, arm64::SP, 0);
                enc.mov_ri64(arm64::X1, (int64_t)(16 + i * 32));
                enc.add_rrr(arm64::X0, arm64::X0, arm64::X1);
                enc.ldr_imm(arm64::X1, arm64::SP, 8);
                enc.str_imm(arm64::X1, arm64::X0, 0);
                enc.ldr_imm(arm64::X2, arm64::SP, 16);
                enc.str_imm(arm64::X2, arm64::X0, 8);
                enc.mov_ri64(arm64::X1, (int64_t)kt);
                enc.str_imm(arm64::X1, arm64::X0, 16);
                enc.mov_ri64(arm64::X1, (int64_t)vt);
                enc.str_imm(arm64::X1, arm64::X0, 24);
            }
            enc.ldr_imm(arm64::X0, arm64::SP, 0);
            enc.add_rri(arm64::SP, arm64::SP, 32);
            return ThavaType::DICT;
        }

        // 12f. Dict methods
        if (auto dm = dynamic_cast<DictMethodNode*>(node)) {
            compileExpression(dm->base.get());
            enc.sub_rri(arm64::SP, arm64::SP, 64);
            enc.str_imm(arm64::X0, arm64::SP, 0); // dict

            if (dm->method == "len" || dm->method == "size") {
                enc.ldr_imm(arm64::X0, arm64::X0, 0);
                enc.add_rri(arm64::SP, arm64::SP, 64);
                return ThavaType::INT;
            }
            if (dm->method == "clear") {
                enc.movz(arm64::X1, 0, 0);
                enc.str_imm(arm64::X1, arm64::X0, 0);
                enc.add_rri(arm64::SP, arm64::SP, 64);
                return ThavaType::VOID;
            }
            if (dm->method == "keys" || dm->method == "values") {
                bool takeKeys = (dm->method == "keys");
                enc.ldr_imm(arm64::X1, arm64::X0, 0);
                enc.str_imm(arm64::X1, arm64::SP, 8); // count
                enc.add_rri(arm64::X0, arm64::X1, 1);
                enc.movz(arm64::X2, 8, 0);
                enc.mul_rrr(arm64::X0, arm64::X0, arm64::X2);
                enc.bl(runtime.lblMalloc);
                enc.str_imm(arm64::X0, arm64::SP, 16); // list
                enc.ldr_imm(arm64::X1, arm64::SP, 8);
                enc.str_imm(arm64::X1, arm64::X0, 0);
                enc.movz(arm64::X0, 0, 0);
                enc.str_imm(arm64::X0, arm64::SP, 24); // i
                size_t kLoop = enc.createLabel();
                size_t kDone = enc.createLabel();
                enc.bindLabel(kLoop);
                enc.ldr_imm(arm64::X0, arm64::SP, 24);
                enc.ldr_imm(arm64::X1, arm64::SP, 8);
                enc.cmp_rr(arm64::X0, arm64::X1);
                enc.b_cond(arm64::GE, kDone);
                enc.movz(arm64::X2, 32, 0);
                enc.mul_rrr(arm64::X3, arm64::X0, arm64::X2);
                enc.ldr_imm(arm64::X4, arm64::SP, 0);
                enc.add_rrr(arm64::X4, arm64::X4, arm64::X3);
                enc.ldr_imm(arm64::X5, arm64::X4, takeKeys ? 16 : 24);
                enc.movz(arm64::X2, 8, 0);
                enc.mul_rrr(arm64::X3, arm64::X0, arm64::X2);
                enc.ldr_imm(arm64::X4, arm64::SP, 16);
                enc.add_rrr(arm64::X4, arm64::X4, arm64::X3);
                enc.str_imm(arm64::X5, arm64::X4, 8);
                enc.add_rri(arm64::X0, arm64::X0, 1);
                enc.str_imm(arm64::X0, arm64::SP, 24);
                enc.b(kLoop);
                enc.bindLabel(kDone);
                enc.ldr_imm(arm64::X0, arm64::SP, 16);
                enc.add_rri(arm64::SP, arm64::SP, 64);
                return ThavaType::LIST;
            }
            if (dm->method == "has" || dm->method == "contains") {
                if (dm->args.empty()) {
                    enc.movz(arm64::X0, 0, 0);
                    enc.add_rri(arm64::SP, arm64::SP, 64);
                    return ThavaType::BOOL;
                }
                ThavaType kt = compileExpression(dm->args[0].get());
                enc.str_imm(arm64::X0, arm64::SP, 8);
                enc.ldr_imm(arm64::X0, arm64::SP, 0);
                enc.ldr_imm(arm64::X1, arm64::SP, 8);
                enc.movz(arm64::X2, kt == ThavaType::STRING ? 1 : 0, 0);
                enc.bl(runtime.lblDictFind);
                enc.cmp_ri(arm64::X0, 0);
                enc.cset(arm64::X0, arm64::GE);
                enc.add_rri(arm64::SP, arm64::SP, 64);
                return ThavaType::BOOL;
            }
            if (dm->method == "get") {
                if (dm->args.empty()) {
                    enc.movz(arm64::X0, 0, 0);
                    enc.add_rri(arm64::SP, arm64::SP, 64);
                    return ThavaType::INT;
                }
                ThavaType kt = compileExpression(dm->args[0].get());
                enc.str_imm(arm64::X0, arm64::SP, 8); // key
                bool hasDefault = dm->args.size() > 1;
                if (hasDefault) {
                    compileExpression(dm->args[1].get());
                    enc.str_imm(arm64::X0, arm64::SP, 16); // default
                }
                enc.ldr_imm(arm64::X0, arm64::SP, 0);
                enc.ldr_imm(arm64::X1, arm64::SP, 8);
                enc.movz(arm64::X2, kt == ThavaType::STRING ? 1 : 0, 0);
                enc.bl(runtime.lblDictFind);
                size_t foundLbl = enc.createLabel();
                size_t endLbl = enc.createLabel();
                enc.cmp_ri(arm64::X0, 0);
                enc.b_cond(arm64::GE, foundLbl);
                if (hasDefault) enc.ldr_imm(arm64::X0, arm64::SP, 16);
                else enc.movz(arm64::X0, 0, 0);
                enc.b(endLbl);
                enc.bindLabel(foundLbl);
                enc.movz(arm64::X2, 32, 0);
                enc.mul_rrr(arm64::X3, arm64::X0, arm64::X2);
                enc.ldr_imm(arm64::X4, arm64::SP, 0);
                enc.add_rrr(arm64::X4, arm64::X4, arm64::X3);
                enc.ldr_imm(arm64::X0, arm64::X4, 24);
                enc.bindLabel(endLbl);
                enc.add_rri(arm64::SP, arm64::SP, 64);
                return ThavaType::INT;
            }
            if (dm->method == "remove" || dm->method == "del") {
                if (dm->args.empty()) {
                    enc.add_rri(arm64::SP, arm64::SP, 64);
                    return ThavaType::VOID;
                }
                ThavaType kt = compileExpression(dm->args[0].get());
                enc.str_imm(arm64::X0, arm64::SP, 8);
                enc.ldr_imm(arm64::X0, arm64::SP, 0);
                enc.ldr_imm(arm64::X1, arm64::SP, 8);
                enc.movz(arm64::X2, kt == ThavaType::STRING ? 1 : 0, 0);
                enc.bl(runtime.lblDictFind);
                size_t doneLbl = enc.createLabel();
                size_t shiftLoop = enc.createLabel();
                size_t shiftDone = enc.createLabel();
                enc.cmp_ri(arm64::X0, 0);
                enc.b_cond(arm64::LT, doneLbl);
                enc.str_imm(arm64::X0, arm64::SP, 24); // idx
                enc.bindLabel(shiftLoop);
                enc.ldr_imm(arm64::X0, arm64::SP, 24);
                enc.add_rri(arm64::X0, arm64::X0, 1);
                enc.ldr_imm(arm64::X1, arm64::SP, 0);
                enc.ldr_imm(arm64::X2, arm64::X1, 0);
                enc.cmp_rr(arm64::X0, arm64::X2);
                enc.b_cond(arm64::GE, shiftDone);
                enc.movz(arm64::X3, 32, 0);
                enc.mul_rrr(arm64::X4, arm64::X0, arm64::X3);
                enc.add_rrr(arm64::X4, arm64::X1, arm64::X4);
                enc.sub_rri(arm64::X5, arm64::X4, 32); // dst
                enc.ldr_imm(arm64::X6, arm64::X4, 16);
                enc.str_imm(arm64::X6, arm64::X5, 16);
                enc.ldr_imm(arm64::X6, arm64::X4, 24);
                enc.str_imm(arm64::X6, arm64::X5, 24);
                enc.ldr_imm(arm64::X6, arm64::X4, 32);
                enc.str_imm(arm64::X6, arm64::X5, 32);
                enc.ldr_imm(arm64::X6, arm64::X4, 40);
                enc.str_imm(arm64::X6, arm64::X5, 40);
                enc.ldr_imm(arm64::X6, arm64::SP, 24);
                enc.add_rri(arm64::X6, arm64::X6, 1);
                enc.str_imm(arm64::X6, arm64::SP, 24);
                enc.b(shiftLoop);
                enc.bindLabel(shiftDone);
                enc.ldr_imm(arm64::X1, arm64::SP, 0);
                enc.ldr_imm(arm64::X2, arm64::X1, 0);
                enc.sub_rri(arm64::X2, arm64::X2, 1);
                enc.str_imm(arm64::X2, arm64::X1, 0);
                enc.bindLabel(doneLbl);
                enc.add_rri(arm64::SP, arm64::SP, 64);
                return ThavaType::VOID;
            }
            enc.add_rri(arm64::SP, arm64::SP, 64);
            return ThavaType::UNKNOWN;
        }

        // 13. UnaryBNotNode (~x)
        if (auto bn = dynamic_cast<UnaryBNotNode*>(node)) {
            compileExpression(bn->expr.get());
            enc.movn(arm64::X1, 0, 0);
            enc.eor_rrr(arm64::X0, arm64::X0, arm64::X1);
            return ThavaType::INT;
        }

        // 14. Console input
        if (auto inp = dynamic_cast<InputNode*>(node)) {
            compileExpression(inp->prompt.get());
            // mode 1 lowercases the line, which lets the bool parser below
            // match "true"/"yes" regardless of the input casing.
            enc.movz(arm64::X1, inp->inputType == ThavaType::BOOL ? 1 : 0, 0);
            enc.bl(runtime.lblInputLine);
            if (inp->inputType == ThavaType::STRING) return ThavaType::STRING;
            if (inp->inputType == ThavaType::BOOL) {
                // "true"/"t"/"yes"/"y" -> true, otherwise fall back to atoi
                // so "1" is true and "0"/"no" are false.
                size_t boolTrue = enc.createLabel();
                size_t boolEnd = enc.createLabel();
                enc.mov_rr(arm64::X2, arm64::X0);
                enc.ldrb_imm(arm64::X1, arm64::X2, 0);
                enc.mov_ri64(arm64::X3, 't');
                enc.cmp_rr(arm64::X1, arm64::X3);
                enc.b_cond(arm64::EQ, boolTrue);
                enc.mov_ri64(arm64::X3, 'y');
                enc.cmp_rr(arm64::X1, arm64::X3);
                enc.b_cond(arm64::EQ, boolTrue);
                enc.mov_rr(arm64::X0, arm64::X2);
                enc.bl(runtime.lblAtoi);
                enc.cmp_ri(arm64::X0, 0);
                enc.cset(arm64::X0, arm64::NE);
                enc.b(boolEnd);
                enc.bindLabel(boolTrue);
                enc.mov_ri64(arm64::X0, 1);
                enc.bindLabel(boolEnd);
                return ThavaType::BOOL;
            }
            if (inp->inputType == ThavaType::DOUBLE || inp->inputType == ThavaType::FLOAT) {
                enc.bl(runtime.lblAtoD);
                return ThavaType::DOUBLE;
            }
            enc.bl(runtime.lblAtoi);
            return ThavaType::INT;
        }

        if (auto ic = dynamic_cast<InputCaseNode*>(node)) {
            compileExpression(ic->prompt.get());
            enc.movz(arm64::X1, ic->mode == "lower" ? 1 : (ic->mode == "upper" ? 2 : 0), 0);
            enc.bl(runtime.lblInputLine);
            return ThavaType::STRING;
        }

        // 15. rand()/randint()/randres()
        if (auto rc = dynamic_cast<RandCallNode*>(node)) {
            if (rc->funcName == "randres" && !rc->args.empty()) {
                ASTNode* list = rc->args[0].get();
                compileExpression(list);
                if (rc->weightExpr) {
                    ASTNode* weights = rc->weightExpr.get();
                    enc.sub_rri(arm64::SP, arm64::SP, 16);
                    enc.str_imm(arm64::X0, arm64::SP, 0);
                    compileExpression(weights);
                    const auto type = elemTypeOfIndexedList(weights);
                    if (type != ThavaType::INT && type != ThavaType::FLOAT && type != ThavaType::DOUBLE) {
                        std::cerr << "[ARM64 Codegen Error]: randres() requires numeric weights.\n";
                        exit(1);
                    }
                    enc.mov_rr(arm64::X1, arm64::X0);
                    enc.ldr_imm(arm64::X0, arm64::SP, 0);
                    enc.add_rri(arm64::SP, arm64::SP, 16);
                    enc.movz(arm64::X2, type != ThavaType::INT, 0);
                    enc.bl(runtime.lblWeightedChoice);
                } else {
                    enc.bl(runtime.lblRandChoice);
                }
                auto resultType = elemTypeOfIndexedList(list);
                return resultType == ThavaType::UNKNOWN ? ThavaType::STRING : resultType;
            }
            if (rc->funcName == "randint") {
                if (rc->args.size() == 2) {
                    compileExpression(rc->args[0].get());
                    enc.sub_rri(arm64::SP, arm64::SP, 16);
                    enc.str_imm(arm64::X0, arm64::SP, 0);
                    compileExpression(rc->args[1].get());
                    enc.ldr_imm(arm64::X1, arm64::SP, 0);
                    enc.add_rri(arm64::SP, arm64::SP, 16);
                    // range = b - a + 1
                    enc.sub_rrr(arm64::X2, arm64::X0, arm64::X1);
                    enc.add_rri(arm64::X2, arm64::X2, 1);
                    enc.sub_rri(arm64::SP, arm64::SP, 16);
                    enc.str_imm(arm64::X2, arm64::SP, 0);
                    enc.str_imm(arm64::X1, arm64::SP, 8);
                    enc.bl(runtime.lblRand);
                    enc.ldr_imm(arm64::X2, arm64::SP, 0);
                    enc.udiv_rrr(arm64::X3, arm64::X0, arm64::X2);
                    enc.mul_rrr(arm64::X3, arm64::X3, arm64::X2);
                    enc.sub_rrr(arm64::X0, arm64::X0, arm64::X3);
                    enc.ldr_imm(arm64::X1, arm64::SP, 8);
                    enc.add_rri(arm64::SP, arm64::SP, 16);
                    enc.add_rrr(arm64::X0, arm64::X0, arm64::X1);
                    return ThavaType::INT;
                }
                enc.bl(runtime.lblRand);
                enc.movz(arm64::X1, 11, 0);
                enc.udiv_rrr(arm64::X2, arm64::X0, arm64::X1);
                enc.mul_rrr(arm64::X2, arm64::X2, arm64::X1);
                enc.sub_rrr(arm64::X0, arm64::X0, arm64::X2);
                return ThavaType::INT;
            }
            // rand() -> double in [0, 1)
            enc.bl(runtime.lblRand);
            intToDoubleBits();
            x0BitsToD0();
            enc.adr_data(arm64::X16, runtime.offDoubleMaxRand);
            enc.fldr_imm(arm64::D1, arm64::X16, 0);
            enc.fdiv_ddd(arm64::D0, arm64::D0, arm64::D1);
            d0BitsToX0();
            return ThavaType::DOUBLE;
        }

        // 16. String methods: upper/lower/title/strip/trim
        if (auto sm = dynamic_cast<StrMethodNode*>(node)) {
            compileExpression(sm->base.get());
            if (sm->method == "upper")      enc.bl(runtime.lblStrUpper);
            else if (sm->method == "title")  enc.bl(runtime.lblStrTitle);
            else if (sm->method == "strip" || sm->method == "trim") enc.bl(runtime.lblStrStrip);
            else                             enc.bl(runtime.lblStrLower);
            return ThavaType::STRING;
        }

        // 17. String methods: find/index/count/replace/startswith/endswith/join/split
        if (auto am = dynamic_cast<StrAdvMethodNode*>(node)) {
            if (am->method == "replace" && am->args.size() >= 2) {
                compileExpression(am->base.get());
                enc.sub_rri(arm64::SP, arm64::SP, 16);
                enc.str_imm(arm64::X0, arm64::SP, 0);
                compileExpression(am->args[0].get());
                enc.sub_rri(arm64::SP, arm64::SP, 16);
                enc.str_imm(arm64::X0, arm64::SP, 0);
                compileExpression(am->args[1].get());
                enc.mov_rr(arm64::X2, arm64::X0);
                enc.ldr_imm(arm64::X1, arm64::SP, 0);
                enc.add_rri(arm64::SP, arm64::SP, 16);
                enc.ldr_imm(arm64::X0, arm64::SP, 0);
                enc.add_rri(arm64::SP, arm64::SP, 16);
                enc.bl(runtime.lblStrReplace);
                return ThavaType::STRING;
            }
            if (am->method == "join" && !am->args.empty()) {
                compileExpression(am->base.get());
                enc.sub_rri(arm64::SP, arm64::SP, 16);
                enc.str_imm(arm64::X0, arm64::SP, 0);
                compileExpression(am->args[0].get());
                enc.mov_rr(arm64::X1, arm64::X0);
                enc.ldr_imm(arm64::X0, arm64::SP, 0);
                enc.add_rri(arm64::SP, arm64::SP, 16);
                enc.bl(runtime.lblStrJoin);
                return ThavaType::STRING;
            }
            if (am->method == "split") {
                compileExpression(am->base.get());
                enc.sub_rri(arm64::SP, arm64::SP, 16);
                enc.str_imm(arm64::X0, arm64::SP, 0);
                if (!am->args.empty()) {
                    compileExpression(am->args[0].get());
                } else {
                    uint32_t spaceOff = emitter.addStringLiteralOffset(" ");
                    enc.adr_data(arm64::X0, spaceOff);
                }
                enc.mov_rr(arm64::X1, arm64::X0);
                enc.ldr_imm(arm64::X0, arm64::SP, 0);
                enc.add_rri(arm64::SP, arm64::SP, 16);
                enc.bl(runtime.lblStrSplit);
                return ThavaType::LIST;
            }

            // one-argument helpers: (base, arg)
            compileExpression(am->base.get());
            enc.sub_rri(arm64::SP, arm64::SP, 16);
            enc.str_imm(arm64::X0, arm64::SP, 0);
            if (!am->args.empty()) compileExpression(am->args[0].get());
            else enc.movz(arm64::X0, 0, 0);
            enc.mov_rr(arm64::X1, arm64::X0);
            enc.ldr_imm(arm64::X0, arm64::SP, 0);
            enc.add_rri(arm64::SP, arm64::SP, 16);
            if (am->method == "find" || am->method == "index") {
                enc.bl(runtime.lblStrFind);
                return ThavaType::INT;
            }
            if (am->method == "startswith") {
                enc.bl(runtime.lblStrStartsWith);
                return ThavaType::BOOL;
            }
            if (am->method == "endswith") {
                enc.bl(runtime.lblStrEndsWith);
                return ThavaType::BOOL;
            }
            if (am->method == "count") {
                enc.bl(runtime.lblStrCount);
                return ThavaType::INT;
            }
            enc.bl(runtime.lblStrFind);
            return ThavaType::INT;
        }

        // 18. s.split(",")[idx]
        if (auto sp = dynamic_cast<StrSplitNode*>(node)) {
            compileExpression(sp->base.get());
            enc.sub_rri(arm64::SP, arm64::SP, 16);
            enc.str_imm(arm64::X0, arm64::SP, 0);
            compileExpression(sp->delimExpr.get());
            enc.mov_rr(arm64::X1, arm64::X0);
            enc.ldr_imm(arm64::X0, arm64::SP, 0);
            enc.add_rri(arm64::SP, arm64::SP, 16);
            enc.bl(runtime.lblStrSplit);
            int32_t aIdx = 0;
            try { aIdx = std::stoi(sp->idxACode); } catch (...) { aIdx = 0; }
            enc.mov_ri64(arm64::X1, (int64_t)(8 + aIdx * 8));
            enc.add_rrr(arm64::X0, arm64::X0, arm64::X1);
            enc.ldr_imm(arm64::X0, arm64::X0, 0);
            return ThavaType::STRING;
        }

        // 19. 'item in list/string'
        if (auto inN = dynamic_cast<InNode*>(node)) {
            ThavaType itemT = compileExpression(inN->left.get());
            enc.sub_rri(arm64::SP, arm64::SP, 16);
            enc.str_imm(arm64::X0, arm64::SP, 0);
            ThavaType contT = compileExpression(inN->listExpr.get());
            if (contT == ThavaType::STRING) {
                // X0 = haystack (container), X1 = needle (item)
                enc.ldr_imm(arm64::X1, arm64::SP, 0);
                enc.add_rri(arm64::SP, arm64::SP, 16);
                enc.bl(runtime.lblStrFind);
                enc.cmp_ri(arm64::X0, 0);
                enc.cset(arm64::X0, arm64::GE);
                return ThavaType::BOOL;
            }
            enc.ldr_imm(arm64::X1, arm64::SP, 0);
            enc.add_rri(arm64::SP, arm64::SP, 16);
            enc.movz(arm64::X2, itemT == ThavaType::STRING ? 1 : 0, 0);
            // X0 = container, X1 = item, X2 = isStr
            if (contT == ThavaType::DICT) {
                enc.bl(runtime.lblDictFind);
                enc.cmp_ri(arm64::X0, 0);
                enc.cset(arm64::X0, arm64::GE);
                return ThavaType::BOOL;
            }
            enc.bl(runtime.lblListIn);
            return ThavaType::BOOL;
        }

        // 19a. Struct instantiation: Point(1, 2) -> zeroed heap block + fields
        // 18a. locvar / parvar -> the current object
        if (dynamic_cast<LocVarNode*>(node) || dynamic_cast<ParVarNode*>(node)) {
            VarInfo* thi = lookupVar("@this");
            if (!thi) {
                std::cerr << "[ARM64 Codegen Error]: 'locvar'/'parvar' used outside a class method.\n";
                exit(1);
            }
            enc.ldr_imm(arm64::X0, arm64::FP, thi->offset);
            return ThavaType::CUSTOM;
        }

        // 18b. class instantiation
        if (auto ci = dynamic_cast<ClassInstantiateNode*>(node)) {
            auto lit = classLayouts.find(ci->className);
            if (lit == classLayouts.end()) {
                std::cerr << "[ARM64 Codegen Error]: Class '" << ci->className << "' is not declared!\n";
                exit(1);
            }
            int32_t size = lit->second.totalSize > 0 ? lit->second.totalSize : 8;
            enc.mov_ri64(arm64::X0, size);
            enc.bl(runtime.lblMalloc);
            enc.sub_rri(arm64::SP, arm64::SP, 16);
            enc.str_imm(arm64::X0, arm64::SP, 0); // [SP] = instance pointer

            // Field default values, base class first.
            std::vector<std::string> chain;
            for (std::string c = ci->className; !c.empty();) {
                chain.push_back(c);
                auto b = classBase.find(c);
                c = (b != classBase.end()) ? b->second : std::string();
            }
            for (int k = (int)chain.size() - 1; k >= 0; --k) {
                auto dit = classFieldDefaults.find(chain[k]);
                if (dit == classFieldDefaults.end()) continue;
                auto lay = classLayouts.find(chain[k]);
                for (auto& d : dit->second) {
                    const StructFieldInfo* fld = nullptr;
                    if (lay != classLayouts.end()) {
                        for (const auto& f : lay->second.fields)
                            if (f.offset == d.first) { fld = &f; break; }
                    }
                    if (!fld) continue;
                    compileExpression(d.second);
                    enc.ldr_imm(arm64::X9, arm64::SP, 0);
                    if (fld->type == ThavaType::INT) narrowIntX0(fld->customType);
                    enc.str_imm(arm64::X0, arm64::X9, fld->offset);
                }
            }

            // Constructor call
            ThavaType ctorRet = ThavaType::VOID;
            std::string ctorKey = ci->className + "::init";
            size_t ctorLbl = findMethodLabel(ci->className, "init", &ctorRet);
            std::vector<std::pair<ThavaType, std::string>> ctorParams;
            auto cpit = classMethodParams.find(ctorKey);
            if (cpit != classMethodParams.end()) ctorParams = cpit->second;

            if (ctorLbl != 0 && ctorParams.size() == ci->args.size()) {
                emitClassCall(ctorLbl, ci->args, nullptr, true, &ctorParams);
            }

            enc.ldr_imm(arm64::X0, arm64::SP, 0);
            enc.add_rri(arm64::SP, arm64::SP, 16);
            return ThavaType::CUSTOM;
        }

        // 18c. class method call
        if (auto mc = dynamic_cast<MethodCallNode*>(node)) {
            std::string cls = resolveStructName(mc->target.get());
            if (classLayouts.count(cls)) {
                ThavaType ret = ThavaType::VOID;
                std::string key = cls + "::" + mc->methodName;
                size_t lbl = findMethodLabel(cls, mc->methodName, &ret);
                if (lbl == 0) {
                    std::cerr << "[ARM64 Codegen Error]: class '" << cls << "' has no method '"
                              << mc->methodName << "'.\n";
                    exit(1);
                }
                const auto mpIt = classMethodParams.find(key);
                if (mpIt != classMethodParams.end())
                    emitClassCall(lbl, mc->args, mc->target.get(), false, &mpIt->second);
                else
                    emitClassCall(lbl, mc->args, mc->target.get(), false, nullptr);
                return ret;
            }
            std::cerr << "[ARM64 Codegen Error]: cannot resolve the class of method call '"
                      << mc->methodName << "'.\n";
            exit(1);
        }

        if (auto si = dynamic_cast<StructInstantiateNode*>(node)) {
            auto sit = g_structs.find(si->structName);
            if (sit == g_structs.end()) {
                std::cerr << "[ARM64 Codegen Error]: Struct not found '" << si->structName << "'!\n";
                exit(1);
            }
            const auto& sinfo = sit->second;
            enc.mov_ri64(arm64::X0, sinfo.totalSize > 0 ? sinfo.totalSize : 8);
            enc.bl(runtime.lblMalloc); // mmap memory is already zero-filled
            enc.sub_rri(arm64::SP, arm64::SP, 16);
            enc.str_imm(arm64::X0, arm64::SP, 0);
            for (size_t i = 0; i < si->args.size() && i < sinfo.fields.size(); ++i) {
                const auto& f = sinfo.fields[i];
                ThavaType at = compileExpression(si->args[i].get());
                enc.ldr_imm(arm64::X6, arm64::SP, 0);
                if (f.type == ThavaType::DOUBLE) {
                    if (at == ThavaType::INT) intToDoubleBits();
                    enc.str_imm(arm64::X0, arm64::X6, f.offset);
                } else if (f.type == ThavaType::FLOAT) {
                    if (at == ThavaType::INT) intToDoubleBits();
                    enc.fmov_dx(arm64::D0, arm64::X0);
                    enc.fcvt_ds(arm64::D0, arm64::D0);
                    enc.fstr_s_imm(arm64::D0, arm64::X6, f.offset);
                } else {
                    if (f.type == ThavaType::INT) narrowIntX0(f.customType);
                    enc.str_imm(arm64::X0, arm64::X6, f.offset);
                }
            }
            enc.ldr_imm(arm64::X0, arm64::SP, 0);
            enc.add_rri(arm64::SP, arm64::SP, 16);
            return ThavaType::CUSTOM;
        }

        // 19a2. Struct field read: p.x / p.inner.y
        if (auto mem = dynamic_cast<MemberAccessNode*>(node)) {
            std::string structName = resolveStructName(mem->target.get());
            auto sit = g_structs.find(structName);
            if (sit == g_structs.end()) {
                if (auto v = dynamic_cast<VarNode*>(mem->target.get())) {
                    auto eit = g_enums.find(v->getName());
                    if (eit != g_enums.end()) {
                        auto mit = eit->second.members.find(mem->memberName);
                        if (mit != eit->second.members.end()) {
                            enc.mov_ri64(arm64::X0, mit->second);
                            return ThavaType::INT;
                        }
                    }
                }
                std::cerr << "[ARM64 Codegen Error]: Struct or enum not found for '"
                          << mem->memberName << "'!\n";
                exit(1);
            }
            const auto& sinfo = sit->second;
            auto fit = sinfo.fieldIndex.find(mem->memberName);
            if (fit == sinfo.fieldIndex.end()) {
                std::cerr << "[ARM64 Codegen Error]: Struct '" << structName
                          << "' has no member '" << mem->memberName << "'!\n";
                exit(1);
            }
            const auto& fld = sinfo.fields[fit->second];
            compileExpression(mem->target.get());
            if (fld.type == ThavaType::FLOAT) {
                enc.fldr_s_imm(arm64::D0, arm64::X0, fld.offset);
                enc.fcvt_sd(arm64::D0, arm64::D0);
                enc.fmov_xd(arm64::X0, arm64::D0);
                return ThavaType::DOUBLE;
            }
            enc.ldr_imm(arm64::X0, arm64::X0, fld.offset);
            return fld.type;
        }

        // 19b. File methods
        if (auto fm = dynamic_cast<FileMethodNode*>(node)) {
            VarInfo* vi = lookupVar(fm->varName);
            if (!vi) {
                std::cerr << "[ARM64 Codegen Error]: File variable '" << fm->varName
                          << "' is not declared!\n";
                exit(1);
            }
            std::string m = fm->method;
            if (m == "readline") m = "read_line";
            else if (m == "readlines") m = "read_lines";
            else if (m == "writeline") m = "write_line";

            if (m == "read") {
                enc.ldr_imm(arm64::X0, arm64::FP, vi->offset);
                enc.bl(runtime.lblFileReadAll);
                return ThavaType::STRING;
            }
            if (m == "read_line") {
                enc.ldr_imm(arm64::X0, arm64::FP, vi->offset);
                enc.bl(runtime.lblFileReadLine);
                return ThavaType::STRING;
            }
            if (m == "read_lines") {
                enc.ldr_imm(arm64::X0, arm64::FP, vi->offset);
                enc.bl(runtime.lblFileReadLines);
                return ThavaType::LIST;
            }
            if (m == "write" || m == "write_line") {
                if (!fm->args.empty()) {
                    ThavaType t = compileExpression(fm->args[0].get());
                    if (t == ThavaType::STRING) {
                        enc.sub_rri(arm64::SP, arm64::SP, 16);
                        enc.str_imm(arm64::X0, arm64::SP, 0);
                        enc.bl(runtime.lblStrLen);
                        enc.mov_rr(arm64::X2, arm64::X0);
                        enc.ldr_imm(arm64::X1, arm64::SP, 0);
                        enc.add_rri(arm64::SP, arm64::SP, 16);
                        enc.ldr_imm(arm64::X0, arm64::FP, vi->offset);
                        enc.bl(runtime.lblFileWrite);
                    } else if (t == ThavaType::INT || t == ThavaType::BOOL) {
                        if (t == ThavaType::BOOL) {
                            size_t trueLbl = enc.createLabel();
                            size_t endLbl = enc.createLabel();
                            enc.cbnz(arm64::X0, trueLbl);
                            enc.adr_data(arm64::X0, runtime.offStrFalse);
                            enc.b(endLbl);
                            enc.bindLabel(trueLbl);
                            enc.adr_data(arm64::X0, runtime.offStrTrue);
                            enc.bindLabel(endLbl);
                            enc.sub_rri(arm64::SP, arm64::SP, 16);
                            enc.str_imm(arm64::X0, arm64::SP, 0);
                            enc.bl(runtime.lblStrLen);
                            enc.mov_rr(arm64::X2, arm64::X0);
                            enc.ldr_imm(arm64::X1, arm64::SP, 0);
                            enc.add_rri(arm64::SP, arm64::SP, 16);
                            enc.ldr_imm(arm64::X0, arm64::FP, vi->offset);
                            enc.bl(runtime.lblFileWrite);
                        } else {
                            enc.mov_rr(arm64::X1, arm64::X0);
                            enc.ldr_imm(arm64::X0, arm64::FP, vi->offset);
                            enc.bl(runtime.lblFileWriteInt);
                        }
                    } else if (t == ThavaType::DOUBLE || t == ThavaType::FLOAT) {
                        enc.mov_rr(arm64::X1, arm64::X0);
                        enc.ldr_imm(arm64::X0, arm64::FP, vi->offset);
                        enc.bl(runtime.lblFileWriteDouble);
                    }
                }
                if (m == "write_line") {
                    uint32_t nlOff = emitter.addStringLiteralOffset("\n");
                    enc.sub_rri(arm64::SP, arm64::SP, 16);
                    enc.adr_data(arm64::X1, nlOff);
                    enc.movz(arm64::X2, 1, 0);
                    enc.ldr_imm(arm64::X0, arm64::FP, vi->offset);
                    enc.bl(runtime.lblFileWrite);
                    enc.add_rri(arm64::SP, arm64::SP, 16);
                }
                return ThavaType::VOID;
            }
            return ThavaType::VOID;
        }

        // 20. os.* (Linux AArch64 syscalls)
        if (auto os = dynamic_cast<OsMethodNode*>(node)) {
            if constexpr (std::is_same_v<TRuntime, Arm64WinRuntime>) return emitWindowsOs(os);
            if (os->method == "name") {
                uint32_t off = emitter.addStringLiteralOffset("linux");
                enc.adr_data(arm64::X0, off);
                return ThavaType::STRING;
            }
            if (os->method == "getpid") {
                enc.movz(arm64::X8, 172, 0);
                enc.svc(0);
                return ThavaType::INT;
            }
            if (os->method == "sleep") {
                if (!os->args.empty()) {
                    compileExpression(os->args[0].get());
                    enc.movz(arm64::X3, 1000, 0);
                    enc.sdiv_rrr(arm64::X1, arm64::X0, arm64::X3);
                    enc.mul_rrr(arm64::X4, arm64::X1, arm64::X3);
                    enc.sub_rrr(arm64::X5, arm64::X0, arm64::X4);
                    enc.mov_ri64(arm64::X6, 1000000);
                    enc.mul_rrr(arm64::X5, arm64::X5, arm64::X6);
                    enc.sub_rri(arm64::SP, arm64::SP, 32);
                    enc.str_imm(arm64::X1, arm64::SP, 16);
                    enc.str_imm(arm64::X5, arm64::SP, 24);
                    enc.add_rri(arm64::X0, arm64::SP, 16);
                    enc.movz(arm64::X1, 0, 0);
                    enc.movz(arm64::X8, 101, 0);
                    enc.svc(0);
                    enc.add_rri(arm64::SP, arm64::SP, 32);
                }
                enc.movz(arm64::X0, 0, 0);
                return ThavaType::INT;
            }
            if (os->method == "system") {
                if (os->args.empty()) {
                    enc.movz(arm64::X0, 0, 0);
                    return ThavaType::INT;
                }
                compileExpression(os->args[0].get());
                enc.sub_rri(arm64::SP, arm64::SP, 64);
                enc.str_imm(arm64::X0, arm64::SP, 0);
                enc.adr_data(arm64::X9, runtime.offStrSh);
                enc.str_imm(arm64::X9, arm64::SP, 16);
                enc.adr_data(arm64::X9, runtime.offStrDashC);
                enc.str_imm(arm64::X9, arm64::SP, 24);
                enc.ldr_imm(arm64::X9, arm64::SP, 0);
                enc.str_imm(arm64::X9, arm64::SP, 32);
                enc.movz(arm64::X9, 0, 0);
                enc.str_imm(arm64::X9, arm64::SP, 40);
                // AArch64 has no fork(2): clone(SIGCHLD, 0, 0, 0, 0)
                enc.movz(arm64::X0, 17, 0); // SIGCHLD
                enc.movz(arm64::X1, 0, 0);
                enc.movz(arm64::X2, 0, 0);
                enc.movz(arm64::X3, 0, 0);
                enc.movz(arm64::X4, 0, 0);
                enc.movz(arm64::X8, 220, 0); // clone
                enc.svc(0);
                size_t parentLbl = enc.createLabel();
                enc.cbnz(arm64::X0, parentLbl);
                enc.adr_data(arm64::X0, runtime.offStrSh);
                enc.add_rri(arm64::X1, arm64::SP, 16);
                enc.movz(arm64::X2, 0, 0);
                enc.movz(arm64::X8, 221, 0); // execve
                enc.svc(0);
                enc.movz(arm64::X0, 1, 0);
                enc.movz(arm64::X8, 93, 0);
                enc.svc(0);
                enc.bindLabel(parentLbl);
                enc.add_rri(arm64::X1, arm64::SP, 8);
                enc.movz(arm64::X2, 0, 0);
                enc.movz(arm64::X3, 0, 0);
                enc.movz(arm64::X8, 260, 0); // wait4
                enc.svc(0);
                enc.ldr_imm(arm64::X0, arm64::SP, 8);
                enc.lsr_ri(arm64::X0, arm64::X0, 8);
                enc.movz(arm64::X9, 0xFF, 0);
                enc.and_rrr(arm64::X0, arm64::X0, arm64::X9);
                enc.add_rri(arm64::SP, arm64::SP, 64);
                return ThavaType::INT;
            }
            if (os->method == "chdir" && !os->args.empty()) {
                compileExpression(os->args[0].get());
                enc.movz(arm64::X8, 49, 0);
                enc.svc(0);
                enc.cmp_ri(arm64::X0, 0);
                enc.cset(arm64::X0, arm64::EQ);
                return ThavaType::BOOL;
            }
            if (os->method == "exists" && !os->args.empty()) {
                compileExpression(os->args[0].get());
                enc.mov_rr(arm64::X1, arm64::X0);
                enc.movn(arm64::X0, 99, 0);
                enc.movz(arm64::X2, 0, 0);
                enc.movz(arm64::X3, 0, 0);
                enc.movz(arm64::X8, 48, 0); // faccessat
                enc.svc(0);
                enc.cmp_ri(arm64::X0, 0);
                enc.cset(arm64::X0, arm64::EQ);
                return ThavaType::BOOL;
            }
            if ((os->method == "remove" || os->method == "rmdir") && !os->args.empty()) {
                compileExpression(os->args[0].get());
                enc.mov_rr(arm64::X1, arm64::X0);
                enc.movn(arm64::X0, 99, 0);
                enc.movz(arm64::X2, os->method == "rmdir" ? 0x200 : 0, 0);
                enc.movz(arm64::X8, 35, 0); // unlinkat
                enc.svc(0);
                enc.cmp_ri(arm64::X0, 0);
                enc.cset(arm64::X0, arm64::EQ);
                return ThavaType::BOOL;
            }
            if (os->method == "mkdir" && !os->args.empty()) {
                compileExpression(os->args[0].get());
                enc.mov_rr(arm64::X1, arm64::X0);
                enc.movn(arm64::X0, 99, 0);
                enc.mov_ri64(arm64::X2, 0x1FF);
                enc.movz(arm64::X8, 34, 0); // mkdirat
                enc.svc(0);
                enc.cmp_ri(arm64::X0, 0);
                enc.cset(arm64::X0, arm64::EQ);
                return ThavaType::BOOL;
            }
            if (os->method == "rename" && os->args.size() >= 2) {
                compileExpression(os->args[0].get());
                enc.sub_rri(arm64::SP, arm64::SP, 16);
                enc.str_imm(arm64::X0, arm64::SP, 0);
                compileExpression(os->args[1].get());
                enc.mov_rr(arm64::X3, arm64::X0);
                enc.ldr_imm(arm64::X1, arm64::SP, 0);
                enc.add_rri(arm64::SP, arm64::SP, 16);
                enc.movn(arm64::X0, 99, 0);
                enc.movn(arm64::X2, 99, 0);
                enc.movz(arm64::X8, 38, 0); // renameat
                enc.svc(0);
                enc.cmp_ri(arm64::X0, 0);
                enc.cset(arm64::X0, arm64::EQ);
                return ThavaType::BOOL;
            }
            if (os->method == "getenv" && !os->args.empty()) {
                compileExpression(os->args[0].get());
                enc.bl(runtime.lblGetenv);
                return ThavaType::STRING;
            }
            if (os->method == "getcwd") {
                enc.mov_ri64(arm64::X0, 4096);
                enc.bl(runtime.lblMalloc);
                enc.str_imm(arm64::X0, arm64::SP, -16);
                enc.sub_rri(arm64::SP, arm64::SP, 16);
                enc.ldr_imm(arm64::X0, arm64::SP, 0);
                enc.mov_ri64(arm64::X1, 4096);
                enc.movz(arm64::X8, 17, 0); // getcwd
                enc.svc(0);
                enc.ldr_imm(arm64::X0, arm64::SP, 0);
                enc.add_rri(arm64::SP, arm64::SP, 16);
                return ThavaType::STRING;
            }
            if (os->method == "getsize" && !os->args.empty()) {
                compileExpression(os->args[0].get());
                enc.mov_rr(arm64::X1, arm64::X0);
                enc.sub_rri(arm64::SP, arm64::SP, 160);
                enc.movn(arm64::X0, 99, 0);
                enc.add_rri(arm64::X2, arm64::SP, 16);
                enc.movz(arm64::X3, 0, 0);
                enc.movz(arm64::X8, 79, 0); // newfstatat
                enc.svc(0);
                size_t sizeOk = enc.createLabel();
                size_t sizeEnd = enc.createLabel();
                enc.cmp_ri(arm64::X0, 0);
                enc.b_cond(arm64::GE, sizeOk);
                enc.movn(arm64::X0, 0, 0);
                enc.b(sizeEnd);
                enc.bindLabel(sizeOk);
                enc.ldr_imm(arm64::X0, arm64::SP, 16 + 48); // st_size
                enc.bindLabel(sizeEnd);
                enc.add_rri(arm64::SP, arm64::SP, 160);
                return ThavaType::INT;
            }
            return ThavaType::UNKNOWN;
        }

        // 21. time.* (timestamp/sleep)
        if (auto tm = dynamic_cast<TimeMethodNode*>(node)) {
            if constexpr (std::is_same_v<TRuntime, Arm64WinRuntime>) return emitWindowsTime(tm);
            if (tm->method == "timestamp") {
                enc.sub_rri(arm64::SP, arm64::SP, 32);
                enc.add_rri(arm64::X0, arm64::SP, 16);
                enc.movz(arm64::X1, 0, 0);
                enc.movz(arm64::X8, 113, 0);
                enc.svc(0);
                enc.ldr_imm(arm64::X0, arm64::SP, 16);
                enc.add_rri(arm64::SP, arm64::SP, 32);
                return ThavaType::INT;
            }
            if (tm->method == "sleep") {
                if (!tm->args.empty()) {
                    compileExpression(tm->args[0].get());
                    enc.movz(arm64::X3, 1000, 0);
                    enc.sdiv_rrr(arm64::X1, arm64::X0, arm64::X3);
                    enc.mul_rrr(arm64::X4, arm64::X1, arm64::X3);
                    enc.sub_rrr(arm64::X5, arm64::X0, arm64::X4);
                    enc.mov_ri64(arm64::X6, 1000000);
                    enc.mul_rrr(arm64::X5, arm64::X5, arm64::X6);
                    enc.sub_rri(arm64::SP, arm64::SP, 32);
                    enc.str_imm(arm64::X1, arm64::SP, 16);
                    enc.str_imm(arm64::X5, arm64::SP, 24);
                    enc.add_rri(arm64::X0, arm64::SP, 16);
                    enc.movz(arm64::X1, 0, 0);
                    enc.movz(arm64::X8, 101, 0);
                    enc.svc(0);
                    enc.add_rri(arm64::SP, arm64::SP, 32);
                }
                enc.movz(arm64::X0, 0, 0);
                return ThavaType::INT;
            }
            return ThavaType::UNKNOWN;
        }

        return ThavaType::UNKNOWN;
    }

    static bool isForeignScalar(ThavaType type) {
        return type == ThavaType::INT || type == ThavaType::BOOL || type == ThavaType::STRING ||
               type == ThavaType::FLOAT || type == ThavaType::DOUBLE;
    }

    // Stage values so a nested call cannot clobber an earlier argument.
    // Windows ARM64 uses separate integer and SIMD register banks.
    int emitWindowsForeignArgs(const std::vector<ASTPtr>& args, size_t first,
                              const std::vector<ThavaType>* signature = nullptr) {
        using namespace arm64;
        const size_t count = args.size() - first;
        if (count > 240) {
            std::cerr << "[ARM64 Codegen Error]: Too many foreign arguments.\n";
            exit(1);
        }
        const int frame = static_cast<int>(count * 16);
        if (frame) enc.sub_rri(SP, SP, frame);
        std::vector<ThavaType> types;
        for (size_t i = 0; i < count; ++i) {
            const auto actual = compileExpression(args[first + i].get());
            const auto type = signature ? signature->at(i) : actual;
            if (!isForeignScalar(actual) || !isForeignScalar(type) ||
                ((type == ThavaType::STRING) != (actual == ThavaType::STRING))) {
                std::cerr << "[ARM64 Codegen Error]: Expected a matching scalar C argument.\n";
                exit(1);
            }
            const int slot = static_cast<int>((count + i) * 8);
            const bool fp = type == ThavaType::FLOAT || type == ThavaType::DOUBLE;
            const bool actualFp = actual == ThavaType::FLOAT || actual == ThavaType::DOUBLE;
            if (fp && !actualFp) intToDoubleBits();
            if (!fp && actualFp) doubleBitsToInt();
            if (type == ThavaType::FLOAT) {
                enc.movz(X9, 0, 0);
                enc.str_imm(X9, SP, slot);
                enc.fmov_dx(D0, X0);
                enc.fcvt_ds(D0, D0);
                enc.fstr_s_imm(D0, SP, slot);
            } else enc.str_imm(X0, SP, slot);
            types.push_back(type);
        }

        int integer = 0, simd = 0, spill = 0;
        for (size_t i = 0; i < count; ++i) {
            const int slot = static_cast<int>((count + i) * 8);
            const auto type = types[i];
            if (type == ThavaType::FLOAT || type == ThavaType::DOUBLE) {
                if (simd < 8) {
                    const auto reg = static_cast<VReg>(simd++);
                    if (type == ThavaType::FLOAT) enc.fldr_s_imm(reg, SP, slot);
                    else enc.fldr_imm(reg, SP, slot);
                    continue;
                }
            } else if (integer < 8) {
                enc.ldr_imm(static_cast<Reg>(integer++), SP, slot);
                continue;
            }
            enc.ldr_imm(X9, SP, slot);
            enc.str_imm(X9, SP, spill);
            spill += 8;
        }
        return frame;
    }

    ThavaType emitWindowsCFunction(const std::string& name, const std::vector<ASTPtr>& args) {
        if (!g_isCFunc.count(name) || args.size() != g_funcParamTypes.at(name).size()) {
            std::cerr << "[ARM64 Codegen Error]: c." << name
                      << " requires a matching cfunc declaration.\n";
            exit(1);
        }
        const auto result = g_funcReturnTypes.at(name);
        if (result != ThavaType::VOID && !isForeignScalar(result)) {
            std::cerr << "[ARM64 Codegen Error]: Unsupported C return type.\n";
            exit(1);
        }
        const auto label = runtime.importFunction("msvcrt.dll", name);
        const int frame = emitWindowsForeignArgs(args, 0, &g_funcParamTypes.at(name));
        enc.bl(label);
        if (frame) enc.add_rri(arm64::SP, arm64::SP, frame);
        if (result == ThavaType::INT) narrowIntX0(g_funcReturnWidths[name]);
        if (result == ThavaType::FLOAT) enc.fcvt_sd(arm64::D0, arm64::D0);
        if (result == ThavaType::FLOAT || result == ThavaType::DOUBLE) d0BitsToX0();
        return result;
    }

    ThavaType emitWindowsCLib(CLibMethodNode* node) {
        using namespace arm64;
        const auto& method = node->method;
        const auto require = [&](size_t count) {
            if (node->args.size() < count) {
                std::cerr << "[ARM64 Codegen Error]: clib." << method
                          << " requires " << count << " argument(s).\n";
                exit(1);
            }
        };

        if (method == "load" || method == "free" || method == "alloc" || method == "free_mem") {
            if (method == "free" && node->target) compileExpression(node->target.get());
            else {
                require(1);
                compileExpression(node->args[0].get());
            }
            if (method == "load") emitWindowsImport("LoadLibraryA");
            else if (method == "alloc") enc.bl(runtime.lblMalloc);
            else if (method == "free_mem") {
                emitWindowsImport("free");
                return ThavaType::VOID;
            } else {
                emitWindowsImport("FreeLibrary");
                enc.cmp_ri(X0, 0);
                enc.cset(X0, NE);
                return ThavaType::BOOL;
            }
            return ThavaType::INT;
        }

        if (method == "has" || method == "call" || method == "call_str" || method == "call_dbl") {
            const size_t symbol = node->target ? 0 : 1;
            require(symbol + 1);
            enc.sub_rri(SP, SP, 16);
            if (node->target) compileExpression(node->target.get());
            else if (compileExpression(node->args[0].get()) == ThavaType::STRING)
                enc.bl(runtime.lblNamedModule);
            enc.str_imm(X0, SP, 0);
            compileExpression(node->args[symbol].get());
            enc.mov_rr(X1, X0);
            enc.ldr_imm(X0, SP, 0);
            auto missing = enc.createLabel(), cleanup = enc.createLabel();
            enc.cbz(X0, missing);
            emitWindowsImport("GetProcAddress");
            enc.cbz(X0, missing);
            if (method == "has") {
                enc.movz(X0, 1, 0);
            } else {
                enc.str_imm(X0, SP, 8);
                const int frame = emitWindowsForeignArgs(node->args, symbol + 1);
                enc.ldr_imm(X16, SP, frame + 8);
                enc.blr(X16);
                if (frame) enc.add_rri(SP, SP, frame);
                if (method == "call_dbl") d0BitsToX0();
                if (method == "call_str") {
                    enc.cbz(X0, missing);
                    emitWindowsImport("_strdup");
                    enc.cbz(X0, missing);
                }
            }
            enc.b(cleanup);
            enc.bindLabel(missing);
            if (method == "call_str") enc.adr_data(X0, runtime.offEmptyStr);
            enc.bindLabel(cleanup);
            enc.add_rri(SP, SP, 16);
            if (method == "has") return ThavaType::BOOL;
            if (method == "call_str") return ThavaType::STRING;
            if (method == "call_dbl") return ThavaType::DOUBLE;
            return ThavaType::INT;
        }

        const bool store = method == "poke" || method == "poke32" || method == "poke64" || method == "poke_f";
        if (store || method == "peek" || method == "peek32" || method == "peek64" ||
            method == "peek_f" || method == "peek_u8") {
            require(store ? 3 : 1);
            enc.sub_rri(SP, SP, 16);
            compileExpression(node->args[0].get());
            enc.str_imm(X0, SP, 0);
            if (node->args.size() > 1) {
                compileExpression(node->args[1].get());
                enc.ldr_imm(X9, SP, 0);
                enc.add_rrr(X0, X9, X0);
                enc.str_imm(X0, SP, 0);
            }
            if (store) {
                const auto type = compileExpression(node->args[2].get());
                if (method == "poke_f" && type != ThavaType::FLOAT && type != ThavaType::DOUBLE)
                    intToDoubleBits();
                enc.ldr_imm(X9, SP, 0);
                if (method == "poke" || method == "poke32") enc.str_w(X0, X9);
                else enc.str_imm(X0, X9, 0);
                enc.add_rri(SP, SP, 16);
                return ThavaType::VOID;
            }
            enc.ldr_imm(X9, SP, 0);
            if (method == "peek_u8") enc.ldrb_imm(X0, X9, 0);
            else if (method == "peek" || method == "peek32") {
                enc.ldr_w(X0, X9);
                narrowIntX0("");
            } else enc.ldr_imm(X0, X9, 0);
            enc.add_rri(SP, SP, 16);
            return method == "peek_f" ? ThavaType::DOUBLE : ThavaType::INT;
        }

        std::cerr << "[ARM64 Codegen Error]: Unsupported clib method: " << method << "\n";
        exit(1);
    }

    void emitWindowsImport(const std::string& name) {
        enc.bl(runtime.windowsImportLabels.at(name));
    }

    void emitWindowsSleep(const std::vector<ASTPtr>& args) {
        if (!args.empty()) {
            compileExpression(args[0].get());
            auto skip = enc.createLabel();
            enc.cmp_ri(arm64::X0, 0);
            enc.b_cond(arm64::LE, skip);
            enc.mov_ri64(arm64::X9, 0xfffffffe);
            enc.cmp_rr(arm64::X0, arm64::X9);
            enc.b_cond(arm64::HI, skip);
            emitWindowsImport("Sleep");
            enc.bindLabel(skip);
        }
        enc.movz(arm64::X0, 0, 0);
    }

    ThavaType emitWindowsOs(OsMethodNode* os) {
        const auto& name = os->method;
        const size_t required = name == "showmessagebox" ? 4 :
            (name == "rename" || name == "setenv") ? 2 :
            (name == "name" || name == "sleep" || name == "getpid" ||
             name == "cpu_count" || name == "getcwd" || name == "listdir" ? 0 : 1);
        if (os->args.size() < required) {
            std::cerr << "[ARM64 Codegen Error]: os." << name << " requires "
                      << required << " argument(s).\n";
            exit(1);
        }
        if (name == "name") {
            enc.adr_data(arm64::X0, emitter.addStringLiteralOffset("windows"));
            return ThavaType::STRING;
        }
        if (name == "sleep") {
            emitWindowsSleep(os->args);
            return ThavaType::INT;
        }
        if (name == "getpid" || name == "cpu_count") {
            if (name == "cpu_count") enc.movz(arm64::X0, 0xffff, 0);
            emitWindowsImport(name == "getpid" ? "GetCurrentProcessId" : "GetActiveProcessorCount");
            return ThavaType::INT;
        }
        if (name == "getcwd" || name == "getenv") {
            if (name == "getcwd") {
                enc.movz(arm64::X0, 0, 0);
                enc.movz(arm64::X1, 0, 0);
                emitWindowsImport("_getcwd");
            } else {
                compileExpression(os->args.at(0).get());
                emitWindowsImport("getenv");
                auto missing = enc.createLabel();
                enc.cbz(arm64::X0, missing);
                emitWindowsImport("_strdup");
                enc.bindLabel(missing);
            }
            auto found = enc.createLabel();
            enc.cbnz(arm64::X0, found);
            enc.adr_data(arm64::X0, runtime.offEmptyStr);
            enc.bindLabel(found);
            return ThavaType::STRING;
        }
        if (name == "listdir") {
            if (os->args.empty()) enc.adr_data(arm64::X0, runtime.offDot);
            else compileExpression(os->args[0].get());
            enc.bl(runtime.lblListDir);
            return ThavaType::LIST;
        }
        if (name == "showmessagebox") {
            enc.sub_rri(arm64::SP, arm64::SP, 32);
            for (int i = 0; i < 4; ++i) {
                const auto type = compileExpression(os->args[i].get());
                if ((i == 0 || i == 3) && type == ThavaType::STRING)
                    enc.bl(i == 0 ? runtime.lblMessageIcon : runtime.lblMessageButtons);
                enc.str_imm(arm64::X0, arm64::SP, i * 8);
            }
            enc.movz(arm64::X0, 0, 0);
            enc.ldr_imm(arm64::X1, arm64::SP, 16); // content
            enc.ldr_imm(arm64::X2, arm64::SP, 8);  // title
            enc.ldr_imm(arm64::X3, arm64::SP, 0);
            enc.ldr_imm(arm64::X9, arm64::SP, 24);
            enc.orr_rrr(arm64::X3, arm64::X3, arm64::X9);
            enc.mov_ri64(arm64::X9, 0x40000); // MB_TOPMOST
            enc.orr_rrr(arm64::X3, arm64::X3, arm64::X9);
            enc.add_rri(arm64::SP, arm64::SP, 32);
            emitWindowsImport("MessageBoxA");
            return ThavaType::INT;
        }
        if (name == "getsize") {
            compileExpression(os->args.at(0).get());
            enc.sub_rri(arm64::SP, arm64::SP, 48);
            enc.movz(arm64::X1, 0, 0);
            enc.mov_rr(arm64::X2, arm64::SP);
            emitWindowsImport("GetFileAttributesExA");
            auto missing = enc.createLabel(), done = enc.createLabel();
            enc.cbz(arm64::X0, missing);
            // WIN32_FILE_ATTRIBUTE_DATA ends with nFileSizeHigh and nFileSizeLow.
            enc.ldr_imm(arm64::X9, arm64::SP, 28);
            enc.lsl_ri(arm64::X0, arm64::X9, 32);
            enc.lsr_ri(arm64::X9, arm64::X9, 32);
            enc.orr_rrr(arm64::X0, arm64::X0, arm64::X9);
            enc.b(done);
            enc.bindLabel(missing);
            enc.movn(arm64::X0, 0, 0);
            enc.bindLabel(done);
            enc.add_rri(arm64::SP, arm64::SP, 48);
            return ThavaType::INT;
        }
        if (name == "exists" || name == "is_file" || name == "is_dir") {
            compileExpression(os->args.at(0).get());
            emitWindowsImport("GetFileAttributesA");
            auto valid = enc.createLabel(), done = enc.createLabel();
            enc.mov_ri64(arm64::X9, 0xffffffff);
            enc.cmp_rr(arm64::X0, arm64::X9);
            enc.b_cond(arm64::NE, valid);
            enc.movz(arm64::X0, 0, 0);
            enc.b(done);
            enc.bindLabel(valid);
            if (name == "exists") enc.movz(arm64::X0, 1, 0);
            else {
                enc.movz(arm64::X9, 0x10, 0); // FILE_ATTRIBUTE_DIRECTORY
                enc.and_rrr(arm64::X0, arm64::X0, arm64::X9);
                enc.cmp_ri(arm64::X0, 0);
                enc.cset(arm64::X0, name == "is_dir" ? arm64::NE : arm64::EQ);
            }
            enc.bindLabel(done);
            return ThavaType::BOOL;
        }

        static const std::map<std::string, std::string> crtMethods = {
            {"chdir", "_chdir"}, {"mkdir", "_mkdir"}, {"rmdir", "_rmdir"},
            {"remove", "remove"}, {"rename", "rename"}, {"system", "system"}
        };
        compileExpression(os->args.at(0).get());
        if (name == "rename" || name == "setenv") {
            enc.sub_rri(arm64::SP, arm64::SP, 16);
            enc.str_imm(arm64::X0, arm64::SP, 0);
            compileExpression(os->args.at(1).get());
            enc.mov_rr(arm64::X1, arm64::X0);
            enc.ldr_imm(arm64::X0, arm64::SP, 0);
            enc.add_rri(arm64::SP, arm64::SP, 16);
        }
        if (name == "setenv") {
            enc.bl(runtime.lblSetEnv);
            return ThavaType::BOOL;
        }
        emitWindowsImport(crtMethods.at(name));
        if (name == "system") {
            enc.lsl_ri(arm64::X0, arm64::X0, 32);
            enc.asr_ri(arm64::X0, arm64::X0, 32);
            return ThavaType::INT;
        }
        enc.cmp_ri(arm64::X0, 0);
        enc.cset(arm64::X0, arm64::EQ);
        return ThavaType::BOOL;
    }

    ThavaType emitWindowsTime(TimeMethodNode* time) {
        if (time->method == "date" || time->method == "now") {
            if (time->args.empty()) enc.movz(arm64::X0, 0, 0);
            else compileExpression(time->args[0].get());
            enc.adr_data(arm64::X1, runtime.offTimeDefault[time->method == "date"]);
            enc.bl(runtime.lblTimeFormat);
            return ThavaType::STRING;
        }
        if (time->method == "sleep") {
            emitWindowsSleep(time->args);
        } else if (time->method == "timestamp") {
            enc.movz(arm64::X0, 0, 0);
            emitWindowsImport("time");
            enc.lsl_ri(arm64::X0, arm64::X0, 32);
            enc.asr_ri(arm64::X0, arm64::X0, 32);
        } else {
            enc.sub_rri(arm64::SP, arm64::SP, 16);
            enc.mov_rr(arm64::X0, arm64::SP);
            emitWindowsImport("GetLocalTime");
            const auto field = windowsTimeField(time->method);
            enc.ldrb_imm(arm64::X0, arm64::SP, field);
            enc.ldrb_imm(arm64::X9, arm64::SP, field + 1);
            enc.lsl_ri(arm64::X9, arm64::X9, 8);
            enc.orr_rrr(arm64::X0, arm64::X0, arm64::X9);
            enc.add_rri(arm64::SP, arm64::SP, 16);
        }
        return ThavaType::INT;
    }

    // ==========================================
    // Print a literal piece of text (no newline)
    // ==========================================
    void emitPrintValueRaw(ThavaType t) {
        if (t == ThavaType::BOXED || t == ThavaType::MULTYPE) {
            if (t == ThavaType::BOXED) emitBoxedString();
            else emitMultypeString();
            enc.bl(runtime.lblPrintStr);
        } else if (t == ThavaType::STRING || t == ThavaType::DECIMAL) {
            enc.bl(runtime.lblPrintStr);
        } else if (t == ThavaType::NONE) {
            enc.adr_data(arm64::X0, emitter.addStringLiteralOffset("none"));
            enc.bl(runtime.lblPrintStr);
        } else if (t == ThavaType::BOOL) {
            size_t falseLbl = enc.createLabel();
            size_t doneLbl  = enc.createLabel();
            enc.cbz(arm64::X0, falseLbl);
            enc.adr_data(arm64::X0, runtime.offStrTrue);
            enc.bl(runtime.lblPrintStr);
            enc.b(doneLbl);
            enc.bindLabel(falseLbl);
            enc.adr_data(arm64::X0, runtime.offStrFalse);
            enc.bl(runtime.lblPrintStr);
            enc.bindLabel(doneLbl);
        } else if (t == ThavaType::FLOAT || t == ThavaType::DOUBLE) {
            enc.sub_rri(arm64::SP, arm64::SP, 16);
            enc.str_imm(arm64::X0, arm64::SP, 0);
            enc.fldr_imm(arm64::D0, arm64::SP, 0);
            enc.add_rri(arm64::SP, arm64::SP, 16);
            enc.bl(runtime.lblPrintDouble);
        } else if (t == ThavaType::DICT) {
            emitPrintDict();
        } else if (t == ThavaType::LIST || t == ThavaType::TUPLE) {
            emitPrintList(lastCollectionElemType, false);
        } else {
            enc.bl(runtime.lblPrintInt);
        }
    }

    // ==========================================
    // Print a dict as {k: v, ...} + newline. Expects the dict pointer in X0.
    // ==========================================
    void emitPrintDict() {
        enc.sub_rri(arm64::SP, arm64::SP, 48);
        enc.str_imm(arm64::X0, arm64::SP, 8);
        enc.movz(arm64::X0, 0, 0);
        enc.str_imm(arm64::X0, arm64::SP, 0);

        uint32_t offOpen = emitter.addStringLiteralOffset("{");
        uint32_t offColon = emitter.addStringLiteralOffset(": ");
        uint32_t offComma = emitter.addStringLiteralOffset(", ");
        uint32_t offClose = emitter.addStringLiteralOffset("}\n");
        uint32_t offQuote = emitter.addStringLiteralOffset("\"");

        enc.adr_data(arm64::X0, offOpen);
        enc.bl(runtime.lblPrintStr);

        size_t loopLbl = enc.createLabel();
        size_t doneLbl = enc.createLabel();
        size_t noComma = enc.createLabel();
        size_t strKeyLbl = enc.createLabel();
        size_t keyDone = enc.createLabel();
        size_t strValLbl = enc.createLabel();
        size_t dblValLbl = enc.createLabel();
        size_t valDone = enc.createLabel();

        enc.bindLabel(loopLbl);
        enc.ldr_imm(arm64::X0, arm64::SP, 0);
        enc.ldr_imm(arm64::X1, arm64::SP, 8);
        enc.ldr_imm(arm64::X2, arm64::X1, 0);
        enc.cmp_rr(arm64::X0, arm64::X2);
        enc.b_cond(arm64::GE, doneLbl);
        enc.cbz(arm64::X0, noComma);
        enc.adr_data(arm64::X0, offComma);
        enc.bl(runtime.lblPrintStr);

        enc.bindLabel(noComma);
        enc.ldr_imm(arm64::X0, arm64::SP, 0);
        enc.movz(arm64::X2, 32, 0);
        enc.mul_rrr(arm64::X3, arm64::X0, arm64::X2);
        enc.ldr_imm(arm64::X4, arm64::SP, 8);
        enc.add_rrr(arm64::X4, arm64::X4, arm64::X3);
        enc.add_rri(arm64::X4, arm64::X4, 16);
        enc.str_imm(arm64::X4, arm64::SP, 16);

        enc.ldr_imm(arm64::X5, arm64::X4, 16);
        enc.mov_ri64(arm64::X6, (int64_t)ThavaType::STRING);
        enc.cmp_rr(arm64::X5, arm64::X6);
        enc.b_cond(arm64::EQ, strKeyLbl);
        enc.ldr_imm(arm64::X0, arm64::X4, 0);
        enc.bl(runtime.lblPrintInt);
        enc.b(keyDone);

        enc.bindLabel(strKeyLbl);
        enc.adr_data(arm64::X0, offQuote);
        enc.bl(runtime.lblPrintStr);
        enc.ldr_imm(arm64::X0, arm64::SP, 16);
        enc.ldr_imm(arm64::X0, arm64::X0, 0);
        enc.bl(runtime.lblPrintStr);
        enc.adr_data(arm64::X0, offQuote);
        enc.bl(runtime.lblPrintStr);

        enc.bindLabel(keyDone);
        enc.adr_data(arm64::X0, offColon);
        enc.bl(runtime.lblPrintStr);

        enc.ldr_imm(arm64::X4, arm64::SP, 16);
        enc.ldr_imm(arm64::X5, arm64::X4, 24);
        enc.mov_ri64(arm64::X6, (int64_t)ThavaType::STRING);
        enc.cmp_rr(arm64::X5, arm64::X6);
        enc.b_cond(arm64::EQ, strValLbl);
        enc.mov_ri64(arm64::X6, (int64_t)ThavaType::DOUBLE);
        enc.cmp_rr(arm64::X5, arm64::X6);
        enc.b_cond(arm64::EQ, dblValLbl);
        enc.mov_ri64(arm64::X6, (int64_t)ThavaType::FLOAT);
        enc.cmp_rr(arm64::X5, arm64::X6);
        enc.b_cond(arm64::EQ, dblValLbl);
        enc.ldr_imm(arm64::X0, arm64::X4, 8);
        enc.bl(runtime.lblPrintInt);
        enc.b(valDone);

        enc.bindLabel(strValLbl);
        enc.ldr_imm(arm64::X0, arm64::X4, 8);
        enc.bl(runtime.lblPrintStr);
        enc.b(valDone);

        enc.bindLabel(dblValLbl);
        enc.ldr_imm(arm64::X0, arm64::X4, 8);
        enc.sub_rri(arm64::SP, arm64::SP, 16);
        enc.str_imm(arm64::X0, arm64::SP, 0);
        enc.fldr_imm(arm64::D0, arm64::SP, 0);
        enc.add_rri(arm64::SP, arm64::SP, 16);
        enc.bl(runtime.lblPrintDouble);

        enc.bindLabel(valDone);
        enc.ldr_imm(arm64::X0, arm64::SP, 0);
        enc.add_rri(arm64::X0, arm64::X0, 1);
        enc.str_imm(arm64::X0, arm64::SP, 0);
        enc.b(loopLbl);

        enc.bindLabel(doneLbl);
        enc.adr_data(arm64::X0, offClose);
        enc.bl(runtime.lblPrintStr);
        enc.add_rri(arm64::SP, arm64::SP, 48);
    }

    // ==========================================
    // Print a list as [a, b, c] on stdout. Expects the list pointer in X0.
    // ==========================================
    void emitPrintList(ThavaType elemType, bool withNewline = true) {
        // [SP+0] = index, [SP+16] = list pointer
        enc.sub_rri(arm64::SP, arm64::SP, 32);
        enc.str_imm(arm64::X0, arm64::SP, 16);
        enc.movz(arm64::X0, 0, 0);
        enc.str_imm(arm64::X0, arm64::SP, 0);

        enc.adr_data(arm64::X0, runtime.offStrListOpen);
        enc.bl(runtime.lblPrintStr);

        size_t loopLbl = enc.createLabel();
        size_t doneLbl = enc.createLabel();
        size_t noCommaLbl = enc.createLabel();

        enc.bindLabel(loopLbl);
        enc.ldr_imm(arm64::X0, arm64::SP, 0);
        enc.ldr_imm(arm64::X1, arm64::SP, 16);
        enc.ldr_imm(arm64::X2, arm64::X1, 0);
        enc.cmp_rr(arm64::X0, arm64::X2);
        enc.b_cond(arm64::GE, doneLbl);

        enc.ldr_imm(arm64::X0, arm64::SP, 0);
        enc.cbz(arm64::X0, noCommaLbl);
        enc.adr_data(arm64::X0, runtime.offStrListComma);
        enc.bl(runtime.lblPrintStr);
        enc.bindLabel(noCommaLbl);

        enc.ldr_imm(arm64::X0, arm64::SP, 0);
        enc.movz(arm64::X3, 8, 0);
        enc.mul_rrr(arm64::X0, arm64::X0, arm64::X3);
        enc.ldr_imm(arm64::X1, arm64::SP, 16);
        enc.add_rrr(arm64::X1, arm64::X1, arm64::X0);

        if (elemType == ThavaType::FLOAT || elemType == ThavaType::DOUBLE) {
            enc.fldr_imm(arm64::D0, arm64::X1, 8);
            enc.bl(runtime.lblPrintDouble);
        } else if (elemType == ThavaType::STRING) {
            enc.ldr_imm(arm64::X0, arm64::X1, 8);
            enc.bl(runtime.lblPrintStr);
        } else {
            enc.ldr_imm(arm64::X0, arm64::X1, 8);
            enc.bl(runtime.lblPrintInt);
        }

        enc.ldr_imm(arm64::X0, arm64::SP, 0);
        enc.add_rri(arm64::X0, arm64::X0, 1);
        enc.str_imm(arm64::X0, arm64::SP, 0);
        enc.b(loopLbl);

        enc.bindLabel(doneLbl);
        if (withNewline) {
            enc.adr_data(arm64::X0, runtime.offStrListClose);
        } else {
            uint32_t offClose = emitter.addStringLiteralOffset("]");
            enc.adr_data(arm64::X0, offClose);
        }
        enc.bl(runtime.lblPrintStr);
        enc.add_rri(arm64::SP, arm64::SP, 32);
    }

    // ==========================================
    // STATEMENT CODE GENERATION
    // ==========================================
    void compileStatement(ASTNode* node) {
        diag::Scope location(node ? node->span : SourceSpan{});
        if (!node) return;
        checkUnsupported(node);
        checkRuntimeSupport(node);

        // 0a. throw / raise_error
        if (auto th = dynamic_cast<ThrowNode*>(node)) {
            emitThrowCall(th->expr.get(), nullptr, "Runtime Exception occurred!");
            return;
        }
        if (auto re = dynamic_cast<RaiseErrorNode*>(node)) {
            if (re->isCompileTime) return;
            emitThrowCall(re->msgExpr.get(), re->typeExpr.get(), "Runtime Error!");
            return;
        }

        // 0b. try / except
        if (auto tn = dynamic_cast<TryExceptNode*>(node)) {
            int32_t rec = currentFrameOffset - 48;
            currentFrameOffset = rec;

            size_t resumeLbl = enc.createLabel();
            size_t afterLbl = enc.createLabel();

            // Build the handler record: [msg][type][prev][resume][saved FP][saved SP]
            enc.mov_rr(arm64::X9, arm64::X28);                 // previous handler
            enc.sub_rri(arm64::X28, arm64::FP, -rec);          // &record
            enc.str_imm(arm64::X9, arm64::X28, 16);
            enc.movz(arm64::X9, 0, 0);
            enc.str_imm(arm64::X9, arm64::X28, 0);
            enc.str_imm(arm64::X9, arm64::X28, 8);
            enc.adr(arm64::X9, resumeLbl);
            enc.str_imm(arm64::X9, arm64::X28, 24);
            enc.mov_rr(arm64::X9, arm64::FP);
            enc.str_imm(arm64::X9, arm64::X28, 32);
            enc.mov_rr(arm64::X9, arm64::SP);
            enc.str_imm(arm64::X9, arm64::X28, 40);

            enterScope();
            for (const auto& s : tn->tryBody) compileStatement(s.get());
            exitScope();

            enc.ldr_imm(arm64::X28, arm64::X28, 16); // pop handler
            enc.b(afterLbl);

            enc.bindLabel(resumeLbl); // arrived here from throw (X28 already popped)

            for (const auto& clause : tn->exceptClauses) {
                bool isCatchAll = clause.exceptionType.empty() || clause.exceptionType == "Exception" ||
                                  clause.exceptionType == "RuntimeError";
                size_t nextClause = 0;
                if (!isCatchAll) {
                    nextClause = enc.createLabel();
                    enc.sub_rri(arm64::X9, arm64::FP, -rec);
                    enc.ldr_imm(arm64::X0, arm64::X9, 8);
                    enc.adr_data(arm64::X1, emitter.addStringLiteralOffset(clause.exceptionType));
                    enc.bl(runtime.lblStrCmpEq);
                    enc.cbz(arm64::X0, nextClause);
                }

                enterScope();
                if (!clause.varName.empty()) {
                    int32_t varOff = declareVar(clause.varName, ThavaType::STRING);
                    enc.sub_rri(arm64::X9, arm64::FP, -rec);
                    enc.ldr_imm(arm64::X0, arm64::X9, 0);
                    enc.str_imm(arm64::X0, arm64::FP, varOff);
                }
                for (const auto& s : clause.body) compileStatement(s.get());
                exitScope();
                enc.b(afterLbl);

                if (!isCatchAll) enc.bindLabel(nextClause);
                else break;
            }

            // No clause matched: keep unwinding with the same payload.
            enc.sub_rri(arm64::X9, arm64::FP, -rec);
            enc.ldr_imm(arm64::X0, arm64::X9, 0);
            enc.ldr_imm(arm64::X1, arm64::X9, 8);
            enc.bl(lblThrow);

            enc.bindLabel(afterLbl);
            return;
        }

        // 0. MemberAssignNode (p.x = 5, p.y += 2)
        if (auto ma = dynamic_cast<MemberAssignNode*>(node)) {
            std::string structName = resolveStructName(ma->target.get());
            auto sit = g_structs.find(structName);
            if (sit == g_structs.end()) {
                std::cerr << "[ARM64 Codegen Error]: Struct '" << structName
                          << "' not found for assignment '" << ma->memberName << "'!\n";
                exit(1);
            }
            const auto& sinfo = sit->second;
            auto fit = sinfo.fieldIndex.find(ma->memberName);
            if (fit == sinfo.fieldIndex.end()) {
                std::cerr << "[ARM64 Codegen Error]: Struct '" << structName
                          << "' has no member '" << ma->memberName << "'!\n";
                exit(1);
            }
            const auto& fld = sinfo.fields[fit->second];

            compileExpression(ma->target.get());
            enc.sub_rri(arm64::SP, arm64::SP, 16);
            enc.str_imm(arm64::X0, arm64::SP, 0);
            ThavaType et = compileExpression(ma->expr.get());
            enc.ldr_imm(arm64::X6, arm64::SP, 0);
            enc.add_rri(arm64::SP, arm64::SP, 16);

            if (fld.type == ThavaType::DOUBLE || fld.type == ThavaType::FLOAT) {
                if (et == ThavaType::INT) intToDoubleBits();
                enc.fmov_dx(arm64::D0, arm64::X0);
                if (fld.type == ThavaType::FLOAT) enc.fcvt_ds(arm64::D0, arm64::D0);
                if (ma->op != "=") {
                    if (fld.type == ThavaType::FLOAT) {
                        enc.fldr_s_imm(arm64::D1, arm64::X6, fld.offset);
                        enc.fcvt_sd(arm64::D1, arm64::D1);
                    } else {
                        enc.fldr_imm(arm64::D1, arm64::X6, fld.offset);
                    }
                    if (ma->op == "+=") enc.fadd_ddd(arm64::D0, arm64::D1, arm64::D0);
                    else if (ma->op == "-=") enc.fsub_ddd(arm64::D0, arm64::D1, arm64::D0);
                    else if (ma->op == "*=") enc.fmul_ddd(arm64::D0, arm64::D1, arm64::D0);
                    else if (ma->op == "/=") enc.fdiv_ddd(arm64::D0, arm64::D1, arm64::D0);
                }
                if (fld.type == ThavaType::FLOAT) {
                    enc.fcvt_ds(arm64::D0, arm64::D0);
                    enc.fstr_s_imm(arm64::D0, arm64::X6, fld.offset);
                } else {
                    enc.fmov_xd(arm64::X0, arm64::D0);
                    enc.str_imm(arm64::X0, arm64::X6, fld.offset);
                }
            } else {
                if (ma->op == "=") {
                    if (fld.type == ThavaType::INT) narrowIntX0(fld.customType);
                    enc.str_imm(arm64::X0, arm64::X6, fld.offset);
                } else {
                    enc.ldr_imm(arm64::X7, arm64::X6, fld.offset);
                    if (ma->op == "+=") enc.add_rrr(arm64::X7, arm64::X7, arm64::X0);
                    else if (ma->op == "-=") enc.sub_rrr(arm64::X7, arm64::X7, arm64::X0);
                    else if (ma->op == "*=") enc.mul_rrr(arm64::X7, arm64::X7, arm64::X0);
                    if (fld.type == ThavaType::INT) {
                        enc.mov_rr(arm64::X0, arm64::X7);
                        narrowIntX0(fld.customType);
                        enc.mov_rr(arm64::X7, arm64::X0);
                    }
                    enc.str_imm(arm64::X7, arm64::X6, fld.offset);
                }
            }
            return;
        }

        // 1. VarDeclNode (<int> x = 10)
        if (auto vdecl = dynamic_cast<VarDeclNode*>(node)) {
            ThavaType dt = vdecl->varType;
            ThavaType t = dt;
            if (vdecl->initExpr) {
                t = compileExpression(vdecl->initExpr.get(), dt);
                if ((dt == ThavaType::DOUBLE || dt == ThavaType::FLOAT) && t == ThavaType::INT) {
                    intToDoubleBits();
                    t = ThavaType::DOUBLE;
                } else if (dt == ThavaType::INT &&
                           (t == ThavaType::DOUBLE || t == ThavaType::FLOAT)) {
                    doubleBitsToInt();
                    t = ThavaType::INT;
                }
            } else if (dt == ThavaType::CUSTOM && g_structs.count(vdecl->customTypeName)) {
                // `<Point> p` with no initializer: zeroed heap block
                const auto& sinfo = g_structs[vdecl->customTypeName];
                enc.mov_ri64(arm64::X0, sinfo.totalSize > 0 ? sinfo.totalSize : 8);
                enc.bl(runtime.lblMalloc);
            } else {
                enc.movz(arm64::X0, 0, 0);
            }
            int32_t off;
            if (emittingProgramBody && globalVarSlots.count(vdecl->varName)) {
                // Global: its slot was reserved in the program scope. A local
                // declaration with the same name inside a function shadows it.
                off = globalVarSlots[vdecl->varName];
            } else {
                off = declareVar(vdecl->varName, dt, vdecl->customTypeName);
            }
            if (dt == ThavaType::INT) narrowIntX0(vdecl->customTypeName);
            enc.str_imm(arm64::X0, arm64::FP, off);

            if (dt == ThavaType::LIST || dt == ThavaType::TUPLE) {
                if (dynamic_cast<CollectionNode*>(vdecl->initExpr.get())) {
                    listElemTypes[vdecl->varName] = lastCollectionElemType;
                } else if (auto sp = dynamic_cast<StrAdvMethodNode*>(vdecl->initExpr.get())) {
                    if (sp->method == "split") listElemTypes[vdecl->varName] = ThavaType::STRING;
                } else if (dynamic_cast<FileMethodNode*>(vdecl->initExpr.get()) || isListDir(vdecl->initExpr.get())) {
                    listElemTypes[vdecl->varName] = ThavaType::STRING;
                } else if (listElemTypes.find(vdecl->varName) == listElemTypes.end()) {
                    listElemTypes[vdecl->varName] = ThavaType::INT;
                }
            }
            return;
        }

        // 2. AssignNode (x = 20, x += 5, etc.)
        if (auto assign = dynamic_cast<AssignNode*>(node)) {
            VarInfo* v = lookupVar(assign->name);
            if (!v && isGlobalName(assign->name)) {
                ThavaType gt = globalEnv->get(assign->name);
                ThavaType t = compileExpression(assign->expr.get(), gt);
                if ((gt == ThavaType::DOUBLE || gt == ThavaType::FLOAT) && (t == ThavaType::INT || t == ThavaType::BOOL))
                    intToDoubleBits();
                if (gt == ThavaType::INT) {
                    if (t == ThavaType::DOUBLE || t == ThavaType::FLOAT) doubleBitsToInt();
                    narrowIntX0(globalEnv->getCustomType(assign->name));
                }
                if (isListDir(assign->expr.get())) listElemTypes[assign->name] = ThavaType::STRING;
                emitGlobalBase(arm64::X9);
                enc.str_imm(arm64::X0, arm64::X9, globalVarSlots.at(assign->name));
                return;
            }
            if (!v) {
                std::cerr << "[ARM64 Codegen Error]: Assigning to undeclared variable '" << assign->name << "'\n";
                return;
            }
            ThavaType vt = compileExpression(assign->expr.get(), v->type);
            if (dynamic_cast<CollectionNode*>(assign->expr.get())) {
                listElemTypes[assign->name] = lastCollectionElemType;
            } else if (dynamic_cast<FileMethodNode*>(assign->expr.get()) || isListDir(assign->expr.get())) {
                listElemTypes[assign->name] = ThavaType::STRING;
            }

            // Double variables: operate on the raw bit patterns
            if ((v->type == ThavaType::DOUBLE || v->type == ThavaType::FLOAT) &&
                (assign->op == "=" || assign->op == "+=" || assign->op == "-=" ||
                 assign->op == "*=" || assign->op == "/=")) {
                if (vt == ThavaType::INT) intToDoubleBits();
                if (assign->op == "=") {
                    enc.str_imm(arm64::X0, arm64::FP, v->offset);
                } else {
                    enc.sub_rri(arm64::SP, arm64::SP, 16);
                    enc.str_imm(arm64::X0, arm64::SP, 0);
                    enc.fldr_imm(arm64::D1, arm64::SP, 0); // rhs
                    enc.add_rri(arm64::SP, arm64::SP, 16);

                    enc.ldr_imm(arm64::X1, arm64::FP, v->offset);
                    enc.sub_rri(arm64::SP, arm64::SP, 16);
                    enc.str_imm(arm64::X1, arm64::SP, 0);
                    enc.fldr_imm(arm64::D0, arm64::SP, 0); // old value
                    enc.add_rri(arm64::SP, arm64::SP, 16);

                    if (assign->op == "+=") enc.fadd_ddd(arm64::D0, arm64::D0, arm64::D1);
                    else if (assign->op == "-=") enc.fsub_ddd(arm64::D0, arm64::D0, arm64::D1);
                    else if (assign->op == "*=") enc.fmul_ddd(arm64::D0, arm64::D0, arm64::D1);
                    else enc.fdiv_ddd(arm64::D0, arm64::D0, arm64::D1);
                    d0BitsToX0();
                    enc.str_imm(arm64::X0, arm64::FP, v->offset);
                }
                return;
            }
            if (v->type == ThavaType::INT && assign->op == "=" &&
                (vt == ThavaType::DOUBLE || vt == ThavaType::FLOAT)) {
                doubleBitsToInt();
            }
            if (v->type == ThavaType::STRING && assign->op != "=") {
                // `s += x` concatenates and `s *= n` repeats; the integer path would
                // add the pointers and leave a dangling string behind.
                if (assign->op == "+=") {
                    enc.mov_rr(arm64::X1, arm64::X0);              // s2
                    enc.ldr_imm(arm64::X0, arm64::FP, v->offset);   // s1
                    enc.bl(runtime.lblStrConcat);
                } else if (assign->op == "*=") {
                    enc.mov_rr(arm64::X1, arm64::X0);              // count
                    enc.ldr_imm(arm64::X0, arm64::FP, v->offset);   // s
                    enc.bl(runtime.lblStrMul);
                } else {
                    std::cerr << "[ARM64 Codegen Error]: operator '" << assign->op
                              << "' is not defined on <str> variable '" << assign->name << "'.\n";
                    exit(1);
                }
                storeIntVar(v->offset, v);
                return;
            }
            if (assign->op == "=") {
                storeIntVar(v->offset, v);
            } else if (assign->op == "+=") {
                enc.ldr_imm(arm64::X1, arm64::FP, v->offset);
                enc.add_rrr(arm64::X0, arm64::X1, arm64::X0);
                storeIntVar(v->offset, v);
            } else if (assign->op == "-=") {
                enc.ldr_imm(arm64::X1, arm64::FP, v->offset);
                enc.sub_rrr(arm64::X0, arm64::X1, arm64::X0);
                storeIntVar(v->offset, v);
            } else if (assign->op == "*=") {
                enc.ldr_imm(arm64::X1, arm64::FP, v->offset);
                enc.mul_rrr(arm64::X0, arm64::X1, arm64::X0);
                storeIntVar(v->offset, v);
            } else if (assign->op == "/=") {
                enc.ldr_imm(arm64::X1, arm64::FP, v->offset);
                enc.sdiv_rrr(arm64::X0, arm64::X1, arm64::X0);
                storeIntVar(v->offset, v);
            } else if (assign->op == "%=") {
                enc.ldr_imm(arm64::X1, arm64::FP, v->offset);
                enc.sdiv_rrr(arm64::X2, arm64::X1, arm64::X0);
                enc.mul_rrr(arm64::X2, arm64::X2, arm64::X0);
                enc.sub_rrr(arm64::X0, arm64::X1, arm64::X2);
                storeIntVar(v->offset, v);
            } else if (assign->op == "**=") {
                enc.ldr_imm(arm64::X1, arm64::FP, v->offset);
                emitIntegerPower();
                storeIntVar(v->offset, v);
            } else {
                storeIntVar(v->offset, v);
            }
            return;
        }

        // List element assignment: lst[i] = v, +=, -=, *=, /=
        if (auto ia = dynamic_cast<IndexAssignNode*>(node)) {
            VarInfo* vi = lookupVar(ia->varName);
            if (!vi && !isGlobalName(ia->varName)) {
                std::cerr << "[ARM64 Codegen Error]: Assigning to undeclared list '"
                          << ia->varName << "'\n";
                return;
            }
            if ((vi ? vi->type : globalEnv->get(ia->varName)) == ThavaType::MULTYPE) {
                mutateMultype(ia->varName, {native_multype::assignmentMutation(ia->op),
                                            ia->indexExpr.get(), ia->valExpr.get()});
                return;
            }
            if ((vi ? vi->type : globalEnv->get(ia->varName)) == ThavaType::DICT) {
                ThavaType kt = compileExpression(ia->indexExpr.get());
                enc.sub_rri(arm64::SP, arm64::SP, 48);
                enc.str_imm(arm64::X0, arm64::SP, 0); // key
                ThavaType vt = compileExpression(ia->valExpr.get());
                enc.str_imm(arm64::X0, arm64::SP, 8); // value

                loadCollectionPtr(ia->varName, vi, arm64::X0);
                enc.str_imm(arm64::X0, arm64::SP, 16); // dict
                enc.ldr_imm(arm64::X1, arm64::SP, 0);
                enc.movz(arm64::X2, kt == ThavaType::STRING ? 1 : 0, 0);
                enc.bl(runtime.lblDictFind);

                size_t foundLbl = enc.createLabel();
                size_t appendLbl = enc.createLabel();
                size_t endLbl = enc.createLabel();
                enc.cmp_ri(arm64::X0, 0);
                enc.b_cond(arm64::LT, appendLbl);

                enc.bindLabel(foundLbl);
                enc.movz(arm64::X2, 32, 0);
                enc.mul_rrr(arm64::X3, arm64::X0, arm64::X2);
                enc.ldr_imm(arm64::X4, arm64::SP, 16);
                enc.add_rrr(arm64::X4, arm64::X4, arm64::X3);
                if (ia->op == "+=") {
                    enc.ldr_imm(arm64::X5, arm64::X4, 24);
                    enc.ldr_imm(arm64::X6, arm64::SP, 8);
                    enc.add_rrr(arm64::X5, arm64::X5, arm64::X6);
                    enc.str_imm(arm64::X5, arm64::X4, 24);
                } else if (ia->op == "-=") {
                    enc.ldr_imm(arm64::X5, arm64::X4, 24);
                    enc.ldr_imm(arm64::X6, arm64::SP, 8);
                    enc.sub_rrr(arm64::X5, arm64::X5, arm64::X6);
                    enc.str_imm(arm64::X5, arm64::X4, 24);
                } else {
                    enc.ldr_imm(arm64::X5, arm64::SP, 8);
                    enc.str_imm(arm64::X5, arm64::X4, 24);
                }
                enc.b(endLbl);

                enc.bindLabel(appendLbl);
                enc.ldr_imm(arm64::X4, arm64::SP, 16);
                enc.ldr_imm(arm64::X5, arm64::X4, 0); // count
                enc.movz(arm64::X6, 32, 0);
                enc.mul_rrr(arm64::X5, arm64::X5, arm64::X6);
                enc.add_rrr(arm64::X4, arm64::X4, arm64::X5);
                enc.ldr_imm(arm64::X5, arm64::SP, 0);
                enc.str_imm(arm64::X5, arm64::X4, 16); // key
                enc.ldr_imm(arm64::X6, arm64::SP, 8);
                enc.str_imm(arm64::X6, arm64::X4, 24); // value
                enc.mov_ri64(arm64::X5, (int64_t)kt);
                enc.str_imm(arm64::X5, arm64::X4, 32); // keyType
                enc.mov_ri64(arm64::X5, (int64_t)vt);
                enc.str_imm(arm64::X5, arm64::X4, 40); // valType
                enc.ldr_imm(arm64::X4, arm64::SP, 16);
                enc.ldr_imm(arm64::X5, arm64::X4, 0);
                enc.add_rri(arm64::X5, arm64::X5, 1);
                enc.str_imm(arm64::X5, arm64::X4, 0); // count++

                enc.bindLabel(endLbl);
                enc.add_rri(arm64::SP, arm64::SP, 48);
                return;
            }

            ThavaType elemT = ThavaType::INT;
            auto eit = listElemTypes.find(ia->varName);
            if (eit != listElemTypes.end()) elemT = eit->second;

            compileExpression(ia->indexExpr.get());
            enc.sub_rri(arm64::SP, arm64::SP, 16);
            enc.str_imm(arm64::X0, arm64::SP, 0); // [SP+0] = index

            ThavaType vt = compileExpression(ia->valExpr.get());
            if (elemT == ThavaType::INT && (vt == ThavaType::DOUBLE || vt == ThavaType::FLOAT)) {
                elemT = ThavaType::DOUBLE;
                listElemTypes[ia->varName] = ThavaType::DOUBLE;
            }

            // Element address -> X1
            loadCollectionPtr(ia->varName, vi, arm64::X1);
            enc.ldr_imm(arm64::X2, arm64::SP, 0);
            enc.add_rri(arm64::SP, arm64::SP, 16);
            enc.movz(arm64::X3, 8, 0);
            enc.mul_rrr(arm64::X2, arm64::X2, arm64::X3);
            enc.add_rrr(arm64::X1, arm64::X1, arm64::X2);

            // [SP+0] = address, [SP+8] = value
            enc.sub_rri(arm64::SP, arm64::SP, 32);
            enc.str_imm(arm64::X1, arm64::SP, 0);
            enc.str_imm(arm64::X0, arm64::SP, 8);

            if (elemT == ThavaType::DOUBLE || elemT == ThavaType::FLOAT) {
                if (vt == ThavaType::INT) {
                    enc.ldr_imm(arm64::X0, arm64::SP, 8);
                    intToDoubleBits();
                    enc.str_imm(arm64::X0, arm64::SP, 8);
                }
                if (ia->op == "=") {
                    enc.ldr_imm(arm64::X1, arm64::SP, 0);
                    enc.ldr_imm(arm64::X0, arm64::SP, 8);
                    enc.str_imm(arm64::X0, arm64::X1, 8);
                } else {
                    enc.ldr_imm(arm64::X1, arm64::SP, 0);
                    enc.fldr_imm(arm64::D0, arm64::X1, 8);
                    enc.fldr_imm(arm64::D1, arm64::SP, 8);
                    if (ia->op == "+=") enc.fadd_ddd(arm64::D0, arm64::D0, arm64::D1);
                    else if (ia->op == "-=") enc.fsub_ddd(arm64::D0, arm64::D0, arm64::D1);
                    else if (ia->op == "*=") enc.fmul_ddd(arm64::D0, arm64::D0, arm64::D1);
                    else enc.fdiv_ddd(arm64::D0, arm64::D0, arm64::D1);
                    d0BitsToX0();
                    enc.ldr_imm(arm64::X1, arm64::SP, 0);
                    enc.str_imm(arm64::X0, arm64::X1, 8);
                }
            } else {
                if (ia->op == "=") {
                    enc.ldr_imm(arm64::X1, arm64::SP, 0);
                    enc.ldr_imm(arm64::X0, arm64::SP, 8);
                    enc.str_imm(arm64::X0, arm64::X1, 8);
                } else {
                    enc.ldr_imm(arm64::X1, arm64::SP, 0);
                    enc.ldr_imm(arm64::X0, arm64::X1, 8); // old
                    enc.ldr_imm(arm64::X2, arm64::SP, 8); // rhs
                    if (ia->op == "+=") enc.add_rrr(arm64::X0, arm64::X0, arm64::X2);
                    else if (ia->op == "-=") enc.sub_rrr(arm64::X0, arm64::X0, arm64::X2);
                    else if (ia->op == "*=") enc.mul_rrr(arm64::X0, arm64::X0, arm64::X2);
                    else if (ia->op == "/=") enc.sdiv_rrr(arm64::X0, arm64::X0, arm64::X2);
                    enc.str_imm(arm64::X0, arm64::X1, 8);
                }
            }
            enc.add_rri(arm64::SP, arm64::SP, 32);
            return;
        }

        // 2b. ConsClearNode (cons.clear())
        if (dynamic_cast<ConsClearNode*>(node)) {
            uint32_t offClear = emitter.addStringLiteralOffset("\x1b[2J\x1b[H");
            enc.adr_data(arm64::X0, offClear);
            enc.bl(runtime.lblPrintStr);
            return;
        }

        // 3. ConsOutNode (cons.out(expr))
        if (auto cout = dynamic_cast<ConsOutNode*>(node)) {
            if (auto interpolation = dynamic_cast<InterpolatedStringNode*>(cout->getExpr())) {
                for (const auto& part : interpolation->parts) emitPrintValueRaw(compileExpression(part.get()));
                enc.bl(runtime.lblPrintNL);
                return;
            }

            ThavaType t = compileExpression(cout->getExpr());
            if (t == ThavaType::BOXED || t == ThavaType::MULTYPE) {
                if (t == ThavaType::BOXED) emitBoxedString();
                else emitMultypeString();
                enc.bl(runtime.lblPrintStrNL);
            } else if (t == ThavaType::STRING || t == ThavaType::DECIMAL) {
                enc.bl(runtime.lblPrintStrNL);
            } else if (t == ThavaType::BOOL) {
                enc.bl(runtime.lblPrintBoolNL);
            } else if (t == ThavaType::FLOAT || t == ThavaType::DOUBLE) {
                // Doubles live as raw 64-bit patterns in X0; move them into D0 for printing
                enc.sub_rri(arm64::SP, arm64::SP, 16);
                enc.str_imm(arm64::X0, arm64::SP, 0);
                enc.fldr_imm(arm64::D0, arm64::SP, 0);
                enc.add_rri(arm64::SP, arm64::SP, 16);
                enc.bl(runtime.lblPrintDoubleNL);
            } else if (t == ThavaType::DICT) {
                emitPrintDict();
            } else if (t == ThavaType::LIST || t == ThavaType::TUPLE) {
                ThavaType elemT = ThavaType::INT;
                if (auto pv = dynamic_cast<VarNode*>(cout->getExpr())) {
                    auto it = listElemTypes.find(pv->getName());
                    if (it != listElemTypes.end()) elemT = it->second;
                } else {
                    elemT = lastCollectionElemType;
                }
                emitPrintList(elemT);
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

            compileExpression(ifn->cond.get(), ThavaType::BOOL);
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
            compileExpression(wh->cond.get(), ThavaType::BOOL);
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

        // C-style for loop: for (init; cond; update) { ... }
        if (auto forNode = dynamic_cast<ForNode*>(node)) {
            enterScope();
            if (forNode->init) compileStatement(forNode->init.get());

            size_t startLbl = enc.createLabel();
            size_t stepLbl  = enc.createLabel();
            size_t endLbl   = enc.createLabel();

            breakLabels.push_back(endLbl);
            continueLabels.push_back(stepLbl);

            enc.bindLabel(startLbl);
            if (forNode->cond) {
                compileExpression(forNode->cond.get(), ThavaType::BOOL);
                enc.cbz(arm64::X0, endLbl);
            }

            compileStatement(forNode->body.get());

            enc.bindLabel(stepLbl);
            if (forNode->update) compileStatement(forNode->update.get());
            enc.b(startLbl);

            enc.bindLabel(endLbl);
            breakLabels.pop_back();
            continueLabels.pop_back();
            exitScope();
            return;
        }

        // For-each loop: for <type> x in collection { ... }
        if (auto fe = dynamic_cast<ForEachNode*>(node)) {
            enterScope();
            ThavaType collT = compileExpression(fe->collectionExpr.get());
            int32_t collOff = declareVar("_tcoll_" + fe->varName, ThavaType::INT);
            enc.str_imm(arm64::X0, arm64::FP, collOff);

            int32_t lenOff = declareVar("_tlen_" + fe->varName, ThavaType::INT);
            if (collT == ThavaType::STRING) {
                enc.bl(runtime.lblStrLen);
            } else {
                enc.ldr_imm(arm64::X0, arm64::X0, 0);
            }
            enc.str_imm(arm64::X0, arm64::FP, lenOff);

            int32_t idxOff = declareVar("_tidx_" + fe->varName, ThavaType::INT);
            enc.movz(arm64::X0, 0, 0);
            enc.str_imm(arm64::X0, arm64::FP, idxOff);

            ThavaType itemType = fe->varType;
            if (auto v = dynamic_cast<VarNode*>(fe->collectionExpr.get())) {
                auto it = listElemTypes.find(v->getName());
                if (it != listElemTypes.end()) itemType = it->second;
            }
            if (collT == ThavaType::STRING) itemType = ThavaType::STRING;
            if (itemType == ThavaType::UNKNOWN) itemType = ThavaType::INT;
            int32_t varSlot = declareVar(fe->varName, itemType);
            int32_t scrOff = declareVar("_tscr_" + fe->varName, ThavaType::INT);

            size_t startLbl = enc.createLabel();
            size_t stepLbl  = enc.createLabel();
            size_t endLbl   = enc.createLabel();
            breakLabels.push_back(endLbl);
            continueLabels.push_back(stepLbl);

            enc.bindLabel(startLbl);
            enc.ldr_imm(arm64::X0, arm64::FP, idxOff);
            enc.ldr_imm(arm64::X1, arm64::FP, lenOff);
            enc.cmp_rr(arm64::X0, arm64::X1);
            enc.b_cond(arm64::GE, endLbl);

            if (collT == ThavaType::STRING) {
                enc.ldr_imm(arm64::X1, arm64::FP, collOff);
                enc.add_rrr(arm64::X1, arm64::X1, arm64::X0);
                enc.ldrb_imm(arm64::X2, arm64::X1, 0);
                enc.str_imm(arm64::X2, arm64::FP, scrOff);
                enc.movz(arm64::X0, 2, 0);
                enc.bl(runtime.lblMalloc);
                enc.ldr_imm(arm64::X2, arm64::FP, scrOff);
                enc.strb_imm(arm64::X2, arm64::X0, 0);
                enc.movz(arm64::X3, 0, 0);
                enc.strb_imm(arm64::X3, arm64::X0, 1);
            } else if (collT == ThavaType::DICT) {
                // Iterating a dict yields its keys (entry key lives at 16 + i*32)
                enc.movz(arm64::X2, 32, 0);
                enc.mul_rrr(arm64::X0, arm64::X0, arm64::X2);
                enc.ldr_imm(arm64::X1, arm64::FP, collOff);
                enc.add_rrr(arm64::X1, arm64::X1, arm64::X0);
                enc.ldr_imm(arm64::X0, arm64::X1, 16);
            } else {
                enc.movz(arm64::X2, 8, 0);
                enc.mul_rrr(arm64::X0, arm64::X0, arm64::X2);
                enc.ldr_imm(arm64::X1, arm64::FP, collOff);
                enc.add_rrr(arm64::X1, arm64::X1, arm64::X0);
                enc.ldr_imm(arm64::X0, arm64::X1, 8);
            }
            enc.str_imm(arm64::X0, arm64::FP, varSlot);

            compileStatement(fe->body.get());

            enc.bindLabel(stepLbl);
            enc.ldr_imm(arm64::X0, arm64::FP, idxOff);
            enc.add_rri(arm64::X0, arm64::X0, 1);
            enc.str_imm(arm64::X0, arm64::FP, idxOff);
            enc.b(startLbl);

            enc.bindLabel(endLbl);
            breakLabels.pop_back();
            continueLabels.pop_back();
            exitScope();
            return;
        }

        // file.open(path, mode) { ... }
        if (auto fb = dynamic_cast<FileOpenBlockNode*>(node)) {
            compileExpression(fb->pathExpr.get());
            enc.sub_rri(arm64::SP, arm64::SP, 32);
            enc.str_imm(arm64::X0, arm64::SP, 0);
            if (fb->modeExpr) {
                compileExpression(fb->modeExpr.get());
            } else {
                uint32_t rOff = emitter.addStringLiteralOffset("r");
                enc.adr_data(arm64::X0, rOff);
            }
            enc.mov_rr(arm64::X1, arm64::X0);
            enc.ldr_imm(arm64::X0, arm64::SP, 0);
            enc.bl(runtime.lblFileOpen);
            size_t okLbl = enc.createLabel();
            enc.cbnz(arm64::X0, okLbl);
            enc.adr_data(arm64::X0, runtime.offFileReadErr);
            enc.bl(runtime.lblPrintStr);
            enc.ldr_imm(arm64::X0, arm64::SP, 0);
            enc.bl(runtime.lblPrintStr);
            enc.adr_data(arm64::X0, runtime.offStrQuoteNL);
            enc.bl(runtime.lblPrintStr);
            enc.movz(arm64::X0, 1, 0);
            enc.bl(runtime.lblExit);
            enc.bindLabel(okLbl);

            enterScope();
            int32_t hOff = declareVar(fb->varName, ThavaType::FILE);
            enc.str_imm(arm64::X0, arm64::FP, hOff);
            for (const auto& st : fb->body) compileStatement(st.get());
            enc.ldr_imm(arm64::X0, arm64::FP, hOff);
            enc.bl(runtime.lblFileClose);
            exitScope();
            enc.add_rri(arm64::SP, arm64::SP, 32);
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
                ThavaType rt = compileExpression(ret->expr.get(), currentFuncRetType);
                if ((currentFuncRetType == ThavaType::DOUBLE || currentFuncRetType == ThavaType::FLOAT) &&
                    rt == ThavaType::INT) {
                    intToDoubleBits();
                } else if (currentFuncRetType == ThavaType::INT &&
                           (rt == ThavaType::DOUBLE || rt == ThavaType::FLOAT)) {
                    doubleBitsToInt();
                }
                if (currentFuncRetType == ThavaType::INT) narrowIntX0(currentFuncRetWidth);
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
        // Save FP and LR, establish frame pointer, allocate the local stack frame
        enc.stp_pre(arm64::FP, arm64::LR, arm64::SP, -16);
        enc.mov_rr(arm64::FP, arm64::SP);
        const auto localFrame = reserveLocalFrame();

        enterFunctionScope();
        currentFrameOffset = 0;
        currentFuncRetLabel = enc.createLabel();
        currentFuncRetType = func->returnType;
        currentFuncRetWidth = func->customReturnType;

        std::vector<DeferNode*> prevDefers = functionDefers;
        functionDefers.clear();

        // Map parameters from X0..X7 to local stack slots at [FP - offset]
        for (size_t p = 0; p < func->params.size(); ++p) {
            const std::string& pct = (p < func->paramCustomTypes.size())
                                            ? func->paramCustomTypes[p] : std::string();
            int32_t off = declareVar(func->params[p].second, func->params[p].first, pct);
            if (isCollectionType(func->params[p].first)) {
                // Element type comes from the call sites (g_funcListParamElem); the
                // parameter shadows a same-named global, so never inherit its type.
                auto eit = g_funcListParamElem.find(func->name);
                ThavaType et = (eit != g_funcListParamElem.end() && p < eit->second.size())
                                   ? eit->second[p] : ThavaType::UNKNOWN;
                if (et != ThavaType::UNKNOWN) listElemTypes[func->params[p].second] = et;
                else listElemTypes.erase(func->params[p].second);
                // Collection parameters arrive as the address of the caller's slot.
                int32_t refSlot = declareVar("@ref" + std::to_string(p), ThavaType::INT);
                VarInfo* pv = lookupVar(func->params[p].second);
                if (pv) { pv->listOut = true; pv->listOutSlot = refSlot; }
                if (p < 8) {
                    enc.str_imm(static_cast<arm64::Reg>(p), arm64::FP, refSlot);
                } else {
                    enc.ldr_imm(arm64::X9, arm64::FP, (int32_t)(16 + 8 * (int)(p - 8)));
                    enc.str_imm(arm64::X9, arm64::FP, refSlot);
                }
                enc.ldr_imm(arm64::X9, arm64::FP, refSlot);
                enc.ldr_imm(arm64::X9, arm64::X9, 0);
                enc.str_imm(arm64::X9, arm64::FP, off);
            } else if (p < 8) {
                arm64::Reg pReg = static_cast<arm64::Reg>(p);
                if (func->params[p].first == ThavaType::INT && pct != "int64") {
                    uint32_t bits = (pct == "int16") ? 16 : 32;
                    enc.lsl_ri(pReg, pReg, bits);
                    enc.asr_ri(pReg, pReg, bits);
                }
                enc.str_imm(pReg, arm64::FP, off);
            } else {
                // Extra parameters arrive on the caller's stack (AAPCS64).
                enc.ldr_imm(arm64::X9, arm64::FP, (int32_t)(16 + 8 * (int)(p - 8)));
                if (func->params[p].first == ThavaType::INT && pct != "int64") {
                    uint32_t bits = (pct == "int16") ? 16 : 32;
                    enc.lsl_ri(arm64::X9, arm64::X9, bits);
                    enc.asr_ri(arm64::X9, arm64::X9, bits);
                }
                enc.str_imm(arm64::X9, arm64::FP, off);
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

        // Copy collection parameters back to the caller (same rule as x86).
        for (size_t p = 0; p < func->params.size(); ++p) {
            if (!isCollectionType(func->params[p].first)) continue;
            VarInfo* pv = lookupVar(func->params[p].second);
            if (!pv || !pv->listOut) continue;
            enc.ldr_imm(arm64::X9, arm64::FP, pv->offset);
            enc.ldr_imm(arm64::X10, arm64::FP, pv->listOutSlot);
            enc.str_imm(arm64::X9, arm64::X10, 0);
        }

        finalizeLocalFrame(localFrame);
        exitFunctionScope();
        functionDefers = prevDefers;

        // Epilogue: restore SP, restore FP, LR and return
        enc.mov_rr(arm64::SP, arm64::FP);
        enc.ldp_post(arm64::FP, arm64::LR, arm64::SP, 16);
        enc.ret();
    }

    // ==========================================
    // PROGRAM COMPILATION ENTRY POINT
    // ==========================================
    // Maps every @dlattach library into the running process and relocates its
    // absolute data words before any library code runs.
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
            const char* expectedMachine = std::is_same_v<TEmitter, ElfArm64Emitter> ? "arm64-elf" : "arm64-pe";
            const bool growableLists = lib.machine == std::string(expectedMachine) + "-multype-v2";
            if (!lib.machine.empty() && lib.machine != expectedMachine && !growableLists) {
                std::cerr << "[Thava Compiler] Error: dynamic library '" << libPath << "' was built for '"
                          << lib.machine << "', but this target is '" << expectedMachine << "'.\n";
                exit(1);
            }
            if (!growableLists) {
                for (const auto& [name, signature] : g_dlFunctions) {
                    if (signature.libPath == libPath &&
                        std::find(signature.paramTypes.begin(), signature.paramTypes.end(),
                                  ThavaType::MULTYPE) != signature.paramTypes.end()) {
                        diag::fail("Rebuild module '" + libPath + "' before passing <multype> to '" + name + "'.", {});
                    }
                }
            }
            const int64_t base = (int64_t)dlLoadBaseForPath(libPath);
            const int64_t delta = base - (int64_t)lib.imageBase;

            uint32_t pathStr = emitter.addStringLiteralOffset(libPath);
            uint32_t baseNameStr = emitter.addStringLiteralOffset(lib.basename.empty() ? libPath : lib.basename);
            const std::string errMsg =
                "[Thava Runtime] cannot load library '" + libPath + "'\n";
            uint32_t errStr = emitter.addStringLiteralOffset(errMsg);
            size_t errLbl = enc.createLabel();
            size_t okLbl = enc.createLabel();
            size_t altLbl = enc.createLabel();
            size_t fdLbl = enc.createLabel();

            // fd = openat(AT_FDCWD, path, O_RDONLY, 0); jumps to failLbl on error,
            // leaves the fd in X19.
            auto emitOpen = [&](uint32_t strOff, size_t failLbl) {
                enc.movn(arm64::X0, 99, 0);       // -100 = AT_FDCWD
                enc.adr_data(arm64::X1, strOff);
                enc.movz(arm64::X2, 0, 0);
                enc.movz(arm64::X3, 0, 0);
                enc.movz(arm64::X8, 56, 0);       // sys_openat
                enc.svc(0);
                enc.cmp_ri(arm64::X0, 0);
                enc.b_cond(arm64::LT, failLbl);
                enc.mov_rr(arm64::X19, arm64::X0); // fd
            };

            emitOpen(pathStr, altLbl);
            enc.b(fdLbl);

            // The recorded path can belong to the build machine (cross-compiling), so
            // fall back to the plain file name in the current directory.
            enc.bindLabel(altLbl);
            emitOpen(baseNameStr, errLbl);
            enc.bindLabel(fdLbl);

            // size = lseek(fd, 0, SEEK_END)
            enc.mov_rr(arm64::X0, arm64::X19);
            enc.movz(arm64::X1, 0, 0);
            enc.movz(arm64::X2, 2, 0);
            enc.movz(arm64::X8, 62, 0);
            enc.svc(0);
            enc.cmp_ri(arm64::X0, 0);
            enc.b_cond(arm64::LE, errLbl);        // same rule as the x86-64 loader
            enc.mov_rr(arm64::X20, arm64::X0);   // file size

            // Anonymous reservation first so the unaligned tail stays zero-filled.
            enc.mov_ri64(arm64::X0, base);
            // add_rri only encodes 12-bit immediates, so build the slack in a register.
            enc.mov_ri64(arm64::X1, kDLHeapSlack);
            enc.add_rrr(arm64::X1, arm64::X20, arm64::X1);
            enc.movz(arm64::X2, 7, 0);            // PROT_READ|WRITE|EXEC
            enc.movz(arm64::X3, 0x22, 0);         // MAP_PRIVATE|MAP_ANONYMOUS
            enc.mov_ri64(arm64::X4, -1);
            enc.movz(arm64::X5, 0, 0);
            enc.movz(arm64::X8, 222, 0);          // mmap
            enc.svc(0);
            enc.mov_ri64(arm64::X9, base);
            enc.cmp_rr(arm64::X0, arm64::X9);
            enc.b_cond(arm64::NE, errLbl);

            // Map the library file over the reservation (MAP_FIXED).
            enc.mov_ri64(arm64::X0, base);
            enc.mov_rr(arm64::X1, arm64::X20);
            enc.movz(arm64::X2, 7, 0);
            enc.movz(arm64::X3, 0x12, 0);         // MAP_PRIVATE|MAP_FIXED
            enc.mov_rr(arm64::X4, arm64::X19);
            enc.movz(arm64::X5, 0, 0);
            enc.movz(arm64::X8, 222, 0);
            enc.svc(0);
            enc.mov_ri64(arm64::X9, base);
            enc.cmp_rr(arm64::X0, arm64::X9);
            enc.b_cond(arm64::NE, errLbl);

            // close(fd)
            enc.mov_rr(arm64::X0, arm64::X19);
            enc.movz(arm64::X8, 57, 0);
            enc.svc(0);

            // Relocate the module's absolute function-pointer words.
            for (uint32_t relocOff : lib.relocs) {
                enc.mov_ri64(arm64::X9, base + (int64_t)relocOff);
                enc.ldr_imm(arm64::X10, arm64::X9, 0);
                enc.mov_ri64(arm64::X11, delta);
                enc.add_rrr(arm64::X10, arm64::X10, arm64::X11);
                enc.str_imm(arm64::X10, arm64::X9, 0);
            }

            // Run the module's top-level initializer.
            if (lib.hasInit) {
                enc.mov_ri64(arm64::X16, base + (int64_t)lib.textRva + (int64_t)lib.initOffset);
                enc.blr(arm64::X16);
            }
            enc.b(okLbl);

            // Report on stderr and exit(1) with raw syscalls only.
            enc.bindLabel(errLbl);
            enc.movz(arm64::X0, 2, 0);
            enc.adr_data(arm64::X1, errStr);
            enc.mov_ri64(arm64::X2, (int64_t)errMsg.size());
            enc.movz(arm64::X8, 64, 0);   // sys_write
            enc.svc(0);
            enc.movz(arm64::X0, 1, 0);
            enc.movz(arm64::X8, 93, 0);   // sys_exit
            enc.svc(0);

            enc.bindLabel(okLbl);
        }
    }

    size_t registerPELibraryThunk(const std::string& dll, const std::string& symbol) {
        const auto key = std::make_pair(dll, symbol);
        auto existing = peLibraryThunks.find(key);
        if (existing != peLibraryThunks.end()) return existing->second;
        emitter.addImport(dll, symbol);
        return peLibraryThunks.emplace(key, enc.createLabel()).first->second;
    }

    void registerPELibraryImports() {
        std::set<std::string> dllNames;
        for (const auto& path : g_dlLibraries) {
            const auto library = g_dlLibraryInfos.find(path);
            if (library == g_dlLibraryInfos.end() || library->second.machine != "arm64-pe" ||
                !library->second.hasPEInitializer) {
                std::cerr << "[Thava Compiler] Error: '" << path
                          << "' is not a Windows ARM64 Thava DLL; rebuild it with --target win-arm64 --lib.\n";
                exit(1);
            }
            std::string key = library->second.basename;
            for (char& ch : key) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            if (!dllNames.insert(key).second) {
                std::cerr << "[Thava Compiler] Error: multiple attached libraries named '" << key << "'.\n";
                exit(1);
            }
            if (library->second.machine != "arm64-pe" && library->second.machine != "arm64-pe-multype-v2")
                diag::fail("Incompatible Windows ARM64 module '" + library->second.path + "'.", {});
            if (library->second.machine == "arm64-pe") {
                for (const auto& [name, signature] : g_dlFunctions) {
                    if (signature.libPath == library->second.path &&
                        std::find(signature.paramTypes.begin(), signature.paramTypes.end(),
                                  ThavaType::MULTYPE) != signature.paramTypes.end()) {
                        diag::fail("Rebuild module '" + library->second.path +
                                   "' before passing <multype> to '" + name + "'.", {});
                    }
                }
            }
            peLibraryInitializers.push_back(registerPELibraryThunk(library->second.basename, peInitializerName));
        }
        for (const auto& [name, info] : g_dlFunctions)
            registerPELibraryThunk(info.libBasename, name);
    }

    void emitPELibraryThunks() {
        for (const auto& [key, label] : peLibraryThunks) {
            enc.bindLabel(label);
            enc.adr_data(arm64::X16, emitter.getFuncIatOffsetInRdata(key.first, key.second));
            enc.ldr_imm(arm64::X16, arm64::X16, 0);
            enc.br(arm64::X16);
        }
    }

    bool compileProgram(const std::vector<ASTPtr>& ast, const std::string& outputFile) {
        runtime.init();
        constexpr bool peTarget = std::is_same_v<TEmitter, PeEmitter>;
        const bool peLibrary = peTarget && g_isLibraryMode;
        if constexpr (peTarget) {
            registerPELibraryImports();
            if (peLibrary) {
                emitter.setDll(true);
                emitter.setDllExportName(outputFile);
                // Run Thava's module initializer explicitly outside the Windows loader lock.
                emitter.setEntryPointRva(0);
            }
        }
        size_t libInitLabel = 0;

        // 1. Jump over functions to program entry point (_start)
        size_t entryLabel = enc.createLabel();
        enc.b(entryLabel);

        // A library's initializer is its entry point: the loader calls it directly
        // and it sets up (and tears down) the module frame itself.
        if (g_isLibraryMode) libInitLabel = entryLabel;

        lblBoxedToString = enc.createLabel();
        lblMultypeToString = enc.createLabel();

        // 1.1. Exception runtime helper (must exist before any body is emitted)
        emitThrowHelper();

        // 1.2. Globals: one writable data cell for the program frame pointer, and
        // a slot per top-level variable in the program frame.
        currentFrameOffset = 0;
        enterFunctionScope();
        globalBaseCell = emitter.addDataSlot(8);
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
        lookupFloor = scopeStack.size();
        programFrameBase = currentFrameOffset;

        // 2. Pre-register classes (layouts + method labels)
        for (const auto& node : ast) {
            if (auto cd = dynamic_cast<ClassDeclNode*>(node.get())) registerNativeClass(cd);
        }

        // 2.1. Pre-register all user functions & forward declarations
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

        // 3.1. Emit bodies for all class methods and constructors
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



        // 4. Main Entry Point (_start)
        enc.bindLabel(entryLabel);
        if (peLibrary) {
            const auto init = enc.createLabel();
            emitGlobalBase(arm64::X9);
            enc.cbz(arm64::X9, init);
            enc.ret();
            enc.bindLabel(init);
        }
        // Setup main frame, preserving the caller's exception chain for a DLL initializer.
        const int savedFrame = peLibrary ? 32 : 16;
        enc.stp_pre(arm64::FP, arm64::LR, arm64::SP, -savedFrame);
        enc.mov_rr(arm64::FP, arm64::SP);
        if (peLibrary) enc.str_imm(arm64::X28, arm64::FP, 16);
        const auto localFrame = reserveLocalFrame();

        // Globals already reserved the first slots of this frame (step 1.2);
        // locals continue after them.
        currentFrameOffset = programFrameBase;
        enterFunctionScope();
        lookupFloor = 0;
        // Globals already reserved the first slots of this frame (step 1.2);
        // publish FP so functions and methods can reach them.
        enc.adr_data(arm64::X9, globalBaseCell);
        enc.str_imm(arm64::FP, arm64::X9, 0);

        // Map and relocate every @dlattach library before any of its code runs (a
        // library itself never runs the loader: its exports are entered directly).
        if constexpr (std::is_same_v<TEmitter, ElfArm64Emitter>) {
            if (!g_isLibraryMode) emitDLAttachLoader();
        }

        if constexpr (std::is_same_v<TRuntime, Arm64WinRuntime>) {
            enc.movz(arm64::X0, 65001, 0);
            emitWindowsImport("SetConsoleCP");
            enc.movz(arm64::X0, 65001, 0);
            emitWindowsImport("SetConsoleOutputCP");
            enc.bl(runtime.lblSeedRand);
        }

        // No exception handler is installed at program start.
        enc.movz(arm64::X28, 0, 0);

        if constexpr (peTarget) {
            for (const auto label : peLibraryInitializers) enc.bl(label);
        }

        topLevelDefers.clear();

        // Compile top-level statements
        emittingProgramBody = true;
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

        finalizeLocalFrame(localFrame);
        exitFunctionScope();

        if (g_isLibraryMode) {
            if (peLibrary) {
                // Top-level code and any functions called there used the live
                // initializer frame. Move its globals into persistent image data.
                uint32_t bytes = static_cast<uint32_t>(-programFrameBase);
                uint32_t storage = emitter.addDataSlot(bytes + 8);
                enc.adr_data(arm64::X9, storage + bytes);
                for (const auto& slot : globalVarSlots) {
                    enc.ldr_imm(arm64::X10, arm64::FP, slot.second);
                    enc.str_imm(arm64::X10, arm64::X9, slot.second);
                }
                enc.adr_data(arm64::X10, globalBaseCell);
                enc.str_imm(arm64::X9, arm64::X10, 0);
                enc.ldr_imm(arm64::X28, arm64::FP, 16);
            }
            enc.mov_rr(arm64::SP, arm64::FP);
            enc.ldp_post(arm64::FP, arm64::LR, arm64::SP, savedFrame);
            enc.ret();
        } else {
            // Same trailing banner as the x86-64 PE/ELF backends (Linux runtime only:
            // the Windows ARM64 runtime has no such literal).
            if constexpr (std::is_same_v<TRuntime, Arm64NativeRuntime>) {
                enc.adr_data(arm64::X0, runtime.offSuccessMsg);
                enc.bl(runtime.lblPrintStr);
            }

            // Exit with status 0
            enc.movz(arm64::X0, 0, 0);
            enc.bl(runtime.lblExit);
        }

        // Dynamic library metadata and PE export registrations precede IAT layout.
        uint32_t metaSlot = 0;
        if (g_isLibraryMode) {
            metaSlot = emitter.addDataSlot(8);
            if constexpr (peTarget) {
                emitter.addExport(peInitializerName, static_cast<uint32_t>(enc.getLabelOffset(entryLabel)));
                emitter.addExport(peMetadataName, static_cast<uint32_t>(enc.code.size()));
                for (const auto& node : ast) {
                    if (auto fn = dynamic_cast<FunctionDeclNode*>(node.get())) {
                        if (fn->name == peInitializerName || fn->name == peMetadataName) {
                            std::cerr << "[ARM64 Codegen Error]: Reserved DLL export '" << fn->name << "'.\n";
                            exit(1);
                        }
                        emitter.addExport(fn->name, static_cast<uint32_t>(enc.getLabelOffset(functions.at(fn->name).labelId)));
                    }
                }
            }
            enc.adr_data(arm64::X9, metaSlot);
            enc.ldr_imm(arm64::X0, arm64::X9, 0);
            if (peLibrary) enc.add_rrr(arm64::X0, arm64::X9, arm64::X0);
            enc.ret();
        }
        emitMultypeHelpers();
        // The linked runtime uses ARM64 page-relative addressing.
        if (decimalEntry) {
            constexpr size_t alignment = 4096;
            enc.code.resize((enc.code.size() + alignment - 1) & ~(alignment - 1));
            const auto first = std::begin(decimal_runtime::arm64);
            const auto last = std::end(decimal_runtime::arm64);
            const auto entry = first + decimal_runtime::arm64Entry;
            enc.code.insert(enc.code.end(), first, entry);
            enc.bindLabel(*decimalEntry);
            enc.code.insert(enc.code.end(), entry, last);
        }
        if (mutationEntry) {
            enc.bindLabel(mutationAllocator);
            enc.b(runtime.lblMalloc);
            constexpr size_t alignment = 4096;
            enc.code.resize((enc.code.size() + alignment - 1) & ~(alignment - 1));
            const auto first = std::begin(multype_mutation::arm64);
            const auto entry = first + multype_mutation::arm64Entry;
            enc.code.insert(enc.code.end(), first, entry);
            enc.bindLabel(*mutationEntry);
            enc.code.insert(enc.code.end(), entry, std::end(multype_mutation::arm64));
        }
        runtime.emitHelpers();
        if constexpr (peTarget) emitPELibraryThunks();

        // 6. Resolve all branch and data offset fixups. The .data RVA depends on the
        // final .text size, so the code has to be handed over first; the metadata
        // string then carries the real image offsets.
        emitter.setTextCode(enc.code);
        uint32_t dataRva = 0;
        if constexpr (std::is_same_v<TEmitter, ElfArm64Emitter>) {
            dataRva = emitter.getDataRva();
        } else {
            dataRva = emitter.getRdataRva();
        }
        if (g_isLibraryMode) {
            auto hexVal = [](uint64_t v) {
                static const char* digits = "0123456789abcdef";
                std::string s;
                do { s.insert(s.begin(), digits[v & 0xF]); v >>= 4; } while (v);
                return "0x" + s;
            };

            std::string metaStr = "__THAVA_METADATA_START__\n# THAVA DYNAMIC LIBRARY METADATA\n";
            metaStr += "machine:";
            metaStr += std::is_same_v<TEmitter, ElfArm64Emitter> ? "arm64-elf" : "arm64-pe";
            if (hasMultypeExports(ast)) metaStr += "-multype-v2";
            metaStr += "\n";
            if (peLibrary) metaStr += "pe-dll:1\n";
            metaStr += "image:" + hexVal(peLibrary ? PeEmitter::IMAGE_BASE : 0x400000) + "\n";
            metaStr += "text:" + hexVal(emitter.textRva) + "\n";
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
            if constexpr (std::is_same_v<TEmitter, ElfArm64Emitter>) {
                metaStr += "reloc:";
                bool firstReloc = true;
                for (const auto& slot : emitter.getFuncSlotOffsets()) {
                    if (!firstReloc) metaStr += ",";
                    firstReloc = false;
                    metaStr += hexVal((uint64_t)dataRva + slot.second);
                }
                metaStr += "\n";
            }
            metaStr += "__THAVA_METADATA_END__\n";
            uint32_t metaStrOffset = emitter.addStringLiteralOffset(metaStr);
            emitter.setDataSlotValue(metaSlot, peLibrary ? metaStrOffset - metaSlot
                                                        : 0x400000 + dataRva + metaStrOffset);
        }
        enc.applyFixups(emitter.textRva, dataRva);

        // 7. Hand machine code to emitter and write to disk
        emitter.setTextCode(enc.code);
        return emitter.buildAndWrite(outputFile);
    }
};

using Arm64ElfCodegen = Arm64Codegen<ElfArm64Emitter, Arm64NativeRuntime>;
using Arm64PeCodegen  = Arm64Codegen<PeEmitter, Arm64WinRuntime>;

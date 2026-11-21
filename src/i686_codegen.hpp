#pragma once

#include "elf_i686_emitter.hpp"
#include "i686_encoder.hpp"
#include <stdexcept>

namespace i686 {

// Thava's internal i686 convention uses EDX:EAX for scalar results and one
// 8-byte stack slot per argument. EBX remains the image base across calls.
class Codegen {
    struct Slot {
        Reg base;
        int32_t offset;
        ThavaType type;
        std::string width;
    };
    struct Function {
        const FunctionDeclNode* decl;
        size_t entry;
    };
    struct DataFixup { size_t at; uint32_t offset; };

    const bool windows;
    Encoder enc;
    PeEmitter pe{true};
    std::vector<uint8_t> elfData;
    std::map<std::string, uint32_t> strings;
    std::vector<DataFixup> dataFixups;
    std::vector<std::pair<size_t, std::string>> importFixups;
    std::map<std::string, Slot> globals, locals;
    std::map<std::string, Function> functions;
    std::vector<std::pair<size_t, size_t>> loops; // break, continue
    const FunctionDeclNode* currentFunction = nullptr;
    size_t returnLabel = 0;
    size_t writeLabel = enc.label(), printStringLabel = enc.label(), printIntLabel = enc.label();
    size_t compareStringsLabel = enc.label(), divideLabel = enc.label(), errorLabel = enc.label();
    uint32_t stdoutSlot = 0;

    [[noreturn]] static void unsupported(const std::string& feature) {
        throw std::runtime_error(feature + " is not supported by the native i686 backend yet. "
                                 "Use the same --target with --cpp for this program.");
    }

    static bool scalar(ThavaType t) {
        return t == ThavaType::INT || t == ThavaType::BOOL || t == ThavaType::STRING;
    }

    uint32_t addSlot() {
        if (windows) return pe.addDataSlot();
        while (elfData.size() % 8) elfData.push_back(0);
        const uint32_t offset = static_cast<uint32_t>(elfData.size());
        elfData.resize(offset + 8);
        return offset;
    }

    uint32_t addString(const std::string& value) {
        if (windows) return pe.addStringLiteralOffset(value);
        const auto [it, inserted] = strings.try_emplace(value, static_cast<uint32_t>(elfData.size()));
        if (inserted) {
            elfData.insert(elfData.end(), value.begin(), value.end());
            elfData.push_back(0);
        }
        return it->second;
    }

    void dataAddress(Reg reg, uint32_t offset) {
        enc.lea(reg, EBX, 0);
        dataFixups.push_back({enc.code.size() - 4, offset});
    }

    void callWindows(const std::string& name) {
        importFixups.emplace_back(enc.callImport(), name);
    }

    void loadValue(const Slot& slot) {
        enc.load(EAX, slot.base, slot.base == EBX ? 0 : slot.offset);
        if (slot.base == EBX) dataFixups.push_back({enc.code.size() - 4, uint32_t(slot.offset)});
        enc.load(EDX, slot.base, slot.base == EBX ? 0 : slot.offset + 4);
        if (slot.base == EBX) dataFixups.push_back({enc.code.size() - 4, uint32_t(slot.offset + 4)});
    }

    void normalize(ThavaType expected, const std::string& width, ThavaType actual) {
        if (expected != actual) unsupported("This implicit type conversion");
        if (expected == ThavaType::INT && width != "int64") {
            if (width == "int16") enc.signExtendWord();
            enc.signExtend();
        }
    }

    void storeValue(const Slot& slot, ThavaType actual) {
        normalize(slot.type, slot.width, actual);
        enc.store(slot.base, slot.base == EBX ? 0 : slot.offset, EAX);
        if (slot.base == EBX) dataFixups.push_back({enc.code.size() - 4, uint32_t(slot.offset)});
        enc.store(slot.base, slot.base == EBX ? 0 : slot.offset + 4, EDX);
        if (slot.base == EBX) dataFixups.push_back({enc.code.size() - 4, uint32_t(slot.offset + 4)});
    }

    const Slot& lookup(const std::string& name) const {
        if (auto found = locals.find(name); found != locals.end()) return found->second;
        if (auto found = globals.find(name); found != globals.end()) return found->second;
        unsupported("Variable '" + name + "'");
    }

    void integer(uint64_t value) {
        enc.imm(EAX, uint32_t(value));
        enc.imm(EDX, uint32_t(value >> 32));
    }

    void stringValue(const std::string& value) {
        dataAddress(EAX, addString(value));
        enc.alu(Xor, EDX, EDX);
    }

    void defaultValue(ThavaType type) {
        if (type == ThavaType::STRING) stringValue("");
        else integer(0);
    }

    void saveValue() { enc.push(EDX); enc.push(EAX); }
    void loadLeft() { enc.pop(ECX); enc.pop(ESI); }
    void testValue() { enc.alu(Or, EAX, EDX); }

    void negate(Reg low, Reg high) {
        enc.unary(3, low);
        enc.alu(Adc, high, 0);
        enc.unary(3, high);
    }

    static Cond condition(const std::string& op, bool unsignedComparison) {
        if (op == "==") return Equal;
        if (op == "!=") return NotEqual;
        if (op == "<") return unsignedComparison ? Below : Less;
        if (op == "<=") return unsignedComparison ? BelowEqual : LessEqual;
        if (op == ">") return unsignedComparison ? Above : Greater;
        return unsignedComparison ? AboveEqual : GreaterEqual;
    }

    static bool comparison(const std::string& op) {
        return op == "==" || op == "!=" || op == "<" || op == "<=" || op == ">" || op == ">=";
    }

    ThavaType emitBinary(const std::string& op, ThavaType left, ThavaType right) {
        loadLeft();
        if (comparison(op) && left == right && scalar(left)) {
            const auto yes = enc.label(), done = enc.label();
            if (left == ThavaType::STRING) {
                enc.call(compareStringsLabel);
                enc.alu(Cmp, EAX, 0);
                enc.jump(condition(op, false), yes);
            } else {
                const auto low = enc.label(), no = enc.label();
                enc.alu(Cmp, ESI, EDX);
                enc.jump(Equal, low);
                enc.jump(condition(op, false), yes);
                enc.jump(no);
                enc.bind(low);
                enc.alu(Cmp, ECX, EAX);
                enc.jump(condition(op, true), yes);
                enc.bind(no);
            }
            integer(0);
            enc.jump(done);
            enc.bind(yes);
            integer(1);
            enc.bind(done);
            return ThavaType::BOOL;
        }
        if (left != ThavaType::INT || right != ThavaType::INT)
            unsupported("Operator '" + op + "' for these operand types");

        if (op == "+" || op == "-" || op == "band" || op == "bor" || op == "bxor") {
            const Alu low = op == "+" ? Add : op == "-" ? Sub : op == "band" ? And : op == "bor" ? Or : Xor;
            const Alu high = low == Add ? Adc : low == Sub ? Sbb : low;
            enc.alu(low, ECX, EAX);
            enc.alu(high, ESI, EDX);
            enc.mov(EAX, ECX);
            enc.mov(EDX, ESI);
        } else if (op == "*") {
            // The high 32 bits include both cross products, modulo 2^64.
            enc.imul(ESI, EAX);
            enc.imul(EDX, ECX);
            enc.alu(Add, ESI, EDX);
            enc.unary(4, ECX); // MUL EDX:EAX = EAX * ECX
            enc.alu(Add, EDX, ESI);
        } else if (op == "/" || op == "//" || op == "%") {
            enc.call(divideLabel);
            if (op == "%") {
                enc.mov(EAX, EDI);
                enc.mov(EDX, ECX);
            }
        } else if (op == "bshl" || op == "bshr") {
            const bool rightShift = op == "bshr";
            enc.mov(EDI, ECX);
            enc.mov(ECX, EAX);
            enc.mov(EAX, EDI);
            enc.mov(EDX, ESI);
            const auto small = enc.label(), done = enc.label();
            enc.alu(And, ECX, 63);
            enc.alu(Cmp, ECX, 32);
            enc.jump(Below, small);
            if (rightShift) {
                enc.mov(EAX, EDX);
                enc.shift(7, EDX, 31); // SAR
                enc.shiftCL(7, EAX);
            } else {
                enc.mov(EDX, EAX);
                enc.alu(Xor, EAX, EAX);
                enc.shiftCL(4, EDX); // SHL
            }
            enc.jump(done);
            enc.bind(small);
            enc.shiftPairCL(rightShift, rightShift ? EAX : EDX, rightShift ? EDX : EAX);
            enc.shiftCL(rightShift ? 7 : 4, rightShift ? EDX : EAX);
            enc.bind(done);
        } else {
            unsupported("Integer operator '" + op + "'");
        }
        return ThavaType::INT;
    }

    ThavaType expression(ASTNode* node) {
        if (auto lit = dynamic_cast<LiteralNode*>(node)) {
            const auto type = lit->getLitType();
            const auto value = lit->getVal();
            if (type == ThavaType::STRING) stringValue(value);
            else if (type == ThavaType::INT) integer(std::stoll(value));
            else if (type == ThavaType::BOOL) integer(value == "true");
            else unsupported("This literal type");
            return type;
        }
        if (auto var = dynamic_cast<VarNode*>(node)) {
            const auto& slot = lookup(var->getName());
            loadValue(slot);
            return slot.type;
        }
        if (auto unary = dynamic_cast<UnaryMinusNode*>(node)) {
            if (expression(unary->expr.get()) != ThavaType::INT) unsupported("Non-integer negation");
            negate(EAX, EDX);
            return ThavaType::INT;
        }
        if (auto unary = dynamic_cast<UnaryBNotNode*>(node)) {
            if (expression(unary->expr.get()) != ThavaType::INT) unsupported("Non-integer bitwise NOT");
            enc.unary(2, EAX);
            enc.unary(2, EDX);
            return ThavaType::INT;
        }
        if (auto unary = dynamic_cast<UnaryNotNode*>(node)) {
            if (expression(unary->expr.get()) != ThavaType::BOOL) unsupported("Non-boolean NOT");
            enc.alu(Xor, EAX, 1);
            return ThavaType::BOOL;
        }
        if (auto op = dynamic_cast<BinaryOpNode*>(node)) {
            const auto left = expression(op->left.get());
            if (op->op == "and" || op->op == "or") {
                if (left != ThavaType::BOOL) unsupported("Non-boolean condition");
                const auto end = enc.label();
                enc.alu(Cmp, EAX, 0);
                enc.jump(op->op == "and" ? Equal : NotEqual, end);
                if (expression(op->right.get()) != ThavaType::BOOL) unsupported("Non-boolean condition");
                enc.bind(end);
                return ThavaType::BOOL;
            }
            saveValue();
            const auto right = expression(op->right.get());
            return emitBinary(op->op, left, right);
        }
        if (auto call = dynamic_cast<CallNode*>(node)) {
            const auto fn = functions.find(call->name);
            if (fn == functions.end()) unsupported("Call to '" + call->name + "'");
            const auto& callee = *fn->second.decl;
            if (callee.params.size() != call->args.size()) unsupported("Arguments to '" + call->name + "'");
            const auto bytes = uint32_t(8 * call->args.size());
            if (bytes > 4080) unsupported("A call with more than 510 arguments");
            if (bytes) enc.alu(Sub, ESP, bytes);
            for (size_t i = 0; i < call->args.size(); ++i) {
                const auto type = expression(call->args[i].get());
                const auto width = i < callee.paramCustomTypes.size() ? callee.paramCustomTypes[i] : "";
                storeValue({ESP, int32_t(8 * i), callee.params[i].first, width}, type);
            }
            enc.call(fn->second.entry);
            if (bytes) enc.alu(Add, ESP, bytes);
            return callee.returnType;
        }
        unsupported("This expression");
    }

    void print(ThavaType type) {
        if (type == ThavaType::BOOL) {
            const auto isTrue = enc.label(), done = enc.label();
            enc.alu(Cmp, EAX, 0);
            enc.jump(NotEqual, isTrue);
            stringValue("false");
            enc.jump(done);
            enc.bind(isTrue);
            stringValue("true");
            enc.bind(done);
            type = ThavaType::STRING;
        }
        if (type == ThavaType::STRING) enc.call(printStringLabel);
        else if (type == ThavaType::INT) enc.call(printIntLabel);
        else unsupported("Printing this type");
    }

    void printText(const std::string& value) {
        if (!value.empty()) {
            stringValue(value);
            enc.call(printStringLabel);
        }
    }

    void printLine(ASTNode* node) {
        if (auto literal = dynamic_cast<LiteralNode*>(node); literal && literal->getLitType() == ThavaType::STRING) {
            const auto text = literal->getVal();
            size_t cursor = 0;
            while (cursor < text.size()) {
                const auto open = text.find("@{", cursor);
                const auto close = open == std::string::npos ? open : text.find('}', open + 2);
                if (close == std::string::npos) {
                    printText(text.substr(cursor));
                    break;
                }
                printText(text.substr(cursor, open - cursor));
                auto snippet = parseThavaExpression(text.substr(open + 2, close - open - 2));
                print(expression(snippet.get()));
                cursor = close + 1;
            }
        } else print(expression(node));
        printText("\n");
    }

    void jumpIfFalse(ASTNode* condition, size_t target) {
        if (expression(condition) != ThavaType::BOOL) unsupported("Non-boolean condition");
        testValue();
        enc.jump(Equal, target);
    }

    void loop(ASTNode* cond, ASTNode* body, ASTNode* update = nullptr) {
        const auto test = enc.label(), next = update ? enc.label() : test, end = enc.label();
        enc.bind(test);
        jumpIfFalse(cond, end);
        loops.emplace_back(end, next);
        statement(body);
        loops.pop_back();
        if (update) {
            enc.bind(next);
            statement(update);
        }
        enc.jump(test);
        enc.bind(end);
    }

    void statement(ASTNode* node) {
        if (auto decl = dynamic_cast<VarDeclNode*>(node)) {
            const auto& slot = lookup(decl->varName);
            if (decl->initExpr) storeValue(slot, expression(decl->initExpr.get()));
            else {
                defaultValue(slot.type);
                storeValue(slot, slot.type);
            }
        } else if (auto assign = dynamic_cast<AssignNode*>(node)) {
            const auto& slot = lookup(assign->name);
            if (assign->op == "=") storeValue(slot, expression(assign->expr.get()));
            else {
                loadValue(slot);
                saveValue();
                const auto value = expression(assign->expr.get());
                storeValue(slot, emitBinary(assign->op.substr(0, assign->op.size() - 1), slot.type, value));
            }
        } else if (auto body = dynamic_cast<BlockNode*>(node)) {
            for (const auto& child : body->statements) statement(child.get());
        } else if (auto branch = dynamic_cast<IfNode*>(node)) {
            const auto otherwise = enc.label();
            jumpIfFalse(branch->cond.get(), otherwise);
            statement(branch->thenBranch.get());
            if (branch->elseBranch) {
                const auto done = enc.label();
                enc.jump(done);
                enc.bind(otherwise);
                statement(branch->elseBranch.get());
                enc.bind(done);
            } else enc.bind(otherwise);
        } else if (auto w = dynamic_cast<WhileNode*>(node)) {
            loop(w->cond.get(), w->body.get());
        } else if (auto f = dynamic_cast<ForNode*>(node)) {
            statement(f->init.get());
            loop(f->cond.get(), f->body.get(), f->update.get());
        } else if (dynamic_cast<BreakNode*>(node) || dynamic_cast<ContinueNode*>(node)) {
            if (loops.empty()) unsupported("Loop control outside a loop");
            enc.jump(dynamic_cast<BreakNode*>(node) ? loops.back().first : loops.back().second);
        } else if (auto out = dynamic_cast<ConsOutNode*>(node)) {
            printLine(out->getExpr());
        } else if (auto ret = dynamic_cast<ReturnNode*>(node)) {
            if (!currentFunction) unsupported("Return outside a function");
            if (ret->expr) {
                const auto value = expression(ret->expr.get());
                normalize(currentFunction->returnType, currentFunction->customReturnType, value);
            } else integer(0);
            enc.jump(returnLabel);
        } else if (auto expr = dynamic_cast<ExpressionStatementNode*>(node)) {
            expression(expr->expr.get());
        } else if (!dynamic_cast<GlobalDeclNode*>(node)) {
            unsupported("This statement");
        }
    }

    void beginFrame(const std::vector<ASTPtr>& body, bool entry) {
        enc.push(EBP);
        enc.mov(EBP, ESP);
        locals.clear();
        if (!entry) {
            const auto& fn = *currentFunction;
            for (size_t i = 0; i < fn.params.size(); ++i) {
                const auto& [type, name] = fn.params[i];
                if (!scalar(type)) unsupported("Parameter '" + name + "' of '" + fn.name + "'");
                const auto width = i < fn.paramCustomTypes.size() ? fn.paramCustomTypes[i] : "";
                locals.emplace(name, Slot{EBP, int32_t(8 + 8 * i), type, width});
            }
        }
        std::vector<VarDeclNode*> decls;
        for (const auto& node : body) {
            if (entry && dynamic_cast<VarDeclNode*>(node.get())) continue;
            thavaCollectVarDecls(node.get(), decls);
        }
        int32_t frame = 0;
        for (const auto* decl : decls) {
            if (!scalar(decl->varType)) unsupported("Type of variable '" + decl->varName + "'");
            if (locals.try_emplace(decl->varName, Slot{EBP, -frame - 8, decl->varType, decl->customTypeName}).second)
                frame += 8;
        }
        if (frame > 4080) unsupported("A local stack frame larger than 4080 bytes");
        if (frame) enc.alu(Sub, ESP, uint32_t(frame));
        for (const auto& [name, slot] : locals) {
            if (slot.offset > 0) continue;
            defaultValue(slot.type);
            storeValue(slot, slot.type);
        }
    }

    void emitFunction(const Function& function) {
        const auto& fn = *function.decl;
        if (!scalar(fn.returnType) && fn.returnType != ThavaType::VOID)
            unsupported("Return type of '" + fn.name + "'");
        currentFunction = &fn;
        returnLabel = enc.label();
        enc.bind(function.entry);
        beginFrame(fn.body, false);
        for (const auto& node : fn.body) statement(node.get());
        defaultValue(fn.returnType);
        enc.bind(returnLabel);
        enc.leave();
        enc.ret();
        currentFunction = nullptr;
    }

    void exitProcess(uint32_t status) {
        if (windows) {
            enc.pushImm(status);
            callWindows("ExitProcess");
        } else {
            enc.imm(EBX, status);
            enc.imm(EAX, 1); // sys_exit
            enc.syscall();
        }
        enc.bytes({0x0f, 0x0b}); // UD2 if a non-returning OS call returns
    }

    void emitOutput() {
        // Write ECX bytes address, EDX length. EBX always survives the call.
        enc.bind(writeLabel);
        if (windows) {
            enc.alu(Sub, ESP, 4);
            enc.mov(EAX, ESP);
            enc.pushImm(0);
            enc.push(EAX);
            enc.push(EDX);
            enc.push(ECX);
            dataAddress(EAX, stdoutSlot);
            enc.load(EAX, EAX);
            enc.push(EAX);
            callWindows("WriteFile");
            enc.alu(Add, ESP, 4);
        } else {
            enc.push(EBX);
            enc.imm(EBX, 1); // stdout
            const auto next = enc.label(), done = enc.label();
            enc.bind(next);
            enc.alu(Cmp, EDX, 0);
            enc.jump(Equal, done);
            enc.imm(EAX, 4); // sys_write
            enc.syscall();
            enc.alu(Cmp, EAX, uint32_t(-4)); // EINTR
            enc.jump(Equal, next);
            enc.alu(Cmp, EAX, 0);
            enc.jump(LessEqual, done);
            enc.alu(Add, ECX, EAX);
            enc.alu(Sub, EDX, EAX);
            enc.jump(next);
            enc.bind(done);
            enc.pop(EBX);
        }
        enc.ret();

        enc.bind(printStringLabel);
        enc.mov(ECX, EAX);
        enc.mov(ESI, EAX);
        const auto byte = enc.label(), end = enc.label();
        enc.bind(byte);
        enc.loadByte(EAX, ESI);
        enc.alu(Cmp, EAX, 0);
        enc.jump(Equal, end);
        enc.alu(Add, ESI, 1);
        enc.jump(byte);
        enc.bind(end);
        enc.mov(EDX, ESI);
        enc.alu(Sub, EDX, ECX);
        enc.jump(writeLabel);

        enc.bind(printIntLabel);
        enc.alu(Sub, ESP, 32);
        enc.lea(EDI, ESP, 32);
        enc.mov(ESI, EDX);
        enc.alu(Cmp, EDX, 0);
        const auto digits = enc.label(), write = enc.label();
        enc.jump(GreaterEqual, digits);
        negate(EAX, EDX);
        enc.bind(digits);
        enc.mov(ECX, EAX);
        enc.mov(EAX, EDX);
        enc.alu(Xor, EDX, EDX);
        enc.push(ECX);
        enc.imm(ECX, 10);
        enc.unary(6, ECX); // Divide upper word, then carry its remainder into lower word.
        enc.pop(ECX);
        enc.push(EAX);
        enc.mov(EAX, ECX);
        enc.imm(ECX, 10);
        enc.unary(6, ECX);
        enc.alu(Add, EDX, '0');
        enc.alu(Sub, EDI, 1);
        enc.storeByte(EDI, EDX);
        enc.pop(EDX);
        enc.mov(ECX, EAX);
        enc.alu(Or, ECX, EDX);
        enc.jump(NotEqual, digits);
        enc.alu(Cmp, ESI, 0);
        enc.jump(GreaterEqual, write);
        enc.alu(Sub, EDI, 1);
        enc.imm(EAX, '-');
        enc.storeByte(EDI, EAX);
        enc.bind(write);
        enc.mov(ECX, EDI);
        enc.lea(EDX, ESP, 32);
        enc.alu(Sub, EDX, ECX);
        enc.call(writeLabel);
        enc.alu(Add, ESP, 32);
        enc.ret();

        enc.bind(compareStringsLabel);
        enc.mov(ESI, ECX);
        enc.mov(EDI, EAX);
        const auto compare = enc.label(), finish = enc.label();
        enc.bind(compare);
        enc.loadByte(EAX, ESI);
        enc.loadByte(EDX, EDI);
        enc.alu(Sub, EAX, EDX);
        enc.jump(NotEqual, finish);
        enc.alu(Cmp, EDX, 0);
        enc.jump(Equal, finish);
        enc.alu(Add, ESI, 1);
        enc.alu(Add, EDI, 1);
        enc.jump(compare);
        enc.bind(finish);
        enc.ret();
    }

    void emitDivision() {
        // Signed division via unsigned restoring division. Inputs ESI:ECX and
        // EDX:EAX. Return quotient EDX:EAX and remainder ECX:EDI.
        enc.bind(divideLabel);
        const auto lhsReady = enc.label(), rhsReady = enc.label(), nonzero = enc.label();
        const auto step = enc.label(), subtract = enc.label(), advance = enc.label();
        const auto quotientReady = enc.label(), remainderReady = enc.label();
        enc.push(EBP);
        enc.mov(EBP, ESP);
        enc.push(EBX);
        enc.push(ESI); // original high word of dividend
        enc.push(EDX); // original high word of divisor
        enc.pushImm(64);
        enc.mov(EDI, EAX);
        enc.mov(EAX, ECX);
        enc.mov(ECX, EDX);
        enc.alu(Cmp, ESI, 0);
        enc.jump(GreaterEqual, lhsReady);
        negate(EAX, ESI);
        enc.bind(lhsReady);
        enc.alu(Cmp, ECX, 0);
        enc.jump(GreaterEqual, rhsReady);
        negate(EDI, ECX);
        enc.bind(rhsReady);
        enc.mov(EDX, EDI);
        enc.alu(Or, EDX, ECX);
        enc.jump(NotEqual, nonzero);
        enc.jump(errorLabel);
        enc.bind(nonzero);
        enc.alu(Xor, EDX, EDX); // remainder high
        enc.alu(Xor, EBX, EBX); // remainder low
        enc.bind(step);
        enc.shift(4, EAX, 1); // SHL, then propagate carry through all four words.
        enc.shift(2, ESI, 1); // RCL
        enc.shift(2, EBX, 1);
        enc.shift(2, EDX, 1);
        enc.alu(Cmp, EDX, ECX);
        enc.jump(Above, subtract);
        enc.jump(Below, advance);
        enc.alu(Cmp, EBX, EDI);
        enc.jump(Below, advance);
        enc.bind(subtract);
        enc.alu(Sub, EBX, EDI);
        enc.alu(Sbb, EDX, ECX);
        enc.alu(Or, EAX, 1);
        enc.bind(advance);
        enc.decrement(EBP, -16);
        enc.jump(NotEqual, step);
        enc.mov(EDI, EBX);
        enc.mov(ECX, EDX);
        enc.mov(EDX, ESI);
        enc.load(ESI, EBP, -8);
        enc.alu(Cmp, ESI, 0);
        enc.jump(GreaterEqual, remainderReady);
        negate(EDI, ECX);
        enc.bind(remainderReady);
        enc.load(EBX, EBP, -12);
        enc.alu(Xor, ESI, EBX);
        enc.jump(NotSign, quotientReady);
        negate(EAX, EDX);
        enc.bind(quotientReady);
        enc.load(EBX, EBP, -4);
        enc.leave();
        enc.ret();
    }

public:
    explicit Codegen(bool windowsTarget) : windows(windowsTarget) {}

    bool compile(const std::vector<ASTPtr>& ast, const std::string& output) {
        if (g_isLibraryMode || g_hasDLAttach) unsupported("Dynamic libraries");
        for (const auto& node : ast) {
            if (auto func = dynamic_cast<FunctionDeclNode*>(node.get()))
                functions.emplace(func->name, Function{func, enc.label()});
            else if (auto decl = dynamic_cast<VarDeclNode*>(node.get())) {
                if (!scalar(decl->varType)) unsupported("Type of global '" + decl->varName + "'");
                globals.emplace(decl->varName, Slot{EBX, int32_t(addSlot()), decl->varType, decl->customTypeName});
            }
        }

        // A call to the next instruction yields a PC without requiring text relocations.
        const auto start = enc.label();
        enc.call(start);
        const auto entryAddressRva = ElfI686Emitter::textRva + enc.code.size();
        enc.bind(start);
        enc.pop(EBX);
        enc.alu(Sub, EBX, uint32_t(entryAddressRva));
        if (windows) {
            stdoutSlot = addSlot();
            enc.pushImm(65001);
            callWindows("SetConsoleOutputCP");
            enc.pushImm(uint32_t(-11)); // STD_OUTPUT_HANDLE
            callWindows("GetStdHandle");
            dataAddress(ECX, stdoutSlot);
            enc.store(ECX, 0, EAX);
        }
        for (const auto& [name, slot] : globals) {
            if (slot.type != ThavaType::STRING) continue;
            stringValue("");
            storeValue(slot, slot.type);
        }
        beginFrame(ast, true);
        for (const auto& node : ast) {
            if (dynamic_cast<FunctionDeclNode*>(node.get()) || dynamic_cast<ForwardDeclNode*>(node.get()))
                continue;
            statement(node.get());
        }
        exitProcess(0);

        for (const auto& entry : functions) emitFunction(entry.second);
        emitOutput();
        emitDivision();
        enc.bind(errorLabel);
        exitProcess(1);
        if (!enc.resolveBranches()) throw std::runtime_error("Unresolved native i686 branch.");

        uint32_t dataRva = ElfI686Emitter::dataRva(enc.code.size());
        if (windows) {
            pe.setTextBytes(enc.code);
            pe.layoutRdata();
            dataRva = pe.getRdataRva();
        }
        for (const auto& fixup : dataFixups) enc.patch(fixup.at, dataRva + fixup.offset);
        for (const auto& fixup : importFixups) enc.patch(fixup.first, pe.getImportIatRva(fixup.second));
        if (!windows) {
            if (elfData.empty()) elfData.push_back(0);
            return ElfI686Emitter::write(output, enc.code, elfData);
        }
        pe.setTextBytes(enc.code);
        return pe.buildAndWrite(output);
    }
};

} // namespace i686

#pragma once

#include "elf_i686_emitter.hpp"
#include "i686_encoder.hpp"
#include "i686_runtime_data.hpp"
#include "i686_os_ops.h"
#include "i686_value_kinds.h"
#include "unicode_case.hpp"
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
        bool indirect = false;
    };
    struct LoopSlots { Slot first, second; };
    struct LoopTarget { size_t end, next, scopes; };
    struct Cleanup { int32_t offset; size_t action; };
    enum RecordField { Previous = 0, Frame = 4, Action = 8, Stack = 12, Defers = 16, Image = 20, RecordBytes = 24 };
    enum ExceptionField { CleanupHead = 0, PendingException = 8 };
    enum DeferField { NextDefer = 0, DeferAction = 4, DeferBytes = 8 };
    struct Function {
        std::string name, owner;
        ThavaType returnType;
        std::string customReturnType;
        const std::vector<std::pair<ThavaType, std::string>>& params;
        const std::vector<std::string>& paramCustomTypes;
        const std::vector<ASTPtr>& body;
        size_t entry;
    };
    struct ObjectLayout {
        size_t initializer;
        int32_t size = 0;
        uint32_t descriptor = 0;
        std::map<std::string, Slot> fields;
        std::string schema;
    };
    struct ClassLayout : ObjectLayout {
        const ClassDeclNode* decl;
        std::vector<Function> constructors;
        std::map<std::string, Function> methods;
    };
    struct StructLayout : ObjectLayout {
        const StructDeclNode* decl;
    };
    struct Import {
        const DLFunctionInfo* info;
        size_t entry;
        uint32_t slot;
        std::vector<std::pair<ThavaType, std::string>> params;
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
    const std::vector<ASTPtr> emptyBody;
    std::map<size_t, std::pair<Function, size_t>> boxedBridges, methodBridges;
    std::map<std::string, Import> imports;
    std::map<std::string, uint32_t> moduleSlots;
    uint32_t moduleRegistry = 0, moduleRegistryPointer = 0, moduleState = 0;
    static constexpr const char* moduleInitializer = "__thava_i686_init";
    std::map<std::string, ClassLayout> classes;
    std::map<std::string, StructLayout> structs;
    std::string lastCustomType, initializerClass;
    std::map<ASTNode*, LoopSlots> loopSlots;
    std::vector<LoopTarget> loops;
    std::map<FileOpenBlockNode*, Slot> fileSlots;
    std::map<const FileOpenBlockNode*, Cleanup> fileCleanups;
    std::map<const TryExceptNode*, Cleanup> handlers;
    std::map<const ExceptClause*, Slot> catchSlots;
    std::map<const DeferNode*, int32_t> deferSlots;
    std::vector<Cleanup> scopes;
    Cleanup functionCleanup{};
    bool inDefer = false;
    uint32_t exceptionContext = 0;
    size_t unwindLabel = enc.label(), runDefersLabel = enc.label();
    struct OpenFile { std::string name; const Slot* slot; Cleanup cleanup; };
    std::vector<OpenFile> activeFiles;
    bool fileUsed = false;
    size_t fileReadLabel = enc.label(), fileWriteLabel = enc.label();
    const Function* currentFunction = nullptr;
    size_t returnLabel = 0;
    size_t writeLabel = enc.label(), printStringLabel = enc.label(), printIntLabel = enc.label();
    size_t compareStringsLabel = enc.label(), divideLabel = enc.label(), errorLabel = enc.label();
    uint32_t stdoutSlot = 0, stdinSlot = 0;
    size_t readLineLabel = enc.label(), readByteLabel = enc.label();
    bool readUsed = false;
    uint32_t caseTable = UINT32_MAX;
    uint32_t environmentSlot = 0;
    uint32_t linuxPlatformSlot = 0;
    uint32_t randomState = UINT32_MAX;
    size_t allocateLabel = enc.label();
    bool allocatorUsed = false;
    std::optional<size_t> decimalEntry;
    std::vector<DataFixup> textFixups;
    std::map<uint32_t, size_t> runtimeCalls;

    [[noreturn]] static void unsupported(const std::string& feature) {
        throw std::runtime_error(feature + " is not supported by the native i686 backend yet. "
                                 "Use the same --target with --cpp for this program.");
    }

    static bool floating(ThavaType t) {
        return t == ThavaType::FLOAT || t == ThavaType::DOUBLE;
    }

    static bool scalar(ThavaType t) {
        return t == ThavaType::INT || t == ThavaType::BOOL || t == ThavaType::STRING || t == ThavaType::DECIMAL || floating(t);
    }

    static bool collection(ThavaType type) {
        return type == ThavaType::LIST || type == ThavaType::TUPLE || type == ThavaType::DICT || type == ThavaType::MULTYPE;
    }

    static bool slotType(ThavaType type) {
        return scalar(type) || collection(type) || type == ThavaType::CUSTOM || type == ThavaType::FUNC;
    }

    static uint32_t valueKind(ThavaType type) {
        switch (type) {
            case ThavaType::NONE: return VALUE_NONE;
            case ThavaType::INT: return VALUE_INT;
            case ThavaType::FLOAT: case ThavaType::DOUBLE: return VALUE_REAL;
            case ThavaType::BOOL: return VALUE_BOOL;
            case ThavaType::STRING: case ThavaType::DECIMAL: return VALUE_STRING;
            case ThavaType::LIST: case ThavaType::MULTYPE: return VALUE_LIST;
            case ThavaType::TUPLE: return VALUE_TUPLE;
            case ThavaType::DICT: return VALUE_DICT;
            case ThavaType::CUSTOM: return VALUE_OBJECT;
            case ThavaType::FUNC: return VALUE_FUNCTION;
            default: unsupported("This collection value type");
        }
    }

    uint32_t addSlot() {
        if (windows) return pe.addDataSlot();
        while (elfData.size() % 8) elfData.push_back(0);
        const uint32_t offset = static_cast<uint32_t>(elfData.size());
        elfData.resize(offset + 8);
        return offset;
    }

    uint32_t reserveData(size_t bytes) {
        const auto base = addSlot();
        for (size_t i = 8; i < bytes; i += 8) addSlot();
        return base;
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

    void callRuntime(uint32_t offset, uint32_t argumentBytes) {
        auto found = runtimeCalls.find(offset);
        if (found == runtimeCalls.end()) found = runtimeCalls.emplace(offset, enc.label()).first;
        enc.call(found->second);
        enc.alu(Add, ESP, argumentBytes);
    }

    void codeAddress(Reg reg, size_t label) {
        enc.lea(reg, EBX, 0);
        textFixups.push_back({enc.code.size() - 4, static_cast<uint32_t>(label)});
    }

    void pushAllocationContext() {
        allocatorUsed = true;
        enc.push(EBX);
        codeAddress(EDI, allocateLabel);
        enc.push(EDI);
    }

    uint32_t caseMappings() {
        if (caseTable != UINT32_MAX) return caseTable;
        if (windows) caseTable = thava_unicode::addCaseMappings(pe);
        else {
            while (elfData.size() % 8) elfData.push_back(0);
            caseTable = static_cast<uint32_t>(elfData.size());
            for (const auto& row : thava_unicode::caseMappings)
                for (auto field : row)
                    for (int byte = 0; byte < 8; ++byte)
                        elfData.push_back(static_cast<uint8_t>(field >> (8 * byte)));
        }
        return caseTable;
    }

    void floatCast(uint32_t operation) {
        enc.pushImm(operation);
        saveValue();
        callRuntime(runtime::rt_float_cast, 12);
    }

    void slotAddress(const Slot& slot, Reg destination) {
        if (slot.base == EBX) dataAddress(destination, slot.offset);
        else if (slot.indirect) enc.load(destination, slot.base, slot.offset);
        else enc.lea(destination, slot.base, slot.offset);
    }

    void loadValue(const Slot& slot) {
        slotAddress(slot, ECX);
        enc.load(EAX, ECX);
        enc.load(EDX, ECX, 4);
        lastCustomType = slot.type == ThavaType::CUSTOM ? slot.width : "";
    }

    void normalize(ThavaType expected, const std::string& width, ThavaType actual) {
        if (expected == ThavaType::UNKNOWN && actual == ThavaType::UNKNOWN) return;
        if (actual == ThavaType::UNKNOWN) {
            if (expected == ThavaType::CUSTOM) {
                dataAddress(ECX, objectLayout(width).descriptor);
                enc.push(ECX);
                enc.push(EAX);
                callRuntime(runtime::rt_value_as_object, 8);
                const auto valid = enc.label();
                enc.alu(Cmp, EAX, uint32_t(-1));
                enc.jump(NotEqual, valid);
                LiteralNode message("Expected object of type '" + width + "'", ThavaType::STRING);
                raiseException(&message);
                enc.bind(valid);
                enc.alu(Xor, EDX, EDX);
            } else {
                enc.pushImm(valueKind(expected));
                enc.push(EAX);
                callRuntime(runtime::rt_value_as, 8);
            }
            actual = expected;
            lastCustomType = expected == ThavaType::CUSTOM ? width : "";
        }
        if (collection(expected) && collection(actual) &&
            (expected == ThavaType::MULTYPE || actual == ThavaType::MULTYPE) &&
            expected != ThavaType::DICT && actual != ThavaType::DICT) return;
        if ((expected == ThavaType::CUSTOM || expected == ThavaType::FUNC) && actual == ThavaType::NONE) return;
        if (expected == ThavaType::CUSTOM && actual == ThavaType::CUSTOM && !lastCustomType.empty()) {
            std::string type = lastCustomType;
            while (type != width) {
                const auto found = classes.find(type);
                if (found == classes.end() || found->second.decl->baseClassName.empty())
                    unsupported("Conversion from '" + lastCustomType + "' to '" + width + "'");
                type = found->second.decl->baseClassName;
            }
        }
        if (floating(expected) && actual == ThavaType::INT) floatCast(0);
        else if (expected == ThavaType::INT && floating(actual)) floatCast(1);
        else if (expected != actual && !(floating(expected) && floating(actual)))
            unsupported("This type conversion");
        if (expected == ThavaType::FLOAT) floatCast(2);
        if (expected == ThavaType::INT && width != "int64") {
            if (width == "int16") enc.signExtendWord();
            enc.signExtend();
        }
    }

    void storeValue(const Slot& slot, ThavaType actual) {
        normalize(slot.type, slot.width, actual);
        slotAddress(slot, ECX);
        enc.store(ECX, 0, EAX);
        enc.store(ECX, 4, EDX);
    }

    Slot* findSlot(const std::string& name) {
        if (auto found = locals.find(name); found != locals.end()) return &found->second;
        if (auto found = globals.find(name); found != globals.end()) return &found->second;
        return nullptr;
    }

    Slot& lookup(const std::string& name) {
        if (auto* slot = findSlot(name)) return *slot;
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
        lastCustomType.clear();
        if (type == ThavaType::DECIMAL) stringValue("0");
        else if (type == ThavaType::STRING) stringValue("");
        else if (collection(type)) {
            pushAllocationContext();
            callRuntime(runtime::rt_collection_new, 8);
            enc.alu(Xor, EDX, EDX);
        } else integer(0);
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
        if (left == ThavaType::UNKNOWN || right == ThavaType::UNKNOWN ||
            ((op == "==" || op == "!=") && collection(left) && collection(right)))
            return emitValueBinary(op, left, right);
        loadLeft();
        if ((op == "==" || op == "!=") &&
            (left == ThavaType::CUSTOM || left == ThavaType::FUNC || left == ThavaType::NONE) &&
            (right == ThavaType::CUSTOM || right == ThavaType::FUNC || right == ThavaType::NONE)) {
            const auto yes = enc.label(), done = enc.label();
            enc.alu(Cmp, ECX, EAX);
            enc.jump(condition(op, false), yes);
            integer(0);
            enc.jump(done);
            enc.bind(yes);
            integer(1);
            enc.bind(done);
            return ThavaType::BOOL;
        }
        if (floating(left) || floating(right)) {
            if ((!floating(left) && left != ThavaType::INT) ||
                (!floating(right) && right != ThavaType::INT)) unsupported("Floating-point operands");
            static const std::map<std::string, uint32_t> operations = {
                {"+",0}, {"-",1}, {"*",2}, {"/",3}, {"//",3},
                {"==",4}, {"!=",5}, {"<",6}, {"<=",7}, {">",8}, {">=",9}
            };
            auto operation = operations.find(op);
            if (operation == operations.end()) unsupported("Floating-point operator '" + op + "'");
            enc.pushImm(operation->second);
            enc.pushImm((left == ThavaType::INT ? 1 : 0) | (right == ThavaType::INT ? 2 : 0));
            saveValue();
            enc.push(ESI);
            enc.push(ECX);
            callRuntime(runtime::rt_float_binary, 24);
            if (comparison(op)) return ThavaType::BOOL;
            if (op == "//") {
                floatCast(1);
                return ThavaType::INT;
            }
            if (left == ThavaType::DOUBLE || right == ThavaType::DOUBLE) return ThavaType::DOUBLE;
            floatCast(2);
            return ThavaType::FLOAT;
        }
        if (op == "+" && left == ThavaType::STRING && right == ThavaType::STRING) {
            pushAllocationContext();
            enc.push(EAX);
            enc.push(ECX);
            callRuntime(runtime::rt_concat, 16);
            enc.alu(Xor, EDX, EDX);
            return ThavaType::STRING;
        }
        if (op == "*" && ((left == ThavaType::STRING && right == ThavaType::INT) ||
                          (left == ThavaType::INT && right == ThavaType::STRING))) {
            pushAllocationContext();
            if (left == ThavaType::STRING) {
                saveValue();
                enc.push(ECX);
            } else {
                enc.push(ESI);
                enc.push(ECX);
                enc.push(EAX);
            }
            callRuntime(runtime::rt_repeat, 20);
            enc.alu(Xor, EDX, EDX);
            return ThavaType::STRING;
        }
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
        } else if (op == "**") {
            saveValue();
            enc.push(ESI);
            enc.push(ECX);
            callRuntime(runtime::rt_int_pow, 16);
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

    void expectString(ASTNode* node) {
        const auto type = expression(node);
        if (type == ThavaType::UNKNOWN) normalize(ThavaType::STRING, "", type);
        else if (type != ThavaType::STRING) unsupported("This string operand");
    }

    void convertCase(const std::string& method) {
        const auto mode = method == "lower" ? 1 : method == "upper" ? 2 : method == "title" ? 3 : 0;
        if (!mode) unsupported("String method '" + method + "'");
        pushAllocationContext();
        enc.pushImm(thava_unicode::caseMappingCount);
        dataAddress(ECX, caseMappings());
        enc.push(ECX);
        enc.pushImm(mode);
        enc.push(EAX);
        callRuntime(runtime::rt_case, 24);
        enc.alu(Xor, EDX, EDX);
    }

    ThavaType stringMethod(StrMethodNode* node) {
        expectString(node->base.get());
        if (node->method == "strip") {
            pushAllocationContext();
            enc.push(EAX);
            callRuntime(runtime::rt_strip, 12);
            enc.alu(Xor, EDX, EDX);
        } else convertCase(node->method);
        return ThavaType::STRING;
    }

    void readString(ASTNode* prompt) {
        readUsed = true;
        print(expression(prompt));
        enc.call(readLineLabel);
        enc.alu(Xor, EDX, EDX);
    }

    void parseScalar(ThavaType type) {
        if (type == ThavaType::STRING) return;
        uint32_t routine;
        if (type == ThavaType::INT) routine = runtime::rt_parse_int;
        else if (type == ThavaType::BOOL) routine = runtime::rt_parse_bool;
        else if (floating(type)) routine = runtime::rt_parse_float;
        else unsupported("Console input of this type");
        enc.push(EAX);
        callRuntime(routine, 4);
        if (type == ThavaType::BOOL) enc.alu(Xor, EDX, EDX);
        if (type == ThavaType::FLOAT) floatCast(2);
    }

    ThavaType substring(ASTNode* base, ASTNode* first, ASTNode* last, bool loaded = false) {
        const uint32_t bytes = last ? 20 : 12;
        pushAllocationContext();
        enc.alu(Sub, ESP, bytes);
        if (!loaded) expectString(base);
        enc.store(ESP, 0, EAX);
        if (expression(first) != ThavaType::INT) unsupported("Non-integer string index");
        enc.store(ESP, 4, EAX);
        enc.store(ESP, 8, EDX);
        if (last) {
            if (expression(last) != ThavaType::INT) unsupported("Non-integer substring bound");
            enc.store(ESP, 12, EAX);
            enc.store(ESP, 16, EDX);
        }
        callRuntime(last ? runtime::rt_substring : runtime::rt_str_index, bytes + 8);
        enc.alu(Xor, EDX, EDX);
        return ThavaType::STRING;
    }

    ThavaType stringSearch(StrAdvMethodNode* node) {
        if (node->method == "join" || node->method == "split") {
            if (node->args.size() != 1) unsupported("Arguments to '" + node->method + "'");
            const bool join = node->method == "join";
            pushAllocationContext();
            enc.alu(Sub, ESP, 8);
            expectString(node->base.get());
            enc.store(ESP, 0, EAX);
            const auto arg = expression(node->args[0].get());
            if (arg == ThavaType::UNKNOWN) normalize(join ? ThavaType::LIST : ThavaType::STRING, "", arg);
            else if (join ? !collection(arg) || arg == ThavaType::DICT : arg != ThavaType::STRING)
                unsupported(join ? "Joining this collection" : "A non-string split delimiter");
            enc.store(ESP, 4, EAX);
            callRuntime(join ? runtime::rt_join : runtime::rt_split, 16);
            enc.alu(Xor, EDX, EDX);
            return join ? ThavaType::STRING : ThavaType::LIST;
        }
        const bool replace = node->method == "replace";
        if (node->args.size() != (replace ? 2u : 1u)) unsupported("Arguments to '" + node->method + "'");
        if (replace) pushAllocationContext();
        const uint32_t slots = replace ? 12 : 8;
        enc.alu(Sub, ESP, slots);
        expectString(node->base.get());
        enc.store(ESP, 0, EAX);
        for (size_t i = 0; i < node->args.size(); ++i) {
            expectString(node->args[i].get());
            enc.store(ESP, static_cast<int32_t>(4 * (i + 1)), EAX);
        }
        if (replace) {
            callRuntime(runtime::rt_replace, 20);
            enc.alu(Xor, EDX, EDX);
            return ThavaType::STRING;
        }
        static const std::map<std::string, uint32_t> search = {
            {"find",runtime::rt_find}, {"index",runtime::rt_find}, {"count",runtime::rt_count},
            {"startswith",runtime::rt_startswith}, {"endswith",runtime::rt_endswith}
        };
        auto fn = search.find(node->method);
        if (fn == search.end()) unsupported("String method '" + node->method + "'");
        callRuntime(fn->second, 8);
        enc.signExtend();
        return node->method == "startswith" || node->method == "endswith" ? ThavaType::BOOL : ThavaType::INT;
    }

    void formatScalar(ThavaType type) {
        if (type == ThavaType::STRING || type == ThavaType::DECIMAL) return;
        if (type == ThavaType::BOOL) {
            const auto isTrue = enc.label(), done = enc.label();
            enc.alu(Cmp, EAX, 0);
            enc.jump(NotEqual, isTrue);
            stringValue("false");
            enc.jump(done);
            enc.bind(isTrue);
            stringValue("true");
            enc.bind(done);
            return;
        }
        if (collection(type) || type == ThavaType::UNKNOWN) {
            pushAllocationContext();
            if (type != ThavaType::UNKNOWN) enc.pushImm(type == ThavaType::DICT);
            enc.push(EAX);
            callRuntime(type == ThavaType::UNKNOWN ? runtime::rt_value_format : runtime::rt_collection_format,
                        type == ThavaType::UNKNOWN ? 12 : 16);
            enc.alu(Xor, EDX, EDX);
            return;
        }
        if (type == ThavaType::NONE) {
            stringValue("none");
            return;
        }
        if (type == ThavaType::CUSTOM || type == ThavaType::FUNC) {
            pushAllocationContext();
            enc.alu(Sub, ESP, 12);
            storeTagged(type, 0);
            callRuntime(runtime::rt_value_new, 20);
            formatScalar(ThavaType::UNKNOWN);
            return;
        }
        if (type != ThavaType::INT && !floating(type)) unsupported("Formatting this type");
        pushAllocationContext();
        saveValue();
        callRuntime(floating(type) ? runtime::rt_format_float : runtime::rt_format_int, 16);
        enc.alu(Xor, EDX, EDX);
    }

    void storeArgument(int32_t offset) {
        enc.store(ESP, offset, EAX);
        enc.store(ESP, offset + 4, EDX);
    }

    // Dynamic expressions carry a pointer to a snapshot of a tagged collection value.
    void storeTagged(ThavaType type, int32_t offset) {
        if (type == ThavaType::UNKNOWN) {
            enc.load(ECX, EAX, 8);
            enc.load(EDX, EAX, 4);
            enc.load(EAX, EAX);
        } else {
            enc.imm(ECX, valueKind(type));
            if (type == ThavaType::CUSTOM || type == ThavaType::FUNC) {
                const auto value = enc.label();
                enc.alu(Cmp, EAX, 0);
                enc.jump(NotEqual, value);
                enc.imm(ECX, VALUE_NONE);
                enc.bind(value);
            }
        }
        storeArgument(offset);
        enc.store(ESP, offset + 8, ECX);
    }

    void requireValue(const char* error) {
        const auto valid = enc.label();
        enc.alu(Cmp, EAX, 0);
        enc.jump(NotEqual, valid);
        LiteralNode message(error, ThavaType::STRING);
        raiseException(&message);
        enc.bind(valid);
        enc.alu(Xor, EDX, EDX);
    }

    ThavaType emitValueBinary(const std::string& op, ThavaType left, ThavaType right) {
        static const std::map<std::string, uint32_t> operations = {
            {"+", VALUE_ADD}, {"-", VALUE_SUB}, {"*", VALUE_MUL}, {"/", VALUE_DIV},
            {"//", VALUE_IDIV}, {"%", VALUE_MOD}, {"**", VALUE_POW}, {"==", VALUE_EQ},
            {"!=", VALUE_NE}, {"<", VALUE_LT}, {"<=", VALUE_LE}, {">", VALUE_GT}, {">=", VALUE_GE},
            {"band", VALUE_AND}, {"bor", VALUE_OR}, {"bxor", VALUE_XOR}, {"bshl", VALUE_SHL}, {"bshr", VALUE_SHR},
        };
        const auto entry = operations.find(op);
        if (entry == operations.end()) unsupported("Collection element operator '" + op + "'");
        pushAllocationContext();
        enc.alu(Sub, ESP, 28);
        storeTagged(right, 12);
        enc.load(EAX, ESP, 36);
        enc.load(EDX, ESP, 40);
        storeTagged(left, 0);
        enc.imm(EAX, entry->second);
        enc.store(ESP, 24, EAX);
        callRuntime(runtime::rt_value_binary, 36);
        enc.alu(Add, ESP, 8);
        requireValue("Invalid operation on a collection value");
        if (!comparison(op)) return ThavaType::UNKNOWN;
        normalize(ThavaType::BOOL, "", ThavaType::UNKNOWN);
        return ThavaType::BOOL;
    }

    ThavaType collectionLiteral(CollectionNode* node) {
        const auto type = node->getIsTuple() ? ThavaType::TUPLE : ThavaType::LIST;
        defaultValue(type);
        enc.push(EAX);
        uint32_t index = 0;
        for (const auto& value : node->getElements()) {
            pushAllocationContext();
            enc.alu(Sub, ESP, 24);
            enc.load(EAX, ESP, 32);
            enc.store(ESP, 0, EAX);
            integer(index++);
            storeArgument(4);
            const auto element = expression(value.get());
            storeTagged(element, 12);
            callRuntime(runtime::rt_list_insert, 32);
        }
        enc.pop(EAX);
        enc.alu(Xor, EDX, EDX);
        return type;
    }

    ThavaType dictLiteral(DictNode* node) {
        defaultValue(ThavaType::DICT);
        enc.push(EAX);
        for (const auto& entry : node->entries) {
            pushAllocationContext();
            enc.alu(Sub, ESP, 28);
            enc.load(EAX, ESP, 36);
            enc.store(ESP, 0, EAX);
            const auto key = expression(entry.first.get());
            storeTagged(key, 4);
            const auto value = expression(entry.second.get());
            storeTagged(value, 16);
            callRuntime(runtime::rt_dict_set, 36);
        }
        enc.pop(EAX);
        enc.alu(Xor, EDX, EDX);
        return ThavaType::DICT;
    }

    ThavaType collectionGet(ThavaType type, ASTNode* key, bool missingOk = false, ASTNode* fallback = nullptr) {
        pushAllocationContext();
        enc.alu(Sub, ESP, 40);
        storeTagged(type, 0);
        const auto keyType = expression(key);
        if (collection(type) && type != ThavaType::DICT && keyType != ThavaType::INT && keyType != ThavaType::UNKNOWN)
            unsupported("A non-integer list index");
        storeTagged(keyType, 12);
        if (fallback) storeTagged(expression(fallback), 24);
        else {
            integer(0);
            storeTagged(ThavaType::NONE, 24);
        }
        enc.imm(EAX, missingOk);
        enc.store(ESP, 36, EAX);
        callRuntime(runtime::rt_value_index, 48);
        requireValue("Invalid collection index or missing key");
        return ThavaType::UNKNOWN;
    }

    void mutateCollection(Slot& slot, ASTNode* key, ASTNode* value, bool insert, const std::string& op = "=") {
        const bool dict = slot.type == ThavaType::DICT;
        if (slot.type != ThavaType::LIST && slot.type != ThavaType::MULTYPE && !dict)
            unsupported("Mutating this collection type");
        pushAllocationContext();
        enc.alu(Sub, ESP, 32);
        loadValue(slot);
        enc.store(ESP, 0, EAX);
        auto keyType = ThavaType::INT;
        if (key) keyType = expression(key);
        else integer(0x7fffffffffffffffULL);
        if (!dict && keyType != ThavaType::INT && keyType != ThavaType::UNKNOWN) unsupported("A non-integer list index");
        storeTagged(keyType, 4);
        if (op != "=") {
            // Reuse the staged receiver and key without evaluating either again.
            pushAllocationContext();
            enc.alu(Sub, ESP, 20);
            for (int32_t offset = 0; offset < 16; offset += 4) {
                enc.load(EAX, ESP, 28 + offset);
                enc.store(ESP, offset, EAX);
            }
            enc.imm(EAX, dict);
            enc.store(ESP, 16, EAX);
            callRuntime(runtime::rt_collection_current, 28);
            requireValue("Invalid collection index or missing key");
            saveValue();
        }
        auto element = expression(value);
        if (op != "=") element = emitBinary(op.substr(0, op.size() - 1), ThavaType::UNKNOWN, element);
        storeTagged(element, 16);
        enc.imm(EAX, dict ? COLLECTION_DICT_SET : insert ? COLLECTION_INSERT : COLLECTION_SET);
        enc.store(ESP, 28, EAX);
        callRuntime(runtime::rt_collection_write, 40);
    }

    static ASTNode* unwrapListIndex(ASTNode* node) {
        auto sub = dynamic_cast<CollectionNode*>(node);
        return sub && !sub->getIsTuple() && sub->getElements().size() == 1
            ? sub->getElements().front().get() : nullptr;
    }

    ThavaType listMethod(ListMethodNode* node) {
        auto& slot = lookup(node->varName);
        if (slot.type != ThavaType::LIST && slot.type != ThavaType::MULTYPE)
            unsupported("Mutating this collection type");
        const auto& method = node->method;
        if (method == "addval" || method == "assign") {
            const bool insert = method == "addval";
            const auto size = node->args.size();
            if (insert && size == 1) mutateCollection(slot, nullptr, node->args[0].get(), true);
            else if (size == 2) {
                auto* key = unwrapListIndex(node->args[0].get());
                if (!key) unsupported("A list method index without [index]");
                mutateCollection(slot, key, node->args[1].get(), insert);
            } else unsupported("Arguments to list method '" + method + "'");
        } else if (method == "delval" && node->args.empty()) {
            loadValue(slot);
            enc.push(EAX);
            callRuntime(runtime::rt_collection_clear, 4);
        } else if (method == "removeval" && node->args.size() == 1) {
            if (auto* index = unwrapListIndex(node->args[0].get())) {
                enc.alu(Sub, ESP, 12);
                loadValue(slot);
                enc.store(ESP, 0, EAX);
                const auto type = expression(index);
                if (type != ThavaType::INT && type != ThavaType::UNKNOWN) unsupported("A non-integer list index");
                if (type == ThavaType::UNKNOWN) normalize(ThavaType::INT, "int64", type);
                storeArgument(4);
                callRuntime(runtime::rt_list_remove_at, 12);
            } else {
                enc.alu(Sub, ESP, 16);
                loadValue(slot);
                enc.store(ESP, 0, EAX);
                storeTagged(expression(node->args[0].get()), 4);
                callRuntime(runtime::rt_list_remove_value, 16);
            }
        } else unsupported("List method '" + method + "'");
        return ThavaType::VOID;
    }

    ThavaType dictMethod(DictMethodNode* node) {
        const auto base = expression(node->base.get());
        if (base == ThavaType::UNKNOWN) normalize(ThavaType::DICT, "", base);
        else if (base != ThavaType::DICT) unsupported("A non-dictionary receiver");
        const auto& method = node->method;
        if (method == "get" && (node->args.size() == 1 || node->args.size() == 2))
            return collectionGet(ThavaType::DICT, node->args[0].get(), true,
                                 node->args.size() == 2 ? node->args[1].get() : nullptr);
        if ((method == "keys" || method == "values") && node->args.empty()) {
            pushAllocationContext();
            enc.pushImm(method == "keys");
            enc.push(EAX);
            callRuntime(runtime::rt_dict_project, 16);
            enc.alu(Xor, EDX, EDX);
            return ThavaType::LIST;
        }
        if (method == "clear" && node->args.empty()) {
            enc.push(EAX);
            callRuntime(runtime::rt_collection_clear, 4);
            return ThavaType::VOID;
        }
        const bool has = method == "has" || method == "contains";
        if ((has || method == "remove" || method == "del") && node->args.size() == 1) {
            enc.alu(Sub, ESP, 16);
            enc.store(ESP, 0, EAX);
            storeTagged(expression(node->args[0].get()), 4);
            callRuntime(has ? runtime::rt_dict_has : runtime::rt_dict_remove, 16);
            enc.alu(Xor, EDX, EDX);
            return has ? ThavaType::BOOL : ThavaType::VOID;
        }
        unsupported("Dictionary method '" + method + "'");
    }

    ThavaType membership(InNode* node) {
        enc.alu(Sub, ESP, 24);
        storeTagged(expression(node->left.get()), 0);
        storeTagged(expression(node->listExpr.get()), 12);
        callRuntime(runtime::rt_value_contains, 24);
        enc.alu(Xor, EDX, EDX);
        return ThavaType::BOOL;
    }

    const Slot& file(const std::string& name) {
        for (auto it = activeFiles.rbegin(); it != activeFiles.rend(); ++it)
            if (it->name == name) return *it->slot;
        unsupported("An unbound file handle '" + name + "'");
    }

    ThavaType fileMethod(FileMethodNode* node) {
        const auto& slot = file(node->varName);
        const auto& method = node->method;
        if ((method == "write" || method == "write_line" || method == "writeline") &&
            node->args.size() == 1) {
            const auto type = expression(node->args[0].get());
            formatScalar(type);
            enc.push(EBX);
            codeAddress(EDI, fileWriteLabel);
            enc.push(EDI);
            enc.pushImm(method != "write");
            enc.push(EAX);
            loadValue(slot);
            enc.push(EAX);
            callRuntime(runtime::rt_file_write, 20);
            return ThavaType::VOID;
        }
        if (node->args.empty() && (method == "read" || method == "read_line" || method == "readline" ||
                                   method == "read_lines" || method == "readlines")) {
            pushAllocationContext();
            codeAddress(EDI, fileReadLabel);
            enc.push(EDI);
            const bool lines = method == "read_lines" || method == "readlines";
            if (!lines) enc.pushImm(method != "read");
            loadValue(slot);
            enc.push(EAX);
            callRuntime(lines ? runtime::rt_file_lines : runtime::rt_file_read, lines ? 16 : 20);
            enc.alu(Xor, EDX, EDX);
            return lines ? ThavaType::LIST : ThavaType::STRING;
        }
        unsupported("File method '" + method + "'");
    }

    ThavaType randomCall(RandCallNode* node) {
        if (randomState == UINT32_MAX) randomState = addSlot();
        if (node->funcName == "rand" || node->funcName == "randint") {
            const bool real = node->funcName == "rand";
            const auto type = real ? ThavaType::DOUBLE : ThavaType::INT;
            if (!node->args.empty() && node->args.size() != 2)
                unsupported("Random bounds other than a pair");
            enc.alu(Sub, ESP, 20);
            if (node->args.empty()) {
                integer(0);
                storeArgument(0);
                integer(real ? 0x3ff0000000000000ULL : 10ULL);
                storeArgument(8);
            } else {
                for (size_t i = 0; i < 2; ++i) {
                    normalize(type, "int64", expression(node->args[i].get()));
                    storeArgument(int32_t(8 * i));
                }
            }
            dataAddress(EAX, randomState);
            enc.store(ESP, 16, EAX);
            callRuntime(real ? runtime::rt_randfloat : runtime::rt_randint, 20);
            return type;
        }

        if (node->funcName != "randres" || node->args.size() != 1)
            unsupported("This randres invocation");
        pushAllocationContext();
        enc.alu(Sub, ESP, 24);
        const auto type = expression(node->args[0].get());
        if (type == ThavaType::UNKNOWN) normalize(ThavaType::LIST, "", type);
        else if (!collection(type) || type == ThavaType::DICT) unsupported("Choosing from this type");
        enc.store(ESP, 0, EAX);
        if (node->weightExpr) {
            const auto weights = expression(node->weightExpr.get());
            if (weights == ThavaType::UNKNOWN) normalize(ThavaType::LIST, "", weights);
            else if (!collection(weights) || weights == ThavaType::DICT) unsupported("Non-collection weights");
        } else integer(0);
        enc.store(ESP, 4, EAX);
        integer(0);
        storeTagged(ThavaType::INT, 8);
        dataAddress(EAX, randomState);
        enc.store(ESP, 20, EAX);
        callRuntime(runtime::rt_randres, 32);
        requireValue("Cannot allocate a collection value");
        return ThavaType::UNKNOWN;
    }

    void loadOsImport(const char* library, const char* name, int32_t offset) {
        pe.addImport(library, name);
        enc.load(EAX, EBX);
        importFixups.emplace_back(enc.code.size() - 4, name);
        enc.store(ESP, offset, EAX);
    }

    void beginOsCall(OsOperation operation) {
        pushAllocationContext();
        enc.alu(Sub, ESP, 32);
        enc.imm(EAX, operation);
        enc.store(ESP, 0, EAX);
        enc.alu(Xor, EAX, EAX);
        for (int offset = 4; offset < 32; offset += 4) enc.store(ESP, offset, EAX);
    }

    void finishOsCall(const char* library, const char* first = nullptr) {
        if (windows && first) loadOsImport(library, first, 20);
        else if (!windows) {
            dataAddress(EAX, environmentSlot);
            enc.store(ESP, 20, EAX);
            if (!linuxPlatformSlot) linuxPlatformSlot = addSlot();
            dataAddress(EAX, linuxPlatformSlot);
            enc.store(ESP, 24, EAX);
        }
        callRuntime(windows ? runtime::rt_windows_os : runtime::rt_linux_os, 40);
    }

    void emitSleep(const std::vector<ASTPtr>& args) {
        if (args.size() > 1) unsupported("Sleep with more than one argument");
        beginOsCall(OS_SLEEP);
        if (args.empty()) integer(0);
        else normalize(ThavaType::INT, "int64", expression(args[0].get()));
        storeArgument(4);
        finishOsCall("kernel32.dll", "Sleep");
    }

    ThavaType osMethod(OsMethodNode* node) {
        if (node->method == "name") {
            stringValue(windows ? "windows" : "linux");
            return ThavaType::STRING;
        }
        if (node->method == "sleep") {
            if (node->args.empty()) unsupported("os.sleep without a duration");
            emitSleep(node->args);
            return ThavaType::INT;
        }
        struct Operation { OsOperation operation; unsigned count; const char* api; bool crt = false; };
        static const std::map<std::string, Operation> methods = {
            {"getpid", {OS_PID, 0, "GetCurrentProcessId"}},
            {"cpu_count", {OS_CPU, 0, "GetActiveProcessorCount"}},
            {"getcwd", {OS_CWD, 0, "GetCurrentDirectoryA"}},
            {"chdir", {OS_CHDIR, 1, "_chdir", true}},
            {"mkdir", {OS_MKDIR, 1, "_mkdir", true}},
            {"rmdir", {OS_RMDIR, 1, "_rmdir", true}},
            {"remove", {OS_REMOVE, 1, "remove", true}},
            {"rename", {OS_RENAME, 2, "rename", true}},
            {"exists", {OS_EXISTS, 1, "GetFileAttributesExA"}},
            {"is_file", {OS_IS_FILE, 1, "GetFileAttributesExA"}},
            {"is_dir", {OS_IS_DIR, 1, "GetFileAttributesExA"}},
            {"getsize", {OS_SIZE, 1, "GetFileAttributesExA"}},
            {"listdir", {OS_LIST, 1, "FindFirstFileA"}},
            {"getenv", {OS_ENV, 1, "getenv", true}},
            {"setenv", {OS_SETENV, 2, "_putenv", true}},
            {"system", {OS_SYSTEM, 1, "system", true}},
            {"showmessagebox", {OS_MESSAGE, 4, "MessageBoxA"}},
        };
        auto found = methods.find(node->method);
        if (found == methods.end()) unsupported("OS method '" + node->method + "'");
        const auto& method = found->second;
        const bool defaultDirectory = method.operation == OS_LIST && node->args.empty();
        if (node->args.size() != method.count && !defaultDirectory)
            unsupported("Arguments to os." + node->method);
        beginOsCall(method.operation);
        if (defaultDirectory) {
            stringValue(".");
            enc.store(ESP, 4, EAX);
        }
        for (size_t i = 0; i < node->args.size(); ++i) {
            expectString(node->args[i].get());
            enc.store(ESP, static_cast<int32_t>(4 + 4 * i), EAX);
        }
        if (windows && method.operation == OS_LIST) {
            loadOsImport("kernel32.dll", "FindNextFileA", 24);
            loadOsImport("kernel32.dll", "FindClose", 28);
        }
        const char* library = method.crt ? "msvcrt.dll" :
                              method.operation == OS_MESSAGE ? "user32.dll" : "kernel32.dll";
        finishOsCall(library, method.api);
        return node->getType(nullptr);
    }

    ThavaType timeMethod(TimeMethodNode* node) {
        const auto& method = node->method;
        if (method == "sleep") {
            emitSleep(node->args);
            return ThavaType::INT;
        }
        if (method == "timestamp") {
            if (!node->args.empty()) unsupported("Arguments to time.timestamp");
            beginOsCall(TIME_STAMP);
            finishOsCall("kernel32.dll", "GetSystemTimeAsFileTime");
            return ThavaType::INT;
        }
        if (method == "now" || method == "date") {
            if (node->args.size() > 1) unsupported("Arguments to time." + method);
            beginOsCall(TIME_FORMAT);
            if (node->args.empty()) stringValue(method == "date" ? "date" : "datetime");
            else {
                expectString(node->args[0].get());
                const auto haveFormat = enc.label();
                enc.loadByte(ECX, EAX);
                enc.alu(Cmp, ECX, 0);
                enc.jump(NotEqual, haveFormat);
                stringValue(method == "date" ? "date" : "datetime");
                enc.bind(haveFormat);
            }
            enc.store(ESP, 4, EAX);
            finishOsCall("kernel32.dll", "GetLocalTime");
            return ThavaType::STRING;
        }
        static const std::map<std::string, uint32_t> fields = {
            {"year",0}, {"month",1}, {"day",2}, {"hour",3}, {"minute",4}, {"second",5}, {"millis",6}
        };
        auto field = fields.find(method);
        if (field == fields.end() || !node->args.empty()) unsupported("Time method '" + method + "'");
        beginOsCall(TIME_FIELD);
        enc.imm(EAX, field->second);
        enc.store(ESP, 4, EAX);
        finishOsCall("kernel32.dll", "GetLocalTime");
        return ThavaType::INT;
    }

    const ClassLayout& classLayout(const std::string& name) const {
        const auto found = classes.find(name);
        if (found == classes.end()) unsupported("Custom type '" + name + "'");
        return found->second;
    }

    bool derivesFrom(std::string type, const std::string& base) const {
        while (!type.empty() && type != base) {
            const auto found = classes.find(type);
            if (found == classes.end()) return false;
            type = found->second.decl->baseClassName;
        }
        return type == base;
    }

    const ObjectLayout& objectLayout(const std::string& name) const {
        const auto found = structs.find(name);
        if (found != structs.end()) return found->second;
        return classLayout(name);
    }

    const Function* constructor(const ClassLayout& layout, const std::vector<ASTPtr>& args) {
        std::vector<const Function*> candidates;
        for (const auto& candidate : layout.constructors)
            if (candidate.params.size() == args.size()) candidates.push_back(&candidate);
        if (candidates.size() < 2) return candidates.empty() ? nullptr : candidates.front();

        auto env = std::make_shared<Environment>(g_programEnv);
        for (const auto& [name, slot] : locals) env->define(name, slot.type, slot.width);
        if (currentFunction) {
            env->currentClassName = currentFunction->owner;
            if (!currentFunction->owner.empty()) env->currentBaseClassName = classLayout(currentFunction->owner).decl->baseClassName;
        }
        std::vector<std::pair<ThavaType, std::string>> types;
        for (const auto& arg : args) {
            const auto type = arg->getType(env);
            std::string name;
            if (auto var = dynamic_cast<VarNode*>(arg.get())) name = lookup(var->getName()).width;
            else if (auto cast = dynamic_cast<CastNode*>(arg.get())) name = cast->customTarget;
            else if (type == ThavaType::CUSTOM) name = resolveTargetClass(arg.get(), env);
            else if (auto lit = dynamic_cast<LiteralNode*>(arg.get()); lit && type == ThavaType::INT) {
                const auto value = std::stoll(lit->getVal());
                if (value < INT32_MIN || value > INT32_MAX) name = "int64";
            }
            types.emplace_back(type, std::move(name));
        }
        const Function* result = nullptr;
        size_t best = SIZE_MAX;
        bool tied = false;
        for (const auto* candidate : candidates) {
            size_t score = 0;
            for (size_t i = 0; i < types.size(); ++i) {
                const auto& [actual, name] = types[i];
                const auto formal = candidate->params[i].first;
                const auto width = i < candidate->paramCustomTypes.size() ? candidate->paramCustomTypes[i] : "";
                if (actual == ThavaType::UNKNOWN || actual == ThavaType::NONE) score += 3;
                else if (actual == formal && (name == width || (actual != ThavaType::INT && actual != ThavaType::CUSTOM))) continue;
                else if ((actual == formal && actual == ThavaType::INT) ||
                         (actual == ThavaType::CUSTOM && formal == actual && derivesFrom(name, width))) ++score;
                else if ((actual == ThavaType::INT || floating(actual)) && (formal == ThavaType::INT || floating(formal))) score += 2;
                else { score = SIZE_MAX; break; }
            }
            if (score < best) { result = candidate; best = score; tied = false; }
            else if (score == best) tied = true;
        }
        if (!result || tied) unsupported("Ambiguous or incompatible constructor arguments for '" + layout.decl->className + "'");
        return result;
    }

    const Function& method(const std::string& name, const std::vector<ASTPtr>& args) {
        for (std::string owner = lastCustomType; !owner.empty();) {
            const auto& layout = classLayout(owner);
            if (name == "init") {
                if (const auto* init = constructor(layout, args)) return *init;
            } else {
                const auto found = layout.methods.find(name);
                if (found != layout.methods.end()) return found->second;
            }
            owner = layout.decl->baseClassName;
        }
        unsupported("Method '" + lastCustomType + "::" + name + "'");
    }

    static std::string moduleTypeName(ThavaType type, const std::string& width) {
        if (type == ThavaType::CUSTOM) return "custom:" + width;
        return type == ThavaType::INT && !width.empty() ? width : typeToString(type);
    }

    static std::string methodSignature(const std::string& name, const Function& fn) {
        std::string signature = name + ":" + moduleTypeName(fn.returnType, fn.customReturnType);
        for (size_t i = 0; i < fn.params.size(); ++i)
            signature += "," + moduleTypeName(fn.params[i].first, i < fn.paramCustomTypes.size() ? fn.paramCustomTypes[i] : "");
        return signature;
    }

    void callMethod(const Function& callee, const std::string& methodName) {
        enc.load(EAX, ESP);
        dataAddress(ECX, addString(methodSignature(methodName, callee)));
        enc.push(ECX);
        enc.push(EAX);
        callRuntime(runtime::rt_object_method, 8);
        requireValue("Cannot dispatch method on this object");
        enc.callReg(EAX);
    }

    void requireObject() {
        requireValue("Cannot access a member of none");
    }

    size_t bridge(const Function& fn, bool boxed) {
        auto& entries = boxed ? boxedBridges : methodBridges;
        const auto found = entries.find(fn.entry);
        if (found != entries.end()) return found->second.second;
        const auto entry = enc.label();
        entries.emplace(fn.entry, std::make_pair(fn, entry));
        return entry;
    }

    Function importSignature(const Import& importer) const {
        const auto& info = *importer.info;
        const auto& library = g_dlLibraryInfos.at(info.libPath);
        validateImportedObject(library, info.retType, info.returnWidth);
        for (size_t i = 0; i < info.paramTypes.size(); ++i)
            validateImportedObject(library, info.paramTypes[i], info.paramWidths[i]);
        return {info.name, "", info.retType, info.returnWidth, importer.params,
                info.paramWidths, emptyBody, importer.entry};
    }

    // A boxed call passes 16 bytes per argument: Value followed by an optional lvalue address.
    ThavaType boxedCall(const std::vector<ASTPtr>& args) {
        if (args.size() > 254) unsupported("A callback frame larger than 4080 bytes");
        const int32_t bytes = static_cast<int32_t>(16 * args.size());
        saveValue(); // entry and optional receiver
        if (bytes) enc.alu(Sub, ESP, bytes);
        for (size_t i = 0; i < args.size(); ++i) {
            const int32_t offset = static_cast<int32_t>(i * 16);
            const auto type = expression(args[i].get());
            storeTagged(type, offset);
            const auto* variable = dynamic_cast<VarNode*>(args[i].get());
            if (collection(type) && variable) slotAddress(lookup(variable->getName()), EAX);
            else enc.alu(Xor, EAX, EAX);
            enc.store(ESP, offset + 12, EAX);
        }
        enc.lea(EAX, ESP, 0);
        enc.load(ECX, ESP, bytes);
        enc.load(EDX, ESP, bytes + 4);
        enc.push(EDX);
        enc.pushImm(static_cast<uint32_t>(args.size()));
        enc.push(EAX);
        enc.callReg(ECX);
        enc.alu(Add, ESP, bytes + 20);
        requireValue("Cannot allocate callback result");
        return ThavaType::UNKNOWN;
    }

    const Slot& objectField(ThavaType type, const std::string& name) {
        if (type != ThavaType::CUSTOM) unsupported("A member target without fields");
        const auto& layout = objectLayout(lastCustomType);
        const auto& fields = layout.fields;
        const auto found = fields.find(name);
        if (found == fields.end()) unsupported("Field '" + lastCustomType + "::" + name + "'");
        requireObject();
        const auto compatible = enc.label();
        enc.load(EDX, EAX);
        dataAddress(ECX, layout.descriptor);
        enc.alu(Cmp, EDX, ECX);
        enc.jump(Equal, compatible);
        enc.push(ECX);
        enc.push(EAX);
        callRuntime(runtime::rt_object_as, 8);
        requireValue("Object layout does not match the member declaration");
        enc.bind(compatible);
        return found->second;
    }

    const Slot& field(ASTNode* target, const std::string& name) {
        return objectField(expression(target), name);
    }

    ThavaType callFunction(const Function& callee, const std::vector<ASTPtr>& args, const std::string& virtualMethod = "") {
        if (callee.params.size() != args.size()) unsupported("Arguments to '" + callee.name + "'");
        const size_t receiver = callee.owner.empty() ? 0 : 8;
        const bool references = std::any_of(callee.params.begin(), callee.params.end(),
                                            [](const auto& param) { return collection(param.first); });
        const auto bytes = receiver + (references ? 16 : 8) * args.size();
        if (bytes > 4080) unsupported("A call frame larger than 4080 bytes");
        if (bytes) enc.alu(Sub, ESP, uint32_t(bytes));
        if (receiver) {
            enc.store(ESP, 0, EAX);
            enc.store(ESP, 4, EDX);
        }
        for (size_t i = 0; i < args.size(); ++i) {
            const auto width = i < callee.paramCustomTypes.size() ? callee.paramCustomTypes[i] : "";
            const auto formal = callee.params[i].first;
            const auto offset = int32_t(receiver + 8 * i);
            const auto* member = dynamic_cast<MemberAccessNode*>(args[i].get());
            if (collection(formal) && member) {
                const auto& slot = field(member->target.get(), member->memberName);
                if (!collection(slot.type)) unsupported("A non-collection field argument");
                normalize(formal, width, slot.type);
                enc.lea(EAX, EAX, slot.offset);
                enc.store(ESP, offset, EAX);
                continue;
            }
            const auto actual = expression(args[i].get());
            if (collection(formal)) {
                if (auto var = dynamic_cast<VarNode*>(args[i].get()); var && collection(actual)) {
                    normalize(formal, width, actual);
                    slotAddress(lookup(var->getName()), EAX);
                }
                else {
                    const auto temporary = int32_t(receiver + 8 * (args.size() + i));
                    storeValue({ESP, temporary, formal, width}, actual);
                    enc.lea(EAX, ESP, temporary);
                }
                enc.store(ESP, offset, EAX);
            } else storeValue({ESP, offset, formal, width}, actual);
        }
        if (virtualMethod.empty()) enc.call(callee.entry);
        else callMethod(callee, virtualMethod);
        if (bytes) enc.alu(Add, ESP, uint32_t(bytes));
        lastCustomType = callee.returnType == ThavaType::CUSTOM ? callee.customReturnType : "";
        return callee.returnType;
    }

    void allocateObject(const ObjectLayout& layout) {
        allocatorUsed = true;
        enc.push(EBX);
        enc.pushImm(std::max(layout.size, int32_t(8)));
        enc.call(allocateLabel);
        enc.alu(Add, ESP, 8);
        const auto allocated = enc.label();
        enc.alu(Cmp, EAX, 0);
        enc.jump(NotEqual, allocated);
        LiteralNode message("Cannot allocate object", ThavaType::STRING);
        raiseException(&message);
        enc.bind(allocated);
        dataAddress(ECX, layout.descriptor);
        enc.store(EAX, 0, ECX);
        enc.alu(Xor, EDX, EDX);
    }

    ThavaType instantiate(const ClassInstantiateNode& instance) {
        const auto& layout = classLayout(instance.className);
        const auto* init = constructor(layout, instance.args);
        if (layout.constructors.empty()) {
            for (auto base = layout.decl->baseClassName; !init && !base.empty();) {
                const auto& parent = classLayout(base);
                init = constructor(parent, instance.args);
                base = parent.decl->baseClassName;
            }
        }
        if (!init && (!layout.constructors.empty() || !instance.args.empty()))
            unsupported("Constructor arguments for '" + instance.className + "'");
        allocateObject(layout);
        saveValue();
        enc.call(layout.initializer);
        if (init) {
            enc.load(EAX, ESP);
            enc.load(EDX, ESP, 4);
            callFunction(*init, instance.args);
        }
        enc.pop(EAX);
        enc.pop(EDX);
        lastCustomType = instance.className;
        return ThavaType::CUSTOM;
    }

    ThavaType instantiateStruct(const StructLayout& layout, const std::vector<ASTPtr>& args) {
        const auto& fields = layout.decl->fields;
        if (args.size() > fields.size()) unsupported("Too many values for struct '" + layout.decl->structName + "'");

        allocateObject(layout);
        // The initializer receives the object and the count of already assigned fields.
        enc.pushImm(0);
        enc.pushImm(static_cast<uint32_t>(args.size()));
        saveValue();
        for (size_t i = 0; i < args.size(); ++i) {
            const auto& slot = layout.fields.at(fields[i].name);
            normalize(slot.type, slot.width, expression(args[i].get()));
            enc.load(ECX, ESP);
            enc.store(ECX, slot.offset, EAX);
            enc.store(ECX, slot.offset + 4, EDX);
        }
        if (args.size() < fields.size()) enc.call(layout.initializer);
        enc.pop(EAX);
        enc.pop(EDX);
        enc.alu(Add, ESP, 8);
        lastCustomType = layout.decl->structName;
        return ThavaType::CUSTOM;
    }

    ThavaType emitDecimalOperation(int op, ASTNode* left, ASTNode* right = nullptr) {
        if (!decimalEntry) decimalEntry = enc.label();
        const bool comparison = decimalComparison(op);
        enc.alu(Sub, ESP, 12);
        expression(left);
        enc.store(ESP, 0, EAX);
        if (right) {
            expression(right);
            enc.store(ESP, 4, EAX);
        }
        if (!comparison) {
            allocatorUsed = true;
            enc.push(EBX);
            enc.pushImm(THAVA_DECIMAL_BUFFER);
            enc.call(allocateLabel);
            enc.alu(Add, ESP, 8);
            enc.store(ESP, 8, EAX);
        }

        enc.load(ECX, ESP, 0);
        if (right) enc.load(EDX, ESP, 4);
        else enc.alu(Xor, EDX, EDX);
        if (comparison) enc.pushImm(0);
        else {
            enc.load(EAX, ESP, 8);
            enc.push(EAX);
        }
        enc.push(EDX);
        enc.push(ECX);
        enc.pushImm(op);
        enc.call(*decimalEntry);
        enc.alu(Add, ESP, 16);

        const auto done = enc.label();
        enc.alu(Cmp, EAX, 0);
        enc.jump(Equal, done);
        if (comparison) {
            enc.alu(Cmp, EAX, 1);
            enc.jump(Equal, done);
        }
        for (const auto& error : decimalErrors) {
            const auto next = enc.label();
            enc.alu(Cmp, EAX, error.first);
            enc.jump(NotEqual, next);
            enc.alu(Add, ESP, 12);
            LiteralNode message(error.second, ThavaType::STRING);
            raiseException(&message);
            enc.bind(next);
        }
        enc.bind(done);
        if (!comparison) enc.load(EAX, ESP, 8);
        enc.alu(Add, ESP, 12);
        enc.alu(Xor, EDX, EDX);
        return comparison ? ThavaType::BOOL : ThavaType::DECIMAL;
    }

    ThavaType expression(ASTNode* node) {
        diag::Scope location(node ? node->span : SourceSpan{});
        if (auto literal = dynamic_cast<DecimalParseNode*>(node))
            return emitDecimalOperation(THAVA_DECIMAL_PARSE, literal->text.get());
        if (auto minus = dynamic_cast<UnaryMinusNode*>(node); minus && minus->decimalArithmetic)
            return emitDecimalOperation(THAVA_DECIMAL_NEGATE, minus->expr.get());
        if (auto binary = dynamic_cast<BinaryOpNode*>(node); binary && binary->decimalOp >= 0)
            return emitDecimalOperation(binary->decimalOp, binary->left.get(), binary->right.get());
        if (auto interpolation = dynamic_cast<InterpolatedStringNode*>(node)) {
            formatScalar(expression(interpolation->parts.front().get()));
            for (size_t i = 1; i < interpolation->parts.size(); ++i) {
                saveValue();
                formatScalar(expression(interpolation->parts[i].get()));
                emitBinary("+", ThavaType::STRING, ThavaType::STRING);
            }
            return ThavaType::STRING;
        }
        if (auto lit = dynamic_cast<LiteralNode*>(node)) {
            const auto type = lit->getLitType();
            const auto value = lit->getVal();
            if (type == ThavaType::STRING) stringValue(value);
            else if (type == ThavaType::INT) integer(std::stoll(value));
            else if (type == ThavaType::BOOL) integer(value == "true");
            else if (type == ThavaType::NONE) integer(0);
            else if (floating(type)) {
                const double real = type == ThavaType::FLOAT ? static_cast<double>(std::strtof(value.c_str(), nullptr)) : std::strtod(value.c_str(), nullptr);
                uint64_t bits;
                std::memcpy(&bits, &real, sizeof(bits));
                integer(bits);
            }
            else unsupported("This literal type");
            return type;
        }
        if (auto var = dynamic_cast<VarNode*>(node)) {
            if (const auto* slot = findSlot(var->getName())) {
                loadValue(*slot);
                return slot->type;
            }
            const auto fn = functions.find(var->getName());
            const auto imported = imports.find(var->getName());
            if (fn == functions.end() && imported == imports.end()) unsupported("Variable '" + var->getName() + "'");
            codeAddress(EAX, bridge(fn != functions.end() ? fn->second : importSignature(imported->second), true));
            enc.alu(Xor, EDX, EDX);
            return ThavaType::FUNC;
        }
        if (dynamic_cast<LocVarNode*>(node) || dynamic_cast<ParVarNode*>(node)) {
            const auto* parent = dynamic_cast<ParVarNode*>(node);
            const auto owner = parent ? parent->baseClassName :
                (currentFunction ? currentFunction->owner : initializerClass);
            classLayout(owner);
            loadValue({EBP, 8, ThavaType::CUSTOM, owner});
            return ThavaType::CUSTOM;
        }
        if (auto instance = dynamic_cast<ClassInstantiateNode*>(node)) return instantiate(*instance);
        if (auto instance = dynamic_cast<StructInstantiateNode*>(node))
            return instantiateStruct(structs.at(instance->structName), instance->args);
        if (auto member = dynamic_cast<MemberAccessNode*>(node)) {
            const auto type = expression(member->target.get());
            if (type == ThavaType::UNKNOWN) {
                pushAllocationContext();
                dataAddress(ECX, addString(member->memberName));
                enc.push(ECX);
                enc.push(EAX);
                callRuntime(runtime::rt_value_member, 16);
                requireValue("Cannot read this public member");
                return ThavaType::UNKNOWN;
            }
            const auto& slot = objectField(type, member->memberName);
            enc.mov(ECX, EAX);
            loadValue(slot);
            return slot.type;
        }
        if (auto call = dynamic_cast<MethodCallNode*>(node)) {
            const auto type = expression(call->target.get());
            if (type == ThavaType::UNKNOWN) {
                enc.push(EAX);
                dataAddress(ECX, addString(call->methodName));
                enc.push(ECX);
                enc.push(EAX);
                callRuntime(runtime::rt_value_method, 8);
                requireValue("Cannot call this public method");
                enc.pop(EDX);
                enc.load(EDX, EDX);
                return boxedCall(call->args);
            }
            if (type != ThavaType::CUSTOM) unsupported("A non-class method target");
            const auto& callee = method(call->methodName, call->args);
            requireObject();
            const bool direct = call->methodName == "init" || dynamic_cast<ParVarNode*>(call->target.get());
            return callFunction(callee, call->args, direct ? "" : call->methodName);
        }
        if (auto unary = dynamic_cast<UnaryMinusNode*>(node)) {
            if (auto magnitude = dynamic_cast<LiteralNode*>(unary->expr.get());
                magnitude && magnitude->getLitType() == ThavaType::INT &&
                magnitude->getVal() == "9223372036854775808") {
                integer(uint64_t(1) << 63);
                return ThavaType::INT;
            }
            const auto type = expression(unary->expr.get());
            if (type == ThavaType::UNKNOWN) {
                saveValue();
                integer(uint64_t(-1));
                return emitBinary("*", type, ThavaType::INT);
            }
            if (floating(type)) enc.alu(Xor, EDX, 0x80000000u);
            else if (type == ThavaType::INT) negate(EAX, EDX);
            else unsupported("Negating this type");
            return type;
        }
        if (auto unary = dynamic_cast<UnaryBNotNode*>(node)) {
            const auto type = expression(unary->expr.get());
            if (type == ThavaType::UNKNOWN) normalize(ThavaType::INT, "int64", type);
            else if (type != ThavaType::INT) unsupported("Non-integer bitwise NOT");
            enc.unary(2, EAX);
            enc.unary(2, EDX);
            return ThavaType::INT;
        }
        if (auto unary = dynamic_cast<UnaryNotNode*>(node)) {
            requireBoolean(expression(unary->expr.get()));
            enc.alu(Xor, EAX, 1);
            return ThavaType::BOOL;
        }
        if (auto op = dynamic_cast<BinaryOpNode*>(node)) {
            const auto left = expression(op->left.get());
            if (op->op == "and" || op->op == "or") {
                requireBoolean(left);
                const auto end = enc.label();
                enc.alu(Cmp, EAX, 0);
                enc.jump(op->op == "and" ? Equal : NotEqual, end);
                requireBoolean(expression(op->right.get()));
                enc.bind(end);
                return ThavaType::BOOL;
            }
            saveValue();
            const auto right = expression(op->right.get());
            return emitBinary(op->op, left, right);
        }
        if (auto input = dynamic_cast<InputNode*>(node)) {
            readString(input->prompt.get());
            parseScalar(input->inputType);
            return input->inputType;
        }
        if (auto input = dynamic_cast<InputCaseNode*>(node)) {
            readString(input->prompt.get());
            convertCase(input->mode);
            return ThavaType::STRING;
        }
        if (auto cast = dynamic_cast<CastNode*>(node)) {
            const auto type = expression(cast->expr.get());
            if (cast->targetType == ThavaType::STRING) formatScalar(type);
            else if (type == ThavaType::STRING && (cast->targetType == ThavaType::INT || floating(cast->targetType))) {
                parseScalar(cast->targetType);
                if (cast->targetType == ThavaType::INT)
                    normalize(ThavaType::INT, cast->customTarget, ThavaType::INT);
            } else normalize(cast->targetType, cast->customTarget, type);
            return cast->targetType;
        }
        if (auto sub = dynamic_cast<StrSubstringNode*>(node))
            return substring(sub->base.get(), sub->startExpr.get(), sub->endExpr.get());
        if (auto index = dynamic_cast<IndexNode*>(node)) {
            const auto type = expression(index->base.get());
            if (type == ThavaType::STRING) return substring(nullptr, index->indexExpr.get(), nullptr, true);
            if (collection(type) || type == ThavaType::UNKNOWN) return collectionGet(type, index->indexExpr.get());
            unsupported("Indexing this type");
        }
        if (auto literal = dynamic_cast<CollectionNode*>(node)) return collectionLiteral(literal);
        if (auto literal = dynamic_cast<DictNode*>(node)) return dictLiteral(literal);
        if (auto method = dynamic_cast<ListMethodNode*>(node)) return listMethod(method);
        if (auto method = dynamic_cast<DictMethodNode*>(node)) return dictMethod(method);
        if (auto method = dynamic_cast<StrMethodNode*>(node)) return stringMethod(method);
        if (auto method = dynamic_cast<StrAdvMethodNode*>(node)) return stringSearch(method);
        if (auto os = dynamic_cast<OsMethodNode*>(node)) return osMethod(os);
        if (auto time = dynamic_cast<TimeMethodNode*>(node)) return timeMethod(time);
        if (auto method = dynamic_cast<FileMethodNode*>(node)) return fileMethod(method);
        if (auto len = dynamic_cast<LenNode*>(node)) {
            const auto type = expression(len->arg.get());
            if (type != ThavaType::STRING && type != ThavaType::UNKNOWN && !collection(type)) unsupported("Length of this type");
            enc.push(EAX);
            callRuntime(type == ThavaType::STRING ? runtime::rt_len : type == ThavaType::UNKNOWN ?
                        runtime::rt_value_len : runtime::rt_collection_len, 4);
            enc.signExtend();
            return ThavaType::INT;
        }
        if (auto contains = dynamic_cast<InNode*>(node)) return membership(contains);
        if (auto random = dynamic_cast<RandCallNode*>(node)) return randomCall(random);
        if (auto call = dynamic_cast<CallNode*>(node)) {
            if (const auto* slot = findSlot(call->name); slot && slot->type == ThavaType::FUNC) {
                loadValue(*slot);
                requireValue("Cannot call a null function");
                return boxedCall(call->args);
            }
            const auto fn = functions.find(call->name);
            if (fn != functions.end()) return callFunction(fn->second, call->args);
            const auto imported = imports.find(call->name);
            if (imported == imports.end()) unsupported("Call to '" + call->name + "'");
            return callFunction(importSignature(imported->second), call->args);
        }
        unsupported("This expression");
    }

    void print(ThavaType type) {
        if (type == ThavaType::INT) enc.call(printIntLabel);
        else {
            formatScalar(type);
            enc.call(printStringLabel);
        }
    }

    void printText(const std::string& value) {
        if (!value.empty()) {
            stringValue(value);
            enc.call(printStringLabel);
        }
    }

    void printLine(ASTNode* node) {
        if (auto interpolation = dynamic_cast<InterpolatedStringNode*>(node)) {
            for (const auto& part : interpolation->parts) print(expression(part.get()));
        } else {
            print(expression(node));
        }
        printText("\n");
    }

    void requireBoolean(ThavaType type) {
        if (type == ThavaType::UNKNOWN) normalize(ThavaType::BOOL, "", type);
        else if (type != ThavaType::BOOL) unsupported("Non-boolean condition");
    }

    void jumpIfFalse(ASTNode* condition, size_t target) {
        requireBoolean(expression(condition));
        testValue();
        enc.jump(Equal, target);
    }

    void loop(ASTNode* cond, ASTNode* body, ASTNode* update = nullptr) {
        const auto test = enc.label(), next = update ? enc.label() : test, end = enc.label();
        enc.bind(test);
        jumpIfFalse(cond, end);
        loops.push_back({end, next, scopes.size()});
        statement(body);
        loops.pop_back();
        if (update) {
            enc.bind(next);
            statement(update);
        }
        enc.jump(test);
        enc.bind(end);
    }

    ThavaType binarySlots(const std::string& op, const Slot& left, const Slot& right) {
        loadValue(left);
        saveValue();
        loadValue(right);
        return emitBinary(op, left.type, right.type);
    }

    void numericRange(RangeForNode* node) {
        const auto& frames = loopSlots.at(node);
        auto& variable = lookup(node->varName);
        const auto& step = frames.first;
        const auto& stop = frames.second;
        storeValue(variable, expression(node->startExpr.get()));
        storeValue(step, expression(node->stepExpr.get()));
        storeValue(stop, expression(node->stopExpr.get()));
        const auto test = enc.label(), negative = enc.label(), body = enc.label();
        const auto next = enc.label(), end = enc.label();
        enc.bind(test);
        loadValue(step);
        saveValue();
        integer(0);
        emitBinary("==", step.type, ThavaType::INT);
        testValue();
        enc.jump(NotEqual, end);
        loadValue(step);
        saveValue();
        integer(0);
        emitBinary(">", step.type, ThavaType::INT);
        testValue();
        enc.jump(Equal, negative);
        binarySlots("<", variable, stop);
        enc.jump(body);
        enc.bind(negative);
        binarySlots(">", variable, stop);
        enc.bind(body);
        testValue();
        enc.jump(Equal, end);
        loops.push_back({end, next, scopes.size()});
        statement(node->body.get());
        loops.pop_back();
        enc.bind(next);
        storeValue(variable, binarySlots("+", variable, step));
        enc.jump(test);
        enc.bind(end);
    }

    void collectionRange(ForEachNode* node) {
        auto& frames = loopSlots.at(node);
        auto& variable = lookup(node->varName);
        auto& sequence = frames.first;
        const auto& index = frames.second;
        auto type = expression(node->collectionExpr.get());
        if (type == ThavaType::UNKNOWN) {
            normalize(ThavaType::LIST, "", type);
            type = ThavaType::LIST;
        } else if (!collection(type) || type == ThavaType::DICT) unsupported("Iteration over this type");
        sequence.type = type;
        if (variable.type != ThavaType::UNKNOWN && !scalar(variable.type) && !collection(variable.type))
            unsupported("This foreach element type");
        storeValue(sequence, type);
        integer(0);
        storeValue(index, ThavaType::INT);
        const auto test = enc.label(), next = enc.label(), end = enc.label();
        enc.bind(test);
        loadValue(index);
        saveValue();
        loadValue(sequence);
        enc.push(EAX);
        callRuntime(runtime::rt_collection_len, 4);
        enc.signExtend();
        emitBinary("<", ThavaType::INT, ThavaType::INT);
        testValue();
        enc.jump(Equal, end);

        pushAllocationContext();
        enc.alu(Sub, ESP, 20);
        loadValue(sequence);
        enc.store(ESP, 0, EAX);
        loadValue(index);
        storeTagged(ThavaType::INT, 4);
        enc.imm(EAX, 0);
        enc.store(ESP, 16, EAX);
        callRuntime(runtime::rt_collection_current, 28);
        requireValue("Invalid collection index");
        storeValue(variable, ThavaType::UNKNOWN);
        loops.push_back({end, next, scopes.size()});
        statement(node->body.get());
        loops.pop_back();
        enc.bind(next);
        loadValue(index);
        enc.alu(Add, EAX, 1);
        enc.alu(Adc, EDX, 0);
        storeValue(index, ThavaType::INT);
        enc.jump(test);
        enc.bind(end);
    }

    void closeFile(const Slot& slot) {
        const auto done = enc.label();
        loadValue(slot);
        enc.alu(Cmp, EAX, 0);
        enc.jump(Equal, done);
        if (windows) {
            enc.push(EAX);
            callWindows("fclose");
            enc.alu(Add, ESP, 4);
        } else {
            enc.push(EBX);
            enc.lea(EBX, EAX, -1); // Handles are fd + 1; zero is invalid.
            enc.imm(EAX, 6); // sys_close
            enc.syscall();
            enc.pop(EBX);
        }
        enc.bind(done);
    }

    void exceptionAddress(Reg reg, ExceptionField field) {
        dataAddress(reg, exceptionContext);
        enc.load(reg, reg);
        if (field != CleanupHead) enc.lea(reg, reg, field);
    }

    void enterCleanup(Cleanup record) {
        exceptionAddress(EDI, CleanupHead);
        enc.load(EAX, EDI);
        enc.store(EBP, record.offset + Previous, EAX);
        enc.store(EBP, record.offset + Frame, EBP);
        codeAddress(EAX, record.action);
        enc.store(EBP, record.offset + Action, EAX);
        enc.store(EBP, record.offset + Stack, ESP);
        enc.store(EBP, record.offset + Image, EBX);
        enc.alu(Xor, EAX, EAX);
        enc.store(EBP, record.offset + Defers, EAX);
        enc.lea(EAX, EBP, record.offset);
        enc.store(EDI, 0, EAX);
    }

    void popCleanup(Cleanup record) {
        enc.load(EAX, EBP, record.offset + Previous);
        exceptionAddress(EDI, CleanupHead);
        enc.store(EDI, 0, EAX);
    }

    void callCleanup(Cleanup record) {
        enc.lea(ECX, EBP, record.offset);
        enc.call(record.action);
    }

    void leaveScopes(size_t depth) {
        for (size_t i = scopes.size(); i > depth; --i) {
            const auto record = scopes[i - 1];
            if (record.action) callCleanup(record);
            else popCleanup(record); // Normal control flow drops the handler.
        }
    }

    void raiseException(ASTNode* message, ASTNode* type = nullptr) {
        if (message) formatScalar(expression(message));
        else stringValue("Runtime Error!");
        enc.push(EAX);
        if (type) formatScalar(expression(type));
        else stringValue("RuntimeError");
        enc.pop(ECX);
        exceptionAddress(EDI, PendingException);
        enc.store(EDI, 0, ECX);
        enc.store(EDI, 4, EAX);
        enc.jump(unwindLabel);
    }

    void protectedBlock(TryExceptNode* node) {
        const auto handler = handlers.at(node);
        const auto end = enc.label();
        enterCleanup(handler);
        scopes.push_back({handler.offset, 0});
        for (const auto& child : node->tryBody) statement(child.get());
        scopes.pop_back();
        popCleanup(handler);
        enc.jump(end);

        enc.bind(handler.action);
        // The dispatcher restored EBP and passed this record in ECX.
        enc.load(ESP, ECX, Stack);
        popCleanup(handler);
        bool catchAll = false;
        for (const auto& clause : node->exceptClauses) {
            catchAll = clause.exceptionType.empty() || clause.exceptionType == "Exception" ||
                       clause.exceptionType == "RuntimeError";
            size_t next = 0;
            if (!catchAll) {
                next = enc.label();
                exceptionAddress(EDI, PendingException);
                enc.load(ECX, EDI, 4);
                stringValue(clause.exceptionType);
                enc.call(compareStringsLabel);
                enc.alu(Cmp, EAX, 0);
                enc.jump(NotEqual, next);
            }
            decltype(locals)::node_type shadow;
            if (!clause.varName.empty()) {
                shadow = locals.extract(clause.varName);
                const auto& slot = catchSlots.at(&clause);
                locals.emplace(clause.varName, slot);
                exceptionAddress(EAX, PendingException);
                enc.load(EAX, EAX);
                enc.alu(Xor, EDX, EDX);
                storeValue(slot, ThavaType::STRING);
            }
            for (const auto& child : clause.body) statement(child.get());
            if (!clause.varName.empty()) {
                locals.erase(clause.varName);
                if (!shadow.empty()) locals.insert(std::move(shadow));
            }
            enc.jump(end);
            if (catchAll) break;
            enc.bind(next);
        }
        if (!catchAll) enc.jump(unwindLabel);
        enc.bind(end);
    }

    void registerDefer(DeferNode* node) {
        const auto scope = activeFiles.empty() ? functionCleanup : activeFiles.back().cleanup;
        const auto entry = enc.label(), next = enc.label();
        const auto slot = deferSlots.at(node);
        enc.load(EAX, EBP, slot + DeferAction);
        enc.alu(Cmp, EAX, 0);
        enc.jump(NotEqual, next); // One registration per site in this scope.
        enc.load(EAX, EBP, scope.offset + Defers);
        enc.store(EBP, slot + NextDefer, EAX);
        codeAddress(EAX, entry);
        enc.store(EBP, slot + DeferAction, EAX);
        enc.lea(EAX, EBP, slot);
        enc.store(EBP, scope.offset + Defers, EAX);
        enc.jump(next);
        enc.bind(entry);
        const bool alreadyDeferred = inDefer;
        auto surroundingLoops = std::move(loops);
        loops.clear();
        inDefer = true;
        statement(node->stmt.get());
        inDefer = alreadyDeferred;
        loops = std::move(surroundingLoops);
        enc.ret();
        enc.bind(next);
    }

    void scopedFile(FileOpenBlockNode* node) {
        fileUsed = true;
        if (windows) {
            for (const char* name : {"fopen", "fclose", "fgetc", "fwrite"})
                pe.addImport("msvcrt.dll", name);
        }

        expectString(node->pathExpr.get());
        enc.push(EAX);
        if (node->modeExpr) expectString(node->modeExpr.get());
        else stringValue("r");
        const auto done = enc.label(), fail = enc.label();
        if (windows) {
            enc.pop(ECX);
            enc.push(EAX);
            enc.push(ECX);
            callWindows("fopen");
            enc.alu(Add, ESP, 8);
        } else {
            enc.push(EAX);
            callRuntime(runtime::rt_linux_open_flags, 4);
            enc.pop(ECX);
            enc.alu(Cmp, EAX, 0);
            enc.jump(Less, fail);
            enc.push(EBX);
            enc.mov(EBX, ECX);
            enc.mov(ECX, EAX);
            enc.imm(EDX, 0666);
            enc.imm(EAX, 5); // sys_open
            enc.syscall();
            enc.pop(EBX);
            enc.alu(Cmp, EAX, 0);
            enc.jump(Less, fail);
            enc.alu(Add, EAX, 1);
            enc.jump(done);
        }
        enc.jump(done);
        enc.bind(fail);
        enc.alu(Xor, EAX, EAX);
        enc.bind(done);
        enc.alu(Xor, EDX, EDX);
        const auto& slot = fileSlots.at(node);
        storeValue(slot, ThavaType::FILE);
        const auto cleanup = fileCleanups.at(node);
        enterCleanup(cleanup);
        activeFiles.push_back({node->varName, &slot, cleanup});
        scopes.push_back(cleanup);
        for (const auto& child : node->body) statement(child.get());
        scopes.pop_back();
        activeFiles.pop_back();
        callCleanup(cleanup);
        const auto afterClose = enc.label();
        enc.jump(afterClose);
        enc.bind(cleanup.action);
        enc.call(runDefersLabel);
        popCleanup(cleanup);
        closeFile(slot);
        enc.ret();
        enc.bind(afterClose);
    }

    void statement(ASTNode* node) {
        diag::Scope location(node ? node->span : SourceSpan{});
        if (auto decl = dynamic_cast<VarDeclNode*>(node)) {
            auto& slot = lookup(decl->varName);
            if (decl->initExpr) storeValue(slot, expression(decl->initExpr.get()));
            else if (slot.type == ThavaType::CUSTOM && structs.count(slot.width))
                storeValue(slot, instantiateStruct(structs.at(slot.width), {}));
            else {
                defaultValue(slot.type);
                storeValue(slot, slot.type);
            }
        } else if (auto assign = dynamic_cast<AssignNode*>(node)) {
            auto& slot = lookup(assign->name);
            if (assign->op == "=") storeValue(slot, expression(assign->expr.get()));
            else {
                loadValue(slot);
                saveValue();
                const auto value = expression(assign->expr.get());
                storeValue(slot, emitBinary(assign->op.substr(0, assign->op.size() - 1), slot.type, value));
            }
        } else if (auto assign = dynamic_cast<MemberAssignNode*>(node)) {
            const auto& slot = field(assign->target.get(), assign->memberName);
            enc.push(EAX);
            if (assign->op != "=") {
                enc.mov(ECX, EAX);
                loadValue(slot);
                saveValue();
            }
            auto value = expression(assign->expr.get());
            if (assign->op != "=")
                value = emitBinary(assign->op.substr(0, assign->op.size() - 1), slot.type, value);
            normalize(slot.type, slot.width, value);
            enc.pop(ECX);
            enc.store(ECX, slot.offset, EAX);
            enc.store(ECX, slot.offset + 4, EDX);
        } else if (auto open = dynamic_cast<FileOpenBlockNode*>(node)) {
            scopedFile(open);
        } else if (auto block = dynamic_cast<TryExceptNode*>(node)) {
            protectedBlock(block);
        } else if (auto thrown = dynamic_cast<ThrowNode*>(node)) {
            raiseException(thrown->expr.get());
        } else if (auto raised = dynamic_cast<RaiseErrorNode*>(node)) {
            if (!raised->isCompileTime) raiseException(raised->msgExpr.get(), raised->typeExpr.get());
        } else if (auto defer = dynamic_cast<DeferNode*>(node)) {
            registerDefer(defer);
        } else if (auto index = dynamic_cast<IndexAssignNode*>(node)) {
            mutateCollection(lookup(index->varName), index->indexExpr.get(), index->valExpr.get(), false, index->op);
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
        } else if (auto range = dynamic_cast<RangeForNode*>(node)) {
            numericRange(range);
        } else if (auto each = dynamic_cast<ForEachNode*>(node)) {
            collectionRange(each);
        } else if (dynamic_cast<BreakNode*>(node) || dynamic_cast<ContinueNode*>(node)) {
            if (loops.empty()) unsupported("Loop control outside a loop");
            leaveScopes(loops.back().scopes);
            enc.jump(dynamic_cast<BreakNode*>(node) ? loops.back().end : loops.back().next);
        } else if (auto out = dynamic_cast<ConsOutNode*>(node)) {
            printLine(out->getExpr());
        } else if (auto ret = dynamic_cast<ReturnNode*>(node)) {
            if (inDefer) unsupported("Returning from a deferred action");
            if (!currentFunction) unsupported("Return outside a function");
            if (ret->expr) {
                const auto value = expression(ret->expr.get());
                normalize(currentFunction->returnType, currentFunction->customReturnType, value);
            } else integer(0);
            if (!scopes.empty()) {
                saveValue();
                leaveScopes(0);
                enc.pop(EAX);
                enc.pop(EDX);
            }
            enc.jump(returnLabel);
        } else if (auto expr = dynamic_cast<ExpressionStatementNode*>(node)) {
            expression(expr->expr.get());
        } else if (!dynamic_cast<GlobalDeclNode*>(node)) {
            unsupported("This statement");
        }
    }

    void declareFrameLocals(const std::vector<VarDeclNode*>& decls, int32_t& frame) {
        for (const auto* decl : decls) {
            if (!slotType(decl->varType))
                unsupported("Type of variable '" + decl->varName + "'");
            if (decl->varType == ThavaType::CUSTOM) objectLayout(decl->customTypeName);
            if (locals.try_emplace(decl->varName, Slot{EBP, -frame - 8, decl->varType, decl->customTypeName}).second)
                frame += 8;
        }
    }

    void collectLoopSlots(ASTNode* node, int32_t& bytes) {
        auto temporary = [&](ThavaType type) {
            bytes += 8;
            return Slot{EBP, -bytes, type, "int64"};
        };
        if (auto block = dynamic_cast<BlockNode*>(node)) {
            for (const auto& stmt : block->statements) collectLoopSlots(stmt.get(), bytes);
        } else if (auto branch = dynamic_cast<IfNode*>(node)) {
            collectLoopSlots(branch->thenBranch.get(), bytes);
            collectLoopSlots(branch->elseBranch.get(), bytes);
        } else if (auto w = dynamic_cast<WhileNode*>(node)) collectLoopSlots(w->body.get(), bytes);
        else if (auto f = dynamic_cast<ForNode*>(node)) collectLoopSlots(f->body.get(), bytes);
        else if (auto range = dynamic_cast<RangeForNode*>(node)) {
            if (range->varType != ThavaType::INT && !floating(range->varType))
                unsupported("Range loop type");
            if (!locals.count(range->varName)) locals.emplace(range->varName, temporary(range->varType));
            else if (locals.at(range->varName).type != range->varType) unsupported("Conflicting range loop variable");
            loopSlots.emplace(node, LoopSlots{temporary(range->varType), temporary(range->varType)});
            collectLoopSlots(range->body.get(), bytes);
        } else if (auto open = dynamic_cast<FileOpenBlockNode*>(node)) {
            fileSlots.emplace(open, temporary(ThavaType::FILE));
            bytes += RecordBytes;
            fileCleanups.emplace(open, Cleanup{-bytes, enc.label()});
            std::vector<VarDeclNode*> decls;
            for (const auto& child : open->body) thavaCollectVarDecls(child.get(), decls);
            declareFrameLocals(decls, bytes);
            for (const auto& child : open->body) collectLoopSlots(child.get(), bytes);
        } else if (auto protect = dynamic_cast<TryExceptNode*>(node)) {
            bytes += RecordBytes;
            handlers.emplace(protect, Cleanup{-bytes, enc.label()});
            for (const auto& child : protect->tryBody) collectLoopSlots(child.get(), bytes);
            for (const auto& clause : protect->exceptClauses) {
                if (!clause.varName.empty()) catchSlots.emplace(&clause, temporary(ThavaType::STRING));
                for (const auto& child : clause.body) collectLoopSlots(child.get(), bytes);
            }
        } else if (auto deferred = dynamic_cast<DeferNode*>(node)) {
            bytes += DeferBytes;
            deferSlots.emplace(deferred, -bytes);
            std::vector<VarDeclNode*> decls;
            thavaCollectVarDecls(deferred->stmt.get(), decls);
            declareFrameLocals(decls, bytes);
            collectLoopSlots(deferred->stmt.get(), bytes);
        } else if (auto each = dynamic_cast<ForEachNode*>(node)) {
            if (!locals.count(each->varName)) locals.emplace(each->varName, temporary(each->varType));
            loopSlots.emplace(node, LoopSlots{temporary(ThavaType::LIST), temporary(ThavaType::INT)});
            collectLoopSlots(each->body.get(), bytes);
        }
    }

    void beginFrame(const std::vector<ASTPtr>& body, bool entry) {
        enc.push(EBP);
        enc.mov(EBP, ESP);
        locals.clear();
        loopSlots.clear();
        fileSlots.clear();
        fileCleanups.clear();
        handlers.clear();
        catchSlots.clear();
        deferSlots.clear();
        functionCleanup = {};
        if (!entry) {
            const auto& fn = *currentFunction;
            const int32_t first = fn.owner.empty() ? 8 : 16;
            for (size_t i = 0; i < fn.params.size(); ++i) {
                const auto& [type, name] = fn.params[i];
                if (!slotType(type)) unsupported("Parameter '" + name + "' of '" + fn.name + "'");
                const auto width = i < fn.paramCustomTypes.size() ? fn.paramCustomTypes[i] : "";
                if (type == ThavaType::CUSTOM) objectLayout(width);
                Slot slot{EBP, first + int32_t(8 * i), type, width};
                slot.indirect = collection(type);
                locals.emplace(name, slot);
            }
        }
        std::vector<VarDeclNode*> decls;
        for (const auto& node : body) {
            if (entry && dynamic_cast<VarDeclNode*>(node.get())) continue;
            thavaCollectVarDecls(node.get(), decls);
        }
        int32_t frame = 0;
        declareFrameLocals(decls, frame);
        for (const auto& node : body) collectLoopSlots(node.get(), frame);
        if (!deferSlots.empty()) {
            frame += RecordBytes;
            functionCleanup = {-frame, enc.label()};
        }
        if (frame > 4080) unsupported("A local stack frame larger than 4080 bytes");
        if (frame) enc.alu(Sub, ESP, uint32_t(frame));
        for (const auto& [name, slot] : locals) {
            if (slot.offset > 0 || slot.type == ThavaType::UNKNOWN) continue;
            defaultValue(slot.type);
            storeValue(slot, slot.type);
        }
        if (functionCleanup.offset) {
            enc.alu(Xor, EAX, EAX);
            for (const auto& entry : deferSlots) enc.store(EBP, entry.second + DeferAction, EAX);
            enterCleanup(functionCleanup);
        }
    }

    void emitFunctionCleanup() {
        if (!functionCleanup.offset) return;
        enc.bind(functionCleanup.action);
        enc.call(runDefersLabel);
        popCleanup(functionCleanup);
        enc.ret();
    }

    void emitFunction(const Function& fn) {
        if (!slotType(fn.returnType) && fn.returnType != ThavaType::VOID)
            unsupported("Return type of '" + fn.name + "'");
        if (fn.returnType == ThavaType::CUSTOM) objectLayout(fn.customReturnType);
        currentFunction = &fn;
        returnLabel = enc.label();
        enc.bind(fn.entry);
        beginFrame(fn.body, false);
        for (const auto& node : fn.body) statement(node.get());
        defaultValue(fn.returnType);
        enc.bind(returnLabel);
        if (functionCleanup.offset) {
            saveValue();
            callCleanup(functionCleanup);
            enc.pop(EAX);
            enc.pop(EDX);
        }
        enc.leave();
        enc.ret();
        emitFunctionCleanup();
        currentFunction = nullptr;
    }

    static void appendField(ObjectLayout& layout, const ClassFieldDecl& field) {
        if (!slotType(field.type))
            unsupported("Type of field '" + field.name + "'");
        layout.fields.insert_or_assign(field.name, Slot{ECX, layout.size, field.type, field.customTypeName});
        layout.schema += ";" + field.name + ":" + moduleTypeName(field.type, field.customTypeName) +
                         ":" + std::to_string(static_cast<int>(field.access));
        layout.size += 8;
    }

    void registerClass(const ClassDeclNode& decl) {
        ClassLayout layout{{enc.label(), 8, reserveData(28), {}, {}}, &decl, {}, {}};
        layout.schema = "class:" + decl.className;
        if (!decl.baseClassName.empty()) {
            const auto& base = classLayout(decl.baseClassName);
            layout.size = base.size;
            layout.fields = base.fields;
            layout.schema += "[" + base.schema + "]";
        }
        for (const auto& field : decl.fields) appendField(layout, field);
        for (const auto& ctor : decl.constructors)
            layout.constructors.push_back({decl.className + "::init", decl.className, ThavaType::VOID, "",
                                           ctor.params, ctor.paramCustomTypes, ctor.body, enc.label()});
        for (const auto& method : decl.methods)
            layout.methods.emplace(method.name, Function{decl.className + "::" + method.name, decl.className,
                                   method.returnType, method.customReturnType, method.params,
                                   method.paramCustomTypes, method.body, enc.label()});
        for (const auto& method : decl.methods)
            layout.schema += ";func:" + methodSignature(method.name, layout.methods.at(method.name)) +
                             ":" + std::to_string(static_cast<int>(method.access));
        classes.emplace(decl.className, std::move(layout));
    }

    void registerStruct(const StructDeclNode& decl) {
        StructLayout layout{{enc.label(), 8, reserveData(28), {}, {}}, &decl};
        layout.schema = "struct:" + decl.structName;
        for (const auto& field : decl.fields) {
            if (layout.fields.count(field.name)) unsupported("Duplicate struct field '" + field.name + "'");
            appendField(layout, field);
        }
        structs.emplace(decl.structName, std::move(layout));
    }

    void writeDescriptorWord(uint32_t base, uint32_t offset, uint32_t value) {
        enc.imm(EAX, value);
        writeDescriptorPointer(base, offset);
    }

    void writeDescriptorPointer(uint32_t base, uint32_t offset) {
        dataAddress(ECX, base);
        enc.store(ECX, offset, EAX);
    }

    void writeDescriptorString(uint32_t base, uint32_t offset, const std::string& value) {
        dataAddress(EAX, addString(value));
        writeDescriptorPointer(base, offset);
    }

    void emitObjectDescriptor(const std::string& name, const ObjectLayout& layout,
                              const std::vector<ClassFieldDecl>& fields, const ClassLayout* cls = nullptr) {
        const auto descriptor = layout.descriptor;
        writeDescriptorString(descriptor, 0, name);
        writeDescriptorString(descriptor, 24, layout.schema);
        if (cls && !cls->decl->baseClassName.empty()) {
            dataAddress(EAX, classLayout(cls->decl->baseClassName).descriptor);
            writeDescriptorPointer(descriptor, 4);
        }
        if (!fields.empty()) {
            const auto table = reserveData(fields.size() * 12);
            writeDescriptorWord(descriptor, 8, static_cast<uint32_t>(fields.size()));
            dataAddress(EAX, table);
            writeDescriptorPointer(descriptor, 12);
            uint32_t offset = 0;
            for (const auto& field : fields) {
                writeDescriptorString(table, offset, field.name);
                writeDescriptorWord(table, offset + 4, layout.fields.at(field.name).offset);
                writeDescriptorWord(table, offset + 8, field.access == AccessModifier::PUBLIC ? valueKind(field.type) : UINT32_MAX);
                offset += 12;
            }
        }
        if (!cls || cls->methods.empty()) return;
        const auto table = reserveData(cls->methods.size() * 16);
        writeDescriptorWord(descriptor, 16, static_cast<uint32_t>(cls->methods.size()));
        dataAddress(EAX, table);
        writeDescriptorPointer(descriptor, 20);
        uint32_t offset = 0;
        for (const auto& [methodName, fn] : cls->methods) {
            const auto method = std::find_if(cls->decl->methods.begin(), cls->decl->methods.end(),
                                             [&](const auto& item) { return item.name == methodName; });
            writeDescriptorString(table, offset, methodSignature(methodName, fn));
            codeAddress(EAX, bridge(fn, false));
            writeDescriptorPointer(table, offset + 4);
            if (method->access == AccessModifier::PUBLIC) writeDescriptorString(table, offset + 8, methodName);
            codeAddress(EAX, bridge(fn, true));
            writeDescriptorPointer(table, offset + 12);
            offset += 16;
        }
    }

    void emitStructInitializer(const StructLayout& layout) {
        locals.clear();
        enc.bind(layout.initializer);
        enc.push(EBP);
        enc.mov(EBP, ESP);
        for (size_t i = 0; i < layout.decl->fields.size(); ++i) {
            const auto& field = layout.decl->fields[i];
            const auto& slot = layout.fields.at(field.name);
            if (slot.type == ThavaType::CUSTOM) objectLayout(slot.width);

            const auto initialized = enc.label();
            enc.load(ECX, EBP, 16);
            enc.alu(Cmp, ECX, static_cast<uint32_t>(i + 1));
            enc.jump(AboveEqual, initialized);
            if (field.initExpr) normalize(slot.type, slot.width, expression(field.initExpr.get()));
            else defaultValue(slot.type);
            enc.load(ECX, EBP, 8);
            enc.store(ECX, slot.offset, EAX);
            enc.store(ECX, slot.offset + 4, EDX);
            enc.bind(initialized);
        }
        enc.leave();
        enc.ret();
    }

    void emitClassInitializer(const ClassLayout& layout) {
        locals.clear();
        initializerClass = layout.decl->className;
        enc.bind(layout.initializer);
        enc.push(EBP);
        enc.mov(EBP, ESP);
        if (!layout.decl->baseClassName.empty()) {
            enc.pushImm(0);
            enc.load(EAX, EBP, 8);
            enc.push(EAX);
            enc.call(classLayout(layout.decl->baseClassName).initializer);
            enc.alu(Add, ESP, 8);
        }
        for (const auto& field : layout.decl->fields) {
            const auto& slot = layout.fields.at(field.name);
            if (field.type == ThavaType::CUSTOM) objectLayout(field.customTypeName);
            defaultValue(slot.type);
            enc.load(ECX, EBP, 8);
            enc.store(ECX, slot.offset, EAX);
            enc.store(ECX, slot.offset + 4, EDX);
        }
        for (const auto& field : layout.decl->fields) {
            if (!field.initExpr) continue;
            const auto& slot = layout.fields.at(field.name);
            normalize(slot.type, slot.width, expression(field.initExpr.get()));
            enc.load(ECX, EBP, 8);
            enc.store(ECX, slot.offset, EAX);
            enc.store(ECX, slot.offset + 4, EDX);
        }
        enc.leave();
        enc.ret();
        initializerClass.clear();
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

    void emitUnwind() {
        // Division reaches the error path before borrowing EBX for arithmetic.
        enc.bind(errorLabel);
        exceptionAddress(EDI, PendingException);
        dataAddress(EAX, addString("Division by zero"));
        enc.store(EDI, 0, EAX);
        dataAddress(EAX, addString("RuntimeError"));
        enc.store(EDI, 4, EAX);

        enc.bind(unwindLabel);
        exceptionAddress(EDI, CleanupHead);
        enc.load(ECX, EDI);
        enc.alu(Cmp, ECX, 0);
        const auto fatal = enc.label();
        enc.jump(Equal, fatal);
        // Cleanups may catch their own exception and then return to this unwind.
        enc.push(EBX);
        exceptionAddress(EDI, PendingException);
        enc.load(EAX, EDI, 0);
        enc.push(EAX);
        enc.load(EAX, EDI, 4);
        enc.push(EAX);
        enc.load(EBP, ECX, Frame);
        enc.load(EAX, ECX, Action);
        enc.load(EBX, ECX, Image);
        enc.callReg(EAX);
        exceptionAddress(EDI, PendingException);
        enc.pop(EAX);
        enc.store(EDI, 4, EAX);
        enc.pop(EAX);
        enc.store(EDI, 0, EAX);
        enc.pop(EBX);
        enc.jump(unwindLabel);

        enc.bind(fatal);
        printText("[Runtime Exception]: ");
        exceptionAddress(EAX, PendingException);
        enc.load(EAX, EAX);
        enc.call(printStringLabel);
        printText("\n");
        exitProcess(1);

        enc.bind(runDefersLabel);
        enc.push(ECX);
        const auto next = enc.label(), done = enc.label();
        enc.bind(next);
        enc.load(ECX, ESP);
        enc.load(EAX, ECX, Defers);
        enc.alu(Cmp, EAX, 0);
        enc.jump(Equal, done);
        enc.load(EDX, EAX, NextDefer);
        enc.store(ECX, Defers, EDX);
        enc.load(EDX, EAX, DeferAction);
        enc.alu(Xor, ECX, ECX);
        enc.store(EAX, DeferAction, ECX);
        enc.callReg(EDX);
        enc.jump(next);
        enc.bind(done);
        enc.alu(Add, ESP, 4);
        enc.ret();
    }

    void emitInput() {
        if (!readUsed) return;
        enc.bind(readLineLabel);
        if (windows) emitWindowsConsoleLine();

        // Redirected Windows input and Linux input are byte streams.
        pushAllocationContext();
        codeAddress(EDI, readByteLabel);
        enc.push(EDI);
        callRuntime(runtime::rt_read_line, 12);
        enc.ret();

        enc.bind(readByteLabel);
        enc.push(EBX);
        const auto error = enc.label(), done = enc.label();
        if (windows) {
            pe.addImport("kernel32.dll", "ReadFile");
            enc.load(EBX, ESP, 8);
            enc.alu(Sub, ESP, 8);
            enc.mov(ECX, ESP);
            enc.lea(EDX, ESP, 4);
            enc.pushImm(0);
            enc.push(EDX);
            enc.pushImm(1);
            enc.push(ECX);
            dataAddress(EAX, stdinSlot);
            enc.load(EAX, EAX);
            enc.push(EAX);
            callWindows("ReadFile");
            enc.alu(Cmp, EAX, 0);
            enc.jump(Equal, error);
            enc.load(EAX, ESP, 4);
            enc.alu(Cmp, EAX, 1);
            enc.jump(NotEqual, error);
        } else {
            enc.alu(Sub, ESP, 8);
            enc.alu(Xor, EBX, EBX);
            enc.mov(ECX, ESP);
            enc.imm(EDX, 1);
            const auto retry = enc.label();
            enc.bind(retry);
            enc.imm(EAX, 3); // sys_read
            enc.syscall();
            enc.alu(Cmp, EAX, uint32_t(-4)); // EINTR
            enc.jump(Equal, retry);
            enc.alu(Cmp, EAX, 1);
            enc.jump(NotEqual, error);
        }
        enc.loadByte(EAX, ESP);
        enc.jump(done);
        enc.bind(error);
        enc.imm(EAX, uint32_t(-1));
        enc.bind(done);
        enc.alu(Add, ESP, 8);
        enc.pop(EBX);
        enc.ret();
    }

    void emitWindowsConsoleLine() {
        for (const char* name : {"GetConsoleMode", "ReadConsoleW", "WideCharToMultiByte"})
            pe.addImport("kernel32.dll", name);
        const auto redirected = enc.label(), empty = enc.label();
        constexpr int32_t wide = -2080, handle = -4, count = -8, output = -12;
        enc.push(EBP);
        enc.mov(EBP, ESP);
        enc.alu(Sub, ESP, -wide);
        dataAddress(EAX, stdinSlot);
        enc.load(EAX, EAX);
        enc.store(EBP, handle, EAX);
        enc.lea(ECX, EBP, count);
        enc.push(ECX);
        enc.push(EAX);
        callWindows("GetConsoleMode");
        enc.alu(Cmp, EAX, 0);
        enc.jump(Equal, redirected);

        enc.pushImm(0);
        enc.lea(EAX, EBP, count);
        enc.push(EAX);
        enc.pushImm(1024);
        enc.lea(EAX, EBP, wide);
        enc.push(EAX);
        enc.load(EAX, EBP, handle);
        enc.push(EAX);
        callWindows("ReadConsoleW");
        enc.alu(Cmp, EAX, 0);
        enc.jump(Equal, empty);

        allocatorUsed = true;
        enc.push(EBX);
        enc.pushImm(3073);
        enc.call(allocateLabel);
        enc.alu(Add, ESP, 8);
        enc.alu(Cmp, EAX, 0);
        enc.jump(Equal, empty);
        enc.store(EBP, output, EAX);
        enc.pushImm(0);
        enc.pushImm(0);
        enc.pushImm(3072);
        enc.push(EAX);
        enc.load(EAX, EBP, count);
        enc.push(EAX);
        enc.lea(EAX, EBP, wide);
        enc.push(EAX);
        enc.pushImm(0);
        enc.pushImm(65001); // CP_UTF8
        callWindows("WideCharToMultiByte");
        enc.push(EAX);
        enc.load(EAX, EBP, output);
        enc.push(EAX);
        callRuntime(runtime::rt_terminate_line, 8);
        enc.leave();
        enc.ret();

        enc.bind(empty);
        stringValue("");
        enc.leave();
        enc.ret();

        enc.bind(redirected);
        enc.leave();
    }

    void emitFileCallbacks() {
        if (!fileUsed) return;
        enc.bind(fileReadLabel);
        enc.push(EBX);
        enc.load(EAX, ESP, 8);
        if (windows) {
            enc.load(EBX, ESP, 12);
            enc.push(EAX);
            callWindows("fgetc");
            enc.alu(Add, ESP, 4);
        } else {
            enc.lea(EBX, EAX, -1);
            enc.alu(Sub, ESP, 4);
            enc.mov(ECX, ESP);
            enc.imm(EDX, 1);
            const auto retry = enc.label(), eof = enc.label(), done = enc.label();
            enc.bind(retry);
            enc.imm(EAX, 3); // sys_read
            enc.syscall();
            enc.alu(Cmp, EAX, uint32_t(-4));
            enc.jump(Equal, retry);
            enc.alu(Cmp, EAX, 1);
            enc.jump(NotEqual, eof);
            enc.loadByte(EAX, ESP);
            enc.jump(done);
            enc.bind(eof);
            enc.imm(EAX, uint32_t(-1));
            enc.bind(done);
            enc.alu(Add, ESP, 4);
        }
        enc.pop(EBX);
        enc.ret();

        enc.bind(fileWriteLabel);
        enc.push(EBX);
        enc.load(EAX, ESP, 8);
        enc.load(ECX, ESP, 12);
        enc.load(EDX, ESP, 16);
        if (windows) {
            enc.load(EBX, ESP, 20);
            enc.push(EAX);
            enc.push(EDX);
            enc.pushImm(1);
            enc.push(ECX);
            callWindows("fwrite");
            enc.alu(Add, ESP, 16);
        } else {
            enc.lea(EBX, EAX, -1);
            const auto retry = enc.label(), done = enc.label();
            enc.bind(retry);
            enc.alu(Cmp, EDX, 0);
            enc.jump(Equal, done);
            enc.imm(EAX, 4); // sys_write
            enc.syscall();
            enc.alu(Cmp, EAX, uint32_t(-4));
            enc.jump(Equal, retry);
            enc.alu(Cmp, EAX, 0);
            enc.jump(LessEqual, done);
            enc.alu(Add, ECX, EAX);
            enc.alu(Sub, EDX, EAX);
            enc.jump(retry);
            enc.bind(done);
        }
        enc.pop(EBX);
        enc.ret();
    }

    void emitAllocator() {
        if (!allocatorUsed) return;
        enc.bind(allocateLabel);
        if (windows) {
            pe.addImport("kernel32.dll", "GetProcessHeap");
            pe.addImport("kernel32.dll", "HeapAlloc");
            enc.push(EBX);
            enc.load(EBX, ESP, 12);
            callWindows("GetProcessHeap");
            enc.load(ECX, ESP, 8);
            enc.push(ECX);
            enc.pushImm(0);
            enc.push(EAX);
            callWindows("HeapAlloc");
            enc.pop(EBX);
            enc.ret();
            return;
        }

        const auto arena = addSlot();
        const auto serve = enc.label(), map = enc.label(), fail = enc.label(), done = enc.label();
        enc.push(EBP);
        enc.mov(EBP, ESP);
        enc.push(EBX);
        enc.push(ESI);
        enc.push(EDI);
        enc.load(EBX, EBP, 12);
        enc.load(ECX, EBP, 8);
        enc.alu(Add, ECX, 7);
        enc.jump(Below, fail);
        enc.alu(And, ECX, uint32_t(-8));
        enc.push(ECX); // size at EBP - 16
        dataAddress(EDI, arena);
        enc.load(EAX, EDI);
        enc.load(EDX, EDI, 4);
        enc.alu(Cmp, ECX, EDX);
        enc.jump(BelowEqual, serve);
        enc.alu(Cmp, ECX, 65536);
        enc.jump(AboveEqual, map);
        enc.imm(ECX, 65536);
        enc.bind(map);
        enc.push(EDI);
        enc.push(ECX);
        enc.push(EBP);
        enc.alu(Xor, EBX, EBX);
        enc.imm(EDX, 3); // PROT_READ | PROT_WRITE
        enc.imm(ESI, 0x22); // MAP_PRIVATE | MAP_ANONYMOUS
        enc.imm(EDI, uint32_t(-1));
        enc.alu(Xor, EBP, EBP);
        enc.imm(EAX, 192); // mmap2
        enc.syscall();
        enc.pop(EBP);
        enc.pop(EDX);
        enc.pop(EDI);
        enc.alu(Cmp, EAX, uint32_t(-4095));
        enc.jump(AboveEqual, fail);
        enc.load(ECX, EBP, -16);
        enc.bind(serve);
        enc.alu(Sub, EDX, ECX);
        enc.alu(Add, ECX, EAX);
        enc.store(EDI, 0, ECX);
        enc.store(EDI, 4, EDX);
        enc.jump(done);
        enc.bind(fail);
        enc.alu(Xor, EAX, EAX);
        enc.bind(done);
        enc.load(EBX, EBP, -4);
        enc.load(ESI, EBP, -8);
        enc.load(EDI, EBP, -12);
        enc.leave();
        enc.ret();
    }

    static bool moduleType(ThavaType type) { return slotType(type); }

    const char* moduleMachine() const { return windows ? "i686-pe" : "i686-elf"; }

    bool sharedExceptionModule(const DLLibraryInfo& info) const {
        return info.machine == std::string(moduleMachine()) + "-v2";
    }

    void validateImportedObject(const DLLibraryInfo& library, ThavaType type, const std::string& name) const {
        if (type != ThavaType::CUSTOM) return;
        const auto layout = library.objectLayouts.find(name);
        if ((!classes.count(name) && !structs.count(name)) || layout == library.objectLayouts.end())
            throw std::runtime_error("Include the shared class or struct declaration for '" + name + "' before using it across .thdl modules.");
        if (layout->second != objectLayout(name).schema)
            throw std::runtime_error("Object layout of '" + name + "' differs from the definition in '" + library.path + "'.");
    }

    void prepareModules() {
        if (g_isLibraryMode) moduleState = addSlot();
        if (!windows && (g_hasDLAttach || g_isLibraryMode)) {
            moduleRegistryPointer = addSlot();
            if (!g_isLibraryMode) moduleRegistry = addSlot();
        }
        for (const auto& path : g_dlLibraries) {
            const auto& info = g_dlLibraryInfos.at(path);
            const std::string machine = moduleMachine();
            if ((info.machine != machine && !sharedExceptionModule(info)) || !info.hasOffsets || !info.hasInit || info.textRva != ElfI686Emitter::textRva ||
                info.dataRva <= info.textRva || info.initOffset >= info.dataRva - info.textRva)
                throw std::runtime_error("Library '" + path + "' is not a compatible native " + machine + " module.");
            moduleSlots.emplace(path, addSlot());
        }
        for (const auto& [name, info] : g_dlFunctions) {
            const auto& library = g_dlLibraryInfos.at(info.libPath);
            if (!info.hasOffset || info.funcOffset >= library.dataRva - library.textRva ||
                (info.retType != ThavaType::VOID && !moduleType(info.retType)))
                unsupported("Imported signature of '" + name + "'");
            Import importer{&info, enc.label(), addSlot(), {}};
            for (size_t i = 0; i < info.paramTypes.size(); ++i) {
                if (!moduleType(info.paramTypes[i])) unsupported("Imported parameter of '" + name + "'");
                importer.params.emplace_back(info.paramTypes[i], info.paramNames[i]);
            }
            imports.emplace(name, std::move(importer));
        }
        if (windows && g_hasDLAttach) {
            pe.addImport("kernel32.dll", "LoadLibraryW");
            pe.addImport("kernel32.dll", "GetProcAddress");
        }
    }

    void imageBase() {
        // A call to the next instruction yields a PC without a text relocation.
        const auto pc = enc.label();
        enc.call(pc);
        const auto rva = ElfI686Emitter::textRva + enc.code.size();
        enc.bind(pc);
        enc.pop(EBX);
        enc.alu(Sub, EBX, static_cast<uint32_t>(rva));
    }

    void loadWindowsModule(const std::string& path) {
        std::string bytes;
        for (char16_t word : std::filesystem::path(path).u16string()) {
            bytes += char(word & 255);
            bytes += char(word >> 8);
        }
        bytes += '\0';
        bytes += '\0';
        stringValue(bytes);
        enc.push(EAX);
        callWindows("LoadLibraryW");
    }

    void windowsExport(uint32_t module, const std::string& name) {
        stringValue(name);
        enc.push(EAX);
        dataAddress(ECX, module);
        enc.load(EAX, ECX);
        enc.push(EAX);
        callWindows("GetProcAddress");
    }

    void loadModules() {
        for (const auto& path : g_dlLibraries) {
            const auto& info = g_dlLibraryInfos.at(path);
            const auto slot = moduleSlots.at(path);
            if (windows) {
                const auto found = enc.label();
                loadWindowsModule(path);
                enc.alu(Cmp, EAX, 0);
                enc.jump(NotEqual, found);
                loadWindowsModule(".\\" + info.basename);
                enc.bind(found);
            } else {
                pushAllocationContext();
                dataAddress(ECX, moduleRegistryPointer);
                enc.load(EAX, ECX);
                enc.push(EAX);
                enc.pushImm(info.dataRva);
                stringValue(info.basename);
                enc.push(EAX);
                stringValue(path);
                enc.push(EAX);
                callRuntime(runtime::rt_linux_dl_open, 24);
            }
            requireValue(("Cannot load library '" + path + "'").c_str());
            dataAddress(ECX, slot);
            enc.store(ECX, 0, EAX);
            if (windows) {
                windowsExport(slot, moduleInitializer);
                requireValue("Cannot find the native i686 library initializer");
                if (sharedExceptionModule(info)) {
                    exceptionAddress(ECX, CleanupHead);
                    enc.push(ECX);
                }
                enc.callReg(EAX);
                if (sharedExceptionModule(info)) enc.alu(Add, ESP, 4);
            } else {
                enc.alu(Add, EAX, info.textRva + info.initOffset);
                if (sharedExceptionModule(info)) {
                    exceptionAddress(ECX, CleanupHead);
                    enc.push(ECX);
                }
                dataAddress(ECX, environmentSlot);
                enc.load(EDX, ECX);
                enc.push(EDX);
                dataAddress(ECX, moduleRegistryPointer);
                enc.load(EDX, ECX);
                enc.push(EDX);
                enc.callReg(EAX);
                enc.alu(Add, ESP, sharedExceptionModule(info) ? 12 : 8);
            }
            requireValue(("Cannot initialize library '" + path + "'").c_str());
        }
        for (const auto& [name, import] : imports) {
            const auto slot = moduleSlots.at(import.info->libPath);
            if (windows) {
                windowsExport(slot, name);
                requireValue(("Cannot resolve library function '" + name + "'").c_str());
            } else {
                dataAddress(ECX, slot);
                enc.load(EAX, ECX);
                enc.alu(Add, EAX, import.info->libTextRva + import.info->funcOffset);
            }
            dataAddress(ECX, import.slot);
            enc.store(ECX, 0, EAX);
        }
    }

    void emitModuleThunks() {
        for (const auto& [name, import] : imports) {
            enc.bind(import.entry);
            dataAddress(ECX, import.slot);
            enc.load(EAX, ECX);
            enc.jumpReg(EAX);
        }
    }

    void emitRawBridge(const Function& fn) {
        enc.push(EBX);
        imageBase();
        const auto bytes = static_cast<uint32_t>(fn.params.size() + !fn.owner.empty()) * 8;
        if (bytes > 4080) unsupported("A library or method frame larger than 4080 bytes");
        if (bytes) {
            enc.alu(Sub, ESP, bytes);
            for (uint32_t index = 0; index < bytes; index += 4) {
                enc.load(EAX, ESP, bytes + 8 + index);
                enc.store(ESP, index, EAX);
            }
        }
        enc.call(fn.entry);
        if (bytes) enc.alu(Add, ESP, bytes);
        enc.pop(EBX);
        enc.ret();
    }

    void emitBoxedBridge(const Function& fn) {
        enc.push(EBP);
        enc.mov(EBP, ESP);
        enc.push(EBX);
        imageBase();
        const auto arity = enc.label();
        if (fn.params.size() <= 254) {
            enc.load(EAX, EBP, 12);
            enc.alu(Cmp, EAX, static_cast<uint32_t>(fn.params.size()));
            enc.jump(Equal, arity);
        }
        LiteralNode message("Wrong number of arguments for '" + fn.name + "'", ThavaType::STRING);
        raiseException(&message);
        enc.bind(arity);
        if (fn.params.size() > 254) return;

        const int32_t receiver = fn.owner.empty() ? 0 : 8;
        const int32_t bytes = receiver + static_cast<int32_t>(8 * fn.params.size());
        const int32_t frame = bytes + static_cast<int32_t>(8 * fn.params.size());
        if (frame) enc.alu(Sub, ESP, frame);
        if (receiver) {
            enc.load(EAX, EBP, 16);
            enc.alu(Xor, EDX, EDX);
            storeArgument(0);
        }
        for (size_t i = 0; i < fn.params.size(); ++i) {
            const auto formal = fn.params[i].first;
            const auto width = i < fn.paramCustomTypes.size() ? fn.paramCustomTypes[i] : "";
            const auto boxed = static_cast<int32_t>(16 * i);
            const auto offset = receiver + static_cast<int32_t>(8 * i);
            enc.load(EAX, EBP, 8);
            if (boxed) enc.lea(EAX, EAX, boxed);
            if (collection(formal)) {
                const auto temporary = bytes + static_cast<int32_t>(8 * i);
                storeValue({ESP, temporary, formal, width}, ThavaType::UNKNOWN);
                enc.load(ECX, EBP, 8);
                enc.load(EAX, ECX, boxed + 12);
                enc.load(EDX, ECX, boxed + 8);
                const auto temp = enc.label(), ready = enc.label();
                enc.alu(Cmp, EAX, 0);
                enc.jump(Equal, temp);
                enc.alu(Cmp, EDX, valueKind(formal));
                enc.jump(Equal, ready);
                enc.bind(temp);
                enc.lea(EAX, ESP, temporary);
                enc.bind(ready);
                enc.store(ESP, offset, EAX);
            } else storeValue({ESP, offset, formal, width}, ThavaType::UNKNOWN);
        }
        enc.call(fn.entry);
        const auto type = fn.returnType == ThavaType::VOID ? ThavaType::NONE : fn.returnType;
        if (type == ThavaType::NONE) integer(0);
        pushAllocationContext();
        enc.alu(Sub, ESP, 12);
        storeTagged(type, 0);
        callRuntime(runtime::rt_value_new, 20);
        enc.load(EBX, EBP, -4);
        enc.leave();
        enc.ret();
    }

    std::string emitModuleExports(const std::string& output) {
        std::string metadata = "__THAVA_METADATA_START__\nmachine:";
        metadata += std::string(moduleMachine()) + "-v2\n";
        if (windows) metadata += "pe-dll:1\n";
        metadata += "text:" + std::to_string(ElfI686Emitter::textRva) + '\n';
        for (const auto& [name, layout] : classes) metadata += "layout:" + name + '|' + layout.schema + '\n';
        for (const auto& [name, layout] : structs) metadata += "layout:" + name + '|' + layout.schema + '\n';
        if (windows) {
            pe.setDll(true);
            pe.setEntryPointRva(0);
            pe.setDllExportName(output);
            pe.addExport(moduleInitializer, 0);
        }
        for (const auto& [name, fn] : functions) {
            if (name == moduleInitializer) unsupported("Reserved module function name '" + name + "'");
            if ((fn.returnType != ThavaType::VOID && !moduleType(fn.returnType)) ||
                std::any_of(fn.params.begin(), fn.params.end(), [](const auto& param) { return !moduleType(param.first); })) continue;
            const auto offset = static_cast<uint32_t>(enc.code.size());
            if (windows) pe.addExport(name, offset);
            emitRawBridge(fn);

            metadata += "func:" + name + "|ret:" + moduleTypeName(fn.returnType, fn.customReturnType) + "|params:";
            for (size_t i = 0; i < fn.params.size(); ++i) {
                if (i) metadata += ',';
                const auto& [type, param] = fn.params[i];
                metadata += moduleTypeName(type, i < fn.paramCustomTypes.size() ? fn.paramCustomTypes[i] : "") + " " + param;
            }
            metadata += "|off:" + std::to_string(offset) + '\n';
        }
        return metadata;
    }

public:
    explicit Codegen(bool windowsTarget) : windows(windowsTarget) {}

    bool compile(const std::vector<ASTPtr>& ast, const std::string& output) {
        exceptionContext = addSlot();
        const auto localExceptionState = g_isLibraryMode ? 0 : addSlot();
        if (!g_isLibraryMode) addSlot(); // Pending exception message and type.
        for (const auto& node : ast) {
            if (auto decl = dynamic_cast<ClassDeclNode*>(node.get())) registerClass(*decl);
            else if (auto decl = dynamic_cast<StructDeclNode*>(node.get())) registerStruct(*decl);
        }
        prepareModules();
        for (const auto& node : ast) {
            if (auto func = dynamic_cast<FunctionDeclNode*>(node.get()))
                functions.emplace(func->name, Function{func->name, "", func->returnType, func->customReturnType,
                                  func->params, func->paramCustomTypes, func->body, enc.label()});
            else if (auto decl = dynamic_cast<VarDeclNode*>(node.get())) {
                if (!slotType(decl->varType)) unsupported("Type of global '" + decl->varName + "'");
                if (decl->varType == ThavaType::CUSTOM) objectLayout(decl->customTypeName);
                Slot slot{EBX, int32_t(addSlot()), decl->varType, decl->customTypeName};
                globals.emplace(decl->varName, slot);
            }
        }

        const auto initialized = enc.label(), initializing = enc.label();
        if (g_isLibraryMode) enc.push(EBX);
        imageBase();
        if (g_isLibraryMode) {
            dataAddress(ECX, moduleState);
            enc.load(EAX, ECX);
            enc.alu(Cmp, EAX, 2);
            enc.jump(Equal, initialized);
            enc.alu(Cmp, EAX, 1);
            enc.jump(Equal, initializing);
            enc.imm(EAX, 1);
            enc.store(ECX, 0, EAX);
        }
        if (g_isLibraryMode) enc.load(EAX, ESP, windows ? 8 : 16);
        else dataAddress(EAX, localExceptionState);
        dataAddress(ECX, exceptionContext);
        enc.store(ECX, 0, EAX);
        if (windows) {
            stdoutSlot = addSlot();
            stdinSlot = addSlot();
            enc.pushImm(65001);
            callWindows("SetConsoleOutputCP");
            enc.pushImm(uint32_t(-11)); // STD_OUTPUT_HANDLE
            callWindows("GetStdHandle");
            dataAddress(ECX, stdoutSlot);
            enc.store(ECX, 0, EAX);
            enc.pushImm(uint32_t(-10)); // STD_INPUT_HANDLE
            callWindows("GetStdHandle");
            dataAddress(ECX, stdinSlot);
            enc.store(ECX, 0, EAX);
        }
        if (!windows) {
            environmentSlot = addSlot();
            if (moduleRegistryPointer) {
                if (g_isLibraryMode) enc.load(EAX, ESP, 8);
                else dataAddress(EAX, moduleRegistry);
                dataAddress(ECX, moduleRegistryPointer);
                enc.store(ECX, 0, EAX);
            }
            if (g_isLibraryMode) enc.load(EAX, ESP, 12);
            else {
                enc.load(EAX, ESP);
                enc.shift(4, EAX, 2);
                enc.alu(Add, EAX, ESP);
                enc.alu(Add, EAX, 8); // argc, argv[0..argc-1], null, then envp
            }
            dataAddress(ECX, environmentSlot);
            enc.store(ECX, 0, EAX);
        }
        for (const auto& [name, layout] : structs) emitObjectDescriptor(name, layout, layout.decl->fields);
        for (const auto& [name, layout] : classes) emitObjectDescriptor(name, layout, layout.decl->fields, &layout);
        loadModules();
        for (const auto& [name, slot] : globals) {
            if (slot.type != ThavaType::STRING && !collection(slot.type)) continue;
            defaultValue(slot.type);
            storeValue(slot, slot.type);
        }
        beginFrame(ast, true);
        for (const auto& node : ast) {
            if (dynamic_cast<FunctionDeclNode*>(node.get()) || dynamic_cast<ForwardDeclNode*>(node.get()) ||
                dynamic_cast<ClassDeclNode*>(node.get()) || dynamic_cast<StructDeclNode*>(node.get()) ||
                dynamic_cast<EnumDeclNode*>(node.get()))
                continue;
            statement(node.get());
        }
        if (functionCleanup.offset) callCleanup(functionCleanup);
        if (g_isLibraryMode) {
            dataAddress(ECX, moduleState);
            enc.imm(EAX, 2);
            enc.store(ECX, 0, EAX);
            enc.leave();
            enc.bind(initialized);
            enc.imm(EAX, 1);
            enc.pop(EBX);
            enc.ret();
            enc.bind(initializing);
            enc.alu(Xor, EAX, EAX);
            enc.pop(EBX);
            enc.ret();
        } else {
            printText("\n[Thava Native] Build and run successful!\n");
            exitProcess(0);
        }
        emitFunctionCleanup();

        for (const auto& entry : functions) emitFunction(entry.second);
        for (const auto& entry : structs) emitStructInitializer(entry.second);
        for (const auto& entry : classes) {
            emitClassInitializer(entry.second);
            for (const auto& ctor : entry.second.constructors) emitFunction(ctor);
            for (const auto& method : entry.second.methods) emitFunction(method.second);
        }
        emitOutput();
        emitDivision();
        emitUnwind();
        emitInput();
        emitFileCallbacks();
        for (const auto& [target, entry] : methodBridges) {
            enc.bind(entry.second);
            emitRawBridge(entry.first);
        }
        for (const auto& [target, entry] : boxedBridges) {
            enc.bind(entry.second);
            emitBoxedBridge(entry.first);
        }
        emitAllocator();
        emitModuleThunks();
        const auto metadata = g_isLibraryMode ? emitModuleExports(output) : std::string();
        if (!runtimeCalls.empty()) {
            const auto base = enc.code.size();
            for (const auto& [offset, label] : runtimeCalls) enc.bindAt(label, base + offset);
            enc.code.insert(enc.code.end(), std::begin(runtime::code), std::end(runtime::code));
        }
        if (decimalEntry) {
            constexpr size_t alignment = 16;
            enc.code.resize((enc.code.size() + alignment - 1) & ~(alignment - 1));
            const auto first = std::begin(decimal_runtime::i686);
            const auto last = std::end(decimal_runtime::i686);
            enc.bindAt(*decimalEntry, enc.code.size() + decimal_runtime::i686Entry);
            enc.code.insert(enc.code.end(), first, last);
        }
        for (const auto& fixup : textFixups)
            enc.patch(fixup.at, ElfI686Emitter::textRva + static_cast<uint32_t>(enc.offset(fixup.offset)));
        if (!enc.resolveBranches()) throw std::runtime_error("Unresolved native i686 branch.");

        uint32_t dataRva = ElfI686Emitter::dataRva(enc.code.size());
        if (windows) {
            pe.setTextBytes(enc.code);
            dataRva = pe.getRdataRva();
        }
        if (g_isLibraryMode) {
            addString(metadata + "data:" + std::to_string(dataRva) + "\ninit:0\n__THAVA_METADATA_END__\n");
        }
        if (windows) pe.layoutRdata();
        for (const auto& fixup : dataFixups) enc.patch(fixup.at, dataRva + fixup.offset);
        for (const auto& fixup : importFixups) enc.patch(fixup.first, pe.getImportIatRva(fixup.second));
        if (!windows) {
            if (elfData.empty()) elfData.push_back(0);
            return ElfI686Emitter::write(output, enc.code, elfData, g_isLibraryMode);
        }
        pe.setTextBytes(enc.code);
        return pe.buildAndWrite(output);
    }
};

} // namespace i686

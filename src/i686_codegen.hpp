#pragma once

#include "elf_i686_emitter.hpp"
#include "i686_encoder.hpp"
#include "i686_runtime_data.hpp"
#include "i686_os_ops.h"
#include "unicode_case.hpp"
#include <stdexcept>

namespace i686 {

// Thava's internal i686 convention uses EDX:EAX for scalar results and one
// 8-byte stack slot per argument. EBX remains the image base across calls.
class Codegen {
    struct Shape { ThavaType key = ThavaType::UNKNOWN, element = ThavaType::UNKNOWN; };
    struct Slot {
        Reg base;
        int32_t offset;
        ThavaType type;
        std::string width;
        Shape shape{};
        bool indirect = false;
    };
    struct LoopSlots { Slot first, second; };
    struct LoopTarget { size_t end, next, scopes; };
    struct Cleanup { int32_t offset; size_t action; };
    enum RecordField { Previous = 0, Frame = 4, Action = 8, Stack = 12, Defers = 16, RecordBytes = 24 };
    enum DeferField { NextDefer = 0, DeferAction = 4, DeferBytes = 8 };
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
    Shape lastShape;
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
    uint32_t cleanupHead = 0, pendingException = 0;
    size_t unwindLabel = enc.label(), runDefersLabel = enc.label();
    struct OpenFile { std::string name; const Slot* slot; Cleanup cleanup; };
    std::vector<OpenFile> activeFiles;
    bool fileUsed = false;
    size_t fileReadLabel = enc.label(), fileWriteLabel = enc.label();
    const FunctionDeclNode* currentFunction = nullptr;
    size_t returnLabel = 0;
    size_t writeLabel = enc.label(), printStringLabel = enc.label(), printIntLabel = enc.label();
    size_t compareStringsLabel = enc.label(), divideLabel = enc.label(), errorLabel = enc.label();
    uint32_t stdoutSlot = 0, stdinSlot = 0;
    size_t readLineLabel = enc.label(), readByteLabel = enc.label();
    bool readUsed = false;
    uint32_t caseTable = UINT32_MAX;
    uint32_t environmentSlot = 0;
    uint32_t randomState = UINT32_MAX;
    size_t allocateLabel = enc.label();
    bool allocatorUsed = false;
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
        return t == ThavaType::INT || t == ThavaType::BOOL || t == ThavaType::STRING || floating(t);
    }

    static bool collection(ThavaType type) {
        return type == ThavaType::LIST || type == ThavaType::TUPLE || type == ThavaType::DICT;
    }

    static uint32_t comparisonKind(ThavaType type) {
        return type == ThavaType::STRING ? 1u : floating(type) ? 2u : 0u;
    }

    static ThavaType elementKind(ThavaType type) {
        // Real scalars share the IEEE-754 double representation in a collection.
        return floating(type) ? ThavaType::DOUBLE : type;
    }

    static void updateShape(ThavaType& slot, ThavaType value) {
        if (value == ThavaType::UNKNOWN) return;
        if (!scalar(value)) unsupported("A non-scalar collection element");
        value = elementKind(value);
        if (slot != ThavaType::UNKNOWN && elementKind(slot) != value)
            unsupported("A collection with mixed element types");
        slot = value;
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
        lastShape = slot.shape;
    }

    void normalize(ThavaType expected, const std::string& width, ThavaType actual) {
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

    Slot& lookup(const std::string& name) {
        if (auto found = locals.find(name); found != locals.end()) return found->second;
        if (auto found = globals.find(name); found != globals.end()) return found->second;
        unsupported("Variable '" + name + "'");
    }

    void storeVariable(Slot& slot, ThavaType actual) {
        if (collection(actual)) {
            updateShape(slot.shape.key, lastShape.key);
            updateShape(slot.shape.element, lastShape.element);
        }
        storeValue(slot, actual);
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
        lastShape = {};
        if (type == ThavaType::STRING) stringValue("");
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
        loadLeft();
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
        if (expression(node) != ThavaType::STRING) unsupported("This string operand");
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

    void readString(const std::string& prompt) {
        readUsed = true;
        printText(prompt);
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
            if (join) {
                if ((arg != ThavaType::LIST && arg != ThavaType::TUPLE) ||
                    (lastShape.element != ThavaType::STRING && lastShape.element != ThavaType::UNKNOWN))
                    unsupported("Joining a non-string collection");
            } else if (arg != ThavaType::STRING) unsupported("A non-string split delimiter");
            enc.store(ESP, 4, EAX);
            callRuntime(join ? runtime::rt_join : runtime::rt_split, 16);
            enc.alu(Xor, EDX, EDX);
            lastShape = join ? Shape{} : Shape{ThavaType::UNKNOWN, ThavaType::STRING};
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
        if (type == ThavaType::STRING) return;
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
        if (collection(type)) {
            const auto format = [](ThavaType item) {
                return item == ThavaType::BOOL ? 3u : comparisonKind(item);
            };
            const uint32_t mask = (type == ThavaType::DICT ? 0x100u : 0u) |
                                  (format(lastShape.element) << 4) | format(lastShape.key);
            pushAllocationContext();
            enc.pushImm(mask);
            enc.push(EAX);
            callRuntime(runtime::rt_collection_format, 16);
            enc.alu(Xor, EDX, EDX);
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

    // Preserve the new collection on the stack while evaluating each element.
    ThavaType collectionLiteral(CollectionNode* node) {
        const auto type = node->getIsTuple() ? ThavaType::TUPLE : ThavaType::LIST;
        defaultValue(type);
        enc.push(EAX);
        Shape shape;
        uint32_t index = 0;
        for (const auto& value : node->getElements()) {
            pushAllocationContext();
            enc.alu(Sub, ESP, 20);
            enc.load(EAX, ESP, 28);
            enc.store(ESP, 0, EAX);
            integer(index++);
            storeArgument(4);
            updateShape(shape.element, expression(value.get()));
            storeArgument(12);
            callRuntime(runtime::rt_list_insert, 28);
        }
        enc.pop(EAX);
        enc.alu(Xor, EDX, EDX);
        lastShape = shape;
        return type;
    }

    ThavaType dictLiteral(DictNode* node) {
        defaultValue(ThavaType::DICT);
        enc.push(EAX);
        Shape shape;
        for (const auto& entry : node->entries) {
            pushAllocationContext();
            enc.alu(Sub, ESP, 24);
            enc.load(EAX, ESP, 32);
            enc.store(ESP, 0, EAX);
            updateShape(shape.key, expression(entry.first.get()));
            storeArgument(4);
            updateShape(shape.element, expression(entry.second.get()));
            storeArgument(12);
            enc.imm(EAX, comparisonKind(shape.key));
            enc.store(ESP, 20, EAX);
            callRuntime(runtime::rt_dict_set, 32);
        }
        enc.pop(EAX);
        enc.alu(Xor, EDX, EDX);
        lastShape = shape;
        return ThavaType::DICT;
    }

    // The collection pointer is in EAX. Stage the key before the fallback.
    ThavaType collectionGet(ThavaType type, Shape shape, ASTNode* key, ASTNode* fallback = nullptr) {
        const bool dict = type == ThavaType::DICT;
        const uint32_t frame = dict ? 24 : 20;
        enc.alu(Sub, ESP, frame);
        enc.store(ESP, 0, EAX);
        const auto keyType = expression(key);
        if (!dict && keyType != ThavaType::INT) unsupported("A non-integer list index");
        if (dict && shape.key != ThavaType::UNKNOWN && elementKind(keyType) != shape.key)
            unsupported("A dictionary lookup with a different key type");
        storeArgument(4);
        if (dict) {
            enc.imm(EAX, comparisonKind(keyType));
            enc.store(ESP, 12, EAX);
        }
        if (fallback) updateShape(shape.element, expression(fallback));
        else {
            if (shape.element == ThavaType::UNKNOWN) shape.element = ThavaType::INT;
            defaultValue(shape.element);
        }
        storeArgument(dict ? 16 : 12);
        callRuntime(dict ? runtime::rt_dict_get : runtime::rt_list_get, frame);
        return shape.element;
    }

    uint32_t stageCollectionKey(ThavaType type, Shape shape, ASTNode* key) {
        const auto frame = type == ThavaType::DICT ? 16u : 12u;
        enc.alu(Sub, ESP, frame);
        enc.store(ESP, 0, EAX);
        const auto actual = expression(key);
        if (type != ThavaType::DICT && actual != ThavaType::INT) unsupported("A non-integer list index");
        if (type == ThavaType::DICT && shape.key != ThavaType::UNKNOWN && elementKind(actual) != shape.key)
            unsupported("A dictionary operation with a different key type");
        storeArgument(4);
        if (type == ThavaType::DICT) {
            enc.imm(EAX, comparisonKind(actual));
            enc.store(ESP, 12, EAX);
        }
        return frame;
    }

    void mutateCollection(Slot& slot, ASTNode* key, ASTNode* value, bool insert) {
        const bool dict = slot.type == ThavaType::DICT;
        if (slot.type != ThavaType::LIST && !dict) unsupported("Mutating this collection type");
        const bool allocate = dict || insert;
        if (allocate) pushAllocationContext();
        const uint32_t args = dict ? 24 : 20;
        enc.alu(Sub, ESP, args);
        loadValue(slot);
        enc.store(ESP, 0, EAX);
        if (key) {
            const auto actual = expression(key);
            if (dict) updateShape(slot.shape.key, actual);
            else if (actual != ThavaType::INT) unsupported("A non-integer list index");
        } else integer(0x7fffffffffffffffULL); // Append after the last element.
        storeArgument(4);
        updateShape(slot.shape.element, expression(value));
        storeArgument(12);
        if (dict) {
            enc.imm(EAX, comparisonKind(slot.shape.key));
            enc.store(ESP, 20, EAX);
        }
        callRuntime(dict ? runtime::rt_dict_set : insert ? runtime::rt_list_insert : runtime::rt_list_set,
                    args + (allocate ? 8 : 0));
    }

    static ASTNode* unwrapListIndex(ASTNode* node) {
        auto sub = dynamic_cast<CollectionNode*>(node);
        return sub && !sub->getIsTuple() && sub->getElements().size() == 1
            ? sub->getElements().front().get() : nullptr;
    }

    ThavaType listMethod(ListMethodNode* node) {
        auto& slot = lookup(node->varName);
        if (slot.type != ThavaType::LIST) unsupported("Mutating this collection type");
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
                loadValue(slot);
                const auto frame = stageCollectionKey(slot.type, slot.shape, index);
                callRuntime(runtime::rt_list_remove_at, frame);
            } else {
                enc.pushImm(comparisonKind(slot.shape.element));
                enc.alu(Sub, ESP, 12);
                loadValue(slot);
                enc.store(ESP, 0, EAX);
                const auto value = expression(node->args[0].get());
                if (slot.shape.element != ThavaType::UNKNOWN && slot.shape.element != elementKind(value))
                    unsupported("Removing a value of a different list element type");
                storeArgument(4);
                callRuntime(runtime::rt_list_remove_value, 16);
            }
        } else unsupported("List method '" + method + "'");
        return ThavaType::VOID;
    }

    ThavaType dictMethod(DictMethodNode* node) {
        if (expression(node->base.get()) != ThavaType::DICT) unsupported("A non-dictionary receiver");
        const auto shape = lastShape;
        const auto& method = node->method;
        if (method == "get" && (node->args.size() == 1 || node->args.size() == 2))
            return collectionGet(ThavaType::DICT, shape, node->args[0].get(),
                                 node->args.size() == 2 ? node->args[1].get() : nullptr);
        if ((method == "keys" || method == "values") && node->args.empty()) {
            pushAllocationContext();
            enc.pushImm(method == "keys");
            enc.push(EAX);
            callRuntime(runtime::rt_dict_project, 16);
            enc.alu(Xor, EDX, EDX);
            lastShape = {ThavaType::UNKNOWN, method == "keys" ? shape.key : shape.element};
            return ThavaType::LIST;
        }
        if (method == "clear" && node->args.empty()) {
            enc.push(EAX);
            callRuntime(runtime::rt_collection_clear, 4);
            return ThavaType::VOID;
        }
        const bool has = method == "has" || method == "contains";
        if ((has || method == "remove" || method == "del") && node->args.size() == 1) {
            const auto args = stageCollectionKey(ThavaType::DICT, shape, node->args[0].get());
            callRuntime(has ? runtime::rt_dict_has : runtime::rt_dict_remove, args);
            enc.alu(Xor, EDX, EDX);
            return has ? ThavaType::BOOL : ThavaType::VOID;
        }
        unsupported("Dictionary method '" + method + "'");
    }

    ThavaType membership(InNode* node) {
        const auto needle = expression(node->left.get());
        saveValue();
        const auto container = expression(node->listExpr.get());
        loadLeft();
        if (container == ThavaType::STRING && needle == ThavaType::STRING) {
            enc.push(ECX);
            enc.push(EAX);
            callRuntime(runtime::rt_contains, 8);
        } else if (collection(container)) {
            const auto type = container == ThavaType::DICT ? lastShape.key : lastShape.element;
            if (type != ThavaType::UNKNOWN && elementKind(needle) != elementKind(type)) {
                integer(0);
                return ThavaType::BOOL;
            }
            enc.pushImm(comparisonKind(needle));
            enc.push(ESI);
            enc.push(ECX);
            enc.push(EAX);
            callRuntime(container == ThavaType::DICT ? runtime::rt_dict_has : runtime::rt_list_find, 16);
            if (container != ThavaType::DICT) {
                const auto absent = enc.label(), done = enc.label();
                enc.alu(Cmp, EAX, uint32_t(-1));
                enc.jump(Equal, absent);
                enc.imm(EAX, 1);
                enc.jump(done);
                enc.bind(absent);
                enc.alu(Xor, EAX, EAX);
                enc.bind(done);
            }
        } else unsupported("Membership on this type");
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
            lastShape = lines ? Shape{ThavaType::UNKNOWN, ThavaType::STRING} : Shape{};
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
        enc.alu(Sub, ESP, 24);
        const auto type = expression(node->args[0].get());
        if (type != ThavaType::LIST && type != ThavaType::TUPLE)
            unsupported("Choosing from this type");
        const auto elem = lastShape.element == ThavaType::UNKNOWN ? ThavaType::INT : lastShape.element;
        enc.store(ESP, 0, EAX);
        auto weight = ThavaType::UNKNOWN;
        if (node->weightExpr) {
            const auto weights = expression(node->weightExpr.get());
            if (weights != ThavaType::LIST && weights != ThavaType::TUPLE)
                unsupported("Non-collection weights");
            weight = lastShape.element;
            if (weight != ThavaType::UNKNOWN && weight != ThavaType::INT && !floating(weight))
                unsupported("Non-numeric randres weights");
        } else integer(0);
        enc.store(ESP, 4, EAX);
        enc.imm(EAX, floating(weight));
        enc.store(ESP, 8, EAX);
        defaultValue(elem);
        storeArgument(12);
        dataAddress(EAX, randomState);
        enc.store(ESP, 20, EAX);
        callRuntime(runtime::rt_randres, 24);
        return elem;
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
        lastShape = method.operation == OS_LIST ? Shape{ThavaType::UNKNOWN, ThavaType::STRING} : Shape{};
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

    ThavaType expression(ASTNode* node) {
        if (auto lit = dynamic_cast<LiteralNode*>(node)) {
            const auto type = lit->getLitType();
            const auto value = lit->getVal();
            if (type == ThavaType::STRING) stringValue(value);
            else if (type == ThavaType::INT) integer(std::stoll(value));
            else if (type == ThavaType::BOOL) integer(value == "true");
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
            const auto& slot = lookup(var->getName());
            loadValue(slot);
            return slot.type;
        }
        if (auto unary = dynamic_cast<UnaryMinusNode*>(node)) {
            if (auto magnitude = dynamic_cast<LiteralNode*>(unary->expr.get());
                magnitude && magnitude->getLitType() == ThavaType::INT &&
                magnitude->getVal() == "9223372036854775808") {
                integer(uint64_t(1) << 63);
                return ThavaType::INT;
            }
            const auto type = expression(unary->expr.get());
            if (floating(type)) enc.alu(Xor, EDX, 0x80000000u);
            else if (type == ThavaType::INT) negate(EAX, EDX);
            else unsupported("Negating this type");
            return type;
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
        if (auto input = dynamic_cast<InputNode*>(node)) {
            readString(input->prompt);
            parseScalar(input->inputType);
            return input->inputType;
        }
        if (auto input = dynamic_cast<InputCaseNode*>(node)) {
            readString(input->prompt);
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
            if (collection(type)) return collectionGet(type, lastShape, index->indexExpr.get());
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
            if (type != ThavaType::STRING && !collection(type)) unsupported("Length of this type");
            enc.push(EAX);
            callRuntime(type == ThavaType::STRING ? runtime::rt_len : runtime::rt_collection_len, 4);
            enc.signExtend();
            return ThavaType::INT;
        }
        if (auto contains = dynamic_cast<InNode*>(node)) return membership(contains);
        if (auto random = dynamic_cast<RandCallNode*>(node)) return randomCall(random);
        if (auto call = dynamic_cast<CallNode*>(node)) {
            const auto fn = functions.find(call->name);
            if (fn == functions.end()) unsupported("Call to '" + call->name + "'");
            const auto& callee = *fn->second.decl;
            if (callee.params.size() != call->args.size()) unsupported("Arguments to '" + call->name + "'");
            const bool references = std::any_of(callee.params.begin(), callee.params.end(),
                                                [](const auto& param) { return collection(param.first); });
            if (call->args.size() > (references ? 255u : 510u))
                unsupported("A call frame larger than 4080 bytes");
            const auto bytes = static_cast<uint32_t>((references ? 16 : 8) * call->args.size());
            if (bytes) enc.alu(Sub, ESP, bytes);
            for (size_t i = 0; i < call->args.size(); ++i) {
                const auto type = expression(call->args[i].get());
                const auto width = i < callee.paramCustomTypes.size() ? callee.paramCustomTypes[i] : "";
                const auto formal = callee.params[i].first;
                if (collection(formal)) {
                    if (formal != type) unsupported("Collection argument type mismatch");
                    auto parameter = g_funcListParamElem.find(callee.name);
                    if (parameter != g_funcListParamElem.end() && i < parameter->second.size() &&
                        parameter->second[i] != ThavaType::UNKNOWN && lastShape.element != ThavaType::UNKNOWN &&
                        parameter->second[i] != lastShape.element)
                        unsupported("A collection argument with incompatible element types");
                    if (auto var = dynamic_cast<VarNode*>(call->args[i].get())) slotAddress(lookup(var->getName()), EAX);
                    else {
                        const auto temporary = int32_t(8 * (call->args.size() + i));
                        storeValue({ESP, temporary, type, ""}, type);
                        enc.lea(EAX, ESP, temporary);
                    }
                    enc.store(ESP, int32_t(8 * i), EAX);
                } else storeValue({ESP, int32_t(8 * i), formal, width}, type);
            }
            enc.call(fn->second.entry);
            if (bytes) enc.alu(Add, ESP, bytes);
            return callee.returnType;
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
        const auto type = expression(node->collectionExpr.get());
        if (type != ThavaType::LIST && type != ThavaType::TUPLE) unsupported("Iteration over this type");
        sequence.type = type;
        sequence.shape = lastShape;
        if (variable.type == ThavaType::UNKNOWN) variable.type = sequence.shape.element;
        if (!scalar(variable.type)) unsupported("Unknown foreach element type");
        const auto element = sequence.shape.element == ThavaType::UNKNOWN ? variable.type : sequence.shape.element;
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

        enc.alu(Sub, ESP, 20);
        loadValue(sequence);
        enc.store(ESP, 0, EAX);
        loadValue(index);
        storeArgument(4);
        defaultValue(element);
        storeArgument(12);
        callRuntime(runtime::rt_list_get, 20);
        storeValue(variable, element);
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

    void enterCleanup(Cleanup record) {
        dataAddress(EDI, cleanupHead);
        enc.load(EAX, EDI);
        enc.store(EBP, record.offset + Previous, EAX);
        enc.store(EBP, record.offset + Frame, EBP);
        codeAddress(EAX, record.action);
        enc.store(EBP, record.offset + Action, EAX);
        enc.store(EBP, record.offset + Stack, ESP);
        enc.alu(Xor, EAX, EAX);
        enc.store(EBP, record.offset + Defers, EAX);
        enc.lea(EAX, EBP, record.offset);
        enc.store(EDI, 0, EAX);
    }

    void popCleanup(Cleanup record) {
        enc.load(EAX, EBP, record.offset + Previous);
        dataAddress(EDI, cleanupHead);
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
        dataAddress(EDI, pendingException);
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
                dataAddress(EDI, pendingException);
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
                dataAddress(EAX, pendingException);
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
        if (auto decl = dynamic_cast<VarDeclNode*>(node)) {
            auto& slot = lookup(decl->varName);
            if (decl->initExpr) storeVariable(slot, expression(decl->initExpr.get()));
            else {
                defaultValue(slot.type);
                storeValue(slot, slot.type);
            }
        } else if (auto assign = dynamic_cast<AssignNode*>(node)) {
            auto& slot = lookup(assign->name);
            if (assign->op == "=") storeVariable(slot, expression(assign->expr.get()));
            else {
                loadValue(slot);
                saveValue();
                const auto value = expression(assign->expr.get());
                storeValue(slot, emitBinary(assign->op.substr(0, assign->op.size() - 1), slot.type, value));
            }
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
            if (index->op != "=") unsupported("A compound indexed assignment");
            mutateCollection(lookup(index->varName), index->indexExpr.get(), index->valExpr.get(), false);
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
            if (!scalar(decl->varType) && !collection(decl->varType))
                unsupported("Type of variable '" + decl->varName + "'");
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
            for (size_t i = 0; i < fn.params.size(); ++i) {
                const auto& [type, name] = fn.params[i];
                if (!scalar(type) && !collection(type)) unsupported("Parameter '" + name + "' of '" + fn.name + "'");
                const auto width = i < fn.paramCustomTypes.size() ? fn.paramCustomTypes[i] : "";
                Slot slot{EBP, int32_t(8 + 8 * i), type, width};
                slot.indirect = collection(type);
                auto info = g_funcListParamElem.find(fn.name);
                if (slot.indirect && info != g_funcListParamElem.end() && i < info->second.size())
                    slot.shape.element = info->second[i];
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
        dataAddress(EDI, pendingException);
        dataAddress(EAX, addString("Division by zero"));
        enc.store(EDI, 0, EAX);
        dataAddress(EAX, addString("RuntimeError"));
        enc.store(EDI, 4, EAX);

        enc.bind(unwindLabel);
        dataAddress(EDI, cleanupHead);
        enc.load(ECX, EDI);
        enc.alu(Cmp, ECX, 0);
        const auto fatal = enc.label();
        enc.jump(Equal, fatal);
        // Cleanups may catch their own exception and then return to this unwind.
        dataAddress(EDI, pendingException);
        enc.load(EAX, EDI, 0);
        enc.push(EAX);
        enc.load(EAX, EDI, 4);
        enc.push(EAX);
        enc.load(EBP, ECX, Frame);
        enc.load(EAX, ECX, Action);
        enc.callReg(EAX);
        dataAddress(EDI, pendingException);
        enc.pop(EAX);
        enc.store(EDI, 4, EAX);
        enc.pop(EAX);
        enc.store(EDI, 0, EAX);
        enc.jump(unwindLabel);

        enc.bind(fatal);
        printText("[Runtime Exception]: ");
        dataAddress(EAX, pendingException);
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

public:
    explicit Codegen(bool windowsTarget) : windows(windowsTarget) {}

    bool compile(const std::vector<ASTPtr>& ast, const std::string& output) {
        if (g_isLibraryMode || g_hasDLAttach) unsupported("Dynamic libraries");
        cleanupHead = addSlot();
        pendingException = addSlot();
        for (const auto& node : ast) {
            if (auto func = dynamic_cast<FunctionDeclNode*>(node.get()))
                functions.emplace(func->name, Function{func, enc.label()});
            else if (auto decl = dynamic_cast<VarDeclNode*>(node.get())) {
                if (!scalar(decl->varType) && !collection(decl->varType)) unsupported("Type of global '" + decl->varName + "'");
                Slot slot{EBX, int32_t(addSlot()), decl->varType, decl->customTypeName};
                if (decl->varType == ThavaType::LIST || decl->varType == ThavaType::TUPLE)
                    slot.shape.element = g_programEnv->getListElem(decl->varName);
                globals.emplace(decl->varName, slot);
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
            enc.load(EAX, ESP);
            enc.shift(4, EAX, 2);
            enc.alu(Add, EAX, ESP);
            enc.alu(Add, EAX, 8); // argc, argv[0..argc-1], null, then envp
            dataAddress(ECX, environmentSlot);
            enc.store(ECX, 0, EAX);
        }
        for (const auto& [name, slot] : globals) {
            if (slot.type != ThavaType::STRING && !collection(slot.type)) continue;
            defaultValue(slot.type);
            storeValue(slot, slot.type);
        }
        beginFrame(ast, true);
        for (const auto& node : ast) {
            if (dynamic_cast<FunctionDeclNode*>(node.get()) || dynamic_cast<ForwardDeclNode*>(node.get()))
                continue;
            statement(node.get());
        }
        if (functionCleanup.offset) callCleanup(functionCleanup);
        exitProcess(0);
        emitFunctionCleanup();

        for (const auto& entry : functions) emitFunction(entry.second);
        emitOutput();
        emitDivision();
        emitUnwind();
        emitInput();
        emitFileCallbacks();
        emitAllocator();
        if (!runtimeCalls.empty()) {
            const auto base = enc.code.size();
            for (const auto& [offset, label] : runtimeCalls) enc.bindAt(label, base + offset);
            enc.code.insert(enc.code.end(), std::begin(runtime::code), std::end(runtime::code));
        }
        for (const auto& fixup : textFixups)
            enc.patch(fixup.at, ElfI686Emitter::textRva + static_cast<uint32_t>(enc.offset(fixup.offset)));
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

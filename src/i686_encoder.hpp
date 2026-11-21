#pragma once

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <vector>

namespace i686 {

enum Reg : uint8_t { EAX, ECX, EDX, EBX, ESP, EBP, ESI, EDI };
enum Alu : uint8_t { Add, Or, Adc, Sbb, And, Sub, Xor, Cmp };
enum Cond : uint8_t { Below = 2, AboveEqual, Equal, NotEqual, BelowEqual, Above,
                      Sign, NotSign, Less = 12, GreaterEqual, LessEqual, Greater };

class Encoder {
    struct Branch { size_t displacement, label; };
    std::vector<size_t> labels;
    std::vector<Branch> branches;

    void modrm(uint8_t mode, uint8_t field, Reg operand) {
        bytes({uint8_t((mode << 6) | (field << 3) | operand)});
    }
    void address(uint8_t field, Reg base, int32_t displacement) {
        modrm(2, field, base);
        if (base == ESP) bytes({0x24});
        word(displacement);
    }
    void branchTo(size_t label) {
        branches.push_back({code.size(), label});
        word(0);
    }

public:
    std::vector<uint8_t> code;

    void bytes(std::initializer_list<uint8_t> values) {
        code.insert(code.end(), values);
    }
    void word(uint32_t value) {
        for (int i = 0; i < 4; ++i) code.push_back(uint8_t(value >> (8 * i)));
    }
    void patch(size_t at, uint32_t value) {
        for (int i = 0; i < 4; ++i) code.at(at + i) = uint8_t(value >> (8 * i));
    }

    size_t label() { labels.push_back(size_t(-1)); return labels.size() - 1; }
    void bind(size_t label) { labels.at(label) = code.size(); }
    bool resolveBranches() {
        for (const auto& branch : branches) {
            const auto target = labels.at(branch.label);
            if (target == size_t(-1)) return false;
            patch(branch.displacement, uint32_t(target - (branch.displacement + 4)));
        }
        return true;
    }
    void jump(size_t label) { bytes({0xe9}); branchTo(label); }
    void jump(Cond cond, size_t label) { bytes({0x0f, uint8_t(0x80 + cond)}); branchTo(label); }
    void call(size_t label) { bytes({0xe8}); branchTo(label); }
    size_t callImport() { bytes({0xff, 0x93}); const auto at = code.size(); word(0); return at; }
    void ret() { bytes({0xc3}); }
    void leave() { bytes({0xc9}); }
    void syscall() { bytes({0xcd, 0x80}); }

    void mov(Reg dest, Reg src) { bytes({0x89}); modrm(3, src, dest); }
    void imm(Reg dest, uint32_t value) { bytes({uint8_t(0xb8 + dest)}); word(value); }
    void load(Reg dest, Reg base, int32_t off = 0) { bytes({0x8b}); address(dest, base, off); }
    void store(Reg base, int32_t off, Reg src) { bytes({0x89}); address(src, base, off); }
    void loadByte(Reg dest, Reg base) { bytes({0x0f, 0xb6}); address(dest, base, 0); }
    // Only the four low byte registers are available in 32-bit mode.
    void storeByte(Reg base, Reg src) { bytes({0x88}); address(src, base, 0); }
    void lea(Reg dest, Reg base, int32_t off) { bytes({0x8d}); address(dest, base, off); }
    void push(Reg value) { bytes({uint8_t(0x50 + value)}); }
    void pushImm(uint32_t value) { bytes({0x68}); word(value); }
    void pop(Reg value) { bytes({uint8_t(0x58 + value)}); }
    void decrement(Reg base, int32_t off) { bytes({0xff}); address(1, base, off); }

    void alu(Alu op, Reg dest, Reg src) { bytes({uint8_t(op * 8 + 1)}); modrm(3, src, dest); }
    void alu(Alu op, Reg dest, uint32_t value) { bytes({0x81}); modrm(3, op, dest); word(value); }
    void unary(uint8_t op, Reg value) { bytes({0xf7}); modrm(3, op, value); }
    void shift(uint8_t op, Reg value, uint8_t count) { bytes({0xc1}); modrm(3, op, value); bytes({count}); }
    void shiftCL(uint8_t op, Reg value) { bytes({0xd3}); modrm(3, op, value); }
    void shiftPairCL(bool right, Reg dest, Reg src) {
        bytes({0x0f, uint8_t(right ? 0xad : 0xa5)}); modrm(3, src, dest);
    }
    void imul(Reg dest, Reg src) { bytes({0x0f, 0xaf}); modrm(3, dest, src); }
    void signExtend() { bytes({0x99}); } // CDQ
    void signExtendWord() { bytes({0x0f, 0xbf, 0xc0}); }
};

} // namespace i686

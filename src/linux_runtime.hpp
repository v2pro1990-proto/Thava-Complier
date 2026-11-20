#pragma once

#include "elf_emitter.hpp"
#include "x86_encoder.hpp"
#include <string>
#include <vector>
#include <map>
#include <cstdint>

class LinuxNativeRuntime {
public:
    x86::Encoder& enc;
    ElfEmitter& elf;

    // Offsets of global variables in .data
    uint32_t offRandSeed = 0;
    uint32_t offHeapCur  = 0;
    uint32_t offHeapTop  = 0;
    uint32_t offReadBuf  = 0;  // 1024-byte input buffer
    uint32_t offStrNL    = 0;  // "\n"
    uint32_t offDoubleMillion = 0; // 1000000.0
    uint32_t offDoubleHalf    = 0; // 0.5
    uint32_t offDoubleTen     = 0; // 10.0 (atof exponent scaling)
    uint32_t offShellPath     = 0; // "/bin/sh"
    uint32_t offDashC         = 0; // "-c"

    LinuxNativeRuntime(x86::Encoder& e, ElfEmitter& el)
        : enc(e), elf(el) {}

    void emitAndRegister() {
        // 1. Allocate globals in .data
        offRandSeed = elf.addDoubleLiteralOffset(12345.0);
        offHeapCur  = elf.addDoubleLiteralOffset(0.0);
        offHeapTop  = elf.addDoubleLiteralOffset(0.0);
        offStrNL    = elf.addStringLiteralOffset("\n");
        offDoubleMillion = elf.addDoubleLiteralOffset(1000000.0);
        offDoubleHalf    = elf.addDoubleLiteralOffset(0.5);
        offDoubleTen     = elf.addDoubleLiteralOffset(10.0);
        offShellPath     = elf.addStringLiteralOffset("/bin/sh");
        offDashC         = elf.addStringLiteralOffset("-c");

        // Allocate 1024 bytes buffer in .data for console input
        std::string zeroBuf(1024, '\0');
        offReadBuf = elf.addStringLiteralOffset(zeroBuf);

        // 2. Emit helper routines into .text and register them with ElfEmitter

        // --- A. _thava_linux_noop ---
        size_t lblNoop = enc.createLabel();
        enc.bindLabel(lblNoop);
        uint32_t rvaNoop = elf.textRva + (uint32_t)enc.code.size();
        enc.xor_r32(x86::RAX, x86::RAX);
        enc.ret();

        elf.registerFuncPointer("SetConsoleCP", rvaNoop);
        elf.registerFuncPointer("SetConsoleOutputCP", rvaNoop);
        elf.registerFuncPointer("fflush", rvaNoop);
        elf.registerFuncPointer("free", rvaNoop);

        // --- B. _thava_linux_exit (code in RCX) ---
        size_t lblExit = enc.createLabel();
        enc.bindLabel(lblExit);
        uint32_t rvaExit = elf.textRva + (uint32_t)enc.code.size();
        enc.mov_rr(x86::RDI, x86::RCX);     // rdi = exit_code
        enc.mov_ri64(x86::RAX, 60);         // sys_exit = 60
        enc.syscall();
        elf.registerFuncPointer("exit", rvaExit);
        elf.registerFuncPointer("ExitProcess", rvaExit);

        // --- C. _thava_linux_strlen (str in RCX -> RAX) ---
        size_t lblStrlen = enc.createLabel();
        enc.bindLabel(lblStrlen);
        uint32_t rvaStrlen = elf.textRva + (uint32_t)enc.code.size();
        size_t doneStrlen = enc.createLabel();
        size_t loopStrlen = enc.createLabel();

        enc.xor_r32(x86::RAX, x86::RAX);
        enc.test_rr(x86::RCX, x86::RCX);
        enc.je(doneStrlen);

        enc.mov_rr(x86::RAX, x86::RCX);
        enc.bindLabel(loopStrlen);
        enc.movzx_rm8(x86::RDX, x86::RAX, 0);
        enc.test_rr(x86::RDX, x86::RDX);
        enc.je(doneStrlen);
        enc.inc_r(x86::RAX);
        enc.jmp(loopStrlen);

        enc.bindLabel(doneStrlen);
        enc.sub_rr(x86::RAX, x86::RCX);
        enc.ret();
        elf.registerFuncPointer("strlen", rvaStrlen);

        // --- D. _thava_linux_strcpy (dst in RCX, src in RDX -> RAX dst) ---
        size_t lblStrcpy = enc.createLabel();
        enc.bindLabel(lblStrcpy);
        uint32_t rvaStrcpy = elf.textRva + (uint32_t)enc.code.size();
        size_t loopStrcpy = enc.createLabel();

        enc.mov_rr(x86::RAX, x86::RCX);
        enc.bindLabel(loopStrcpy);
        enc.movzx_rm8(x86::R8, x86::RDX, 0);
        enc.mov_mr8(x86::RCX, 0, x86::R8);
        enc.inc_r(x86::RCX);
        enc.inc_r(x86::RDX);
        enc.test_rr(x86::R8, x86::R8);
        enc.jne(loopStrcpy);
        enc.ret();
        elf.registerFuncPointer("strcpy", rvaStrcpy);

        // --- E. _thava_linux_strcat (dst in RCX, src in RDX -> RAX dst) ---
        size_t lblStrcat = enc.createLabel();
        enc.bindLabel(lblStrcat);
        uint32_t rvaStrcat = elf.textRva + (uint32_t)enc.code.size();
        size_t loopFindCat = enc.createLabel();
        size_t loopCopyCat = enc.createLabel();

        enc.mov_rr(x86::RAX, x86::RCX);
        enc.bindLabel(loopFindCat);
        enc.movzx_rm8(x86::R8, x86::RCX, 0);
        enc.test_rr(x86::R8, x86::R8);
        enc.je(loopCopyCat);
        enc.inc_r(x86::RCX);
        enc.jmp(loopFindCat);

        enc.bindLabel(loopCopyCat);
        enc.movzx_rm8(x86::R8, x86::RDX, 0);
        enc.mov_mr8(x86::RCX, 0, x86::R8);
        enc.inc_r(x86::RCX);
        enc.inc_r(x86::RDX);
        enc.test_rr(x86::R8, x86::R8);
        enc.jne(loopCopyCat);
        enc.ret();
        elf.registerFuncPointer("strcat", rvaStrcat);

        // --- F. _thava_linux_strcmp (s1 in RCX, s2 in RDX -> RAX 0 if equal) ---
        size_t lblStrcmp = enc.createLabel();
        enc.bindLabel(lblStrcmp);
        uint32_t rvaStrcmp = elf.textRva + (uint32_t)enc.code.size();
        size_t loopCmp = enc.createLabel();
        size_t equalCmp = enc.createLabel();
        size_t diffCmp = enc.createLabel();

        enc.bindLabel(loopCmp);
        enc.movzx_rm8(x86::RAX, x86::RCX, 0);
        enc.movzx_rm8(x86::R8, x86::RDX, 0);
        enc.cmp_rr(x86::RAX, x86::R8);
        enc.jne(diffCmp);
        enc.test_rr(x86::RAX, x86::RAX);
        enc.je(equalCmp);
        enc.inc_r(x86::RCX);
        enc.inc_r(x86::RDX);
        enc.jmp(loopCmp);

        enc.bindLabel(equalCmp);
        enc.xor_r32(x86::RAX, x86::RAX);
        enc.ret();

        enc.bindLabel(diffCmp);
        enc.sub_rr(x86::RAX, x86::R8);
        enc.ret();
        elf.registerFuncPointer("strcmp", rvaStrcmp);

        // --- F2. _thava_linux_strstr (haystack in RCX, needle in RDX -> RAX ptr or 0) ---
        size_t lblStrstr = enc.createLabel();
        enc.bindLabel(lblStrstr);
        uint32_t rvaStrstr = elf.textRva + (uint32_t)enc.code.size();
        size_t ssStart = enc.createLabel();
        size_t ssOuter = enc.createLabel();
        size_t ssInner = enc.createLabel();
        size_t ssFound = enc.createLabel();
        size_t ssNext = enc.createLabel();
        size_t ssNotFound = enc.createLabel();

        // Empty needle -> haystack
        enc.movzx_rm8(x86::RAX, x86::RDX, 0);
        enc.test_rr(x86::RAX, x86::RAX);
        enc.jne(ssStart);
        enc.mov_rr(x86::RAX, x86::RCX);
        enc.ret();

        enc.bindLabel(ssStart);
        enc.mov_rr(x86::R8, x86::RCX); // current haystack position
        enc.bindLabel(ssOuter);
        enc.movzx_rm8(x86::RAX, x86::R8, 0);
        enc.test_rr(x86::RAX, x86::RAX);
        enc.je(ssNotFound);
        enc.mov_rr(x86::R9, x86::RDX); // needle cursor
        enc.mov_rr(x86::R10, x86::R8); // haystack cursor
        enc.bindLabel(ssInner);
        enc.movzx_rm8(x86::RAX, x86::R9, 0);
        enc.test_rr(x86::RAX, x86::RAX);
        enc.je(ssFound);
        enc.movzx_rm8(x86::R11, x86::R10, 0);
        enc.cmp_rr(x86::R11, x86::RAX);
        enc.jne(ssNext);
        enc.inc_r(x86::R9);
        enc.inc_r(x86::R10);
        enc.jmp(ssInner);
        enc.bindLabel(ssNext);
        enc.inc_r(x86::R8);
        enc.jmp(ssOuter);
        enc.bindLabel(ssFound);
        enc.mov_rr(x86::RAX, x86::R8);
        enc.ret();
        enc.bindLabel(ssNotFound);
        enc.xor_r32(x86::RAX, x86::RAX);
        enc.ret();
        elf.registerFuncPointer("strstr", rvaStrstr);

        // --- F3. _thava_linux_strncmp (a in RCX, b in RDX, n in R8 -> int) ---
        size_t lblStrncmp = enc.createLabel();
        enc.bindLabel(lblStrncmp);
        uint32_t rvaStrncmp = elf.textRva + (uint32_t)enc.code.size();
        size_t snLoop = enc.createLabel();
        size_t snDiff = enc.createLabel();
        size_t snEqual = enc.createLabel();
        enc.bindLabel(snLoop);
        enc.test_rr(x86::R8, x86::R8);
        enc.je(snEqual);
        enc.movzx_rm8(x86::RAX, x86::RCX, 0);
        enc.movzx_rm8(x86::R9, x86::RDX, 0);
        enc.cmp_rr(x86::RAX, x86::R9);
        enc.jne(snDiff);
        enc.test_rr(x86::RAX, x86::RAX);
        enc.je(snEqual);
        enc.inc_r(x86::RCX);
        enc.inc_r(x86::RDX);
        enc.dec_r(x86::R8);
        enc.jmp(snLoop);
        enc.bindLabel(snDiff);
        enc.sub_rr(x86::RAX, x86::R9);
        enc.ret();
        enc.bindLabel(snEqual);
        enc.xor_r32(x86::RAX, x86::RAX);
        enc.ret();
        elf.registerFuncPointer("strncmp", rvaStrncmp);

        // --- F4. _thava_linux_memset (dst in RCX, byte in RDX, n in R8 -> RAX dst) ---
        size_t lblMemset = enc.createLabel();
        enc.bindLabel(lblMemset);
        uint32_t rvaMemset = elf.textRva + (uint32_t)enc.code.size();
        size_t msLoop = enc.createLabel();
        size_t msDone = enc.createLabel();
        enc.mov_rr(x86::R9, x86::RCX);
        enc.test_rr(x86::R8, x86::R8);
        enc.je(msDone);
        enc.bindLabel(msLoop);
        enc.mov_mr8(x86::R9, 0, x86::RDX);
        enc.inc_r(x86::R9);
        enc.dec_r(x86::R8);
        enc.jne(msLoop);
        enc.bindLabel(msDone);
        enc.mov_rr(x86::RAX, x86::RCX);
        enc.ret();
        elf.registerFuncPointer("memset", rvaMemset);

        // --- F5. _thava_linux_pow (x in XMM0, y in XMM1 -> XMM0) via x87 ---
        size_t lblPow = enc.createLabel();
        enc.bindLabel(lblPow);
        uint32_t rvaPow = elf.textRva + (uint32_t)enc.code.size();
        enc.push(x86::RBP);
        enc.mov_rr(x86::RBP, x86::RSP);
        enc.sub_rsp(32);
        enc.movsd_mr_rbp(-8, x86::XMM0);  // x
        enc.movsd_mr_rbp(-16, x86::XMM1); // y
        enc.emit8(0xDD); enc.emit8(0x45); enc.emit8(0xF0); // fld qword [rbp-16]
        enc.emit8(0xDD); enc.emit8(0x45); enc.emit8(0xF8); // fld qword [rbp-8]
        enc.emit8(0xD9); enc.emit8(0xF1);                   // fyl2x -> y*log2(x)
        enc.emit8(0xD9); enc.emit8(0xC0);                   // fld st0
        enc.emit8(0xD9); enc.emit8(0xFC);                   // frndint -> k
        enc.emit8(0xD9); enc.emit8(0xC9);                   // fxch st1
        enc.emit8(0xD8); enc.emit8(0xE1);                   // fsub st0, st1 -> f
        enc.emit8(0xD9); enc.emit8(0xF0);                   // f2xm1 -> 2^f - 1
        enc.emit8(0xD9); enc.emit8(0xE8);                   // fld1
        enc.emit8(0xDE); enc.emit8(0xC1);                   // faddp -> 2^f
        enc.emit8(0xD9); enc.emit8(0xFD);                   // fscale -> 2^f * 2^k
        enc.emit8(0xDD); enc.emit8(0xD9);                   // fstp st1
        enc.emit8(0xDD); enc.emit8(0x5D); enc.emit8(0xF8);  // fstp qword [rbp-8]
        enc.movsd_rm_rbp(x86::XMM0, -8);
        enc.mov_rr(x86::RSP, x86::RBP);
        enc.pop(x86::RBP);
        enc.ret();
        elf.registerFuncPointer("pow", rvaPow);

        // --- F6. _thava_linux_system (cmd in RCX -> exit status) ---
        size_t lblSystem = enc.createLabel();
        enc.bindLabel(lblSystem);
        uint32_t rvaSystem = elf.textRva + (uint32_t)enc.code.size();
        size_t sysParent = enc.createLabel();
        size_t sysFail = enc.createLabel();
        size_t sysDone = enc.createLabel();
        enc.push(x86::RBP);
        enc.mov_rr(x86::RBP, x86::RSP);
        enc.sub_rsp(64);
        enc.mov_mr_rbp(-8, x86::RCX); // cmd
        enc.mov_ri64(x86::RAX, 57);   // sys_fork
        enc.syscall();
        enc.test_rr(x86::RAX, x86::RAX);
        enc.jl(sysFail);
        enc.jne(sysParent);

        // child: execve("/bin/sh", { "/bin/sh", "-c", cmd, 0 }, 0)
        enc.lea_rip_rdata(x86::RDI, offShellPath);
        enc.lea_rip_rdata(x86::RDX, offDashC);
        enc.lea_rm_rbp(x86::R10, -56);
        enc.mov_mr(x86::R10, 0, x86::RDI);
        enc.mov_mr(x86::R10, 8, x86::RDX);
        enc.mov_rm_rbp(x86::R9, -8);
        enc.mov_mr(x86::R10, 16, x86::R9);
        enc.xor_r32(x86::R11, x86::R11);
        enc.mov_mr(x86::R10, 24, x86::R11);
        enc.mov_rr(x86::RSI, x86::R10);
        enc.xor_r32(x86::RDX, x86::RDX);
        enc.mov_ri64(x86::RAX, 59); // sys_execve
        enc.syscall();
        enc.mov_ri64(x86::RAX, 60); // sys_exit
        enc.mov_ri64(x86::RDI, 127);
        enc.syscall();

        enc.bindLabel(sysParent);
        enc.mov_rr(x86::RDI, x86::RAX);
        enc.lea_rm_rbp(x86::RSI, -16);
        enc.xor_r32(x86::RDX, x86::RDX);
        enc.xor_r32(x86::R10, x86::R10);
        enc.mov_ri64(x86::RAX, 61); // sys_wait4
        enc.syscall();
        enc.mov_rm_rbp(x86::RAX, -16);
        enc.shr_ri(x86::RAX, 8);
        enc.mov_ri64(x86::R10, 0xFF);
        enc.and_rr(x86::RAX, x86::R10);
        enc.jmp(sysDone);

        enc.bindLabel(sysFail);
        enc.mov_ri64(x86::RAX, -1);
        enc.bindLabel(sysDone);
        enc.mov_rr(x86::RSP, x86::RBP);
        enc.pop(x86::RBP);
        enc.ret();
        elf.registerFuncPointer("system", rvaSystem);

        // --- G. _thava_linux_memcpy (dst in RCX, src in RDX, count in R8 -> RAX dst) ---
        size_t lblMemcpy = enc.createLabel();
        enc.bindLabel(lblMemcpy);
        uint32_t rvaMemcpy = elf.textRva + (uint32_t)enc.code.size();
        size_t loopCpy = enc.createLabel();
        size_t doneCpy = enc.createLabel();

        enc.mov_rr(x86::RAX, x86::RCX);
        enc.bindLabel(loopCpy);
        enc.test_rr(x86::R8, x86::R8);
        enc.je(doneCpy);
        enc.movzx_rm8(x86::R9, x86::RDX, 0);
        enc.mov_mr8(x86::RCX, 0, x86::R9);
        enc.inc_r(x86::RCX);
        enc.inc_r(x86::RDX);
        enc.dec_r(x86::R8);
        enc.jmp(loopCpy);

        enc.bindLabel(doneCpy);
        enc.ret();
        elf.registerFuncPointer("memcpy", rvaMemcpy);

        // --- H. _thava_linux_malloc (size in RCX -> RAX ptr) via sys_brk ---
        size_t lblMalloc = enc.createLabel();
        enc.bindLabel(lblMalloc);
        uint32_t rvaMalloc = elf.textRva + (uint32_t)enc.code.size();
        size_t initBrk = enc.createLabel();
        size_t checkTop = enc.createLabel();
        size_t expandBrk = enc.createLabel();
        size_t returnChunk = enc.createLabel();

        // 16-byte align size: size = (size + 15) & ~15
        enc.mov_ri64(x86::R10, 15);
        enc.add_rr(x86::RCX, x86::R10);
        enc.mov_ri64(x86::R10, ~15LL);
        enc.and_rr(x86::RCX, x86::R10);

        // Check if heap initialized
        enc.lea_rip_rdata(x86::R11, offHeapCur);
        enc.mov_rm(x86::RAX, x86::R11, 0);
        enc.test_rr(x86::RAX, x86::RAX);
        enc.je(initBrk);
        enc.jmp(checkTop);

        enc.bindLabel(initBrk);
        // sys_brk(0) to get initial brk
        enc.push(x86::RCX);
        enc.mov_ri64(x86::RAX, 12); // sys_brk = 12
        enc.xor_r32(x86::RDI, x86::RDI);
        enc.syscall();
        enc.pop(x86::RCX);

        enc.lea_rip_rdata(x86::R11, offHeapCur);
        enc.mov_mr(x86::R11, 0, x86::RAX);
        enc.lea_rip_rdata(x86::R11, offHeapTop);
        enc.mov_mr(x86::R11, 0, x86::RAX);

        enc.bindLabel(checkTop);
        // RAX = current
        enc.lea_rip_rdata(x86::R11, offHeapCur);
        enc.mov_rm(x86::RAX, x86::R11, 0);
        enc.mov_rr(x86::R8, x86::RAX);
        enc.add_rr(x86::R8, x86::RCX); // newCur in R8

        enc.lea_rip_rdata(x86::R11, offHeapTop);
        enc.mov_rm(x86::R9, x86::R11, 0);
        enc.cmp_rr(x86::R8, x86::R9);
        enc.jg(expandBrk);
        enc.jmp(returnChunk);

        enc.bindLabel(expandBrk);
        // grow by requested + 128KB
        enc.push(x86::RAX);
        enc.push(x86::RCX);
        enc.push(x86::R8);
        enc.mov_rr(x86::RDI, x86::R8);
        enc.mov_ri64(x86::R10, 131072);
        enc.add_rr(x86::RDI, x86::R10);
        enc.mov_ri64(x86::RAX, 12); // sys_brk = 12
        enc.syscall();

        enc.lea_rip_rdata(x86::R11, offHeapTop);
        enc.mov_mr(x86::R11, 0, x86::RAX);
        enc.pop(x86::R8);
        enc.pop(x86::RCX);
        enc.pop(x86::RAX);

        enc.bindLabel(returnChunk);
        enc.lea_rip_rdata(x86::R11, offHeapCur);
        enc.mov_mr(x86::R11, 0, x86::R8);
        enc.ret(); // old cur is in RAX
        elf.registerFuncPointer("malloc", rvaMalloc);
        elf.registerFuncPointer("realloc", rvaMalloc);

        // --- I. _thava_linux_time & rand & srand ---
        size_t lblTime = enc.createLabel();
        enc.bindLabel(lblTime);
        uint32_t rvaTime = elf.textRva + (uint32_t)enc.code.size();
        enc.mov_ri64(x86::RAX, 201); // sys_time = 201
        enc.mov_rr(x86::RDI, x86::RCX);
        enc.syscall();
        enc.ret();
        elf.registerFuncPointer("time", rvaTime);

        size_t lblSrand = enc.createLabel();
        enc.bindLabel(lblSrand);
        uint32_t rvaSrand = elf.textRva + (uint32_t)enc.code.size();
        enc.lea_rip_rdata(x86::R11, offRandSeed);
        enc.mov_mr(x86::R11, 0, x86::RCX);
        enc.ret();
        elf.registerFuncPointer("srand", rvaSrand);

        size_t lblRand = enc.createLabel();
        enc.bindLabel(lblRand);
        uint32_t rvaRand = elf.textRva + (uint32_t)enc.code.size();
        enc.lea_rip_rdata(x86::R11, offRandSeed);
        enc.mov_rm(x86::RAX, x86::R11, 0);
        enc.mov_ri64(x86::RDX, 6364136223846793005ULL);
        enc.imul_rr(x86::RAX, x86::RDX);
        enc.inc_r(x86::RAX);
        enc.mov_mr(x86::R11, 0, x86::RAX);
        enc.shr_ri(x86::RAX, 33);
        enc.mov_ri64(x86::RDX, 0x7FFFFFFF);
        enc.and_rr(x86::RAX, x86::RDX);
        enc.ret();
        elf.registerFuncPointer("rand", rvaRand);

        // --- J. _thava_linux_atoi64 (str in RCX -> int64 in RAX) ---
        size_t lblAtoi = enc.createLabel();
        enc.bindLabel(lblAtoi);
        uint32_t rvaAtoi = elf.textRva + (uint32_t)enc.code.size();
        size_t loopSkipWhite = enc.createLabel();
        size_t checkSign = enc.createLabel();
        size_t isNeg = enc.createLabel();
        size_t loopDigits = enc.createLabel();
        size_t doneAtoi = enc.createLabel();

        enc.xor_r32(x86::RAX, x86::RAX);   // result
        enc.xor_r32(x86::R8, x86::R8);     // sign (0 = pos, 1 = neg)
        enc.test_rr(x86::RCX, x86::RCX);
        enc.je(doneAtoi);

        enc.bindLabel(loopSkipWhite);
        enc.movzx_rm8(x86::RDX, x86::RCX, 0);
        enc.cmp_ri32(x86::RDX, ' ');
        enc.je(checkSign);
        enc.cmp_ri32(x86::RDX, '\t');
        enc.je(checkSign);
        enc.jmp(checkSign);

        enc.bindLabel(checkSign);
        enc.cmp_ri32(x86::RDX, '-');
        enc.je(isNeg);
        enc.cmp_ri32(x86::RDX, '+');
        size_t skipPlus = enc.createLabel();
        enc.je(skipPlus);
        enc.jmp(loopDigits);

        enc.bindLabel(isNeg);
        enc.mov_ri64(x86::R8, 1);
        enc.inc_r(x86::RCX);
        enc.jmp(loopDigits);

        enc.bindLabel(skipPlus);
        enc.inc_r(x86::RCX);

        enc.bindLabel(loopDigits);
        enc.movzx_rm8(x86::RDX, x86::RCX, 0);
        enc.cmp_ri32(x86::RDX, '0');
        enc.jl(doneAtoi);
        enc.cmp_ri32(x86::RDX, '9');
        enc.jg(doneAtoi);

        enc.mov_ri64(x86::R10, 10);
        enc.imul_rr(x86::RAX, x86::R10);
        enc.sub_ri32(x86::RDX, '0');
        enc.add_rr(x86::RAX, x86::RDX);
        enc.inc_r(x86::RCX);
        enc.jmp(loopDigits);

        enc.bindLabel(doneAtoi);
        enc.test_rr(x86::R8, x86::R8);
        size_t notNeg = enc.createLabel();
        enc.je(notNeg);
        enc.neg_r(x86::RAX);
        enc.bindLabel(notNeg);
        enc.ret();
        elf.registerFuncPointer("atoi", rvaAtoi);
        elf.registerFuncPointer("_atoi64", rvaAtoi);

        // --- K. _thava_linux_atof (str in RCX -> double in XMM0) ---
        // Full decimal parse: optional blanks, sign, integer digits, fraction
        // digits and an optional e/E exponent. The previous version reused the
        // integer parser, so "3.5" became 3 (key.input(<dbl>) lost the fraction).
        size_t lblAtof = enc.createLabel();
        enc.bindLabel(lblAtof);
        uint32_t rvaAtof = elf.textRva + (uint32_t)enc.code.size();
        size_t afSkipSpace = enc.createLabel();
        size_t afAfterSpace = enc.createLabel();
        size_t afPlusSign = enc.createLabel();
        size_t afDigits = enc.createLabel();
        size_t afStoreFrac = enc.createLabel();
        size_t afNextChar = enc.createLabel();
        size_t afNotDot = enc.createLabel();
        size_t afStop = enc.createLabel();
        size_t afExpSign = enc.createLabel();
        size_t afExpPlus = enc.createLabel();
        size_t afExpLoop = enc.createLabel();
        size_t afExpDone = enc.createLabel();
        size_t afScaleLoop = enc.createLabel();
        size_t afScaleMul = enc.createLabel();
        size_t afScaleDone = enc.createLabel();
        size_t afExpMul = enc.createLabel();
        size_t afExpDiv = enc.createLabel();
        size_t afNegate = enc.createLabel();
        size_t afFinish = enc.createLabel();

        enc.push(x86::RBP);
        enc.mov_rr(x86::RBP, x86::RSP);
        enc.sub_rsp(96);

        enc.mov_mr_rbp(-8, x86::RCX);   // p
        enc.mov_ri64(x86::RAX, 0);
        enc.mov_mr_rbp(-16, x86::RAX);  // negative flag
        enc.mov_mr_rbp(-24, x86::RAX);  // integer part
        enc.mov_mr_rbp(-32, x86::RAX);  // fraction value
        enc.mov_mr_rbp(-40, x86::RAX);  // fraction digit count
        enc.mov_mr_rbp(-48, x86::RAX);  // exponent value
        enc.mov_mr_rbp(-56, x86::RAX);  // saw decimal point
        enc.mov_mr_rbp(-64, x86::RAX);  // exponent is negative

        // Skip leading blanks.
        enc.bindLabel(afSkipSpace);
        enc.mov_rm_rbp(x86::RCX, -8);
        enc.movzx_rm8(x86::RAX, x86::RCX, 0);
        enc.cmp_ri32(x86::RAX, ' ');
        enc.jne(afAfterSpace);
        enc.inc_r(x86::RCX);
        enc.mov_mr_rbp(-8, x86::RCX);
        enc.jmp(afSkipSpace);

        // Optional sign.
        enc.bindLabel(afAfterSpace);
        enc.cmp_ri32(x86::RAX, '-');
        enc.jne(afPlusSign);
        enc.mov_ri64(x86::R10, 1);
        enc.mov_mr_rbp(-16, x86::R10);
        enc.inc_r(x86::RCX);
        enc.mov_mr_rbp(-8, x86::RCX);
        enc.jmp(afDigits);

        enc.bindLabel(afPlusSign);
        enc.cmp_ri32(x86::RAX, '+');
        enc.jne(afDigits);
        enc.inc_r(x86::RCX);
        enc.mov_mr_rbp(-8, x86::RCX);

        // Integer and fraction digits.
        enc.bindLabel(afDigits);
        enc.mov_rm_rbp(x86::RCX, -8);
        enc.movzx_rm8(x86::RAX, x86::RCX, 0);
        enc.cmp_ri32(x86::RAX, '0');
        enc.jl(afNotDot);
        enc.cmp_ri32(x86::RAX, '9');
        enc.jg(afNotDot);
        enc.sub_ri32(x86::RAX, '0');

        enc.mov_rm_rbp(x86::R10, -56);
        enc.test_rr(x86::R10, x86::R10);
        enc.jne(afStoreFrac);

        // integerPart = integerPart * 10 + digit
        enc.mov_rm_rbp(x86::R8, -24);
        enc.mov_ri64(x86::R9, 10);
        enc.imul_rr(x86::R8, x86::R9);
        enc.add_rr(x86::R8, x86::RAX);
        enc.mov_mr_rbp(-24, x86::R8);
        enc.jmp(afNextChar);

        // fractionValue = fractionValue * 10 + digit; fractionDigits++
        enc.bindLabel(afStoreFrac);
        enc.mov_rm_rbp(x86::R8, -32);
        enc.mov_ri64(x86::R9, 10);
        enc.imul_rr(x86::R8, x86::R9);
        enc.add_rr(x86::R8, x86::RAX);
        enc.mov_mr_rbp(-32, x86::R8);
        enc.mov_rm_rbp(x86::R8, -40);
        enc.inc_r(x86::R8);
        enc.mov_mr_rbp(-40, x86::R8);

        enc.bindLabel(afNextChar);
        enc.inc_r(x86::RCX);
        enc.mov_mr_rbp(-8, x86::RCX);
        enc.jmp(afDigits);

        // Not a digit: maybe the decimal point.
        enc.bindLabel(afNotDot);
        enc.cmp_ri32(x86::RAX, '.');
        enc.jne(afStop);
        enc.mov_rm_rbp(x86::R10, -56);
        enc.test_rr(x86::R10, x86::R10);
        enc.jne(afStop);
        enc.mov_ri64(x86::R10, 1);
        enc.mov_mr_rbp(-56, x86::R10);
        enc.inc_r(x86::RCX);
        enc.mov_mr_rbp(-8, x86::RCX);
        enc.jmp(afDigits);

        // Optional exponent.
        enc.bindLabel(afStop);
        enc.cmp_ri32(x86::RAX, 'e');
        enc.je(afExpSign);
        enc.cmp_ri32(x86::RAX, 'E');
        enc.jne(afScaleLoop);

        enc.bindLabel(afExpSign);
        enc.inc_r(x86::RCX);
        enc.mov_mr_rbp(-8, x86::RCX);
        enc.movzx_rm8(x86::RAX, x86::RCX, 0);
        enc.cmp_ri32(x86::RAX, '-');
        enc.jne(afExpPlus);
        enc.mov_ri64(x86::R10, 1);
        enc.mov_mr_rbp(-64, x86::R10);
        enc.inc_r(x86::RCX);
        enc.mov_mr_rbp(-8, x86::RCX);
        enc.jmp(afExpLoop);

        enc.bindLabel(afExpPlus);
        enc.cmp_ri32(x86::RAX, '+');
        enc.jne(afExpLoop);
        enc.inc_r(x86::RCX);
        enc.mov_mr_rbp(-8, x86::RCX);

        enc.bindLabel(afExpLoop);
        enc.mov_rm_rbp(x86::RCX, -8);
        enc.movzx_rm8(x86::RAX, x86::RCX, 0);
        enc.cmp_ri32(x86::RAX, '0');
        enc.jl(afExpDone);
        enc.cmp_ri32(x86::RAX, '9');
        enc.jg(afExpDone);
        enc.sub_ri32(x86::RAX, '0');
        enc.mov_rm_rbp(x86::R8, -48);
        enc.mov_ri64(x86::R9, 10);
        enc.imul_rr(x86::R8, x86::R9);
        enc.add_rr(x86::R8, x86::RAX);
        enc.mov_mr_rbp(-48, x86::R8);
        enc.inc_r(x86::RCX);
        enc.mov_mr_rbp(-8, x86::RCX);
        enc.jmp(afExpLoop);

        // Clamp the exponent so absurd input cannot spin the scaling loop.
        enc.bindLabel(afExpDone);
        enc.mov_rm_rbp(x86::R8, -48);
        enc.mov_ri64(x86::R9, 400);
        enc.cmp_rr(x86::R8, x86::R9);
        enc.jle(afScaleLoop);
        enc.mov_mr_rbp(-48, x86::R9);

        // scale = 10^fractionDigits
        enc.bindLabel(afScaleLoop);
        enc.mov_rm_rbp(x86::R11, -40);
        enc.mov_ri64(x86::R9, 1);
        enc.test_rr(x86::R11, x86::R11);
        enc.je(afScaleDone);
        enc.bindLabel(afScaleMul);
        enc.mov_ri64(x86::RAX, 10);
        enc.imul_rr(x86::R9, x86::RAX);
        enc.sub_ri32(x86::R11, 1);
        enc.test_rr(x86::R11, x86::R11);
        enc.jne(afScaleMul);

        // (integerPart * scale + fractionValue) / scale
        enc.bindLabel(afScaleDone);
        enc.mov_rm_rbp(x86::R8, -24);
        enc.imul_rr(x86::R8, x86::R9);
        enc.mov_rm_rbp(x86::R10, -32);
        enc.add_rr(x86::R8, x86::R10);
        enc.cvtsi2sd(x86::XMM0, x86::R8);
        enc.cvtsi2sd(x86::XMM1, x86::R9);
        enc.divsd_rr(x86::XMM0, x86::XMM1);

        // Apply the exponent.
        enc.mov_rm_rbp(x86::R11, -48);
        enc.test_rr(x86::R11, x86::R11);
        enc.je(afNegate);
        enc.movsd_rip_rdata(x86::XMM1, offDoubleTen);
        enc.mov_rm_rbp(x86::R10, -64);
        enc.test_rr(x86::R10, x86::R10);
        enc.jne(afExpDiv);

        enc.bindLabel(afExpMul);
        enc.mulsd_rr(x86::XMM0, x86::XMM1);
        enc.sub_ri32(x86::R11, 1);
        enc.test_rr(x86::R11, x86::R11);
        enc.jne(afExpMul);
        enc.jmp(afNegate);

        enc.bindLabel(afExpDiv);
        enc.divsd_rr(x86::XMM0, x86::XMM1);
        enc.sub_ri32(x86::R11, 1);
        enc.test_rr(x86::R11, x86::R11);
        enc.jne(afExpDiv);

        // Negative sign.
        enc.bindLabel(afNegate);
        enc.mov_rm_rbp(x86::R10, -16);
        enc.test_rr(x86::R10, x86::R10);
        enc.je(afFinish);
        enc.subsd_rr(x86::XMM1, x86::XMM1); // XMM1 = 0.0
        enc.subsd_rr(x86::XMM1, x86::XMM0);
        enc.movsd_rr(x86::XMM0, x86::XMM1);

        enc.bindLabel(afFinish);
        enc.add_rsp(96);
        enc.pop(x86::RBP);
        enc.ret();
        elf.registerFuncPointer("atof", rvaAtof);

        // --- L. _thava_linux_printf ---
        // Format in RCX, value in RDX
        size_t lblPrintf = enc.createLabel();
        enc.bindLabel(lblPrintf);
        uint32_t rvaPrintf = elf.textRva + (uint32_t)enc.code.size();
        size_t pFmtStr = enc.createLabel();
        size_t pFmtInt = enc.createLabel();
        size_t pFmtFloat = enc.createLabel();
        size_t pFmtNL  = enc.createLabel();
        size_t pDone   = enc.createLabel();

        enc.push(x86::RBP);
        enc.mov_rr(x86::RBP, x86::RSP);
        enc.sub_rsp(512);

        enc.mov_mr_rbp(-8, x86::RCX);  // fmt
        enc.mov_mr_rbp(-16, x86::RDX); // val

        // Inspect fmt[0]
        enc.movzx_rm8(x86::RAX, x86::RCX, 0);
        enc.cmp_ri32(x86::RAX, '%');
        size_t pPercent = enc.createLabel();
        enc.je(pPercent);

        // Check if newline "\n"
        enc.cmp_ri32(x86::RAX, '\n');
        enc.je(pFmtNL);

        // String without % (prompt or message): write(1, rcx, strlen(rcx))
        enc.call_label(lblStrlen); // length in RAX
        enc.mov_rr(x86::RDX, x86::RAX);
        enc.mov_rm_rbp(x86::RSI, -8);
        enc.mov_ri64(x86::RDI, 1);
        enc.mov_ri64(x86::RAX, 1); // sys_write = 1
        enc.syscall();
        enc.jmp(pDone);

        enc.bindLabel(pPercent);
        // fmt[1]
        enc.movzx_rm8(x86::RAX, x86::RCX, 1);
        enc.cmp_ri32(x86::RAX, 's');
        enc.je(pFmtStr);
        enc.cmp_ri32(x86::RAX, 'l');
        enc.je(pFmtInt);
        enc.cmp_ri32(x86::RAX, 'g');
        enc.je(pFmtFloat);
        enc.cmp_ri32(x86::RAX, 'f');
        enc.je(pFmtFloat);
        enc.jmp(pDone);

        // Print string in RDX
        enc.bindLabel(pFmtStr);
        enc.mov_rm_rbp(x86::RCX, -16); // str ptr
        enc.call_label(lblStrlen);
        enc.mov_rr(x86::RDX, x86::RAX); // len
        enc.mov_rm_rbp(x86::RSI, -16);  // buf
        enc.mov_ri64(x86::RDI, 1);
        enc.mov_ri64(x86::RAX, 1);      // sys_write = 1
        enc.syscall();

        // If fmt[2] == '\n', print newline
        enc.mov_rm_rbp(x86::RCX, -8);
        enc.movzx_rm8(x86::RAX, x86::RCX, 2);
        enc.cmp_ri32(x86::RAX, '\n');
        size_t pSkipNLStr = enc.createLabel();
        enc.jne(pSkipNLStr);
        enc.lea_rip_rdata(x86::RSI, offStrNL);
        enc.mov_ri64(x86::RDX, 1);
        enc.mov_ri64(x86::RDI, 1);
        enc.mov_ri64(x86::RAX, 1);
        enc.syscall();
        enc.bindLabel(pSkipNLStr);
        enc.jmp(pDone);

        // Print double (raw IEEE-754 bits in RDX) in Thava's standard format:
        // 6 significant digits, fixed notation, trailing zeros trimmed.
        // Matches the x86-64 backend (printf "%g") for |v| < 1e6.
        enc.bindLabel(pFmtFloat);
        {
            size_t fAbs = enc.createLabel();
            size_t fZero = enc.createLabel();
            size_t fCount = enc.createLabel();
            size_t fCountLoop = enc.createLabel();
            size_t dOk = enc.createLabel();
            size_t powLoop = enc.createLabel();
            size_t powDone = enc.createLabel();
            size_t fNoDec = enc.createLabel();
            size_t fScaled = enc.createLabel();
            size_t fIntLoop = enc.createLabel();
            size_t fIntPop = enc.createLabel();
            size_t fIntDone = enc.createLabel();
            size_t fNoFrac = enc.createLabel();
            size_t fFracLoop = enc.createLabel();
            size_t fFracPop = enc.createLabel();
            size_t fTrim = enc.createLabel();
            size_t fTrimDone = enc.createLabel();
            size_t fSkipNL = enc.createLabel();

            // slot map: -24 |v| bits, -32 scaled6, -40 d, -48 pow10, -56 scaled, -64 intPart, -72 frac
            enc.mov_rm_rbp(x86::RAX, -16); // raw bits
            enc.mov_mr_rbp(-24, x86::RAX);
            enc.lea_rm_rbp(x86::RDI, -400); // output buffer cursor

            // sign
            enc.test_rr(x86::RAX, x86::RAX);
            enc.jns(fAbs);
            enc.mov_ri64(x86::R10, '-');
            enc.mov_mr8(x86::RDI, 0, x86::R10);
            enc.inc_r(x86::RDI);
            enc.bindLabel(fAbs);

            // absolute value
            enc.mov_rm_rbp(x86::RAX, -24);
            enc.mov_ri64(x86::R10, 0x7FFFFFFFFFFFFFFFLL);
            enc.and_rr(x86::RAX, x86::R10);
            enc.mov_mr_rbp(-24, x86::RAX);
            enc.movsd_rm_rbp(x86::XMM0, -24);

            // scaled6 = (int64)(|v| * 1e6 + 0.5)
            enc.lea_rip_rdata(x86::R10, offDoubleMillion);
            enc.movsd_rm(x86::XMM1, x86::R10, 0);
            enc.mulsd_rr(x86::XMM0, x86::XMM1);
            enc.lea_rip_rdata(x86::R10, offDoubleHalf);
            enc.movsd_rm(x86::XMM1, x86::R10, 0);
            enc.addsd_rr(x86::XMM0, x86::XMM1);
            enc.cvttsd2si(x86::RAX, x86::XMM0);
            enc.mov_mr_rbp(-32, x86::RAX);

            enc.test_rr(x86::RAX, x86::RAX);
            enc.je(fZero);

            // digits(scaled6) -> R9, then d = 12 - digits (6 significant digits)
            enc.mov_rr(x86::R11, x86::RAX);
            enc.mov_ri64(x86::R9, 1);
            enc.mov_ri64(x86::RCX, 10);
            enc.bindLabel(fCountLoop);
            enc.mov_ri64(x86::R10, 10);
            enc.cmp_rr(x86::R11, x86::R10);
            enc.jl(fCount);
            enc.mov_rr(x86::RAX, x86::R11);
            enc.xor_r32(x86::RDX, x86::RDX);
            enc.div_r(x86::RCX);
            enc.mov_rr(x86::R11, x86::RAX);
            enc.inc_r(x86::R9);
            enc.jmp(fCountLoop);
            enc.bindLabel(fCount);

            enc.mov_ri64(x86::R10, 12);
            enc.sub_rr(x86::R10, x86::R9); // d
            enc.xor_r32(x86::R11, x86::R11);
            enc.cmp_rr(x86::R10, x86::R11);
            enc.jge(dOk);
            enc.mov_ri64(x86::R10, 0);
            enc.bindLabel(dOk);
            enc.mov_mr_rbp(-40, x86::R10);

            // pow10 = 10^d
            enc.mov_ri64(x86::R11, 1);
            enc.mov_ri64(x86::R9, 10);
            enc.mov_rr(x86::RCX, x86::R10);
            enc.test_rr(x86::RCX, x86::RCX);
            enc.je(powDone);
            enc.bindLabel(powLoop);
            enc.imul_rr(x86::R11, x86::R9);
            enc.dec_r(x86::RCX);
            enc.jne(powLoop);
            enc.bindLabel(powDone);
            enc.mov_mr_rbp(-48, x86::R11);

            // scaled = d == 0 ? scaled6 / 1e6 : (int64)(|v| * pow10 + 0.5)
            enc.mov_rm_rbp(x86::RCX, -40);
            enc.test_rr(x86::RCX, x86::RCX);
            enc.jne(fNoDec);
            enc.mov_rm_rbp(x86::RAX, -32);
            enc.mov_ri64(x86::RCX, 1000000);
            enc.xor_r32(x86::RDX, x86::RDX);
            enc.div_r(x86::RCX);
            enc.jmp(fScaled);
            enc.bindLabel(fNoDec);
            enc.movsd_rm_rbp(x86::XMM0, -24);
            enc.mov_rm_rbp(x86::RAX, -48);
            enc.cvtsi2sd(x86::XMM1, x86::RAX);
            enc.mulsd_rr(x86::XMM0, x86::XMM1);
            enc.lea_rip_rdata(x86::R10, offDoubleHalf);
            enc.movsd_rm(x86::XMM1, x86::R10, 0);
            enc.addsd_rr(x86::XMM0, x86::XMM1);
            enc.cvttsd2si(x86::RAX, x86::XMM0);
            enc.bindLabel(fScaled);
            enc.mov_mr_rbp(-56, x86::RAX);

            // intPart = scaled / pow10, frac = scaled % pow10
            enc.mov_rm_rbp(x86::RCX, -48);
            enc.xor_r32(x86::RDX, x86::RDX);
            enc.div_r(x86::RCX);
            enc.mov_mr_rbp(-64, x86::RAX);
            enc.mov_mr_rbp(-72, x86::RDX);

            // integer digits
            enc.mov_rr(x86::R11, x86::RSP);
            enc.xor_r32(x86::R9, x86::R9);
            enc.mov_ri64(x86::RCX, 10);
            enc.mov_rm_rbp(x86::RAX, -64);
            enc.bindLabel(fIntLoop);
            enc.xor_r32(x86::RDX, x86::RDX);
            enc.div_r(x86::RCX);
            enc.add_ri32(x86::RDX, '0');
            enc.push(x86::RDX);
            enc.inc_r(x86::R9);
            enc.test_rr(x86::RAX, x86::RAX);
            enc.jne(fIntLoop);

            enc.bindLabel(fIntPop);
            enc.test_rr(x86::R9, x86::R9);
            enc.je(fIntDone);
            enc.pop(x86::RDX);
            enc.mov_mr8(x86::RDI, 0, x86::RDX);
            enc.inc_r(x86::RDI);
            enc.dec_r(x86::R9);
            enc.jmp(fIntPop);
            enc.bindLabel(fIntDone);
            enc.mov_rr(x86::RSP, x86::R11);

            enc.mov_rm_rbp(x86::RAX, -72);
            enc.test_rr(x86::RAX, x86::RAX);
            enc.je(fNoFrac);

            // decimal point
            enc.mov_ri64(x86::R10, '.');
            enc.mov_mr8(x86::RDI, 0, x86::R10);
            enc.inc_r(x86::RDI);

            // exactly d fraction digits (leading zeros included)
            enc.mov_rm_rbp(x86::R9, -40);
            enc.mov_ri64(x86::RCX, 10);
            enc.bindLabel(fFracLoop);
            enc.xor_r32(x86::RDX, x86::RDX);
            enc.div_r(x86::RCX);
            enc.add_ri32(x86::RDX, '0');
            enc.push(x86::RDX);
            enc.dec_r(x86::R9);
            enc.jne(fFracLoop);

            enc.mov_rm_rbp(x86::R9, -40);
            enc.bindLabel(fFracPop);
            enc.pop(x86::RDX);
            enc.mov_mr8(x86::RDI, 0, x86::RDX);
            enc.inc_r(x86::RDI);
            enc.dec_r(x86::R9);
            enc.jne(fFracPop);

            // trim trailing zeros
            enc.bindLabel(fTrim);
            enc.dec_r(x86::RDI);
            enc.movzx_rm8(x86::RAX, x86::RDI, 0);
            enc.cmp_ri32(x86::RAX, '0');
            enc.jne(fTrimDone);
            enc.jmp(fTrim);
            enc.bindLabel(fTrimDone);
            enc.inc_r(x86::RDI);

            enc.bindLabel(fNoFrac);
            size_t fWriteOut = enc.createLabel();
            enc.jmp(fWriteOut);

            enc.bindLabel(fZero);
            enc.mov_ri64(x86::R10, '0');
            enc.mov_mr8(x86::RDI, 0, x86::R10);
            enc.inc_r(x86::RDI);

            enc.bindLabel(fWriteOut);
            enc.mov_ri64(x86::RDX, 0);
            enc.mov_mr8(x86::RDI, 0, x86::RDX);
            enc.lea_rm_rbp(x86::RSI, -400);
            enc.mov_rr(x86::RDX, x86::RDI);
            enc.sub_rr(x86::RDX, x86::RSI);
            enc.mov_ri64(x86::RDI, 1);
            enc.mov_ri64(x86::RAX, 1);
            enc.syscall();

            // newline if the format string ends with '\n'
            enc.mov_rm_rbp(x86::RCX, -8);
            enc.call_label(lblStrlen);
            enc.mov_rm_rbp(x86::RCX, -8);
            enc.dec_r(x86::RAX);
            enc.add_rr(x86::RCX, x86::RAX);
            enc.movzx_rm8(x86::RAX, x86::RCX, 0);
            enc.cmp_ri32(x86::RAX, '\n');
            enc.jne(fSkipNL);
            enc.lea_rip_rdata(x86::RSI, offStrNL);
            enc.mov_ri64(x86::RDX, 1);
            enc.mov_ri64(x86::RDI, 1);
            enc.mov_ri64(x86::RAX, 1);
            enc.syscall();
            enc.bindLabel(fSkipNL);
            enc.jmp(pDone);
        }

        // Print integer in RDX
        enc.bindLabel(pFmtInt);
        enc.mov_rm_rbp(x86::RAX, -16); // val
        // Convert integer to decimal string in stack buffer (-80)
        enc.lea_rm_rbp(x86::RDI, -80);
        enc.xor_r32(x86::R8, x86::R8); // sign
        enc.test_rr(x86::RAX, x86::RAX);
        size_t pNotNeg = enc.createLabel();
        enc.jns(pNotNeg);
        enc.mov_mr8(x86::RDI, 0, x86::RAX); // '-'
        enc.mov_ri64(x86::R10, '-');
        enc.mov_mr8(x86::RDI, 0, x86::R10);
        enc.inc_r(x86::RDI);
        enc.neg_r(x86::RAX);
        enc.bindLabel(pNotNeg);

        // Generate digits backward on stack
        enc.mov_rr(x86::R11, x86::RSP); // save stack
        size_t pDigitLoop = enc.createLabel();
        size_t pPopDigits = enc.createLabel();
        enc.xor_r32(x86::R9, x86::R9); // count
        enc.mov_ri64(x86::RCX, 10);

        enc.bindLabel(pDigitLoop);
        enc.xor_r32(x86::RDX, x86::RDX);
        enc.div_r(x86::RCX); // RAX = RAX/10, RDX = rem
        enc.add_ri32(x86::RDX, '0');
        enc.push(x86::RDX);
        enc.inc_r(x86::R9);
        enc.test_rr(x86::RAX, x86::RAX);
        enc.jne(pDigitLoop);

        enc.bindLabel(pPopDigits);
        enc.test_rr(x86::R9, x86::R9);
        size_t pDoneDigits = enc.createLabel();
        enc.je(pDoneDigits);
        enc.pop(x86::RDX);
        enc.mov_mr8(x86::RDI, 0, x86::RDX);
        enc.inc_r(x86::RDI);
        enc.dec_r(x86::R9);
        enc.jmp(pPopDigits);

        enc.bindLabel(pDoneDigits);
        enc.mov_ri64(x86::RDX, 0);
        enc.mov_mr8(x86::RDI, 0, x86::RDX); // null terminate
        enc.lea_rm_rbp(x86::RSI, -80);
        enc.mov_rr(x86::RDX, x86::RDI);
        enc.sub_rr(x86::RDX, x86::RSI); // len in RDX
        enc.mov_ri64(x86::RDI, 1);
        enc.mov_ri64(x86::RAX, 1);      // sys_write = 1
        enc.syscall();

        // Check if newline requested in fmt
        enc.mov_rm_rbp(x86::RCX, -8);
        enc.call_label(lblStrlen);
        enc.mov_rm_rbp(x86::RCX, -8);
        enc.dec_r(x86::RAX);
        enc.add_rr(x86::RCX, x86::RAX);
        enc.movzx_rm8(x86::RAX, x86::RCX, 0);
        enc.cmp_ri32(x86::RAX, '\n');
        size_t pSkipNLInt = enc.createLabel();
        enc.jne(pSkipNLInt);
        enc.lea_rip_rdata(x86::RSI, offStrNL);
        enc.mov_ri64(x86::RDX, 1);
        enc.mov_ri64(x86::RDI, 1);
        enc.mov_ri64(x86::RAX, 1);
        enc.syscall();
        enc.bindLabel(pSkipNLInt);
        enc.jmp(pDone);

        // Print "\n"
        enc.bindLabel(pFmtNL);
        enc.lea_rip_rdata(x86::RSI, offStrNL);
        enc.mov_ri64(x86::RDX, 1);
        enc.mov_ri64(x86::RDI, 1);
        enc.mov_ri64(x86::RAX, 1);
        enc.syscall();

        enc.bindLabel(pDone);
        enc.add_rsp(512);
        enc.pop(x86::RBP);
        enc.ret();
        elf.registerFuncPointer("printf", rvaPrintf);
        elf.registerFuncPointer("puts", rvaPrintf);

        // --- M. _thava_linux_scanf ---
        // Reads exactly ONE line from stdin into readBuf, then either copies the
        // line (line formats such as " %1023[^\n]") or stores its integer value
        // (number formats such as "%lld").
        //
        // One byte per sys_read: a single read(1023) swallowed every buffered
        // line at once (piped input lost all reads after the first).
        // Format sniffing skips leading blanks and looks at the character after
        // '%': 'l'/'d'/'i' mean integer, anything else (a width digit or '['
        // from " %1023[^\n]", or 's') means "hand me the raw line". The previous
        // code peeked at byte 1, which for the leading-space format is '%' and
        // therefore always parsed as an integer, so every key.input returned
        // 0/false/"" on the ELF backend.
        size_t lblScanf = enc.createLabel();
        enc.bindLabel(lblScanf);
        uint32_t rvaScanf = elf.textRva + (uint32_t)enc.code.size();
        size_t sScanInt = enc.createLabel();
        size_t sScanStr = enc.createLabel();
        size_t sDoneScan = enc.createLabel();
        size_t sReadLoop = enc.createLabel();
        size_t sReadDone = enc.createLabel();
        size_t sSkipTrim = enc.createLabel();
        size_t sFmtSkipBlank = enc.createLabel();
        size_t sFmtAfterPct = enc.createLabel();

        enc.push(x86::RBP);
        enc.mov_rr(x86::RBP, x86::RSP);
        enc.sub_rsp(128);

        enc.mov_mr_rbp(-8, x86::RCX);  // fmt
        enc.mov_mr_rbp(-16, x86::RDX); // out ptr

        // i = 0
        enc.mov_ri64(x86::R10, 0);
        enc.mov_mr_rbp(-24, x86::R10);

        // Read bytes until '\n', EOF or the 1023-byte limit.
        enc.bindLabel(sReadLoop);
        enc.mov_rm_rbp(x86::R10, -24);
        enc.mov_ri64(x86::R11, 1023);
        enc.cmp_rr(x86::R10, x86::R11);
        enc.jge(sReadDone);

        enc.xor_r32(x86::RDI, x86::RDI);            // fd 0 = stdin
        enc.lea_rip_rdata(x86::RSI, offReadBuf);
        enc.add_rr(x86::RSI, x86::R10);             // &readBuf[i]
        enc.mov_ri64(x86::RDX, 1);
        enc.xor_r32(x86::RAX, x86::RAX);            // sys_read = 0
        enc.syscall();
        enc.cmp_ri32(x86::RAX, 0);
        enc.jle(sReadDone);                          // EOF or error

        enc.mov_rm_rbp(x86::R10, -24);
        enc.lea_rip_rdata(x86::RCX, offReadBuf);
        enc.add_rr(x86::RCX, x86::R10);
        enc.movzx_rm8(x86::RAX, x86::RCX, 0);
        enc.cmp_ri32(x86::RAX, '\n');
        enc.je(sReadDone);                           // newline is not stored

        enc.inc_r(x86::R10);
        enc.mov_mr_rbp(-24, x86::R10);
        enc.jmp(sReadLoop);

        // Trim the '\r' of CRLF input.
        enc.bindLabel(sReadDone);
        enc.mov_rm_rbp(x86::R10, -24);
        enc.test_rr(x86::R10, x86::R10);
        enc.je(sSkipTrim);
        enc.mov_rm_rbp(x86::R11, -24);
        enc.sub_ri32(x86::R11, 1);
        enc.lea_rip_rdata(x86::RCX, offReadBuf);
        enc.add_rr(x86::RCX, x86::R11);
        enc.movzx_rm8(x86::RAX, x86::RCX, 0);
        enc.cmp_ri32(x86::RAX, '\r');
        enc.jne(sSkipTrim);
        enc.mov_rm_rbp(x86::R10, -24);
        enc.sub_ri32(x86::R10, 1);
        enc.mov_mr_rbp(-24, x86::R10);

        // NUL-terminate at i.
        enc.bindLabel(sSkipTrim);
        enc.mov_rm_rbp(x86::R10, -24);
        enc.lea_rip_rdata(x86::RCX, offReadBuf);
        enc.add_rr(x86::RCX, x86::R10);
        enc.xor_r32(x86::RAX, x86::RAX);
        enc.mov_mr8(x86::RCX, 0, x86::RAX);

        // Classify the format: skip blanks, then inspect the char after '%'.
        enc.mov_rm_rbp(x86::RCX, -8); // fmt
        enc.bindLabel(sFmtSkipBlank);
        enc.movzx_rm8(x86::RAX, x86::RCX, 0);
        enc.cmp_ri32(x86::RAX, ' ');
        enc.jne(sFmtAfterPct);
        enc.inc_r(x86::RCX);
        enc.jmp(sFmtSkipBlank);

        enc.bindLabel(sFmtAfterPct);
        enc.cmp_ri32(x86::RAX, '%');
        enc.jne(sScanInt);
        enc.inc_r(x86::RCX);
        enc.movzx_rm8(x86::RAX, x86::RCX, 0);
        enc.cmp_ri32(x86::RAX, 'l');
        enc.je(sScanInt);
        enc.cmp_ri32(x86::RAX, 'd');
        enc.je(sScanInt);
        enc.cmp_ri32(x86::RAX, 'i');
        enc.je(sScanInt);
        enc.jmp(sScanStr);

        enc.bindLabel(sScanInt);
        enc.lea_rip_rdata(x86::RCX, offReadBuf);
        enc.call_label(lblAtoi);
        enc.mov_rm_rbp(x86::RDX, -16);
        enc.mov_mr(x86::RDX, 0, x86::RAX);
        enc.jmp(sDoneScan);

        enc.bindLabel(sScanStr);
        enc.mov_rm_rbp(x86::RCX, -16); // dst
        enc.lea_rip_rdata(x86::RDX, offReadBuf); // src
        enc.call_label(lblStrcpy);
        enc.jmp(sDoneScan);

        enc.bindLabel(sDoneScan);
        enc.add_rsp(128);
        enc.pop(x86::RBP);
        enc.ret();
        elf.registerFuncPointer("scanf", rvaScanf);

        // --- N. _thava_linux_sprintf (buf in RCX, fmt in RDX, val in R8) ---
        size_t lblSprintf = enc.createLabel();
        enc.bindLabel(lblSprintf);
        uint32_t rvaSprintf = elf.textRva + (uint32_t)enc.code.size();

        enc.push(x86::RBP);
        enc.mov_rr(x86::RBP, x86::RSP);
        enc.sub_rsp(64);

        // Convert val (in R8) to string at RCX
        enc.mov_rr(x86::RAX, x86::R8);
        enc.mov_rr(x86::RDI, x86::RCX);

        enc.test_rr(x86::RAX, x86::RAX);
        size_t spNotNeg = enc.createLabel();
        enc.jns(spNotNeg);
        enc.mov_ri64(x86::R10, '-');
        enc.mov_mr8(x86::RDI, 0, x86::R10);
        enc.inc_r(x86::RDI);
        enc.neg_r(x86::RAX);
        enc.bindLabel(spNotNeg);

        enc.xor_r32(x86::R9, x86::R9); // count
        enc.mov_ri64(x86::RCX, 10);
        size_t spDigitLoop = enc.createLabel();
        size_t spPopDigits = enc.createLabel();

        enc.bindLabel(spDigitLoop);
        enc.xor_r32(x86::RDX, x86::RDX);
        enc.div_r(x86::RCX);
        enc.add_ri32(x86::RDX, '0');
        enc.push(x86::RDX);
        enc.inc_r(x86::R9);
        enc.test_rr(x86::RAX, x86::RAX);
        enc.jne(spDigitLoop);

        enc.bindLabel(spPopDigits);
        enc.test_rr(x86::R9, x86::R9);
        size_t spDoneDigits = enc.createLabel();
        enc.je(spDoneDigits);
        enc.pop(x86::RDX);
        enc.mov_mr8(x86::RDI, 0, x86::RDX);
        enc.inc_r(x86::RDI);
        enc.dec_r(x86::R9);
        enc.jmp(spPopDigits);

        enc.bindLabel(spDoneDigits);
        enc.xor_r32(x86::RAX, x86::RAX);
        enc.mov_mr8(x86::RDI, 0, x86::RAX);

        enc.add_rsp(64);
        enc.pop(x86::RBP);
        enc.ret();
        elf.registerFuncPointer("sprintf", rvaSprintf);

        // --- N2. _thava_linux_dtoa (buf in RCX, raw IEEE-754 bits in R8) ---
        // "%g"-style formatting (6 significant digits, fixed notation, trailing zeros
        // trimmed) for this zero-libc runtime. _thava_linux_sprintf only knows
        // integers, which made <str>(double) print the raw bit pattern.
        size_t lblDtoa = enc.createLabel();
        enc.bindLabel(lblDtoa);
        uint32_t rvaDtoa = elf.textRva + (uint32_t)enc.code.size();

        enc.push(x86::RBP);
        enc.mov_rr(x86::RBP, x86::RSP);
        enc.sub_rsp(96);
        enc.mov_rr(x86::RDI, x86::RCX); // destination cursor
        enc.mov_rr(x86::RSI, x86::R8);  // raw bits

        // Optional minus sign, then continue with the absolute value.
        size_t dtAbs = enc.createLabel();
        enc.test_rr(x86::RSI, x86::RSI);
        enc.jns(dtAbs);
        enc.mov_ri64(x86::R10, '-');
        enc.mov_mr8(x86::RDI, 0, x86::R10);
        enc.inc_r(x86::RDI);
        enc.bindLabel(dtAbs);
        enc.mov_ri64(x86::R10, 0x7FFFFFFFFFFFFFFFLL);
        enc.mov_rr(x86::RAX, x86::RSI);
        enc.and_rr(x86::RAX, x86::R10);
        enc.mov_mr_rbp(-8, x86::RAX);
        enc.movsd_rm_rbp(x86::XMM0, -8); // XMM0 = |v|

        // Zero (and -0.0) is just "0".
        size_t dtZero = enc.createLabel();
        size_t dtFinish = enc.createLabel();
        enc.xor_r32(x86::RAX, x86::RAX);
        enc.mov_mr_rbp(-16, x86::RAX);
        enc.movsd_rm_rbp(x86::XMM1, -16);
        enc.ucomisd_rr(x86::XMM0, x86::XMM1);
        enc.je(dtZero);

        // Huge magnitudes are reduced in groups of 1e18 so that the integer
        // conversion below cannot saturate int64.
        enc.xor_r32(x86::R11, x86::R11);
        enc.mov_ri64(x86::R10, 0x43ABC16D674EC800LL); // 1e18
        enc.mov_mr_rbp(-64, x86::R10);
        enc.movsd_rm_rbp(x86::XMM3, -64);
        enc.ucomisd_rr(x86::XMM0, x86::XMM3);
        size_t dtHugeDone = enc.createLabel();
        size_t dtHuge = enc.createLabel();
        enc.jcc(x86::Cond::B, dtHugeDone);
        enc.bindLabel(dtHuge);
        enc.divsd_rr(x86::XMM0, x86::XMM3);
        enc.inc_r(x86::R11);
        enc.ucomisd_rr(x86::XMM0, x86::XMM3);
        enc.jcc(x86::Cond::AE, dtHuge);
        enc.bindLabel(dtHugeDone);
        enc.mov_mr_rbp(-48, x86::R11); // groups of 1e18
        // The reduced value is the integer part; the scaled-path store below is
        // skipped on this route, so write it now.
        enc.cvttsd2si(x86::RAX, x86::XMM0);
        enc.mov_mr_rbp(-32, x86::RAX);

        // Six decimal places with trailing zeros trimmed: the same rule as the
        // other backends. Integer magnitudes get no fraction at all.
        size_t dtZeroFrac = enc.createLabel();
        size_t dtNStore = enc.createLabel();
        enc.test_rr(x86::R11, x86::R11);
        enc.jne(dtZeroFrac);
        enc.mov_ri64(x86::R10, 0x430C6BF526340000LL); // 1e15
        enc.mov_mr_rbp(-64, x86::R10);
        enc.movsd_rm_rbp(x86::XMM3, -64);
        enc.ucomisd_rr(x86::XMM0, x86::XMM3);
        size_t dtN6 = enc.createLabel();
        enc.jcc(x86::Cond::B, dtN6);
        enc.xor_r32(x86::R9, x86::R9);
        enc.jmp(dtNStore);
        enc.bindLabel(dtN6);
        enc.mov_ri64(x86::R9, 6);
        enc.jmp(dtNStore);
        enc.bindLabel(dtZeroFrac);
        enc.xor_r32(x86::R9, x86::R9);
        enc.bindLabel(dtNStore);
        enc.mov_mr_rbp(-24, x86::R9);
        // Integer part (the fraction width N is already fixed: 6 or 0).
        enc.cvttsd2si(x86::RAX, x86::XMM0);
        enc.mov_mr_rbp(-32, x86::RAX);
        size_t dtNoFrac = enc.createLabel();
        enc.mov_rm_rbp(x86::R9, -24);
        enc.test_rr(x86::R9, x86::R9);
        enc.je(dtNoFrac);

        // XMM1 = 10^N
        enc.mov_ri64(x86::R10, 0x3FF0000000000000LL); // 1.0
        enc.mov_mr_rbp(-64, x86::R10);
        enc.movsd_rm_rbp(x86::XMM1, -64);
        enc.mov_rm_rbp(x86::R11, -24);
        size_t dtPowD = enc.createLabel();
        enc.bindLabel(dtPowD);
        enc.mov_ri64(x86::R10, 0x4024000000000000LL); // 10.0
        enc.mov_mr_rbp(-64, x86::R10);
        enc.movsd_rm_rbp(x86::XMM2, -64);
        enc.mulsd_rr(x86::XMM1, x86::XMM2);
        enc.dec_r(x86::R11);
        enc.jne(dtPowD);

        // scaled = trunc(|v| * 10^N + 0.5)
        enc.mulsd_rr(x86::XMM0, x86::XMM1);
        enc.mov_ri64(x86::R10, 0x3FE0000000000000LL); // 0.5
        enc.mov_mr_rbp(-64, x86::R10);
        enc.movsd_rm_rbp(x86::XMM2, -64);
        enc.addsd_rr(x86::XMM0, x86::XMM2);
        enc.cvttsd2si(x86::RAX, x86::XMM0);

        // pow10 = 10^N as an integer, then split scaled into int part / fraction.
        enc.mov_ri64(x86::R10, 1);
        enc.mov_ri64(x86::R11, 10);
        enc.mov_rm_rbp(x86::R9, -24);
        size_t dtPowI = enc.createLabel();
        enc.bindLabel(dtPowI);
        enc.imul_rr(x86::R10, x86::R11);
        enc.dec_r(x86::R9);
        enc.jne(dtPowI);
        enc.xor_r32(x86::RDX, x86::RDX);
        enc.div_r(x86::R10);
        enc.mov_mr_rbp(-40, x86::RDX); // fraction
        enc.mov_mr_rbp(-32, x86::RAX); // integer part (rounded)
        enc.bindLabel(dtNoFrac);

        // Emit the integer part.
        enc.mov_rm_rbp(x86::RAX, -32);
        enc.xor_r32(x86::R9, x86::R9);
        enc.mov_ri64(x86::R10, 10);
        size_t dtIntLoop = enc.createLabel();
        size_t dtIntPop = enc.createLabel();
        size_t dtIntDone = enc.createLabel();
        enc.bindLabel(dtIntLoop);
        enc.xor_r32(x86::RDX, x86::RDX);
        enc.div_r(x86::R10);
        enc.add_ri32(x86::RDX, '0');
        enc.push(x86::RDX);
        enc.inc_r(x86::R9);
        enc.test_rr(x86::RAX, x86::RAX);
        enc.jne(dtIntLoop);
        enc.bindLabel(dtIntPop);
        enc.test_rr(x86::R9, x86::R9);
        enc.je(dtIntDone);
        enc.pop(x86::RDX);
        enc.mov_mr8(x86::RDI, 0, x86::RDX);
        enc.inc_r(x86::RDI);
        enc.dec_r(x86::R9);
        enc.jmp(dtIntPop);
        enc.bindLabel(dtIntDone);

        // Trailing zeros for the 1e18 groups of a huge magnitude.
        enc.mov_rm_rbp(x86::R9, -48);
        size_t dtNoZeros = enc.createLabel();
        enc.test_rr(x86::R9, x86::R9);
        enc.je(dtNoZeros);
        enc.mov_ri64(x86::R10, 18);
        enc.imul_rr(x86::R9, x86::R10);
        size_t dtZeroLoop = enc.createLabel();
        enc.bindLabel(dtZeroLoop);
        enc.mov_ri64(x86::R10, '0');
        enc.mov_mr8(x86::RDI, 0, x86::R10);
        enc.inc_r(x86::RDI);
        enc.dec_r(x86::R9);
        enc.jne(dtZeroLoop);
        enc.bindLabel(dtNoZeros);

        // Fraction: '.', then exactly N zero-padded digits, then trim.
        enc.mov_rm_rbp(x86::R9, -24);
        enc.test_rr(x86::R9, x86::R9);
        enc.je(dtFinish);
        enc.mov_ri64(x86::R10, '.');
        enc.mov_mr8(x86::RDI, 0, x86::R10);
        enc.inc_r(x86::RDI);

        enc.mov_rm_rbp(x86::RSI, -40); // fraction
        enc.mov_rm_rbp(x86::R9, -24);  // N
        enc.mov_ri64(x86::R10, 10);
        size_t dtFracPush = enc.createLabel();
        enc.bindLabel(dtFracPush);
        enc.mov_rr(x86::RAX, x86::RSI);
        enc.xor_r32(x86::RDX, x86::RDX);
        enc.div_r(x86::R10);
        enc.mov_rr(x86::RSI, x86::RAX);
        enc.add_ri32(x86::RDX, '0');
        enc.push(x86::RDX);
        enc.dec_r(x86::R9);
        enc.jne(dtFracPush);

        enc.mov_rm_rbp(x86::R9, -24);
        size_t dtFracPop = enc.createLabel();
        enc.bindLabel(dtFracPop);
        enc.pop(x86::RDX);
        enc.mov_mr8(x86::RDI, 0, x86::RDX);
        enc.inc_r(x86::RDI);
        enc.dec_r(x86::R9);
        enc.jne(dtFracPop);

        size_t dtTrim = enc.createLabel();
        size_t dtTrimDot = enc.createLabel();
        enc.bindLabel(dtTrim);
        enc.movzx_rm8(x86::RAX, x86::RDI, -1);
        enc.cmp_ri32(x86::RAX, '0');
        enc.jne(dtTrimDot);
        enc.dec_r(x86::RDI);
        enc.jmp(dtTrim);
        enc.bindLabel(dtTrimDot);
        enc.movzx_rm8(x86::RAX, x86::RDI, -1);
        enc.cmp_ri32(x86::RAX, '.');
        size_t dtNoDot = enc.createLabel();
        enc.jne(dtNoDot);
        enc.dec_r(x86::RDI);
        enc.bindLabel(dtNoDot);
        enc.jmp(dtFinish);

        enc.bindLabel(dtZero);
        enc.mov_ri64(x86::R10, '0');
        enc.mov_mr8(x86::RDI, 0, x86::R10);
        enc.inc_r(x86::RDI);

        enc.bindLabel(dtFinish);
        enc.xor_r32(x86::RAX, x86::RAX);
        enc.mov_mr8(x86::RDI, 0, x86::RAX);
        enc.add_rsp(96);
        enc.pop(x86::RBP);
        enc.ret();
        elf.registerFuncPointer("sprintf_d", rvaDtoa);

        // --- O. _thava_linux_fopen (path in RCX, mode in RDX -> RAX filePtr) ---
        size_t lblFopen = enc.createLabel();
        enc.bindLabel(lblFopen);
        uint32_t rvaFopen = elf.textRva + (uint32_t)enc.code.size();

        enc.push(x86::RBP);
        enc.mov_rr(x86::RBP, x86::RSP);
        enc.sub_rsp(64);

        enc.mov_mr_rbp(-8, x86::RCX);  // path
        enc.mov_mr_rbp(-16, x86::RDX); // mode

        // Default: flags = O_RDONLY (0), mode = 0
        enc.xor_r32(x86::RSI, x86::RSI); // flags = 0
        enc.xor_r32(x86::RDX, x86::RDX); // mode = 0

        // Check mode string
        enc.mov_rm_rbp(x86::RAX, -16);
        enc.test_rr(x86::RAX, x86::RAX);
        size_t modeDone = enc.createLabel();
        enc.je(modeDone);

        enc.movzx_rm8(x86::R10, x86::RAX, 0); // mode[0]
        enc.cmp_ri32(x86::R10, 'w');
        size_t notW = enc.createLabel();
        enc.jne(notW);
        // flags = O_WRONLY | O_CREAT | O_TRUNC = 0x241 = 577
        enc.mov_ri64(x86::RSI, 0x241);
        // mode = 0666 octal = 438 decimal (rw-rw-rw-)
        enc.mov_ri64(x86::RDX, 438);
        enc.jmp(modeDone);

        enc.bindLabel(notW);
        enc.cmp_ri32(x86::R10, 'a');
        size_t notA = enc.createLabel();
        enc.jne(notA);
        // flags = O_WRONLY | O_CREAT | O_APPEND = 0x441 = 1089
        enc.mov_ri64(x86::RSI, 0x441);
        enc.mov_ri64(x86::RDX, 438);
        enc.jmp(modeDone);

        enc.bindLabel(notA);
        // Otherwise 'r': flags = 0, mode = 0

        enc.bindLabel(modeDone);
        // sys_open: syscall 2 (rdi = filename, rsi = flags, rdx = mode)
        enc.mov_rm_rbp(x86::RDI, -8); // path
        enc.mov_ri64(x86::RAX, 2);    // sys_open = 2
        enc.syscall();

        // Check if RAX < 0 (error)
        enc.mov_ri64(x86::R10, 0);
        enc.cmp_rr(x86::RAX, x86::R10);
        size_t openFail = enc.createLabel();
        size_t openRet = enc.createLabel();
        enc.jl(openFail);

        // Success: RAX is fd >= 0. Allocate 16 bytes: struct { int64_t fd; }
        enc.mov_mr_rbp(-24, x86::RAX); // save fd
        enc.mov_ri64(x86::RCX, 16);
        enc.call_label(lblMalloc);     // RAX = heap ptr
        enc.mov_rm_rbp(x86::R10, -24); // fd
        enc.mov_mr(x86::RAX, 0, x86::R10); // filePtr->fd = fd
        enc.jmp(openRet);

        enc.bindLabel(openFail);
        enc.xor_r32(x86::RAX, x86::RAX); // return 0 (NULL)

        enc.bindLabel(openRet);
        enc.add_rsp(64);
        enc.pop(x86::RBP);
        enc.ret();
        elf.registerFuncPointer("fopen", rvaFopen);

        // --- P. _thava_linux_fclose (filePtr in RCX -> RAX 0) ---
        size_t lblFclose = enc.createLabel();
        enc.bindLabel(lblFclose);
        uint32_t rvaFclose = elf.textRva + (uint32_t)enc.code.size();

        enc.test_rr(x86::RCX, x86::RCX);
        size_t closeDone = enc.createLabel();
        enc.je(closeDone);

        enc.push(x86::RBP);
        enc.mov_rr(x86::RBP, x86::RSP);
        enc.sub_rsp(32);

        enc.mov_rm(x86::RDI, x86::RCX, 0); // rdi = fd
        enc.mov_ri64(x86::RAX, 3);          // sys_close = 3
        enc.syscall();

        enc.xor_r32(x86::RAX, x86::RAX);
        enc.add_rsp(32);
        enc.pop(x86::RBP);

        enc.bindLabel(closeDone);
        enc.ret();
        elf.registerFuncPointer("fclose", rvaFclose);

        // --- Q. _thava_linux_fwrite (buf in RCX, size in RDX, count in R8, filePtr in R9 -> RAX bytesWritten) ---
        size_t lblFwrite = enc.createLabel();
        enc.bindLabel(lblFwrite);
        uint32_t rvaFwrite = elf.textRva + (uint32_t)enc.code.size();

        enc.test_rr(x86::R9, x86::R9);
        size_t writeDone = enc.createLabel();
        enc.je(writeDone);

        enc.push(x86::RBP);
        enc.mov_rr(x86::RBP, x86::RSP);
        enc.sub_rsp(32);

        // totalBytes = size * count = RDX * R8
        enc.imul_rr(x86::RDX, x86::R8);
        // rsi = buf (RCX)
        enc.mov_rr(x86::RSI, x86::RCX);
        // rdi = fd = [R9 + 0]
        enc.mov_rm(x86::RDI, x86::R9, 0);
        enc.mov_ri64(x86::RAX, 1); // sys_write = 1
        enc.syscall();

        enc.add_rsp(32);
        enc.pop(x86::RBP);
        enc.ret();

        enc.bindLabel(writeDone);
        enc.xor_r32(x86::RAX, x86::RAX);
        enc.ret();
        elf.registerFuncPointer("fwrite", rvaFwrite);

        // --- R. _thava_linux_fputs (str in RCX, filePtr in RDX -> RAX) ---
        size_t lblFputs = enc.createLabel();
        enc.bindLabel(lblFputs);
        uint32_t rvaFputs = elf.textRva + (uint32_t)enc.code.size();

        enc.test_rr(x86::RDX, x86::RDX);
        size_t fputsDone = enc.createLabel();
        enc.je(fputsDone);

        enc.push(x86::RBP);
        enc.mov_rr(x86::RBP, x86::RSP);
        enc.sub_rsp(32);

        enc.mov_mr_rbp(-8, x86::RCX);  // str
        enc.mov_mr_rbp(-16, x86::RDX); // filePtr

        enc.call_label(lblStrlen); // length in RAX
        enc.mov_rr(x86::RDX, x86::RAX); // len in RDX
        enc.mov_rm_rbp(x86::RSI, -8);   // str buf in RSI
        enc.mov_rm_rbp(x86::R10, -16);  // filePtr
        enc.mov_rm(x86::RDI, x86::R10, 0); // fd in RDI
        enc.mov_ri64(x86::RAX, 1);      // sys_write = 1
        enc.syscall();

        enc.add_rsp(32);
        enc.pop(x86::RBP);
        enc.ret();

        enc.bindLabel(fputsDone);
        enc.xor_r32(x86::RAX, x86::RAX);
        enc.ret();
        elf.registerFuncPointer("fputs", rvaFputs);

        // --- S. _thava_linux_fseek (filePtr in RCX, offset in RDX, whence in R8 -> RAX) ---
        size_t lblFseek = enc.createLabel();
        enc.bindLabel(lblFseek);
        uint32_t rvaFseek = elf.textRva + (uint32_t)enc.code.size();

        enc.test_rr(x86::RCX, x86::RCX);
        size_t fseekDone = enc.createLabel();
        enc.je(fseekDone);

        enc.push(x86::RBP);
        enc.mov_rr(x86::RBP, x86::RSP);
        enc.sub_rsp(32);

        enc.mov_rm(x86::RDI, x86::RCX, 0); // fd
        enc.mov_rr(x86::RSI, x86::RDX);     // offset
        enc.mov_rr(x86::RDX, x86::R8);      // whence (0=SET, 1=CUR, 2=END)
        enc.mov_ri64(x86::RAX, 8);          // sys_lseek = 8
        enc.syscall();

        enc.xor_r32(x86::RAX, x86::RAX);   // return 0 on success
        enc.add_rsp(32);
        enc.pop(x86::RBP);
        enc.ret();

        enc.bindLabel(fseekDone);
        enc.mov_ri64(x86::RAX, -1);
        enc.ret();
        elf.registerFuncPointer("fseek", rvaFseek);

        // --- T. _thava_linux_ftell (filePtr in RCX -> RAX offset) ---
        size_t lblFtell = enc.createLabel();
        enc.bindLabel(lblFtell);
        uint32_t rvaFtell = elf.textRva + (uint32_t)enc.code.size();

        enc.test_rr(x86::RCX, x86::RCX);
        size_t ftellDone = enc.createLabel();
        enc.je(ftellDone);

        enc.push(x86::RBP);
        enc.mov_rr(x86::RBP, x86::RSP);
        enc.sub_rsp(32);

        enc.mov_rm(x86::RDI, x86::RCX, 0); // fd
        enc.xor_r32(x86::RSI, x86::RSI);   // offset = 0
        enc.mov_ri64(x86::RDX, 1);          // SEEK_CUR = 1
        enc.mov_ri64(x86::RAX, 8);          // sys_lseek = 8
        enc.syscall();                      // returns pos in RAX

        enc.add_rsp(32);
        enc.pop(x86::RBP);
        enc.ret();

        enc.bindLabel(ftellDone);
        enc.mov_ri64(x86::RAX, -1);
        enc.ret();
        elf.registerFuncPointer("ftell", rvaFtell);

        // --- U. _thava_linux_fread (buf in RCX, size in RDX, count in R8, filePtr in R9 -> RAX bytesRead) ---
        size_t lblFread = enc.createLabel();
        enc.bindLabel(lblFread);
        uint32_t rvaFread = elf.textRva + (uint32_t)enc.code.size();

        enc.test_rr(x86::R9, x86::R9);
        size_t freadDone = enc.createLabel();
        enc.je(freadDone);

        enc.push(x86::RBP);
        enc.mov_rr(x86::RBP, x86::RSP);
        enc.sub_rsp(32);

        enc.imul_rr(x86::RDX, x86::R8);     // totalBytes in RDX
        enc.mov_rr(x86::RSI, x86::RCX);     // buf
        enc.mov_rm(x86::RDI, x86::R9, 0);   // fd
        enc.mov_ri64(x86::RAX, 0);          // sys_read = 0
        enc.syscall();                      // returns bytes read in RAX

        enc.add_rsp(32);
        enc.pop(x86::RBP);
        enc.ret();

        enc.bindLabel(freadDone);
        enc.xor_r32(x86::RAX, x86::RAX);
        enc.ret();
        elf.registerFuncPointer("fread", rvaFread);

        // --- GetCurrentProcessId -> RAX ---
        size_t lblGetpid = enc.createLabel();
        enc.bindLabel(lblGetpid);
        uint32_t rvaGetpid = elf.textRva + (uint32_t)enc.code.size();
        enc.mov_ri64(x86::RAX, 39); // sys_getpid
        enc.syscall();
        enc.ret();
        elf.registerFuncPointer("GetCurrentProcessId", rvaGetpid);

        // --- Sleep (ms in RCX) ---
        size_t lblSleep = enc.createLabel();
        enc.bindLabel(lblSleep);
        uint32_t rvaSleep = elf.textRva + (uint32_t)enc.code.size();
        enc.push(x86::RBP);
        enc.mov_rr(x86::RBP, x86::RSP);
        enc.sub_rsp(32);
        // struct timespec { long tv_sec; long tv_nsec; } at [RBP-16]
        enc.mov_rr(x86::RAX, x86::RCX);
        enc.xor_r32(x86::RDX, x86::RDX);
        enc.mov_ri64(x86::R10, 1000);
        enc.div_r(x86::R10); // RAX = sec, RDX = remainder ms
        enc.mov_mr_rbp(-16, x86::RAX); // tv_sec
        enc.mov_ri64(x86::R10, 1000000);
        enc.imul_rr(x86::RDX, x86::R10);
        enc.mov_mr_rbp(-8, x86::RDX);  // tv_nsec
        enc.lea_rm_rbp(x86::RDI, -16); // req
        enc.xor_r32(x86::RSI, x86::RSI); // rem = NULL
        enc.mov_ri64(x86::RAX, 35); // sys_nanosleep = 35
        enc.syscall();
        enc.add_rsp(32);
        enc.pop(x86::RBP);
        enc.ret();
        elf.registerFuncPointer("Sleep", rvaSleep);

        // --- remove (path in RCX) -> 0 on success ---
        size_t lblRemove = enc.createLabel();
        enc.bindLabel(lblRemove);
        uint32_t rvaRemove = elf.textRva + (uint32_t)enc.code.size();
        enc.mov_rr(x86::RDI, x86::RCX);
        enc.mov_ri64(x86::RAX, 87); // sys_unlink = 87
        enc.syscall();
        enc.ret();
        elf.registerFuncPointer("remove", rvaRemove);

        // --- rename (old in RCX, new in RDX) -> 0 on success ---
        size_t lblRename = enc.createLabel();
        enc.bindLabel(lblRename);
        uint32_t rvaRename = elf.textRva + (uint32_t)enc.code.size();
        enc.mov_rr(x86::RDI, x86::RCX);
        enc.mov_rr(x86::RSI, x86::RDX);
        enc.mov_ri64(x86::RAX, 82); // sys_rename = 82
        enc.syscall();
        enc.ret();
        elf.registerFuncPointer("rename", rvaRename);

        // --- _access (path in RCX, mode in RDX) -> 0 on success ---
        size_t lblAccess = enc.createLabel();
        enc.bindLabel(lblAccess);
        uint32_t rvaAccess = elf.textRva + (uint32_t)enc.code.size();
        enc.mov_rr(x86::RDI, x86::RCX);
        enc.mov_rr(x86::RSI, x86::RDX);
        enc.mov_ri64(x86::RAX, 21); // sys_access = 21
        enc.syscall();
        enc.ret();
        elf.registerFuncPointer("_access", rvaAccess);

        // --- _mkdir (path in RCX) -> 0 on success ---
        size_t lblMkdir = enc.createLabel();
        enc.bindLabel(lblMkdir);
        uint32_t rvaMkdir = elf.textRva + (uint32_t)enc.code.size();
        enc.mov_rr(x86::RDI, x86::RCX);
        enc.mov_ri64(x86::RSI, 0755); // mode 0755
        enc.mov_ri64(x86::RAX, 83);   // sys_mkdir = 83
        enc.syscall();
        enc.ret();
        elf.registerFuncPointer("_mkdir", rvaMkdir);

        // --- _rmdir (path in RCX) -> 0 on success ---
        size_t lblRmdir = enc.createLabel();
        enc.bindLabel(lblRmdir);
        uint32_t rvaRmdir = elf.textRva + (uint32_t)enc.code.size();
        enc.mov_rr(x86::RDI, x86::RCX);
        enc.mov_ri64(x86::RAX, 84); // sys_rmdir = 84
        enc.syscall();
        enc.ret();
        elf.registerFuncPointer("_rmdir", rvaRmdir);

        // --- _chdir (path in RCX) -> 0 on success ---
        size_t lblChdir = enc.createLabel();
        enc.bindLabel(lblChdir);
        uint32_t rvaChdir = elf.textRva + (uint32_t)enc.code.size();
        enc.mov_rr(x86::RDI, x86::RCX);
        enc.mov_ri64(x86::RAX, 80); // sys_chdir = 80
        enc.syscall();
        enc.ret();
        elf.registerFuncPointer("_chdir", rvaChdir);

        // --- _getcwd (buf in RCX, size in RDX) -> RAX ---
        size_t lblGetcwd = enc.createLabel();
        enc.bindLabel(lblGetcwd);
        uint32_t rvaGetcwd = elf.textRva + (uint32_t)enc.code.size();
        enc.lea_rip_rdata(x86::RDI, offReadBuf);
        enc.mov_ri64(x86::RSI, 1024);
        enc.mov_ri64(x86::RAX, 79); // sys_getcwd = 79
        enc.syscall();
        size_t lblCwdFail = enc.createLabel();
        enc.cmp_ri32(x86::RAX, 0);
        enc.jle(lblCwdFail);
        enc.lea_rip_rdata(x86::RAX, offReadBuf);
        enc.ret();
        enc.bindLabel(lblCwdFail);
        enc.xor_r32(x86::RAX, x86::RAX);
        enc.ret();
        elf.registerFuncPointer("_getcwd", rvaGetcwd);

        // --- getenv (name in RCX) -> RAX ---
        size_t lblGetenv = enc.createLabel();
        enc.bindLabel(lblGetenv);
        uint32_t rvaGetenv = elf.textRva + (uint32_t)enc.code.size();
        enc.xor_r32(x86::RAX, x86::RAX);
        enc.ret();
        elf.registerFuncPointer("getenv", rvaGetenv);

        // --- MessageBoxA (hWnd in RCX, text in RDX, caption in R8, uType in R9) -> RAX ---
        size_t lblMsgBox = enc.createLabel();
        enc.bindLabel(lblMsgBox);
        uint32_t rvaMsgBox = elf.textRva + (uint32_t)enc.code.size();
        enc.push(x86::RBP);
        enc.mov_rr(x86::RBP, x86::RSP);
        enc.sub_rsp(64);

        enc.mov_mr_rbp(-8, x86::RDX);  // text
        enc.mov_mr_rbp(-16, x86::R8);  // caption
        enc.mov_mr_rbp(-24, x86::R9);  // uType

        // 1. Print caption: "\n[ " + caption + " ]\n"
        uint32_t offMsgOpen = elf.addStringLiteralOffset("\n[ ");
        enc.lea_rip_rdata(x86::RCX, offMsgOpen);
        enc.call_iat_func("printf");

        enc.mov_rm_rbp(x86::RCX, -16);
        enc.call_iat_func("printf");

        uint32_t offMsgClose = elf.addStringLiteralOffset(" ]\n");
        enc.lea_rip_rdata(x86::RCX, offMsgClose);
        enc.call_iat_func("printf");

        // Print text + "\n"
        enc.mov_rm_rbp(x86::RCX, -8);
        enc.call_iat_func("printf");
        uint32_t offNL = elf.addStringLiteralOffset("\n");
        enc.lea_rip_rdata(x86::RCX, offNL);
        enc.call_iat_func("printf");

        // 2. Check uType for buttons (uType & 0x0F)
        enc.mov_rm_rbp(x86::RAX, -24);
        enc.mov_ri64(x86::R10, 0x0F);
        enc.and_rr(x86::RAX, x86::R10);

        size_t lblIsYesNo = enc.createLabel();
        size_t lblMsgDone = enc.createLabel();

        // cmp rax, 4 (MB_YESNO) or 3 (MB_YESNOCANCEL)
        enc.cmp_ri32(x86::RAX, 4);
        enc.je(lblIsYesNo);
        enc.cmp_ri32(x86::RAX, 3);
        enc.je(lblIsYesNo);

        // Default: MB_OK
        uint32_t offOkPrompt = elf.addStringLiteralOffset("[Press Enter to continue...]\n");
        enc.lea_rip_rdata(x86::RCX, offOkPrompt);
        enc.call_iat_func("printf");
        enc.xor_r32(x86::RDI, x86::RDI); // fd = 0 (stdin)
        enc.lea_rip_rdata(x86::RSI, offReadBuf);
        enc.mov_ri64(x86::RDX, 1);
        enc.mov_ri64(x86::RAX, 0); // sys_read = 0
        enc.syscall();
        enc.mov_ri64(x86::RAX, 1); // IDOK = 1
        enc.jmp(lblMsgDone);

        // Yes/No prompt
        enc.bindLabel(lblIsYesNo);
        uint32_t offYnPrompt = elf.addStringLiteralOffset("[Yes/No] (y/n): ");
        enc.lea_rip_rdata(x86::RCX, offYnPrompt);
        enc.call_iat_func("printf");
        enc.xor_r32(x86::RDI, x86::RDI); // fd = 0
        enc.lea_rip_rdata(x86::RSI, offReadBuf);
        enc.mov_ri64(x86::RDX, 1);
        enc.mov_ri64(x86::RAX, 0); // sys_read = 0
        enc.syscall();
        enc.lea_rip_rdata(x86::RSI, offReadBuf);
        enc.movzx_rm8(x86::RAX, x86::RSI, 0);

        size_t lblSaidYes = enc.createLabel();
        enc.cmp_ri32(x86::RAX, 'y');
        enc.je(lblSaidYes);
        enc.cmp_ri32(x86::RAX, 'Y');
        enc.je(lblSaidYes);
        enc.mov_ri64(x86::RAX, 7); // IDNO = 7
        enc.jmp(lblMsgDone);

        enc.bindLabel(lblSaidYes);
        enc.mov_ri64(x86::RAX, 6); // IDYES = 6

        enc.bindLabel(lblMsgDone);
        enc.add_rsp(64);
        enc.pop(x86::RBP);
        elf.registerFuncPointer("MessageBoxA", rvaMsgBox);

        // --- GetLocalTime (st pointer in RCX) ---
        size_t lblGetLocalTime = enc.createLabel();
        enc.bindLabel(lblGetLocalTime);
        uint32_t rvaGetLocalTime = elf.textRva + (uint32_t)enc.code.size();
        enc.push(x86::RBP);
        enc.mov_rr(x86::RBP, x86::RSP);
        enc.sub_rsp(64);

        // Save st pointer at [rbp - 8]
        enc.mov_mr_rbp(-8, x86::RCX);

        // 1. Call sys_clock_gettime(CLOCK_REALTIME = 0, &timespec at [rbp - 32])
        // timespec: tv_sec at [rbp - 32], tv_nsec at [rbp - 24]
        enc.xor_r32(x86::RDI, x86::RDI); // CLOCK_REALTIME = 0
        enc.lea_rm_rbp(x86::RSI, -32);
        enc.mov_ri64(x86::RAX, 228);      // sys_clock_gettime = 228
        enc.syscall();

        // Check if syscall succeeded (RAX == 0). If not, fallback to sys_time
        enc.test_rr(x86::RAX, x86::RAX);
        size_t lblClockOk = enc.createLabel();
        enc.je(lblClockOk);

        // Fallback: sys_time
        enc.xor_r32(x86::RDI, x86::RDI);
        enc.mov_ri64(x86::RAX, 201); // sys_time = 201
        enc.syscall();
        enc.mov_mr_rbp(-32, x86::RAX); // tv_sec
        enc.mov_ri64(x86::R10, 0);
        enc.mov_mr_rbp(-24, x86::R10); // tv_nsec = 0

        enc.bindLabel(lblClockOk);
        // Load st pointer into RDI
        enc.mov_rm_rbp(x86::RDI, -8);

        // Milliseconds = tv_nsec / 1000000
        enc.mov_rm_rbp(x86::RAX, -24);
        enc.xor_r32(x86::RDX, x86::RDX);
        enc.mov_ri64(x86::R10, 1000000);
        enc.div_r(x86::R10);
        enc.mov_mr16(x86::RDI, 14, x86::RAX); // st->wMilliseconds

        // T = tv_sec
        enc.mov_rm_rbp(x86::RAX, -32);
        // Days D = T / 86400, SecInDay S = T % 86400
        enc.xor_r32(x86::RDX, x86::RDX);
        enc.mov_ri64(x86::R10, 86400);
        enc.div_r(x86::R10);
        // RAX = D, RDX = S
        enc.mov_mr_rbp(-40, x86::RAX); // save D

        // S / 60 -> RAX = MinInDay, RDX = Second
        enc.mov_rr(x86::RAX, x86::RDX);
        enc.xor_r32(x86::RDX, x86::RDX);
        enc.mov_ri64(x86::R10, 60);
        enc.div_r(x86::R10);
        enc.mov_mr16(x86::RDI, 12, x86::RDX); // st->wSecond

        // MinInDay / 60 -> RAX = Hour, RDX = Minute
        enc.xor_r32(x86::RDX, x86::RDX);
        enc.mov_ri64(x86::R10, 60);
        enc.div_r(x86::R10);
        enc.mov_mr16(x86::RDI, 10, x86::RDX); // st->wMinute
        enc.mov_mr16(x86::RDI, 8, x86::RAX);  // st->wHour

        // Day of week: (D + 4) % 7
        enc.mov_rm_rbp(x86::RAX, -40);
        enc.add_ri32(x86::RAX, 4);
        enc.xor_r32(x86::RDX, x86::RDX);
        enc.mov_ri64(x86::R10, 7);
        enc.div_r(x86::R10);
        enc.mov_mr16(x86::RDI, 4, x86::RDX); // st->wDayOfWeek

        // Year loop: from year = 1970
        // RAX = D (days remaining)
        enc.mov_rm_rbp(x86::RAX, -40);
        enc.mov_ri64(x86::R8, 1970); // R8 = year

        size_t lblYearLoop = enc.createLabel();
        size_t lblYearDone = enc.createLabel();
        enc.bindLabel(lblYearLoop);
        // Is year a leap year? In 1970..2099: (year % 4 == 0)
        enc.mov_rr(x86::R9, x86::R8);
        enc.mov_ri64(x86::R10, 3);
        enc.and_rr(x86::R9, x86::R10);
        enc.mov_ri64(x86::R11, 365);
        size_t lblNotLeapYear = enc.createLabel();
        enc.jne(lblNotLeapYear);
        enc.inc_r(x86::R11); // 366
        enc.bindLabel(lblNotLeapYear);

        enc.cmp_rr(x86::RAX, x86::R11);
        enc.jl(lblYearDone);
        enc.sub_rr(x86::RAX, x86::R11);
        enc.inc_r(x86::R8);
        enc.jmp(lblYearLoop);

        enc.bindLabel(lblYearDone);
        enc.mov_mr16(x86::RDI, 0, x86::R8); // st->wYear
        // R8 contains the current year. Check if current year is leap:
        enc.mov_rr(x86::R9, x86::R8);
        enc.mov_ri64(x86::R10, 3);
        enc.and_rr(x86::R9, x86::R10);
        // If R9 == 0, current year is leap year. Save isLeap (1 or 0) in R8
        size_t lblCurYearNotLeap = enc.createLabel();
        size_t lblCurYearLeapDone = enc.createLabel();
        enc.jne(lblCurYearNotLeap);
        enc.mov_ri64(x86::R8, 1);
        enc.jmp(lblCurYearLeapDone);
        enc.bindLabel(lblCurYearNotLeap);
        enc.xor_r32(x86::R8, x86::R8);
        enc.bindLabel(lblCurYearLeapDone);

        // Now RAX is day_of_year (0-indexed).
        // Month loop: R9 = month (1..12)
        enc.mov_ri64(x86::R9, 1);
        size_t lblMonthLoop = enc.createLabel();
        size_t lblMonthDone = enc.createLabel();
        enc.bindLabel(lblMonthLoop);
        // Days in month R9:
        // Feb (R9 == 2): 28 + R8 (isLeap)
        // 30 days: 4, 6, 9, 11
        // 31 days: 1, 3, 5, 7, 8, 10, 12
        size_t lblIsFeb = enc.createLabel();
        size_t lblIs30 = enc.createLabel();
        size_t lblDaysFound = enc.createLabel();

        enc.cmp_ri32(x86::R9, 2);
        enc.je(lblIsFeb);
        enc.cmp_ri32(x86::R9, 4);
        enc.je(lblIs30);
        enc.cmp_ri32(x86::R9, 6);
        enc.je(lblIs30);
        enc.cmp_ri32(x86::R9, 9);
        enc.je(lblIs30);
        enc.cmp_ri32(x86::R9, 11);
        enc.je(lblIs30);

        // 31 days
        enc.mov_ri64(x86::R10, 31);
        enc.jmp(lblDaysFound);

        enc.bindLabel(lblIsFeb);
        enc.mov_ri64(x86::R10, 28);
        enc.add_rr(x86::R10, x86::R8);
        enc.jmp(lblDaysFound);

        enc.bindLabel(lblIs30);
        enc.mov_ri64(x86::R10, 30);

        enc.bindLabel(lblDaysFound);
        // If RAX < R10, done!
        enc.cmp_rr(x86::RAX, x86::R10);
        enc.jl(lblMonthDone);
        enc.sub_rr(x86::RAX, x86::R10);
        enc.inc_r(x86::R9);
        enc.jmp(lblMonthLoop);

        enc.bindLabel(lblMonthDone);
        // st->wMonth = R9
        enc.mov_mr16(x86::RDI, 2, x86::R9);
        // st->wDay = RAX + 1
        enc.inc_r(x86::RAX);
        enc.mov_mr16(x86::RDI, 6, x86::RAX);

        enc.add_rsp(64);
        enc.pop(x86::RBP);
        enc.ret();
        elf.registerFuncPointer("GetLocalTime", rvaGetLocalTime);
    }
};

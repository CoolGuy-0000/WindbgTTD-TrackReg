#include <Windows.h>
#include <string>
#include <vector>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <algorithm>
#include <cstring>

#define KDEXT_64BIT
#include <dbgeng.h>
#include <wdbgexts.h>
#include <atlcomcli.h>

#include "disasm_helper.h"

ZydisRegister GetRegisterByName(const char* reg) {
    static std::unordered_map<std::string, ZydisRegister> registerMap;
    static bool isInitialized = false;

    if (!isInitialized) {
        for (int i = 0; i < ZYDIS_REGISTER_MAX_VALUE; ++i) {
            ZydisRegister currentReg = (ZydisRegister)i;
            const char* regStr = ZydisRegisterGetString(currentReg);

            if (regStr) {
                registerMap[regStr] = currentReg;
            }
        }
        isInitialized = true;
    }

    if (!reg) return ZYDIS_REGISTER_NONE;

    std::string query = reg;
    std::transform(query.begin(), query.end(), query.begin(),
        [](unsigned char c) { return std::tolower(c); });

    auto it = registerMap.find(query);
    if (it != registerMap.end()) {
        return it->second;
    }

    return ZYDIS_REGISTER_NONE;
}

RegValue GetRegisterValue(const AMD64_CONTEXT& context, ZydisRegister reg, bool bError) {
    RegValue ret = {0};

    if (ZydisRegisterGetClass(reg) == ZYDIS_REGCLASS_GPR8) {
        switch (reg) {
            case ZYDIS_REGISTER_AH: ret.val[0] = (context.Rax >> 8) & 0xFF; return ret;
            case ZYDIS_REGISTER_CH: ret.val[0] = (context.Rcx >> 8) & 0xFF; return ret;
            case ZYDIS_REGISTER_DH: ret.val[0] = (context.Rdx >> 8) & 0xFF; return ret;
            case ZYDIS_REGISTER_BH: ret.val[0] = (context.Rbx >> 8) & 0xFF; return ret;
            default: break;
        }
    }

    // GPRs
    switch (reg) {
        case ZYDIS_REGISTER_RAX: case ZYDIS_REGISTER_EAX: case ZYDIS_REGISTER_AX: case ZYDIS_REGISTER_AL:
            ret.val[0] = context.Rax; return ret;

        case ZYDIS_REGISTER_RBX: case ZYDIS_REGISTER_EBX: case ZYDIS_REGISTER_BX: case ZYDIS_REGISTER_BL:
            ret.val[0] = context.Rbx; return ret;

        case ZYDIS_REGISTER_RCX: case ZYDIS_REGISTER_ECX: case ZYDIS_REGISTER_CX: case ZYDIS_REGISTER_CL:
            ret.val[0] = context.Rcx; return ret;

        case ZYDIS_REGISTER_RDX: case ZYDIS_REGISTER_EDX: case ZYDIS_REGISTER_DX: case ZYDIS_REGISTER_DL:
            ret.val[0] = context.Rdx; return ret;

        case ZYDIS_REGISTER_RSI: case ZYDIS_REGISTER_ESI: case ZYDIS_REGISTER_SI: case ZYDIS_REGISTER_SIL:
            ret.val[0] = context.Rsi; return ret;
        case ZYDIS_REGISTER_RDI: case ZYDIS_REGISTER_EDI: case ZYDIS_REGISTER_DI: case ZYDIS_REGISTER_DIL:
            ret.val[0] = context.Rdi; return ret;
        case ZYDIS_REGISTER_RBP: case ZYDIS_REGISTER_EBP: case ZYDIS_REGISTER_BP: case ZYDIS_REGISTER_BPL:
            ret.val[0] = context.Rbp; return ret;
        case ZYDIS_REGISTER_RSP: case ZYDIS_REGISTER_ESP: case ZYDIS_REGISTER_SP: case ZYDIS_REGISTER_SPL:
            ret.val[0] = context.Rsp; return ret;

        case ZYDIS_REGISTER_R8: case ZYDIS_REGISTER_R8D: case ZYDIS_REGISTER_R8W: case ZYDIS_REGISTER_R8B:
            ret.val[0] = context.R8; return ret;
        case ZYDIS_REGISTER_R9: case ZYDIS_REGISTER_R9D: case ZYDIS_REGISTER_R9W: case ZYDIS_REGISTER_R9B:
            ret.val[0] = context.R9; return ret;
        case ZYDIS_REGISTER_R10: case ZYDIS_REGISTER_R10D: case ZYDIS_REGISTER_R10W: case ZYDIS_REGISTER_R10B:
            ret.val[0] = context.R10; return ret;
        case ZYDIS_REGISTER_R11: case ZYDIS_REGISTER_R11D: case ZYDIS_REGISTER_R11W: case ZYDIS_REGISTER_R11B:
            ret.val[0] = context.R11; return ret;
        case ZYDIS_REGISTER_R12: case ZYDIS_REGISTER_R12D: case ZYDIS_REGISTER_R12W: case ZYDIS_REGISTER_R12B:
            ret.val[0] = context.R12; return ret;
        case ZYDIS_REGISTER_R13: case ZYDIS_REGISTER_R13D: case ZYDIS_REGISTER_R13W: case ZYDIS_REGISTER_R13B:
            ret.val[0] = context.R13; return ret;
        case ZYDIS_REGISTER_R14: case ZYDIS_REGISTER_R14D: case ZYDIS_REGISTER_R14W: case ZYDIS_REGISTER_R14B:
            ret.val[0] = context.R14; return ret;
        case ZYDIS_REGISTER_R15: case ZYDIS_REGISTER_R15D: case ZYDIS_REGISTER_R15W: case ZYDIS_REGISTER_R15B:
            ret.val[0] = context.R15; return ret;

        case ZYDIS_REGISTER_RIP:
            ret.val[0] = context.Rip; return ret;
    }

    // XMMs
    // Assuming context.Xmm* are 16 bytes (M128A/M128BIT)
    const void* src = nullptr;
    switch(reg) {
        case ZYDIS_REGISTER_XMM0: src = &context.Xmm0; break;
        case ZYDIS_REGISTER_XMM1: src = &context.Xmm1; break;
        case ZYDIS_REGISTER_XMM2: src = &context.Xmm2; break;
        case ZYDIS_REGISTER_XMM3: src = &context.Xmm3; break;
        case ZYDIS_REGISTER_XMM4: src = &context.Xmm4; break;
        case ZYDIS_REGISTER_XMM5: src = &context.Xmm5; break;
        case ZYDIS_REGISTER_XMM6: src = &context.Xmm6; break;
        case ZYDIS_REGISTER_XMM7: src = &context.Xmm7; break;
        case ZYDIS_REGISTER_XMM8: src = &context.Xmm8; break;
        case ZYDIS_REGISTER_XMM9: src = &context.Xmm9; break;
        case ZYDIS_REGISTER_XMM10: src = &context.Xmm10; break;
        case ZYDIS_REGISTER_XMM11: src = &context.Xmm11; break;
        case ZYDIS_REGISTER_XMM12: src = &context.Xmm12; break;
        case ZYDIS_REGISTER_XMM13: src = &context.Xmm13; break;
        case ZYDIS_REGISTER_XMM14: src = &context.Xmm14; break;
        case ZYDIS_REGISTER_XMM15: src = &context.Xmm15; break;
        default: break;
    }

    if (src) {
        std::memcpy(&ret.val[0], src, 16);
        return ret;
    }

    if (bError)
        throw std::runtime_error("Unsupported register.");

    return ret;
}
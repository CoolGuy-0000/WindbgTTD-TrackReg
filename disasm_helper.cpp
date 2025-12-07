#include <Windows.h>
#include <string>
#include <vector>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

#include <TTD/IReplayEngine.h>
#include <TTD/IReplayEngineStl.h>
#include <TTD/IReplayEngineRegisters.h>

using namespace TTD;
using namespace Replay;

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

uint64_t GetRegisterValue(const AMD64_CONTEXT& context, ZydisRegister reg, bool bError) {

    if (ZydisRegisterGetClass(reg) == ZYDIS_REGCLASS_GPR8) {
        switch (reg) {
            case ZYDIS_REGISTER_AH: return (context.Rax >> 8) & 0xFF;
            case ZYDIS_REGISTER_CH: return (context.Rcx >> 8) & 0xFF;
            case ZYDIS_REGISTER_DH: return (context.Rdx >> 8) & 0xFF;
            case ZYDIS_REGISTER_BH: return (context.Rbx >> 8) & 0xFF;
            default: break;
        }
    }

    switch (reg) {

        case ZYDIS_REGISTER_RAX: case ZYDIS_REGISTER_EAX: case ZYDIS_REGISTER_AX: case ZYDIS_REGISTER_AL:
            return context.Rax;

        case ZYDIS_REGISTER_RBX: case ZYDIS_REGISTER_EBX: case ZYDIS_REGISTER_BX: case ZYDIS_REGISTER_BL:
            return context.Rbx;

        case ZYDIS_REGISTER_RCX: case ZYDIS_REGISTER_ECX: case ZYDIS_REGISTER_CX: case ZYDIS_REGISTER_CL:
            return context.Rcx;

        case ZYDIS_REGISTER_RDX: case ZYDIS_REGISTER_EDX: case ZYDIS_REGISTER_DX: case ZYDIS_REGISTER_DL:
            return context.Rdx;

            // RSI, RDI, RBP, RSP
        case ZYDIS_REGISTER_RSI: case ZYDIS_REGISTER_ESI: case ZYDIS_REGISTER_SI: case ZYDIS_REGISTER_SIL:
            return context.Rsi;
        case ZYDIS_REGISTER_RDI: case ZYDIS_REGISTER_EDI: case ZYDIS_REGISTER_DI: case ZYDIS_REGISTER_DIL:
            return context.Rdi;
        case ZYDIS_REGISTER_RBP: case ZYDIS_REGISTER_EBP: case ZYDIS_REGISTER_BP: case ZYDIS_REGISTER_BPL:
            return context.Rbp;
        case ZYDIS_REGISTER_RSP: case ZYDIS_REGISTER_ESP: case ZYDIS_REGISTER_SP: case ZYDIS_REGISTER_SPL:
            return context.Rsp;

            // R8 ~ R15
        case ZYDIS_REGISTER_R8: case ZYDIS_REGISTER_R8D: case ZYDIS_REGISTER_R8W: case ZYDIS_REGISTER_R8B:
            return context.R8;
        case ZYDIS_REGISTER_R9: case ZYDIS_REGISTER_R9D: case ZYDIS_REGISTER_R9W: case ZYDIS_REGISTER_R9B:
            return context.R9;
        case ZYDIS_REGISTER_R10: case ZYDIS_REGISTER_R10D: case ZYDIS_REGISTER_R10W: case ZYDIS_REGISTER_R10B:
            return context.R10;
        case ZYDIS_REGISTER_R11: case ZYDIS_REGISTER_R11D: case ZYDIS_REGISTER_R11W: case ZYDIS_REGISTER_R11B:
            return context.R11;
        case ZYDIS_REGISTER_R12: case ZYDIS_REGISTER_R12D: case ZYDIS_REGISTER_R12W: case ZYDIS_REGISTER_R12B:
            return context.R12;
        case ZYDIS_REGISTER_R13: case ZYDIS_REGISTER_R13D: case ZYDIS_REGISTER_R13W: case ZYDIS_REGISTER_R13B:
            return context.R13;
        case ZYDIS_REGISTER_R14: case ZYDIS_REGISTER_R14D: case ZYDIS_REGISTER_R14W: case ZYDIS_REGISTER_R14B:
            return context.R14;
        case ZYDIS_REGISTER_R15: case ZYDIS_REGISTER_R15D: case ZYDIS_REGISTER_R15W: case ZYDIS_REGISTER_R15B:
            return context.R15;

        case ZYDIS_REGISTER_RIP:
            return context.Rip;

        default:
            if (bError)
                throw std::runtime_error("Unsupported register.");
            return 0;
    }
}
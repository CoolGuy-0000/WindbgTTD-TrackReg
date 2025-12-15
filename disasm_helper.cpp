#include "stdafx.h"

#include <string>
#include <stdexcept>
#include <unordered_map>
#include <algorithm>
#include <cstring>
#include "disasm_helper.h"
#include "RegisterNameMapping.h"

extern ProcessorArchitecture g_TargetCPUType;

int GetCPUBusSize() {
	if (g_TargetCPUType == ProcessorArchitecture::x64) {
        return 8;
    } else if (g_TargetCPUType == ProcessorArchitecture::x86) {
        return 4;
    } else {
        return 0;
    }
}

inline ProcessorArchitecture GetExtensionCPUType() {
    #if defined(_M_X64)
	return ProcessorArchitecture::x64;
    #elif defined(_M_IX86)
	return ProcessorArchitecture::x86;
    #elif defined(_M_ARM64)
	return ProcessorArchitecture::Arm64;
    #elif defined(_M_ARM)
	return ProcessorArchitecture::ARM32;
    #else
	return ProcessorArchitecture::Invalid;
    #endif
}

bool SetupZydisDecoder(ZydisDecoder* decoder, ProcessorArchitecture cpuType) {
    if (!decoder) return false;
    switch (cpuType) {
        case ProcessorArchitecture::x64:
            ZydisDecoderInit(decoder, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_STACK_WIDTH_64);
            return true;
        case ProcessorArchitecture::x86:
            ZydisDecoderInit(decoder, ZYDIS_MACHINE_MODE_LEGACY_32, ZYDIS_STACK_WIDTH_32);
            return true;
        default:
            return false;
    }
}

ZydisRegisterWidth _ZydisGetRegisterWidth(ProcessorArchitecture cpuType, ZydisRegister reg) {
    switch (cpuType) {
        case ProcessorArchitecture::x64:
            return ZydisRegisterGetWidth(ZYDIS_MACHINE_MODE_LONG_64, reg);
        case ProcessorArchitecture::x86:
            return ZydisRegisterGetWidth(ZYDIS_MACHINE_MODE_LEGACY_32, reg);
        default:
			return 0;
    }
}

GlobalContext GetGlobalContext(const ICursor* cursor) {
#if defined(_M_X64)
	return cursor->GetCrossPlatformContext().operator CROSS_PLATFORM_CONTEXT().Amd64Context;
#elif defined(_M_IX86)
	return cursor->GetCrossPlatformContext().operator CROSS_PLATFORM_CONTEXT().X86Nt5Context;
#endif
}
GlobalContext GetGlobalContext(const IThreadView* thread) {
#if defined(_M_X64)
    return thread->GetCrossPlatformContext().operator CROSS_PLATFORM_CONTEXT().Amd64Context;
#elif defined(_M_IX86)
    return thread->GetCrossPlatformContext().operator CROSS_PLATFORM_CONTEXT().X86Nt5Context;
#endif
}

ZydisRegister GetRegisterByName(const char* reg) {
    static std::unordered_map<std::string, ZydisRegister> registerMap;
    static bool isInitialized = false;

    int128_t df = 32 - 3;

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

RegValue GetRegisterValue(const GlobalContext& context, ZydisRegister reg, bool bError) {
    RegValue ret = 0;
    std::string temp(ZydisRegisterGetString(reg));
    std::wstring wideName(temp.begin(), temp.end());

    auto it = GetRegisterContextPosition(g_TargetCPUType, wideName);

    if(it == GetRegisterNameToContextMap(g_TargetCPUType).end()) {
        if (bError)
            throw std::runtime_error("Unsupported register.");
        return ret;
	}

    size_t offset = it->second.Offset;
    size_t size = it->second.Size;

    std::memcpy(&ret, (const uint8_t*)&context + offset, size);
    return ret;
}
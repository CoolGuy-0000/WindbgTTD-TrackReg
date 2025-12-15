#pragma once

#include <TTD/IReplayEngine.h>
#include <TTD/IReplayEngineRegisters.h>
#include "RegisterNameMapping.h"

#include <Zydis/Zydis.h>
#include <__msvc_int128.hpp>

using namespace TTD;
using namespace Replay;

using int128_t = std::_Unsigned128;

typedef int128_t RegValue;

#if defined(_M_X64)
typedef AMD64_CONTEXT GlobalContext;
#elif defined(_M_IX86)
typedef X86_NT5_CONTEXT GlobalContext;
#else
#error Unknown architecture
#endif

int GetCPUBusSize();
ProcessorArchitecture GetExtensionCPUType();

bool SetupZydisDecoder(ZydisDecoder* decoder, ProcessorArchitecture cpuType);
ZydisRegisterWidth _ZydisGetRegisterWidth(ProcessorArchitecture cpuType, ZydisRegister reg);

GlobalContext GetGlobalContext(const ICursor* cursor);
GlobalContext GetGlobalContext(const IThreadView* thread);

RegValue GetRegisterValue(const GlobalContext& context, ZydisRegister reg, bool bError=true);
ZydisRegister GetRegisterByName(const char* reg);

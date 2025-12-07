#pragma once

#include <Windows.h>
#include <string>
#include <TTD/IReplayEngine.h>
#include <TTD/IReplayEngineStl.h>
#include <TTD/IReplayEngineRegisters.h>

#include <Zydis/Zydis.h>

using namespace TTD;
using namespace Replay;

uint64_t GetRegisterValue(const AMD64_CONTEXT& context, ZydisRegister reg, bool bError=true);
ZydisRegister GetRegisterByName(const char* reg);

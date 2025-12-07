#pragma once

#include <Windows.h>
#include <string>
#include <vector>
#include <array>

#include <TTD/IReplayEngine.h>
#include <TTD/IReplayEngineStl.h>
#include <TTD/IReplayEngineRegisters.h>

#include <Zydis/Zydis.h>

using namespace TTD;
using namespace Replay;

struct RegValue {
    // Stores up to 256 bits (YMM).
    // val[0] = low 64 bits (GPRs, XMM low)
    // val[1] = high 64 bits (XMM high)
    // val[2] = YMM low
    // val[3] = YMM high
    uint64_t val[4];

    bool operator==(const RegValue& other) const {
        return val[0] == other.val[0] && val[1] == other.val[1] &&
               val[2] == other.val[2] && val[3] == other.val[3];
    }
    bool operator!=(const RegValue& other) const {
        return !(*this == other);
    }
};

RegValue GetRegisterValue(const AMD64_CONTEXT& context, ZydisRegister reg, bool bError=true);
ZydisRegister GetRegisterByName(const char* reg);

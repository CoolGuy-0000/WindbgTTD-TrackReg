#pragma once
#include "stdafx.h"
#include <Windows.h>
#include <WDBGEXTS.H>
#include <atlcomcli.h>
#include <map>
#include <vector>
#include <string>
#include <dbgeng.h>
#include <TTD/IReplayEngine.h> // For Position
#include <Zydis/Zydis.h>

using namespace TTD;
using namespace Replay;

// Binary struct for file
struct TraceRecord {
    int id = 0;
    int parentId = 0;
    Position pos = Position::Invalid;
};

// Shared logic from timetrack.cpp
Position FindRegisterWrite(ICursor* cursor, ZydisRegister reg);
Position FindMemoryWrite(ICursor* cursor, uint64_t address, uint64_t size);

std::map<int, std::vector<TraceRecord>> _TimeTrack(IDebugClient* client, std::string targetStr, int size, int maxSteps);

extern std::map<int, std::vector<TraceRecord>> g_LastTraceTree;


// timetrack.cpp
//
// A WinDbg Extension that traces the origin of a value backwards in time using TTD.
#include "stdafx.h"

#include <Windows.h>
#include <exception>
#include <stdexcept>
#include <vector>
#include <string>
#include <format>
#include <iostream>
#include <sstream>
#include <deque>
#include <fstream>
#include <map>

#include "Formatters.h"
#include "ReplayHelpers.h"

#include <TTD/IReplayEngine.h>
#include <TTD/IReplayEngineStl.h>

#include <DbgEng.h>
#include <WDBGEXTS.H>
#include <atlcomcli.h>

#include "disasm_helper.h"

#include <Zydis/Zydis.h>
#include "TimeTrackGUI.h"
#include "TimeTrackLogic.h"

extern IReplayEngineView* g_pReplayEngine;
extern ICursorView* g_pGlobalCursor;
extern ProcessorArchitecture g_TargetCPUType;

extern TimeTrackGUI::TimeTrackGUIWnd* track_gui;

std::map<int, std::vector<TraceRecord>> g_LastTraceTree;

// ----------------------------------------------------------------------------
// Core Logic
// ----------------------------------------------------------------------------

// Find previous write to register
// Returns Position::Invalid if not found.
Position FindRegisterWrite(ICursor* cursor, ZydisRegister reg)
{
    struct __TargetReg {
        ZydisRegister reg = ZYDIS_REGISTER_NONE;
        RegValue value = 0;
    };

    __TargetReg targetReg;
    targetReg.reg = reg;

    GlobalContext context = GetGlobalContext(cursor);
    try {
        targetReg.value = GetRegisterValue(context, reg);
    }
    catch (...) {
        return Position::Invalid;
    }

    auto _MemoryWatchpointCallback = [](uintptr_t targetPtr, ICursor::MemoryWatchpointResult const&, IThreadView const* thread) {
        __TargetReg target = *(__TargetReg*)targetPtr;

        RegValue val;

        try
        {
            val = GetRegisterValue(GetGlobalContext(thread), target.reg);
        }
        catch (...)
        {
            return false;
        }

        if (val != target.value) {
            return true;
        }

        return false;
    };

    MemoryWatchpointData wd = { GuestAddress::Min, (uint64_t)GuestAddress::Max, DataAccessMask::Execute };

    cursor->AddMemoryWatchpoint(wd);
    cursor->SetEventMask(EventMask::MemoryWatchpoint);
    cursor->SetReplayFlags(ReplayFlags::ReplayOnlyCurrentThread | ReplayFlags::ReplaySegmentsSequentially);
    cursor->SetMemoryWatchpointCallback(_MemoryWatchpointCallback, (uintptr_t)&targetReg);

    ICursorView::ReplayResult result = cursor->ReplayBackward();

    cursor->RemoveMemoryWatchpoint(wd);

    if (result.StopReason == EventType::MemoryWatchpoint) {
        return cursor->GetPosition();
    }

    return Position::Invalid;
}

// Find previous write to memory
Position FindMemoryWrite(ICursor* cursor, uint64_t address, uint64_t size)
{
    MemoryWatchpointData wd = { (GuestAddress)address, size, DataAccessMask::Write };

    cursor->AddMemoryWatchpoint(wd);
    cursor->SetEventMask(EventMask::MemoryWatchpoint);
    cursor->SetReplayFlags(ReplayFlags::None);

    ICursorView::ReplayResult result = cursor->ReplayBackward();

	cursor->RemoveMemoryWatchpoint(wd);

    if (result.StopReason == EventType::MemoryWatchpoint){
        Position pos = cursor->GetPosition() - 1;
		cursor->SetPosition(pos);
        return pos;
    }

    return Position::Invalid;
}

// ----------------------------------------------------------------------------
// Main Logic
// ----------------------------------------------------------------------------


void PrintRecordTreeIterative(IDebugClient* client, std::map<int, std::vector<TraceRecord>>& tree, int rootId = 0) {
    CComQIPtr<IDebugControl> control(client);
    CComQIPtr<IDebugSymbols3> symbols(client);

	if (!control || !symbols) return;
    
    ZydisDecoder decoder;
    SetupZydisDecoder(&decoder, g_TargetCPUType);

    ZydisFormatter formatter;
    ZydisFormatterInit(&formatter, ZYDIS_FORMATTER_STYLE_INTEL);

    char buffer[256];
    BufferView bufferView{ buffer, sizeof(buffer) };
    ZydisDecodedInstruction instruction;
    ZydisDecodedOperand operands[ZYDIS_MAX_OPERAND_COUNT];

    UniqueCursor inspectCursor(g_pReplayEngine->NewCursor());

    struct StackState {
        const TraceRecord* record;
        int depth;
    };

    std::deque<StackState> workStack;

    auto rootIt = tree.find(rootId);
    if (rootIt != tree.end()) {
        const auto& children = rootIt->second;
        for (auto it = children.rbegin(); it != children.rend(); ++it) {
            workStack.push_back({ &(*it), 0 });
        }
    }

    while (!workStack.empty()) {
        StackState current = workStack.back();
        workStack.pop_back();

        const TraceRecord& record = *current.record;
        int depth = current.depth;

        inspectCursor->SetPosition(record.pos);
        
        uint64_t curIP = (uint64_t)inspectCursor->GetProgramCounter();
        
        std::string output;
        output.append(depth, '-');

        output += std::format("<exec cmd=\"!tt {}\">{}</exec>\t", record.pos, record.pos);

        uint64_t uDisp;
		symbols->GetNameByOffset(curIP, buffer, sizeof(buffer), NULL, &uDisp);
        output += std::format("{}+{:X}", buffer, uDisp);
        output += "\t";

        inspectCursor->QueryMemoryBuffer((GuestAddress)curIP, bufferView);

        if (ZYAN_SUCCESS(ZydisDecoderDecodeFull(&decoder, buffer, 16, &instruction, operands))) {
            ZydisFormatterFormatInstruction(&formatter, &instruction, operands, instruction.operand_count_visible, buffer, sizeof(buffer), curIP, ZYAN_NULL);
            output += buffer;
        }

        output += "\n";

        control->ControlledOutput(DEBUG_OUTCTL_THIS_CLIENT | DEBUG_OUTCTL_DML, DEBUG_OUTPUT_NORMAL, output.c_str());

        auto childIt = tree.find(record.id);
        if (childIt != tree.end()) {
            const auto& children = childIt->second;
            for (auto it = children.rbegin(); it != children.rend(); ++it) {
                workStack.push_back({ &(*it), depth + 1 });
            }
        }
    }
}

std::map<int, std::vector<TraceRecord>> _TimeTrack(IDebugClient* client, std::string targetStr, int size, int maxSteps)
{
    std::map<int, std::vector<TraceRecord>> tree;
    g_TargetCPUType = GetGuestArchitecture(*g_pGlobalCursor);

    if (g_TargetCPUType == ProcessorArchitecture::Invalid || g_TargetCPUType == ProcessorArchitecture::Arm64 || g_TargetCPUType == ProcessorArchitecture::ARM32) {
        dprintf("ERROR: Unsupported or unknown CPU architecture.\n");
        return tree;
    }

    CComQIPtr<IDebugControl> control(client);

    if (!control) return tree;

    char tempPath[MAX_PATH];
    GetTempPathA(MAX_PATH, tempPath);

    std::string tempFile = std::format("{}timetrack_{}.bin", tempPath, GetTickCount());

    std::ofstream outFile(tempFile, std::ios::binary);
    if (!outFile.is_open()) {
        dprintf("Failed to create temporary file: %s\n", tempFile.c_str());
        return tree;
    }

    UniqueCursor inspectCursor(g_pReplayEngine->NewCursor());
    inspectCursor->SetPosition(g_pGlobalCursor->GetPosition());

    ZydisDecoder decoder;
    SetupZydisDecoder(&decoder, g_TargetCPUType);

    struct WorkItem {
        int parentId; // The ID of the TraceRecord that spawned this work item
        int id;
        ZydisOperandType type;
        ZydisRegister reg;
        uint64_t memAddr;
        uint32_t memSize;
        Position pos;
    };

    int idCounter = 0;

    WorkItem rootItem;
    rootItem.parentId = 0;
    rootItem.pos = inspectCursor->GetPosition();

    TraceRecord rootRecord = {};
    rootRecord.id = ++idCounter;
    rootItem.id = idCounter;

    rootRecord.parentId = 0;
    rootRecord.pos = inspectCursor->GetPosition();

    ZydisRegister TargetRegister = GetRegisterByName(targetStr.c_str());

    if (TargetRegister == ZYDIS_REGISTER_NONE) {
        rootItem.type = ZYDIS_OPERAND_TYPE_MEMORY;
        DEBUG_VALUE val;
        if (SUCCEEDED(control->Evaluate(targetStr.c_str(), DEBUG_VALUE_INT64, &val, NULL))) {
            rootItem.memAddr = val.I64;
            rootItem.memSize = size;
            if (rootItem.memSize == 0) rootItem.memSize = 8;
        }
        else {
            dprintf("Invalid argument.\n");
            outFile.close();
            DeleteFileA(tempFile.c_str());
            return tree;
        }
    }
    else {
        rootItem.type = ZYDIS_OPERAND_TYPE_REGISTER;
        rootItem.reg = TargetRegister;
        rootItem.memSize = _ZydisGetRegisterWidth(g_TargetCPUType, TargetRegister) / 8;
    }

    outFile.write((char*)&rootRecord, sizeof(TraceRecord));

    std::deque<WorkItem> queue;
    queue.push_back(rootItem);

    int steps = 0;


    while (!queue.empty() && steps < maxSteps) {
        WorkItem item = queue.front();
        queue.pop_front();
        steps++;

        inspectCursor->SetPosition(item.pos);

        Position foundPos = Position::Invalid;

        if (item.type == ZYDIS_OPERAND_TYPE_REGISTER) {
            foundPos = FindRegisterWrite(inspectCursor.get(), item.reg);
        }
        else {
            foundPos = FindMemoryWrite(inspectCursor.get(), item.memAddr, item.memSize);
        }

        if (foundPos == Position::Invalid) {
            continue;
        }

        TraceRecord record = {};
        record.parentId = item.id; // Connect to the node that requested this search
        record.pos = foundPos;

        int currentInstId = ++idCounter;
        record.id = currentInstId;


        // Disassemble

        GlobalContext ctx = GetGlobalContext(inspectCursor.get());

        char buffer[256];
        BufferView bufferView{ buffer, sizeof(buffer) };
        inspectCursor->QueryMemoryBuffer(inspectCursor->GetProgramCounter(), bufferView);

        ZydisDecodedInstruction instruction;
        ZydisDecodedOperand operands[ZYDIS_MAX_OPERAND_COUNT];

        if (ZYAN_FAILED(ZydisDecoderDecodeFull(&decoder, bufferView.BaseAddress, bufferView.Size, &instruction, operands))) {
            continue;
        }

        // Logic & Branching
        bool handled = false;

        auto AddItem = [&](ZydisDecodedOperand& op, Position pos) {
            WorkItem newItem;
            newItem.id = record.id;
            newItem.parentId = item.id; // The new item is a child of THIS found instruction
            newItem.pos = inspectCursor->GetPosition();

            if (op.type == ZYDIS_OPERAND_TYPE_MEMORY) {
                newItem.type = ZYDIS_OPERAND_TYPE_MEMORY;
                uint64_t base = op.mem.base == ZYDIS_REGISTER_NONE ? 0 : (uint64_t)GetRegisterValue(ctx, op.mem.base, false);
                uint64_t index = op.mem.index == ZYDIS_REGISTER_NONE ? 0 : (uint64_t)GetRegisterValue(ctx, op.mem.index, false);

                // Scale이 0인 경우 1로 취급 (보통 Zydis는 0이면 register_none이나 마찬가지지만 안전하게)
                uint64_t scale = (op.mem.scale == 0) ? 1 : op.mem.scale;

                newItem.memAddr = base + (index * scale) + op.mem.disp.value;
                newItem.memSize = op.size / 8;
                if (newItem.memSize == 0) newItem.memSize = 8; // Fallback
                queue.push_back(newItem);
            }
            else if (op.type == ZYDIS_OPERAND_TYPE_REGISTER) {
                newItem.type = ZYDIS_OPERAND_TYPE_REGISTER;
                newItem.reg = op.reg.value;
                newItem.memSize = _ZydisGetRegisterWidth(g_TargetCPUType, newItem.reg) / 8; // Fixed rootItem -> newItem
                queue.push_back(newItem);
            }
            else {
                return;
            }
            queue.push_back(newItem);
            };

        if (instruction.mnemonic == ZYDIS_MNEMONIC_LEA) {
            // LEA Dest, [Base + Index*Scale + Disp]
            // Source는 Base 레지스터와 Index 레지스터임.
            const ZydisDecodedOperand* memOp = &operands[1]; // 보통 두 번째 오퍼랜드가 소스

            if (memOp->type == ZYDIS_OPERAND_TYPE_MEMORY) {
                if (memOp->mem.base != ZYDIS_REGISTER_NONE) {
                    WorkItem baseItem = {};
                    baseItem.id = record.id;
                    baseItem.parentId = item.id;
                    baseItem.pos = inspectCursor->GetPosition();
                    baseItem.type = ZYDIS_OPERAND_TYPE_REGISTER;
                    baseItem.reg = memOp->mem.base;
                    baseItem.memSize = 8;
                    queue.push_back(baseItem);
                }
                if (memOp->mem.index != ZYDIS_REGISTER_NONE) {
                    WorkItem indexItem = {};
                    indexItem.id = record.id;
                    indexItem.parentId = item.id;
                    indexItem.pos = inspectCursor->GetPosition();
                    indexItem.type = ZYDIS_OPERAND_TYPE_REGISTER;
                    indexItem.reg = memOp->mem.index;
                    indexItem.memSize = 8;
                    queue.push_back(indexItem);
                }
            }
        }
        else {
            // 일반 명령어 처리 (MOV, ADD, SUB, POP, PUSH, XCHG 등 모두 포함)
            // '읽기(Read)' 속성이 있는 모든 오퍼랜드는 결과값에 영향을 주는 부모입니다.
            // ZydisDecoderDecodeFull은 Explicit(명시적) 오퍼랜드와 Implicit(암시적) 오퍼랜드를 모두 반환합니다.
            // 예: POP RAX -> Explicit: RAX(Write), Implicit: RSP(Read/Write), Implicit: [RSP](Read)

            for (int i = 0; i < instruction.operand_count; i++) {
                // 현재 찾고 있는 타겟이 이 오퍼랜드에 의해 '쓰기'된 것이 맞는지 검증할 수도 있지만,
                // 이미 FoundRegisterWrite/MemoryWrite로 위치를 찾았으므로,
                // 이 명령어의 모든 '입력(Read)' 오퍼랜드를 큐에 넣습니다.

                if (operands[i].actions & ZYDIS_OPERAND_ACTION_READ) {
                    // Flags 레지스터(RFLAGS/EFLAGS) 읽기는 데이터 흐름 추적에서 노이즈가 될 수 있으므로 제외하는 것이 좋습니다.
                    if (operands[i].type == ZYDIS_OPERAND_TYPE_REGISTER &&
                        (operands[i].reg.value == ZYDIS_REGISTER_RFLAGS || operands[i].reg.value == ZYDIS_REGISTER_EFLAGS)) {
                        continue;
                    }

                    AddItem(operands[i], foundPos);
                }
            }
        }

        outFile.write((char*)&record, sizeof(TraceRecord));
    }

    outFile.close();

    std::ifstream inFile(tempFile, std::ios::binary);
    if (!inFile.is_open()) {
        dprintf("Error reading trace file.\n");
        return tree;
    }

    TraceRecord rec;

    while (inFile.read((char*)&rec, sizeof(TraceRecord))) {
        tree[rec.parentId].push_back(rec);
    }

    inFile.close();

    DeleteFileA(tempFile.c_str());
    return tree;
}

HRESULT CALLBACK timetrack(IDebugClient* const pClient, const char* const pArgs) noexcept
try
{
    // 1. 인자 유효성 검사
    if (pArgs == nullptr || strlen(pArgs) == 0)
    {
        dprintf("Usage: !timetrack <target> <size> <Max Steps=50> <gui>\n");
        dprintf("Example: !timetrack @rbp+30 8 100\n");
        dprintf("Example: !timetrack 0x7ff7a000 4\n");
        return S_OK;
    }

    std::stringstream ss(pArgs);

    std::string targetStr;
    std::string sizeStr;
    std::string maxStepsStr;
    std::string guiStr;
    ss >> targetStr;

    unsigned int size = 0;
    unsigned int maxSteps = 50;
    bool showGui = false;

    if (ss >> sizeStr) {
        size = std::stoul(sizeStr, nullptr, 0);
    }

    if (ss >> maxStepsStr) {
        if (maxStepsStr == "gui") {
            showGui = true;
        }
        else {
            maxSteps = std::stoul(maxStepsStr, nullptr, 0);
        }
    }

    if (ss >> guiStr) {
        if (guiStr == "gui") {
            showGui = true;
        }
    }

    g_LastTraceTree = _TimeTrack(pClient, targetStr, size, maxSteps);

    if (showGui && track_gui) {

        PostMessage(track_gui->GetHWND(), WM_TTGUI_COMMAND, (WPARAM)13, (LPARAM)pClient);

        //DWORD tid = GetThreadId(TimeTrackGUI::GUIWnd::m_hThread);
        //if (tid != 0) PostThreadMessage(tid, WM_TTGUI_COMMAND, (WPARAM)13, (LPARAM)pClient);
        //else PostMessage(HWND_BROADCAST, WM_TTGUI_COMMAND, (WPARAM)13, (LPARAM)pClient);
    }
    else PrintRecordTreeIterative(pClient, g_LastTraceTree);

    return S_OK;
}
catch (const std::exception& e)
{
    dprintf("ERROR: %s\n", e.what());
    return E_FAIL;
}
catch (...)
{
    return E_UNEXPECTED;
}
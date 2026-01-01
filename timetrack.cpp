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
#include "TraceRecord.h"
#include "TimeTrackLogic.h"

extern IReplayEngineView* g_pReplayEngine;
extern ICursorView* g_pGlobalCursor;
extern ProcessorArchitecture g_TargetCPUType;

// ----------------------------------------------------------------------------
// Core Logic
// ----------------------------------------------------------------------------

// Find previous write to register
// Returns Position::Invalid if not found.
Position FindRegisterWrite(ICursor* cursor, ZydisRegister reg)
{
    struct __TargetReg {
        ZydisRegister reg;
        RegValue value;
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

std::map<int, std::vector<TraceRecord>> _TimeTrack(IDebugClient* client, const std::string& arg)
{
    std::map<int, std::vector<TraceRecord>> tree;
    g_TargetCPUType = GetGuestArchitecture(*g_pGlobalCursor);

    if (g_TargetCPUType == ProcessorArchitecture::Invalid || g_TargetCPUType == ProcessorArchitecture::Arm64 || g_TargetCPUType == ProcessorArchitecture::ARM32) {
        dprintf("ERROR: Unsupported or unknown CPU architecture.\n");
        return tree;
    }

	CComQIPtr<IDebugControl> control(client);

    if (!control) return tree;

    std::stringstream ss(arg);
    std::string targetStr;
    std::string sizeStr;
    std::string maxStepsStr;
    
    ss >> targetStr;
    if (!(ss >> sizeStr)) {
        sizeStr = "0"; // Default
    }
    if (!(ss >> maxStepsStr)) {
        maxStepsStr = "50"; // Default
    }
    
    int maxSteps = std::stoul(maxStepsStr, nullptr, 0);

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

    if (TargetRegister == ZYDIS_REGISTER_NONE){
        rootItem.type = ZYDIS_OPERAND_TYPE_MEMORY;
        DEBUG_VALUE val;
        if (SUCCEEDED(control->Evaluate(arg.c_str(), DEBUG_VALUE_INT64, &val, NULL))) {
            rootItem.memAddr = val.I64;
            rootItem.memSize = std::stoul(sizeStr, nullptr, 0);
            if (rootItem.memSize == 0) rootItem.memSize = 8;
        } else {
            dprintf("Invalid argument.\n");
            outFile.close();
            DeleteFileA(tempFile.c_str());
            return tree;
        }
    } else {
        rootItem.type = ZYDIS_OPERAND_TYPE_REGISTER;
        rootItem.reg = TargetRegister;
        rootItem.memSize = _ZydisGetRegisterWidth(g_TargetCPUType, TargetRegister) / 8;
    }

    outFile.write((char*)&rootRecord, sizeof(TraceRecord));

    std::deque<WorkItem> queue;
    queue.push_back(rootItem);

    int steps = 0;
    

    while(!queue.empty() && steps < maxSteps) {
        WorkItem item = queue.front();
        queue.pop_front();
        steps++;

		inspectCursor->SetPosition(item.pos);

        Position foundPos = Position::Invalid;

        if (item.type == ZYDIS_OPERAND_TYPE_REGISTER){
            foundPos = FindRegisterWrite(inspectCursor.get(), item.reg);
        } else {
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
                newItem.memAddr = base + (index * op.mem.scale) + op.mem.disp.value;
                newItem.memSize = op.size / 8;
                if (newItem.memSize == 0) newItem.memSize = 8;
            } else if (op.type == ZYDIS_OPERAND_TYPE_REGISTER) {
                newItem.type = ZYDIS_OPERAND_TYPE_REGISTER;
                newItem.reg = op.reg.value;
                rootItem.memSize = _ZydisGetRegisterWidth(g_TargetCPUType, TargetRegister) / 8;
            } else {
                return;
            }
            queue.push_back(newItem);
        };

        if (instruction.mnemonic == ZYDIS_MNEMONIC_PUSH) {
            AddItem(operands[0], foundPos);
            handled = true;
        }
        else if (instruction.mnemonic == ZYDIS_MNEMONIC_POP) {
            WorkItem newItem;
            newItem.parentId = currentInstId;
            newItem.pos = inspectCursor->GetPosition();
            newItem.type = ZYDIS_OPERAND_TYPE_MEMORY;
            newItem.memAddr = (uint64_t)inspectCursor->GetStackPointer();
            newItem.memSize = GetCPUBusSize();
            queue.push_back(newItem);
            handled = true;
        }
        else if (instruction.mnemonic == ZYDIS_MNEMONIC_MOV ||
                 instruction.mnemonic == ZYDIS_MNEMONIC_MOVZX ||
                 instruction.mnemonic == ZYDIS_MNEMONIC_MOVSX ||
                 instruction.mnemonic == ZYDIS_MNEMONIC_MOVAPS ||
                 instruction.mnemonic == ZYDIS_MNEMONIC_MOVUPS ||
                 instruction.mnemonic == ZYDIS_MNEMONIC_MOVDQA ||
                 instruction.mnemonic == ZYDIS_MNEMONIC_MOVDQU) {
            AddItem(operands[1], foundPos);
            handled = true;
        }
        else if (instruction.mnemonic == ZYDIS_MNEMONIC_ADD ||
                 instruction.mnemonic == ZYDIS_MNEMONIC_SUB ||
                 instruction.mnemonic == ZYDIS_MNEMONIC_AND ||
                 instruction.mnemonic == ZYDIS_MNEMONIC_OR ||
                 instruction.mnemonic == ZYDIS_MNEMONIC_XOR ||
                 instruction.mnemonic == ZYDIS_MNEMONIC_IMUL ||
                 instruction.mnemonic == ZYDIS_MNEMONIC_SHL ||
                 instruction.mnemonic == ZYDIS_MNEMONIC_SHR) {

            AddItem(operands[0], foundPos);

            if (instruction.operand_count >= 2)
                AddItem(operands[1], foundPos);

            handled = true;
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
    if (pArgs == nullptr || strlen(pArgs) == 0)
    {
        dprintf("Usage: !timetrack <register or memory(0x7ffff0000 or RBP+30h)> <size> <Max Steps=50>\n");
        return S_OK;
    }

    std::string arg = pArgs;

    auto tree = _TimeTrack(pClient, arg);

    PrintRecordTreeIterative(pClient, tree);

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
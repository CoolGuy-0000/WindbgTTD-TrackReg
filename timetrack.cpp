// DataFlowTracer.cpp
//
// A WinDbg Extension that traces the origin of a value backwards in time using TTD.

#include <Windows.h>
#include <assert.h>
#include <exception>
#include <stdexcept>
#include <vector>
#include <string>
#include <algorithm>
#include <format>
#include <iostream>
#include <sstream>
#include <deque>
#include <fstream>
#include <map>

#include <TTD/IReplayEngine.h>
#include <TTD/IReplayEngineStl.h>
#include <TTD/IReplayEngineRegisters.h>

#define KDEXT_64BIT
#include <DbgEng.h>
#include <WDBGEXTS.H>
#include <atlcomcli.h>

#include "disasm_helper.h"

#include <Zydis/Zydis.h>
#include <ZyCore/Zycore.h>

using namespace TTD;
using namespace Replay;

template < typename Interface >
inline Interface* QueryInterfaceByIoctl()
{
    WDBGEXTS_QUERY_INTERFACE wqi = {};
    wqi.Iid = &__uuidof(Interface);
    auto const ioctlSuccess = Ioctl(IG_QUERY_TARGET_INTERFACE, &wqi, sizeof(wqi));
    if (!ioctlSuccess || wqi.Iface == nullptr)
    {
        throw std::invalid_argument("Unable to get TTD interface.");
    }
    return static_cast<Interface*>(wqi.Iface);
}

// ----------------------------------------------------------------------------
// Core Logic
// ----------------------------------------------------------------------------

ULONG GetCurrentThreadId(IDebugClient* client)
{
    CComQIPtr<IDebugSystemObjects4> pSystemObjects(client);

    if (pSystemObjects){
        ULONG systemThreadId = 0;

        if (SUCCEEDED(pSystemObjects->GetCurrentThreadSystemId(&systemThreadId)))
        {
			return systemThreadId;
        }
    }
    return (ULONG)-1;
}

// Find previous write to register
// Returns Position::Invalid if not found.
static Position FindRegisterWrite(ULONG threadID, IReplayEngineView* engine, ZydisRegister reg, Position endPos)
{
    UniqueCursor cursor(engine->NewCursor());
    cursor->SetPosition(endPos);

    RegValue lastValue = {0};
    RegValue mask = {0};

    // Setup mask based on register width
    memset(&mask, 0xFF, sizeof(mask)); // Default all ones
    uint32_t bits = ZydisRegisterGetWidth(ZYDIS_MACHINE_MODE_LONG_64, reg);
    if (bits < 64) {
        mask.val[0] = (1ULL << bits) - 1;
        mask.val[1] = 0;
    } else if (bits == 64) {
        mask.val[1] = 0; // Clear high part
    }

    // Initial Value
    AMD64_CONTEXT context = cursor->GetCrossPlatformContext().operator CROSS_PLATFORM_CONTEXT().Amd64Context;
    try {
        lastValue = GetRegisterValue(context, reg);
    }
    catch (...) {
        return Position::Invalid;
    }

    // Step backwards and check
    for (int i = 0; i < 10000; i++){
        cursor->ReplayBackward((StepCount)1);

        // Check thread?
        // We are using single stepping, so we are still on the same thread effectively
        // unless a context switch happened?
        // TTD cursors track time, not threads explicitly during ReplayBackward(1).
        // But we want to ensure we are seeing the register change on THIS thread.
        // However, standard TTD replay usually follows execution flow.
        // We should check if thread ID matches just to be safe, but GetThreadInfo overhead?
        // Let's assume standard behavior: ReplayBackward moves global time.
        // If thread switched, the register values of *our* thread wouldn't change until it runs again.

        // Get Context for specific thread
        AMD64_CONTEXT x = cursor->GetCrossPlatformContext((ThreadId)threadID).operator CROSS_PLATFORM_CONTEXT().Amd64Context;

        RegValue val = {0};
        try { val = GetRegisterValue(x, reg); }
        catch (...) { continue; }

        bool changed = false;
        for(int j=0; j<4; ++j) {
            if ((val.val[j] & mask.val[j]) != (lastValue.val[j] & mask.val[j])) {
                changed = true;
                break;
            }
        }

        if (changed) {
            // Found change!
            // The instruction that CAUSED the change is executed at this position?
            // ReplayBackward(1) moves us to the state BEFORE the last instruction executed?
            // No, TTD ReplayBackward moves "back in time".
            // If we were at T=100 (Value=New), ReplayBackward(1) -> T=99.
            // If Value at T=99 is Old, then the instruction at T=99 CAUSED the change to New.
            // So the instruction is at the CURRENT cursor position.
            return cursor->GetPosition();
        }
    }

    return Position::Invalid;
}

// Find previous write to memory
static Position FindMemoryWrite(IReplayEngineView* engine, uint64_t address, uint64_t size, Position endPos)
{
    UniqueCursor cursor(engine->NewCursor());
    cursor->SetPosition(endPos);

    cursor->AddMemoryWatchpoint({ (GuestAddress)address, size, DataAccessMask::Write });
    cursor->SetEventMask(EventMask::MemoryWatchpoint);

    ICursorView::ReplayResult result = cursor->ReplayBackward();
    if (result.StopReason == EventType::MemoryWatchpoint)
    {
        return cursor->GetPosition() - 1; // Instruction that executed the write
    }

    return Position::Invalid;
}

// ----------------------------------------------------------------------------
// Main Logic
// ----------------------------------------------------------------------------

struct WorkItem {
    int id; // My ID
    int parentId; // Parent ID

    ZydisOperandType type;
    ZydisRegister reg;
    uint64_t memAddr;
    uint32_t memSize;
    Position startPos;
    int depth;
};

// Binary struct for file
struct TraceRecord {
    int id;
    int parentId;
    int depth;

    // Position info
    uint64_t seq;
    uint64_t steps;

    // Text Data (fixed size for simplicity)
    char symbol[256];
    char instruction[256];
    char description[256]; // e.g. "Origin: Zeroing"
    char linkCmd[128]; // e.g. !tt ...
};

void PrintTraceDFS(IDebugControl* pControl, const std::map<int, std::vector<TraceRecord>>& tree, int currentId, const std::string& indent) {
    auto it = tree.find(currentId);
    if (it == tree.end()) return;

    for (const auto& record : it->second) {
        std::string output;

        std::string currentIndent(record.depth * 2, ' ');

        output += std::format("{}[{}] ", currentIndent, record.depth);

        if (strlen(record.symbol) > 0) {
            output += std::format("{}{} ", record.symbol, record.linkCmd);
        } else {
            output += std::format("{} ", record.linkCmd);
        }

        output += "\t\t";
        output += record.instruction;

        if (strlen(record.description) > 0) {
            output += "\n";
            output += currentIndent;
            output += "\t\t";
            output += record.description;
        }

        output += "\n";

        pControl->ControlledOutput(DEBUG_OUTCTL_THIS_CLIENT | DEBUG_OUTCTL_DML, DEBUG_OUTPUT_NORMAL, output.c_str());

        // Recurse
        PrintTraceDFS(pControl, tree, record.id, "");
    }
}

void _TimeTrack(IDebugClient* client, const std::string& arg)
{
    auto pEngine = QueryInterfaceByIoctl<IReplayEngineView>();
    auto pCursor = QueryInterfaceByIoctl<ICursorView>();
	CComQIPtr<IDebugControl> pControl(client);
    CComQIPtr<IDebugSymbols3> pSymbols(client);
    
    if (!pControl || !pSymbols) return;

    ULONG threadID = GetCurrentThreadId(client);
    if (threadID == (ULONG)-1) return;

    std::stringstream ss(arg);
    std::string targetStr;
    std::string sizeStr;

    ss >> targetStr;
    if (!(ss >> sizeStr)) {
        sizeStr = "0"; // Default size
    }

    // Temporary File Setup
    char tempPath[MAX_PATH];
    GetTempPathA(MAX_PATH, tempPath);
    std::string tempFile = std::string(tempPath) + "timetrack_" + std::to_string(GetTickCount()) + ".bin";

    std::ofstream outFile(tempFile, std::ios::binary);
    if (!outFile.is_open()) {
        dprintf("Failed to create temporary file: %s\n", tempFile.c_str());
        return;
    }

    ZydisDecoder decoder;
    ZydisDecoderInit(&decoder, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_STACK_WIDTH_64);

    ZydisFormatter formatter;
    ZydisFormatterInit(&formatter, ZYDIS_FORMATTER_STYLE_INTEL);

    Position currentPos = pCursor->GetPosition((ThreadId)threadID);
    
    int idCounter = 0;

    // Root Item
    WorkItem rootItem;
    rootItem.id = ++idCounter;
    rootItem.parentId = 0; // 0 is virtual root
    rootItem.depth = 0;
    rootItem.startPos = currentPos;

    ZydisRegister TargetRegister = GetRegisterByName(targetStr.c_str());
    
    // Initial record for root (start point)
    TraceRecord rootRecord = {};
    rootRecord.id = rootItem.id;
    rootRecord.parentId = 0;
    rootRecord.depth = 0;
    rootRecord.seq = (uint64_t)currentPos.Sequence;
    rootRecord.steps = (uint64_t)currentPos.Steps;

    std::string startMsg;

    if (TargetRegister == ZYDIS_REGISTER_NONE){
        rootItem.type = ZYDIS_OPERAND_TYPE_MEMORY;
        DEBUG_VALUE val;
        if (SUCCEEDED(pControl->Evaluate(arg.c_str(), DEBUG_VALUE_INT64, &val, NULL))) {
            rootItem.memAddr = val.I64;
            rootItem.memSize = std::stoul(sizeStr, nullptr, 0);
            if (rootItem.memSize == 0) rootItem.memSize = 8;
            startMsg = std::format("Tracking origin of {:X}", rootItem.memAddr);
        } else {
            dprintf("Invalid argument.\n");
            outFile.close(); DeleteFileA(tempFile.c_str());
            return;
        }
    } else {
        rootItem.type = ZYDIS_OPERAND_TYPE_REGISTER;
        rootItem.reg = TargetRegister;
        rootItem.memSize = ZydisRegisterGetWidth(ZYDIS_MACHINE_MODE_LONG_64, TargetRegister) / 8;
        startMsg = std::format("Tracking origin of {}", targetStr);
    }

    strncpy_s(rootRecord.instruction, startMsg.c_str(), _TRUNCATE);
    std::string link = std::format("<exec cmd=\"!tt {:X}:{:X}\"><b>{:X}:{:X}</b></exec>", (uint64_t)currentPos.Sequence, (uint64_t)currentPos.Steps, (uint64_t)currentPos.Sequence, (uint64_t)currentPos.Steps);
    strncpy_s(rootRecord.linkCmd, link.c_str(), _TRUNCATE);

    // Write root record
    outFile.write((char*)&rootRecord, sizeof(TraceRecord));

    std::deque<WorkItem> queue;
    queue.push_back(rootItem);

    int steps = 0;
    const int maxSteps = 1000;

    while(!queue.empty() && steps < maxSteps) {
        WorkItem item = queue.front();
        queue.pop_front();
        steps++;

        if (item.depth > 20) continue;

        Position foundPos = Position::Invalid;

        if (item.type == ZYDIS_OPERAND_TYPE_REGISTER){
            foundPos = FindRegisterWrite(threadID, pEngine, item.reg, item.startPos);
        } else {
            foundPos = FindMemoryWrite(pEngine, item.memAddr, item.memSize, item.startPos);
        }

        TraceRecord record = {};
        record.parentId = item.id;
        record.depth = item.depth + 1;

        int currentInstId = ++idCounter;
        record.id = currentInstId;

        if (foundPos == Position::Invalid) {
            strncpy_s(record.description, "Origin not found (Trace start or limit reached).", _TRUNCATE);
            outFile.write((char*)&record, sizeof(TraceRecord));
            continue;
        }

        record.seq = (uint64_t)foundPos.Sequence;
        record.steps = (uint64_t)foundPos.Steps;

        // Disassemble
        UniqueCursor inspectCursor(pEngine->NewCursor());
        inspectCursor->SetPosition(foundPos);
        AMD64_CONTEXT ctx = inspectCursor->GetCrossPlatformContext((ThreadId)threadID).operator CROSS_PLATFORM_CONTEXT().Amd64Context;

        char buffer[256];
        BufferView bufferView{ buffer, sizeof(buffer) };
        pCursor->QueryMemoryBuffer((GuestAddress)ctx.Rip, bufferView);

        ZydisDecodedInstruction instruction;
        ZydisDecodedOperand operands[ZYDIS_MAX_OPERAND_COUNT];

        if (ZYAN_FAILED(ZydisDecoderDecodeFull(&decoder, bufferView.BaseAddress, bufferView.Size, &instruction, operands))) {
            strncpy_s(record.description, "Cannot Disassemble.", _TRUNCATE);
            outFile.write((char*)&record, sizeof(TraceRecord));
            continue;
        }

        // Symbol
        char symBuf[256];
        ULONG64 displacement = 0;
        if (SUCCEEDED(pSymbols->GetNameByOffset(ctx.Rip, symBuf, sizeof(symBuf), nullptr, &displacement))) {
             std::string sym = std::format("{}+0x{:X}", symBuf, displacement);
             strncpy_s(record.symbol, sym.c_str(), _TRUNCATE);
        }

        std::string linkCmd = std::format("<exec cmd=\"!tt {:X}:{:X}\"><b>{:X}:{:X}</b></exec>",
            (uint64_t)foundPos.Sequence, (uint64_t)foundPos.Steps, (uint64_t)foundPos.Sequence, (uint64_t)foundPos.Steps);
        strncpy_s(record.linkCmd, linkCmd.c_str(), _TRUNCATE);

        char instrTxt[256];
        ZydisFormatterFormatInstruction(&formatter, &instruction, operands, instruction.operand_count_visible, instrTxt, sizeof(instrTxt), ctx.Rip, ZYAN_NULL);
        strncpy_s(record.instruction, instrTxt, _TRUNCATE);

        // Logic & Branching
        bool handled = false;

        auto AddItem = [&](ZydisDecodedOperand& op, Position pos, int depth) {
            WorkItem newItem;
            newItem.parentId = currentInstId;
            newItem.depth = depth;
            newItem.startPos = pos;

            if (op.type == ZYDIS_OPERAND_TYPE_MEMORY) {
                newItem.type = ZYDIS_OPERAND_TYPE_MEMORY;
                uint64_t base = op.mem.base == ZYDIS_REGISTER_NONE ? 0 : GetRegisterValue(ctx, op.mem.base).val[0];
                uint64_t index = op.mem.index == ZYDIS_REGISTER_NONE ? 0 : GetRegisterValue(ctx, op.mem.index).val[0];
                newItem.memAddr = base + (index * op.mem.scale) + op.mem.disp.value;
                newItem.memSize = op.size / 8;
                if (newItem.memSize == 0) newItem.memSize = 8;
            } else if (op.type == ZYDIS_OPERAND_TYPE_REGISTER) {
                newItem.type = ZYDIS_OPERAND_TYPE_REGISTER;
                newItem.reg = op.reg.value;
            } else {
                return;
            }
            queue.push_back(newItem);
        };

        if (instruction.mnemonic == ZYDIS_MNEMONIC_SYSCALL) {
            strncpy_s(record.description, "Origin: System Call.", _TRUNCATE);
            handled = true;
        }
        else if (instruction.mnemonic == ZYDIS_MNEMONIC_LEA) {
            strncpy_s(record.description, "Origin: Address Calculation (LEA).", _TRUNCATE);
            handled = true;
        }
        else if (instruction.mnemonic == ZYDIS_MNEMONIC_XOR &&
                 operands[0].type == ZYDIS_OPERAND_TYPE_REGISTER &&
                 operands[1].type == ZYDIS_OPERAND_TYPE_REGISTER &&
                 operands[0].reg.value == operands[1].reg.value) {
            strncpy_s(record.description, "Origin: Zeroing idiom.", _TRUNCATE);
            handled = true;
        }
        else if (instruction.mnemonic == ZYDIS_MNEMONIC_PUSH) {
            AddItem(operands[0], foundPos, item.depth + 1);
            handled = true;
        }
        else if (instruction.mnemonic == ZYDIS_MNEMONIC_POP) {
            WorkItem newItem;
            newItem.parentId = currentInstId;
            newItem.depth = item.depth + 1;
            newItem.startPos = foundPos;
            newItem.type = ZYDIS_OPERAND_TYPE_MEMORY;
            newItem.memAddr = ctx.Rsp;
            newItem.memSize = 8;
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
            AddItem(operands[1], foundPos, item.depth + 1);
            handled = true;
        }
        else if (instruction.mnemonic == ZYDIS_MNEMONIC_ADD ||
                 instruction.mnemonic == ZYDIS_MNEMONIC_SUB ||
                 instruction.mnemonic == ZYDIS_MNEMONIC_AND ||
                 instruction.mnemonic == ZYDIS_MNEMONIC_OR ||
                 instruction.mnemonic == ZYDIS_MNEMONIC_XOR ||
                 instruction.mnemonic == ZYDIS_MNEMONIC_IMUL) {
            if (instruction.operand_count >= 2)
                AddItem(operands[1], foundPos, item.depth + 1);

            AddItem(operands[0], foundPos, item.depth + 1);
            handled = true;
        }

        if (!handled) {
            strncpy_s(record.description, "Stopping: Unhandled or terminal instruction.", _TRUNCATE);
        }

        // Write the record for this instruction
        outFile.write((char*)&record, sizeof(TraceRecord));
    }

    outFile.close();

    // ------------------------------------------------------------------------
    // Post-Processing: Read File and Print Tree
    // ------------------------------------------------------------------------

    std::ifstream inFile(tempFile, std::ios::binary);
    if (!inFile.is_open()) {
        dprintf("Error reading trace file.\n");
        return;
    }

    std::map<int, std::vector<TraceRecord>> tree;
    TraceRecord rec;

    while (inFile.read((char*)&rec, sizeof(TraceRecord))) {
        tree[rec.parentId].push_back(rec);
    }
    inFile.close();

    // Print Tree via DFS from Virtual Root (0)
    PrintTraceDFS(pControl, tree, 0, "");

    DeleteFileA(tempFile.c_str());
}

HRESULT CALLBACK timetrack(IDebugClient* const pClient, const char* const pArgs) noexcept
try
{
    if (pArgs == nullptr || strlen(pArgs) == 0)
    {
        dprintf("Usage: !timetrack <register or memory(0x7ffff0000 or RBP+30h)> <size>\n");
        return S_OK;
    }

    std::string arg = pArgs;

    _TimeTrack(pClient, arg);

    return S_OK;
}
catch (const std::exception& e)
{
    dprintf("Error: %s\n", e.what());
    return E_FAIL;
}
catch (...)
{
    return E_UNEXPECTED;
}
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

    struct RegSearchCtx {
        ZydisRegister targetReg;
        RegValue lastValue;
        RegValue mask; // For masking bits if needed (GPR partials)
        bool initialized;
        ThreadInfo thread_info;
        bool found;
        Position foundPos;
    } ctx;

    ctx.targetReg = reg;

    // Setup mask based on register width
    memset(&ctx.mask, 0xFF, sizeof(ctx.mask)); // Default all ones
    uint32_t bits = ZydisRegisterGetWidth(ZYDIS_MACHINE_MODE_LONG_64, reg);
    if (bits < 64) {
        ctx.mask.val[0] = (1ULL << bits) - 1;
        ctx.mask.val[1] = 0;
    } else if (bits == 64) {
        ctx.mask.val[1] = 0; // Clear high part
    }
    // For 128 bit, default is fine (all ones).

    ctx.initialized = false;
	ctx.foundPos = Position::Invalid;
    ctx.found = false;

    ctx.thread_info = cursor->GetThreadInfo((ThreadId)threadID);

    AMD64_CONTEXT context = cursor->GetCrossPlatformContext().operator CROSS_PLATFORM_CONTEXT().Amd64Context;

    try {
        ctx.lastValue = GetRegisterValue(context, reg);
        ctx.initialized = true;
    }
    catch (...) {
        return Position::Invalid;
    }

    auto callback = [](uintptr_t c, ICursorView::MemoryWatchpointResult const&, IThreadView const* t) -> bool {
        RegSearchCtx* p = (RegSearchCtx*)c;

		if (p->thread_info != t->GetThreadInfo()) {
            return false;
        }

		AMD64_CONTEXT x = t->GetCrossPlatformContext().operator CROSS_PLATFORM_CONTEXT().Amd64Context;
        RegValue val = {0};
        try { val = GetRegisterValue(x, p->targetReg); }
        catch (...) { return false; }

        // Compare using mask
        bool changed = false;
        for(int i=0; i<4; ++i) {
            if ((val.val[i] & p->mask.val[i]) != (p->lastValue.val[i] & p->mask.val[i])) {
                changed = true;
                break;
            }
        }

        if (changed && !p->found) {
            p->found = true;
            p->foundPos = t->GetPosition();
            return true;
        }
        return false;
    };

    cursor->AddMemoryWatchpoint({ GuestAddress::Null, UINT64_MAX, DataAccessMask::Execute });
    cursor->SetMemoryWatchpointCallback(callback, (uintptr_t)&ctx);
    cursor->SetEventMask(EventMask::MemoryWatchpoint);

    for (int i = 0; i < 10000 && !ctx.found; i++){
        cursor->ReplayBackward((StepCount)1);
    }

    return ctx.foundPos;
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
        return cursor->GetPosition() - 1;
    }

    return Position::Invalid;
}

// ----------------------------------------------------------------------------
// Main Logic
// ----------------------------------------------------------------------------

struct WorkItem {
    ZydisOperandType type;
    ZydisRegister reg;
    uint64_t memAddr;
    uint32_t memSize;
    Position startPos;
    int depth;
    std::string pathDesc; // description of how we got here
};

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

    std::string outputBuffer;
    outputBuffer.reserve(4096);
    const size_t FLUSH_THRESHOLD = 512;

    ZydisDecoder decoder;
    ZydisDecoderInit(&decoder, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_STACK_WIDTH_64);

    ZydisFormatter formatter;
    ZydisFormatterInit(&formatter, ZYDIS_FORMATTER_STYLE_INTEL);

    Position currentPos = pCursor->GetPosition((ThreadId)threadID);
    
    // Initial Work Item
    WorkItem rootItem;
    rootItem.depth = 0;
    rootItem.startPos = currentPos;
    rootItem.pathDesc = "";

    ZydisRegister TargetRegister = GetRegisterByName(targetStr.c_str());
    
    if (TargetRegister == ZYDIS_REGISTER_NONE){
        rootItem.type = ZYDIS_OPERAND_TYPE_MEMORY;
        DEBUG_VALUE val;
        if (SUCCEEDED(pControl->Evaluate(arg.c_str(), DEBUG_VALUE_INT64, &val, NULL))) {
            rootItem.memAddr = val.I64;
            rootItem.memSize = std::stoul(sizeStr, nullptr, 0);
            if (rootItem.memSize == 0) rootItem.memSize = 8; // Default 8 bytes if missing

             outputBuffer += std::format("Tracking origin of {:X} starting at: <exec cmd=\"!tt {:X}:{:X}\"><b>{:X}:{:X}</b></exec>\n",
                                rootItem.memAddr, (uint64_t)currentPos.Sequence, (uint64_t)currentPos.Steps, (uint64_t)currentPos.Sequence, (uint64_t)currentPos.Steps);
        } else {
            dprintf("Invalid argument.\n");
            return;
        }
    } else {
        rootItem.type = ZYDIS_OPERAND_TYPE_REGISTER;
        rootItem.reg = TargetRegister;
        rootItem.memSize = ZydisRegisterGetWidth(ZYDIS_MACHINE_MODE_LONG_64, TargetRegister) / 8;
        outputBuffer += std::format("Tracking origin of {} starting at: <exec cmd=\"!tt {:X}:{:X}\"><b>{:X}:{:X}</b></exec>\n",
            targetStr, (uint64_t)currentPos.Sequence, (uint64_t)currentPos.Steps, (uint64_t)currentPos.Sequence, (uint64_t)currentPos.Steps);
    }

    std::deque<WorkItem> queue;
    queue.push_back(rootItem);

    int steps = 0;
    const int maxSteps = 1000; // Total nodes to visit to prevent infinite loops

    while(!queue.empty() && steps < maxSteps) {
        WorkItem item = queue.front();
        queue.pop_front();
        steps++;

        if (item.depth > 20) continue; // Depth limit

        // Flush output
        if (outputBuffer.size() > FLUSH_THRESHOLD) {
            pControl->ControlledOutput(DEBUG_OUTCTL_THIS_CLIENT | DEBUG_OUTCTL_DML, DEBUG_OUTPUT_NORMAL, outputBuffer.c_str());
            outputBuffer.clear();
        }

        // Indentation
        std::string indent(item.depth * 2, ' ');
        outputBuffer += std::format("{}[{}] ", indent, item.depth);

        Position foundPos = Position::Invalid;

        if (item.type == ZYDIS_OPERAND_TYPE_REGISTER){
            foundPos = FindRegisterWrite(threadID, pEngine, item.reg, item.startPos);
        } else {
            foundPos = FindMemoryWrite(pEngine, item.memAddr, item.memSize, item.startPos);
        }

        if (foundPos == Position::Invalid) {
            outputBuffer += "Origin not found (Trace start or limit reached).\n";
            continue;
        }

        // Disassemble at foundPos
        UniqueCursor inspectCursor(pEngine->NewCursor());
        inspectCursor->SetPosition(foundPos);
        AMD64_CONTEXT ctx = inspectCursor->GetCrossPlatformContext((ThreadId)threadID).operator CROSS_PLATFORM_CONTEXT().Amd64Context;

        char buffer[256];
        BufferView bufferView{ buffer, sizeof(buffer) };
        pCursor->QueryMemoryBuffer((GuestAddress)ctx.Rip, bufferView);

        ZydisDecodedInstruction instruction;
        ZydisDecodedOperand operands[ZYDIS_MAX_OPERAND_COUNT];

        if (ZYAN_FAILED(ZydisDecoderDecodeFull(&decoder, bufferView.BaseAddress, bufferView.Size, &instruction, operands))) {
            outputBuffer += "Cannot Disassemble.\n";
            continue;
        }

        // Symbol resolution
        char symBuf[256];
        ULONG64 displacement = 0;
        if (SUCCEEDED(pSymbols->GetNameByOffset(ctx.Rip, symBuf, sizeof(symBuf), nullptr, &displacement))) {
            outputBuffer += std::format(
                "{}+0x{:X} <exec cmd=\"!tt {:X}:{:X}\"><b>{:X}:{:X}</b></exec>\n",
                symBuf, displacement, (uint64_t)foundPos.Sequence, (uint64_t)foundPos.Steps, (uint64_t)foundPos.Sequence, (uint64_t)foundPos.Steps
            );
        } else {
            outputBuffer += std::format(
                "<exec cmd=\"!tt {:X}:{:X}\"><b>{:X}:{:X}</b></exec>\n",
                (uint64_t)foundPos.Sequence, (uint64_t)foundPos.Steps, (uint64_t)foundPos.Sequence, (uint64_t)foundPos.Steps
            );
        }

        // Format instruction
        char instrTxt[256];
        ZydisFormatterFormatInstruction(&formatter, &instruction, operands, instruction.operand_count_visible, instrTxt, sizeof(instrTxt), ctx.Rip, ZYAN_NULL);
        outputBuffer += indent;
        outputBuffer += "\t\t";
        outputBuffer += instrTxt;
        outputBuffer += "\n";

        // Analysis
        // We need to determine what to track next.
        // Branching logic:
        // Binary Ops (ADD, SUB, XOR, etc) -> Track Src AND Dest(previous)

        bool handled = false;

        // Helper to add new item
        auto AddItem = [&](ZydisOperand& op, Position pos, int depth) {
            WorkItem newItem;
            newItem.depth = depth;
            newItem.startPos = pos;
            if (op.type == ZYDIS_OPERAND_TYPE_MEMORY) {
                newItem.type = ZYDIS_OPERAND_TYPE_MEMORY;
                // Calculate Effective Address
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

        // Helper to check if we are tracking the destination of this instruction
        // Usually operand[0] is dest.
        bool isTrackingDest = false;
        if (instruction.operand_count > 0) {
             if (item.type == ZYDIS_OPERAND_TYPE_REGISTER && operands[0].type == ZYDIS_OPERAND_TYPE_REGISTER) {
                 // Check if register matches (or aliases)
                 // Simplification: Direct match or same class
                 // We rely on simple equality for now.
                 // Note: Zydis register enums are distinct (EAX != RAX). We should probably normalize or check overlap.
                 // For now, strict match or assume the tracker found the write to THIS register so it MUST be the dest.
                 isTrackingDest = true;
             }
             else if (item.type == ZYDIS_OPERAND_TYPE_MEMORY && operands[0].type == ZYDIS_OPERAND_TYPE_MEMORY) {
                 // Address match checked by FindMemoryWrite?
                 // FindMemoryWrite guarantees we stopped on a write to that address.
                 isTrackingDest = true;
             }
        }

        if (instruction.mnemonic == ZYDIS_MNEMONIC_SYSCALL) {
            outputBuffer += indent + "\t\tOrigin: System Call.\n";
            handled = true;
        }
        else if (instruction.mnemonic == ZYDIS_MNEMONIC_LEA) {
            outputBuffer += indent + "\t\tOrigin: Address Calculation (LEA).\n";
            // LEA Dest, [Src]
            // Actually LEA is just a calc. We could track the registers involved in the address calc?
            // "Origin found" implies we found the calculation.
            // If user wants to dig deeper, we could track the base/index registers of the source memory operand.
            // Let's stop for now as it's a "calculation".
            handled = true;
        }
        else if (instruction.mnemonic == ZYDIS_MNEMONIC_XOR &&
                 operands[0].type == ZYDIS_OPERAND_TYPE_REGISTER &&
                 operands[1].type == ZYDIS_OPERAND_TYPE_REGISTER &&
                 operands[0].reg.value == operands[1].reg.value) {
            outputBuffer += indent + "\t\tOrigin: Zeroing idiom.\n";
            handled = true;
        }
        else if (instruction.mnemonic == ZYDIS_MNEMONIC_PUSH) {
            // PUSH Src -> Writes to [RSP-8].
            // If we are tracking [RSP-8], origin is Src.
            AddItem(operands[0], foundPos, item.depth + 1);
            handled = true;
        }
        else if (instruction.mnemonic == ZYDIS_MNEMONIC_POP) {
            // POP Dest -> Reads from [RSP].
            // Track [RSP]
            WorkItem newItem;
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
            // MOV Dest, Src
            AddItem(operands[1], foundPos, item.depth + 1);
            handled = true;
        }
        else if (instruction.mnemonic == ZYDIS_MNEMONIC_ADD ||
                 instruction.mnemonic == ZYDIS_MNEMONIC_SUB ||
                 instruction.mnemonic == ZYDIS_MNEMONIC_AND ||
                 instruction.mnemonic == ZYDIS_MNEMONIC_OR ||
                 instruction.mnemonic == ZYDIS_MNEMONIC_XOR ||
                 instruction.mnemonic == ZYDIS_MNEMONIC_IMUL) {
            // ADD Dest, Src
            // Dest = Dest + Src
            // Track Dest (previous value) AND Src

            // Track Src
            if (instruction.operand_count >= 2)
                AddItem(operands[1], foundPos, item.depth + 1);

            // Track Dest (previous value)
            // We need to find where Dest was written BEFORE this instruction.
            // So we add a work item for Dest starting at foundPos.
            // BUT: FindRegisterWrite will look backwards from startPos.
            // So if we pass foundPos, it will find the previous write.

            AddItem(operands[0], foundPos, item.depth + 1);

            handled = true;
        }

        if (!handled) {
            outputBuffer += indent + "\t\tStopping: Unhandled or terminal instruction.\n";
        }
    }

    if (outputBuffer.size() != 0) {
        pControl->ControlledOutput(
            DEBUG_OUTCTL_THIS_CLIENT | DEBUG_OUTCTL_DML,
            DEBUG_OUTPUT_NORMAL,
            outputBuffer.c_str()
        );
    }
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
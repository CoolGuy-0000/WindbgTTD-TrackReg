// DataFlowTracer.cpp
//
// A WinDbg Extension that traces the origin of a value backwards in time using TTD.
//
// Example usage:
// !timetrack rax
// -> Traces RAX backwards.
// -> If RAX was loaded from [RCX+8], traces [RCX+8] backwards.
// -> If [RCX+8] was written by RDX, traces RDX backwards.
// -> Until a calculation, constant, or syscall is found.

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

    // Scan backwards.
    // We reuse the logic from previous task: Set Watchpoint on Execute, check for value change.
    // Optimization: Maybe we check every step? Yes.

    // Define context for callback
    struct RegSearchCtx {
        ZydisRegister targetReg;
        uint64_t lastValue;
        uint64_t mask;
        bool initialized;
        ThreadInfo thread_info;
        bool found;
        Position foundPos;
    } ctx;

    ctx.targetReg = reg;

    uint32_t bits = ZydisRegisterGetWidth(ZYDIS_MACHINE_MODE_LONG_64, reg);
    ctx.mask = (bits >= 64) ? UINT64_MAX : ((1ULL << bits) - 1);

    ctx.initialized = false;
	ctx.foundPos = Position::Invalid;
    ctx.found = false;

    ctx.thread_info = cursor->GetThreadInfo((ThreadId)threadID);

    // We need to capture the initial value (at endPos) so we know when it changes.
    // The cursor is at endPos.
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
        uint64_t val = 0;
        try { val = GetRegisterValue(x, p->targetReg); }
        catch (...) { return false; }

        if ((val & p->mask) != (p->lastValue & p->mask) && !p->found) {
            p->found = true;
            p->foundPos = t->GetPosition();
            return true;
        }
        return false;
    };

    cursor->AddMemoryWatchpoint({ GuestAddress::Null, UINT64_MAX, DataAccessMask::Execute });
    cursor->SetMemoryWatchpointCallback(callback, (uintptr_t)&ctx);
    cursor->SetEventMask(EventMask::MemoryWatchpoint);

    for (int i = 0; i < 10000 && !ctx.found; i++){ // Limit to 10k steps to avoid infinite
        cursor->ReplayBackward((StepCount)1);
    }

    return ctx.foundPos;
}

// Find previous write to memory
static Position FindMemoryWrite(IReplayEngineView* engine, uint64_t address, uint64_t size, Position endPos)
{
    UniqueCursor cursor(engine->NewCursor());
    cursor->SetPosition(endPos);

    // TTD has direct support for memory watchpoints!
    // We want to find a WRITE to this address.

    // Wait, `ReplayBackward` will hit the last write.
    // We set a watchpoint on the address.
    cursor->AddMemoryWatchpoint({ (GuestAddress)address, size, DataAccessMask::Write });
    cursor->SetEventMask(EventMask::MemoryWatchpoint);

    // We don't need a manual callback to check values, the engine stops on write.
    // But we need to distinguish between multiple hits if we want specific one?
    // Usually the immediate backward replay hits the last write.

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

void _TimeTrack(IDebugClient* client, const std::string& arg)
{
    auto pEngine = QueryInterfaceByIoctl<IReplayEngineView>();
    auto pCursor = QueryInterfaceByIoctl<ICursorView>();
	CComQIPtr<IDebugControl> pControl(client);
    CComQIPtr<IDebugSymbols3> pSymbols(client);
    
    if (!pControl || !pSymbols) {
        dprintf("Failed to get interface.\n");
        return;
    }

    ULONG threadID = GetCurrentThreadId(client);
    if (threadID == (ULONG)-1)
    {
        dprintf("Failed to get current thread ID.\n");
        return;
    }

    std::stringstream ss(arg);
    std::string targetStr;
    std::string sizeStr;

    ss >> targetStr;
    ss >> sizeStr;

    std::string outputBuffer;
    outputBuffer.reserve(4096);

    const size_t FLUSH_THRESHOLD = 512;

    ZydisDecoder decoder;
    ZydisDecoderInit(&decoder, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_STACK_WIDTH_64);

    ZydisFormatter formatter;
    ZydisFormatterInit(&formatter, ZYDIS_FORMATTER_STYLE_INTEL);

    Position currentPos = pCursor->GetPosition((ThreadId)threadID);
    
    ZydisRegister TargetRegister = GetRegisterByName(targetStr.c_str());
    uint64_t TargetAddress = NULL;
    uint32_t TargetSize = NULL;

    ZydisOperandType currentType = ZYDIS_OPERAND_TYPE_REGISTER; // Start assuming register
    
    // Check if input is likely memory address (hex)
    if (TargetRegister == ZYDIS_REGISTER_NONE){
        currentType = ZYDIS_OPERAND_TYPE_MEMORY;

        DEBUG_VALUE val;

        if (SUCCEEDED(pControl->Evaluate(arg.c_str(), DEBUG_VALUE_INT64, &val, NULL))) {
            TargetAddress = val.I64;
            TargetSize = std::stoul(sizeStr, nullptr, 0);

            outputBuffer += std::format("Tracking origin of {:X} starting at: <exec cmd=\"!tt {:X}:{:X}\"><b>{:X}:{:X}</b></exec>\n",
                                TargetAddress, (uint64_t)currentPos.Sequence, (uint64_t)currentPos.Steps, (uint64_t)currentPos.Sequence, (uint64_t)currentPos.Steps);
        }
        else {
            dprintf("Invalid argument: '%s' is neither a register nor a valid address.\n", arg.c_str());
            return;
        }
    }
    else {
        TargetSize = ZydisRegisterGetWidth(ZYDIS_MACHINE_MODE_LONG_64, TargetRegister) / 8;

        outputBuffer += std::format("Tracking origin of {} starting at: <exec cmd=\"!tt {:X}:{:X}\"><b>{:X}:{:X}</b></exec>\n",
            targetStr, (uint64_t)currentPos.Sequence, (uint64_t)currentPos.Steps, (uint64_t)currentPos.Sequence, (uint64_t)currentPos.Steps);
    }

    char buffer[64], buffer2[256];

    int depth = 0;
    const int maxDepth = 100;

    while (depth < maxDepth)
    {
        depth++;

        if (outputBuffer.size() > FLUSH_THRESHOLD) {
            pControl->ControlledOutput(
                DEBUG_OUTCTL_THIS_CLIENT | DEBUG_OUTCTL_DML,
                DEBUG_OUTPUT_NORMAL,
                outputBuffer.c_str()
            );
            outputBuffer.clear();
        }

        outputBuffer += std::format("[{}] ", depth);
 
        Position foundPos = Position::Invalid;

        if (currentType == ZYDIS_OPERAND_TYPE_REGISTER){
            foundPos = FindRegisterWrite(threadID, pEngine, TargetRegister, currentPos);
        }
        else if(currentType == ZYDIS_OPERAND_TYPE_MEMORY){
            foundPos = FindMemoryWrite(pEngine, TargetAddress, TargetSize, currentPos);
        }
        
        if (foundPos == Position::Invalid)
        {
            outputBuffer += "Origin not found.\n";
            break;
        }

        // Move to found position to analyze
        currentPos = foundPos;

        // Disassemble
        // We need the IP at this position.
        UniqueCursor inspectCursor(pEngine->NewCursor());
        inspectCursor->SetPosition(foundPos);
        AMD64_CONTEXT ctx = inspectCursor->GetCrossPlatformContext((ThreadId)threadID).operator CROSS_PLATFORM_CONTEXT().Amd64Context;

        BufferView bufferView{ buffer, sizeof(buffer) };
        pCursor->QueryMemoryBuffer((GuestAddress)ctx.Rip, bufferView);

        ZydisDecodedInstruction instruction;
        ZydisDecodedOperand operands[ZYDIS_MAX_OPERAND_COUNT];

        if (ZYAN_FAILED(ZydisDecoderDecodeFull(&decoder, bufferView.BaseAddress, bufferView.Size, &instruction, operands))) {
            outputBuffer += "Cannot Disassemble.\n";
            break;
        }

        ULONG64 displacement = 0;
        if (SUCCEEDED(pSymbols->GetNameByOffset(ctx.Rip, buffer2, sizeof(buffer2), nullptr, &displacement))) {
            outputBuffer += std::format(
                "{}+0x{:X} <exec cmd=\"!tt {:X}:{:X}\"><b>{:X}:{:X}</b></exec>\n",
                buffer2, displacement, (uint64_t)foundPos.Sequence, (uint64_t)foundPos.Steps, (uint64_t)foundPos.Sequence, (uint64_t)foundPos.Steps
            );
        }
        else {
            outputBuffer += std::format(
                "<exec cmd=\"!tt {:X}:{:X}\"><b>{:X}:{:X}</b></exec>\n",
                (uint64_t)foundPos.Sequence, (uint64_t)foundPos.Steps, (uint64_t)foundPos.Sequence, (uint64_t)foundPos.Steps
            );
		}

        ZydisFormatterFormatInstruction(&formatter, &instruction, operands, instruction.operand_count_visible, buffer, sizeof(buffer), ctx.Rip, ZYAN_NULL);
        outputBuffer += "\t\t";
        outputBuffer += buffer;
        outputBuffer += "\n";

        // Parse Instruction to determine next step
        // Logic:
        // 1. MOV Reg, [Mem] -> Track Mem
        // 2. MOV Reg, Reg2  -> Track Reg2
        // 3. MOV [Mem], Reg -> Track Reg (If we were tracking Mem)
        // 4. POP Reg        -> Track [RSP] (Stack)
        // 5. LEA Reg, [Mem] -> STOP (Address origin)
        // 6. ADD/SUB...     -> STOP (Calculation)
        // 7. SYSCALL        -> STOP

        int8_t op_idx = -1;

        if (instruction.mnemonic == ZYDIS_MNEMONIC_SYSCALL){
            outputBuffer += "\t\t";
            outputBuffer += "Origin: System Call.\n";
            break;
        }

        if (instruction.mnemonic == ZYDIS_MNEMONIC_LEA){
            outputBuffer += "\t\t";
            outputBuffer += "Origin: LEA\n";
            break;
        }

        // Data Movement

        if (instruction.mnemonic == ZYDIS_MNEMONIC_PUSH) {
            op_idx = 0;
        }
        else if (instruction.mnemonic == ZYDIS_MNEMONIC_MOV || instruction.mnemonic == ZYDIS_MNEMONIC_MOVZX || instruction.mnemonic == ZYDIS_MNEMONIC_MOVSX){
            op_idx = 1;
        }

        if (instruction.mnemonic == ZYDIS_MNEMONIC_POP)
        {
            // POP Reg -> Value comes from [RSP].
            // And RSP increases.
            // But at the *start* of instruction (foundPos), RSP points to the value.

            currentType = ZYDIS_OPERAND_TYPE_MEMORY;
            TargetAddress = ctx.Rsp;
            continue;
        }

        if (op_idx >= 0) {
            if (operands[op_idx].type == ZYDIS_OPERAND_TYPE_MEMORY) {
                currentType = ZYDIS_OPERAND_TYPE_MEMORY;
                TargetAddress = GetRegisterValue(ctx, operands[op_idx].mem.base, false) + (GetRegisterValue(ctx, operands[op_idx].mem.index, false) * operands[op_idx].mem.scale) + operands[op_idx].mem.disp.value;
            }
            else if (operands[op_idx].type == ZYDIS_OPERAND_TYPE_REGISTER) {
                currentType = ZYDIS_OPERAND_TYPE_REGISTER;
                TargetRegister = operands[op_idx].reg.value;
            }
            else currentType = ZYDIS_OPERAND_TYPE_UNUSED;

            continue;
        }

        outputBuffer += "\t\t";
        outputBuffer += "Stopping: Unhandled or terminal instruction.\n";
        break;
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
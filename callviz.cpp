
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

#include "GUI/TimeTrackVisualizerWnd.h"

extern IReplayEngineView* g_pReplayEngine;
extern ICursorView* g_pGlobalCursor;
extern ProcessorArchitecture g_TargetCPUType;

// Structure to hold call stack info
struct CallRecord {
    Position pos;
    uint64_t targetIP;
    std::string symbolName;
};

// This function will replay backward to find the call stack.
// In TTD, finding the call stack usually means just walking the stack if frames are present,
// but for "Visualizing the history", we might want to see the sequence of calls that *actually executed*
// to reach this point.
// Since TTD has the trace, we can "ReplayBackward" and catch "Call" events?
// Or "CallReturn" events?
//
// If we want the "Chain of calls that led here":
// We can use the registered CallReturnCallback to track when a function *returns*? No, we are going backward.
// Going backward:
// If we hit a RET, we entered a function (reverse of return).
// If we hit a CALL, we are leaving a function (reverse of call).
//
// Actually, standard "k" command works.
// But if the user wants "CallViz" via TTD, maybe they want the *executed path*.
// The executed path backward is just the instruction stream.
// Filtered by "Is it a CALL instruction?"
// If we execute backward and see a CALL instruction where the target was *us*, then that's our caller.
//
// Simplified approach: Replay backward, look for CALL instructions where the destination was the previous IP?
// Or just use the TTD cursor to find where the SP changed?
//
// Let's implement a simple Stack Walk using TTD replay similar to what was hinted.
// We track the SP. If SP increases (pop), we might have returned from a function? No, in reverse, if SP *decreases* (push), we are undoing a POP/RET?
//
// Actually, the TTD Engine has `SetCallReturnCallback`.
// In Backward Replay:
// - A "Call" callback is fired when we *reverse over* a CALL instruction.
// - A "Return" callback is fired when we *reverse over* a RET instruction.
//
// If we want the stack trace:
// We start at depth D.
// If we hit a CALL (going backward), it means we *un-call* a function. So we go up the stack (caller).
// If we hit a RET (going backward), it means we *un-return*? (i.e., we are going back into a function that had finished).
//
// We only care about the "Active Stack".
// So we want to find the CALLs that put us here.
//
// Strategy:
// Replay backward.
// 1. If we encounter a CALL instruction (reverse-executed), record it as a caller.
// 2. Continue until main or limit.

std::vector<CallRecord> GetBackwardCallStack(ICursor* cursor, int maxDepth = 20) {
    std::vector<CallRecord> stack;

    // We need to clone the cursor to not mess up the global one
    UniqueCursor replayCursor(g_pReplayEngine->NewCursor());
    replayCursor->SetPosition(cursor->GetPosition());

    // We need symbols
    IDebugClient* client;
    DebugCreate(__uuidof(IDebugClient), (void**)&client);
    CComQIPtr<IDebugSymbols3> symbols(client);

    auto _CallCallback = [](uintptr_t context, GuestAddress guestInstructionAddress, _In_ GuestAddress guestFallThroughInstructionAddress, const IThreadView* thread) {
        auto* stackPtr = (std::vector<CallRecord>*)context;
        // We found a call!
        // This instruction at `guestInstructionAddress` called `guestFallThroughInstructionAddress` (wait, no. FallThrough is next instr).
        // For a CALL, the target is determined by decoding or reading memory.
        // But the callback gives us the instruction address.

        // In backward replay, we hit the CALL instruction.
        // This means this instruction *called* something.
        // Was it the function we were just in?
        // Yes.

        CallRecord rec;
        rec.pos = thread->GetPosition(); // Position of the CALL
        rec.targetIP = guestInstructionAddress; // Address of the CALL instruction (the caller)

        stackPtr->push_back(rec);

        // If we want to stop, we can't easily return "Stop" from here unless we set a flag.
        // But we can check size.
    };

    // Setting up callback
    // We need to use `SetCallReturnCallback`.
    // Note: The callback signature might be different depending on TTD version.
    // The snippet used:
    // void(uintptr_t context, GuestAddress guestInstructionAddress, GuestAddress guestFallThroughInstructionAddress, const IThreadView* thread)

    replayCursor->SetEventMask(EventMask::Call); // We only care about Calls (going backward, this is identifying callers)
    replayCursor->SetReplayFlags(ReplayFlags::ReplayOnlyCurrentThread | ReplayFlags::ReplaySegmentsSequentially);

    // We can't pass a lambda with capture to a function pointer if it expects a C-style func ptr.
    // But TTD C++ API often takes std::function or similar?
    // The snippet showed passing `(uintptr_t)&curSP` as context.
    // If the API takes a function pointer and a context, we can use a static relay or a capture-less lambda.
    // The lambda above is capture-less.

    static std::vector<CallRecord>* s_stackPtr = nullptr;
    s_stackPtr = &stack;

    // We need a termination condition.
    // ReplayBackward runs until beginning or interrupt.
    // We can interpret the callback return value?
    // The snippet didn't show return value.
    // Checking `IReplayEngineStl.h` or memory would help, but let's assume standard TTD API:
    // callbacks usually return void or bool.
    // If void, we must interrupt via `replayCursor->Interrupt()`. (If available) or just run step by step?
    // Step by step is slow.

    // Let's try ReplayBackward with a condition.
    // Actually, ReplayBackward takes no args in the snippet?
    // `ICursorView::ReplayResult result = cursor->ReplayBackward();`

    // If we can't interrupt from callback easily, we might loop?
    // "ReplayBackward" runs until event.
    // If we set EventMask::Call, it should stop at every CALL.

    // Logic to find Call Stack (Callers)
    // We replay backward.
    // - If we hit a RET (reverse), we "enter" a function that had returned. Depth++.
    // - If we hit a CALL (reverse), we "leave" a function that called us. Depth--.
    // - If Depth < 0, then this CALL is a real caller on the stack.

    // We need to stop at both CALL and RET.
    replayCursor->SetEventMask(EventMask::Call | EventMask::Return);

    int stackDepth = 0; // Current depth relative to start
    int callersFound = 0;

    while(callersFound < maxDepth) {
        ICursorView::ReplayResult result = replayCursor->ReplayBackward();

        if (result.StopReason == EventType::Call) {
            // We hit a CALL (reverse).
            // Means we "un-called".
            stackDepth--;

            if (stackDepth < 0) {
                // This is a caller of the current stack frame!
                CallRecord rec;
                rec.pos = replayCursor->GetPosition();
                rec.targetIP = replayCursor->GetProgramCounter();

                // Symbol
                if (symbols) {
                    char buffer[256];
                    uint64_t disp = 0;
                    if (SUCCEEDED(symbols->GetNameByOffset(rec.targetIP, buffer, sizeof(buffer), NULL, &disp))) {
                        rec.symbolName = std::format("{}+0x{:X}", buffer, disp);
                    } else {
                        rec.symbolName = std::format("0x{:X}", rec.targetIP);
                    }
                }

                stack.push_back(rec);
                callersFound++;

                // Reset depth to 0 relative to this new frame?
                // No, we are now "in" the caller.
                // stackDepth is -1.
                // We want to find *its* caller.
                // Effectively we reset our "relative" depth counter, or just keep going.
                // If we keep going, next caller is at -2, etc.
                // But easier to just set stackDepth = 0 (relative to the *new* current frame).
                 stackDepth = 0;
            }
        }
        else if (result.StopReason == EventType::Return) {
            // We hit a RET (reverse).
            // Means we "un-returned" (entered a function that had finished).
            stackDepth++;
        }
        else {
            // Hit start or something else
            break;
        }

        // Safety check for getting stuck
        if (result.StepCount == 0) {
            break;
        }
    }

    if (client) client->Release();
    return stack;
}


void _CallViz(IDebugClient* client, const std::string& arg)
{
    // 1. Get Call Stack
    std::vector<CallRecord> stack = GetBackwardCallStack(g_pGlobalCursor);

    if (stack.empty()) {
        dprintf("No call history found or trace start reached.\n");
        return;
    }

    // 2. Convert to Visualizer Data
    // We represent the linear stack as a chain of nodes.
    // Parent -> Child
    // Stack: [Caller1, Caller2, Caller3...] (Caller1 is most recent).
    // So Caller1 called Us. Caller2 called Caller1.
    // Tree: CallerN -> ... -> Caller2 -> Caller1 -> (Current)

    std::map<int, std::vector<VisualizerNodeData>> treeData;

    // Root is the oldest caller (last in vector)
    // We assign IDs: 1..N

    int currentParentId = 0; // 0 is root parent

    for (int i = stack.size() - 1; i >= 0; --i) {
        VisualizerNodeData node;
        node.id = stack.size() - i; // 1, 2, ...
        node.parentId = currentParentId;
        node.label = std::wstring(stack[i].symbolName.begin(), stack[i].symbolName.end());

        treeData[currentParentId].push_back(node);

        currentParentId = node.id;
    }

    // Add "Current" node?
    // The user might want to see where they are.
    // The loop ended at the most recent caller. The "Current" function is what was called.
    // We can add a node for "Current Location".
    {
        VisualizerNodeData node;
        node.id = stack.size() + 1;
        node.parentId = currentParentId;
        node.label = L"Current Position";
        treeData[currentParentId].push_back(node);
    }

    // 3. Show GUI
    // We need to run this on the GUI thread or create the window.
    // The GUI system (TimeTrackGUIWnd) seems to handle its own thread or runs on main?
    // TimeTrackGUIWnd::GUIWnd starts a thread "GUIMainThread".
    // So we just new the window.

    new TimeTrackGUI::TimeTrackVisualizerWnd(treeData, "Call Visualizer");
}

HRESULT CALLBACK callviz(IDebugClient* const pClient, const char* const pArgs) noexcept
try
{
    // Ensure GUI system is initialized?
    // The `new TimeTrackVisualizerWnd` constructor should handle it (base `GUIWnd`).

    std::string arg = pArgs ? pArgs : "";
    _CallViz(pClient, arg);

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


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

#include "GUI/TimeTrackGUI.h"

extern IReplayEngineView* g_pReplayEngine;
extern ICursorView* g_pGlobalCursor;
extern ProcessorArchitecture g_TargetCPUType;


Position* FindCallTree(ICursor* cursor){
    /*
    Position* pos = new Position[32];

    struct _result {
        size_t node_count;
        Position node_pos[1];
    };

    GuestAddress curSP = cursor->GetStackPointer();

    auto _CallReturnCallback = [](uintptr_t context, GuestAddress guestInstructionAddress, _In_ GuestAddress guestFallThroughInstructionAddress, const IThreadView* thread){
        GuestAddress curSP = *(GuestAddress*)context;
    };

    cursor->SetEventMask(EventMask::None);
    cursor->SetReplayFlags(ReplayFlags::ReplayOnlyCurrentThread | ReplayFlags::ReplaySegmentsSequentially);
    cursor->SetCallReturnCallback(_CallReturnCallback, (uintptr_t)&curSP);

    ICursorView::ReplayResult result = cursor->ReplayBackward();

    cursor->RemoveMemoryWatchpoint(wd);
    */
    return NULL;
}

void _CallViz(IDebugClient* client, const std::string& arg)
{
    new TimeTrackGUI::TimeTrackGUIWnd();
}
HRESULT CALLBACK callviz(IDebugClient* const pClient, const char* const pArgs) noexcept
try
{
    if (pArgs == nullptr || strlen(pArgs) == 0)
    {
        dprintf("Usage: !callviz <address>\nNote: If <address> is omitted, the command uses the current IP as the starting point.\n");
        return S_OK;
    }

    std::string arg = pArgs;
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
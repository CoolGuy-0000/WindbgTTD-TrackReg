#include "stdafx.h"
#include <Windows.h>
#include <map>
#include <vector>
#include <string>
#include <format>
#include "GUI/TimeTrackVisualizerWnd.h"
#include <DbgEng.h>

#include "TraceRecord.h"
#include "TimeTrackLogic.h"

#include <TTD/IReplayEngine.h>
#include <TTD/IReplayEngineStl.h>


#include "Formatters.h"
#include "ReplayHelpers.h"

#include <DbgEng.h>
#include <WDBGEXTS.H>
#include <atlcomcli.h>


using namespace TTD;
using namespace Replay;

HRESULT CALLBACK timetrackgui(IDebugClient* const pClient, const char* const pArgs) noexcept
try
{
    if (pArgs == nullptr || strlen(pArgs) == 0)
    {
        dprintf("Usage: !timetrackgui <register or memory> <size> <Max Steps=50>\n");
        return S_OK;
    }

    std::string arg = pArgs;

    // Call the original logic
    auto tree = _TimeTrack(pClient, arg);

    if (tree.empty()) {
        dprintf("No trace found or error occurred.\n");
        return S_OK;
    }

    // Convert to Visualizer Data
    std::map<int, std::vector<VisualizerNodeData>> vizData;

    // We need symbols to generate labels
    CComQIPtr<IDebugSymbols3> symbols(pClient);

    // Helper to get label
    auto GetLabel = [&](Position pos) -> std::wstring {
        if (!symbols) return std::format(L"Pos: {:X}", pos.Sequence);

        // We need to resolve the address at this position?
        // TraceRecord has a Position.
        // We need the IP at that position.
        // Accessing the IP requires setting a cursor to that position.
        // This might be slow for many nodes.
        // But we have to do it to get the instruction.

        // Actually, _TimeTrack logic returns the position of the *write*.
        // The user usually wants to see the instruction that wrote it.

        return std::format(L"{:X}:{:X}", pos.Sequence, pos.Steps);
    };

    // We can optimize by reusing one cursor
    extern IReplayEngineView* g_pReplayEngine;
    UniqueCursor cursor(g_pReplayEngine->NewCursor());

    // Iterate the tree
    for (const auto& pair : tree) {
        int parentId = pair.first;
        for (const auto& rec : pair.second) {
            VisualizerNodeData node;
            node.id = rec.id;
            node.parentId = rec.parentId;

            // Get Info
            cursor->SetPosition(rec.pos);
            uint64_t ip = (uint64_t)cursor->GetProgramCounter();

            char buffer[256];
            uint64_t disp = 0;
            std::string symName;
            if (symbols && SUCCEEDED(symbols->GetNameByOffset(ip, buffer, sizeof(buffer), NULL, &disp))) {
                symName = std::format("{}+0x{:X}", buffer, disp);
            } else {
                symName = std::format("0x{:X}", ip);
            }

            node.label = std::wstring(symName.begin(), symName.end());
            node.details = std::format("Seq: {:X}", rec.pos.Sequence);

            vizData[parentId].push_back(node);
        }
    }

    // Launch GUI
    new TimeTrackGUI::TimeTrackVisualizerWnd(vizData, "TimeTrack Visualization");

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

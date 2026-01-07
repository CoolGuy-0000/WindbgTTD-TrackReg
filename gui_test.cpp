#include <Windows.h>
#include <DbgEng.h>
#include <WDBGEXTS.H>
#include <atlcomcli.h>
#include "TimeTrackGUI.h"


extern TimeTrackGUI::TimeTrackGUIWnd* track_gui;

HRESULT CALLBACK timetrackgui(IDebugClient* const pClient, const char* const pArgs) noexcept
try
{
    if(!track_gui)
        track_gui = new TimeTrackGUI::TimeTrackGUIWnd();

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
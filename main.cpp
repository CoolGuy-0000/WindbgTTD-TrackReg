#include "stdafx.h"

#include <Windows.h>
#include <stdexcept>

#include <TTD/IReplayEngine.h>
#include "ReplayHelpers.h"

#include <dbgeng.h>
#include <wdbgexts.h>
#include <atlcomcli.h>

#include <d2d1.h>
#include <dwrite.h>
#include "GUI/TimeTrackGUI.h"

using namespace TTD;
using namespace Replay;

#if defined(_WIN64)
WINDBG_EXTENSION_APIS64 ExtensionApis;
#elif defined(_WIN32)
WINDBG_EXTENSION_APIS ExtensionApis;
#endif

IReplayEngineView* g_pReplayEngine = nullptr;
ICursorView* g_pGlobalCursor = nullptr;
ProcessorArchitecture g_TargetCPUType;

template < typename Interface >
inline Interface* QueryInterfaceByIoctl()
{
    WDBGEXTS_QUERY_INTERFACE wqi = {};
    wqi.Iid = &__uuidof(Interface);
    auto const ioctlSuccess = Ioctl(IG_QUERY_TARGET_INTERFACE, &wqi, sizeof(wqi));
    if (!ioctlSuccess)
    {
        throw std::invalid_argument("Unable to get interface.");
    }
    if (wqi.Iface == nullptr)
    {
        throw std::invalid_argument("Unable to get interface. Query succeeded, but interface was NULL.");
    }
    return static_cast<Interface*>(wqi.Iface);
}

HRESULT CALLBACK DebugExtensionInitialize(_Out_ ULONG* pVersion, _Out_ ULONG* pFlags) noexcept
{
    *pVersion = DEBUG_EXTENSION_VERSION(1, 0);
    *pFlags = 0;

    CComPtr<IDebugClient> client;
    if (SUCCEEDED(DebugCreate(IID_PPV_ARGS(&client))))
    {
        CComQIPtr<IDebugControl> control(client);
        if (control) {
            ExtensionApis.nSize = sizeof(ExtensionApis);
#ifdef _WIN64
            control->GetWindbgExtensionApis64(&ExtensionApis);
#elif _WIN32
			control->GetWindbgExtensionApis32((PWINDBG_EXTENSION_APIS32)&ExtensionApis);
#endif

            g_pReplayEngine = QueryInterfaceByIoctl<IReplayEngineView>();
            g_pGlobalCursor = QueryInterfaceByIoctl<ICursorView>();

            if (!g_pReplayEngine || !g_pGlobalCursor) {
                return E_FAIL;
            }

            g_TargetCPUType = GetGuestArchitecture(*g_pGlobalCursor);

            return S_OK;
        }
    }

    return E_FAIL;
}

void CALLBACK DebugExtensionUninitialize() noexcept {
    if (TimeTrackGUI::GUIWnd::m_hThread) {
        DWORD tid = GetThreadId(TimeTrackGUI::GUIWnd::m_hThread);
        if (tid != 0) {
            PostThreadMessage(tid, WM_TTGUI_DESTROY, 0, 0);
            if (TimeTrackGUI::GUIWnd::m_hThreadExitEvent) {
                WaitForSingleObject(TimeTrackGUI::GUIWnd::m_hThreadExitEvent, INFINITE);
                CloseHandle(TimeTrackGUI::GUIWnd::m_hThreadExitEvent);
                TimeTrackGUI::GUIWnd::m_hThreadExitEvent = NULL;
            }
            else {
                // 이벤트가 없으면 기존 방식 유지(후방 호환)
                WaitForSingleObject(TimeTrackGUI::GUIWnd::m_hThread, 5000);
            }
            return;
        }
    }

    PostMessage(HWND_BROADCAST, WM_TTGUI_DESTROY, 0, 0);
    if (TimeTrackGUI::GUIWnd::m_hThreadExitEvent) {
        WaitForSingleObject(TimeTrackGUI::GUIWnd::m_hThreadExitEvent, INFINITE);
        CloseHandle(TimeTrackGUI::GUIWnd::m_hThreadExitEvent);
        TimeTrackGUI::GUIWnd::m_hThreadExitEvent = NULL;
    }
}
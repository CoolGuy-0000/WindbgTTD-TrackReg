#include "stdafx.h"

#include <Windows.h>
#include <stdexcept>

#include <TTD/IReplayEngine.h>
#include "ReplayHelpers.h"

#include <dbgeng.h>
#include <wdbgexts.h>
#include <atlcomcli.h>

using namespace TTD;
using namespace Replay;

#if defined(_WIN64)
WINDBG_EXTENSION_APIS64 ExtensionApis;
#elif defined(_WIN32)
WINDBG_EXTENSION_APIS ExtensionApis;
#endif

IReplayEngineView* g_pReplayEngine;
ICursorView* g_pGlobalCursor;
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
			control->GetWindbgExtensionApis32((WINDBG_EXTENSION_APIS32*)&ExtensionApis);
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

void CALLBACK DebugExtensionUninitialize() noexcept {}
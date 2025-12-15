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

WINDBG_EXTENSION_APIS ExtensionApis;

IReplayEngineView* g_pReplayEngine;
ICursorView* g_pGlobalCursor;
ProcessorArchitecture g_TargetCPUType;


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

HRESULT CALLBACK DebugExtensionInitialize(_Out_ ULONG* pVersion, _Out_ ULONG* pFlags) noexcept
{
    g_pReplayEngine = QueryInterfaceByIoctl<IReplayEngineView>();
    g_pGlobalCursor = QueryInterfaceByIoctl<ICursorView>();

    if(!g_pReplayEngine || !g_pGlobalCursor){
        return E_FAIL;
    }

	g_TargetCPUType = GetGuestArchitecture(*g_pGlobalCursor);

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
			control->GetWindbgExtensionApis32(&ExtensionApis);
#endif
            return S_OK;
        }
    }

    return E_FAIL;
}

void CALLBACK DebugExtensionUninitialize() noexcept {}
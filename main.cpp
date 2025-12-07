#include <Windows.h>
#include <exception>
#include <stdexcept>
#include <string>

#define KDEXT_64BIT
#include <dbgeng.h>
#include <wdbgexts.h>
#include <atlcomcli.h>

WINDBG_EXTENSION_APIS64 ExtensionApis;

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
            control->GetWindbgExtensionApis64(&ExtensionApis);
            return S_OK;
        }
    }

    return E_FAIL;
}

void CALLBACK DebugExtensionUninitialize() noexcept {}
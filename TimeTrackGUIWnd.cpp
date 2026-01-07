#include "TimeTrackGUI.h"
#include "UILoader.h"
#include "UIButton.h"

using namespace TimeTrackGUI;

TimeTrackGUIWnd::TimeTrackGUIWnd() : GUIWnd() {}
TimeTrackGUIWnd::~TimeTrackGUIWnd() {}

std::wstring GetConfigFilePathInDll() {
    HMODULE hModule = NULL;

    ::GetModuleHandleExW(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        (LPCWSTR)&GetConfigFilePathInDll,
        &hModule
    );

    if (hModule == NULL) {
        return L"";
    }

    wchar_t pathBuffer[MAX_PATH];
    ::GetModuleFileNameW(hModule, pathBuffer, MAX_PATH);

    std::wstring fullPath(pathBuffer);

    size_t lastSlashPos = fullPath.find_last_of(L"\\/");

    if (lastSlashPos != std::wstring::npos) {
        fullPath = fullPath.substr(0, lastSlashPos + 1);
    }

    return fullPath;
}

LRESULT TimeTrackGUIWnd::WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    auto res = GUIWnd::WndProc(hWnd, message, wParam, lParam);

    switch (message) {
        case WM_CREATE: {
            UILoader::CreateUIFromKeyValues(m_manager.get(), GetConfigFilePathInDll() + L"gui_main.txt");
            return WndProc_Success;
        }
        
        default: {
            /*
            if (message == WM_TTGUI_COMMAND)
            {
                switch (m_manager->m_EventMsg.type) {
                    case UIEventType::ButtonClick: {
                        UIButton* button = (UIButton*)m_manager->m_EventMsg.source;

                        if (button->GetId() == 2) {
                            MessageBoxA(NULL, "hello world!", "hello world!", MB_OK);
                        }

                        return WndProc_Success;
                    }
                }
            }
            */
            break;
        }
        
    }

    return res;
}

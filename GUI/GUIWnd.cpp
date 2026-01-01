#include "..\stdafx.h"

#include "TimeTrackGUI.h"
#include <windowsx.h>
#include "..\utils.h"

using namespace TimeTrackGUI;
using Microsoft::WRL::ComPtr;

ComPtr<ID2D1Factory> g_pD2D1Factory = nullptr;
ComPtr<IDWriteFactory> g_pDWriteFactory = nullptr;

const UINT WM_TTGUI_CREATE = RegisterWindowMessage("WM_TTGUI_CREATE");
const UINT WM_TTGUI_DESTROY = RegisterWindowMessage("WM_TTGUI_DESTROY");

HANDLE GUIWnd::m_hThread = NULL;
HANDLE GUIWnd::m_hThreadReadyEvent = NULL;
HANDLE GUIWnd::m_hThreadExitEvent = NULL;
std::vector<GUIWnd*> GUIWnd::m_instances;
std::mutex GUIWnd::m_listMutex;


GUIWnd::GUIWnd() : m_hwnd(NULL) {
    if (!m_hThreadReadyEvent) {
        m_hThreadReadyEvent = CreateEvent(NULL, TRUE, FALSE, NULL); // manual-reset
    }
    
    if (!m_hThreadExitEvent) { // 추가: 종료 이벤트 생성
        m_hThreadExitEvent = CreateEvent(NULL, TRUE, FALSE, NULL); // manual-reset
    }

    if (!m_hThread) {
        m_hThread = CreateThread(NULL, NULL, GUIMainThread, NULL, NULL, NULL);
    }

    // 스레드가 메시지 큐를 만들고 준비될 때까지 대기
    if (m_hThreadReadyEvent) {
        WaitForSingleObject(m_hThreadReadyEvent, INFINITE);
        // 이벤트는 계속 유지(다음 생성자 호출에서 바로 통과). 필요 시 ResetEvent 사용.
    }

    DWORD tid = GetThreadId(m_hThread);
    if (tid != 0) {
        PostThreadMessage(tid, WM_TTGUI_CREATE, (WPARAM)this, 0);
    }
    else {
        PostMessage(HWND_BROADCAST, WM_TTGUI_CREATE, (WPARAM)this, 0);
    }

    std::lock_guard<std::mutex> lock(m_listMutex);
    m_instances.push_back(this);
}

GUIWnd::~GUIWnd() {
    if (m_hwnd) {
        DestroyWindow(m_hwnd);
        m_hwnd = NULL;
    }

    std::lock_guard<std::mutex> lock(m_listMutex);
    m_instances.erase(
        std::remove(m_instances.begin(), m_instances.end(), this),
        m_instances.end()
    );
}

std::vector<GUIWnd*> GUIWnd::GetGUIInstances() {
    std::lock_guard<std::mutex> lock(m_listMutex);
    return m_instances;
}

HWND GUIWnd::GetHWND() {
    return m_hwnd;
}

LRESULT CALLBACK GUIWnd::GUIPreWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    GUIWnd* pThis = (GUIWnd*)GetWindowLongPtr(hWnd, GWLP_USERDATA);

    if (message == WM_NCCREATE) {
        LPCREATESTRUCT lpcs = (LPCREATESTRUCT)lParam;
        pThis = (GUIWnd*)lpcs->lpCreateParams;
        pThis->m_hwnd = hWnd;
        SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)pThis);

        pThis->m_manager = UIManager::Create<UIManager>(pThis);
        if (!pThis->m_manager)throw;

    }

    if (pThis) {
        return pThis->WndProc(hWnd, message, wParam, lParam);
    }

    return DefWindowProc(hWnd, message, wParam, lParam);
}

LRESULT GUIWnd::WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_CREATE: {
            return OnCreate((LPCREATESTRUCT)lParam);
        }
        case WM_DESTROY:{
            auto result = OnDestroy();
            m_hwnd = NULL;
            SetWindowLongPtr(hWnd, GWLP_USERDATA, NULL);
            delete this;
            return result;
        }
        case WM_PAINT: {
            return OnPaint();
        }
        case WM_MOUSEMOVE: {
            return OnCursorMove({ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) });
        }
        case WM_SIZE: {
            return OnResize((UINT)wParam, { LOWORD(lParam), HIWORD(lParam) });
        }
        case WM_CHAR: {
            return OnKeyInput((WCHAR)wParam, (UINT)(lParam & 0xFFFF));
        }
        case WM_LBUTTONDOWN:{
            return OnLButtonDown((UINT)wParam, { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) });
        }
        case WM_LBUTTONUP: {
            return OnLButtonUp((UINT)wParam, { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) });
        }
        case WM_RBUTTONDOWN: {
            return OnRButtonDown((UINT)wParam, { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) });
        }
        case WM_RBUTTONUP: {
            return OnRButtonUp((UINT)wParam, { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) });
        }
    }
    return DefWindowProc(hWnd, message, wParam, lParam);
}

LRESULT GUIWnd::OnCreate(LPCREATESTRUCT lpcs) {
    return 0;
}
LRESULT GUIWnd::OnDestroy() {
    return 0;
}
LRESULT GUIWnd::OnPaint() {
    return 0;
}
LRESULT GUIWnd::OnCursorMove(POINT pt) {
    return 0;
}
LRESULT GUIWnd::OnResize(UINT flag, POINT new_size) {
    return 0;
}
LRESULT GUIWnd::OnKeyInput(WCHAR ch, UINT repCount) {
    return 0;
}
LRESULT GUIWnd::OnLButtonDown(UINT flag, POINT cursorPt) {
    return 0;
}
LRESULT GUIWnd::OnLButtonUp(UINT flag, POINT cursorPt) {
    return 0;
}
LRESULT GUIWnd::OnRButtonDown(UINT flag, POINT cursorPt) {
    return 0;
}
LRESULT GUIWnd::OnRButtonUp(UINT flag, POINT cursorPt) {
    return 0;
}

DWORD WINAPI GUIWnd::GUIMainThread(LPVOID) {

    HINSTANCE hInst = GetModuleHandle(NULL);
    const char* className = "TTDTimeView";

    WNDCLASSEX wc = { 0 };
    if (!GetClassInfoEx(hInst, className, &wc)) {
        wc.cbSize = sizeof(WNDCLASSEX);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = GUIWnd::GUIPreWndProc;
        wc.hInstance = hInst;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.lpszClassName = className;
        if (!RegisterClassEx(&wc)) {
            return 0;
        }
    }

    if (!g_pD2D1Factory)
        D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, g_pD2D1Factory.GetAddressOf());

    if (!g_pDWriteFactory)
        DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(g_pDWriteFactory.GetAddressOf()));

    MSG dummy;
    PeekMessage(&dummy, NULL, 0, 0, PM_NOREMOVE);

    if (m_hThreadReadyEvent) SetEvent(m_hThreadReadyEvent);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        if (msg.hwnd == NULL) {
            if (msg.message == WM_TTGUI_CREATE) {
                GUIWnd* _this = (GUIWnd*)msg.wParam;

                if (_this && !_this->m_hwnd)
                    CreateWindowEx(
                        NULL,
                        className,
                        "TTD Time View",
                        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                        100, 100, 800, 200,
                        NULL, NULL, wc.hInstance, _this
                    );

                continue;
            }
            else if (msg.message == WM_TTGUI_DESTROY) {
                GUIWnd* _this = (GUIWnd*)msg.wParam;
                if (_this) {
                    delete _this;
                    continue;
                }

                if(m_hThread)
                    CloseHandle(m_hThread);

                m_hThread = NULL;

                PostQuitMessage(0);
                continue;
            }
        }

        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    for (auto gui : m_instances) delete gui;

    g_pD2D1Factory.Reset();
    g_pDWriteFactory.Reset();

    UnregisterClass(className, wc.hInstance);

    if (m_hThreadReadyEvent) {
        CloseHandle(m_hThreadReadyEvent);
        m_hThreadReadyEvent = NULL;
    }

    if (m_hThread)
        CloseHandle(m_hThread);

    m_hThread = NULL;

    if (m_hThreadExitEvent) {
        SetEvent(m_hThreadExitEvent);
        // 핸들은 DebugExtensionUninitialize에서 닫도록 둠
    }

    return 0;
}
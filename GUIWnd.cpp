#include "TimeTrackGUI.h"
#include "resource.h"
#include <windowsx.h>

using namespace TimeTrackGUI;
using Microsoft::WRL::ComPtr;

extern HINSTANCE g_hInstDll;

ComPtr<ID2D1Factory> g_pD2D1Factory = nullptr;
ComPtr<IDWriteFactory> g_pDWriteFactory = nullptr;

const UINT WM_TTGUI_CREATE = RegisterWindowMessage(TEXT("WM_TTGUI_CREATE"));
const UINT WM_TTGUI_DESTROY = RegisterWindowMessage(TEXT("WM_TTGUI_DESTROY"));
const UINT WM_TTGUI_COMMAND = RegisterWindowMessage(TEXT("WM_TTGUI_COMMAND"));

HANDLE GUIWnd::m_hThread = NULL;
HANDLE GUIWnd::m_hThreadReadyEvent = NULL;
std::mutex GUIWnd::m_ThreadExitMutex;

std::vector<GUIWnd*> GUIWnd::m_instances;
std::mutex GUIWnd::m_listMutex;

GUIWnd::GUIWnd() : m_hwnd(NULL) {
    if (!m_hThreadReadyEvent) {
        m_hThreadReadyEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
        if (!m_hThreadReadyEvent) throw std::runtime_error("Failed to create thread event.");
    }

    if (!m_hThread) {
        m_hThread = CreateThread(NULL, NULL, GUIMainThread, NULL, NULL, NULL);
        if (!m_hThread) throw std::runtime_error("Failed to create thread.");
    }

    WaitForSingleObject(m_hThreadReadyEvent, INFINITE);

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
    std::lock_guard<std::mutex> lock(m_listMutex);

    m_instances.erase(
        std::remove(m_instances.begin(), m_instances.end(), this),
        m_instances.end()
    );
}

std::vector<GUIWnd*> GUIWnd::GetGUIInstances() {
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
        auto result = pThis->WndProc(hWnd, message, wParam, lParam);
        
        if (result == WndProc_Unknown)return DefWindowProc(hWnd, message, wParam, lParam);

        return result;
    }

    return DefWindowProc(hWnd, message, wParam, lParam);
}

LRESULT GUIWnd::WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_DESTROY) {
        m_hwnd = NULL;
        SetWindowLongPtr(hWnd, GWLP_USERDATA, NULL);
        delete this;
        
        if(m_instances.size() == 0) ShutdownAllGUIWnds();

        return WndProc_Success;
    }
    else if (message == WM_GETMINMAXINFO) {
        PMINMAXINFO info = (PMINMAXINFO)lParam;

        if (m_MinMaxSize.left > 0) info->ptMinTrackSize.x = (LONG)m_MinMaxSize.left;
        if (m_MinMaxSize.top > 0) info->ptMinTrackSize.y = (LONG)m_MinMaxSize.top;

        if (m_MinMaxSize.right > 0) info->ptMaxTrackSize.x = (LONG)m_MinMaxSize.right;
        if (m_MinMaxSize.bottom > 0) info->ptMaxTrackSize.y = (LONG)m_MinMaxSize.bottom;
        return WndProc_Success;
    }

    return m_manager->WndProc(hWnd, message, wParam, lParam);
}

DWORD WINAPI GUIWnd::GUIMainThread(LPVOID) {
    const TCHAR* className = TEXT("TTDTimeView");

    WNDCLASSEX wc = { 0 };
    if (!GetClassInfoEx(g_hInstDll, className, &wc)) {
        wc.cbSize = sizeof(WNDCLASSEX);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = GUIWnd::GUIPreWndProc;
        wc.hInstance = g_hInstDll;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hIconSm = (HICON)LoadImage(g_hInstDll, MAKEINTRESOURCE(IDI_ICON1), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);
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

    std::lock_guard<std::mutex> lock(m_ThreadExitMutex);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        if (msg.hwnd == NULL) {
            if (msg.message == WM_TTGUI_CREATE) {
                GUIWnd* _this = (GUIWnd*)msg.wParam;

                if (_this && !_this->m_hwnd) {
                    CreateWindowEx(
                        NULL,
                        className,
                        TEXT("TTD Time View"),
                        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                        100, 100, 800, 200,
                        NULL, NULL, wc.hInstance, _this
                    );

                    UpdateWindow(_this->m_hwnd);
                }

                continue;
            }
            else if (msg.message == WM_TTGUI_DESTROY) {
                GUIWnd* _this = (GUIWnd*)msg.wParam;
                if (_this) {
                    _this->Destroy();
                    continue;
                }

                PostQuitMessage(0);
                continue;
            }
        }

        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    for (auto gui : m_instances) gui->Destroy();

    std::vector<GUIWnd*>().swap(m_instances);

    g_pD2D1Factory.Reset();
    g_pDWriteFactory.Reset();

    UnregisterClass(className, wc.hInstance);

    if (m_hThreadReadyEvent) {
        CloseHandle(m_hThreadReadyEvent);
        m_hThreadReadyEvent = NULL;
    }

    if (m_hThread) {
        CloseHandle(m_hThread);
        m_hThread = NULL;
    }

    return 0;
}

void GUIWnd::ShutdownAllGUIWnds() {
    DWORD tid = GetThreadId(m_hThread);
    if (tid != 0) PostThreadMessage(tid, WM_TTGUI_DESTROY, 0, 0);
    else PostMessage(HWND_BROADCAST, WM_TTGUI_DESTROY, 0, 0);
}
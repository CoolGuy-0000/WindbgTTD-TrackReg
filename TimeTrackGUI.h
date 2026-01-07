#pragma once
#include <Windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <vector>
#include <string>
#include <mutex>
#include <unordered_map>
#include <wrl/client.h>
#include <functional>
using Microsoft::WRL::ComPtr;

extern ComPtr<ID2D1Factory> g_pD2D1Factory;
extern ComPtr<IDWriteFactory> g_pDWriteFactory;

extern const UINT WM_TTGUI_CREATE;
extern const UINT WM_TTGUI_DESTROY;
extern const UINT WM_TTGUI_COMMAND;

namespace TimeTrackGUI {
    class GUIWnd;
    class UIManager;
    class UIElement;


    enum {
        WndProc_Success,
        WndProc_Unknown
    };

    enum class UIEventType {
        Unknown,
        ButtonClick,
        CheckBoxChange,
        TreeSelect,
        TreeRightClick
    };

    struct UIEventMsg {
        UIEventType     type = UIEventType::Unknown;
        UIElement*      source = nullptr;
        void*           data[4];
	};

    class GUIWnd {
    public:
        GUIWnd();
        virtual ~GUIWnd();

        void Destroy() {DestroyWindow(m_hwnd);}

        static std::vector<GUIWnd*> GetGUIInstances();
		static void ShutdownAllGUIWnds();
        
        HWND GetHWND();

        virtual LRESULT WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

    private:
        static LRESULT CALLBACK GUIPreWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
        static DWORD WINAPI GUIMainThread(LPVOID);

    public:
        static HANDLE m_hThread;
        static HANDLE m_hThreadReadyEvent;
        static std::mutex m_ThreadExitMutex;

    protected:
        static std::vector<GUIWnd*> m_instances;
        static std::mutex m_listMutex;
        
        std::unique_ptr<UIManager> m_manager;

        HWND m_hwnd;

    public:
        D2D1_RECT_F m_MinMaxSize = { 100.0f, 100.0f, 800.0f, 200.0f };
    };


    //one UIManager per GUIWnd
    class UIManager {
    public:
        UIManager(GUIWnd* gui);
        ~UIManager();

        template <typename T = UIManager, typename... Args>
        static std::unique_ptr<T> Create(Args&&... args) {
            return std::make_unique<T>(std::forward<Args>(args)...);
        }

        GUIWnd* GetGUIWnd() { return m_gui; }
        
        void Register(UIElement* element);
        void Unregister(UIElement* element);
		void RemoveAllElements() { m_elements.clear(); }

        ID2D1HwndRenderTarget* GetRenderTarget() { return m_pRenderTarget.Get(); }

        void OnDeviceLost();
        void Resize(UINT width, UINT height);
        void Clear();
        void Render();

        LRESULT WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

        ID2D1SolidColorBrush* AcquireBrush(const D2D1_COLOR_F& color);
        void ReleaseBrush(const D2D1_COLOR_F& color);

        ComPtr<ID2D1SolidColorBrush> GetOrCreateSharedBrush();
        void DiscardBrushCache(); // 브러시 캐시/공유 브러시 해제

        // 텍스트 포맷: UIManager 생애주기동안 보관
        IDWriteTextFormat* GetOrCreateTextFormat(const std::string& id,
            const std::function<HRESULT(IDWriteFactory*, IDWriteTextFormat**)>& creator);

        // 텍스트 포맷/글로벌 자원 강제 정리 (옵션)
        void DiscardTextFormats();

        void InvalidateZOrder() { m_isZOrderDirty = true; }

        UIEventMsg m_EventMsg;
    private:
        GUIWnd* m_gui = nullptr;
        std::vector<std::unique_ptr<UIElement>> m_elements;
        std::recursive_mutex m_mutex;

        ComPtr<ID2D1HwndRenderTarget> m_pRenderTarget = nullptr;

        std::unordered_map<uint64_t, std::pair<ComPtr<ID2D1SolidColorBrush>, int>> m_brushCache;
        ComPtr<ID2D1SolidColorBrush> m_sharedBrush;
        std::unordered_map<std::string, ComPtr<IDWriteTextFormat>> m_textFormats;

        bool m_isZOrderDirty = false;
    };

    class UIElement {
    public:
        friend class UIManager;

        UIElement(UIManager* manager, D2D1_RECT_F rect);
        virtual ~UIElement();
        
        void Destroy() {
            if (m_manager)m_manager->Unregister(this);
        }

        virtual void Render() {}
        virtual void CreateDeviceResources() {}
        virtual void DiscardDeviceResources() {}

        virtual LRESULT WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

        void SetZIndex(int zIndex);
        int GetZIndex() const { return m_zIndex; }

    public:
        UINT32 m_id = (UINT32)-1;

    protected:
        UIManager* m_manager;
        D2D1_RECT_F m_rect;
        bool        m_isCursorOver = false;
        bool        m_isPressed = false;
        bool        m_isClicked = false;
        int         m_zIndex = 0;
    };

    class TimeTrackGUIWnd : public GUIWnd {
    public:
        TimeTrackGUIWnd();
        ~TimeTrackGUIWnd();

        LRESULT WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) override;

    };
}

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

namespace TimeTrackGUI {
    class GUIWnd;
    class UIManager;
    class UIElement;

    struct UIEvent {
        enum class Type {
            Unknown,
            ButtonClicked
        } type = Type::Unknown;

        UIElement* source = nullptr;
        LPVOID reserved = nullptr;
    };

    class GUIWnd {
    public:
        GUIWnd();
        virtual ~GUIWnd();

        static std::vector<GUIWnd*> GetGUIInstances();
        HWND GetHWND();

        virtual LRESULT WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

        virtual LRESULT OnCreate(LPCREATESTRUCT lpcs);
        virtual LRESULT OnDestroy();
        virtual LRESULT OnPaint();
        virtual LRESULT OnCursorMove(POINT pt);
        virtual LRESULT OnResize(UINT flag, POINT new_size);
        virtual LRESULT OnKeyInput(WCHAR ch, UINT repCount);
        virtual LRESULT OnLButtonDown(UINT flag, POINT cursorPt);
        virtual LRESULT OnLButtonUp(UINT flag, POINT cursorPt);
        virtual LRESULT OnRButtonDown(UINT flag, POINT cursorPt);
        virtual LRESULT OnRButtonUp(UINT flag, POINT cursorPt);

        virtual void OnUIEvent(const UIEvent& ev) {}

    private:
        static LRESULT CALLBACK GUIPreWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
        static DWORD WINAPI GUIMainThread(LPVOID);

    public:
        static HANDLE m_hThread;
        static HANDLE m_hThreadReadyEvent;
        static HANDLE m_hThreadExitEvent;
    protected:
        static std::vector<GUIWnd*> m_instances;
        static std::mutex m_listMutex;
        
        std::unique_ptr<UIManager> m_manager;

        HWND m_hwnd;
    };

    //one UIManager per GUIWnd
    class UIManager {
    public:
        UIManager(GUIWnd* gui);
        ~UIManager();

        bool IsShuttingDown() const noexcept { return m_shuttingDown.load(); }

        template <typename T = UIManager, typename... Args>
        static std::unique_ptr<T> Create(Args&&... args) {
            return std::make_unique<T>(std::forward<Args>(args)...);
        }

        template <typename T, typename... Args>
        T* CreateElement(Args&&... args) {
            std::lock_guard<std::recursive_mutex> lk(m_mutex);

            auto element = std::make_unique<T>(this, std::forward<Args>(args)...);

            T* ptr = element.get();

            ptr->CreateDeviceResources();

            m_elements.push_back(std::move(element));

            return ptr;
        }

        GUIWnd* GetGUIWnd() { return m_gui; }
        
        void Register(std::unique_ptr<UIElement> element);
        void Unregister(UIElement* element);
        
        int GetElementId(UIElement* element);

        ID2D1HwndRenderTarget* GetRenderTarget() { return m_pRenderTarget.Get(); }

        void OnDeviceLost();
        void Resize(UINT width, UINT height);
        void Clear();
        void Render();

        void OnCursorMove(D2D1_POINT_2F cursorPt);
        void OnLButtonDown(D2D1_POINT_2F cursorPt);
        void OnLButtonUp(D2D1_POINT_2F cursorPt);

        ID2D1SolidColorBrush* AcquireBrush(const D2D1_COLOR_F& color);
        void ReleaseBrush(const D2D1_COLOR_F& color);

        ComPtr<ID2D1SolidColorBrush> GetOrCreateSharedBrush();
        void DiscardBrushCache(); // 브러시 캐시/공유 브러시 해제

        // 텍스트 포맷: UIManager 생애주기동안 보관
        IDWriteTextFormat* GetOrCreateTextFormat(const std::string& id,
            const std::function<HRESULT(IDWriteFactory*, IDWriteTextFormat**)>& creator);

        // 텍스트 포맷/글로벌 자원 강제 정리 (옵션)
        void DiscardTextFormats();

    private:
        GUIWnd* m_gui = nullptr;
        std::vector<std::unique_ptr<UIElement>> m_elements;
        std::recursive_mutex m_mutex;
        std::atomic<bool> m_shuttingDown{ false };

        ComPtr<ID2D1HwndRenderTarget> m_pRenderTarget = nullptr;

        std::unordered_map<uint64_t, std::pair<ComPtr<ID2D1SolidColorBrush>, int>> m_brushCache;
        ComPtr<ID2D1SolidColorBrush> m_sharedBrush;
        std::unordered_map<std::string, ComPtr<IDWriteTextFormat>> m_textFormats;
    };

    class UIElement {
    public:
        UIElement(UIManager* manager, D2D1_RECT_F rect);
        virtual ~UIElement();

        virtual void Render() {}
        virtual void CreateDeviceResources() {}
        virtual void DiscardDeviceResources() {}

        virtual void OnCursorMove(D2D1_POINT_2F cursorPt);
        virtual void OnLButtonDown(D2D1_POINT_2F cursorPt);
        virtual void OnLButtonUp(D2D1_POINT_2F cursorPt);

    protected:
        UIManager* m_manager;
        D2D1_RECT_F m_rect;
        bool        m_isCursorOver = false;
        bool        m_isPressed = false;
        bool        m_isClicked = false;
    };

    class TimeTrackGUIWnd : public GUIWnd {
    public:
        TimeTrackGUIWnd();
        ~TimeTrackGUIWnd();

        LRESULT OnCreate(LPCREATESTRUCT lpcs) override;
        LRESULT OnDestroy() override;
        LRESULT OnPaint() override;
        LRESULT OnCursorMove(POINT cursorPt) override;
        LRESULT OnResize(UINT flag, POINT new_size) override;
        LRESULT OnLButtonDown(UINT flag, POINT cursorPt) override;
        LRESULT OnLButtonUp(UINT flag, POINT cursorPt) override;

        void OnUIEvent(const UIEvent& ev) override;
    };
}

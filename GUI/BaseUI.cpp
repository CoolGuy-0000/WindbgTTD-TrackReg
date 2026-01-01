#include "TimeTrackGUI.h"
#include "../utils.h"

using namespace TimeTrackGUI;

namespace TimeTrackGUI {

    static inline UINT32 ColorToKey(const D2D1_COLOR_F& c) {
        UINT8 r = static_cast<UINT8>(c.r * 255.0f);
        UINT8 g = static_cast<UINT8>(c.g * 255.0f);
        UINT8 b = static_cast<UINT8>(c.b * 255.0f);
        UINT8 a = static_cast<UINT8>(c.a * 255.0f);
        return (static_cast<UINT32>(a) << 24) | (static_cast<UINT32>(r) << 16) | (static_cast<UINT32>(g) << 8) | static_cast<UINT32>(b);
    }

    static inline uint64_t MakeColorKey(ID2D1RenderTarget* rt, UINT32 colorKey) {
        uint64_t p = reinterpret_cast<uint64_t>(rt);
        return (p << 32) ^ static_cast<uint64_t>(colorKey);
    }

    UIManager::UIManager(GUIWnd* gui) : m_gui(gui) {

        HWND hwnd = gui->GetHWND();

        RECT rc;
        GetClientRect(hwnd, &rc);

        g_pD2D1Factory->CreateHwndRenderTarget(
            D2D1::RenderTargetProperties(),
            D2D1::HwndRenderTargetProperties(
                hwnd,
                D2D1::SizeU(rc.right - rc.left, rc.bottom - rc.top)
            ),
            &m_pRenderTarget
        );

    }

    UIManager::~UIManager() { m_shuttingDown.store(true); }

    void UIManager::Register(std::unique_ptr<UIElement> element) {
        if (!element) return;
        std::lock_guard<std::recursive_mutex> lk(m_mutex);

        m_elements.push_back(std::move(element));
        m_elements.back()->CreateDeviceResources();
    }

    void UIManager::Unregister(UIElement* element) {
        if (!element) return;
        std::lock_guard<std::recursive_mutex> lk(m_mutex);

        m_elements.erase(std::remove_if(m_elements.begin(), m_elements.end(),
            [element](const std::unique_ptr<UIElement>& e) {
                return e.get() == element;
            }), m_elements.end());
    }
    
    int UIManager::GetElementId(UIElement* element) {
        if (!element) return -1;
        std::lock_guard<std::recursive_mutex> lk(m_mutex);
        for (size_t i = 0; i < m_elements.size(); ++i) {
            if (m_elements[i].get() == element) return static_cast<int>(i);
        }
        return -1;
    }

    void UIManager::OnDeviceLost() {
        Clear();
        DiscardBrushCache();
        m_pRenderTarget.Reset();

        HWND hwnd = m_gui->GetHWND();
        RECT rc;
        GetClientRect(hwnd, &rc);

        g_pD2D1Factory->CreateHwndRenderTarget(
            D2D1::RenderTargetProperties(),
            D2D1::HwndRenderTargetProperties(hwnd, D2D1::SizeU(rc.right - rc.left, rc.bottom - rc.top)),
            &m_pRenderTarget
        );

        std::lock_guard<std::recursive_mutex> lk(m_mutex);
        for (const auto& el : m_elements) el->CreateDeviceResources();
    }

    void UIManager::Resize(UINT width, UINT height) {
        if (m_pRenderTarget)
            m_pRenderTarget->Resize(D2D1::SizeU(width, height));
    }

    void UIManager::Clear() {
        std::lock_guard<std::recursive_mutex> lk(m_mutex);
        for (const auto& el : m_elements) el->DiscardDeviceResources();
    }

    void UIManager::Render() {
        if (!m_pRenderTarget) return;

        m_pRenderTarget->BeginDraw();
        m_pRenderTarget->Clear(D2D1::ColorF(D2D1::ColorF::White));

        {
            std::lock_guard<std::recursive_mutex> lk(m_mutex);
            for (const auto& el : m_elements) el->Render();
        }

        if (m_pRenderTarget->EndDraw() == D2DERR_RECREATE_TARGET) {
            OnDeviceLost();
        }
    }

    void UIManager::OnCursorMove(D2D1_POINT_2F pt) {
        std::lock_guard<std::recursive_mutex> lk(m_mutex);
        for (const auto& el : m_elements) el->OnCursorMove(pt);
    }
    void UIManager::OnLButtonDown(D2D1_POINT_2F pt) {
        std::lock_guard<std::recursive_mutex> lk(m_mutex);
        for (const auto& el : m_elements) el->OnLButtonDown(pt);
    }
    void UIManager::OnLButtonUp(D2D1_POINT_2F pt) {
        std::lock_guard<std::recursive_mutex> lk(m_mutex);
        for (const auto& el : m_elements) el->OnLButtonUp(pt);
    }

    // color-specific brush cache (per UIManager / per render target)
    ID2D1SolidColorBrush* UIManager::AcquireBrush(const D2D1_COLOR_F& color) {
        if (!m_pRenderTarget) return nullptr;
        UINT32 ckey = ColorToKey(color);
        uint64_t key = MakeColorKey(m_pRenderTarget.Get(), ckey);

        std::lock_guard<std::recursive_mutex> lk(m_mutex);

        auto it = m_brushCache.find(key);
        if (it != m_brushCache.end()) {
            it->second.second += 1;
            return it->second.first.Get();
        }

        ComPtr<ID2D1SolidColorBrush> brush;
        HRESULT hr = m_pRenderTarget->CreateSolidColorBrush(color, &brush);
        if (SUCCEEDED(hr) && brush) {
            m_brushCache.emplace(key, std::make_pair(brush, 1));
            return brush.Get();
        }
        return nullptr;
    }

    void UIManager::ReleaseBrush(const D2D1_COLOR_F& color) {
        if (!m_pRenderTarget) return;
        UINT32 ckey = ColorToKey(color);
        uint64_t key = MakeColorKey(m_pRenderTarget.Get(), ckey);

        std::lock_guard<std::recursive_mutex> lk(m_mutex);

        auto it = m_brushCache.find(key);
        if (it == m_brushCache.end()) return;
        it->second.second -= 1;
        if (it->second.second <= 0) {
            // ComPtr가 범위를 벗어나면 자동으로 Release
            m_brushCache.erase(it);
        }
    }

    // shared single brush (효율 경로)
    ComPtr<ID2D1SolidColorBrush> UIManager::GetOrCreateSharedBrush() {
        if (!m_pRenderTarget) return nullptr;
        if (m_sharedBrush) return m_sharedBrush;

        std::lock_guard<std::recursive_mutex> lk(m_mutex);

        ComPtr<ID2D1SolidColorBrush> brush;
        HRESULT hr = m_pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Black), &brush);
        
        if (SUCCEEDED(hr) && brush) {
            m_sharedBrush = brush;
            return m_sharedBrush;
        }

        return nullptr;
    }

    void UIManager::DiscardBrushCache() {
        std::lock_guard<std::recursive_mutex> lk(m_mutex);
        m_brushCache.clear(); // ComPtr 자동 해제
        m_sharedBrush.Reset();
    }

    IDWriteTextFormat* UIManager::GetOrCreateTextFormat(const std::string& id,
        const std::function<HRESULT(IDWriteFactory*, IDWriteTextFormat**)>& creator)
    {
        std::lock_guard<std::recursive_mutex> lk(m_mutex);

        auto it = m_textFormats.find(id);
        if (it != m_textFormats.end()) return it->second.Get();

        IDWriteTextFormat* fmt = nullptr;
        HRESULT hr = creator(g_pDWriteFactory.Get(), &fmt);
        if (SUCCEEDED(hr) && fmt) {
            ComPtr<IDWriteTextFormat> cfmt(fmt);
            m_textFormats.emplace(id, cfmt);
            return fmt;
        }
        SafeRelease((IUnknown*&)fmt);
        return nullptr;
    }

    void UIManager::DiscardTextFormats() {
        std::lock_guard<std::recursive_mutex> lk(m_mutex);
        m_textFormats.clear();
    }

    //----------------------------------------------------------------------------------------------------


    UIElement::UIElement(UIManager* manager, D2D1_RECT_F rect) : m_manager(manager), m_rect(rect) {}
    UIElement::~UIElement() {
        DiscardDeviceResources();
        if (m_manager && !m_manager->IsShuttingDown()) {
            m_manager->Unregister(this);
        }
    }

    void UIElement::OnCursorMove(D2D1_POINT_2F cursorPt) {
        m_isCursorOver = (cursorPt.x >= m_rect.left && cursorPt.x <= m_rect.right &&
            cursorPt.y >= m_rect.top && cursorPt.y <= m_rect.bottom);
    }
    void UIElement::OnLButtonDown(D2D1_POINT_2F cursorPt) {
        m_isPressed = false;
        m_isClicked = false;
        if (m_isCursorOver) m_isPressed = true;
    }
    void UIElement::OnLButtonUp(D2D1_POINT_2F cursorPt) {
        m_isClicked = (m_isPressed && m_isCursorOver);
        m_isPressed = false;
    }

}

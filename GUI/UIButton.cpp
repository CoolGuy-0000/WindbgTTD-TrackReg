#include "UIButton.h"

using namespace TimeTrackGUI;

static inline void SetBrushColorSafe(ID2D1SolidColorBrush* b, const D2D1_COLOR_F& c) {
    if (b) b->SetColor(c);
}

UIButton::UIButton(UIManager* manager, const std::wstring& text, D2D1_RECT_F rect, UINT32 id, UIButtonStyle style)
    : UIElement(manager, rect), m_style(style), m_text(text), m_id(id) {
}

UIButton::~UIButton() {
    DiscardDeviceResources();
}

void UIButton::CreateDeviceResources() {}
void UIButton::DiscardDeviceResources() {}

void UIButton::Render() {
    ID2D1HwndRenderTarget* rt = m_manager ? m_manager->GetRenderTarget() : nullptr;
    if (!rt) return;
    if (m_text.empty()) return;

    // 텍스트 포맷은 UIManager가 관리하도록 요청
    IDWriteTextFormat* fmt = m_manager->GetOrCreateTextFormat("Arial-18-Bold-Center",
        [](IDWriteFactory* f, IDWriteTextFormat** out) -> HRESULT {
            return f->CreateTextFormat(
                L"Arial",
                NULL,
                DWRITE_FONT_WEIGHT_BOLD,
                DWRITE_FONT_STYLE_NORMAL,
                DWRITE_FONT_STRETCH_NORMAL,
                18.0f,
                L"ko-kr",
                out
            );
        }
    );

    if (!fmt) return;

    // 효율 경로: 렌더타깃당 하나의 공유 브러시 사용
    auto sharedBrush = m_manager->GetOrCreateSharedBrush();
    if (!sharedBrush) return;

    D2D1_COLOR_F fillColor = m_isPressed ? m_style.activeColor : (m_isCursorOver ? m_style.hoverColor : m_style.normalColor);

    // 색상 변경 후 복구(RAII 스타일 수동 구현)
    D2D1_COLOR_F prev = sharedBrush->GetColor();
    sharedBrush->SetColor(fillColor);
    D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(m_rect, m_style.borderRadius, m_style.borderRadius);
    rt->FillRoundedRectangle(rr, sharedBrush.Get());
    // 텍스트
    sharedBrush->SetColor(m_style.textColor);
    rt->DrawText(m_text.c_str(), static_cast<UINT32>(m_text.length()), fmt, m_rect, sharedBrush.Get());
    // 복구
    sharedBrush->SetColor(prev);
}

void UIButton::OnCursorMove(D2D1_POINT_2F cursorPt) {
    UIElement::OnCursorMove(cursorPt);
}

void UIButton::OnLButtonDown(D2D1_POINT_2F cursorPt) {
    UIElement::OnLButtonDown(cursorPt);
}

void UIButton::OnLButtonUp(D2D1_POINT_2F cursorPt) {
    UIElement::OnLButtonUp(cursorPt);
    if (m_isClicked) {
        InvalidateRect(m_manager->GetGUIWnd()->GetHWND(), nullptr, FALSE);

        UIEvent ev;
        ev.type = UIEvent::Type::ButtonClicked;
        ev.source = this;

        m_manager->GetGUIWnd()->OnUIEvent(ev);
    }
}

void UIButton::SetText(const std::wstring& text) {
    m_text = text;
}
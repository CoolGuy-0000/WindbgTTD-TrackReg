#include "UIGraphNode.h"

using namespace TimeTrackGUI;

UIGraphNode::UIGraphNode(UIManager* manager, const std::wstring& text, D2D1_RECT_F rect, int id, UIGraphNodeStyle style)
    : UIElement(manager, rect), m_text(text), m_id(id), m_style(style) {
}

UIGraphNode::~UIGraphNode() {
    DiscardDeviceResources();
}

void UIGraphNode::CreateDeviceResources() {}
void UIGraphNode::DiscardDeviceResources() {}

void UIGraphNode::Render() {
    ID2D1HwndRenderTarget* rt = m_manager ? m_manager->GetRenderTarget() : nullptr;
    if (!rt) return;

    // Text format
    IDWriteTextFormat* fmt = m_manager->GetOrCreateTextFormat("Arial-12-Center",
        [](IDWriteFactory* f, IDWriteTextFormat** out) -> HRESULT {
            return f->CreateTextFormat(
                L"Arial",
                NULL,
                DWRITE_FONT_WEIGHT_NORMAL,
                DWRITE_FONT_STYLE_NORMAL,
                DWRITE_FONT_STRETCH_NORMAL,
                12.0f,
                L"ko-kr",
                out
            );
        }
    );

    if (!fmt) return;

    // We use the shared brush from manager
    auto sharedBrush = m_manager->GetOrCreateSharedBrush();
    if (!sharedBrush) return;

    D2D1_COLOR_F prev = sharedBrush->GetColor();

    // Fill
    sharedBrush->SetColor(m_style.fillColor);
    D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(m_rect, m_style.borderRadius, m_style.borderRadius);
    rt->FillRoundedRectangle(rr, sharedBrush.Get());

    // Border
    sharedBrush->SetColor(m_style.borderColor);
    rt->DrawRoundedRectangle(rr, sharedBrush.Get(), m_style.borderWidth);

    // Text
    // Center the text
    sharedBrush->SetColor(m_style.textColor);

    // We might want to measure text to center it vertically if needed, but DrawText with alignment handles it mostly.
    // For now simple draw.
    rt->DrawText(m_text.c_str(), static_cast<UINT32>(m_text.length()), fmt, m_rect, sharedBrush.Get());

    sharedBrush->SetColor(prev);
}

D2D1_POINT_2F UIGraphNode::GetCenter() const {
    return D2D1::Point2F(m_rect.left + (m_rect.right - m_rect.left) / 2.0f,
                         m_rect.top + (m_rect.bottom - m_rect.top) / 2.0f);
}

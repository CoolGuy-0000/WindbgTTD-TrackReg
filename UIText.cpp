#include "UIText.h"
#include <string> // std::to_string

using namespace TimeTrackGUI;

UIText::UIText(UIManager* manager, const std::wstring& text, D2D1_RECT_F rect, UINT32 id)
    : UIElement(manager, rect), m_text(text)
{
    m_id = id;
    m_color = D2D1::ColorF(D2D1::ColorF::White);
}

void UIText::Render() {
    ID2D1HwndRenderTarget* rt = m_manager ? m_manager->GetRenderTarget() : nullptr;
    if (!rt) return;
    if (m_text.empty()) return;

    // 1. 텍스트 포맷 키 생성 (속성에 따라 유니크한 키를 만듦)
    // 예: "Font_14.0_0" (Size_Align)
    // 실제로는 더 정교한 키 생성이 필요하지만 여기선 간단히 처리
    std::string key = "Font_" + std::to_string((int)m_fontSize);
    if (m_align == TextAlign::Center) key += "_C";
    else if (m_align == TextAlign::Right) key += "_R";
    else key += "_L";

    // 2. 포맷 가져오기
    IDWriteTextFormat* fmt = m_manager->GetOrCreateTextFormat(key,
        [&](IDWriteFactory* f, IDWriteTextFormat** out) -> HRESULT {
            HRESULT hr = f->CreateTextFormat(
                L"Arial", NULL,
                DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                m_fontSize, // 설정된 크기 사용
                L"ko-kr", out
            );

            if (SUCCEEDED(hr)) {
                // 정렬 설정
                if (m_align == TextAlign::Center) (*out)->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                else if (m_align == TextAlign::Right) (*out)->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
                else (*out)->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);

                (*out)->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER); // 수직은 중앙 고정
            }
            return hr;
        }
    );

    if (!fmt) return;

    auto sharedBrush = m_manager->GetOrCreateSharedBrush();
    if (!sharedBrush) return;

    D2D1_COLOR_F prev = sharedBrush->GetColor();

    // 3. 그리기 (배경 없이 글자만)
    sharedBrush->SetColor(m_color);
    rt->DrawText(m_text.c_str(), (UINT32)m_text.length(), fmt, m_rect, sharedBrush.Get());

    sharedBrush->SetColor(prev);
}
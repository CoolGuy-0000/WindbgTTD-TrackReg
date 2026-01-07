#include "UIButton.h"

using namespace TimeTrackGUI;

// 생성자 구현부: style 초기화 코드 제거
UIButton::UIButton(UIManager* manager, const std::wstring& text, D2D1_RECT_F rect, UINT32 id)
    : UIElement(manager, rect), m_text(text)
{
    m_id = id;
}

void UIButton::Render() {
    ID2D1HwndRenderTarget* rt = m_manager ? m_manager->GetRenderTarget() : nullptr;
    if (!rt) return;
    if (m_text.empty()) return;

    // 텍스트 포맷 생성 (기존 유지)
    IDWriteTextFormat* fmt = m_manager->GetOrCreateTextFormat("Arial-18-Bold-Center",
        [](IDWriteFactory* f, IDWriteTextFormat** out) -> HRESULT {
            HRESULT hr = f->CreateTextFormat(
                L"Arial",
                NULL,
                DWRITE_FONT_WEIGHT_BOLD,
                DWRITE_FONT_STYLE_NORMAL,
                DWRITE_FONT_STRETCH_NORMAL,
                18.0f,
                L"ko-kr",
                out
            );

            // 생성 성공 시, 정렬 속성 설정
            if (SUCCEEDED(hr)) {
                // 가로 중앙 정렬
                (*out)->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                // 세로 중앙 정렬
                (*out)->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            }

            return hr;
        }
    );

    if (!fmt) return;

    auto sharedBrush = m_manager->GetOrCreateSharedBrush();
    if (!sharedBrush) return;

    // ==========================================
    // [수정됨] 여기에 기본 스타일을 정의합니다.
    // ==========================================
    const static D2D1_COLOR_F colorNormal = D2D1::ColorF(0.2f, 0.2f, 0.2f, 1.0f); // 평상시 (진한 회색)
    const static D2D1_COLOR_F colorHover = D2D1::ColorF(0.3f, 0.3f, 0.3f, 1.0f); // 마우스 오버 (조금 밝은 회색)
    const static D2D1_COLOR_F colorActive = D2D1::ColorF(0.1f, 0.1f, 0.1f, 1.0f); // 클릭 시 (더 진한 회색)
    const static D2D1_COLOR_F colorText = D2D1::ColorF(D2D1::ColorF::White);    // 텍스트 색상 (흰색)
    const static float borderRadius = 4.0f;                                       // 둥근 모서리

    // 상태에 따른 배경색 결정
    D2D1_COLOR_F fillColor = m_isPressed ? colorActive : (m_isCursorOver ? colorHover : colorNormal);

    // 색상 변경 및 그리기
    D2D1_COLOR_F prev = sharedBrush->GetColor();

    // 배경 그리기
    sharedBrush->SetColor(fillColor);
    D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(m_rect, borderRadius, borderRadius);
    rt->FillRoundedRectangle(rr, sharedBrush.Get());

    // 텍스트 그리기
    sharedBrush->SetColor(colorText);

    // 텍스트 중앙 정렬을 위해 포맷 설정 확인 (이미 Center로 생성했으므로 그리기만 하면 됨)
    rt->DrawText(m_text.c_str(), static_cast<UINT32>(m_text.length()), fmt, m_rect, sharedBrush.Get());

    // 브러시 복구
    sharedBrush->SetColor(prev);
}

LRESULT UIButton::WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    auto res = UIElement::WndProc(hWnd, message, wParam, lParam);

    switch (message) {
        case WM_LBUTTONDOWN: {
            if (!m_isClicked && m_bUpdateState) {
                m_bUpdateState = false;
                InvalidateRect(hWnd, NULL, FALSE);
            }
            return WndProc_Success;
        }
        case WM_LBUTTONUP: {
            if (m_isClicked) {
                InvalidateRect(hWnd, NULL, FALSE);

				m_manager->m_EventMsg.type = UIEventType::ButtonClick;
				m_manager->m_EventMsg.source = this;

                SendMessage(hWnd, WM_TTGUI_COMMAND, 0, 0);
                m_bUpdateState = true;
            }
            return WndProc_Success;
        }
    }

    return res;
}

void UIButton::SetText(const std::wstring& text) {
    m_text = text;
}
#include "UICheckBox.h"

using namespace TimeTrackGUI;

// 생성자
UICheckBox::UICheckBox(UIManager* manager, const std::wstring& text, D2D1_RECT_F rect, UINT32 id, bool initiallyChecked)
    : UIElement(manager, rect), m_text(text), m_isChecked(initiallyChecked)
{
    m_id = id;
}

// 렌더링 구현 (핵심)
void UICheckBox::Render() {
    ID2D1HwndRenderTarget* rt = m_manager ? m_manager->GetRenderTarget() : nullptr;
    if (!rt) return;

    // --- 레이아웃 상수 및 스타일 정의 (예쁘게 하드코딩) ---
    const float boxSize = 18.0f;        // 체크박스 크기
    const float textPadding = 8.0f;     // 박스와 텍스트 사이 간격
    const float borderRadius = 3.0f;    // 박스 모서리 둥글기

    // 색상 팔레트
    const D2D1_COLOR_F colorBorderNormal = D2D1::ColorF(0.5f, 0.5f, 0.5f, 1.0f); // 회색 테두리
    const D2D1_COLOR_F colorBorderHover = D2D1::ColorF(0.3f, 0.7f, 1.0f, 1.0f); // 호버시 파란 테두리
    const D2D1_COLOR_F colorBgChecked = D2D1::ColorF(0.3f, 0.7f, 1.0f, 1.0f); // 체크시 파란 배경
    const D2D1_COLOR_F colorBgHover = D2D1::ColorF(0.9f, 0.9f, 0.9f, 0.5f); // 호버시 연한 회색 배경
    const D2D1_COLOR_F colorCheckMark = D2D1::ColorF(D2D1::ColorF::White);    // 흰색 체크 표시
    const D2D1_COLOR_F colorText = D2D1::ColorF(D2D1::ColorF::Black);    // 검은색 텍스트

    // 텍스트 포맷 가져오기 (왼쪽 정렬, 수직 중앙 정렬)
    IDWriteTextFormat* fmt = m_manager->GetOrCreateTextFormat("Arial-16-Reg-Left",
        [](IDWriteFactory* f, IDWriteTextFormat** out) -> HRESULT {
            HRESULT hr = f->CreateTextFormat(L"Arial", NULL, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                16.0f, L"ko-kr", out);
            if (SUCCEEDED(hr)) {
                (*out)->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING); // 왼쪽 정렬
                (*out)->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER); // 수직 중앙
            }
            return hr;
        });

    auto sharedBrush = m_manager->GetOrCreateSharedBrush();
    if (!rt || !fmt || !sharedBrush) return;

    D2D1_COLOR_F prevColor = sharedBrush->GetColor();

    // --- 형상 계산 ---
    // 전체 영역의 수직 중앙 계산
    float centerY = m_rect.top + (m_rect.bottom - m_rect.top) / 2.0f;

    // 1. 체크박스 사각형 영역 계산 (왼쪽에 위치)
    D2D1_RECT_F boxRect;
    boxRect.left = m_rect.left;
    boxRect.top = centerY - (boxSize / 2.0f);
    boxRect.right = boxRect.left + boxSize;
    boxRect.bottom = boxRect.top + boxSize;

    // 2. 텍스트 영역 계산 (박스 오른쪽)
    D2D1_RECT_F textRect = m_rect;
    textRect.left = boxRect.right + textPadding;


    // --- 그리기 시작 ---

    // 1. 박스 배경 그리기
    if (m_isChecked) {
        sharedBrush->SetColor(colorBgChecked);
        rt->FillRoundedRectangle(D2D1::RoundedRect(boxRect, borderRadius, borderRadius), sharedBrush.Get());
    }
    else if (m_isCursorOver) {
        sharedBrush->SetColor(colorBgHover);
        rt->FillRoundedRectangle(D2D1::RoundedRect(boxRect, borderRadius, borderRadius), sharedBrush.Get());
    }

    // 2. 박스 테두리 그리기
    sharedBrush->SetColor((m_isCursorOver || m_isChecked) ? colorBorderHover : colorBorderNormal);
    rt->DrawRoundedRectangle(D2D1::RoundedRect(boxRect, borderRadius, borderRadius), sharedBrush.Get(), 1.5f);

    // 3. 체크 표시 그리기 (체크된 경우만)
    if (m_isChecked) {
        sharedBrush->SetColor(colorCheckMark);
        DrawCheckMark(rt, sharedBrush.Get(), boxRect);
    }

    // 4. 텍스트 그리기
    if (!m_text.empty()) {
        sharedBrush->SetColor(colorText);
        rt->DrawText(m_text.c_str(), static_cast<UINT32>(m_text.length()), fmt, textRect, sharedBrush.Get());
    }

    // 브러시 색상 복구
    sharedBrush->SetColor(prevColor);
}

// V자 체크 표시를 그리는 헬퍼 함수
void UICheckBox::DrawCheckMark(ID2D1HwndRenderTarget* rt, ID2D1SolidColorBrush* brush, D2D1_RECT_F boxRect) {
    // 박스 내부 좌표를 기준으로 V자 모양의 세 점을 정의합니다.
    // 비율을 사용하여 박스 크기가 변해도 비율이 유지되도록 합니다.
    float width = boxRect.right - boxRect.left;
    float height = boxRect.bottom - boxRect.top;

    D2D1_POINT_2F p1 = D2D1::Point2F(boxRect.left + width * 0.2f, boxRect.top + height * 0.5f); // 시작점 (중간 좌측)
    D2D1_POINT_2F p2 = D2D1::Point2F(boxRect.left + width * 0.45f, boxRect.top + height * 0.75f);// 꺾이는 점 (하단 중앙 약간 우측)
    D2D1_POINT_2F p3 = D2D1::Point2F(boxRect.left + width * 0.8f, boxRect.top + height * 0.25f); // 끝점 (상단 우측)

    // 두 개의 선으로 V자를 그립니다.
    rt->DrawLine(p1, p2, brush, 2.0f, NULL); // 두께 2.0f
    rt->DrawLine(p2, p3, brush, 2.0f, NULL);
}


LRESULT UICheckBox::WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    // 기본 UIElement 처리 (호버 상태 등 업데이트)
    auto res = UIElement::WndProc(hWnd, message, wParam, lParam);

    switch (message) {
    case WM_LBUTTONUP: {
        // 클릭되었을 때
        if (m_isClicked) {
            // 1. 상태 토글
            m_isChecked = !m_isChecked;

            // 2. 화면 다시 그리기 요청
            InvalidateRect(hWnd, NULL, FALSE);

            // 3. 부모 윈도우에 알림 (이벤트 타입을 CheckBoxChange로 가정)
            // TimeTrackGUI.h 에 UIEventType::CheckBoxChange 가 추가되어야 합니다.
            SendMessage(hWnd, WM_TTGUI_COMMAND, (WPARAM)this, (LPARAM)UIEventType::CheckBoxChange);
        }
        return WndProc_Success;
    }
    }
    return res;
}

void UICheckBox::SetChecked(bool checked) {
    if (m_isChecked != checked) {
        m_isChecked = checked;
        // 상태가 변하면 다시 그리기 위해 Invalidate 필요 (HWND 접근이 필요하므로 실제로는 Manager를 통해 호출하는 것이 좋음)
        // 현재 구조상으로는 다음 렌더링 루프에서 반영되거나, 명시적으로 InvalidateRect를 호출해야 합니다.
    }
}

void UICheckBox::SetText(const std::wstring& text) {
    m_text = text;
}
#include <windowsx.h>
#include "UISlider.h"

using namespace TimeTrackGUI;

LRESULT UISlider::WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    auto res = UIElement::WndProc(hWnd, message, wParam, lParam);

    switch (message) {
        case WM_MOUSEMOVE: {
            if (m_isPressed) {
                D2D1_POINT_2F pt = D2D1::Point2F(static_cast<FLOAT>(GET_X_LPARAM(lParam)), GET_Y_LPARAM(lParam));

                // 마우스 위치에 따라 value 계산 (0.0 ~ 1.0)
                float width = m_rect.right - m_rect.left;
                m_value = (pt.x - m_rect.left) / width;

                // 범위 제한 (Clamping)
                if (m_value < 0.0f) m_value = 0.0f;
                if (m_value > 1.0f) m_value = 1.0f;

                UpdateHandleRect();
                InvalidateRect(m_manager->GetGUIWnd()->GetHWND(), NULL, FALSE);
            }

            return WndProc_Success;
        }
        case WM_LBUTTONDOWN: {
            D2D1_POINT_2F pt = D2D1::Point2F(static_cast<FLOAT>(GET_X_LPARAM(lParam)), GET_Y_LPARAM(lParam));

            if (pt.x >= m_rect.left && pt.x <= m_rect.right &&
                pt.y >= m_rect.top && pt.y <= m_rect.bottom) {
                m_isPressed = true;
            }

            SendMessage(hWnd, WM_MOUSEMOVE, NULL, lParam);
            return WndProc_Success;
        }
        case WM_LBUTTONUP: {
            m_isPressed = false;
            return WndProc_Success;
        }
    }

    return res;
}

void UISlider::Render() {
    ID2D1HwndRenderTarget* rt = m_manager ? m_manager->GetRenderTarget() : nullptr;
    auto sharedBrush = m_manager ? m_manager->GetOrCreateSharedBrush() : nullptr;
    if (!rt || !sharedBrush) return;

    D2D1_COLOR_F prevColor = sharedBrush->GetColor();

    // --- 디자인 설정값 ---
    D2D1_COLOR_F trackBgColor = D2D1::ColorF(0.25f, 0.25f, 0.25f); // 어두운 회색 (빈 구간)
    D2D1_COLOR_F activeColor = D2D1::ColorF(0.0f, 0.47f, 0.84f);   // 윈도우 블루 (채워진 구간)
    D2D1_COLOR_F handleColor = m_isPressed ? D2D1::ColorF(1.0f, 1.0f, 1.0f) : D2D1::ColorF(0.9f, 0.9f, 0.9f); // 핸들 색상

    float trackHeight = 4.0f;
    float centerY = m_rect.top + (m_rect.bottom - m_rect.top) / 2.0f;
    float currentX = m_rect.left + (m_rect.right - m_rect.left) * m_value;

    // 1. 전체 트랙 배경 (빈 구간) 그리기 - 둥근 모서리
    sharedBrush->SetColor(trackBgColor);
    D2D1_RECT_F bgRect = D2D1::RectF(m_rect.left, centerY - (trackHeight / 2), m_rect.right, centerY + (trackHeight / 2));
    rt->FillRoundedRectangle(D2D1::RoundedRect(bgRect, 2.0f, 2.0f), sharedBrush.Get());

    // 2. 채워진 트랙 (Active Track) 그리기 - 값(value)만큼만
    sharedBrush->SetColor(activeColor);
    D2D1_RECT_F fillRect = D2D1::RectF(m_rect.left, centerY - (trackHeight / 2), currentX, centerY + (trackHeight / 2));
    rt->FillRoundedRectangle(D2D1::RoundedRect(fillRect, 2.0f, 2.0f), sharedBrush.Get());

    // 3. 핸들 그리기 (원형)
    float handleRadius = 8.0f; // UpdateHandleRect의 크기와 맞춰주세요
    // 마우스가 올라갔거나 눌렸을 때 핸들을 조금 더 키워주는 효과
    if (m_isPressed || m_isCursorOver) handleRadius += 1.0f;

    D2D1_ELLIPSE handleShape = D2D1::Ellipse(D2D1::Point2F(currentX, centerY), handleRadius, handleRadius);

    // 3-1. 핸들 내부 채우기
    sharedBrush->SetColor(handleColor);
    rt->FillEllipse(handleShape, sharedBrush.Get());

    // 3-2. 핸들 테두리 (Stroke) - 배경과 구분감을 줌
    // 채워진 색과 같은 색을 쓰거나, 흰색 등을 사용
    sharedBrush->SetColor(activeColor);
    rt->DrawEllipse(handleShape, sharedBrush.Get(), 1.5f); // 두께 1.5f

    // 브러시 원상복구
    sharedBrush->SetColor(prevColor);
}

void UISlider::UpdateHandleRect() {
    float width = m_rect.right - m_rect.left;
    float centerX = m_rect.left + (width * m_value);

    // 핸들을 원형으로 그릴 것이므로, 중심점과 반지름 개념으로 생각합니다.
    // 여기서는 클릭 판정을 위한 사각형(Rect)만 잡아줍니다.
    float handleRadius = 8.0f; // 핸들 반지름 (크기 조절)

    m_handleRect = D2D1::RectF(
        centerX - handleRadius,
        (m_rect.top + m_rect.bottom) / 2.0f - handleRadius,
        centerX + handleRadius,
        (m_rect.top + m_rect.bottom) / 2.0f + handleRadius
    );

    // 터치/클릭 편의성을 위해 판정 영역(m_handleRect)을 조금 더 크게 잡아도 좋습니다.
}
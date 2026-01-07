#include "UIComboBox.h"

using namespace TimeTrackGUI;

UIComboBox::UIComboBox(UIManager* manager, D2D1_RECT_F rect, UINT32 id)
    : UIElement(manager, rect)
{
    m_id = id;
}

void UIComboBox::AddItem(const std::wstring& item) {
    m_items.push_back(item);
    // 첫 아이템 추가 시 자동으로 선택
    if (m_selectedIndex == -1) {
        m_selectedIndex = 0;
    }
}

void UIComboBox::ClearItems() {
    m_items.clear();
    m_selectedIndex = -1;
    m_isExpanded = false;
}

void UIComboBox::SetSelectedIndex(int index) {
    if (index >= -1 && index < (int)m_items.size()) {
        m_selectedIndex = index;
    }
}

std::wstring UIComboBox::GetSelectedText() const {
    if (m_selectedIndex >= 0 && m_selectedIndex < m_items.size()) {
        return m_items[m_selectedIndex];
    }
    return L"";
}

D2D1_RECT_F UIComboBox::GetDropdownRect() const {
    // 메인 박스 바로 아래에 위치
    float height = m_items.size() * m_itemHeight;
    return D2D1::RectF(m_rect.left, m_rect.bottom, m_rect.right, m_rect.bottom + height);
}

void UIComboBox::Render() {
    ID2D1HwndRenderTarget* rt = m_manager ? m_manager->GetRenderTarget() : nullptr;
    if (!rt) return;

    // 텍스트 포맷 (왼쪽 정렬)
    IDWriteTextFormat* fmt = m_manager->GetOrCreateTextFormat("Arial-14-Reg-Left",
        [](IDWriteFactory* f, IDWriteTextFormat** out) {
            HRESULT hr = f->CreateTextFormat(L"Arial", NULL, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 14.0f, L"ko-kr", out);
            if (SUCCEEDED(hr)) {
                (*out)->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
                (*out)->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            }
            return hr;
        });

    auto sharedBrush = m_manager->GetOrCreateSharedBrush();
    if (!sharedBrush || !fmt) return;

    // --- 스타일 정의 ---
    const static D2D1_COLOR_F colorBgNormal = D2D1::ColorF(0.2f, 0.2f, 0.2f, 1.0f);
    const static D2D1_COLOR_F colorBgHover = D2D1::ColorF(0.25f, 0.25f, 0.25f, 1.0f);
    const static D2D1_COLOR_F colorBorder = D2D1::ColorF(0.4f, 0.4f, 0.4f, 1.0f);
    const static D2D1_COLOR_F colorText = D2D1::ColorF(0.9f, 0.9f, 0.9f, 1.0f);
    const static D2D1_COLOR_F colorListBg = D2D1::ColorF(0.18f, 0.18f, 0.18f, 1.0f); // 리스트 배경
    const static D2D1_COLOR_F colorItemHover = D2D1::ColorF(0.0f, 0.47f, 0.84f, 1.0f);  // 리스트 아이템 호버 (파랑)

    D2D1_COLOR_F prevColor = sharedBrush->GetColor();

    // 1. 메인 헤더 박스 그리기
    D2D1_COLOR_F currentBg = (m_isCursorOver || m_isExpanded) ? colorBgHover : colorBgNormal;
    sharedBrush->SetColor(currentBg);
    rt->FillRoundedRectangle(D2D1::RoundedRect(m_rect, 4.0f, 4.0f), sharedBrush.Get());

    sharedBrush->SetColor(colorBorder);
    rt->DrawRoundedRectangle(D2D1::RoundedRect(m_rect, 4.0f, 4.0f), sharedBrush.Get());

    // 2. 현재 선택된 텍스트
    D2D1_RECT_F textRect = m_rect;
    textRect.left += 8.0f; // 패딩
    textRect.right -= 24.0f; // 화살표 공간 확보

    sharedBrush->SetColor(colorText);
    if (m_selectedIndex >= 0 && m_selectedIndex < m_items.size()) {
        rt->DrawText(m_items[m_selectedIndex].c_str(), (UINT32)m_items[m_selectedIndex].length(), fmt, textRect, sharedBrush.Get());
    }

    // 3. 화살표 그리기
    D2D1_RECT_F arrowRect = D2D1::RectF(m_rect.right - 24.0f, m_rect.top, m_rect.right, m_rect.bottom);
    DrawArrow(rt, sharedBrush.Get(), arrowRect);

    // 4. 드롭다운 리스트 그리기 (펼쳐졌을 때만)
    if (m_isExpanded) {
        D2D1_RECT_F listRect = GetDropdownRect();

        // 리스트 배경
        sharedBrush->SetColor(colorListBg);
        rt->FillRectangle(listRect, sharedBrush.Get());

        // 리스트 테두리
        sharedBrush->SetColor(colorBorder);
        rt->DrawRectangle(listRect, sharedBrush.Get());

        // 리스트 아이템들
        float currentY = listRect.top;

        // 현재 마우스 위치 파악 (호버 효과용)
        // 주의: Render 내부에서 GetCursorPos를 호출하는 것은 약간 비효율적일 수 있으나, 
        // 간단한 구현을 위해 사용하거나, WndProc에서 저장한 위치를 사용해야 함.
        // 여기서는 WndProc 구조상 정확한 마우스 위치를 멤버로 가지고 있다고 가정하기 어려우므로,
        // 간단히 그리기만 하고 호버 색상은 WndProc에서 처리된 Invalidate로 갱신됨을 가정.

        // *팁*: 더 정교한 호버를 위해 UIElement에 m_lastMouseX, m_lastMouseY를 저장하는 것이 좋습니다.
        // 여기서는 편의상 생략하고, 항목 텍스트만 그립니다.

        for (int i = 0; i < m_items.size(); ++i) {
            D2D1_RECT_F itemRect = D2D1::RectF(listRect.left, currentY, listRect.right, currentY + m_itemHeight);

            // 텍스트
            D2D1_RECT_F itemTextRect = itemRect;
            itemTextRect.left += 8.0f;

            sharedBrush->SetColor(colorText);
            rt->DrawText(m_items[i].c_str(), (UINT32)m_items[i].length(), fmt, itemTextRect, sharedBrush.Get());

            // 구분선 (옵션)
            // sharedBrush->SetColor(D2D1::ColorF(0.3f, 0.3f, 0.3f));
            // rt->DrawLine(D2D1::Point2F(itemRect.left, itemRect.bottom), D2D1::Point2F(itemRect.right, itemRect.bottom), sharedBrush.Get());

            currentY += m_itemHeight;
        }

        // 현재 선택된 아이템에 대한 하이라이트 박스 (선택적 구현)
        if (m_selectedIndex >= 0) {
            D2D1_RECT_F selectedRect = D2D1::RectF(listRect.left, listRect.top + (m_selectedIndex * m_itemHeight), listRect.right, listRect.top + ((m_selectedIndex + 1) * m_itemHeight));
            sharedBrush->SetColor(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.1f)); // 살짝 밝게
            rt->DrawRectangle(selectedRect, sharedBrush.Get());
        }
    }

    sharedBrush->SetColor(prevColor);
}

void UIComboBox::DrawArrow(ID2D1HwndRenderTarget* rt, ID2D1SolidColorBrush* brush, D2D1_RECT_F rect) {
    float cx = (rect.left + rect.right) / 2.0f;
    float cy = (rect.top + rect.bottom) / 2.0f;
    float size = 5.0f;

    // V자 모양
    brush->SetColor(D2D1::ColorF(0.8f, 0.8f, 0.8f));
    rt->DrawLine(D2D1::Point2F(cx - size, cy - 2), D2D1::Point2F(cx, cy + size), brush, 2.0f);
    rt->DrawLine(D2D1::Point2F(cx, cy + size), D2D1::Point2F(cx + size, cy - 2), brush, 2.0f);
}

LRESULT UIComboBox::WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    // 기본 처리 (헤더 영역에 대한 호버 등)
    auto res = UIElement::WndProc(hWnd, message, wParam, lParam);

    // 마우스 좌표
    int x = (short)LOWORD(lParam);
    int y = (short)HIWORD(lParam);
    D2D1_POINT_2F pt = D2D1::Point2F((float)x, (float)y);

    switch (message) {
        case WM_LBUTTONUP: {
            // 1. 헤더(메인 박스) 클릭 확인

            if (m_isClicked) {
                m_isExpanded = !m_isExpanded; // 토글
                InvalidateRect(hWnd, NULL, FALSE);
                return WndProc_Success; // 처리됨
            }

            // 2. 리스트(드롭다운) 클릭 확인 (펼쳐져 있을 때만)
            if (m_isExpanded) {
                D2D1_RECT_F listRect = GetDropdownRect();
                bool clickedList = (pt.x >= listRect.left && pt.x <= listRect.right && pt.y >= listRect.top && pt.y <= listRect.bottom);

                if (clickedList) {
                    // 어떤 아이템을 눌렀는지 계산
                    float relativeY = pt.y - listRect.top;
                    int index = (int)(relativeY / m_itemHeight);

                    if (index >= 0 && index < m_items.size()) {
                        m_selectedIndex = index;
                        m_isExpanded = false; // 선택하면 닫기

                        // 변경 알림
                        SendMessage(hWnd, WM_TTGUI_COMMAND, (WPARAM)this, (LPARAM)UIEventType::ButtonClick); // ComboBoxChange 이벤트를 추가하는 게 좋음
                        InvalidateRect(hWnd, NULL, FALSE);
                    }
                }
                else {
                    // 헤더도 아니고 리스트도 아닌 곳을 클릭 -> 닫기
                    m_isExpanded = false;
                    InvalidateRect(hWnd, NULL, FALSE);
                }
            }
            break;
        }
    }

    return res;
}
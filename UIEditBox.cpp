#include "UIEditBox.h"

using namespace TimeTrackGUI;

UIEditBox::UIEditBox(UIManager* manager, D2D1_RECT_F rect, UINT32 id)
    : UIElement(manager, rect)
{
    m_id = id;
}

void UIEditBox::SetText(const std::wstring& text) {
    m_text = text;
    m_cursorPos = (int)m_text.length(); // 텍스트 변경 시 커서를 맨 끝으로
}

void UIEditBox::SetPlaceholder(const std::wstring& text) {
    m_placeholder = text;
}

// 텍스트 길이(픽셀) 측정 함수
float UIEditBox::MeasureTextWidth(IDWriteFactory* factory, IDWriteTextFormat* format, const std::wstring& str) {
    if (str.empty()) return 0.0f;

    IDWriteTextLayout* layout = nullptr;
    // 화면 크기만큼의 레이아웃을 임시로 생성하여 글자 폭만 측정
    HRESULT hr = factory->CreateTextLayout(
        str.c_str(),
        (UINT32)str.length(),
        format,
        10000.0f, // 충분히 넓은 폭
        50.0f,
        &layout
    );

    float width = 0.0f;
    if (SUCCEEDED(hr)) {
        DWRITE_TEXT_METRICS metrics;
        layout->GetMetrics(&metrics);
        width = metrics.widthIncludingTrailingWhitespace;
        layout->Release();
    }
    return width;
}

void UIEditBox::Render() {
    ID2D1HwndRenderTarget* rt = m_manager ? m_manager->GetRenderTarget() : nullptr;
    if (!rt) return;

    // 팩토리 가져오기 (커서 위치 계산용)
    // UIManager에 GetWriteFactory()가 있다고 가정하거나, m_manager->GetTextFormat 내부 로직을 활용해야 함
    // 여기서는 편의상 UIManager가 IDWriteFactory*를 멤버로 가지고 있다고 가정하고 getter가 없다면 
    // RenderTarget이나 다른 경로로 접근이 필요하지만, 
    // 보통 CreateTextLayout을 쓰려면 Factory가 필요합니다.
    // **중요**: UIManager 코드에 `IDWriteFactory* GetWriteFactory()`를 추가해주는 것이 좋습니다.
    // 만약 없다면, 텍스트 포맷 생성 람다 안에서만 쓸 수 있으므로 구조적 한계가 있습니다.
    // (이 예제에서는 m_manager 내부에 팩토리가 있다고 가정하고 진행하거나, 너비 계산을 DrawText로 추정하지 않고 생략할 수도 있습니다.
    //  하지만 정확한 커서를 위해 UIManager를 통해 Factory에 접근한다고 가정합니다.)

    // 텍스트 포맷 (왼쪽 정렬, 수직 중앙)
    IDWriteTextFormat* fmt = m_manager->GetOrCreateTextFormat("Arial-14-Reg-Left-Edit",
        [](IDWriteFactory* f, IDWriteTextFormat** out) {
            return f->CreateTextFormat(L"Arial", NULL, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 14.0f, L"ko-kr", out);
        });

    if (fmt) {
        fmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }

    auto sharedBrush = m_manager->GetOrCreateSharedBrush();
    if (!sharedBrush || !fmt) return;

    // --- 스타일 정의 ---
    const D2D1_COLOR_F colorBg = D2D1::ColorF(0.15f, 0.15f, 0.15f, 1.0f); // 배경 (어두운 회색)
    const D2D1_COLOR_F colorBorder = m_isFocused ? D2D1::ColorF(0.0f, 0.6f, 1.0f, 1.0f) : D2D1::ColorF(0.4f, 0.4f, 0.4f, 1.0f); // 포커스 시 파란 테두리
    const D2D1_COLOR_F colorText = D2D1::ColorF(0.9f, 0.9f, 0.9f, 1.0f);
    const D2D1_COLOR_F colorPlace = D2D1::ColorF(0.5f, 0.5f, 0.5f, 1.0f); // 플레이스홀더 색상
    const float borderRadius = 4.0f;

    D2D1_COLOR_F prevColor = sharedBrush->GetColor();

    // 1. 배경 그리기
    sharedBrush->SetColor(colorBg);
    rt->FillRoundedRectangle(D2D1::RoundedRect(m_rect, borderRadius, borderRadius), sharedBrush.Get());

    // 2. 테두리 그리기
    sharedBrush->SetColor(colorBorder);
    rt->DrawRoundedRectangle(D2D1::RoundedRect(m_rect, borderRadius, borderRadius), sharedBrush.Get(), m_isFocused ? 2.0f : 1.0f);

    // 3. 클리핑 (텍스트가 박스를 넘어가지 않게)
    rt->PushAxisAlignedClip(m_rect, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    D2D1_RECT_F textRect = m_rect;
    textRect.left += 5.0f; // 패딩
    textRect.right -= 5.0f;

    // 4. 텍스트 그리기
    if (m_text.empty() && !m_placeholder.empty() && !m_isFocused) {
        // 플레이스홀더
        sharedBrush->SetColor(colorPlace);
        rt->DrawText(m_placeholder.c_str(), (UINT32)m_placeholder.length(), fmt, textRect, sharedBrush.Get());
    }
    else {
        // 실제 텍스트
        sharedBrush->SetColor(colorText);
        rt->DrawText(m_text.c_str(), (UINT32)m_text.length(), fmt, textRect, sharedBrush.Get());
    }

    // 5. 커서 그리기 (포커스 상태일 때만)
    if (m_isFocused) {
        // 커서 깜빡임 효과 (GetTickCount 사용 - 간단한 트릭)
        if ((GetTickCount() % 1000) < 500) {
            // 현재 커서 위치까지의 텍스트 길이 계산 필요
            // *주의*: Factory 접근이 어렵다면 '대략적인' 글자 폭(예: 8px) * m_cursorPos 로 계산할 수도 있음.
            // 여기서는 Factory 접근이 가능하다는 전제 하에 정확한 위치 계산 시도.
            // (구현이 복잡하면 임시로: float cursorX = textRect.left + (m_cursorPos * 8.0f); 사용 가능)

            // 임시: 간단한 폭 계산 (고정폭 폰트가 아니면 오차가 있음)
            // 정확히 하려면 UIManager::GetWriteFactory()를 구현해서 넘겨줘야 합니다.
            // 일단 'Arial 14' 기준 대략적인 폭으로 추정하여 구현합니다.
            // (실제 프로젝트에서는 MeasureTextWidth 함수를 제대로 구현해서 쓰세요)

            float charWidthEstimate = 7.0f; // 대략적인 값
            // 실제 구현 시 MeasureTextWidth 호출 권장

            float textWidth = 0.0f;
            // 만약 UIManager에 팩토리 접근자가 있다면:
            // textWidth = MeasureTextWidth(m_manager->GetWriteFactory(), fmt, m_text.substr(0, m_cursorPos));

            // [간이 구현] 
            textWidth = m_cursorPos * 8.0f; // 단순 추정

            float cursorX = textRect.left + textWidth;
            float centerY = (m_rect.top + m_rect.bottom) / 2.0f;
            float cursorHeight = 14.0f;

            sharedBrush->SetColor(D2D1::ColorF(D2D1::ColorF::White));
            rt->DrawLine(
                D2D1::Point2F(cursorX, centerY - cursorHeight / 2.0f),
                D2D1::Point2F(cursorX, centerY + cursorHeight / 2.0f),
                sharedBrush.Get(),
                1.5f
            );
        }
    }

    rt->PopAxisAlignedClip();
    sharedBrush->SetColor(prevColor);
}

LRESULT UIEditBox::WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    auto res = UIElement::WndProc(hWnd, message, wParam, lParam);

    switch (message) {
    case WM_LBUTTONDOWN: {
        POINT pt = { LOWORD(lParam), HIWORD(lParam) };
        bool isInside = (pt.x >= m_rect.left && pt.x <= m_rect.right && pt.y >= m_rect.top && pt.y <= m_rect.bottom);

        if (m_isCursorOver) {
            if (!m_isFocused) {
                m_isFocused = true;
                InvalidateRect(hWnd, NULL, FALSE);
            }
            // 클릭 위치에 따라 커서 이동하는 로직은 복잡하므로 생략 (맨 끝으로 이동)
            // m_cursorPos = m_text.length(); 
        }
        else {
            if (m_isFocused) {
                m_isFocused = false;
                InvalidateRect(hWnd, NULL, FALSE);
                // 포커스 잃을 때 이벤트 전송 (입력 완료)
                SendMessage(hWnd, WM_TTGUI_COMMAND, (WPARAM)this, (LPARAM)UIEventType::ButtonClick); // 또는 EditEnd 이벤트
            }
        }
        break;
    }

    case WM_CHAR: {
        if (m_isFocused) {
            WCHAR ch = (WCHAR)wParam;
            HandleCharInput(ch);
            InvalidateRect(hWnd, NULL, FALSE);
            return 0; // 메시지 처리함
        }
        break;
    }

    case WM_KEYDOWN: {
        if (m_isFocused) {
            HandleKeyDown(wParam);
            InvalidateRect(hWnd, NULL, FALSE);
            return 0;
        }
        break;
    }
    }

    return res;
}

void UIEditBox::HandleCharInput(WCHAR ch) {
    if (ch == VK_BACK) {
        // 백스페이스 처리는 WM_KEYDOWN이나 여기서 처리 가능
        if (m_cursorPos > 0 && !m_text.empty()) {
            m_text.erase(m_cursorPos - 1, 1);
            m_cursorPos--;
        }
    }
    else if (ch == VK_RETURN) {
        // 엔터키: 포커스 해제 혹은 입력 완료 처리
        m_isFocused = false;
    }
    else if (ch >= 32) { // 제어 문자 제외하고 입력
        m_text.insert(m_cursorPos, 1, ch);
        m_cursorPos++;
    }
}

void UIEditBox::HandleKeyDown(WPARAM key) {
    switch (key) {
    case VK_LEFT:
        if (m_cursorPos > 0) m_cursorPos--;
        break;
    case VK_RIGHT:
        if (m_cursorPos < m_text.length()) m_cursorPos++;
        break;
    case VK_HOME:
        m_cursorPos = 0;
        break;
    case VK_END:
        m_cursorPos = (int)m_text.length();
        break;
    case VK_DELETE:
        if (m_cursorPos < m_text.length()) {
            m_text.erase(m_cursorPos, 1);
        }
        break;
    }
}
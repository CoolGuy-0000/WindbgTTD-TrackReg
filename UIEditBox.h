#pragma once
#include "TimeTrackGUI.h"
#include <string>
#include <dwrite.h> // 텍스트 레이아웃 계산용

namespace TimeTrackGUI {

    class UIEditBox : public UIElement {
    public:
        UIEditBox(UIManager* manager, D2D1_RECT_F rect, UINT32 id);
        ~UIEditBox() = default;

        void Render() override;
        LRESULT WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) override;

        // 텍스트 조작
        void SetText(const std::wstring& text);
        std::wstring GetText() const { return m_text; }

        // 플레이스홀더 (비어있을 때 흐리게 보이는 글자)
        void SetPlaceholder(const std::wstring& text);

    private:
        std::wstring m_text;
        std::wstring m_placeholder;

        bool m_isFocused = false;   // 현재 입력 포커스를 가지고 있는지
        int m_cursorPos = 0;        // 현재 커서 위치 (글자 인덱스)
        float m_scrollOffset = 0.0f;// 텍스트가 길어질 경우 스크롤 (간이 구현)

        // 내부 헬퍼: 키 입력 처리
        void HandleCharInput(WCHAR ch);
        void HandleKeyDown(WPARAM key);

        // 내부 헬퍼: 텍스트 폭 계산 (커서 위치 잡기용)
        float MeasureTextWidth(IDWriteFactory* factory, IDWriteTextFormat* format, const std::wstring& str);
    };

}
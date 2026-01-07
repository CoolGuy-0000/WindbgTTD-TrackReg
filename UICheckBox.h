#pragma once
#include "TimeTrackGUI.h"
#include <string>

namespace TimeTrackGUI {

    class UICheckBox : public UIElement {
    public:
        UICheckBox(UIManager* manager, const std::wstring& text, D2D1_RECT_F rect, UINT32 id, bool initiallyChecked = false);
        ~UICheckBox() = default;

        void Render() override;
        LRESULT WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) override;

        // 체크 상태 관련 함수
        void SetChecked(bool checked);
        bool IsChecked() const { return m_isChecked; }

        // 텍스트 변경 및 ID 가져오기
        void SetText(const std::wstring& text);
        UINT32 GetId() const { return m_id; }

    private:
        std::wstring m_text;
        bool m_isChecked = false; // 체크 상태 저장

        // 내부 헬퍼 함수: 체크 표시 그리기
        void DrawCheckMark(ID2D1HwndRenderTarget* rt, ID2D1SolidColorBrush* brush, D2D1_RECT_F boxRect);
    };

}
#pragma once
#include "TimeTrackGUI.h"

namespace TimeTrackGUI {
    class UISlider : public UIElement {
    public:
        UISlider(UIManager* manager, D2D1_RECT_F rect, float value) : UIElement(manager, rect), m_value(value) {}
        ~UISlider() = default;

        LRESULT WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) override;
        void Render() override;

        float GetValue() const { return m_value; }

    private:
        void UpdateHandleRect();

        float m_value = 0.0;            // 0.0 ~ 1.0 사이의 값
        D2D1_RECT_F m_handleRect = { 0, 0, 0, 0 }; // 실제로 그려질 핸들의 사각형 영역
    };
}


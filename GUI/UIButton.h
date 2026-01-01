#pragma once
#include "TimeTrackGUI.h"
#include <string>

namespace TimeTrackGUI {

    struct UIButtonStyle {
        D2D1_COLOR_F normalColor;
        D2D1_COLOR_F hoverColor;
        D2D1_COLOR_F activeColor;
        D2D1_COLOR_F textColor;
        float borderRadius;
    };

    class UIButton : public UIElement {
    public:
        UIButton(UIManager* manager, const std::wstring& text, D2D1_RECT_F rect, UINT32 id, UIButtonStyle style);
        ~UIButton();

        void Render() override;
        void CreateDeviceResources() override;
        void DiscardDeviceResources() override;

        void OnCursorMove(D2D1_POINT_2F cursorPt) override;
        void OnLButtonDown(D2D1_POINT_2F cursorPt) override;
        void OnLButtonUp(D2D1_POINT_2F cursorPt) override;

        void SetText(const std::wstring& text);

        UINT32 GetId() { return m_id; }

    private:
        UIButtonStyle m_style;
        std::wstring m_text;

        UINT32 m_id = 0;
    };

}
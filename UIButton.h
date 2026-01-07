#pragma once
#include "TimeTrackGUI.h"
#include <string>

namespace TimeTrackGUI {

    class UIButton : public UIElement {
    public:
        UIButton(UIManager* manager, const std::wstring& text, D2D1_RECT_F rect, UINT32 id);
        ~UIButton() = default;

        void Render() override;

        LRESULT WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) override;

        void SetText(const std::wstring& text);

        UINT32 GetId() { return m_id; }

    private:
        std::wstring m_text;
        bool m_bUpdateState = false;
    };

}
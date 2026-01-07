#pragma once
#include "TimeTrackGUI.h"
#include <string>

namespace TimeTrackGUI {
    enum class TextAlign {
        Left,
        Center,
        Right
    };

    class UIText : public UIElement {
    public:
        UIText(UIManager* manager, const std::wstring& text, D2D1_RECT_F rect, UINT32 id);
        ~UIText() = default;

        void Render() override;

        // 속성 설정
        void SetText(const std::wstring& text) { m_text = text; }
        void SetColor(const D2D1_COLOR_F& color) { m_color = color; }
        void SetFontSize(float size) { m_fontSize = size; }
        void SetAlignment(TextAlign align) { m_align = align; }

    private:
        std::wstring m_text;
        D2D1_COLOR_F m_color;
        float m_fontSize = 14.0f;
        TextAlign m_align = TextAlign::Left;
    };
}
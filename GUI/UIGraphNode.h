#pragma once
#include "TimeTrackGUI.h"
#include <string>

namespace TimeTrackGUI {

    struct UIGraphNodeStyle {
        D2D1_COLOR_F fillColor;
        D2D1_COLOR_F borderColor;
        D2D1_COLOR_F textColor;
        float borderRadius;
        float borderWidth;
    };

    class UIGraphNode : public UIElement {
    public:
        UIGraphNode(UIManager* manager, const std::wstring& text, D2D1_RECT_F rect, int id, UIGraphNodeStyle style);
        ~UIGraphNode();

        void Render() override;
        void CreateDeviceResources() override;
        void DiscardDeviceResources() override;

        // Nodes might need to be dragged? For now let's just display them.
        // void OnCursorMove(D2D1_POINT_2F cursorPt) override;

        int GetId() const { return m_id; }
        D2D1_POINT_2F GetCenter() const;

    private:
        std::wstring m_text;
        int m_id;
        UIGraphNodeStyle m_style;
    };

}

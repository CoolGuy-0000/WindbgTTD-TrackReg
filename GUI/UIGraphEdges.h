#pragma once
#include "TimeTrackGUI.h"
#include "UIGraphNode.h"

namespace TimeTrackGUI {

    // A special UI element to draw lines between nodes.
    // Must be added to UIManager *before* nodes to draw behind them.
    class UIGraphEdges : public UIElement {
    public:
        struct Edge {
            UIGraphNode* from;
            UIGraphNode* to;
        };

        UIGraphEdges(UIManager* manager, const std::vector<Edge>& edges)
            : UIElement(manager, D2D1::RectF(0,0,0,0)), m_edges(edges) {}

        void Render() override {
            auto rt = m_manager->GetRenderTarget();
            if (!rt) return;

            auto brush = m_manager->GetOrCreateSharedBrush();
            D2D1_COLOR_F prev = brush->GetColor();
            brush->SetColor(D2D1::ColorF(0x95a5a6));

            for (const auto& edge : m_edges) {
                if (edge.from && edge.to) {
                    rt->DrawLine(edge.from->GetCenter(), edge.to->GetCenter(), brush.Get(), 2.0f);
                }
            }
            brush->SetColor(prev);
        }

    private:
        std::vector<Edge> m_edges;
    };
}

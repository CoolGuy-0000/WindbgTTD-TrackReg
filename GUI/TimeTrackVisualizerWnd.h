#pragma once
#include "TimeTrackGUI.h"
#include "UIGraphNode.h"
#include <map>
#include <vector>

// We need the TraceRecord definition.
// Since it's currently defined inside timetrack.cpp and not in a header, we should probably move it to a shared header.
// But for now, I will redefine a compatible structure or include a shared header if I create one.
// The user said "don't touch existing timetrack", but moving a struct definition to a header is usually safe refactoring.
// However, to be strictly compliant, I will define a structure here that can hold the data.

struct VisualizerNodeData {
    int id;
    int parentId;
    std::wstring label;
    std::string details;
    // ... other info
};

namespace TimeTrackGUI {

    class TimeTrackVisualizerWnd : public GUIWnd {
    public:
        TimeTrackVisualizerWnd(const std::map<int, std::vector<VisualizerNodeData>>& data, const std::string& title);
        ~TimeTrackVisualizerWnd();

        LRESULT OnCreate(LPCREATESTRUCT lpcs) override;
        LRESULT OnDestroy() override;
        LRESULT OnPaint() override;

        // We might want scrolling/panning in the future
        LRESULT OnLButtonDown(UINT flag, POINT cursorPt) override;
        LRESULT OnLButtonUp(UINT flag, POINT cursorPt) override;
        LRESULT OnCursorMove(POINT cursorPt) override;

    private:
        void LayoutNodes(); // Simple tree layout

        std::map<int, std::vector<VisualizerNodeData>> m_data;
        std::string m_title;

        // We keep track of edges to draw them
        struct Edge {
            UIGraphNode* from;
            UIGraphNode* to;
        };
        std::vector<Edge> m_edges;

        // Panning offset
        float m_offsetX = 0.0f;
        float m_offsetY = 0.0f;
        D2D1_POINT_2F m_lastMousePos = {0,0};
        bool m_isDragging = false;
    };
}

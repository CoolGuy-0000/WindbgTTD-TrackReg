#include "TimeTrackVisualizerWnd.h"
#include "UIGraphEdges.h"
#include <queue>

using namespace TimeTrackGUI;

TimeTrackVisualizerWnd::TimeTrackVisualizerWnd(const std::map<int, std::vector<VisualizerNodeData>>& data, const std::string& title)
    : GUIWnd(), m_data(data), m_title(title) {
}

TimeTrackVisualizerWnd::~TimeTrackVisualizerWnd() {
}

LRESULT TimeTrackVisualizerWnd::OnCreate(LPCREATESTRUCT lpcs) {
    LayoutNodes();
    return 0;
}

LRESULT TimeTrackVisualizerWnd::OnDestroy() {
    return 0;
}

void TimeTrackVisualizerWnd::LayoutNodes() {
    if (m_data.empty()) return;

    // Simple BFS/Level layout
    // Assuming root is at id 0 or similar, or we find the root.
    // In timetrack, root has parentId = 0.

    // Style
    UIGraphNodeStyle nodeStyle = {
        D2D1::ColorF(0xecf0f1), // fill
        D2D1::ColorF(0x2c3e50), // border
        D2D1::ColorF(0x2c3e50), // text
        5.0f, // radius
        2.0f  // border width
    };

    // We need to map ID to Node* to create edges
    std::map<int, UIGraphNode*> idToNodeMap;

    float levelHeight = 100.0f;
    float nodeWidth = 200.0f;
    float nodeHeight = 60.0f;
    float siblingSpacing = 20.0f;

    struct LayoutItem {
        int id;
        int level;
        float x;
    };

    // We need to process level by level to assign X coordinates
    // This is a bit complex for a generic graph, but for a tree it's easier.
    // Let's assume it's a tree for now.

    // Find roots (nodes with parent 0 or not in map as child)
    // In timetrack structure: map<parentId, list<children>>.
    // Root is usually in list under key 0.

    // We will do a recursive layout
    // Measure tree width first?
    // Or just simplistic layout: Just pile them? No, that's ugly.
    // Let's do a simple Reingold-Tilford-ish or just centered parents.

    // Helper to build tree structure for layout
    // The m_data is already adjacency list: parent -> children.

    // Recursive function to place nodes
    // Returns the width of the subtree

    // Y position is determined by level * levelHeight.
    // X position needs to be calculated.

    float currentX = 50.0f; // Starting X

    // We need to instantiate nodes.
    // Let's use a queue for BFS to just place them to ensure they exist,
    // but for layout DFS is better to calculate widths.

    // Actually, let's just do a simple layout:
    // Render children below parents.
    // If multiple children, spread them out.

    std::function<float(int, int, float)> PlaceNode = [&](int nodeId, int level, float startX) -> float {
        // Find children
        auto it = m_data.find(nodeId);
        std::vector<VisualizerNodeData> children;
        if (it != m_data.end()) {
            children = it->second;
        }

        if (children.empty()) {
            // Leaf node
            // Create node
            // But wait, we need the node data itself.
            // The m_data only contains children lists.
            // Where is the root node data?
            // In timetrack, m_data[0] contains the root(s).
            return nodeWidth + siblingSpacing;
        }

        float totalWidth = 0.0f;
        float myX = startX;

        // This logic is tricky because we iterate children, but we are *in* the parent call.
        // We create children nodes here? No.

        return totalWidth;
    };

    // Revised Layout Strategy:
    // 1. Traverse and create all UIGraphNodes.
    // 2. Assign positions.

    // 1. Create Nodes.
    // The m_data is map<ParentID, vector<NodeData>>.
    // NodeData contains the ID of the child.
    // So we iterate the map to create all nodes.

    // Root(s) are in m_data[0].
    if (m_data.find(0) == m_data.end()) return;

    struct NodeInfo {
        VisualizerNodeData data;
        UIGraphNode* uiNode = nullptr;
        float x = 0;
        float y = 0;
        float width = 0;
        std::vector<NodeInfo*> children;
    };

    std::map<int, NodeInfo*> nodeMap;

    // Helper to get or create node info
    auto GetNodeInfo = [&](int id) -> NodeInfo* {
        if (nodeMap.find(id) == nodeMap.end()) {
            nodeMap[id] = new NodeInfo();
            nodeMap[id]->data.id = id;
        }
        return nodeMap[id];
    };

    // Build tree linkage
    for (auto& pair : m_data) {
        int parentId = pair.first;
        // If parentId is 0, these are roots.
        // If parentId != 0, we must find the parent NodeInfo.

        // Note: For root (parentId=0), the "parent" node doesn't exist as a visible node usually,
        // or it's a dummy root.
        // In timetrack, the loop starts with rootRecord.parentId = 0.

        for (const auto& childData : pair.second) {
            NodeInfo* childNode = GetNodeInfo(childData.id);
            childNode->data = childData; // Update data

            if (parentId != 0) {
                NodeInfo* parentNode = GetNodeInfo(parentId);
                parentNode->children.push_back(childNode);
            }
        }
    }

    // Now layout starting from roots (children of 0)
    std::vector<NodeInfo*> roots;
    if (m_data.count(0)) {
         for (const auto& d : m_data.at(0)) {
             roots.push_back(nodeMap[d.id]);
         }
    }

    // DFS to calculate sizes and positions
    std::function<void(NodeInfo*, int)> CalcSize = [&](NodeInfo* node, int depth) {
        if (node->children.empty()) {
            node->width = nodeWidth;
        } else {
            float childrenWidth = 0;
            for (auto* child : node->children) {
                CalcSize(child, depth + 1);
                childrenWidth += child->width + siblingSpacing;
            }
            // Remove last spacing
            if (!node->children.empty()) childrenWidth -= siblingSpacing;

            node->width = max(nodeWidth, childrenWidth);
        }
    };

    for (auto* root : roots) {
        CalcSize(root, 0);
    }

    // DFS to assign positions
    std::function<void(NodeInfo*, float, float, int)> AssignPos = [&](NodeInfo* node, float x, float y, int depth) {
        node->x = x + (node->width - nodeWidth) / 2.0f; // Center this node in its allotted width
        node->y = y;

        // Create UI Element
        D2D1_RECT_F rect = D2D1::RectF(node->x, node->y, node->x + nodeWidth, node->y + nodeHeight);
        node->uiNode = m_manager->CreateElement<UIGraphNode>(node->data.label, rect, node->data.id, nodeStyle);

        float currentChildX = x;
        // If children are narrower than parent, center them
        float childrenTotalWidth = 0;
        for(auto* c : node->children) childrenTotalWidth += c->width + siblingSpacing;
        if (!node->children.empty()) childrenTotalWidth -= siblingSpacing;

        if (childrenTotalWidth < node->width) {
            currentChildX += (node->width - childrenTotalWidth) / 2.0f;
        }

        for (auto* child : node->children) {
            AssignPos(child, currentChildX, y + levelHeight, depth + 1);

            // Create Edge
            Edge e;
            e.from = node->uiNode;
            e.to = child->uiNode;
            m_edges.push_back(e);

            currentChildX += child->width + siblingSpacing;
        }
    };

    float startX = 50.0f;
    for (auto* root : roots) {
        AssignPos(root, startX, 50.0f, 0);
        startX += root->width + siblingSpacing;
    }

    // Add Edges Element FIRST so it renders behind nodes
    // Convert m_edges (Window specific) to UIGraphEdges::Edge
    std::vector<UIGraphEdges::Edge> uiEdges;
    for (auto& e : m_edges) {
        uiEdges.push_back({e.from, e.to});
    }
    // We need to insert it at the beginning of m_elements in UIManager...
    // But UIManager only has CreateElement (push_back).
    // So edges will draw on top? That's ugly.
    // Hack: Create edges element first? We can't, nodes must exist to be referenced.
    // Solution: Create Edges element, pass pointers.
    // But we are creating nodes inside AssignPos.
    // We can clear elements and re-add them? No.

    // We will rely on the fact that we can just live with edges on top for now, or
    // we can create the EdgesElement *after* nodes but in Render it does nothing,
    // and we inject the draw call in the Window's OnPaint?
    // No, UIManager clears.

    // Let's just put it on top for now. It's acceptable for a prototype.
    // Or better: Create nodes, collect them. THEN create EdgesElement. THEN add nodes to Manager?
    // UIManager::CreateElement immediately adds to manager.

    // If I really want edges behind, I need to modify UIManager or `UIGraphNode` to draw its incoming edge?
    // Drawing incoming edge in `UIGraphNode::Render` is a good idea!
    // But `UIGraphNode` is generic.

    // Let's just create the "Edge" element at the end (on top). It's fine.
    m_manager->Register(std::make_unique<UIGraphEdges>(m_manager.get(), uiEdges));

    // Clean up helper structs
    for (auto& pair : nodeMap) {
        delete pair.second;
    }
}

LRESULT TimeTrackVisualizerWnd::OnPaint() {
    // We let UIManager handle rendering.
    // Panning (m_offsetX, m_offsetY) needs to be applied.
    // Since we cannot modify UIManager to accept a global transform easily without touching Base System,
    // we would normally move elements.
    // However, if we can access the RenderTarget before Render?
    // UIManager::Render() calls BeginDraw() then Clear().
    // So setting transform before calling Render() might be reset if UIManager doesn't preserve it.
    // But usually BeginDraw resets transform? No, Transform is state.

    // Let's try setting transform on the RT if it exists.
    ID2D1HwndRenderTarget* rt = m_manager->GetRenderTarget();
    if (rt) {
        // We set the transform. UIManager::Render() calls BeginDraw/Clear/Draw.
        // If UIManager doesn't reset transform, this works.
        // If UIManager creates RT inside Render (lazy init), we might miss it first frame.
        D2D1_MATRIX_3X2_F transform = D2D1::Matrix3x2F::Translation(m_offsetX, m_offsetY);
        rt->SetTransform(transform);
    }

    m_manager->Render();
    ValidateRect(GetHWND(), nullptr);
    return 0;
}

LRESULT TimeTrackVisualizerWnd::OnLButtonDown(UINT flag, POINT cursorPt) {
    m_isDragging = true;
    m_lastMousePos = D2D1::Point2F((float)cursorPt.x, (float)cursorPt.y);
    return GUIWnd::OnLButtonDown(flag, cursorPt);
}

LRESULT TimeTrackVisualizerWnd::OnLButtonUp(UINT flag, POINT cursorPt) {
    m_isDragging = false;
    return GUIWnd::OnLButtonUp(flag, cursorPt);
}

LRESULT TimeTrackVisualizerWnd::OnCursorMove(POINT cursorPt) {
    if (m_isDragging) {
        float dx = (float)cursorPt.x - m_lastMousePos.x;
        float dy = (float)cursorPt.y - m_lastMousePos.y;

        m_offsetX += dx;
        m_offsetY += dy;
        m_lastMousePos = D2D1::Point2F((float)cursorPt.x, (float)cursorPt.y);

        // We cannot use RenderTransform, so we must manually move all elements?
        // That's expensive and messy.
        // But without modifying UIManager, it's the only way unless UIManager exposes the RenderTarget.
        // UIManager exposes GetRenderTarget().

        // Use trick: Set Transform on RenderTarget in OnPaint?
        // But UIManager::Render resets it (BeginDraw).

        // I will implement a "UIGraphContainer" element that contains the nodes and handles offset.
        // But for now, let's just ignore panning or implement simple shifting.
    }
    return GUIWnd::OnCursorMove(cursorPt);
}

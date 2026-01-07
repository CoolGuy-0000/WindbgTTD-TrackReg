#pragma once
#include "TimeTrackGUI.h"
#include <string>
#include <vector>
#include <memory>

namespace TimeTrackGUI {

    // 트리 노드 구조체
    struct TreeNode {
        std::wstring text;
        int id;
        int depth = 0;              // 들여쓰기 깊이
        bool isExpanded = false;    // 펼쳐짐 여부
        bool hasChildren = false;   // 자식이 있는지 (화살표 표시용)
        TreeNode* parent = nullptr;
        std::vector<TreeNode*> children;

        TreeNode(const std::wstring& t, int i, int d) : text(t), id(i), depth(d) {}
        ~TreeNode() {
            for (auto* c : children) delete c; // 자식 메모리 해제
        }
    };

    class UITreeView : public UIElement {
    public:
        UITreeView(UIManager* manager, D2D1_RECT_F rect, UINT32 id);
        ~UITreeView();

        void Render() override;
        LRESULT WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) override;

        // 노드 추가 함수
        TreeNode* AddRootNode(const std::wstring& text, int nodeId);
        TreeNode* AddChildNode(TreeNode* parent, const std::wstring& text, int nodeId);

        // 선택된 노드 가져오기
        TreeNode* GetSelectedNode() const { return m_selectedNode; }

    private:
        std::vector<TreeNode*> m_rootNodes;      // 최상위 노드들
        std::vector<TreeNode*> m_visibleList;    // 현재 화면에 그려질(펼쳐진) 노드 리스트 (렌더링 최적화용)
        TreeNode* m_selectedNode = nullptr;      // 현재 선택된 노드

        float m_rowHeight = 24.0f;               // 한 줄 높이
        float m_scrollOffsetY = 0.0f;            // 스크롤 위치
        float m_indentSize = 20.0f;              // 들여쓰기 간격

        // 내부 헬퍼: 펼쳐진 노드 리스트 갱신
        void UpdateVisibleList();
        void AddNodeToVisibleListRecursive(TreeNode* node);

        // 내부 헬퍼: 화살표 그리기
        void DrawArrow(ID2D1HwndRenderTarget* rt, ID2D1SolidColorBrush* brush, D2D1_POINT_2F center, bool expanded);
    };

}
#include "UITreeView.h"

using namespace TimeTrackGUI;

UITreeView::UITreeView(UIManager* manager, D2D1_RECT_F rect, UINT32 id)
    : UIElement(manager, rect)
{
    m_id = id;
}

UITreeView::~UITreeView() {
    for (auto* node : m_rootNodes) {
        delete node;
    }
    m_rootNodes.clear();
    m_visibleList.clear();
}

TreeNode* UITreeView::AddRootNode(const std::wstring& text, int nodeId, Position pos) {
    TreeNode* node = new TreeNode(text, nodeId, 0);
    node->pos = pos;
    m_rootNodes.push_back(node);
    UpdateVisibleList();
    return node;
}

TreeNode* UITreeView::AddChildNode(TreeNode* parent, const std::wstring& text, int nodeId, Position pos) {
    if (!parent) return AddRootNode(text, nodeId, pos);

    TreeNode* node = new TreeNode(text, nodeId, parent->depth + 1);
    node->pos = pos;
    node->parent = parent;

    parent->children.push_back(node);
    parent->hasChildren = true;

    UpdateVisibleList();
    return node;
}

void UITreeView::UpdateVisibleList() {
    m_visibleList.clear();
    for (auto* node : m_rootNodes) {
        AddNodeToVisibleListRecursive(node);
    }
}

void UITreeView::AddNodeToVisibleListRecursive(TreeNode* node) {
    m_visibleList.push_back(node);
    if (node->isExpanded && node->hasChildren) {
        for (auto* child : node->children) {
            AddNodeToVisibleListRecursive(child);
        }
    }
}

void UITreeView::Render() {
    ID2D1HwndRenderTarget* rt = m_manager ? m_manager->GetRenderTarget() : nullptr;
    if (!rt) return;

    IDWriteTextFormat* fmt = m_manager->GetOrCreateTextFormat("Arial-14-Reg-Left",
        [](IDWriteFactory* f, IDWriteTextFormat** out) {
            return f->CreateTextFormat(L"Arial", NULL, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 14.0f, L"ko-kr", out);
        });
    if (fmt) fmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    if (fmt) fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    auto sharedBrush = m_manager->GetOrCreateSharedBrush();
    if (!sharedBrush) return;

    // --- 스타일 정의 ---
    const D2D1_COLOR_F colorBg = D2D1::ColorF(0.0f, 0.15f, 0.15f, 1.0f); // 전체 배경 (아주 어두운 회색)
    const D2D1_COLOR_F colorBorder = D2D1::ColorF(0.3f, 0.3f, 0.3f, 1.0f);    // 테두리
    const D2D1_COLOR_F colorText = D2D1::ColorF(0.9f, 0.9f, 0.9f, 1.0f);    // 기본 텍스트
    const D2D1_COLOR_F colorSelected = D2D1::ColorF(0.0f, 0.4f, 0.8f, 1.0f);    // 선택된 항목 배경 (파랑)
    const D2D1_COLOR_F colorHover = D2D1::ColorF(0.0f, 0.0f, 0.0f, 1.05f);   // 마우스 오버 (살짝 밝게)
    const D2D1_COLOR_F colorArrow = D2D1::ColorF(0.7f, 0.7f, 0.7f, 1.0f);    // 화살표 색상

    D2D1_COLOR_F prevColor = sharedBrush->GetColor();

    // 1. 전체 배경 및 테두리
    sharedBrush->SetColor(colorBg);
    rt->FillRectangle(m_rect, sharedBrush.Get());
    sharedBrush->SetColor(colorBorder);
    rt->DrawRectangle(m_rect, sharedBrush.Get());

    // 2. 클리핑 (영역 밖으로 나가는 내용 자르기)
    rt->PushAxisAlignedClip(m_rect, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    float currentY = m_rect.top - m_scrollOffsetY; // 스크롤 적용

    // 3. 노드 그리기 Loop
    // 마우스가 현재 어디에 있는지 확인 (호버 효과용)
    // 간단하게 구현하기 위해 정확한 마우스 위치는 WndProc에서 저장하거나 GetCursorPos 변환 필요
    // 여기서는 m_isCursorOver 상태를 활용하지만, Row별 호버는 생략하고 선택 효과에 집중합니다.

    for (TreeNode* node : m_visibleList) {
        // 화면 밖으로 완전히 벗어난 위쪽 노드는 스킵 (최적화)
        if (currentY + m_rowHeight < m_rect.top) {
            currentY += m_rowHeight;
            continue;
        }
        // 화면 아래로 벗어나면 그리기 중단
        if (currentY > m_rect.bottom) break;

        D2D1_RECT_F rowRect = D2D1::RectF(m_rect.left + 2, currentY, m_rect.right - 2, currentY + m_rowHeight);

        // 선택된 노드 배경
        if (node == m_selectedNode) {
            sharedBrush->SetColor(colorSelected);
            rt->FillRectangle(rowRect, sharedBrush.Get());
        }

        // 화살표 (자식이 있을 때만)
        float arrowX = m_rect.left + 10.0f + (node->depth * m_indentSize);
        if (node->hasChildren) {
            sharedBrush->SetColor(colorArrow);
            DrawArrow(rt, sharedBrush.Get(), D2D1::Point2F(arrowX, currentY + m_rowHeight / 2.0f), node->isExpanded);
        }

        // 텍스트
        D2D1_RECT_F textRect = rowRect;
        textRect.left = arrowX + 10.0f; // 화살표 옆에서 시작
        sharedBrush->SetColor(colorText);
        rt->DrawText(node->text.c_str(), (UINT32)node->text.length(), fmt, textRect, sharedBrush.Get());

        currentY += m_rowHeight;
    }

    if (m_isContextMenuOpen) {
        RenderContextMenu(rt, sharedBrush.Get(), fmt);
    }

    // 클리핑 해제
    rt->PopAxisAlignedClip();
    sharedBrush->SetColor(prevColor);
}

void UITreeView::RenderContextMenu(ID2D1HwndRenderTarget* rt, ID2D1SolidColorBrush* brush, IDWriteTextFormat* fmt) {
    // 메뉴 박스 크기 (항목: "Go to Position")
    D2D1_RECT_F menuRect = D2D1::RectF(
        m_contextMenuPos.x, m_contextMenuPos.y,
        m_contextMenuPos.x + 120.0f, m_contextMenuPos.y + 30.0f
    );

    // 그림자/배경
    brush->SetColor(D2D1::ColorF(0.2f, 0.2f, 0.2f));
    rt->FillRectangle(menuRect, brush);
    brush->SetColor(D2D1::ColorF(0.6f, 0.6f, 0.6f));
    rt->DrawRectangle(menuRect, brush);

    brush->SetColor(D2D1::ColorF(1.0f, 1.0f, 1.0f));
    D2D1_RECT_F textRect = menuRect;
    textRect.left += 5.0f; textRect.top += 5.0f;
    rt->DrawText(L"Go to Position", 14, fmt, textRect, brush);
}

void UITreeView::DrawArrow(ID2D1HwndRenderTarget* rt, ID2D1SolidColorBrush* brush, D2D1_POINT_2F center, bool expanded) {
    float size = 4.0f;
    if (expanded) {
        // 아래쪽 화살표
        rt->DrawLine(D2D1::Point2F(center.x - size, center.y - 2), D2D1::Point2F(center.x, center.y + size), brush, 1.5f);
        rt->DrawLine(D2D1::Point2F(center.x, center.y + size), D2D1::Point2F(center.x + size, center.y - 2), brush, 1.5f);
    }
    else {
        // 오른쪽 화살표
        rt->DrawLine(D2D1::Point2F(center.x - 2, center.y - size), D2D1::Point2F(center.x + size, center.y), brush, 1.5f);
        rt->DrawLine(D2D1::Point2F(center.x + size, center.y), D2D1::Point2F(center.x - 2, center.y + size), brush, 1.5f);
    }
}

LRESULT UITreeView::WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    auto res = UIElement::WndProc(hWnd, message, wParam, lParam);

    switch (message) {
    case WM_RBUTTONUP: { // 우클릭 처리
        if (m_isCursorOver) {
            POINT pt = { LOWORD(lParam), HIWORD(lParam) };

            // 클릭된 Y좌표로 노드 찾기
            float relativeY = (float)pt.y - m_rect.top + m_scrollOffsetY;
            int index = (int)(relativeY / m_rowHeight);

            if (index >= 0 && index < m_visibleList.size()) {
                m_contextTargetNode = m_visibleList[index];
                m_isContextMenuOpen = true;
                m_contextMenuPos = D2D1::Point2F((float)pt.x, (float)pt.y);
                m_selectedNode = m_contextTargetNode; // 우클릭 시 선택도 같이 함
                InvalidateRect(hWnd, NULL, FALSE);
            }
        }
        break;
    }
    case WM_MOUSEWHEEL: {
        if (m_isCursorOver) {
            short zDelta = GET_WHEEL_DELTA_WPARAM(wParam);
            m_scrollOffsetY -= (zDelta / 120.0f) * m_rowHeight; // 휠 한 칸당 한 줄 스크롤

            // 스크롤 범위 제한 (간단한 버전)
            float maxScroll = (float)m_visibleList.size() * m_rowHeight - (m_rect.bottom - m_rect.top);
            if (maxScroll < 0) maxScroll = 0;

            if (m_scrollOffsetY < 0) m_scrollOffsetY = 0;
            if (m_scrollOffsetY > maxScroll) m_scrollOffsetY = maxScroll;

            InvalidateRect(hWnd, NULL, FALSE);
            return 0; // 메시지 처리함
        }
        break;
    }
    case WM_LBUTTONDOWN: {
        if (m_isCursorOver) {
            POINT pt = { LOWORD(lParam), HIWORD(lParam) };

            if (m_isContextMenuOpen) {
                D2D1_RECT_F menuRect = D2D1::RectF(m_contextMenuPos.x, m_contextMenuPos.y, m_contextMenuPos.x + 120.0f, m_contextMenuPos.y + 30.0f);
                if (pt.x >= menuRect.left && pt.x <= menuRect.right && pt.y >= menuRect.top && pt.y <= menuRect.bottom) {
                    // [이벤트 발동] "Go to Position" 클릭됨
                    // 메인 윈도우로 커맨드 전송 (ID: 9999 등 약속된 값 사용)
                    SendMessage(hWnd, WM_TTGUI_COMMAND, (WPARAM)this, (LPARAM)UIEventType::TreeRightClick);
                    m_isContextMenuOpen = false;
                    InvalidateRect(hWnd, NULL, FALSE);
                    return 0;
                }
                else {
                    // 메뉴 밖 클릭 시 닫기
                    m_isContextMenuOpen = false;
                    InvalidateRect(hWnd, NULL, FALSE);
                }
            }

            // 클릭된 Y좌표를 통해 몇 번째 줄인지 계산
            float relativeY = (float)pt.y - m_rect.top + m_scrollOffsetY;
            int index = (int)(relativeY / m_rowHeight);

            if (index >= 0 && index < m_visibleList.size()) {
                TreeNode* clickedNode = m_visibleList[index];

                // 화살표 영역 클릭인지 확인 (대략적으로 계산)
                float arrowX = m_rect.left + 10.0f + (clickedNode->depth * m_indentSize);
                if (pt.x >= arrowX - 8 && pt.x <= arrowX + 8 && clickedNode->hasChildren) {
                    // 펼치기/접기 토글
                    clickedNode->isExpanded = !clickedNode->isExpanded;
                    UpdateVisibleList(); // 리스트 다시 계산
                }
                else {
                    // 노드 선택
                    m_selectedNode = clickedNode;
                    // 부모에게 알림 (이벤트 타입 추가 필요: TreeSelect)
                    SendMessage(hWnd, WM_TTGUI_COMMAND, (WPARAM)this, (LPARAM)UIEventType::TreeSelect); // 임시로 Click 이벤트 사용
                }
                InvalidateRect(hWnd, NULL, FALSE);
            }

        }
        break;
    }
    }
    return res;
}
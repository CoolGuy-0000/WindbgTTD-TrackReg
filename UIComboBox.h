#pragma once
#include "TimeTrackGUI.h"
#include <vector>
#include <string>

namespace TimeTrackGUI {

    class UIComboBox : public UIElement {
    public:
        UIComboBox(UIManager* manager, D2D1_RECT_F rect, UINT32 id);
        ~UIComboBox() = default;

        void Render() override;
        LRESULT WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) override;

        // 데이터 관리
        void AddItem(const std::wstring& item);
        void ClearItems();

        // 선택 관리
        void SetSelectedIndex(int index);
        int GetSelectedIndex() const { return m_selectedIndex; }
        std::wstring GetSelectedText() const;

    private:
        std::vector<std::wstring> m_items;
        int m_selectedIndex = -1;
        bool m_isExpanded = false; // 드롭다운이 열려있는지 여부

        // 스타일 상수
        float m_itemHeight = 28.0f; // 리스트 각 항목의 높이

        // 내부 헬퍼: 드롭다운 리스트 전체 영역 계산
        D2D1_RECT_F GetDropdownRect() const;

        // 내부 헬퍼: 화살표 그리기
        void DrawArrow(ID2D1HwndRenderTarget* rt, ID2D1SolidColorBrush* brush, D2D1_RECT_F rect);
    };

}
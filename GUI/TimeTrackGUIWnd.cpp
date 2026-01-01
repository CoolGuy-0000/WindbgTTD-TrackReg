#include "..\stdafx.h"
#include "TimeTrackGUI.h"
#include "UIButton.h"

using namespace TimeTrackGUI;

TimeTrackGUIWnd::TimeTrackGUIWnd() : GUIWnd() {}
TimeTrackGUIWnd::~TimeTrackGUIWnd() {}

void TimeTrackGUIWnd::OnUIEvent(const UIEvent& ev) {
    if (ev.type == UIEvent::Type::ButtonClicked) {
        UINT32 id = ((UIButton*)ev.source)->GetId();
        if (id == 2) {
            MessageBoxA(NULL, "취소를 하셨습니다", "메시지", MB_OK);
            return;
        }
    }
}

LRESULT TimeTrackGUIWnd::OnCreate(LPCREATESTRUCT lpcs) {
    // 스타일 정의
    static UIButtonStyle blueStyle = {
        D2D1::ColorF(0x2e86de), // normal
        D2D1::ColorF(0x54a0ff), // hover
        D2D1::ColorF(0x01a3a4), // active
        D2D1::ColorF(D2D1::ColorF::White), // text
        6.0f // radius
    };

    static UIButtonStyle redStyle = {
        D2D1::ColorF(0xee5253),
        D2D1::ColorF(0xff6b6b),
        D2D1::ColorF(0xff9f43),
        D2D1::ColorF(D2D1::ColorF::White),
        4.0f
    };

    // 버튼 생성 (UIManager가 요소를 소유함)
    m_manager->CreateElement<UIButton>(L"확인", D2D1::RectF(30, 30, 120, 80), 1, blueStyle);
    m_manager->CreateElement<UIButton>(L"취소", D2D1::RectF(140, 30, 230, 80), 2, redStyle);

    return 0;
}

LRESULT TimeTrackGUIWnd::OnDestroy() {
    return 0;
}

LRESULT TimeTrackGUIWnd::OnPaint() {
    m_manager->Render();
    ValidateRect(GetHWND(), nullptr);
    return 0;
}

LRESULT TimeTrackGUIWnd::OnCursorMove(POINT cursorPt) {
    D2D1_POINT_2F pt = D2D1::Point2F(static_cast<FLOAT>(cursorPt.x), static_cast<FLOAT>(cursorPt.y));
    m_manager->OnCursorMove(pt);
    return 0;
}

LRESULT TimeTrackGUIWnd::OnResize(UINT flag, POINT new_size) {
    m_manager->Resize(new_size.x, new_size.y);
    return 0;
}

LRESULT TimeTrackGUIWnd::OnLButtonDown(UINT flag, POINT cursorPt) {
    D2D1_POINT_2F pt = D2D1::Point2F(static_cast<FLOAT>(cursorPt.x), static_cast<FLOAT>(cursorPt.y));
    m_manager->OnLButtonDown(pt);
    return 0;
}

LRESULT TimeTrackGUIWnd::OnLButtonUp(UINT flag, POINT cursorPt) {
    D2D1_POINT_2F pt = D2D1::Point2F(static_cast<FLOAT>(cursorPt.x), static_cast<FLOAT>(cursorPt.y));
    m_manager->OnLButtonUp(pt);
    return 0;
}
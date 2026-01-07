#include "TimeTrackGUI.h"
#include "TimeTrackLogic.h"
#include "UILoader.h"
#include "UITreeView.h"
#include "utils.h"

#include <memory>
#include <deque>
#include <format>
#include <map>
#include "disasm_helper.h"
#include <Zydis/Zydis.h>

#include <TTD/IReplayEngine.h>
#include <TTD/IReplayEngineStl.h>
#include "Formatters.h"

using namespace TimeTrackGUI;
using namespace TTD;
using namespace Replay;

extern IReplayEngineView* g_pReplayEngine;
extern ICursorView* g_pGlobalCursor;
extern ProcessorArchitecture g_TargetCPUType;
TimeTrackGUI::TimeTrackGUIWnd* track_gui = nullptr;

TimeTrackGUIWnd::TimeTrackGUIWnd() : GUIWnd() {}
TimeTrackGUIWnd::~TimeTrackGUIWnd() { track_gui = nullptr;  }

LRESULT TimeTrackGUIWnd::WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    auto res = GUIWnd::WndProc(hWnd, message, wParam, lParam);

    switch (message) {
        case WM_CREATE: {
            UILoader::CreateUIFromKeyValues(m_manager.get(), GetConfigFilePathInDll() + L"gui_main.txt");
            return WndProc_Success;
        }
        
        default: {
            if (message == WM_TTGUI_COMMAND)
            {
                if (wParam == 13) {
                    m_manager->RemoveAllElements();
                    UILoader::CreateUIFromKeyValues(m_manager.get(), GetConfigFilePathInDll() + L"gui_timetrack.txt");
                    break;
                }

                UIEventType type = (UIEventType)lParam;

                if (type == UIEventType::TreeRightClick) {
                    UITreeView* tree = (UITreeView*)wParam;
                    if (tree) {
                        Position targetPos = tree->GetSelectedPos();
                        if (targetPos != Position::Invalid) {
                            g_pGlobalCursor->SetPosition(targetPos);
                        }

                    }
                }
            }
            
            break;
        }
        
    }

    return res;
}



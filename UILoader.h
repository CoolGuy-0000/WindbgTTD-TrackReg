#pragma once
#include "TimeTrackGUI.h"
#include "KeyValue.h"
#include "UITreeView.h"
#include <vector>

namespace TimeTrackGUI {
    class UILoader {
    public:
        static bool CreateUIFromKeyValues(UIManager* mgr, const std::wstring& filepath);

    private:
        static UIElement* CreateControl(UIManager* mgr, KeyValue* node);
        static void ParseTreeNodes(UITreeView* tree, TreeNode* parent, KeyValue* kvNode);
    };
}
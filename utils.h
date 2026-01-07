#pragma once
#include <map>
#include <string>
#include "TimeTrackLogic.h"
#include "UITreeView.h"
#include <dbgeng.h>
#include <wdbgexts.h>
#include <atlcomcli.h>

std::wstring GetConfigFilePathInDll();
void LoadTraceDataToTree(TimeTrackGUI::UITreeView* uiTree, std::map<int, std::vector<TraceRecord>>& treeData, int rootId);
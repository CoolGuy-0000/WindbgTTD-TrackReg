#include "UILoader.h"
#include "UIButton.h"
#include "UICheckBox.h"
#include "UIComboBox.h"
#include "UIImage.h"
#include "UITreeView.h"
#include "UIText.h"
#include <Windows.h>
#include <memory>
#include <sstream>

// ... 필요한 헤더들

using namespace TimeTrackGUI;

D2D1_COLOR_F ParseColor(const std::wstring& str) {
    if (str.empty()) return D2D1::ColorF(1, 1, 1, 1); // 기본 흰색
    std::wstringstream ss(str);
    float r, g, b, a = 1.0f;
    ss >> r >> g >> b;
    if (!ss.eof()) ss >> a; // 알파값은 선택 사항
    return D2D1::ColorF(r, g, b, a);
}

void UILoader::ParseTreeNodes(UITreeView* tree, TreeNode* parent, KeyValue* kvContainer) {
    // kvContainer는 "Nodes" 혹은 "Children" 블록입니다.
    // 이 블록 아래의 자식들(각 노드 정의)을 순회합니다.
    for (auto& itemKV : kvContainer->GetChildren()) {

        // 1. 속성 읽기
        std::wstring text = itemKV->GetKey(); // 기본값은 키 이름 (예: "Node1")
        if (auto* t = itemKV->FindChild(L"Text")) text = t->GetValue();

        int nodeId = 0;
        if (auto* i = itemKV->FindChild(L"Id")) nodeId = i->AsInt();

        // 2. 노드 생성 (부모가 없으면 Root, 있으면 Child)
        TreeNode* newNode = nullptr;
        if (parent == nullptr) {
            newNode = tree->AddRootNode(text, nodeId);
        }
        else {
            newNode = tree->AddChildNode(parent, text, nodeId);
        }

        // 3. 자식 노드가 있는지 확인하고 재귀 호출 ("Children" 키가 있을 경우)
        if (auto* childrenKV = itemKV->FindChild(L"Children")) {
            ParseTreeNodes(tree, newNode, childrenKV); // 재귀!
        }

        // (선택 사항) 처음부터 펼쳐진 상태로 만들고 싶다면
        if (auto* ex = itemKV->FindChild(L"Expanded")) {
            if (ex->AsBool()) newNode->isExpanded = true;
        }
    }
}

bool UILoader::CreateUIFromKeyValues(UIManager* mgr, const std::wstring& filepath) {
    // 1. KeyValue 시스템에게 파일 해석 요청
    std::unique_ptr<KeyValue> root(KeyValue::LoadFromFile(filepath));
    if (!root) return false;

    // 2. "Elements" 섹션 찾기
    // (파일 구조상 최상위 키가 무엇이든 그 아래 Elements를 찾거나, 구조에 따라 조정)
    // 여기서는 최상위 노드의 자식 중 "Elements"를 찾습니다.

    // 구조가 "MainWindow" { "Elements" { ... } } 이므로
    // root(ROOT) -> MainWindow -> Elements 순으로 탐색
    KeyValue* mainWin = nullptr;
    if (!root->GetChildren().empty()) mainWin = root->GetChildren()[0].get();

    if (mainWin) {
        KeyValue* _kv = mainWin->FindChild(L"Title");
        if (_kv)
            SetWindowTextW(mgr->GetGUIWnd()->GetHWND(), _kv->GetValue().c_str());
        
        _kv = mainWin->FindChild(L"Size");
        if (_kv) {
            mgr->GetGUIWnd()->m_MinMaxSize = _kv->AsRect();
            SetWindowPos(mgr->GetGUIWnd()->GetHWND(), NULL, 0, 0, (int)mgr->GetGUIWnd()->m_MinMaxSize.left, (int)mgr->GetGUIWnd()->m_MinMaxSize.top, SWP_NOMOVE | SWP_NOZORDER);
        }

        _kv = mainWin->FindChild(L"Elements");
        if (_kv) {
            for (auto& itemNode : _kv->GetChildren())
                    CreateControl(mgr, itemNode.get());
        }

    }

    return true;
}

UIElement* UILoader::CreateControl(UIManager* mgr, KeyValue* node) {
    // node->GetKey() 는 "SaveButton" 같은 이름

    // 속성 가져오기 (없으면 기본값 반환하는 메서드 활용)
    KeyValue* typeKv = node->FindChild(L"Type");
    if (!typeKv) return nullptr;

    std::wstring type = typeKv->GetValue();

    // 공통 속성: Rect, Id
    D2D1_RECT_F rect = { 0,0,0,0 };
    if (auto* r = node->FindChild(L"Rect")) rect = r->AsRect();

    int id = 0;
    if (auto* i = node->FindChild(L"Id")) id = i->AsInt();

    int z = 0;
    if (auto* i = node->FindChild(L"Z")) z = i->AsInt();

	UIElement* element = nullptr;

    // 타입별 생성
    if (type == L"Button") {
        std::wstring text = L"Button";
        if (auto* t = node->FindChild(L"Text")) text = t->GetValue();
        
        element = new UIButton(mgr, text, rect, id);
    }
    else if (type == L"CheckBox") {
        std::wstring text = L"Check";
        if (auto* t = node->FindChild(L"Text")) text = t->GetValue();
        bool checked = false;
        if (auto* c = node->FindChild(L"Checked")) checked = c->AsBool();

        element = new UICheckBox(mgr, text, rect, id, checked);
    }
    else if (type == L"ComboBox") {
        UIComboBox* combo = new UIComboBox(mgr, rect, id);

        // 콤보박스 아이템 추가 처리
        KeyValue* itemsNode = node->FindChild(L"Items");
        if (itemsNode) {
            for (auto& item : itemsNode->GetChildren()) {
                combo->AddItem(item->GetValue());
            }
        }

        element = combo;
    }
    else if (type == L"Image") {
        std::wstring path = L"";
        if (auto* p = node->FindChild(L"Path")) path = p->GetValue();
        element = new UIImage(mgr, path, rect, id);
    }
    else if (type == L"Tree") {
        UITreeView* tree = new UITreeView(mgr, rect, id);

        if (auto* nodesKV = node->FindChild(L"Nodes")) {
            ParseTreeNodes(tree, nullptr, nodesKV);
        }

        element = tree;
    }
    else if (type == L"Text") {
        std::wstring text = L"Label";
        if (auto* t = node->FindChild(L"Text")) text = t->GetValue();

        UIText* uiText = new UIText(mgr, text, rect, id);

        // 폰트 크기
        if (auto* s = node->FindChild(L"FontSize")) {
            uiText->SetFontSize(s->AsFloat());
        }

        // 색상 (예: "1.0 0.5 0.5")
        if (auto* c = node->FindChild(L"Color")) {
            uiText->SetColor(ParseColor(c->GetValue()));
        }

        // 정렬 (Left, Center, Right)
        if (auto* a = node->FindChild(L"Align")) {
            std::wstring alignStr = a->GetValue();
            if (alignStr == L"Center") uiText->SetAlignment(TextAlign::Center);
            else if (alignStr == L"Right") uiText->SetAlignment(TextAlign::Right);
        }

        element = uiText;
    }

    element->SetZIndex(z);
    mgr->InvalidateZOrder();

    return element;
}
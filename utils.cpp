#include "utils.h"
#include <Zydis/Zydis.h>
#include <TTD/IReplayEngine.h>
#include <TTD/IReplayEngineStl.h>
#include "Formatters.h"
#include "disasm_helper.h"
#include <deque>

extern IReplayEngineView* g_pReplayEngine;
extern ProcessorArchitecture g_TargetCPUType;

std::wstring GetConfigFilePathInDll() {
    HMODULE hModule = NULL;

    ::GetModuleHandleExW(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        (LPCWSTR)&GetConfigFilePathInDll,
        &hModule
    );

    if (hModule == NULL) {
        return L"";
    }

    wchar_t pathBuffer[MAX_PATH];
    ::GetModuleFileNameW(hModule, pathBuffer, MAX_PATH);

    std::wstring fullPath(pathBuffer);

    size_t lastSlashPos = fullPath.find_last_of(L"\\/");

    if (lastSlashPos != std::wstring::npos) {
        fullPath = fullPath.substr(0, lastSlashPos + 1);
    }

    return fullPath;
}

void LoadTraceDataToTree(TimeTrackGUI::UITreeView* uiTree, std::map<int, std::vector<TraceRecord>>& treeData, int rootId) {
    CComPtr<IDebugClient> client;
    if (FAILED(DebugCreate(IID_PPV_ARGS(&client))))return;

    CComQIPtr<IDebugSymbols3> symbols(client);

    if (!symbols) return;

    ZydisDecoder decoder;
    SetupZydisDecoder(&decoder, g_TargetCPUType);

    ZydisFormatter formatter;
    ZydisFormatterInit(&formatter, ZYDIS_FORMATTER_STYLE_INTEL);

    char buffer[256];
    BufferView bufferView{ buffer, sizeof(buffer) };
    ZydisDecodedInstruction instruction;
    ZydisDecodedOperand operands[ZYDIS_MAX_OPERAND_COUNT];

    UniqueCursor inspectCursor(g_pReplayEngine->NewCursor());

    struct StackState {
        const TraceRecord* record = nullptr;
        int depth = 0;
        TimeTrackGUI::TreeNode* parentNode = nullptr;
    };

    std::deque<StackState> workStack;

    // 2. 루트 찾기
    auto rootIt = treeData.find(rootId);
    if (rootIt != treeData.end()) {
        const auto& children = rootIt->second;
        for (auto it = children.rbegin(); it != children.rend(); ++it) {
            // 루트의 부모는 nullptr
            workStack.push_back({ &(*it), 0, nullptr });
        }
    }


    while (!workStack.empty()) {
        StackState current = workStack.back();
        workStack.pop_back();

        const TraceRecord& record = *current.record;

        inspectCursor->SetPosition(record.pos);

        uint64_t curIP = (uint64_t)inspectCursor->GetProgramCounter();

        std::string output;

        output = std::format("{} | ", record.pos);

        uint64_t uDisp;
        symbols->GetNameByOffset(curIP, buffer, sizeof(buffer), NULL, &uDisp);
        output += std::format("{}+{:X}", buffer, uDisp);
        output += "\t";

        inspectCursor->QueryMemoryBuffer((GuestAddress)curIP, bufferView);

        if (ZYAN_SUCCESS(ZydisDecoderDecodeFull(&decoder, buffer, 16, &instruction, operands))) {
            ZydisFormatterFormatInstruction(&formatter, &instruction, operands, instruction.operand_count_visible, buffer, sizeof(buffer), curIP, ZYAN_NULL);
            output += buffer;
        }

        std::wstring wLineStr(output.begin(), output.end());

        TimeTrackGUI::TreeNode* newNode = nullptr;
        if (current.parentNode == nullptr) {
            newNode = uiTree->AddRootNode(wLineStr, record.id, record.pos);
        }
        else {
            newNode = uiTree->AddChildNode(current.parentNode, wLineStr, record.id, record.pos);
        }

        // 자식 노드 처리
        auto childIt = treeData.find(record.id);
        if (childIt != treeData.end()) {
            const auto& children = childIt->second;
            for (auto it = children.rbegin(); it != children.rend(); ++it) {
                workStack.push_back({ &(*it), current.depth + 1, newNode }); // 부모로 newNode 전달
            }
        }

        // 편의상 루트 레벨은 펼쳐둠
        if (current.depth < 1) newNode->isExpanded = true;
    }

}

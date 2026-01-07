#include "KeyValue.h"
#include <fstream>
#include <sstream>
#include <vector>
#include <stack>

using namespace TimeTrackGUI;

KeyValue::KeyValue(const std::wstring& key, const std::wstring& value)
    : m_key(key), m_value(value) {
}

KeyValue::~KeyValue() {
    m_children.clear();
}

int KeyValue::AsInt() const {
    try { return std::stoi(m_value); }
    catch (...) { return 0; }
}

float KeyValue::AsFloat() const {
    try { return std::stof(m_value); }
    catch (...) { return 0.0f; }
}

bool KeyValue::AsBool() const {
    return (m_value == L"1" || m_value == L"true" || m_value == L"True");
}

D2D1_RECT_F KeyValue::AsRect() const {
    std::wstringstream ss(m_value);
    float l = 0, t = 0, r = 0, b = 0;
    ss >> l >> t >> r >> b;
    return D2D1::RectF(l, t, r, b);
}

void KeyValue::AddChild(KeyValue* child) {
    m_children.push_back(std::unique_ptr<KeyValue>(child));
}

KeyValue* KeyValue::FindChild(const std::wstring& key) {
    for (auto& c : m_children) {
        if (c->GetKey() == key) return c.get();
    }
    return nullptr; // 못 찾음
}

// --- 파싱 로직 ---

// 문자열을 토큰 단위로 쪼개는 헬퍼
std::vector<std::wstring> Tokenize(const std::wstring& text) {
    std::vector<std::wstring> tokens;
    std::wstring current;
    bool inQuote = false;

    for (size_t i = 0; i < text.length(); ++i) {
        wchar_t c = text[i];

        if (c == L'\"') { // 따옴표 처리
            inQuote = !inQuote;
            continue;
        }

        if (!inQuote && (c == L' ' || c == L'\t' || c == L'\n' || c == L'\r')) {
            if (!current.empty()) {
                tokens.push_back(current);
                current.clear();
            }
        }
        else if (!inQuote && (c == L'{' || c == L'}')) { // 중괄호는 별도 토큰
            if (!current.empty()) {
                tokens.push_back(current);
                current.clear();
            }
            tokens.push_back(std::wstring(1, c));
        }
        else {
            current += c;
        }
    }
    if (!current.empty()) tokens.push_back(current);
    return tokens;
}


std::wstring Utf8ToWstring(const std::string& str) {
    if (str.empty()) return std::wstring();

    // 1. 필요한 버퍼 크기 계산
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0);

    // 2. 변환 실행
    std::wstring wstrTo(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &wstrTo[0], size_needed);

    return wstrTo;
}


KeyValue* KeyValue::LoadFromFile(const std::wstring& filepath) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) return nullptr;

    // 파일 전체 읽기
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string utf8Content = buffer.str();



    std::wstring content = Utf8ToWstring(utf8Content);

    // 주석 제거 (// 이후 라인 끝까지) - 간단 구현
    // (실제로는 더 정교한 처리가 필요할 수 있음)

    // 토큰화
    auto tokens = Tokenize(content);

    // 루트 노드 생성
    KeyValue* root = new KeyValue(L"ROOT");
    int pos = 0;

    // 재귀 파싱 시작
    ParseRecursive(tokens, pos, root);

    return root;
}

void KeyValue::ParseRecursive(std::vector<std::wstring>& tokens, int& pos, KeyValue* parent) {
    while (pos < tokens.size()) {
        std::wstring key = tokens[pos++];

        if (key == L"}") return; // 블록 끝, 리턴

        // 다음 토큰 확인 (값인지, 블록 시작인지)
        if (pos >= tokens.size()) break;
        std::wstring next = tokens[pos];

        if (next == L"{") {
            // 자식 블록 시작 "Section" { ... }
            pos++; // '{' 건너뜀
            KeyValue* newBlock = new KeyValue(key, L"");
            parent->AddChild(newBlock);
            ParseRecursive(tokens, pos, newBlock); // 재귀 호출
        }
        else {
            // 단순 키-값 쌍 "Key" "Value"
            pos++; // 값 건너뜀
            KeyValue* newNode = new KeyValue(key, next);
            parent->AddChild(newNode);
        }
    }
}
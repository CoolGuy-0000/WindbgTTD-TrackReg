#pragma once
#include <string>
#include <vector>
#include <memory>
#include <map>
#include <d2d1.h> // 편의상 Rect 변환을 위해 포함 (없어도 무방)

namespace TimeTrackGUI {

    class KeyValue {
    public:
        KeyValue(const std::wstring& key = L"", const std::wstring& value = L"");
        ~KeyValue();

        // --- 데이터 접근 ---
        std::wstring GetKey() const { return m_key; }
        std::wstring GetValue() const { return m_value; }

        // 값을 특정 타입으로 변환해서 가져오기
        int AsInt() const;
        float AsFloat() const;
        bool AsBool() const;
        D2D1_RECT_F AsRect() const; // "0 0 100 100" -> Rect 변환

        // --- 자식 노드 관리 (트리 구조) ---
        void AddChild(KeyValue* child);
        KeyValue* FindChild(const std::wstring& key); // 특정 키를 가진 자식 찾기
        const std::vector<std::unique_ptr<KeyValue>>& GetChildren() const { return m_children; }

        // --- 파일 입출력 ---
        // 텍스트 파일 전체를 읽어서 Root KeyValue로 반환
        static KeyValue* LoadFromFile(const std::wstring& filepath);

    private:
        std::wstring m_key;
        std::wstring m_value;
        std::vector<std::unique_ptr<KeyValue>> m_children;

        // 파싱 헬퍼 함수
        static void ParseRecursive(std::vector<std::wstring>& tokens, int& pos, KeyValue* currentParent);
    };

}
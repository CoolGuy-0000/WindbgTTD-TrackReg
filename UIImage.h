#pragma once
#include "TimeTrackGUI.h"
#include <string>
#include <wincodec.h> // WIC 헤더 필수
#include <wrl/client.h> // ComPtr 사용을 권장 (안전한 포인터 관리)

using Microsoft::WRL::ComPtr;

// 라이브러리 링크 (Visual Studio 설정에 없으면 여기서 명시)
//#pragma comment(lib, "windowscodecs.lib")

namespace TimeTrackGUI {

    class UIImage : public UIElement {
    public:
        UIImage(UIManager* manager, const std::wstring& filePath, D2D1_RECT_F rect, UINT32 id);
        ~UIImage();

        void Render() override;

        // 이미지 변경
        void SetImage(const std::wstring& filePath);

        // 투명도 설정 (0.0 ~ 1.0)
        void SetOpacity(float opacity);

    private:
        std::wstring m_filePath;
        float m_opacity = 1.0f;

        // Direct2D 비트맵 객체
        ComPtr<ID2D1Bitmap> m_pBitmap = nullptr;

        // 내부 헬퍼: 파일에서 비트맵 로드
        HRESULT LoadBitmapFromFile(ID2D1RenderTarget* renderTarget, const std::wstring& uri);
    };

}
#include "UIImage.h"

using namespace TimeTrackGUI;

UIImage::UIImage(UIManager* manager, const std::wstring& filePath, D2D1_RECT_F rect, UINT32 id)
    : UIElement(manager, rect), m_filePath(filePath)
{
    m_id = id;
}

UIImage::~UIImage() {}

void UIImage::SetImage(const std::wstring& filePath) {
    if (m_filePath != filePath) {
        m_filePath = filePath;
        m_pBitmap.Reset();
    }
}

void UIImage::SetOpacity(float opacity) {
    if (opacity < 0.0f) m_opacity = 0.0f;
    else if (opacity > 1.0f) m_opacity = 1.0f;
    else m_opacity = opacity;
}

void UIImage::Render() {
    ID2D1HwndRenderTarget* rt = m_manager ? m_manager->GetRenderTarget() : nullptr;
    if (!rt) return;

    // 비트맵이 아직 없거나 해제되었다면 로드 시도
    if (!m_pBitmap && !m_filePath.empty()) {
        LoadBitmapFromFile(rt, m_filePath);
    }

    // 로드에 실패했거나 파일이 없으면 그리지 않음
    if (!m_pBitmap) return;

    // 이미지 그리기
    // m_rect 영역에 꽉 채워서 그립니다. (비율 유지 필요 시 계산 로직 추가 가능)
    rt->DrawBitmap(
        m_pBitmap.Get(),
        m_rect,
        m_opacity,
        D2D1_BITMAP_INTERPOLATION_MODE_LINEAR,
        NULL // 소스 영역 NULL은 전체 이미지를 의미
    );
}

// WIC를 사용하여 파일을 읽고 D2D1Bitmap으로 변환하는 함수
HRESULT UIImage::LoadBitmapFromFile(ID2D1RenderTarget* renderTarget, const std::wstring& uri) {
    HRESULT hr = S_OK;

    // 1. WIC 팩토리 생성 (이미지 코덱 관리자)
    ComPtr<IWICImagingFactory> pIWICFactory;

    hr = CoCreateInstance(
        CLSID_WICImagingFactory,
        NULL,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&pIWICFactory)
    );

    if (FAILED(hr)) return hr;

    // 2. 디코더 생성 (파일 읽기)
    ComPtr<IWICBitmapDecoder> pDecoder;

    hr = pIWICFactory->CreateDecoderFromFilename(
        uri.c_str(),
        NULL,
        GENERIC_READ,
        WICDecodeMetadataCacheOnLoad,
        &pDecoder
    );

    // 3. 프레임 가져오기 (첫 번째 장)
    ComPtr<IWICBitmapFrameDecode> pSource;

    if (SUCCEEDED(hr)) {
        hr = pDecoder->GetFrame(0, &pSource);
    }

    // 4. 포맷 컨버터 생성 (Direct2D 호환 픽셀 포맷으로 변환)
    ComPtr<IWICFormatConverter> pConverter;

    if (SUCCEEDED(hr)) {
        hr = pIWICFactory->CreateFormatConverter(&pConverter);
    }

    // 5. 포맷 변환 설정 (32bppPBGRA - 투명도 지원 포맷)
    if (SUCCEEDED(hr)) {
        hr = pConverter->Initialize(
            pSource.Get(),
            GUID_WICPixelFormat32bppPBGRA,
            WICBitmapDitherTypeNone,
            NULL,
            0.f,
            WICBitmapPaletteTypeMedianCut
        );
    }

    // 6. Direct2D 비트맵 생성
    if (SUCCEEDED(hr)) {
        m_pBitmap.Reset();

        hr = renderTarget->CreateBitmapFromWicBitmap(
            pConverter.Get(),
            NULL,
            &m_pBitmap
        );
    }

    return hr;
}
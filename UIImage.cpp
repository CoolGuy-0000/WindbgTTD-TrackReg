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

    if (!m_pBitmap && !m_filePath.empty()) {
        LoadBitmapFromFile(rt, m_filePath);
    }

    if (!m_pBitmap) return;

    rt->DrawBitmap(
        m_pBitmap.Get(),
        m_rect,
        m_opacity,
        D2D1_BITMAP_INTERPOLATION_MODE_LINEAR,
        NULL // 소스 영역 NULL은 전체 이미지를 의미
    );
}

HRESULT UIImage::LoadBitmapFromFile(ID2D1RenderTarget* renderTarget, const std::wstring& uri) {
    HRESULT hr = S_OK;

    ComPtr<IWICImagingFactory> pIWICFactory;

    hr = CoCreateInstance(
        CLSID_WICImagingFactory,
        NULL,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&pIWICFactory)
    );

    if (FAILED(hr)) return hr;

    ComPtr<IWICBitmapDecoder> pDecoder;

    hr = pIWICFactory->CreateDecoderFromFilename(
        uri.c_str(),
        NULL,
        GENERIC_READ,
        WICDecodeMetadataCacheOnLoad,
        &pDecoder
    );

    ComPtr<IWICBitmapFrameDecode> pSource;

    if (SUCCEEDED(hr)) {
        hr = pDecoder->GetFrame(0, &pSource);
    }

    ComPtr<IWICFormatConverter> pConverter;

    if (SUCCEEDED(hr)) {
        hr = pIWICFactory->CreateFormatConverter(&pConverter);
    }

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
#include "SSMTOverlayFont.h"

#include <algorithm>
#include <cstring>
#include <set>
#include <stdexcept>
#include <vector>
#include <wrl/client.h>

#pragma comment(lib, "gdi32.lib")

namespace SSMTOverlayFont {
namespace {
struct FontDC {
    HDC dc = CreateCompatibleDC(nullptr);
    HFONT font = nullptr;
    HGDIOBJ previous = nullptr;
    ~FontDC() {
        if (previous && previous != HGDI_ERROR) SelectObject(dc, previous);
        if (font) DeleteObject(font);
        if (dc) DeleteDC(dc);
    }
};

void Check(bool success) { if (!success) throw std::runtime_error("System overlay font unavailable"); }
}

std::unique_ptr<DirectX::SpriteFont> Create(ID3D11Device* device, const std::wstring& characters, const wchar_t* family)
{
    FontDC font;
    Check(font.dc != nullptr);
    font.font = CreateFontW(-20, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        GB2312_CHARSET, OUT_TT_ONLY_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, family);
    Check(font.font != nullptr);
    font.previous = SelectObject(font.dc, font.font);
    Check(font.previous && font.previous != HGDI_ERROR);
    wchar_t actual[LF_FACESIZE] = {};
    Check(GetTextFaceW(font.dc, LF_FACESIZE, actual) > 0);
    // CreateFont 会静默替代不存在的字体，必须验证实际字体，不能把缺字方框当作成功。
    Check(!_wcsicmp(actual, family) ||
        (!_wcsicmp(family, L"Microsoft YaHei") && !wcscmp(actual, L"微软雅黑")));
    WORD probe = 0xffff;
    Check(GetGlyphIndicesW(font.dc, L"中", 1, &probe, GGI_MARK_NONEXISTING_GLYPHS) != GDI_ERROR && probe != 0xffff);
    TEXTMETRICW metrics = {};
    Check(GetTextMetricsW(font.dc, &metrics) != 0);

    std::set<wchar_t> unique;
    for (wchar_t c = 32; c < 127; ++c) unique.insert(c);
    for (wchar_t c : characters) if (c >= 32 && (c < 0xd800 || c > 0xdfff)) unique.insert(c);
    Check(unique.size() <= 4096);

    const unsigned width = 1024, maxHeight = 2048;
    std::vector<uint32_t> pixels(width * maxHeight, 0);
    std::vector<DirectX::SpriteFont::Glyph> glyphs;
    const MAT2 identity = { {0, 1}, {0, 0}, {0, 0}, {0, 1} };
    unsigned x = 1, y = 1, rowHeight = 0;
    for (wchar_t c : unique) {
        WORD index;
        if (GetGlyphIndicesW(font.dc, &c, 1, &index, GGI_MARK_NONEXISTING_GLYPHS) == GDI_ERROR || index == 0xffff) continue;
        GLYPHMETRICS gm = {};
        DWORD size = GetGlyphOutlineW(font.dc, c, GGO_GRAY8_BITMAP, &gm, 0, nullptr, &identity);
        Check(size != GDI_ERROR);
        unsigned w = (std::max)(1u, gm.gmBlackBoxX), h = (std::max)(1u, gm.gmBlackBoxY);
        Check(w + 2 < width);
        if (x + w + 1 > width) { x = 1; y += rowHeight + 1; rowHeight = 0; }
        Check(y + h + 1 <= maxHeight);
        if (size) {
            std::vector<BYTE> bitmap(size);
            Check(GetGlyphOutlineW(font.dc, c, GGO_GRAY8_BITMAP, &gm, size, bitmap.data(), &identity) != GDI_ERROR);
            unsigned pitch = (gm.gmBlackBoxX + 3) & ~3u;
            for (unsigned row = 0; row < gm.gmBlackBoxY; ++row) {
                for (unsigned col = 0; col < gm.gmBlackBoxX; ++col) {
                    unsigned alpha = (std::min)(255u, unsigned(bitmap[row * pitch + col]) * 255 / 64);
                    // SpriteBatch 默认使用预乘 alpha 混合，透明像素的 RGB 也必须为零。
                    pixels[(y + row) * width + x + col] = alpha * 0x01010101u;
                }
            }
        }
        glyphs.push_back({ uint32_t(c), { LONG(x), LONG(y), LONG(x + w), LONG(y + h) },
            float(gm.gmptGlyphOrigin.x), float(metrics.tmAscent - gm.gmptGlyphOrigin.y),
            float(gm.gmCellIncX - gm.gmptGlyphOrigin.x - int(w)) });
        x += w + 1;
        rowHeight = (std::max)(rowHeight, h);
    }

    unsigned height = 1;
    while (height < y + rowHeight + 1) height *= 2;
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = width; desc.Height = height; desc.MipLevels = 1; desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_IMMUTABLE; desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA data = { pixels.data(), width * sizeof(uint32_t), 0 };
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    Check(SUCCEEDED(device->CreateTexture2D(&desc, &data, &texture)));
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view;
    Check(SUCCEEDED(device->CreateShaderResourceView(texture.Get(), nullptr, &view)));
    std::unique_ptr<DirectX::SpriteFont> result(new DirectX::SpriteFont(view.Get(), glyphs.data(), glyphs.size(), float(metrics.tmHeight)));
    result->SetDefaultCharacter(L'?');
    return result;
}
}

#include "SSMTOverlayFont.h"
#include "SSMTOverlayText.h"
#include <wrl/client.h>
#include <DirectXColors.h>
#include <cstdio>
#include <fstream>
#include <stdexcept>
#include <vector>

using Microsoft::WRL::ComPtr;
using namespace SSMTOverlayText;

static void Check(bool success, const char* message)
{
    if (!success) throw std::runtime_error(message);
}

static Message FormatMessage(const char* format, ...)
{
    va_list args; va_start(args, format);
    auto result = Format(format, args);
    va_end(args); return result;
}

static Message FormatMessage(const wchar_t* format, ...)
{
    va_list args; va_start(args, format);
    auto result = Format(format, args);
    va_end(args); return result;
}

static void TestText()
{
    Check(Configure(L"auto", MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_SIMPLIFIED)) && UseChinese(), "zh-CN auto");
    Check(Configure(L"auto", MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_TRADITIONAL)) && !UseChinese(), "zh-TW fallback");
    Check(Configure(L"auto", MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US)) && !UseChinese(), "en-US auto");
    Check(Configure(L"zh_s") && UseChinese(), "explicit zh_s");
    Check(!Configure(L"invalid") && UseChinese(), "invalid language does not change current state");
    Check(Configure(L"en") && !UseChinese(), "explicit English");
    Configure(L"zh_s");

    auto reload = FormatMessage(L"> Reloaded config in %.3fs\n", 0.125);
    Check(reload.english == L"> Reloaded config in 0.125s\n", "original log text");
    Check(reload.chinese == L"> 配置已重载，用时 0.125 秒\n", "wide translation and precision");
    auto path = FormatMessage("  Error opening %S\n", L"C:\\模组\\Водяница.ini");
    Check(path.chinese == L"  打开文件失败：C:\\模组\\Водяница.ini\n", "narrow %S with Unicode filename");
    auto narrow = FormatMessage("> %s hash %0*llx copied to clipboard\n", "PS", 16, 0x1234ULL);
    Check(narrow.chinese == L"> PS 哈希 0000000000001234 已复制到剪贴板\n", "dynamic width and 64-bit hash");
    auto texture = FormatMessage(L"Failed to load custom texture resource %ls: 0x%x\n", L"贴图.dds", 0x80070002u);
    Check(texture.chinese.find(L"贴图.dds，错误：0x80070002") != std::wstring::npos, "texture filename and HRESULT");
    auto unknown = FormatMessage("Custom Mod: %s / %u %%\n", "未翻译的文本", 42u);
    Check(unknown.chinese.empty() && unknown.english == L"Custom Mod: 未翻译的文本 / 42 %\n", "unknown UTF-8 message passthrough");
    auto longMessage = FormatMessage(L"%ls", std::wstring(5000, L'中').c_str());
    Check(longMessage.english.size() == 1023, "long messages safely terminated");
    auto invalid = FormatMessage("%s", "\xff");
    Check(invalid.chinese.empty(), "invalid UTF-8 safely handled");
    auto cleanup = FormatMessage(L"NOTICE: Detected %d unknown user settings in d3dx_user.ini\n This is normal if you removed/changed any mods\n%ls Press %ls to reload the config now, or %ls to reset all settings to default\n The first unrecognised entry was: \"%ls = %f\"\n",
        2, L" Unknown settings clean-up is disabled, so they will stay in d3dx_user.ini\n", L"F10", L"Ctrl+Alt+F10", L"$test", 1.0);
    Check(cleanup.chinese.find(L"自动清理已关闭") != std::wstring::npos && cleanup.chinese.find(L"F10") != std::wstring::npos, "unknown settings nested notice");
}

static void TestFont()
{
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    Check(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
        D3D11_SDK_VERSION, &device, nullptr, &context)), "create software WARP device");
    bool rejected = false;
    try { SSMTOverlayFont::Create(device.Get(), L"中文", L"SSMT-Nonexistent-Font-12345"); }
    catch (const std::exception&) { rejected = true; }
    Check(rejected, "silently substituted font must be rejected for English fallback");
    auto font = SSMTOverlayFont::Create(device.Get(), FontCharacters() + L"贴图\\Водяница");
    Check(font->ContainsCharacter(L'中') && font->ContainsCharacter(L'图') && font->ContainsCharacter(L'В'), "real Chinese and Cyrillic glyphs");
    Check(font->FindGlyph(L'中')->Character != font->FindGlyph(L'?')->Character, "Chinese glyph is not fallback");
    Check(!font->ContainsCharacter(wchar_t(0xd800)), "surrogate is not a standalone glyph");

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = 1100; desc.Height = 230; desc.MipLevels = 1; desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; desc.SampleDesc.Count = 1;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET;
    ComPtr<ID3D11Texture2D> target;
    Check(SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &target)), "render target");
    ComPtr<ID3D11RenderTargetView> view;
    Check(SUCCEEDED(device->CreateRenderTargetView(target.Get(), nullptr, &view)), "render view");
    auto rawView = view.Get(); context->OMSetRenderTargets(1, &rawView, nullptr);
    D3D11_VIEWPORT viewport = { 0, 0, float(desc.Width), float(desc.Height), 0, 1 };
    context->RSSetViewports(1, &viewport);
    const float background[] = { 0.025f, 0.04f, 0.06f, 1 };
    context->ClearRenderTargetView(view.Get(), background);
    DirectX::SpriteBatch batch(context.Get());
    batch.Begin();
    font->DrawString(&batch, L"> 配置已重载，用时 0.125 秒\n帧分析已保存到 C:\\模组\\FrameAnalysis\n警告：未知选项：test\n加载自定义贴图失败：贴图.dds，错误：0x80070002\n着色器 Hunting 模式（标记方式：skip）\nPS 哈希 0000000000001234 已复制到剪贴板",
        DirectX::XMFLOAT2(15, 12), DirectX::Colors::LimeGreen);
    batch.End();
    desc.Usage = D3D11_USAGE_STAGING; desc.BindFlags = 0; desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;
    Check(SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &staging)), "readback texture");
    context->CopyResource(staging.Get(), target.Get());
    D3D11_MAPPED_SUBRESOURCE mapped;
    Check(SUCCEEDED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)), "readback");
    std::vector<uint32_t> pixels(desc.Width * desc.Height);
    unsigned greenPixels = 0;
    for (UINT y = 0; y < desc.Height; ++y) {
        auto row = reinterpret_cast<const uint8_t*>(mapped.pData) + y * mapped.RowPitch;
        for (UINT x = 0; x < desc.Width; ++x) {
            auto p = row + x * 4;
            if (p[1] > 100 && p[1] > p[0] * 2) ++greenPixels;
            pixels[y * desc.Width + x] = (uint32_t(p[3]) << 24) | (uint32_t(p[0]) << 16) | (uint32_t(p[1]) << 8) | p[2];
        }
    }
    context->Unmap(staging.Get(), 0);
    Check(greenPixels > 2000, "Chinese glyphs actually render on WARP");
    BITMAPFILEHEADER fileHeader = {};
    BITMAPINFOHEADER bitmapHeader = {};
    fileHeader.bfType = 0x4d42; fileHeader.bfOffBits = sizeof(fileHeader) + sizeof(bitmapHeader);
    fileHeader.bfSize = fileHeader.bfOffBits + DWORD(pixels.size() * 4);
    bitmapHeader.biSize = sizeof(bitmapHeader); bitmapHeader.biWidth = desc.Width;
    bitmapHeader.biHeight = -LONG(desc.Height); bitmapHeader.biPlanes = 1; bitmapHeader.biBitCount = 32;
    std::ofstream preview("overlay-preview.bmp", std::ios::binary);
    preview.write(reinterpret_cast<const char*>(&fileHeader), sizeof(fileHeader));
    preview.write(reinterpret_cast<const char*>(&bitmapHeader), sizeof(bitmapHeader));
    preview.write(reinterpret_cast<const char*>(pixels.data()), pixels.size() * 4);
    Check(preview.good(), "write preview");
}

int main()
{
    try { TestText(); TestFont(); puts("PASS: language, printf/Unicode, system font fallback, Chinese WARP rendering"); return 0; }
    catch (const std::exception& error) { fprintf(stderr, "FAIL: %s\n", error.what()); return 1; }
}

#include <Windows.h>
#include <d3d11_4.h>
#include <wrl/client.h>
#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>

using Microsoft::WRL::ComPtr;

static bool write_replacement(const std::filesystem::path& directory)
{
    // 未压缩 RGBA8 DDS；回读绿色像素，验证真正执行了 INI 的纹理替换。
    const unsigned header[32] = {0x20534444, 124, 0x100f, 4, 4, 16, 0, 1,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 32, 0x41, 0, 32,
        0xff, 0xff00, 0xff0000, 0xff000000, 0x1000, 0, 0, 0, 0};
    const std::array<unsigned, 16> green = {0xff00ff00, 0xff00ff00, 0xff00ff00, 0xff00ff00,
        0xff00ff00, 0xff00ff00, 0xff00ff00, 0xff00ff00, 0xff00ff00, 0xff00ff00, 0xff00ff00,
        0xff00ff00, 0xff00ff00, 0xff00ff00, 0xff00ff00, 0xff00ff00};
    std::ofstream output(directory / L"green.dds", std::ios::binary);
    output.write(reinterpret_cast<const char*>(header), sizeof(header));
    output.write(reinterpret_cast<const char*>(green.data()), sizeof(green));
    return output.good();
}

static bool read_pixel(ID3D11Device* device, ID3D11DeviceContext* context,
    ID3D11Resource* resource, unsigned expected)
{
    D3D11_RESOURCE_DIMENSION type{};
    resource->GetType(&type);
    ComPtr<ID3D11Resource> staging;
    if (type == D3D11_RESOURCE_DIMENSION_TEXTURE2D) {
        ComPtr<ID3D11Texture2D> texture, copy;
        if (FAILED(resource->QueryInterface(IID_PPV_ARGS(&texture)))) return false;
        D3D11_TEXTURE2D_DESC desc{};
        texture->GetDesc(&desc);
        desc.BindFlags = desc.MiscFlags = 0;
        desc.Usage = D3D11_USAGE_STAGING; desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        if (FAILED(device->CreateTexture2D(&desc, nullptr, &copy))) return false;
        copy.As(&staging);
    } else {
        ComPtr<ID3D11Texture3D> texture, copy;
        if (FAILED(resource->QueryInterface(IID_PPV_ARGS(&texture)))) return false;
        D3D11_TEXTURE3D_DESC desc{};
        texture->GetDesc(&desc);
        desc.BindFlags = desc.MiscFlags = 0;
        desc.Usage = D3D11_USAGE_STAGING; desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        if (FAILED(device->CreateTexture3D(&desc, nullptr, &copy))) return false;
        copy.As(&staging);
    }
    context->CopyResource(staging.Get(), resource);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) return false;
    const unsigned actual = *static_cast<const unsigned*>(mapped.pData);
    context->Unmap(staging.Get(), 0);
    if (actual != expected) std::printf("pixel=%08x expected=%08x\n", actual, expected);
    return actual == expected;
}

static bool check_texture(ID3D11Device3* device, ID3D11DeviceContext* context,
    bool modern, bool volume, bool matching, ID3D11RenderTargetView* target)
{
    std::array<unsigned, 32> pixels{};
    pixels.fill(matching ? 0xffff0000 : 0xff0000ff);
    const D3D11_SUBRESOURCE_DATA data = {pixels.data(), 16, 64};
    ComPtr<ID3D11Resource> resource;
    if (volume) {
        D3D11_TEXTURE3D_DESC1 desc = {4, 4, 2, 1, DXGI_FORMAT_R8G8B8A8_UNORM,
            D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0, D3D11_TEXTURE_LAYOUT_UNDEFINED};
        if (modern) {
            ComPtr<ID3D11Texture3D1> texture;
            if (FAILED(device->CreateTexture3D1(&desc, &data, &texture))) return false;
            texture.As(&resource);
        } else {
            const D3D11_TEXTURE3D_DESC base = {4, 4, 2, 1, desc.Format, desc.Usage, desc.BindFlags, 0, 0};
            ComPtr<ID3D11Texture3D> texture;
            if (FAILED(device->CreateTexture3D(&base, &data, &texture))) return false;
            texture.As(&resource);
        }
    } else {
        D3D11_TEXTURE2D_DESC1 desc = {4, 4, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, {1, 0},
            D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0, D3D11_TEXTURE_LAYOUT_UNDEFINED};
        if (modern) {
            ComPtr<ID3D11Texture2D1> texture;
            if (FAILED(device->CreateTexture2D1(&desc, &data, &texture))) return false;
            D3D11_TEXTURE2D_DESC1 actual{};
            texture->GetDesc1(&actual);
            if (actual.TextureLayout != desc.TextureLayout) return false;
            texture.As(&resource);
        } else {
            const D3D11_TEXTURE2D_DESC base = {4, 4, 1, 1, desc.Format, {1, 0}, desc.Usage, desc.BindFlags, 0, 0};
            ComPtr<ID3D11Texture2D> texture;
            if (FAILED(device->CreateTexture2D(&base, &data, &texture))) return false;
            texture.As(&resource);
        }
    }
    ComPtr<ID3D11ShaderResourceView> view;
    if (modern) {
        ComPtr<ID3D11ShaderResourceView1> extended;
        if (FAILED(device->CreateShaderResourceView1(resource.Get(), nullptr, &extended))) return false;
        extended.As(&view);
    } else if (FAILED(device->CreateShaderResourceView(resource.Get(), nullptr, &view))) return false;
    ID3D11ShaderResourceView* raw = view.Get();
    context->PSSetShaderResources(0, 1, &raw);
    const float black[4]{};
    context->ClearRenderTargetView(target, black);
    ComPtr<ID3D11ShaderResourceView> replaced;
    context->PSGetShaderResources(0, 1, &replaced);
    if (!replaced) return false;
    ComPtr<ID3D11Resource> output;
    replaced->GetResource(&output);
    const bool passed = (matching ? output.Get() != resource.Get() : output.Get() == resource.Get()) &&
        read_pixel(device, context, output.Get(), matching ? 0xff00ff00 : 0xff0000ff);
    // 释放绑定及全部调用方引用，再用另一份内容创建，覆盖候选缓存的生命周期。
    context->ClearState();
    context->Flush();
    return passed;
}

int wmain(int argc, wchar_t** argv)
{
    if (argc != 2 || GetEnvironmentVariableW(L"SSMT_CONTEXT_TEST_HARDWARE", nullptr, 0)) return 2;
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    if (!write_replacement(std::filesystem::path(argv[1]).parent_path())) return 3;
    if (!LoadLibraryExW(L"d3d11.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32) || !LoadLibraryW(argv[1])) return 3;
    ComPtr<ID3D11Device> base;
    ComPtr<ID3D11DeviceContext> context;
    if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
        D3D11_SDK_VERSION, &base, nullptr, &context))) return 4;
    ComPtr<ID3D11Device3> device;
    if (FAILED(base.As(&device))) return 5;
    const D3D11_TEXTURE2D_DESC target_desc = {1, 1, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM,
        {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_RENDER_TARGET, 0, 0};
    ComPtr<ID3D11Texture2D> target;
    ComPtr<ID3D11RenderTargetView> target_view;
    if (FAILED(device->CreateTexture2D(&target_desc, nullptr, &target)) ||
        FAILED(device->CreateRenderTargetView(target.Get(), nullptr, &target_view))) return 6;
    bool ok = true;
    for (bool modern : {false, true}) {
        for (bool volume : {false, true}) {
            bool passed = true;
            for (int i = 0; i < 8; ++i)
                passed = check_texture(device.Get(), context.Get(), modern, volume, i % 2 == 0, target_view.Get()) && passed;
            std::printf("%s Texture%s: %s\n", modern ? "Device3" : "legacy", volume ? "3D" : "2D", passed ? "PASS" : "BYPASS");
            std::fflush(stdout);
            ok = passed && ok;
        }
    }
    D3D11_TEXTURE2D_DESC1 resize = {7, 5, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, {1, 0},
        D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0, D3D11_TEXTURE_LAYOUT_UNDEFINED};
    ComPtr<ID3D11Texture2D1> resized;
    D3D11_TEXTURE2D_DESC1 actual{};
    bool passed = SUCCEEDED(device->CreateTexture2D1(&resize, nullptr, &resized));
    if (passed) { resized->GetDesc1(&actual); passed = actual.Width == 9 && actual.TextureLayout == resize.TextureLayout; }
    std::printf("Device3 descriptor override: %s\n", passed ? "PASS" : "BYPASS");
    std::fflush(stdout);
    ok = passed && ok;
    const HRESULT validation = device->CreateTexture2D1(&resize, nullptr, nullptr);
    resize.Width = 0;
    ComPtr<ID3D11Texture2D1> invalid;
    ok = validation == S_FALSE && FAILED(device->CreateTexture2D1(&resize, nullptr, &invalid)) && !invalid && ok;

    // 非默认布局的支持由系统决定；包装器必须保留原 API 的返回值与描述，不能降级创建。
    const GUID pass_through_device = {0xdb17dc9a, 0x5a5a, 0x4ac7, {0xa4, 0xce, 0xef, 0x41, 0xf7, 0xc5, 0x1d, 0x5c}};
    ComPtr<ID3D11Device> original;
    ComPtr<ID3D11Device3> original3;
    passed = SUCCEEDED(device->QueryInterface(pass_through_device, reinterpret_cast<void**>(original.GetAddressOf()))) &&
        SUCCEEDED(original.As(&original3));
    if (passed) {
        D3D11_TEXTURE2D_DESC1 row_major = {4, 4, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, {1, 0},
            D3D11_USAGE_STAGING, 0, D3D11_CPU_ACCESS_READ, 0, D3D11_TEXTURE_LAYOUT_ROW_MAJOR};
        ComPtr<ID3D11Texture2D1> raw_texture, wrapped_texture;
        const HRESULT raw_hr = original3->CreateTexture2D1(&row_major, nullptr, &raw_texture);
        const HRESULT wrapped_hr = device->CreateTexture2D1(&row_major, nullptr, &wrapped_texture);
        passed = raw_hr == wrapped_hr;
        if (SUCCEEDED(raw_hr) && SUCCEEDED(wrapped_hr)) {
            D3D11_TEXTURE2D_DESC1 raw_desc{}, wrapped_desc{};
            raw_texture->GetDesc1(&raw_desc); wrapped_texture->GetDesc1(&wrapped_desc);
            passed = raw_desc.TextureLayout == wrapped_desc.TextureLayout &&
                raw_desc.TextureLayout == row_major.TextureLayout;
        }
        std::printf("Device3 ROW_MAJOR: %s (system=%08lx wrapped=%08lx)\n", passed ? "PASS" : "FAIL", raw_hr, wrapped_hr);
        std::fflush(stdout);
    }
    ok = passed && ok;
    std::printf("texture replacement checks completed: %s\n", ok ? "PASS" : "FAIL");
    std::fflush(stdout);
    return ok ? 0 : 7;
}

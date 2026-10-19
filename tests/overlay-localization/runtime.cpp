#include <Windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <cstdio>
#include <stdexcept>

using Microsoft::WRL::ComPtr;
static void Check(bool success, const char* reason) { if (!success) throw std::runtime_error(reason); }

int wmain(int argc, wchar_t** argv)
{
    try {
        Check(argc == 2, "expected Runtime DLL path");
        SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
        Check(LoadLibraryExW(L"d3d11.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32) != nullptr, "load system D3D11");
        Check(LoadLibraryW(argv[1]) != nullptr, "load Runtime");
        ComPtr<ID3D11Device> device;
        ComPtr<ID3D11DeviceContext> context;
        Check(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
            D3D11_SDK_VERSION, &device, nullptr, &context)), "create wrapped WARP device");
        const GUID hackerContext = { 0xa3046b1e, 0x336b, 0x4d90, { 0x9f, 0xd6, 0x23, 0x4b, 0xc0, 0x9b, 0x86, 0x87 } };
        ComPtr<IUnknown> wrapper;
        Check(SUCCEEDED(context->QueryInterface(hackerContext, &wrapper)), "actual Runtime context required");
        HWND window = CreateWindowExW(0, L"STATIC", L"", WS_POPUP, 0, 0, 1100, 230,
            nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        Check(window != nullptr, "hidden window");
        ComPtr<IDXGIDevice> dxgi;
        ComPtr<IDXGIAdapter> adapter;
        ComPtr<IDXGIFactory2> factory;
        Check(SUCCEEDED(device.As(&dxgi)), "DXGI device");
        Check(SUCCEEDED(dxgi->GetAdapter(&adapter)), "DXGI adapter");
        Check(SUCCEEDED(adapter->GetParent(IID_PPV_ARGS(&factory))), "DXGI factory");
        DXGI_SWAP_CHAIN_DESC1 chainDesc = {};
        chainDesc.Width = 1100; chainDesc.Height = 230;
        chainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        chainDesc.SampleDesc.Count = 1; chainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        chainDesc.BufferCount = 2; chainDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
        ComPtr<IDXGISwapChain1> chain;
        Check(SUCCEEDED(factory->CreateSwapChainForHwnd(device.Get(), window, &chainDesc, nullptr, nullptr, &chain)), "swap chain");
        D3D11_TEXTURE2D_DESC textureDesc = {};
        textureDesc.Width = 7; textureDesc.Height = 9; textureDesc.MipLevels = textureDesc.ArraySize = 1;
        textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; textureDesc.SampleDesc.Count = 1;
        textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        ComPtr<ID3D11Texture2D> texture;
        ComPtr<ID3D11ShaderResourceView> view;
        Check(SUCCEEDED(device->CreateTexture2D(&textureDesc, nullptr, &texture)), "game texture");
        Check(SUCCEEDED(device->CreateShaderResourceView(texture.Get(), nullptr, &view)), "game SRV");
        const D3D11_VIEWPORT viewport = { 2, 3, 5, 6, 0.25f, 0.75f };
        auto rawView = view.Get(); context->PSSetShaderResources(0, 1, &rawView);
        // 同时检查原本未绑定 RTV 的状态，不能残留叠加层的交换链目标。
        context->RSSetViewports(1, &viewport);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
        DWORD gdiCount = 0;
        for (unsigned frame = 0; frame < 32; ++frame) {
            Check(SUCCEEDED(chain->Present(0, 0)), "Runtime overlay Present");
            ComPtr<ID3D11ShaderResourceView> after;
            context->PSGetShaderResources(0, 1, &after);
            D3D11_VIEWPORT actual = {};
            UINT count = 1; context->RSGetViewports(&count, &actual);
            D3D11_PRIMITIVE_TOPOLOGY topology;
            context->IAGetPrimitiveTopology(&topology);
            Check(after.Get() == view.Get(), "overlay restored original PS SRV");
            Check(count == 1 && actual.TopLeftX == viewport.TopLeftX && actual.Width == viewport.Width &&
                actual.MinDepth == viewport.MinDepth, "overlay restored original viewport");
            Check(topology == D3D11_PRIMITIVE_TOPOLOGY_LINELIST, "overlay restored original topology");
            ComPtr<ID3D11RenderTargetView> afterTarget;
            context->OMGetRenderTargets(1, &afterTarget, nullptr);
            Check(!afterTarget, "overlay restored empty RTV");
            const DWORD current = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
            if (!frame) gdiCount = current;
            Check(current == gdiCount, "font init and repeated Presents do not leak GDI objects");
        }
        context->ClearState();
        chain.Reset(); DestroyWindow(window);
        puts("PASS: actual Runtime overlay, 32 WARP Presents, SRV/RTV/viewport/topology restoration, GDI lifetime");
        return 0;
    } catch (const std::exception& error) { fprintf(stderr, "FAIL: %s\n", error.what()); return 1; }
}

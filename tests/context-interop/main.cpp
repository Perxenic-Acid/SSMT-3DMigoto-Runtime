#include <windows.h>
#include <d3d11_1.h>
#include <dxgi1_5.h>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>

static LONG WINAPI crash(EXCEPTION_POINTERS* info)
{
    std::printf("exception=%08lx address=%p\n", info->ExceptionRecord->ExceptionCode,
        info->ExceptionRecord->ExceptionAddress);
    std::fflush(stdout);
    ExitProcess(10);
    return EXCEPTION_EXECUTE_HANDLER;
}

static bool test_swap_chain_interfaces(ID3D11Device* device)
{
    wchar_t class_name[64]{};
    swprintf_s(class_name, L"SSMTContextInterop-%lu", GetCurrentProcessId());
    WNDCLASSW window_class{};
    window_class.lpfnWndProc = DefWindowProcW;
    window_class.hInstance = GetModuleHandleW(nullptr);
    window_class.lpszClassName = class_name;
    if (!RegisterClassW(&window_class)) return false;

    HWND window = CreateWindowExW(0, class_name, L"", WS_POPUP,
        0, 0, 4, 4, nullptr, nullptr, window_class.hInstance, nullptr);
    IDXGIDevice* dxgi_device = nullptr;
    IDXGIAdapter* adapter = nullptr;
    IDXGIFactory2* factory = nullptr;
    IDXGISwapChain1* swap_chain = nullptr;
    IDXGISwapChain3* swap_chain3 = nullptr;
    IDXGISwapChain4* swap_chain4 = nullptr;
    IUnknown* base_identity = nullptr;
    IUnknown* v4_identity = nullptr;
    HRESULT hr = window ? device->QueryInterface(IID_PPV_ARGS(&dxgi_device)) : E_FAIL;
    if (SUCCEEDED(hr)) hr = dxgi_device->GetAdapter(&adapter);
    if (SUCCEEDED(hr)) hr = adapter->GetParent(IID_PPV_ARGS(&factory));

    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width = 4;
    desc.Height = 4;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.Scaling = DXGI_SCALING_STRETCH;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    if (SUCCEEDED(hr)) hr = factory->CreateSwapChainForHwnd(device, window, &desc,
        nullptr, nullptr, &swap_chain);
    if (SUCCEEDED(hr)) hr = swap_chain->QueryInterface(IID_PPV_ARGS(&swap_chain4));
    if (SUCCEEDED(hr)) hr = swap_chain->QueryInterface(IID_PPV_ARGS(&swap_chain3));
    if (SUCCEEDED(hr)) hr = swap_chain->QueryInterface(IID_PPV_ARGS(&base_identity));
    if (SUCCEEDED(hr)) hr = swap_chain4->QueryInterface(IID_PPV_ARGS(&v4_identity));

    const bool same_identity = SUCCEEDED(hr) && base_identity == v4_identity;
    HRESULT metadata_hr = E_FAIL;
    HRESULT present_hr = E_FAIL;
    if (same_identity) {
        metadata_hr = swap_chain4->SetHDRMetaData(DXGI_HDR_METADATA_TYPE_NONE, 0, nullptr);
        present_hr = swap_chain4->Present(0, DXGI_PRESENT_TEST);
    }
    std::printf("SwapChain4=%08lx same_identity=%d metadata=%08lx Present(TEST)=%08lx\n",
        hr, same_identity, metadata_hr, present_hr);

    if (v4_identity) v4_identity->Release();
    if (base_identity) base_identity->Release();
    if (swap_chain4) swap_chain4->Release();
    if (swap_chain3) swap_chain3->Release();
    if (swap_chain) swap_chain->Release();
    if (factory) factory->Release();
    if (adapter) adapter->Release();
    if (dxgi_device) dxgi_device->Release();
    if (window) DestroyWindow(window);
    UnregisterClassW(class_name, window_class.hInstance);
    return same_identity && SUCCEEDED(present_hr);
}

int wmain(int argc, wchar_t** argv)
{
    if (argc < 3) {
        std::printf("Usage: context-interop-test.exe <runtime.dll> <0|128|134> [extra.dll ...]\n");
        return 1;
    }
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    SetUnhandledExceptionFilter(crash);
    const unsigned count = std::wcstoul(argv[2], nullptr, 10);
    if (count != 0 && count != 128 && count != 134) return 2;
    // EXE 只导入系统 D3D11，Runtime 位于独立目录，不使用同目录代理冷加载。
    // 随后与挂起目标的热注入顺序一致：Runtime、附加 DLL、设备创建。
    HMODULE system = LoadLibraryExW(L"d3d11.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    HMODULE runtime = LoadLibraryW(argv[1]);
    if (!system || !runtime) return 3;
    for (int i = 3; i < argc; ++i) {
        if (!LoadLibraryW(argv[i])) {
            std::printf("extra DLL load failed=%lu index=%d\n", GetLastError(), i);
            return 4;
        }
    }
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    const auto driver = GetEnvironmentVariableW(L"SSMT_CONTEXT_TEST_HARDWARE", nullptr, 0)
        ? D3D_DRIVER_TYPE_HARDWARE : D3D_DRIVER_TYPE_WARP;
    const HRESULT result = D3D11CreateDevice(nullptr, driver, nullptr, 0, nullptr, 0,
        D3D11_SDK_VERSION, &device, nullptr, &context);
    std::printf("CreateDevice=%08lx context=%p clone=%u\n", result, context, count);
    std::fflush(stdout);
    if (FAILED(result) || !device || !context) return 5;
    const GUID hackerContext = { 0xa3046b1e, 0x336b, 0x4d90, { 0x9f, 0xd6, 0x23, 0x4b, 0xc0, 0x9b, 0x86, 0x87 } };
    IUnknown* wrapper = nullptr;
    if (FAILED(context->QueryInterface(hackerContext, reinterpret_cast<void**>(&wrapper)))) {
        std::printf("Runtime wrapper identity unavailable; this path cannot validate its context calls\n");
        std::fflush(stdout);
        return 7;
    }
    wrapper->Release();

    void** original = *reinterpret_cast<void***>(context);
    // 第三方只认识 COM 接口长度，页内其余入口为零，复现 Bridge 的影子虚表。
    void** shadow = nullptr;
    if (count) {
        shadow = static_cast<void**>(VirtualAlloc(nullptr, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
        if (!shadow) return 6;
        std::memcpy(shadow, original, count * sizeof(void*));
        *reinterpret_cast<void***>(context) = shadow;
    }
    std::array<ID3D11ShaderResourceView*, 64> views{};
    context->VSSetShaderResources(0, static_cast<UINT>(views.size()), views.data());
    context->PSSetShaderResources(0, static_cast<UINT>(views.size()), views.data());
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->Draw(0, 0);
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = desc.Height = 4;
    desc.MipLevels = desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET;
    ID3D11Texture2D* texture = nullptr;
    ID3D11RenderTargetView* view = nullptr;
    if (FAILED(device->CreateTexture2D(&desc, nullptr, &texture))) return 8;
    if (FAILED(device->CreateRenderTargetView(texture, nullptr, &view))) return 9;
    const float color[4]{};
    // 测试 INI 的清屏命令还会经 HackerContext 调用帧分析选项派发。
    context->ClearRenderTargetView(view, color);
    view->Release();
    texture->Release();
    context->ClearState();
    context->Flush();
    const bool swap_chain_ok = test_swap_chain_interfaces(device);
    if (shadow) {
        *reinterpret_cast<void***>(context) = original;
        VirtualFree(shadow, 0, MEM_RELEASE);
    }
    context->Release();
    device->Release();
    std::printf("context calls completed; swap-chain interfaces %s\n",
        swap_chain_ok ? "passed" : "failed");
    std::fflush(stdout);
    return swap_chain_ok ? 0 : 11;
}

#include <Windows.h>
#include <d3d11_4.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>

using Microsoft::WRL::ComPtr;
static void check(HRESULT hr) { if (FAILED(hr)) throw std::runtime_error("D3D11 call failed"); }

struct Surface {
    HWND window = nullptr;
    ComPtr<IDXGISwapChain1> chain;
    Surface(ID3D11Device* device, IUnknown* chain_device) {
        window = CreateWindowExW(0, L"STATIC", L"", WS_POPUP, 0, 0, 4, 4,
            nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (!window) throw std::runtime_error("hidden window failed");
        ComPtr<IDXGIDevice> dxgi; ComPtr<IDXGIAdapter> adapter; ComPtr<IDXGIFactory2> factory;
        check(device->QueryInterface(IID_PPV_ARGS(&dxgi)));
        check(dxgi->GetAdapter(&adapter)); check(adapter->GetParent(IID_PPV_ARGS(&factory)));
        DXGI_SWAP_CHAIN_DESC1 desc{};
        desc.Width = desc.Height = 4; desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1; desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = 2; desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        check(factory->CreateSwapChainForHwnd(chain_device, window, &desc, nullptr, nullptr, &chain));
    }
    void close() {
        if (!chain) return;
        // 帧动作目标不能反向持有交换链，否则双方窗口均关闭后仍会形成引用环。
        if (chain.Detach()->Release() != 0) throw std::runtime_error("swap chain reference cycle");
    }
    ~Surface() { chain.Reset(); if (window) DestroyWindow(window); }
};

struct Image {
    ComPtr<ID3D11Texture2D> texture, staging;
    ComPtr<ID3D11RenderTargetView> view;
    explicit Image(ID3D11Device* device) {
        D3D11_TEXTURE2D_DESC desc = {4,4,1,1,DXGI_FORMAT_R8G8B8A8_UNORM,{1,0},
            D3D11_USAGE_DEFAULT,D3D11_BIND_RENDER_TARGET,0,0};
        check(device->CreateTexture2D(&desc, nullptr, &texture));
        check(device->CreateRenderTargetView(texture.Get(), nullptr, &view));
        desc.Usage = D3D11_USAGE_STAGING; desc.BindFlags = 0; desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        check(device->CreateTexture2D(&desc, nullptr, &staging));
    }
    void capture(ID3D11DeviceContext* context) {
        const float red[] = {1,0,0,1};
        context->ClearRenderTargetView(view.Get(), red);
        // 零顶点调用标记外部帧活动；验证的颜色完全由真实 INI 帧命令改变。
        context->Draw(0, 0);
    }
    void verify(ID3D11DeviceContext* context, unsigned expected) {
        context->CopyResource(staging.Get(), texture.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};
        check(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped));
        const unsigned actual = *static_cast<const unsigned*>(mapped.pData);
        context->Unmap(staging.Get(), 0);
        if (actual != expected) {
            std::printf("pixel=%08x expected=%08x\n", actual, expected);
            throw std::runtime_error("global frame commands used the wrong device");
        }
    }
};

static void present(Surface& surface, bool modern, UINT flags = 0) {
    if (modern) {
        DXGI_PRESENT_PARAMETERS params{};
        check(surface.chain->Present1(0, flags, &params));
    } else check(surface.chain->Present(0, flags));
}

static void run(const std::filesystem::path& runtime, bool auxiliary_first, bool present1,
    const std::wstring& interface_mode) {
    std::ofstream(runtime.parent_path() / L"d3dx.ini") <<
        "[Loader]\ntarget=present-interop-test.exe\n[System]\nload_library_redirect=2\n"
        "allow_platform_update=1\nallow_create_device=1\nconfig_initialization_delay=-1\n"
        "[Logging]\nlog_level=info\n[Hunting]\nhunting=0\n"
        "[ResourceFrame]\n[ClearRenderTargetView]\nResourceFrame=reference this\n"
        "[Present]\nclear=ResourceFrame 0 1 0 1\npost clear=ResourceFrame 0 0 1 1\n";
    if (!LoadLibraryExW(L"d3d11.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32) || !LoadLibraryW(runtime.c_str()))
        throw std::runtime_error("runtime load failed");
    ComPtr<ID3D11Device> game, auxiliary;
    ComPtr<ID3D11DeviceContext> game_context, auxiliary_context;
    check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
        D3D11_SDK_VERSION, &game, nullptr, &game_context));
    check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
        D3D11_SDK_VERSION, &auxiliary, nullptr, &auxiliary_context));
    const GUID hacker_device = {0x83ffd841,0xa5c9,0x46f4,{0x81,0x09,0xbc,0x25,0x95,0x58,0xfe,0xf4}};
    ComPtr<IUnknown> identity, auxiliary_identity;
    check(game->QueryInterface(hacker_device, &identity));
    check(auxiliary->QueryInterface(hacker_device, &auxiliary_identity));
    Image game_image(game.Get()), auxiliary_image(auxiliary.Get());
    ComPtr<ID3D11Device3> modern; check(auxiliary.As(&modern));
    ComPtr<ID3D11Device> raw; auxiliary_image.texture->GetDevice(&raw);
    IUnknown* auxiliary_pointer = interface_mode == L"raw" ? static_cast<IUnknown*>(raw.Get()) :
        interface_mode == L"modern" ? static_cast<IUnknown*>(modern.Get()) : auxiliary.Get();
    std::unique_ptr<Surface> aux_surface, game_surface;
    if (auxiliary_first) aux_surface = std::make_unique<Surface>(auxiliary.Get(), auxiliary_pointer);
    game_surface = std::make_unique<Surface>(game.Get(), game.Get());
    if (!auxiliary_first) aux_surface = std::make_unique<Surface>(auxiliary.Get(), auxiliary_pointer);
    // 初始化允许无绘制的辅助 Present；随后活动设备必须覆盖创建顺序与接口形态的影响。
    present(*aux_surface, present1);
    game_image.capture(game_context.Get());
    present(*aux_surface, present1, DXGI_PRESENT_TEST);
    game_image.verify(game_context.Get(), 0xff0000ff);
    for (int frame = 0; frame < 6; ++frame) {
        game_image.capture(game_context.Get());
        present(*aux_surface, present1);
        game_image.verify(game_context.Get(), 0xffff0000);
        present(*game_surface, present1);
        game_image.verify(game_context.Get(), 0xffff0000);
    }
    std::puts("alternating Present pixels: PASS");
    // 已确定渲染设备后，新辅助交换链不能夺走全局命令；关闭它也不能结束原会话。
    Surface later(auxiliary.Get(), auxiliary_pointer);
    game_image.capture(game_context.Get()); present(later, present1);
    game_image.verify(game_context.Get(), 0xffff0000);
    later.close();
    std::puts("late auxiliary chain: PASS");
    // 所有者退出后允许剩余交换链接管；Device / Context 快照不得悬空或锁住旧交换链。
    game_surface->close();
    auxiliary_image.capture(auxiliary_context.Get());
    present(*aux_surface, present1);
    auxiliary_image.verify(auxiliary_context.Get(), 0xffff0000);
    aux_surface->close();
    std::puts("owner removal and takeover: PASS");
    // 全部关闭后重新创建交换链，应得到新的会话。
    Surface restarted(game.Get(), game.Get());
    game_image.capture(game_context.Get()); present(restarted, present1);
    game_image.verify(game_context.Get(), 0xffff0000); restarted.close();
    std::puts("restarted session: PASS");
}

int wmain(int argc, wchar_t** argv) {
    if (argc != 5 || GetEnvironmentVariableW(L"SSMT_CONTEXT_TEST_HARDWARE", nullptr, 0)) return 2;
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    setvbuf(stdout, nullptr, _IONBF, 0);
    try { run(std::filesystem::absolute(argv[1]), std::wstring(argv[2]) == L"aux-first",
        std::wstring(argv[3]) == L"Present1", argv[4]); }
    catch (const std::exception& error) { std::printf("FAIL: %s\n", error.what()); return 7; }
    std::puts("global frame resource checks completed: PASS");
    return 0;
}

#include <Windows.h>
#include <d3d11_4.h>
#include <cstdio>
#include <initializer_list>

static const GUID hacker_context = {0xa3046b1e, 0x336b, 0x4d90, {0x9f, 0xd6, 0x23, 0x4b, 0xc0, 0x9b, 0x86, 0x87}};

static bool check_commands(ID3D11Device* device, ID3D11DeviceContext* context)
{
    IUnknown* identity = nullptr;
    if (FAILED(context->QueryInterface(hacker_context, reinterpret_cast<void**>(&identity)))) return false;
    identity->Release();
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = desc.Height = desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
    desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET;
    ID3D11Texture2D *target = nullptr, *readback = nullptr;
    ID3D11RenderTargetView* view = nullptr;
    bool ok = SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &target));
    if (ok) ok = SUCCEEDED(device->CreateRenderTargetView(target, nullptr, &view));
    desc.BindFlags = 0; desc.Usage = D3D11_USAGE_STAGING; desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    if (ok) ok = SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &readback));
    if (ok) {
        const float black[4]{};
        context->ClearRenderTargetView(view, black);
        context->CopyResource(readback, target);
        ID3D11DeviceContext* reader = context;
        if (context->GetType() == D3D11_DEVICE_CONTEXT_DEFERRED) {
            ID3D11CommandList* commands = nullptr;
            ok = SUCCEEDED(context->FinishCommandList(FALSE, &commands));
            device->GetImmediateContext(&reader);
            if (ok) { reader->ExecuteCommandList(commands, FALSE); commands->Release(); }
        }
        D3D11_MAPPED_SUBRESOURCE mapped{};
        ok = ok && SUCCEEDED(reader->Map(readback, 0, D3D11_MAP_READ, 0, &mapped));
        if (ok) {
            const auto* color = static_cast<const float*>(mapped.pData);
            ok = color[0] == 1 && color[1] == 0 && color[2] == 0 && color[3] == 1;
            reader->Unmap(readback, 0);
        }
        if (reader != context) reader->Release();
    }
    if (view) view->Release();
    if (target) target->Release();
    if (readback) readback->Release();
    return ok;
}

int wmain(int argc, wchar_t** argv)
{
    if (argc != 2 || GetEnvironmentVariableW(L"SSMT_CONTEXT_TEST_HARDWARE", nullptr, 0)) return 2;
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    if (!LoadLibraryExW(L"d3d11.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32) || !LoadLibraryW(argv[1])) return 3;
    ID3D11Device* device = nullptr;
    // ReShade 创建设备时不请求立即 Context，之后再取回并升级接口。
    if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
        D3D11_SDK_VERSION, &device, nullptr, nullptr))) return 4;
    bool ok = true;
    // 先调用升级后的尾部方法，避免由基础接口提前初始化掩盖延迟绑定缺陷。
    ID3D11Device3* modern = nullptr;
    if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&modern)))) {
        ID3D11DeviceContext3* immediate = nullptr;
        modern->GetImmediateContext3(&immediate);
        ok = immediate && check_commands(device, immediate);
        if (immediate) immediate->Release();
        ID3D11DeviceContext3* deferred = nullptr;
        const bool passed = SUCCEEDED(modern->CreateDeferredContext3(0, &deferred)) &&
            deferred && check_commands(device, deferred);
        std::printf("modern immediate/deferred commands: %s\n", ok && passed ? "PASS" : "BYPASS");
        ok = ok && passed;
        if (deferred) deferred->Release();
        modern->Release();
    }
    for (const auto iid : {__uuidof(ID3D11Device), __uuidof(ID3D11Device1), __uuidof(ID3D11Device2),
        __uuidof(ID3D11Device3), __uuidof(ID3D11Device4), __uuidof(ID3D11Device5), __uuidof(IUnknown)}) {
        IUnknown* alias = nullptr;
        ID3D11Device* roundtrip = nullptr;
        ID3D11DeviceContext* context = nullptr;
        if (SUCCEEDED(device->QueryInterface(iid, reinterpret_cast<void**>(&alias)))) {
            if (SUCCEEDED(alias->QueryInterface(IID_PPV_ARGS(&roundtrip)))) {
                roundtrip->GetImmediateContext(&context);
                const bool passed = context && check_commands(roundtrip, context);
                std::printf("device upgrade %08lx: %s\n", iid.Data1, passed ? "PASS" : "BYPASS");
                ok = ok && passed;
                if (context) context->Release();
                roundtrip->Release();
            } else ok = false;
            alias->Release();
        }
    }
    ID3D11DeviceContext* context = nullptr;
    device->GetImmediateContext(&context);
    for (const auto iid : {__uuidof(IUnknown), __uuidof(ID3D11DeviceChild), __uuidof(ID3D11DeviceContext1),
        __uuidof(ID3D11DeviceContext2), __uuidof(ID3D11DeviceContext3), __uuidof(ID3D11DeviceContext4)}) {
        IUnknown* alias = nullptr;
        ID3D11DeviceContext* roundtrip = nullptr;
        if (SUCCEEDED(context->QueryInterface(iid, reinterpret_cast<void**>(&alias)))) {
            const bool passed = SUCCEEDED(alias->QueryInterface(IID_PPV_ARGS(&roundtrip))) && check_commands(device, roundtrip);
            std::printf("context upgrade %08lx: %s\n", iid.Data1, passed ? "PASS" : "BYPASS");
            ok = ok && passed;
            if (roundtrip) roundtrip->Release();
            alias->Release();
        }
    }
    context->Release(); device->Release();
    return ok ? 0 : 7;
}

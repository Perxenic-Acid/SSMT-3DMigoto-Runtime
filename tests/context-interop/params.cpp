#include <Windows.h>
#include <d3d11_4.h>
#include <d3dcompiler.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

using Microsoft::WRL::ComPtr;

static void check(HRESULT hr)
{
    if (FAILED(hr)) {
        std::printf("HRESULT=%08lx\n", hr);
        throw std::runtime_error("D3D11 call failed");
    }
}

static ComPtr<ID3DBlob> compile(const char* source, const char* profile)
{
    ComPtr<ID3DBlob> code, errors;
    const HRESULT hr = D3DCompile(source, strlen(source), nullptr, nullptr, nullptr,
        "main", profile, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors);
    if (errors) std::printf("%s\n", static_cast<const char*>(errors->GetBufferPointer()));
    check(hr);
    return code;
}

struct Surface {
    HWND window = nullptr;
    ComPtr<IDXGISwapChain1> chain;
    explicit Surface(ID3D11Device* device)
    {
        window = CreateWindowExW(0, L"STATIC", L"", WS_POPUP, 0, 0, 4, 4,
            nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (!window) throw std::runtime_error("hidden window failed");
        ComPtr<IDXGIDevice> dxgi;
        ComPtr<IDXGIAdapter> adapter;
        ComPtr<IDXGIFactory2> factory;
        check(device->QueryInterface(IID_PPV_ARGS(&dxgi)));
        check(dxgi->GetAdapter(&adapter));
        check(adapter->GetParent(IID_PPV_ARGS(&factory)));
        DXGI_SWAP_CHAIN_DESC1 desc{};
        desc.Width = desc.Height = 4;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = 2;
        desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        check(factory->CreateSwapChainForHwnd(device, window, &desc, nullptr, nullptr, &chain));
    }
    ~Surface() { chain.Reset(); DestroyWindow(window); }
};

static void write_config(const std::filesystem::path& directory, unsigned long long hash,
    bool early, int last_parameter, int constant)
{
    std::ofstream root(directory / L"d3dx.ini");
    root << "[Loader]\ntarget=params-interop-test.exe\n[System]\nload_library_redirect=2\n"
        "allow_platform_update=1\nallow_create_device=1\ncheck_foreground_window=0\n"
        "skip_early_includes_load=1\nconfig_initialization_delay=" << (early ? -1 : 0)
        << "\n[Rendering]\ntexture_hash=0\n[Logging]\nlog_level=info\n[Hunting]\nhunting=0\n"
        "[Include]\ninclude=avatar.ini\n";
    std::ofstream included(directory / L"avatar.ini");
    // 重载键首次出现在延迟 include 内；测试只读取反向按键，不向桌面发按键。
    included << "[Hunting]\nreload_config=no_VK_F24\n";
    if (last_parameter < 0) return;
    included << "[Constants]\nglobal $fix=0\nx1=7\nx2=" << constant << "\nx" << last_parameter
        << "=0\n[ClearRenderTargetView]\nx1=13\n[ShaderOverrideAvatar]\nhash="
        << std::hex << std::setw(16) << std::setfill('0') << hash
        << "\nif $fix == 1\npost run=CustomShaderAvatar\npost $fix=0\nendif\n"
        "[ShaderRegexTextures]\nshader_model=vs_5_0\nrun=CommandListTextures\n"
        "[CommandListTextures]\nchecktextureoverride=ps-t0\n"
        "[TextureOverridePortrait]\nhash=d20f0658\n$fix=1\nhandling=skip\nthis=ResourceGreen\n"
        "[ResourceGreen]\nfilename=green.dds\n[CustomShaderAvatar]\nvs=ui-fix.hlsl\n"
        "vs-t0=vb0\nx0=first_vertex\ndraw=from_caller\n";
}

static void run(const std::filesystem::path& runtime, bool early)
{
    const auto directory = runtime.parent_path();
    const char vs_source[] =
        "struct V{float3 p:POSITION;float4 c:COLOR;float2 uv:TEXCOORD;};"
        "struct O{float4 p:SV_Position;float2 uv:TEXCOORD;};"
        "O main(V v){O o;o.p=float4(v.p.xy,0,1);o.uv=v.uv;return o;}";
    const char ps_source[] = "Texture2D<float4> t:register(t0);SamplerState s:register(s0);"
        "float4 main(float4 p:SV_Position,float2 uv:TEXCOORD):SV_Target{return t.Sample(s,uv);}";
    const auto vs_code = compile(vs_source, "vs_5_0"), ps_code = compile(ps_source, "ps_5_0");
    unsigned long long hash = 0;
    const auto bytes = static_cast<const unsigned char*>(vs_code->GetBufferPointer());
    for (size_t i = 0; i < vs_code->GetBufferSize(); ++i) { hash *= 0x100000001b3ULL; hash ^= bytes[i]; }
    write_config(directory, hash, early, 140, 7);
    // 与头像 Mod 一样，从 24 字节 VB 和 t120 读取非零 first_vertex，再 post 重绘。
    std::ofstream(directory / L"ui-fix.hlsl") <<
        "Texture1D<float4> IniParams:register(t120);struct VB{float3 p;uint c;float2 uv;};"
        "StructuredBuffer<VB> vertices:register(t0);struct V{float3 p:POSITION;float4 c:COLOR;float2 uv:TEXCOORD;};"
        "struct O{float4 p:SV_Position;float2 uv:TEXCOORD;};O main(V v){O o;"
        "VB first=vertices[(uint)IniParams[0].x];o.p=float4(v.p.xy,0,1);"
        "if(first.p.x!=-1 || first.uv.y!=1 || first.c!=0xffffffff || IniParams[1].x!=13 || IniParams[2].x!=v.p.z)"
        "o.p.xy=0;o.uv=v.uv;return o;}";
    const unsigned header[32] = {0x20534444,124,0x100f,4,4,16,0,1,0,0,0,0,0,0,0,0,0,0,0,32,0x41,0,32,
        0xff,0xff00,0xff0000,0xff000000,0x1000,0,0,0,0};
    std::array<unsigned, 16> pixels;
    pixels.fill(0xff00ff00);
    {
        std::ofstream dds(directory / L"green.dds", std::ios::binary);
        dds.write(reinterpret_cast<const char*>(header), sizeof(header));
        dds.write(reinterpret_cast<const char*>(pixels.data()), sizeof(pixels));
    }
    if (!LoadLibraryExW(L"d3d11.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32) || !LoadLibraryW(runtime.c_str()))
        throw std::runtime_error("runtime load failed");

    ComPtr<ID3D11Device> device, auxiliary;
    ComPtr<ID3D11DeviceContext> immediate, auxiliary_context, deferred;
    check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
        D3D11_SDK_VERSION, &device, nullptr, &immediate));
    check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
        D3D11_SDK_VERSION, &auxiliary, nullptr, &auxiliary_context));
    const GUID hacker_device = {0x83ffd841,0xa5c9,0x46f4,{0x81,0x09,0xbc,0x25,0x95,0x58,0xfe,0xf4}};
    // IID 与 Runtime 一致；只载入 DLL 而未接管设备时不能计为通过。
    ComPtr<IUnknown> identity, auxiliary_identity;
    check(device->QueryInterface(hacker_device, &identity));
    check(auxiliary->QueryInterface(hacker_device, &auxiliary_identity));
    ComPtr<ID3D11Device3> modern;
    ComPtr<ID3D11DeviceContext3> deferred3;
    check(device.As(&modern));
    check(modern->CreateDeferredContext3(0, &deferred3));
    deferred = deferred3;
    Surface auxiliary_surface(auxiliary.Get()), game_surface(device.Get());
    // 两台设备和延迟 Context 都已存在，由辅助 Present 首先加载完整 Mod 配置。
    check(auxiliary_surface.chain->Present(0, 0));

    ComPtr<ID3D11VertexShader> vs;
    ComPtr<ID3D11PixelShader> ps;
    ComPtr<ID3D11InputLayout> layout;
    check(device->CreateVertexShader(vs_code->GetBufferPointer(), vs_code->GetBufferSize(), nullptr, &vs));
    check(device->CreatePixelShader(ps_code->GetBufferPointer(), ps_code->GetBufferSize(), nullptr, &ps));
    const D3D11_INPUT_ELEMENT_DESC elements[] = {
        {"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"COLOR",0,DXGI_FORMAT_R8G8B8A8_UNORM,0,12,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,16,D3D11_INPUT_PER_VERTEX_DATA,0}};
    check(device->CreateInputLayout(elements, 3, vs_code->GetBufferPointer(), vs_code->GetBufferSize(), &layout));
    struct Vertex { float x,y,z; unsigned color; float u,v; };
    static_assert(sizeof(Vertex) == 24);
    std::array<unsigned char, 512> vertices{};
    const unsigned short indices[] = {0,1,2,0,2,3};
    D3D11_BUFFER_DESC bd = {512,D3D11_USAGE_DEFAULT,D3D11_BIND_VERTEX_BUFFER,0,0,0};
    ComPtr<ID3D11Buffer> vb, ib;
    check(device->CreateBuffer(&bd, nullptr, &vb));
    bd.ByteWidth = sizeof(indices); bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
    D3D11_SUBRESOURCE_DATA data = {indices,0,0};
    check(device->CreateBuffer(&bd, &data, &ib));
    pixels.fill(0xffff0000);
    D3D11_TEXTURE2D_DESC td = {4,4,1,1,DXGI_FORMAT_R8G8B8A8_UNORM,{1,0},D3D11_USAGE_DEFAULT,D3D11_BIND_SHADER_RESOURCE,0,0};
    ComPtr<ID3D11Texture2D> original, target, staging;
    data = {pixels.data(),16,64};
    check(device->CreateTexture2D(&td, &data, &original));
    ComPtr<ID3D11ShaderResourceView> srv;
    check(device->CreateShaderResourceView(original.Get(), nullptr, &srv));
    td.BindFlags = D3D11_BIND_RENDER_TARGET;
    check(device->CreateTexture2D(&td, nullptr, &target));
    td.BindFlags = 0; td.Usage = D3D11_USAGE_STAGING; td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    check(device->CreateTexture2D(&td, nullptr, &staging));
    ComPtr<ID3D11RenderTargetView> rtv;
    check(device->CreateRenderTargetView(target.Get(), nullptr, &rtv));
    D3D11_RASTERIZER_DESC rd{}; rd.FillMode = D3D11_FILL_SOLID; rd.CullMode = D3D11_CULL_NONE; rd.DepthClipEnable = TRUE;
    ComPtr<ID3D11RasterizerState> rasterizer;
    check(device->CreateRasterizerState(&rd, &rasterizer));
    D3D11_SAMPLER_DESC sd{}; sd.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP; sd.MaxLOD = D3D11_FLOAT32_MAX;
    ComPtr<ID3D11SamplerState> sampler;
    check(device->CreateSamplerState(&sd, &sampler));

    for (int phase = 0; phase < 5; ++phase) {
        const int last_parameter[] = {140,140,191,3,-1};
        const int constant = 7 + phase;
        if (phase) {
            write_config(directory, hash, early, last_parameter[phase], constant);
            check(auxiliary_surface.chain->Present(0, 0));
        }
        Vertex quad[] = {{-1,-1,float(constant),0xffffffff,0,1},{-1,1,float(constant),0xffffffff,0,0},
            {1,1,float(constant),0xffffffff,1,0},{1,-1,float(constant),0xffffffff,1,1}};
        memcpy(vertices.data() + 48, quad, sizeof(quad));
        memcpy(vertices.data() + 192, quad, sizeof(quad));
        immediate->UpdateSubresource(vb.Get(), 0, nullptr, vertices.data(), 0, 0);
        for (auto context : {immediate.Get(), deferred.Get()}) {
            for (int variant = 0; variant < 3; ++variant) {
                const UINT stride = 24, offset = variant == 0 ? 0 : variant == 1 ? 48 : 192;
                const auto raw_vb = vb.Get(); const auto raw_srv = srv.Get(); const auto raw_rtv = rtv.Get();
                const auto raw_sampler = sampler.Get();
                const float black[4] = {0,0,0,1};
                context->ClearRenderTargetView(rtv.Get(), black);
                context->IASetInputLayout(layout.Get());
                context->IASetVertexBuffers(0, 1, &raw_vb, &stride, &offset);
                context->IASetIndexBuffer(ib.Get(), DXGI_FORMAT_R16_UINT, 0);
                context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                context->VSSetShader(vs.Get(), nullptr, 0); context->PSSetShader(ps.Get(), nullptr, 0);
                context->PSSetShaderResources(0, 1, &raw_srv); context->PSSetSamplers(0, 1, &raw_sampler);
                context->OMSetRenderTargets(1, &raw_rtv, nullptr); context->RSSetState(rasterizer.Get());
                const D3D11_VIEWPORT viewport = {0,0,4,4,0,1}; context->RSSetViewports(1, &viewport);
                context->DrawIndexed(6, 0, variant == 0 ? 2 : 0);
                ComPtr<ID3D11ShaderResourceView> params;
                context->VSGetShaderResources(120, 1, &params);
                if ((params != nullptr) != (phase < 4)) throw std::runtime_error("IniParams binding mismatch");
                if (params) {
                    ComPtr<ID3D11Resource> resource; ComPtr<ID3D11Texture1D> texture;
                    params->GetResource(&resource); check(resource.As(&texture));
                    D3D11_TEXTURE1D_DESC desc{}; texture->GetDesc(&desc);
                    if (desc.Width != last_parameter[phase] + 1) throw std::runtime_error("stale IniParams size");
                }
                if (context == deferred.Get()) {
                    ComPtr<ID3D11CommandList> commands;
                    check(context->FinishCommandList(TRUE, &commands));
                    immediate->ExecuteCommandList(commands.Get(), TRUE);
                }
                immediate->CopyResource(staging.Get(), target.Get());
                D3D11_MAPPED_SUBRESOURCE mapped{};
                check(immediate->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped));
                const unsigned actual = *static_cast<const unsigned*>(mapped.pData);
                immediate->Unmap(staging.Get(), 0);
                const unsigned expected = phase < 4 ? 0xff00ff00 : 0xffff0000;
                std::printf("phase=%d deferred=%d offset=%u pixel=%08x expected=%08x\n",
                    phase, context == deferred.Get(), offset, actual, expected);
                if (actual != expected) throw std::runtime_error("avatar redraw pixel mismatch");
            }
        }
    }
    deferred->ClearState(); immediate->ClearState(); immediate->Flush();
}

int wmain(int argc, wchar_t** argv)
{
    if (argc != 3 || GetEnvironmentVariableW(L"SSMT_CONTEXT_TEST_HARDWARE", nullptr, 0)) return 2;
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    try { run(std::filesystem::absolute(argv[1]), std::wstring(argv[2]) == L"early"); }
    catch (const std::exception& error) { std::printf("FAIL: %s\n", error.what()); return 7; }
    std::puts("per-device IniParams checks completed: PASS");
    return 0;
}

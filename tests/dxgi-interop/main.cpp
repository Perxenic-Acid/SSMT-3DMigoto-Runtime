#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <dbghelp.h>
#include <cstdio>

static LONG WINAPI crash(EXCEPTION_POINTERS* exception) {
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    wchar_t* end = wcsrchr(path, L'\\');
    if (!end) return EXCEPTION_EXECUTE_HANDLER;
    wcscpy_s(end + 1, MAX_PATH - (end + 1 - path), L"interop-crash.dmp");
    HANDLE file = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    MINIDUMP_EXCEPTION_INFORMATION info{GetCurrentThreadId(), exception, FALSE};
    if (file != INVALID_HANDLE_VALUE) {
        MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file, MiniDumpNormal, &info, nullptr, nullptr);
        CloseHandle(file);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

static HMODULE load(const wchar_t* name) {
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    wchar_t* end = wcsrchr(path, L'\\');
    if (!end) return nullptr;
    wcscpy_s(end + 1, MAX_PATH - (end + 1 - path), name);
    return LoadLibraryW(path);
}

int wmain(int argc, wchar_t** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    SetUnhandledExceptionFilter(crash);
    if (argc != 2 || (wcscmp(argv[1], L"plain") != 0 && wcscmp(argv[1], L"mod") != 0 &&
        wcscmp(argv[1], L"reshade") != 0 && wcscmp(argv[1], L"both") != 0)) {
        std::printf("Usage: interop-test.exe plain|mod|reshade|both\n");
        return 1;
    }
    // 动态加载系统库，避免导入表在进入 main 前就加载同目录的 Mod DLL，
    // 从而改变本测试需要验证的 ReShade 预加载顺序。
    HMODULE system_d3d = LoadLibraryExW(L"d3d11.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!system_d3d) return 6;
    bool reshade = argc > 1 && wcscmp(argv[1], L"plain") != 0 && wcscmp(argv[1], L"mod") != 0;
    bool mod = argc > 1 && (wcscmp(argv[1], L"both") == 0 || wcscmp(argv[1], L"mod") == 0);
    if (reshade && !load(L"ReShade64.dll")) { std::printf("ReShade load failed: %lu\n", GetLastError()); return 2; }
    if (mod && !load(L"d3d11.dll")) { std::printf("Mod load failed: %lu\n", GetLastError()); return 3; }
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    auto create_device = reinterpret_cast<PFN_D3D11_CREATE_DEVICE>(GetProcAddress(system_d3d, "D3D11CreateDevice"));
    HRESULT hr = create_device(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
        D3D11_SDK_VERSION, &device, nullptr, &context);
    std::printf("CreateDevice=%08lx device=%p\n", hr, device); std::fflush(stdout);
    if (FAILED(hr)) return 4;
    IDXGIDevice* dxgi = nullptr;
    IDXGIAdapter* adapter = nullptr;
    IDXGIOutput* output = nullptr;
    IDXGIOutput1* output1 = nullptr;
    IDXGIOutputDuplication* duplication = nullptr;
    hr = device->QueryInterface(IID_PPV_ARGS(&dxgi));
    if (SUCCEEDED(hr)) hr = dxgi->GetAdapter(&adapter);
    if (SUCCEEDED(hr)) hr = adapter->EnumOutputs(0, &output);
    if (SUCCEEDED(hr)) hr = output->QueryInterface(IID_PPV_ARGS(&output1));
    std::printf("Output setup=%08lx\n", hr); std::fflush(stdout);
    // 只创建复制接口，不取得或保存桌面帧。
    if (SUCCEEDED(hr)) hr = output1->DuplicateOutput(device, &duplication);
    std::printf("DuplicateOutput=%08lx duplication=%p\n", hr, duplication); std::fflush(stdout);
    if (duplication) duplication->Release();
    if (output1) output1->Release();
    if (output) output->Release();
    if (adapter) adapter->Release();
    if (dxgi) dxgi->Release();
    context->Release(); device->Release();
    return SUCCEEDED(hr) ? 0 : 5;
}

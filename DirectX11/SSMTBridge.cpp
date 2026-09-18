#include "SSMTBridge.h"

#include <Windows.h>

#include "log.h"

namespace
{
    using PluginHostOnD3D11ReadyFn =
        DWORD(WINAPI *)(
            void *device,
            void *immediateContext,
            void *swapChain);

    PluginHostOnD3D11ReadyFn
    ResolveD3D11ReadyCallback()
    {
        HMODULE host =
            GetModuleHandleW(
                L"SSMT-PluginHost.dll");

        if (!host)
        {
            LogInfo(
                "[SSMT] PluginHost is not loaded.\n");

            return nullptr;
        }

        FARPROC proc =
            GetProcAddress(
                host,
                "SSMTPluginHost_OnD3D11Ready");

        if (!proc)
        {
            LogInfo(
                "[SSMT] SSMTPluginHost_OnD3D11Ready wad not found.\n");

            return nullptr;
        }

        return reinterpret_cast<
            PluginHostOnD3D11ReadyFn>(proc);
    }
}

void SSMTBridge::NotifyD3D11Ready(
    ID3D11Device *device,
    ID3D11DeviceContext *immediateContext,
    IDXGISwapChain *swapChain)
{
    static PluginHostOnD3D11ReadyFn callback = nullptr;

    if (!callback)
        callback = ResolveD3D11ReadyCallback();

    if (!callback)
        return;

    DWORD status =
        callback(
            device,
            immediateContext,
            swapChain);

    LogInfo(
        "[SSMT] D3D11Ready  dispatched to PluginHost, status=0x%08X\n",
        status);
}
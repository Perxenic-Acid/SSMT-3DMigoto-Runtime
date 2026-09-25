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

    using PluginHostOnPresentFn =
        DWORD(WINAPI *)(
            void *device,
            void *immediateContext,
            void *swapChain,
            UINT syncInterval,
            UINT flags);

    PluginHostOnPresentFn
    ResolvePresentCallback()
    {
        HMODULE host =
            GetModuleHandleW(
                L"SSMT-PluginHost.dll");

        if (!host)
        {
            LogInfo(
                "[SSMT] PluginHost is not loaded; Present bridge disabled.\n");

            return nullptr;
        }

        FARPROC proc =
            GetProcAddress(
                host,
                "SSMTPluginHost_OnPresent");

        if (!proc)
        {
            LogInfo(
                "[SSMT] SSMTPluginHost_OnPresent was not found; Present bridge disabled.\n");

            return nullptr;
        }

        return reinterpret_cast<
            PluginHostOnPresentFn>(proc);
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

void SSMTBridge::NotifyPresent(
    ID3D11Device *device,
    ID3D11DeviceContext *immediateContext,
    IDXGISwapChain *swapChain,
    UINT syncInterval,
    UINT flags)
{
    static const PluginHostOnPresentFn callback =
        ResolvePresentCallback();

    if (!callback)
        return;

    callback(
        device,
        immediateContext,
        swapChain,
        syncInterval,
        flags);
}

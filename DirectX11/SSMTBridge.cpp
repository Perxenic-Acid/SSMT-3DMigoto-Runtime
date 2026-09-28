#include "SSMTBridge.h"

#include <Windows.h>

#include <atomic>

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
                "[SSMT] SSMTPluginHost_OnD3D11Ready was not found.\n");

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

    PluginHostOnPresentFn
    GetPresentCallback()
    {
        static std::atomic<PluginHostOnPresentFn> cachedCallback = nullptr;
        static std::atomic<unsigned> retryCounter = 0;

        if (PluginHostOnPresentFn callback =
                cachedCallback.load(std::memory_order_acquire))
            return callback;

        // A Host injected after Runtime startup is uncommon. Retry discovery at a
        // bounded cadence so the Present path does not call GetModuleHandle on
        // every frame while still covering late injection.
        if (retryCounter.fetch_add(1, std::memory_order_relaxed) % 300 != 0)
            return nullptr;

        PluginHostOnPresentFn callback = ResolvePresentCallback();
        cachedCallback.store(callback, std::memory_order_release);
        return callback;
    }
}

void SSMTBridge::NotifyD3D11Ready(
    ID3D11Device *device,
    ID3D11DeviceContext *immediateContext,
    IDXGISwapChain *swapChain)
{
    // Device creation can race PluginHost injection. Resolve on every device
    // notification so a Host loaded after the first swap chain is still seen.
    PluginHostOnD3D11ReadyFn callback =
        ResolveD3D11ReadyCallback();

    if (!callback)
        return;

    DWORD status =
        callback(
            device,
            immediateContext,
            swapChain);

    LogInfo(
        "[SSMT] D3D11Ready dispatched to PluginHost, status=0x%08X\n",
        status);
}

void SSMTBridge::NotifyPresent(
    ID3D11Device *device,
    ID3D11DeviceContext *immediateContext,
    IDXGISwapChain *swapChain,
    UINT syncInterval,
    UINT flags)
{
    PluginHostOnPresentFn callback =
        GetPresentCallback();

    if (!callback)
        return;

    callback(
        device,
        immediateContext,
        swapChain,
        syncInterval,
        flags);
}

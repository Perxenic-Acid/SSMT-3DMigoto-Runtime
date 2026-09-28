#pragma once

#include <d3d11.h>
#include <dxgi.h>

namespace SSMTBridge
{
    // These pointers are Runtime-owned wrapper interfaces. The bridge borrows
    // them for the callback and does not transfer COM ownership to Native.
    void NotifyD3D11Ready(
        ID3D11Device *device,
        ID3D11DeviceContext *immediateContext,
        IDXGISwapChain *swapChain);

    void NotifyPresent(
        ID3D11Device *device,
        ID3D11DeviceContext *immediateContext,
        IDXGISwapChain *swapChain,
        UINT syncInterval,
        UINT flags);
} // namespace SSMTBridge

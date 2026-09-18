#pragma once

#include <d3d11.h>
#include <dxgi.h>

namespace SSMTBridge
{
    void NotifyD3D11Ready(
        ID3D11Device *device,
        ID3D11DeviceContext *immediateContext,
        IDXGISwapChain *swapChain);
} // namespace SSMTBridge

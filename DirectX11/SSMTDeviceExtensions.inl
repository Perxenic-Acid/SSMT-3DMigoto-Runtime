// 尾部 COM 方法保留新增能力；资源创建与继承的 Draw / 绑定仍须经过 Runtime。

void STDMETHODCALLTYPE HackerDevice::GetImmediateContext2(ID3D11DeviceContext2 **ppImmediateContext)
{
    if (!ppImmediateContext) return;
    *ppImmediateContext = nullptr;
    ID3D11DeviceContext* context = nullptr;
    GetImmediateContext(&context);
    if (context) { context->QueryInterface(IID_PPV_ARGS(ppImmediateContext)); context->Release(); }
}


HRESULT STDMETHODCALLTYPE HackerDevice::CreateDeferredContext2(UINT ContextFlags, ID3D11DeviceContext2 **ppDeferredContext)
{
    if (!ppDeferredContext) return E_INVALIDARG;
    *ppDeferredContext = nullptr;
    ID3D11DeviceContext2* context = nullptr;
    HRESULT hr = mExtendedDevice2->CreateDeferredContext2(ContextFlags, &context);
    if (FAILED(hr)) return hr;
    auto* wrapped = HackerContextFactory(mRealOrigDevice1, context);
    wrapped->SetHackerDevice(this);
    wrapped->Bind3DMigotoResources();
    *ppDeferredContext = wrapped;
    return hr;
}


void STDMETHODCALLTYPE HackerDevice::GetResourceTiling(ID3D11Resource *pTiledResource, UINT *pNumTilesForEntireResource, D3D11_PACKED_MIP_DESC *pPackedMipDesc, D3D11_TILE_SHAPE *pStandardTileShapeForNonPackedMips, UINT *pNumSubresourceTilings, UINT FirstSubresourceTilingToGet, D3D11_SUBRESOURCE_TILING *pSubresourceTilingsForNonPackedMips)
{
    return mExtendedDevice2->GetResourceTiling(pTiledResource, pNumTilesForEntireResource, pPackedMipDesc, pStandardTileShapeForNonPackedMips, pNumSubresourceTilings, FirstSubresourceTilingToGet, pSubresourceTilingsForNonPackedMips);
}


HRESULT STDMETHODCALLTYPE HackerDevice::CheckMultisampleQualityLevels1(DXGI_FORMAT Format, UINT SampleCount, UINT Flags, UINT *pNumQualityLevels)
{
    return mExtendedDevice2->CheckMultisampleQualityLevels1(Format, SampleCount, Flags, pNumQualityLevels);
}


HRESULT STDMETHODCALLTYPE HackerDevice::CreateTexture2D1(const D3D11_TEXTURE2D_DESC1 *pDesc1, const D3D11_SUBRESOURCE_DATA *pInitialData, ID3D11Texture2D1 **ppTexture2D)
{
    if (!pDesc1) return mExtendedDevice3->CreateTexture2D1(pDesc1, pInitialData, ppTexture2D);
    D3D11_TEXTURE2D_DESC desc;
    memcpy(&desc, pDesc1, sizeof(desc));
    ID3D11Texture2D* texture = nullptr;
    const HRESULT hr = CreateTexture2DImpl(&desc, pInitialData, ppTexture2D ? &texture : nullptr, pDesc1);
    // Impl 的新版分支直接持有 CreateTexture2D1 返回的接口及其调用方引用。
    if (ppTexture2D) *ppTexture2D = static_cast<ID3D11Texture2D1*>(texture);
    return hr;
}


HRESULT STDMETHODCALLTYPE HackerDevice::CreateTexture3D1(const D3D11_TEXTURE3D_DESC1 *pDesc1, const D3D11_SUBRESOURCE_DATA *pInitialData, ID3D11Texture3D1 **ppTexture3D)
{
    if (!pDesc1) return mExtendedDevice3->CreateTexture3D1(pDesc1, pInitialData, ppTexture3D);
    D3D11_TEXTURE3D_DESC desc;
    memcpy(&desc, pDesc1, sizeof(desc));
    ID3D11Texture3D* texture = nullptr;
    const HRESULT hr = CreateTexture3DImpl(&desc, pInitialData, ppTexture3D ? &texture : nullptr, pDesc1);
    if (ppTexture3D) *ppTexture3D = static_cast<ID3D11Texture3D1*>(texture);
    return hr;
}


HRESULT STDMETHODCALLTYPE HackerDevice::CreateRasterizerState2(const D3D11_RASTERIZER_DESC2 *pRasterizerDesc, ID3D11RasterizerState2 **ppRasterizerState)
{
    return mExtendedDevice3->CreateRasterizerState2(pRasterizerDesc, ppRasterizerState);
}


HRESULT STDMETHODCALLTYPE HackerDevice::CreateShaderResourceView1(ID3D11Resource *pResource, const D3D11_SHADER_RESOURCE_VIEW_DESC1 *pDesc1, ID3D11ShaderResourceView1 **ppSRView1)
{
    return mExtendedDevice3->CreateShaderResourceView1(pResource, pDesc1, ppSRView1);
}


HRESULT STDMETHODCALLTYPE HackerDevice::CreateUnorderedAccessView1(ID3D11Resource *pResource, const D3D11_UNORDERED_ACCESS_VIEW_DESC1 *pDesc1, ID3D11UnorderedAccessView1 **ppUAView1)
{
    return mExtendedDevice3->CreateUnorderedAccessView1(pResource, pDesc1, ppUAView1);
}


HRESULT STDMETHODCALLTYPE HackerDevice::CreateRenderTargetView1(ID3D11Resource *pResource, const D3D11_RENDER_TARGET_VIEW_DESC1 *pDesc1, ID3D11RenderTargetView1 **ppRTView1)
{
    return mExtendedDevice3->CreateRenderTargetView1(pResource, pDesc1, ppRTView1);
}


HRESULT STDMETHODCALLTYPE HackerDevice::CreateQuery1(const D3D11_QUERY_DESC1 *pQueryDesc1, ID3D11Query1 **ppQuery1)
{
    return mExtendedDevice3->CreateQuery1(pQueryDesc1, ppQuery1);
}


void STDMETHODCALLTYPE HackerDevice::GetImmediateContext3(ID3D11DeviceContext3 **ppImmediateContext)
{
    if (!ppImmediateContext) return;
    *ppImmediateContext = nullptr;
    ID3D11DeviceContext* context = nullptr;
    GetImmediateContext(&context);
    if (context) { context->QueryInterface(IID_PPV_ARGS(ppImmediateContext)); context->Release(); }
}


HRESULT STDMETHODCALLTYPE HackerDevice::CreateDeferredContext3(UINT ContextFlags, ID3D11DeviceContext3 **ppDeferredContext)
{
    if (!ppDeferredContext) return E_INVALIDARG;
    *ppDeferredContext = nullptr;
    ID3D11DeviceContext3* context = nullptr;
    HRESULT hr = mExtendedDevice3->CreateDeferredContext3(ContextFlags, &context);
    if (FAILED(hr)) return hr;
    auto* wrapped = HackerContextFactory(mRealOrigDevice1, context);
    wrapped->SetHackerDevice(this);
    wrapped->Bind3DMigotoResources();
    *ppDeferredContext = wrapped;
    return hr;
}


void STDMETHODCALLTYPE HackerDevice::WriteToSubresource(ID3D11Resource *pDstResource, UINT DstSubresource, const D3D11_BOX *pDstBox, const void *pSrcData, UINT SrcRowPitch, UINT SrcDepthPitch)
{
    return mExtendedDevice3->WriteToSubresource(pDstResource, DstSubresource, pDstBox, pSrcData, SrcRowPitch, SrcDepthPitch);
}


void STDMETHODCALLTYPE HackerDevice::ReadFromSubresource(void *pDstData, UINT DstRowPitch, UINT DstDepthPitch, ID3D11Resource *pSrcResource, UINT SrcSubresource, const D3D11_BOX *pSrcBox)
{
    return mExtendedDevice3->ReadFromSubresource(pDstData, DstRowPitch, DstDepthPitch, pSrcResource, SrcSubresource, pSrcBox);
}


HRESULT STDMETHODCALLTYPE HackerDevice::RegisterDeviceRemovedEvent(HANDLE hEvent, DWORD *pdwCookie)
{
    return mExtendedDevice4->RegisterDeviceRemovedEvent(hEvent, pdwCookie);
}


void STDMETHODCALLTYPE HackerDevice::UnregisterDeviceRemoved(DWORD dwCookie)
{
    return mExtendedDevice4->UnregisterDeviceRemoved(dwCookie);
}


HRESULT STDMETHODCALLTYPE HackerDevice::OpenSharedFence(HANDLE hFence, REFIID ReturnedInterface, void **ppFence)
{
    return mExtendedDevice5->OpenSharedFence(hFence, ReturnedInterface, ppFence);
}


HRESULT STDMETHODCALLTYPE HackerDevice::CreateFence(UINT64 InitialValue, D3D11_FENCE_FLAG Flags, REFIID ReturnedInterface, void **ppFence)
{
    return mExtendedDevice5->CreateFence(InitialValue, Flags, ReturnedInterface, ppFence);
}


// 尾部 COM 方法仅转发新增能力，继承的 Draw / 资源绑定继续走 Runtime。

HRESULT STDMETHODCALLTYPE HackerContext::UpdateTileMappings(ID3D11Resource *pTiledResource, UINT NumTiledResourceRegions, const D3D11_TILED_RESOURCE_COORDINATE *pTiledResourceRegionStartCoordinates, const D3D11_TILE_REGION_SIZE *pTiledResourceRegionSizes, ID3D11Buffer *pTilePool, UINT NumRanges, const UINT *pRangeFlags, const UINT *pTilePoolStartOffsets, const UINT *pRangeTileCounts, UINT Flags)
{
    return mExtendedContext2->UpdateTileMappings(pTiledResource, NumTiledResourceRegions, pTiledResourceRegionStartCoordinates, pTiledResourceRegionSizes, pTilePool, NumRanges, pRangeFlags, pTilePoolStartOffsets, pRangeTileCounts, Flags);
}


HRESULT STDMETHODCALLTYPE HackerContext::CopyTileMappings(ID3D11Resource *pDestTiledResource, const D3D11_TILED_RESOURCE_COORDINATE *pDestRegionStartCoordinate, ID3D11Resource *pSourceTiledResource, const D3D11_TILED_RESOURCE_COORDINATE *pSourceRegionStartCoordinate, const D3D11_TILE_REGION_SIZE *pTileRegionSize, UINT Flags)
{
    return mExtendedContext2->CopyTileMappings(pDestTiledResource, pDestRegionStartCoordinate, pSourceTiledResource, pSourceRegionStartCoordinate, pTileRegionSize, Flags);
}


void STDMETHODCALLTYPE HackerContext::CopyTiles(ID3D11Resource *pTiledResource, const D3D11_TILED_RESOURCE_COORDINATE *pTileRegionStartCoordinate, const D3D11_TILE_REGION_SIZE *pTileRegionSize, ID3D11Buffer *pBuffer, UINT64 BufferStartOffsetInBytes, UINT Flags)
{
    return mExtendedContext2->CopyTiles(pTiledResource, pTileRegionStartCoordinate, pTileRegionSize, pBuffer, BufferStartOffsetInBytes, Flags);
}


void STDMETHODCALLTYPE HackerContext::UpdateTiles(ID3D11Resource *pDestTiledResource, const D3D11_TILED_RESOURCE_COORDINATE *pDestTileRegionStartCoordinate, const D3D11_TILE_REGION_SIZE *pDestTileRegionSize, const void *pSourceTileData, UINT Flags)
{
    return mExtendedContext2->UpdateTiles(pDestTiledResource, pDestTileRegionStartCoordinate, pDestTileRegionSize, pSourceTileData, Flags);
}


HRESULT STDMETHODCALLTYPE HackerContext::ResizeTilePool(ID3D11Buffer *pTilePool, UINT64 NewSizeInBytes)
{
    return mExtendedContext2->ResizeTilePool(pTilePool, NewSizeInBytes);
}


void STDMETHODCALLTYPE HackerContext::TiledResourceBarrier(ID3D11DeviceChild *pTiledResourceOrViewAccessBeforeBarrier, ID3D11DeviceChild *pTiledResourceOrViewAccessAfterBarrier)
{
    return mExtendedContext2->TiledResourceBarrier(pTiledResourceOrViewAccessBeforeBarrier, pTiledResourceOrViewAccessAfterBarrier);
}


BOOL STDMETHODCALLTYPE HackerContext::IsAnnotationEnabled(void)
{
    return mExtendedContext2->IsAnnotationEnabled();
}


void STDMETHODCALLTYPE HackerContext::SetMarkerInt(LPCWSTR pLabel, INT Data)
{
    return mExtendedContext2->SetMarkerInt(pLabel, Data);
}


void STDMETHODCALLTYPE HackerContext::BeginEventInt(LPCWSTR pLabel, INT Data)
{
    return mExtendedContext2->BeginEventInt(pLabel, Data);
}


void STDMETHODCALLTYPE HackerContext::EndEvent(void)
{
    return mExtendedContext2->EndEvent();
}


void STDMETHODCALLTYPE HackerContext::Flush1(D3D11_CONTEXT_TYPE ContextType, HANDLE hEvent)
{
    return mExtendedContext3->Flush1(ContextType, hEvent);
}


void STDMETHODCALLTYPE HackerContext::SetHardwareProtectionState(BOOL HwProtectionEnable)
{
    return mExtendedContext3->SetHardwareProtectionState(HwProtectionEnable);
}


void STDMETHODCALLTYPE HackerContext::GetHardwareProtectionState(BOOL *pHwProtectionEnable)
{
    return mExtendedContext3->GetHardwareProtectionState(pHwProtectionEnable);
}


HRESULT STDMETHODCALLTYPE HackerContext::Signal(ID3D11Fence *pFence, UINT64 Value)
{
    return mExtendedContext4->Signal(pFence, Value);
}


HRESULT STDMETHODCALLTYPE HackerContext::Wait(ID3D11Fence *pFence, UINT64 Value)
{
    return mExtendedContext4->Wait(pFence, Value);
}


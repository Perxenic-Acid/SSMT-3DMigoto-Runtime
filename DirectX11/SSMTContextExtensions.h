// 新版接口仍使用同一 Runtime 包装；不能因接口升级把游戏绘制交还底层对象。
HRESULT STDMETHODCALLTYPE UpdateTileMappings(ID3D11Resource *pTiledResource, UINT NumTiledResourceRegions, const D3D11_TILED_RESOURCE_COORDINATE *pTiledResourceRegionStartCoordinates, const D3D11_TILE_REGION_SIZE *pTiledResourceRegionSizes, ID3D11Buffer *pTilePool, UINT NumRanges, const UINT *pRangeFlags, const UINT *pTilePoolStartOffsets, const UINT *pRangeTileCounts, UINT Flags) override;
HRESULT STDMETHODCALLTYPE CopyTileMappings(ID3D11Resource *pDestTiledResource, const D3D11_TILED_RESOURCE_COORDINATE *pDestRegionStartCoordinate, ID3D11Resource *pSourceTiledResource, const D3D11_TILED_RESOURCE_COORDINATE *pSourceRegionStartCoordinate, const D3D11_TILE_REGION_SIZE *pTileRegionSize, UINT Flags) override;
void STDMETHODCALLTYPE CopyTiles(ID3D11Resource *pTiledResource, const D3D11_TILED_RESOURCE_COORDINATE *pTileRegionStartCoordinate, const D3D11_TILE_REGION_SIZE *pTileRegionSize, ID3D11Buffer *pBuffer, UINT64 BufferStartOffsetInBytes, UINT Flags) override;
void STDMETHODCALLTYPE UpdateTiles(ID3D11Resource *pDestTiledResource, const D3D11_TILED_RESOURCE_COORDINATE *pDestTileRegionStartCoordinate, const D3D11_TILE_REGION_SIZE *pDestTileRegionSize, const void *pSourceTileData, UINT Flags) override;
HRESULT STDMETHODCALLTYPE ResizeTilePool(ID3D11Buffer *pTilePool, UINT64 NewSizeInBytes) override;
void STDMETHODCALLTYPE TiledResourceBarrier(ID3D11DeviceChild *pTiledResourceOrViewAccessBeforeBarrier, ID3D11DeviceChild *pTiledResourceOrViewAccessAfterBarrier) override;
BOOL STDMETHODCALLTYPE IsAnnotationEnabled(void) override;
void STDMETHODCALLTYPE SetMarkerInt(LPCWSTR pLabel, INT Data) override;
void STDMETHODCALLTYPE BeginEventInt(LPCWSTR pLabel, INT Data) override;
void STDMETHODCALLTYPE EndEvent(void) override;
void STDMETHODCALLTYPE Flush1(D3D11_CONTEXT_TYPE ContextType, HANDLE hEvent) override;
void STDMETHODCALLTYPE SetHardwareProtectionState(BOOL HwProtectionEnable) override;
void STDMETHODCALLTYPE GetHardwareProtectionState(BOOL *pHwProtectionEnable) override;
HRESULT STDMETHODCALLTYPE Signal(ID3D11Fence *pFence, UINT64 Value) override;
HRESULT STDMETHODCALLTYPE Wait(ID3D11Fence *pFence, UINT64 Value) override;

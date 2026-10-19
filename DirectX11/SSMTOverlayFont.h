#pragma once

#include <memory>
#include <string>
#include "SpriteFont.h"

namespace SSMTOverlayFont {
// 在冷路径从系统字体生成图集，返回对象拥有 GPU 纹理；不接管游戏的渲染状态。
std::unique_ptr<DirectX::SpriteFont> Create(ID3D11Device* device, const std::wstring& characters,
    const wchar_t* family = L"Microsoft YaHei");
}

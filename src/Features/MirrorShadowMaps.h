#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <DirectXMath.h>
#include <d3d11.h>
#include <wrl/client.h>
#include "MirrorPrivateSun.h"
#include "../../features/Mirrors of Fallout/Shaders/MirrorsOfFallout/MirrorLightingABI.hlsli"

namespace MirrorShadowMaps
{
    inline constexpr unsigned kLights = MIRROR_MAX_LIGHTS, kMaps = 36, kResources = 20, kABI = MIRROR_SHADOW_ABI;

    struct Map
    {
        DirectX::XMFLOAT4X4 worldToShadow{};
        DirectX::XMFLOAT4 bounds{}; 
        DirectX::XMFLOAT4 sampling{}; 
        DirectX::XMUINT4 resource{}; 

        DirectX::XMFLOAT4 sourceEyeFar{}; 
        DirectX::XMFLOAT4 sourceForwardMin{}; 
        DirectX::XMFLOAT4 sourceRange{}; 
        DirectX::XMUINT4 casterHull{}; 
        std::array<DirectX::XMFLOAT4,6> casterPlanes{}; 
    };
    static_assert(sizeof(Map) == 272);
    struct SunView
    {

        DirectX::XMFLOAT4 eyeFar{};
        DirectX::XMFLOAT4 forwardBlend{}; 
        DirectX::XMFLOAT4 splits{};
        DirectX::XMUINT4 metadata{}; 
    };
    static_assert(sizeof(SunView) == 64);
    struct alignas(16) Constants
    {
        DirectX::XMUINT4 contract{kABI, 0, 0, 0}; 
        std::array<DirectX::XMUINT4, kLights> lights{}; 
        std::array<Map, kMaps> maps{};
        std::array<SunView, kLights> sunViews{};
    };
    static_assert(sizeof(Constants) == 30288);
    struct Bindings
    {
        MirrorPrivateSun::Bindings privateSun{};
        ID3D11Buffer* constants{};
        ID3D11SamplerState* sampler{};
        std::array<ID3D11ShaderResourceView*, kResources> resources{};
    };

    struct Lease
    {
        Microsoft::WRL::ComPtr<ID3D11Buffer> constants;
        Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler;
        std::array<Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>, kResources> resources{};
        Bindings bindings{};

        void Pin(const Bindings& source)
        {
            constants=source.constants;sampler=source.sampler;
            for (unsigned i=0;i<resources.size();++i) resources[i]=source.resources[i];
            bindings=source;
        }
    };

    inline bool Finite(const DirectX::XMFLOAT4X4& matrix) noexcept
    {
        for (const auto& row : matrix.m)
            for (float value : row) if (!std::isfinite(value)) return false;
        return std::fabs(matrix._11) + std::fabs(matrix._12) + std::fabs(matrix._13) > 1.e-12f &&
            std::fabs(matrix._21) + std::fabs(matrix._22) + std::fabs(matrix._23) > 1.e-12f;
    }

    inline bool DepthDescription(const D3D11_TEXTURE2D_DESC& texture,
        const D3D11_SHADER_RESOURCE_VIEW_DESC& view) noexcept
    {
        const bool depth = view.Format == DXGI_FORMAT_R32_FLOAT ||
            view.Format == DXGI_FORMAT_R16_UNORM || view.Format == DXGI_FORMAT_R24_UNORM_X8_TYPELESS ||
            view.Format == DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
        return depth && texture.Width && texture.Height && texture.Width <= 8192 && texture.Height <= 8192 &&
            texture.MipLevels == 1 && texture.ArraySize && texture.ArraySize <= 4 && texture.SampleDesc.Count == 1 &&
            (texture.BindFlags & D3D11_BIND_SHADER_RESOURCE) &&
            ((view.ViewDimension == D3D11_SRV_DIMENSION_TEXTURE2D && texture.ArraySize == 1 &&
                view.Texture2D.MostDetailedMip == 0) ||
             (view.ViewDimension == D3D11_SRV_DIMENSION_TEXTURE2DARRAY &&
                view.Texture2DArray.MostDetailedMip == 0 && view.Texture2DArray.FirstArraySlice == 0 &&
                view.Texture2DArray.ArraySize == texture.ArraySize));
    }

    inline std::uint64_t Bytes(const D3D11_TEXTURE2D_DESC& texture) noexcept
    {
        const unsigned bytes = texture.Format == DXGI_FORMAT_R16_TYPELESS ||
            texture.Format == DXGI_FORMAT_R16_UNORM ? 2 :
            texture.Format == DXGI_FORMAT_R32G8X24_TYPELESS ? 8 : 4;
        return std::uint64_t(texture.Width) * texture.Height * texture.ArraySize * bytes;
    }
}

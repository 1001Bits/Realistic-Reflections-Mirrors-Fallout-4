#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <vector>
#include <d3d11.h>
#include <wrl/client.h>

namespace MirrorLightingReadback
{
    inline constexpr UINT Grid = 5, Pixels = Grid * Grid;
    using Color = std::array<std::uint16_t, 4>;
    struct Sample
    {
        UINT width{}, height{};
        
        std::array<std::array<Color, Pixels>, 5> colors{};
        std::array<std::uint32_t, Pixels> metadata{};
        std::vector<std::byte> constants;
    };
    inline UINT Coordinate(UINT index, UINT extent) noexcept
    { return (index + 1) * extent / (Grid + 1); }

    class Readback
    {
    public:
        bool Pending() const noexcept { return begun_; }
        bool Finished() const noexcept { return finished_; }
        void Reset() noexcept { *this = {}; }
        bool Begin(ID3D11DeviceContext* context, const std::array<ID3D11Texture2D*, 4>& colors,
            ID3D11Texture2D* metadata, ID3D11Buffer* constants)
        {
            if (!context || begun_ || !constants || !metadata ||
                context->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE) return false;
            context->GetDevice(device_.ReleaseAndGetAddressOf());
            D3D11_BUFFER_DESC cb{}; constants->GetDesc(&cb);
            if (!cb.ByteWidth || cb.ByteWidth > 65536) return false;
            cb.Usage = D3D11_USAGE_STAGING; cb.BindFlags = cb.MiscFlags = cb.StructureByteStride = 0;
            cb.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            if (FAILED(device_->CreateBuffer(&cb, nullptr, constants_.ReleaseAndGetAddressOf()))) return false;
            sample_ = {}; sample_.constants.resize(cb.ByteWidth);
            for (UINT i = 0; i < colors.size(); ++i)
                if (!CopyPixels(context, colors[i], i, DXGI_FORMAT_R16G16B16A16_FLOAT)) { Reset(); return false; }
            if (!CopyPixels(context, metadata, 5, DXGI_FORMAT_R32_UINT)) { Reset(); return false; }
            const D3D11_QUERY_DESC query{D3D11_QUERY_EVENT, 0};
            if (FAILED(device_->CreateQuery(&query, done_.ReleaseAndGetAddressOf()))) { Reset(); return false; }
            context->CopyResource(constants_.Get(), constants);
            begun_ = true; finished_ = false;
            return true;
        }
        bool Finish(ID3D11DeviceContext* context, ID3D11Texture2D* finalColor)
        {
            if (!begun_ || finished_ || !SameDevice(context)) return false;
            if (!CopyPixels(context, finalColor, 4, DXGI_FORMAT_R16G16B16A16_FLOAT)) { Reset(); return false; }
            context->End(done_.Get()); finished_ = true;
            return true;
        }
        bool Poll(ID3D11DeviceContext* context, Sample& result)
        {
            if (!finished_ || !SameDevice(context)) return false;
            BOOL ready = FALSE;
            const auto status = context->GetData(done_.Get(), &ready, sizeof(ready), D3D11_ASYNC_GETDATA_DONOTFLUSH);
            if (FAILED(status)) { Reset(); return false; }
            if (status != S_OK || !ready) return false;
            D3D11_MAPPED_SUBRESOURCE map{};
            for (UINT i = 0; i < textures_.size(); ++i) {
                const auto hr = context->Map(textures_[i].Get(), 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &map);
                if (hr == DXGI_ERROR_WAS_STILL_DRAWING) return false;
                if (FAILED(hr)) { Reset(); return false; }
                const UINT pixelBytes = i == 5 ? 4 : 8;
                auto* destination = i == 5 ? static_cast<void*>(sample_.metadata.data()) :
                    static_cast<void*>(sample_.colors[i].data());
                for (UINT row = 0; row < Grid; ++row)
                    std::memcpy(static_cast<std::byte*>(destination) + row * Grid * pixelBytes,
                        static_cast<const std::byte*>(map.pData) + row * map.RowPitch, Grid * pixelBytes);
                context->Unmap(textures_[i].Get(), 0);
            }
            const auto hr = context->Map(constants_.Get(), 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &map);
            if (hr == DXGI_ERROR_WAS_STILL_DRAWING) return false;
            if (FAILED(hr)) { Reset(); return false; }
            std::memcpy(sample_.constants.data(), map.pData, sample_.constants.size());
            context->Unmap(constants_.Get(), 0);
            result = std::move(sample_); Reset(); return true;
        }
    private:
        bool SameDevice(ID3D11DeviceContext* context) const noexcept
        {
            if (!context || context->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE) return false;
            Microsoft::WRL::ComPtr<ID3D11Device> device; context->GetDevice(&device);
            return device.Get() == device_.Get();
        }
        bool CopyPixels(ID3D11DeviceContext* context, ID3D11Texture2D* source, UINT slot, DXGI_FORMAT format)
        {
            if (!source) return false;
            Microsoft::WRL::ComPtr<ID3D11Device> device; source->GetDevice(&device);
            if (device.Get() != device_.Get()) return false;
            D3D11_TEXTURE2D_DESC desc{}; source->GetDesc(&desc);
            if (desc.Format != format || desc.SampleDesc.Count != 1 || desc.ArraySize != 1 ||
                desc.Width < Grid + 1 || desc.Height < Grid + 1) return false;
            if (!sample_.width) { sample_.width = desc.Width; sample_.height = desc.Height; }
            if (sample_.width != desc.Width || sample_.height != desc.Height) return false;
            desc.Width = desc.Height = Grid; desc.MipLevels = desc.ArraySize = 1;
            desc.BindFlags = desc.MiscFlags = 0; desc.Usage = D3D11_USAGE_STAGING;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            if (FAILED(device_->CreateTexture2D(&desc, nullptr, textures_[slot].ReleaseAndGetAddressOf()))) return false;
            for (UINT y = 0; y < Grid; ++y) for (UINT x = 0; x < Grid; ++x) {
                const UINT sx = Coordinate(x, sample_.width), sy = Coordinate(y, sample_.height);
                const D3D11_BOX pixel{sx, sy, 0, sx + 1, sy + 1, 1};
                context->CopySubresourceRegion(textures_[slot].Get(), 0, x, y, 0, source, 0, &pixel);
            }
            return true;
        }
        Microsoft::WRL::ComPtr<ID3D11Device> device_;
        std::array<Microsoft::WRL::ComPtr<ID3D11Texture2D>, 6> textures_;
        Microsoft::WRL::ComPtr<ID3D11Buffer> constants_;
        Microsoft::WRL::ComPtr<ID3D11Query> done_;
        Sample sample_;
        bool begun_{}, finished_{};
    };
}

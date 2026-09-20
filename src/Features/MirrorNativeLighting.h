#pragma once
#include <array>
#include <cstdint>
#include <d3d11_1.h>
#include <wrl/client.h>

namespace MirrorNativeLighting
{
    inline bool WorldAmbient(std::uint32_t descriptor) noexcept
    { return (descriptor & 0x20000u) && !(descriptor & (0x6000000u | 0x140u)); }

    struct Bindings
    {
        std::array<ID3D11Buffer*,2> buffers{};
        std::array<UINT,2> first{}, count{};
        bool directional{};
        explicit operator bool() const noexcept { return buffers[0] && buffers[1]; }
        void Bind(ID3D11DeviceContext* context, bool pixel) const noexcept
        {
            Microsoft::WRL::ComPtr<ID3D11DeviceContext1> ranged;
            if (SUCCEEDED(context->QueryInterface(IID_PPV_ARGS(&ranged)))) {
                if (pixel) ranged->PSSetConstantBuffers1(3,2,buffers.data(),first.data(),count.data());
                else ranged->CSSetConstantBuffers1(3,2,buffers.data(),first.data(),count.data());
            } else {
                if (pixel) context->PSSetConstantBuffers(3,2,buffers.data());
                else context->CSSetConstantBuffers(3,2,buffers.data());
            }
        }
    };

    class Snapshot
    {
    public:
        void Reset() noexcept { *this={}; }
        Bindings Get(std::uint64_t frame, std::uint64_t generation) const noexcept
        { return ready_ && frame==frame_ && generation==generation_ ? binding_ : Bindings{}; }
        bool Capture(ID3D11DeviceContext* context, std::uint32_t descriptor,
            std::uint64_t frame, std::uint64_t generation)
        {
            if (!context || !WorldAmbient(descriptor)) return false;
            Microsoft::WRL::ComPtr<ID3D11Device> device;
            context->GetDevice(&device);
            if (device!=device_) { Reset(); device_=device; }

            const bool directional=(descriptor & 0x200u)!=0;
            if (Get(frame,generation) && (binding_.directional || !directional)) return true;
            Microsoft::WRL::ComPtr<ID3D11DeviceContext1> ranged;
            (void)context->QueryInterface(IID_PPV_ARGS(&ranged));
            std::array<Microsoft::WRL::ComPtr<ID3D11Buffer>,2> source;
            auto nextCopies=copies_;
            auto nextBytes=bytes_;
            constexpr UINT slots[]{2,12}, required[]{9,30};
            Bindings next;
            next.directional=directional;
            for (unsigned i=0;i<2;++i) {
                if (ranged) ranged->PSGetConstantBuffers1(slots[i],1,&source[i],&next.first[i],&next.count[i]);
                else context->PSGetConstantBuffers(slots[i],1,&source[i]);
                if (!source[i]) return false;
                D3D11_BUFFER_DESC desc{};source[i]->GetDesc(&desc);
                if (!ranged) next.count[i]=desc.ByteWidth/16;
                if (next.count[i]<required[i] || desc.ByteWidth>1048576 ||
                    (std::uint64_t(next.first[i])+required[i])*16>desc.ByteWidth) return false;
                if (!nextCopies[i] || nextBytes[i]!=desc.ByteWidth) {
                    nextCopies[i].Reset();nextBytes[i]=desc.ByteWidth;
                    desc.Usage=D3D11_USAGE_DEFAULT;desc.CPUAccessFlags=desc.MiscFlags=desc.StructureByteStride=0;
                    desc.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
                    if (FAILED(device->CreateBuffer(&desc,nullptr,&nextCopies[i]))) return false;
                }
                next.buffers[i]=nextCopies[i].Get();
            }

            for (unsigned i=0;i<2;++i) context->CopyResource(nextCopies[i].Get(),source[i].Get());
            copies_=std::move(nextCopies);bytes_=nextBytes;
            binding_=next;frame_=frame;generation_=generation;ready_=true;
            return true;
        }
    private:
        Microsoft::WRL::ComPtr<ID3D11Device> device_;
        std::array<Microsoft::WRL::ComPtr<ID3D11Buffer>,2> copies_;
        std::array<UINT,2> bytes_{};
        Bindings binding_;
        std::uint64_t frame_{},generation_{};
        bool ready_{};
    };
}

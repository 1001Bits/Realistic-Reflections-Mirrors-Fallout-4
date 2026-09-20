#pragma once
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <d3d11.h>
#include <wrl/client.h>

namespace MirrorVisibility
{
    
    inline bool ValidDistance(float squared) noexcept
    { return (std::bit_cast<std::uint32_t>(squared) & 0x7f800000u) != 0x7f800000u && squared >= 0.f; }

    class Query
    {
    public:
        void Reset() noexcept { *this = {}; }
        void Invalidate() noexcept { known_ = false; zeros_ = 0; ++epoch_; }
        bool Occluded(std::uint64_t frame) const noexcept
        {
            return known_ && zeros_ >= 2 && frame >= resultFrame_ &&
                frame - resultFrame_ <= 2;
        }
        void Poll(ID3D11DeviceContext* context) noexcept
        {
            if (!context || context->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE) return;
            struct Result { std::uint64_t frame{}, samples{}; };
            std::array<Result,3> ready{};
            unsigned count=0;
            bool failed=false;
            for (auto& slot : slots_) {
                if (!slot.pending) continue;
                std::uint64_t samples{};
                const auto hr=context->GetData(slot.query.Get(), &samples, sizeof(samples),
                    D3D11_ASYNC_GETDATA_DONOTFLUSH);
                if (hr==S_FALSE) continue;
                slot.pending=false;
                if (hr==S_OK && slot.epoch==epoch_) ready[count++]={slot.frame,samples};
                else if (FAILED(hr)) failed=true;
            }
            std::sort(ready.begin(),ready.begin()+count,[](const Result& a,const Result& b) {
                return a.frame<b.frame;
            });
            for (unsigned i=0;i<count;++i) {
                const auto& r=ready[i];
                if (known_ && r.frame<=resultFrame_) continue;
                known_=true; resultFrame_=r.frame;
                zeros_=r.samples ? 0 : (std::min)(zeros_+1,2u);
            }
            if (failed) Invalidate();
        }
        bool Begin(ID3D11Device* device, ID3D11DeviceContext* context, std::uint64_t frame) noexcept
        {
            if (!device || !context || active_<slots_.size()) return false;
            if (device_!=device) { Reset(); device_=device; }
            if ((issued_ && frame==issuedFrame_) || frame<retryAfter_) return false;
            for (unsigned i=0;i<slots_.size();++i) {
                auto& slot=slots_[i];
                if (slot.pending) continue;
                if (!slot.query) {
                    const D3D11_QUERY_DESC desc{D3D11_QUERY_OCCLUSION,0};
                    if (FAILED(device->CreateQuery(&desc,slot.query.GetAddressOf()))) {
                        Invalidate(); retryAfter_=frame+60; return false;
                    }
                }
                slot.frame=frame; slot.epoch=epoch_; active_=i; issued_=true; issuedFrame_=frame;
                context->Begin(slot.query.Get());
                return true;
            }
            return false;
        }
        void End(ID3D11DeviceContext* context) noexcept
        {
            if (active_>=slots_.size()) return;
            auto& slot=slots_[active_];
            context->End(slot.query.Get());
            slot.pending=true; active_=3;
        }
    private:
        struct Slot {
            Microsoft::WRL::ComPtr<ID3D11Query> query;
            std::uint64_t frame{},epoch{};
            bool pending{};
        };
        std::array<Slot,3> slots_{};
        ID3D11Device* device_{};
        std::uint64_t resultFrame_{},issuedFrame_{},retryAfter_{},epoch_{};
        unsigned active_{3},zeros_{};
        bool known_{},issued_{};
    };
}

#pragma once
#include "../Utils/D3DDeviceIdentity.h"
#include <array>
#include <algorithm>
#include <cstdint>

class MirrorPresentationPool
{
    using Texture=Microsoft::WRL::ComPtr<ID3D11Texture2D>;
    using View=Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>;
    struct Entry {
        Texture texture; View view;
        Microsoft::WRL::ComPtr<ID3D11Query> fence;
        D3D11_TEXTURE2D_DESC desc{};
        std::uint64_t touched{},bytes{};
    };
    std::array<Entry,4> entries_{};
public:
    static constexpr std::uint64_t Budget=64ull*1024*1024, KeepMs=15000;
    struct ShapeStats {
        unsigned width{},height{};
        std::uint64_t allocations{},hits{},failures{};
        double allocationMs{},worstMs{};
    };
    std::array<ShapeStats,32> shapes{};
    std::uint64_t evictions{};
    static bool Same(const D3D11_TEXTURE2D_DESC& a,const D3D11_TEXTURE2D_DESC& b) noexcept
    {
        return a.Width==b.Width && a.Height==b.Height && a.Format==b.Format && a.MipLevels==b.MipLevels &&
            a.ArraySize==b.ArraySize && a.SampleDesc.Count==b.SampleDesc.Count && a.SampleDesc.Quality==b.SampleDesc.Quality &&
            a.Usage==b.Usage && a.BindFlags==b.BindFlags && a.CPUAccessFlags==b.CPUAccessFlags && a.MiscFlags==b.MiscFlags;
    }
    static std::uint64_t Bytes(const D3D11_TEXTURE2D_DESC& desc) noexcept
    {
        const unsigned bpp=desc.Format==DXGI_FORMAT_R16G16B16A16_FLOAT ? 8u :
            (desc.Format==DXGI_FORMAT_R8G8B8A8_UNORM ? 4u : 0u);
        if(!bpp || !desc.MipLevels || desc.ArraySize!=1 || desc.SampleDesc.Count!=1) return 0;
        std::uint64_t bytes{};
        auto w=desc.Width,h=desc.Height;
        for(unsigned level=0;level<desc.MipLevels;++level) {
            bytes+=std::uint64_t(w)*h*bpp;
            w=(std::max)(1u,w/2);h=(std::max)(1u,h/2);
        }
        return bytes;
    }
    std::uint64_t RetainedBytes() const noexcept
    { std::uint64_t bytes{};for(const auto& entry:entries_) bytes+=entry.bytes;return bytes; }
    ShapeStats* Stats(unsigned w,unsigned h) noexcept
    {
        for(auto& s:shapes) if(s.width==w && s.height==h) return &s;
        for(auto& s:shapes) if(!s.width) {s.width=w;s.height=h;return &s;}
        return nullptr;
    }
    void NoteAllocation(const D3D11_TEXTURE2D_DESC& desc,double ms,bool success) noexcept
    {
        if(auto* s=Stats(desc.Width,desc.Height)) {
            ++s->allocations;s->failures+=!success;s->allocationMs+=ms;s->worstMs=(std::max)(s->worstMs,ms);
        }
    }
    void Maintain(ID3D11Device* device,std::uint64_t now) noexcept
    {
        for(auto& entry:entries_) if(entry.texture &&
            (!Util::D3DChildUsesDevice(entry.texture.Get(),device) || now<entry.touched || now-entry.touched>KeepMs)) {
            entry={};++evictions;
        }
    }
    bool Retire(ID3D11Device* device,ID3D11DeviceContext* context,Texture& texture,View& view,
        std::uint64_t now,bool stillOwned) noexcept
    {
        if(stillOwned || !texture || !view || !context || context->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE ||
            !Util::D3DChildUsesDevice(context,device) || !Util::D3DChildUsesDevice(texture.Get(),device) ||
            !Util::D3DChildUsesDevice(view.Get(),device)) return false;
        Microsoft::WRL::ComPtr<ID3D11Resource> resource;
        view->GetResource(&resource);
        if(resource.Get()!=texture.Get()) return false;
        D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);
        D3D11_SHADER_RESOURCE_VIEW_DESC viewDesc{};view->GetDesc(&viewDesc);
        if(viewDesc.Format!=desc.Format || viewDesc.ViewDimension!=D3D11_SRV_DIMENSION_TEXTURE2D ||
            viewDesc.Texture2D.MostDetailedMip!=0 || viewDesc.Texture2D.MipLevels!=desc.MipLevels) return false;
        const auto bytes=Bytes(desc);
        if(!bytes || bytes>Budget) return false;
        Maintain(device,now);
        Entry* slot{};
        for(;;) {
            Entry* oldest{};
            for(auto& e:entries_) {
                if(!e.texture) slot=&e;
                else if(!oldest || e.touched<oldest->touched) oldest=&e;
            }
            if(slot && RetainedBytes()+bytes<=Budget) break;
            if(!oldest) return false;
            *oldest={};++evictions;
        }
        D3D11_QUERY_DESC query{D3D11_QUERY_EVENT,0};
        if(FAILED(device->CreateQuery(&query,&slot->fence))) return false;
        context->End(slot->fence.Get());
        slot->texture.Swap(texture);slot->view.Swap(view);
        slot->desc=desc;slot->bytes=bytes;slot->touched=now;
        return true;
    }
    bool Acquire(ID3D11Device* device,ID3D11DeviceContext* context,const D3D11_TEXTURE2D_DESC& desc,
        Texture& texture,View& view,std::uint64_t now) noexcept
    {
        if(!context || context->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE || texture || view || !Util::D3DChildUsesDevice(context,device)) return false;
        Maintain(device,now);
        for(auto& entry:entries_) {
            if(!entry.texture || !Same(entry.desc,desc) || !entry.fence ||
                context->GetData(entry.fence.Get(),nullptr,0,D3D11_ASYNC_GETDATA_DONOTFLUSH)!=S_OK) continue;
            texture.Swap(entry.texture);view.Swap(entry.view);entry={};
            if(auto* stats=Stats(desc.Width,desc.Height)) ++stats->hits;
            return true;
        }
        return false;
    }
    void Clear() noexcept {entries_={};}
};

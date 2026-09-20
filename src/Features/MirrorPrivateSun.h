#pragma once

#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>
#include <DirectXMath.h>
#include <d3d11_1.h>
#include <wrl/client.h>
#include "MirrorShadowCasterVolume.h"
#include "MirrorShadowCasterBatch.h"
#include "MirrorSceneRange.h"

namespace MirrorPrivateSun
{
    using namespace DirectX;
    inline constexpr unsigned kABI = 4;

    inline constexpr unsigned Resolution = 2048, ConstantSlot = 12, TextureSlot = 27, SamplerSlot = 15;
    inline constexpr float WideExtent = 9216, DetailExtent = 256, Near = 1, Far = 36864;

    inline bool KeepCasterSize(bool smallObjects, bool skinned, float modelRadius, float worldScale) noexcept
    {
        if (smallObjects || skinned || !std::isfinite(modelRadius) || modelRadius <= 0 ||
            !std::isfinite(worldScale) || std::fabs(worldScale) < 1.e-5f) return true;
        return modelRadius * std::fabs(worldScale) >= 32.f;
    }
    struct View
    {
        XMFLOAT3 eye{}, right{}, up{}, forward{};
        XMFLOAT4X4 worldToTexture{}, relativeViewProjection{};
        float extent{}, texel{}, reuseGuard{};
        unsigned resolution{Resolution};
    };

    inline std::array<XMFLOAT4,3> CameraRows(const View& view) noexcept
    {
        return {XMFLOAT4{view.forward.x,view.forward.y,view.forward.z,0},
            XMFLOAT4{view.up.x,view.up.y,view.up.z,0},
            XMFLOAT4{view.right.x,view.right.y,view.right.z,0}};
    }
    inline bool Finite(const XMFLOAT3& value) noexcept
    { return MirrorSceneRange::Finite(value.x) && MirrorSceneRange::Finite(value.y) && MirrorSceneRange::Finite(value.z); }

    struct Traversal
    {
        static constexpr unsigned MaxVisits=65536, MaxChildSlots=262144;
        struct Entry { const void* node{}; unsigned depth{}; };
        using Storage=std::vector<Entry>;
        Storage& pending;
        const void* excluded{};
        bool (*retain)(const void*) noexcept {};
        const void* currentNode{};
        const char* failure{"none"};
        unsigned visits{}, childSlots{}, deepest{}, casterBoundsRejected{}, smallCastersRejected{};
        bool smallObjects{true};
        MirrorSceneRange::Sphere range{};
        bool complete{true};
        const MirrorShadowCasterVolume* casterVolume{};
        const MirrorShadowCasterSet* casterSet{};
        Traversal(Storage& storage,const void* skip=nullptr,bool (*pin)(const void*) noexcept=nullptr,
            const MirrorShadowCasterVolume* volume=nullptr,const MirrorShadowCasterSet* set=nullptr) noexcept:
            pending(storage),excluded(skip),retain(pin),casterVolume(volume),casterSet(set)
        { pending.clear(); }

        bool AllowsCaster(const MirrorShadowCasterVolume::Point& center,float radius,bool geometry=true) const noexcept
        {
            if (range.Outside(center.x,center.y,center.z,radius)) return false;
            if (!geometry) return true;
            return casterSet ? casterSet->Intersects(center,radius) :
                (!casterVolume || casterVolume->Intersects(center,radius));
        }
        bool Fail(const char* reason) noexcept
        { if(complete) failure=reason;complete=false;return false; }
        bool Visit(const void* node, unsigned depth) noexcept
        {
            if(!node || node==excluded || !complete) return false;
            currentNode=node;
            if(reinterpret_cast<std::uintptr_t>(node)<=0x10000) return Fail("invalid-node");
            if(visits>=MaxVisits) return Fail("node-budget");
            ++visits;if(depth>deepest) deepest=depth;
            return true;
        }
        unsigned ChildSlots(unsigned size, unsigned capacity) noexcept
        {
            
            if(!complete) return 0;
            if(size>capacity || capacity>65535) {Fail("child-array");return 0;}
            if(capacity>MaxChildSlots-childSlots) {Fail("child-slot-budget");return 0;}
            childSlots+=capacity;
            return capacity;
        }
        bool Queue(const void* node,unsigned depth) noexcept
        {
            if(!complete) return false;
            if(!node || node==excluded) return true;
            if(reinterpret_cast<std::uintptr_t>(node)<=0x10000) return Fail("invalid-node");
            if(pending.size()>=MaxVisits-visits) return Fail("pending-node-budget");
            if(retain && !retain(node)) return Fail("node-retention");
            try { pending.push_back({node,depth}); }
            catch(...) { return Fail("work-storage"); }
            return true;
        }
        template<class Children> void QueueChildren(const Children& children,unsigned depth) noexcept
        {
            const auto slots=ChildSlots(children.size(),children.capacity());
            if(!slots) return;
            const auto* data=children.data();
            if(!data) {Fail("child-storage");return;}
            
            for(unsigned i=slots;i>0;--i)
                if(!Queue(data[i-1].get(),depth)) break;
        }
        Entry Next() noexcept
        {
            if(!complete || pending.empty()) return {};
            const auto result=pending.back();pending.pop_back();return result;
        }
    };

    inline bool CoversSphere(const XMFLOAT4X4& matrix, const XMFLOAT3& center, float radius, unsigned resolution = Resolution) noexcept
    {
        if(!Finite(center) || !std::isfinite(radius) || radius<=0) return false;
        const auto p=XMVector4Transform(XMVectorSet(center.x,center.y,center.z,1),XMLoadFloat4x4(&matrix));
        XMFLOAT4 point;XMStoreFloat4(&point,p);
        if(!std::isfinite(point.w) || std::fabs(point.w-1)>1.e-4f) return false;
        if (resolution < 512 || resolution > 4096) return false;
        const float margin=20.f/resolution;
        for(unsigned axis=0;axis<3;++axis) {
            const float extent=radius*std::sqrt(matrix.m[0][axis]*matrix.m[0][axis]+
                matrix.m[1][axis]*matrix.m[1][axis]+matrix.m[2][axis]*matrix.m[2][axis]);
            const float value=axis==0?point.x:(axis==1?point.y:point.z);
            if(!std::isfinite(extent) || !std::isfinite(value) ||
                value-extent<=margin || value+extent>=1-margin) return false;
        }
        return true;
    }

    inline bool Fit(const XMFLOAT3& focus, const XMFLOAT3& rays, float extent, View& out, float reuseGuard = 0.f, unsigned resolution = Resolution) noexcept
    {
        out = {};
        if (!Finite(focus) || !Finite(rays) || !std::isfinite(extent) || extent <= 0 ||
            (resolution != 512 && resolution != 1024 && resolution != 2048 && resolution != 4096)) return false;
        const auto direction = XMLoadFloat3(&rays);
        const float length = XMVectorGetX(XMVector3Length(direction));
        if (!std::isfinite(length) || length < 1.e-5f) return false;
        const auto f = XMVector3Normalize(direction);
        const auto seed = std::fabs(XMVectorGetZ(f)) > .9f ? XMVectorSet(0,1,0,0) : XMVectorSet(0,0,1,0);
        const auto r = XMVector3Normalize(XMVector3Cross(f, seed));
        const auto u = XMVector3Cross(r, f);
        const auto p = XMLoadFloat3(&focus);
        const float texel = 2 * extent / resolution;
        const float snap = texel * 16;
        const auto project = [&](FXMVECTOR axis) { return std::floor(XMVectorGetX(XMVector3Dot(p, axis))/snap + .5f)*snap; };
        const float x = project(r), y = project(u), z = project(f) - 3 * WideExtent;
        XMStoreFloat3(&out.eye, r*x + u*y + f*z);
        XMStoreFloat3(&out.right, r); XMStoreFloat3(&out.up, u); XMStoreFloat3(&out.forward, f);
        const auto view = XMMatrixSet(out.right.x,out.up.x,out.forward.x,0,
            out.right.y,out.up.y,out.forward.y,0, out.right.z,out.up.z,out.forward.z,0, 0,0,0,1);
        const auto projection = XMMatrixOrthographicLH(2*extent,2*extent,Near,Far);
        const auto texture = XMMatrixSet(.5f,0,0,0, 0,-.5f,0,0, 0,0,1,0, .5f,.5f,0,1);
        XMStoreFloat4x4(&out.relativeViewProjection, view * projection);
        auto absoluteView = view; absoluteView.r[3] = XMVectorSet(-x,-y,-z,1);
        XMStoreFloat4x4(&out.worldToTexture, absoluteView * projection * texture);
        out.extent=extent; out.texel=texel;
        out.resolution=resolution;
        out.reuseGuard=reuseGuard > 0 ? reuseGuard : (extent==WideExtent ? 1024.f : (extent==DetailExtent ? 32.f : 0.f));
        return Finite(out.eye);
    }

    inline bool Covers(const View& volume, const View& request) noexcept
    {
        if (!Finite(volume.eye) || !Finite(request.eye) ||
            volume.extent != request.extent || volume.texel != request.texel || volume.resolution != request.resolution ||
            volume.reuseGuard != request.reuseGuard || !std::isfinite(volume.reuseGuard) ||
            !(volume.texel > 0) || !std::isfinite(volume.texel)) return false;
        const auto same = [](const XMFLOAT3& a, const XMFLOAT3& b) noexcept {
            return Finite(a) && Finite(b) && a.x == b.x && a.y == b.y && a.z == b.z;
        };
        if (!same(volume.right, request.right) || !same(volume.up, request.up) ||
            !same(volume.forward, request.forward)) return false;
        for (const auto& axis : {volume.right, volume.up, volume.forward}) {
            const float squared=XMVectorGetX(XMVector3LengthSq(XMLoadFloat3(&axis)));
            if (std::fabs(squared-1.f) > 1.e-4f) return false;
        }
        const float guard = volume.reuseGuard;
        const float reach = guard - 3 * volume.texel;
        if (!(reach > 0)) return false;
        const auto delta = XMLoadFloat3(&request.eye) - XMLoadFloat3(&volume.eye);
        for (const auto& axis : {volume.right, volume.up, volume.forward}) {
            const float distance = std::fabs(XMVectorGetX(XMVector3Dot(delta, XMLoadFloat3(&axis))));
            if (!std::isfinite(distance) || distance > reach) return false;
        }
        return true;
    }

    struct Epoch
    {
        const void* device{};
        const void* scene{};
        const void* livePlayer{};
        const void* firstPerson{};
        const void* player{};
        std::uint32_t loadGeneration{}, frame{};
        std::uint64_t cameraGeneration{}, poseSerial{}, sourceSerial{}, playerGeneration{};
        std::array<float, 13> playerTransform{}; 
        bool smallObjects{true};
        bool outdoorsOnly{};
        bool playerSunlightShadows{};
        bool lowShadowQuality{};
        unsigned wideResolution{Resolution}, detailResolution{Resolution};
        std::array<float,4> range{};
        bool operator==(const Epoch&) const = default;
        bool Valid() const noexcept
        {
            if (!device || !scene || !loadGeneration || !frame || !cameraGeneration) return false;
            return !player || (poseSerial && sourceSerial && playerGeneration &&
                std::all_of(playerTransform.begin(), playerTransform.end(), [](float n) { return std::isfinite(n); }) &&
                std::fabs(playerTransform.back()) > 1.e-5f);
        }
    };

    struct Publication
    {
        Epoch epoch{};
        View volume{};
        XMFLOAT4X4 matrix{}; 
        bool complete{};
        void Invalidate() noexcept { complete = false; }
        bool Matches(const Epoch& candidate, const View& request) const noexcept
        { return complete && candidate.Valid() && epoch == candidate && Covers(volume, request); }
        bool Publish(const Epoch& source, const View& fit, const XMFLOAT4X4& nativeMatrix) noexcept
        {
            complete = false;
            if (!source.Valid() || !Covers(fit, fit)) return false;
            for (const auto& row : nativeMatrix.m) for (const float value : row)
                if (!std::isfinite(value)) return false;
            epoch = source; volume = fit; matrix = nativeMatrix; complete = true;
            return true;
        }
    };
    struct alignas(16) Constants
    {
        XMUINT4 contract{}; 
        std::array<XMFLOAT4X4,2> worldToTexture{};
        XMFLOAT4 sampling{1.f/Resolution,1.f/Resolution, .025f/(Far-Near), 0}; 
        XMFLOAT4 range{}; 
    };
    static_assert(sizeof(Constants)==176);

    struct Bindings
    {
        ID3D11Buffer* constants{};
        std::array<ID3D11ShaderResourceView*,3> resources{};
        ID3D11SamplerState* sampler{};
        void Compute(ID3D11DeviceContext* context) const noexcept
        {
            context->CSSetConstantBuffers(ConstantSlot,1,&constants);
            context->CSSetShaderResources(TextureSlot,3,resources.data());
            context->CSSetSamplers(SamplerSlot,1,&sampler);
        }
        void Pixel(ID3D11DeviceContext* context) const noexcept
        {
            context->PSSetConstantBuffers(ConstantSlot,1,&constants);
            context->PSSetShaderResources(TextureSlot,3,resources.data());
            context->PSSetSamplers(SamplerSlot,1,&sampler);
        }
    };

    struct PixelLease
    {
        ID3D11DeviceContext* context{};
        ID3D11DeviceContext1* context1{};
        UINT first{},count{};
        Bindings saved{}, replacement{};
        bool Begin(ID3D11DeviceContext* target,const Bindings& value) noexcept
        {
            if(context || !target) return false;
            context=target; replacement=value;
            context->PSGetConstantBuffers(ConstantSlot,1,&saved.constants);
            if(SUCCEEDED(context->QueryInterface(IID_PPV_ARGS(&context1)))) {
                ID3D11Buffer* buffer{};
                context1->PSGetConstantBuffers1(ConstantSlot,1,&buffer,&first,&count);
                if(buffer) buffer->Release();
            }
            context->PSGetShaderResources(TextureSlot,3,saved.resources.data());
            context->PSGetSamplers(SamplerSlot,1,&saved.sampler);
            Apply(); return true;
        }
        void Apply() const noexcept { if(context) replacement.Pixel(context); }
        bool Restore() noexcept
        {
            if(!context) return true;
            saved.Pixel(context);
            if(context1) {
                context1->PSSetConstantBuffers1(ConstantSlot,1,&saved.constants,&first,&count);
                context1->Release();
            }
            if(saved.constants) saved.constants->Release();
            if(saved.sampler) saved.sampler->Release();
            for(auto* resource:saved.resources) if(resource) resource->Release();
            *this={}; return true;
        }
    };
    class DepthTarget
    {
    public:
        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
        Microsoft::WRL::ComPtr<ID3D11DepthStencilView> dsv;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
        unsigned width{},height{};
        bool Create(ID3D11Device* device,unsigned w,unsigned h) noexcept
        {
            if(!device || !w || !h || w>8192 || h>8192) return false;
            if(texture && width==w && height==h) {
                Microsoft::WRL::ComPtr<ID3D11Device> owner; texture->GetDevice(&owner);
                if(owner.Get()==device) return true;
            }
            D3D11_TEXTURE2D_DESC desc{};
            desc.Width=w; desc.Height=h; desc.MipLevels=desc.ArraySize=1;
            desc.Format=DXGI_FORMAT_R32_TYPELESS; desc.SampleDesc.Count=1;
            desc.BindFlags=D3D11_BIND_DEPTH_STENCIL|D3D11_BIND_SHADER_RESOURCE;
            Microsoft::WRL::ComPtr<ID3D11Texture2D> newTexture;
            Microsoft::WRL::ComPtr<ID3D11DepthStencilView> newDSV;
            Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> newSRV;
            D3D11_DEPTH_STENCIL_VIEW_DESC depth{}; depth.Format=DXGI_FORMAT_D32_FLOAT; depth.ViewDimension=D3D11_DSV_DIMENSION_TEXTURE2D;
            D3D11_SHADER_RESOURCE_VIEW_DESC read{}; read.Format=DXGI_FORMAT_R32_FLOAT;
            read.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D; read.Texture2D.MipLevels=1;
            if(FAILED(device->CreateTexture2D(&desc,nullptr,&newTexture)) ||
               FAILED(device->CreateDepthStencilView(newTexture.Get(),&depth,&newDSV)) ||
               FAILED(device->CreateShaderResourceView(newTexture.Get(),&read,&newSRV))) return false;
            texture=std::move(newTexture); dsv=std::move(newDSV); srv=std::move(newSRV); width=w;height=h;
            return true;
        }
        void Bind(ID3D11DeviceContext* context) const noexcept
        {
            context->OMSetRenderTargets(0,nullptr,dsv.Get());
            const D3D11_VIEWPORT viewport{0,0,float(width),float(height),0,1};
            const D3D11_RECT scissor{0,0,LONG(width),LONG(height)};
            context->RSSetViewports(1,&viewport); context->RSSetScissorRects(1,&scissor);
        }
    };
}

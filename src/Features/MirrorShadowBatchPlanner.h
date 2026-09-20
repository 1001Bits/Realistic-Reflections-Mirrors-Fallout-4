#pragma once
#include "MirrorPrivateSun.h"

class MirrorShadowBatchPlanner
{
    struct Entry {
        std::uint32_t receiver{}, frame{};
        MirrorPrivateSun::View view{};
        MirrorShadowCasterVolume volume{};
    };
    std::array<Entry,64> entries_{};
    std::uint32_t load_{};
    static DirectX::XMFLOAT3 Coordinates(const MirrorPrivateSun::View& view) noexcept
    {
        using namespace DirectX;
        const auto eye=XMLoadFloat3(&view.eye);
        return {XMVectorGetX(XMVector3Dot(eye,XMLoadFloat3(&view.right))),
            XMVectorGetX(XMVector3Dot(eye,XMLoadFloat3(&view.up))),
            XMVectorGetX(XMVector3Dot(eye,XMLoadFloat3(&view.forward)))};
    }
    static bool Center(const MirrorPrivateSun::View& source,const DirectX::XMFLOAT3& low,
        const DirectX::XMFLOAT3& high,MirrorPrivateSun::View& result) noexcept
    {
        using namespace DirectX;
        if (!(source.texel>0)) return false;
        const auto snap=[step=source.texel*16.f](float v){return std::floor(v/step+.5f)*step;};
        const float x=snap((low.x+high.x)*.5f),y=snap((low.y+high.y)*.5f),
            z=snap((low.z+high.z)*.5f+3*MirrorPrivateSun::WideExtent)-3*MirrorPrivateSun::WideExtent;
        result=source;
        XMStoreFloat3(&result.eye,XMLoadFloat3(&source.right)*x+XMLoadFloat3(&source.up)*y+XMLoadFloat3(&source.forward)*z);
        auto relative=XMMatrixSet(source.right.x,source.up.x,source.forward.x,0,
            source.right.y,source.up.y,source.forward.y,0,source.right.z,source.up.z,source.forward.z,0,0,0,0,1);
        const auto projection=XMMatrixOrthographicLH(2*source.extent,2*source.extent,MirrorPrivateSun::Near,MirrorPrivateSun::Far);
        XMStoreFloat4x4(&result.relativeViewProjection,relative*projection);
        relative.r[3]=XMVectorSet(-x,-y,-z,1);
        const auto texture=XMMatrixSet(.5f,0,0,0,0,-.5f,0,0,0,0,1,0,.5f,.5f,0,1);
        XMStoreFloat4x4(&result.worldToTexture,relative*projection*texture);
        return MirrorPrivateSun::Finite(result.eye);
    }
public:
    void Prepare(std::uint32_t receiver,std::uint32_t frame,std::uint32_t load,
        MirrorPrivateSun::View& map,const MirrorShadowCasterVolume& volume,MirrorShadowCasterSet& casters) noexcept
    {
        casters.Reset(volume);
        if (load_!=load) { entries_={};load_=load; }
        for(auto& entry:entries_) if(!frame || frame<entry.frame || frame-entry.frame>4) entry={};
        if(!receiver || !frame || !volume.valid) return;
        const auto request=map;
        auto low=Coordinates(request),high=low;
        std::array<MirrorPrivateSun::View,64> accepted{};unsigned count=0;
        for(const auto& entry:entries_) {
            if(!entry.receiver || entry.receiver==receiver || !entry.volume.valid ||
                entry.view.extent!=request.extent || entry.view.resolution!=request.resolution ||
                entry.view.reuseGuard!=request.reuseGuard) continue;

            auto predicted=request;
            predicted.eye=entry.view.eye;
            const auto point=Coordinates(predicted);
            DirectX::XMFLOAT3 lo{(std::min)(low.x,point.x),(std::min)(low.y,point.y),(std::min)(low.z,point.z)},
                hi{(std::max)(high.x,point.x),(std::max)(high.y,point.y),(std::max)(high.z,point.z)};
            MirrorPrivateSun::View candidate;
            if(!Center(request,lo,hi,candidate) || !MirrorPrivateSun::Covers(candidate,request) ||
                !MirrorPrivateSun::Covers(candidate,predicted)) continue;
            bool allCovered=true;
            for(unsigned i=0;i<count;++i) allCovered &= MirrorPrivateSun::Covers(candidate,accepted[i]);
            if(!allCovered || !casters.Add(entry.volume)) continue;
            accepted[count++]=predicted;map=candidate;low=lo;high=hi;
        }
        auto* replacement=&entries_[0];
        for(auto& entry:entries_) {
            if(entry.receiver==receiver) {replacement=&entry;break;}
            if(!entry.receiver || entry.frame<replacement->frame) replacement=&entry;
        }
        *replacement={receiver,frame,request,volume};
    }
};

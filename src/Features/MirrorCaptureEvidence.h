#pragma once
#include "MirrorFrameHistogram.h"
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace MirrorPerformance
{

    struct CaptureEvidence
    {
        struct Shape {
            unsigned width{},height{};
            std::uint64_t publications{},allocations{},poolHits{},allocationFailures{};
            double allocationMs{},worstAllocationMs{};
        };
        struct Receiver {
            unsigned id{},lastSourceFrame{};
            std::uint64_t publications{},bodyRequired{},bodySubmitted{},lastPose{};
            double firstMs{},lastMs{},maxGapMs{};
        };
        std::array<Shape,32> shapes{};
        std::array<Receiver,64> receivers{};
        std::uint64_t bodyMissing{},retainedBytes{},sourceFrames{};
        double sourceTotalMs{},sourceLastMs{-1};
        unsigned lastSourceFrame{};
        bool overflow{};
        FrameHistogram sourceHistogram;
        Shape* ShapeFor(unsigned w,unsigned h) noexcept
        {
            if(!w || !h) {overflow=true;return nullptr;}
            for(auto& s:shapes) if(s.width==w && s.height==h) return &s;
            for(auto& s:shapes) if(!s.width) {s.width=w;s.height=h;return &s;}
            overflow=true;return nullptr;
        }
        void Publish(unsigned id,unsigned w,unsigned h,unsigned frame,std::uint64_t pose,
            bool required,bool submitted,double now) noexcept
        {
            if(auto* s=ShapeFor(w,h)) ++s->publications;
            Receiver* receiver{};
            for(auto& r:receivers) if(r.id==id && id) {receiver=&r;break;}
            if(!receiver && id) for(auto& r:receivers) if(!r.id) {receiver=&r;r.id=id;break;}
            if(!receiver) {overflow=true;return;}
            auto& r=*receiver;
            if(r.publications) r.maxGapMs=(std::max)(r.maxGapMs,now-r.lastMs);
            else r.firstMs=now;
            ++r.publications;r.lastMs=now;r.lastSourceFrame=frame;r.lastPose=pose;
            r.bodyRequired+=required;r.bodySubmitted+=submitted;
            bodyMissing+=required && !submitted;
        }
        void Allocation(unsigned w,unsigned h,double ms,bool success,bool pooled,std::uint64_t retained) noexcept
        {
            retainedBytes=retained;
            if(auto* s=ShapeFor(w,h)) {
                s->poolHits+=pooled;
                if(!pooled) {++s->allocations;s->allocationFailures+=!success;s->allocationMs+=ms;
                    s->worstAllocationMs=(std::max)(s->worstAllocationMs,ms);}
            }
        }
        void SourceFrame(unsigned frame,double now,bool active) noexcept
        {
            if(frame==lastSourceFrame) return;
            lastSourceFrame=frame;
            if(!active) {sourceLastMs=-1;return;}
            const auto elapsed=now-sourceLastMs;
            if(sourceLastMs>=0 && elapsed>0 && elapsed<=1000 && std::isfinite(elapsed)) {
                ++sourceFrames;sourceTotalMs+=elapsed;sourceHistogram.Add(elapsed);
            }
            sourceLastMs=now;
        }
        bool Complete() const noexcept {return !overflow && !bodyMissing;}
        bool Comparable(bool rendering) const noexcept
        {
            return Complete() && (!rendering || std::any_of(receivers.begin(),receivers.end(),
                [](const Receiver& receiver) {return receiver.publications!=0;}));
        }
    };
    inline bool BenchmarkDriftAcceptable(double initialMean,double finalMean,double initialP95,double finalP95) noexcept
    {
        return initialMean>0 && initialP95>0 && std::isfinite(finalMean) && std::isfinite(finalP95) &&
            std::abs(finalMean/initialMean-1)<=.10 && std::abs(finalP95/initialP95-1)<=.15;
    }
}

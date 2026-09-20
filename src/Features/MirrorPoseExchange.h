#pragma once
#include <array>
#include <atomic>

class MirrorPoseExchange
{
public:
    static constexpr int Slots=3, None=-1;
    int BeginWrite() noexcept
    {
        const int published=published_.load(std::memory_order_acquire);
        for(int i=0;i<Slots;++i) {
            if(i==published) continue;
            unsigned expected=0;
            if(state_[i].compare_exchange_strong(expected,2,std::memory_order_acq_rel)) return i;
        }
        return None;
    }
    void EndWrite(int slot,bool complete) noexcept
    {
        state_[slot].store(0,std::memory_order_release);
        if(complete) published_.store(slot,std::memory_order_release);
    }

    int AcquireLatest(int held) noexcept
    {
        for(int attempt=0;attempt<Slots;++attempt) {
            const int candidate=published_.load(std::memory_order_acquire);
            if(candidate==None || candidate==held) return held;
            unsigned expected=0;
            if(!state_[candidate].compare_exchange_strong(expected,1,std::memory_order_acq_rel)) continue;
            if(candidate==published_.load(std::memory_order_acquire)) return candidate;
            Release(candidate);
        }
        return held;
    }
    void Release(int slot) noexcept
    { if(slot!=None) state_[slot].store(0,std::memory_order_release); }
private:
    std::array<std::atomic<unsigned>,Slots> state_{};
    std::atomic<int> published_{None};
};

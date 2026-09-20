#pragma once

#include "MirrorShadowCasterVolume.h"
#include <cstdint>
#include <vector>

class MirrorShadowCasterSet
{
public:
    void Reset(const MirrorShadowCasterVolume& current) noexcept
    {
        volumes_.clear();
        unpruned_ = !current.valid;
        if (!unpruned_ && !Add(current)) unpruned_ = true;
    }
    bool Add(const MirrorShadowCasterVolume& prediction) noexcept
    {
        if (unpruned_ || !prediction.valid) return true;
        try { volumes_.push_back(prediction); return true; }
        catch (...) { return false; } 
    }
    bool Assign(const MirrorShadowCasterSet& source) noexcept
    {
        try { volumes_ = source.volumes_; unpruned_ = source.unpruned_; return true; }
        catch (...) { volumes_.clear(); unpruned_ = false; return false; }
    }
    bool Intersects(const MirrorShadowCasterVolume::Point& center, float radius) const noexcept
    {
        if (unpruned_ || volumes_.empty()) return true;
        return std::any_of(volumes_.begin(), volumes_.end(), [&](const auto& volume) {
            return volume.Intersects(center, radius);
        });
    }
    bool Covers(const MirrorShadowCasterVolume& current) const noexcept
    {
        return unpruned_ || (current.valid && std::any_of(volumes_.begin(), volumes_.end(),
            [&](const auto& volume) { return volume.Covers(current); }));
    }
    std::size_t Size() const noexcept { return volumes_.size(); }

private:
    std::vector<MirrorShadowCasterVolume> volumes_;
    bool unpruned_{};
};

class MirrorShadowCasterHistory
{
public:
    void Prepare(std::uintptr_t receiver, std::uint32_t frame, std::uint32_t load,
        const MirrorShadowCasterVolume& current, MirrorShadowCasterSet& out) noexcept
    {
        out.Reset(current);
        if (load != load_) { entries_.clear(); load_ = load; }
        std::erase_if(entries_, [&](const Entry& entry) {
            return !frame || std::uint32_t(frame - entry.frame) > 4;
        });
        if (!frame || !receiver) return;
        for (const auto& entry : entries_)
            if (entry.receiver != receiver) (void)out.Add(entry.volume);
        const auto found = std::find_if(entries_.begin(), entries_.end(),
            [&](const Entry& entry) { return entry.receiver == receiver; });
        if (!current.valid) {
            if (found != entries_.end()) entries_.erase(found);
        } else if (found != entries_.end()) {
            *found = {receiver, frame, current};
        } else {
            try { entries_.push_back({receiver, frame, current}); }
            catch (...) {} 
        }
    }

private:
    struct Entry { std::uintptr_t receiver{}; std::uint32_t frame{}; MirrorShadowCasterVolume volume{}; };
    std::vector<Entry> entries_;
    std::uint32_t load_{};
};

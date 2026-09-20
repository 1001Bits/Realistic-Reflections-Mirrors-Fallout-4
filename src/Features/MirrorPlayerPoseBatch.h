#pragma once

#include <cstdint>
#include <limits>

class MirrorPlayerPoseBatch
{
public:
    struct Pose
    {
        std::uintptr_t root{};
        std::uint64_t generation{}, pose{}, source{};
        bool Valid() const noexcept { return root && generation && pose && source; }
        bool operator==(const Pose&) const = default;
    };

    bool Accepts(std::uint32_t frame, std::uint32_t load, const Pose& pose,
        std::uint64_t nativeSource, std::uint64_t pendingPrivateSource = 0) const noexcept
    {
        if (!pose.Valid()) return false;
        if (pose.source == nativeSource) return true;
        return active_ && frame && frame == frame_ && load == load_ &&
            pose.source != (std::numeric_limits<std::uint64_t>::max)() && nativeSource == pose.source + 1 &&
            (pose == admitted_ || pendingPrivateSource == nativeSource);
    }

    void Remember(std::uint32_t frame, std::uint32_t load, const Pose& pose,
        std::uint64_t sourceAtAdmission, std::uint64_t pendingPrivateSource = 0) noexcept
    {
        if (active_ && frame && frame == frame_ && load == load_ &&
            Accepts(frame, load, pose, sourceAtAdmission, pendingPrivateSource)) admitted_ = pose;
    }

    class Scope
    {
    public:
        Scope(MirrorPlayerPoseBatch& owner, std::uint32_t frame, std::uint32_t load) noexcept :
            owner_(owner), owns_(!owner.active_)
        {
            if (owns_) { owner_.active_ = true; owner_.frame_ = frame; owner_.load_ = load; owner_.admitted_ = {}; }
        }
        ~Scope() { if (owns_) { owner_.admitted_ = {}; owner_.active_ = false; } }
        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;
    private:
        MirrorPlayerPoseBatch& owner_;
        bool owns_;
    };

private:
    bool active_{};
    std::uint32_t frame_{}, load_{};
    Pose admitted_{};
};

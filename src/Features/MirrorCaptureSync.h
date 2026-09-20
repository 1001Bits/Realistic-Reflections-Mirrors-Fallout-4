#pragma once

#include "MirrorPlayerPoseBatch.h"

class MirrorCaptureSync
{
public:
    using Pose = MirrorPlayerPoseBatch::Pose;

    bool Seal(std::uint32_t frame, std::uint32_t load, std::uint64_t cameraGeneration,
        const Pose& pose, std::uint64_t nativeSource, bool playerRequired) noexcept
    {
        Reset();
        if (!frame || !cameraGeneration ||
            (playerRequired && (!pose.Valid() || pose.source != nativeSource))) return false;
        frame_ = frame;
        load_ = load;
        cameraGeneration_ = cameraGeneration;
        pose_ = pose;
        playerRequired_ = playerRequired;
        valid_ = true;
        return true;
    }

    bool Matches(std::uint32_t frame, std::uint32_t load, std::uint64_t cameraGeneration,
        const Pose& pose) const noexcept
    {
        return valid_ && frame == frame_ && load == load_ && cameraGeneration == cameraGeneration_ &&
            (!playerRequired_ || pose == pose_);
    }

    bool Active() const noexcept { return valid_; }
    bool PlayerRequired() const noexcept { return valid_ && playerRequired_; }
    void Reset() noexcept { valid_ = false; }

private:
    std::uint32_t frame_{}, load_{};
    std::uint64_t cameraGeneration_{};
    Pose pose_{};
    bool playerRequired_{}, valid_{};
};

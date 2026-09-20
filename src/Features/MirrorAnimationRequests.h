#pragma once

#include "MirrorAnimationInputs.h"

namespace MirrorAnimationRequests
{

    inline bool ThirdPersonLocomotion(const char* name) noexcept
    {
        if (!name) return false;

        for (const char* request : {"MoveStart", "MoveStop", "walkStart", "runStart", "walkRunBlendStart",
                 "moveForward", "moveBackward", "runbackstart", "runForwardStart",
                 "TurnLeft", "TurnRight", "TurnStop"})
            if (MirrorAnimationInputs::EqualName(name, request)) return true;
        return false;
    }

    inline bool ShouldForward(bool accepted, unsigned activeGraph, const char* name) noexcept
    {

        const bool terminal = name && (MirrorAnimationInputs::EqualName(name,"attackRelease") ||
            MirrorAnimationInputs::EqualName(name,"attackEnd") ||
            MirrorAnimationInputs::EqualName(name,"rifleSightedEnd"));
        return accepted || (activeGraph == 1 && (ThirdPersonLocomotion(name) || terminal));
    }

    inline const char* WeaponPostureCounterpart(unsigned activeGraph, const char* name) noexcept
    {
        if (activeGraph != 1 || !name) return nullptr;

        if (MirrorAnimationInputs::EqualName(name, "GunDown")) return "RelaxedStateStart";
        if (MirrorAnimationInputs::EqualName(name, "GunUp")) return "ReadyStateStart";
        return nullptr;
    }
}

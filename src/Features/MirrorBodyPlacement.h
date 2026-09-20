#pragma once
#include <cmath>

namespace MirrorBodyPlacement
{

    template<class Position, class Transform>
    bool AnchorFirstPerson(bool firstPerson, bool detached, const Position& actor,
        Transform& local, Transform& world) noexcept
    {
        if (!firstPerson || !detached || !std::isfinite(actor.x) ||
            !std::isfinite(actor.y) || !std::isfinite(actor.z)) return false;
        local.translate.x = world.translate.x = actor.x;
        local.translate.y = world.translate.y = actor.y;
        local.translate.z = world.translate.z = actor.z;
        return true;
    }

    template<class Transform, class ReadActorScale>
    bool RestoreFirstPersonScale(bool firstPerson, bool detached, bool tfpLoaded,
        ReadActorScale&& readActorScale, Transform& local, Transform& world) noexcept
    {
        if (!firstPerson || !detached || !tfpLoaded) return false;
        float scale{};
        if (!readActorScale(scale) || !std::isfinite(scale) || scale <= 0.0f) return false;
        local.scale = world.scale = scale;
        return true;
    }
}

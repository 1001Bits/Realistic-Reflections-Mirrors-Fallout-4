#pragma once
#include <cstdint>

struct MirrorPlayerDrawRequirements
{
    bool face{true}, nonFace{true}, faceSkin{true};
    constexpr bool Covered(std::uint32_t playerPasses, std::uint32_t facePasses,
        std::uint32_t nonFacePasses, std::uint32_t skinPasses,
        std::uint32_t drawnSkinPasses) const noexcept
    {
        return playerPasses != 0 && (!face || facePasses != 0) &&
            (!nonFace || nonFacePasses != 0) &&
            (!faceSkin || (skinPasses != 0 && drawnSkinPasses != 0));
    }
};

#pragma once

#include <cstdint>

namespace MirrorSunLocation
{

    constexpr bool InPrewarPlayerHouse(std::uint32_t cell, float x, float y, float z) noexcept
    {
        if (cell != 0x000A801Eu) return false;
        const float dx = x - (-80200.328125f);
        const float dy = y - 91262.0234375f;
        
        constexpr float c = 0.7755631f, s = -0.6312700f;
        const float localX = c * dx - s * dy;
        const float localY = s * dx + c * dy;
        const float localZ = z - 7852.6591796875f;
        return localX >= -912.0f && localX <= 368.0f &&
            localY >= -439.0f && localY <= 415.0f &&
            localZ >= -8.0f && localZ <= 208.0f;
    }
}

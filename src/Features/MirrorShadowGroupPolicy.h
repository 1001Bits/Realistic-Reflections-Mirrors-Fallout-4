#pragma once
#include <cstdint>

namespace MirrorPrivateSun
{

    inline void ConfigureFlatShadowGroup(std::uint8_t (&group)[0x170]) noexcept
    {
        group[0x168] = 0;
        group[0x169] = 0;
        group[0x16A] = 1;
    }
}

#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace MirrorSkinPalette
{
    template<class Calculate>
    bool Refresh(void* skin, std::uint32_t frame, Calculate&& calculate)
    {
        if (!skin) return false;
        auto* stamp = static_cast<std::byte*>(skin) + 0xBC;
        const std::uint32_t invalid = frame - 1u;
        std::memcpy(stamp, &invalid, sizeof(invalid));
        calculate();
        std::uint32_t calculated{};
        std::memcpy(&calculated, stamp, sizeof(calculated));
        return calculated == frame;
    }
}

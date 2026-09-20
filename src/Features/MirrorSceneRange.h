#pragma once
#include <array>
#include <algorithm>
#include <bit>
#include <cstdint>
#include <cmath>

namespace MirrorSceneRange
{
    inline constexpr std::uint32_t MinDistance = 500, MaxDistance = 5000, DefaultDistance = 2000;
    inline constexpr float ShadowPadding = 256.f, ShadowReuseGuard = 128.f;
    constexpr std::uint32_t ClampDistance(int distance) noexcept
    { return static_cast<std::uint32_t>(std::clamp(distance, int(MinDistance), int(MaxDistance))); }
    constexpr float ShadowExtent(float distance) noexcept { return distance + ShadowPadding; }
    inline bool Finite(float value) noexcept
    { return (std::bit_cast<std::uint32_t>(value) & 0x7f800000u) != 0x7f800000u; }
    struct Sphere
    {
        float x{}, y{}, z{}, radius{};
        bool Active() const noexcept
        { return Finite(x) && Finite(y) && Finite(z) && Finite(radius) && radius > 0; }
        bool Outside(float bx, float by, float bz, float boundRadius) const noexcept
        {

            if (!Active() || !Finite(bx) || !Finite(by) || !Finite(bz) ||
                !Finite(boundRadius) || boundRadius <= 1.f ||
                (std::fabs(bx) < 4.f && std::fabs(by) < 4.f && std::fabs(bz) < 4.f)) return false;
            const double dx = double(bx)-x, dy = double(by)-y, dz = double(bz)-z;
            const double reach = double(radius)+boundRadius;
            return dx*dx+dy*dy+dz*dz > reach*reach;
        }
    };
    struct Selection
    {
        Sphere sphere{};

        std::array<const void*,2> distantRoots{};
        bool Unlimited(const void* node) const noexcept
        {
            if (!node) return false;
            for (auto* root : distantRoots) if (root == node) return true;
            return false;
        }
    };
}

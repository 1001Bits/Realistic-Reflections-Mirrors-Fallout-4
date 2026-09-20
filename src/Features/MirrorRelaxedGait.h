#pragma once
#include "MirrorLocomotionRequests.h"

namespace MirrorLocomotion
{
    inline bool GaitName(const char* value, const char* expected) noexcept
    {
        if (!value) return false;
        for (std::size_t i = 0; ; ++i) {
            auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c; };
            if (lower(value[i]) != lower(expected[i])) return false;
            if (!expected[i]) return true;
        }
    }

    template<class NameOf>
    float RelaxedRateDirection(const void* contour, float direction, NameOf&& nameOf)
    {
        if (!contour || !std::isfinite(direction) ||
            Requests::Read<std::uint32_t>(contour, 0x68) != 3 ||
            Requests::Read<std::uint8_t>(contour, 0x70) != 0) return direction;
        const auto text = [&](const void* node, std::size_t offset) {
            return nameOf(static_cast<const std::byte*>(node) + offset);
        };
        if (!GaitName(text(contour, 0x40), "") || !GaitName(text(contour, 0x48), "")) return direction;
        const auto forwardOnly = [](const void* node) {
            for (const auto offset : {0x10u, 0x28u}) {
                if (Requests::Read<std::uint32_t>(node, offset + 0x10) != 1) return false;
                const auto* sample = Requests::Read<const void*>(node, offset);
                if (!sample) return false;
                const float angle = Requests::Read<float>(sample, 0);
                const float speed = Requests::Read<float>(sample, 4);
                if (!std::isfinite(angle) || std::fabs(angle) > 0.001f || !std::isfinite(speed)) return false;
            }
            return true;
        };
        if (!forwardOnly(contour)) return direction;
        const auto* children = Requests::Read<const void*>(contour, 0x58);
        if (!children) return direction;
        constexpr const char* events[]{"Walk", "Jog", "Run"};
        constexpr const char* rates[]{"WalkSpeedMult", "JogSpeedMult", "RunSpeedMult"};
        for (std::size_t i = 0; i < 3; ++i) {
            const auto* child = Requests::Read<const void*>(children, i * sizeof(void*));
            if (!child || !forwardOnly(child) ||
                !GaitName(text(child, 0x40), events[i]) ||
                !GaitName(text(child, 0x48), "iSyncLocomotionSpeed") ||
                Requests::Read<std::int32_t>(child, 0x50) != static_cast<std::int32_t>(i) ||
                !GaitName(text(child, 0x68), rates[i])) return direction;
            const float authoredSpeed = Requests::Read<float>(child, 0x70);
            if (!std::isfinite(authoredSpeed) || authoredSpeed <= 0) return direction;
        }
        return 0.f;
    }
}

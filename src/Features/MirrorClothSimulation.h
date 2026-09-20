#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace MirrorClothSimulation
{
    // Simulation follows animation time, never capture count or camera mode.
    struct Frame
    {
        float delta{}, step{};
        unsigned steps{};
        bool resetPrediction{};
    };

    class Clock
    {
        std::uint64_t serial_{}, generation_{};
    public:
        Frame Advance(std::uint64_t serial, std::uint64_t generation, float delta) noexcept
        {
            if (!serial || serial <= serial_ || !std::isfinite(delta) || delta <= 0.0f)
                return {};
            const bool reset = !serial_ || generation != generation_ || delta > 0.1f;
            serial_ = serial;
            generation_ = generation;
            // Bounded catch-up after pauses/hitches; never replay seconds of work.
            delta = (std::min)(delta, 1.0f / 15.0f);
            const unsigned steps = (std::min)(4u,
                (std::max)(1u, static_cast<unsigned>(std::ceil(delta * 60.0f - 0.00001f))));
            return {delta, delta / static_cast<float>(steps), steps, reset};
        }
    };

    // Display/static mesh buffers can address shared vertex storage. Until such
    // storage has an independent publication path, admit only solver-owned data.
    constexpr bool PrivateBufferType(std::uint32_t type) noexcept
    {
        return type == 1 || type == 2 || type == 6 || type == 7 || type == 8;
    }

    constexpr std::uint32_t MaxInstances = 32, MaxTransformSets = 128;
    constexpr std::uint32_t MaxBones = 512, MaxParticles = 16384;
    constexpr std::uint32_t MaxStepBytes = 8 * 1024 * 1024;
}

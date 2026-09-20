#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace MirrorNodeVisitSet
{
    enum class Result { added, duplicate, full };
    template<std::size_t Capacity> struct Set
    {
        static_assert(Capacity && (Capacity & (Capacity - 1)) == 0);
        std::array<const void*, Capacity> slots{};
        void Clear() noexcept { slots.fill(nullptr); }
        Result Insert(const void* node) noexcept
        {
            if (!node) return Result::duplicate;
            auto hash = reinterpret_cast<std::uintptr_t>(node) >> 3;
            hash ^= hash >> 13;
            for (std::size_t probe = 0; probe < Capacity; ++probe) {
                auto& slot = slots[(hash + probe) & (Capacity - 1)];
                if (slot == node) return Result::duplicate;
                if (!slot) { slot = node; return Result::added; }
            }
            return Result::full;
        }
    };
}

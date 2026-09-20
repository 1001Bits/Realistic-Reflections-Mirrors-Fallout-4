#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace MirrorSubmittedNodes
{

    template<std::size_t Capacity = 65536, class Generation = std::uint32_t>
    class Set
    {
        static_assert(Capacity > 0 && (Capacity & (Capacity - 1)) == 0);
        static_assert(std::is_unsigned_v<Generation> && !std::is_same_v<Generation, bool>);
    public:
        void Clear() noexcept
        {

            if (++generation == 0) { tags = {}; generation = 1; }
            count = 0;
        }

        [[nodiscard]] bool Insert(const void* key) noexcept
        {
            if (!key || count == Capacity) return false;
            auto slot = Slot(key);
            for (std::size_t probe = 0; probe < Capacity; ++probe) {
                if (tags[slot] != generation) {
                    keys[slot] = key;
                    tags[slot] = generation;
                    ++count;
                    return true;
                }
                if (keys[slot] == key) return false;
                slot = (slot + 1) & (Capacity - 1);
            }
            return false;
        }

        [[nodiscard]] bool Contains(const void* key) const noexcept
        {
            if (!key) return false;
            auto slot = Slot(key);
            for (std::size_t probe = 0; probe < Capacity; ++probe) {
                if (tags[slot] != generation) return false;
                if (keys[slot] == key) return true;
                slot = (slot + 1) & (Capacity - 1);
            }
            return false;
        }

    private:
        static std::size_t Slot(const void* key) noexcept
        {
            const auto value = reinterpret_cast<std::uintptr_t>(key);
            
            return ((value >> 4) ^ (value >> 21)) & (Capacity - 1);
        }
        std::array<const void*, Capacity> keys{};
        std::array<Generation, Capacity> tags{};
        Generation generation{1};
        std::size_t count{};
    };
}

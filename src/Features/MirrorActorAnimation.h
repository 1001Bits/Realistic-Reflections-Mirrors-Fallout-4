#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace MirrorActorAnimation
{
    struct Identity
    {
        const void* holder{};
        const void* root{};
        std::uint32_t formID{};
        bool operator==(const Identity&) const = default;
    };

    template<std::size_t Capacity> struct Demands
    {
        static_assert(Capacity > 0);
        struct Entry { Identity identity{}; std::uint64_t generation{}, until{}; };
        std::array<Entry, Capacity> entries{};
        static constexpr std::uint64_t kRetentionMs = 1500;

        bool Note(Identity actor, std::uint64_t generation, std::uint64_t now) noexcept
        {
            if (!actor.holder || !actor.root || !actor.formID || !generation) return false;
            Entry* oldest = &entries[0];
            Entry* available = nullptr;
            for (auto& entry : entries) {
                if (entry.identity.holder == actor.holder && entry.generation == generation) {
                    const bool renewed = entry.until <= now || entry.identity != actor;
                    entry.identity = actor;
                    entry.until = now + kRetentionMs;
                    return renewed;
                }
                if (entry.generation != generation || entry.until <= now) available = &entry;
                if (entry.until < oldest->until) oldest = &entry;
            }
            auto* slot = available ? available : oldest;
            *slot = {actor, generation, now + kRetentionMs};
            return true;
        }

        bool Find(const void* holder, std::uint64_t generation, std::uint64_t now, Identity& actor) const noexcept
        {
            for (const auto& entry : entries) {
                if (holder && entry.identity.holder == holder && entry.generation == generation && entry.until > now) {
                    actor = entry.identity;
                    return true;
                }
            }
            return false;
        }
    };

    struct alignas(16) UpdateData
    {
        std::array<std::byte, 0x20> bytes{};
        template<class ShouldUpdate, class NativeUpdate>
        static bool UpdateVisibleActor(void* source, ShouldUpdate&& shouldUpdate, NativeUpdate&& update)
        {
            UpdateData copy;
            std::memcpy(copy.bytes.data(), source, copy.bytes.size());

            if (copy.bytes[0x1A] != std::byte{0}) return update(source);
            if (!shouldUpdate()) return false;

            copy.bytes[0x1A] = std::byte{1};
            return update(copy.bytes.data());
        }
    };
    static_assert(sizeof(UpdateData) == 0x20);
}

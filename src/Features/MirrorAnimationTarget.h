#pragma once

#include <cstdint>

namespace MirrorAnimationTarget
{

    struct TargetContext
    {
        const void* character{};
        const void* behavior{};
    };
    static_assert(sizeof(TargetContext) == 16);

    struct Scope
    {
        const void* privateGraph{};
        const void* sourceGraph{};
        const void* player{};
    };
    inline thread_local Scope current{};

    inline bool Plausible(const void* value) noexcept
    {
        const auto address = reinterpret_cast<std::uintptr_t>(value);
        return address > 0x10000u && address <= 0x00007FFFFFFFFFFFULL && (address & 7u) == 0u;
    }

    template <class ReadPointer>
    const void* Graph(const void* context, ReadPointer read) noexcept
    {
        if (!Plausible(context)) return nullptr;
        const void* behavior = read(context, 8u);
        if (!behavior) {
            const void* character = read(context, 0u);
            if (!Plausible(character)) return nullptr;
            behavior = read(character, 0x80u);
        }
        return Plausible(behavior) ? read(behavior, 0x30u) : nullptr;
    }

    template <class ReadPointer>
    bool ActorContext(const void* context, TargetContext& result, ReadPointer read) noexcept
    {
        result = {};
        if (!Plausible(current.privateGraph) ||
            !Plausible(current.sourceGraph) || !Plausible(current.player) ||
            current.privateGraph == current.sourceGraph || !Plausible(context))
            return false;
        const auto* owner = Graph(context, read);
        const auto* privateCharacter = reinterpret_cast<const void*>(
            reinterpret_cast<std::uintptr_t>(current.privateGraph) + 0x1C8u);

        if (owner != current.privateGraph && (owner || read(context, 0u) != privateCharacter)) return false;

        const TargetContext source{
            reinterpret_cast<const void*>(reinterpret_cast<std::uintptr_t>(current.sourceGraph) + 0x1C8u), nullptr };
        if (Graph(&source, read) != current.sourceGraph || read(current.sourceGraph, 0x380u) != current.player)
            return false;
        result = source;
        return true;
    }

    template <class ReadPointer>
    bool Adapt(const void* context, std::int32_t target, TargetContext& result, ReadPointer read) noexcept
    {
        result = {};
        return target >= 2 && target <= 4 && ActorContext(context, result, read);
    }

    template <class ReadPointer>
    bool AdaptConversion(const void* context, TargetContext& result, ReadPointer read) noexcept
    {
        result = {};
        TargetContext source{};
        if (!ActorContext(context, source, read)) return false;
        const auto* character = read(context, 0u);
        const auto* privateCharacter = reinterpret_cast<const void*>(
            reinterpret_cast<std::uintptr_t>(current.privateGraph) + 0x1C8u);
        if (character != privateCharacter) return false;

        result = {character, read(source.character, 0x80u)};
        return true;
    }

    template <class ReadPointer, class NativeConvert>
    bool Convert(const void* context, void* position, ReadPointer read, NativeConvert native)
    {
        TargetContext adapted{};
        const bool useActor = position && AdaptConversion(context, adapted, read);
        native(useActor ? &adapted : context, position);
        return useActor;
    }
}

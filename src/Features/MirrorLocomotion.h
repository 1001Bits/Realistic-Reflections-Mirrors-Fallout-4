#pragma once
#include "MirrorLocomotionRequests.h"
#include "MirrorRelaxedGait.h"

namespace MirrorLocomotion
{
    struct Context
    {
        const void* playerHolder{};
        const void* reflectionHolder{};
        const void* reflectionManager{};
        bool polling{};
        Requests* requests{};
        const Requests* previousRequests{};
        unsigned contourDepth{};
    };

    inline thread_local Context current{};

    inline bool OwnsChannels(const void* manager) noexcept
    {
        return manager && manager == current.reflectionManager &&
            current.playerHolder && current.reflectionHolder &&
            current.playerHolder != current.reflectionHolder;
    }

    inline const void* ContourHolder(const void* requested) noexcept
    {
        return current.polling && OwnsChannels(current.reflectionManager) &&
            requested == current.playerHolder ? current.reflectionHolder : requested;
    }

    inline Context Begin(const void* player, const void* reflection, const void* manager,
        Requests* requests = nullptr, const Requests* previousRequests = nullptr) noexcept
    {
        const auto previous = current;
        current = {player, reflection, manager, false, requests, previousRequests, 0};
        return previous;
    }

    inline void Restore(const Context& previous) noexcept { current = previous; }
}

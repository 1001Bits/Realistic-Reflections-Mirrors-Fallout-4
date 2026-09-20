

bool IsPrivateMovementChannel(const void* channel) noexcept
{
    const auto* manager = static_cast<const RE::BSAnimationGraphManager*>(MirrorLocomotion::current.reflectionManager);
    if (channel && MirrorLocomotion::OwnsChannels(manager))
        for (std::uint32_t i = 0; i < manager->boundChannel.size(); ++i)
            if (manager->boundChannel[i].get() == channel) return true;
    return false;
}

struct MirrorDirectionChannelHook
{
    static void thunk(void* channel, bool allowGameplay)
    {

        func(channel, IsPrivateMovementChannel(channel) ? false : allowGameplay);
    }
    static inline REL::Relocation<decltype(thunk)> func;
};
struct MirrorMovementControlsHook
{
    static bool thunk(const RE::Actor* actor)
    {

        if (actor && MirrorLocomotion::current.polling &&
            MirrorLocomotion::OwnsChannels(MirrorLocomotion::current.reflectionManager) &&
            static_cast<const RE::IAnimationGraphManagerHolder*>(actor) == MirrorLocomotion::current.playerHolder)
            return true;
        return func(actor);
    }
    static inline REL::Relocation<decltype(thunk)> func;
};

struct MirrorActiveContourHook
{
    static bool thunk(const RE::IAnimationGraphManagerHolder* holder, void* contour)
    {
        return func(static_cast<const RE::IAnimationGraphManagerHolder*>(
            MirrorLocomotion::ContourHolder(holder)), contour);
    }
    static inline REL::Relocation<decltype(thunk)> func;
};

struct MirrorSpeedChannelHook
{
    static void thunk(void* channel, bool allowGameplay)
    {
        if (!IsPrivateMovementChannel(channel)) {
            func(channel, allowGameplay);
            return;
        }
        const bool previous = MirrorLocomotion::current.polling;
        MirrorLocomotion::current.polling = true;
        __try {

            func(channel, false);
        } __finally {
            MirrorLocomotion::current.polling = previous;
        }
    }
    static inline REL::Relocation<decltype(thunk)> func;
};

struct MirrorContourRequestsHook
{
    static float RateDirection(const void* contour, float direction) noexcept
    {
        __try {
            return MirrorLocomotion::RelaxedRateDirection(contour, direction, [](const void* name) {
                return static_cast<const RE::BSFixedString*>(name)->c_str();
            });
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            
            return direction;
        }
    }
    static std::uint32_t EvaluatePrivate(void* contour, const void* requestedSpeed, float direction,
        void* graphSpeed, const void* adjustments)
    {
        MirrorLocomotion::ArenaView<RE::BSFixedString> history(MirrorLocomotion::current.previousRequests);
        return func(contour, requestedSpeed, RateDirection(contour, direction), &history.header, graphSpeed, adjustments);
    }
    static std::uint32_t thunk(void* contour, const void* requestedSpeed, float direction,
        const void* inputs, void* graphSpeed, const void* adjustments)
    {
        if (!MirrorLocomotion::current.polling || !MirrorLocomotion::current.requests ||
            !MirrorLocomotion::OwnsChannels(MirrorLocomotion::current.reflectionManager))
            return func(contour, requestedSpeed, direction, inputs, graphSpeed, adjustments);
        const auto previousDepth = MirrorLocomotion::current.contourDepth++;
        __try {
            const auto result = previousDepth == 0 ?
                EvaluatePrivate(contour, requestedSpeed, direction, graphSpeed, adjustments) :
                func(contour, requestedSpeed, direction, inputs, graphSpeed, adjustments);
            if (result != 0 && previousDepth == 0) {
                MirrorLocomotion::current.requests->CaptureUnsafe(adjustments, [](const void* record) {
                    return static_cast<const RE::BSFixedString*>(record)->c_str();
                });
            }
            return result;
        } __finally {
            MirrorLocomotion::current.contourDepth = previousDepth;
        }
    }
    static inline REL::Relocation<decltype(thunk)> func;
};

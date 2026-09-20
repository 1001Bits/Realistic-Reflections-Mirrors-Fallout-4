

const void* ReadAnimationTargetPointer(const void* object, std::size_t offset) noexcept
{
    if (!MirrorAnimationTarget::Plausible(object)) return nullptr;
    __try {
        return *reinterpret_cast<const void* const*>(
            reinterpret_cast<const std::uint8_t*>(object) + offset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

struct MirrorAnimationTargetHook
{
    static void thunk(void* singleton, const void* context, std::int32_t target,
        bool* found, void* position, void* actorTransform)
    {
        MirrorAnimationTarget::TargetContext adapted{};
        const bool useActorTarget = MirrorAnimationTarget::Adapt(
            context, target, adapted, ReadAnimationTargetPointer);
        func(singleton, useActorTarget ? &adapted : context, target, found, position, actorTransform);
        if (MirrorAnimationTarget::current.privateGraph && target >= 2 && target <= 4) {
            static std::uint32_t samples = 0u;
            if (samples++ < 8u) {
                logger::info("[ReflectionPlayerBody] AIMTARGET native player target={} adapted={} found={} privateGraph={} sourceGraph={}",
                    target, useActorTarget, found && *found, MirrorAnimationTarget::current.privateGraph,
                    MirrorAnimationTarget::current.sourceGraph);
            }
        }
    }
    static inline REL::Relocation<decltype(thunk)> func;
};

struct MirrorAnimationTargetSpaceHook
{
    static void thunk(const void* context, void* position)
    {
        const bool adapted = MirrorAnimationTarget::Convert(context, position,
            ReadAnimationTargetPointer,
            [](const void* nativeContext, void* target) { func(nativeContext, target); });
        if (adapted) {
            static std::uint32_t samples = 0u;
            if (samples++ < 8u)
                logger::info("[ReflectionPlayerBody] AIMSPACE native target conversion: actor supplied, private character retained");
        }
    }
    static inline REL::Relocation<decltype(thunk)> func;
};

bool InstallMirrorAnimationTargetHook() noexcept
{
    constexpr std::array<std::uint8_t, 16> entry163{
        0x40, 0x55, 0x41, 0x57, 0x48, 0x8D, 0x6C, 0x24, 0xB8, 0x48, 0x81, 0xEC, 0x48, 0x01, 0x00, 0x00 };
    constexpr std::array<std::uint8_t, 16> entry240{
        0x40, 0x55, 0x41, 0x54, 0x48, 0x8D, 0x6C, 0x24, 0xB8, 0x48, 0x81, 0xEC, 0x48, 0x01, 0x00, 0x00 };

    constexpr std::array<std::uint8_t, 16> conversion163{
        0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x81, 0xEC, 0xA0, 0x00, 0x00, 0x00, 0x4C, 0x8B, 0x01 };
    constexpr std::array<std::uint8_t, 16> conversion240{
        0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x50, 0x48, 0x83, 0x39, 0x00, 0x48, 0x8B };
    const bool port240 = ReflectionRuntime::IsPort240();
    const auto address = REL::Module::get().base() + (port240 ? 0xE18CA0u : 0xF9AC50u);
    const auto conversionAddress = REL::Module::get().base() + (port240 ? 0xE149A0u : 0xF92B90u);
    const auto& expected = port240 ? entry240 : entry163;
    const auto& expectedConversion = port240 ? conversion240 : conversion163;
    if (REL::Module::IsVR() || std::memcmp(reinterpret_cast<const void*>(address),
            expected.data(), expected.size()) != 0 ||
        std::memcmp(reinterpret_cast<const void*>(conversionAddress),
            expectedConversion.data(), expectedConversion.size()) != 0) {
        logger::error("[ReflectionPlayerBody] private aim target adapter unavailable: native entry-byte contract mismatch");
        return false;
    }

    MirrorAnimationTargetHook::func = address;
    MirrorAnimationTargetSpaceHook::func = conversionAddress;
    const auto status = stl::InstallDetours({
        { reinterpret_cast<PVOID*>(&MirrorAnimationTargetHook::func), reinterpret_cast<PVOID>(MirrorAnimationTargetHook::thunk) },
        { reinterpret_cast<PVOID*>(&MirrorAnimationTargetSpaceHook::func), reinterpret_cast<PVOID>(MirrorAnimationTargetSpaceHook::thunk) } });
    if (status != NO_ERROR) {
        logger::error("[ReflectionPlayerBody] native aim target/conversion transaction failed ({})", status);
        return false;
    }
    logger::info("[ReflectionPlayerBody] native actor aim target adapter installed at {}, conversion {}; private graph ownership and destination pose retained",
        reinterpret_cast<void*>(address), reinterpret_cast<void*>(conversionAddress));
    return true;
}

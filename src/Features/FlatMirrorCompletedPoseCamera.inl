

#pragma push_macro("far")
#undef far
bool ReadCompletedMirrorCamera(CompletedMirrorCamera& camera, std::uint64_t sourceSerial) noexcept
{
    if (g_playerRetargetWaiters.load(std::memory_order_acquire) ||
        !TryAcquireSRWLockShared(&g_playerNaturalPoseLock)) return false;
    bool valid = false;
    __try {
        __try {
            if (g_playerRetargetWaiters.load(std::memory_order_acquire) ||
                sourceSerial != g_playerNativeUpdateSerial.load(std::memory_order_acquire)) return false;
            auto* pc = RE::PlayerCamera::GetSingleton();
            const auto* root = pc ? pc->cameraRoot.get() : nullptr;
            auto getWorldCamera = reinterpret_cast<RE::NiCamera* (*)()>(
                REL::Offset(ReflectionRuntime::Rva(kRVA_WorldRootCamera)).address());
            const auto* source = getWorldCamera();
            if (!root || !source) return false;
            const auto eyeWorld = root->world;
            camera.local = source->local;
            camera.world = source->world;
            camera.previousWorld = source->previousWorld;
            std::memcpy(camera.worldToCam, source->worldToCam, sizeof(camera.worldToCam));
            camera.frustum = source->viewFrustum;
            camera.port = source->port;
            camera.minNear = source->minNearPlaneDist;
            camera.maxRatio = source->maxFarNearRatio;
            camera.lod = source->lodAdjust;
            camera.eye = eyeWorld.translate;
            camera.frame = GetRenderFrame();
            camera.source = sourceSerial;
            valid = camera.frame && FinitePrivatePlayerTransform(eyeWorld) &&
                FinitePrivatePlayerTransform(camera.world) &&
                std::isfinite(camera.frustum.far) && camera.frustum.far > 0.0f &&
                !std::memcmp(&eyeWorld, &root->world, sizeof(eyeWorld)) &&
                !std::memcmp(&camera.world, &source->world, sizeof(camera.world)) &&
                !std::memcmp(&camera.frustum, &source->viewFrustum, sizeof(camera.frustum)) &&
                sourceSerial == g_playerNativeUpdateSerial.load(std::memory_order_acquire);
        } __except (EXCEPTION_EXECUTE_HANDLER) { valid = false; }
    } __finally { ReleaseSRWLockShared(&g_playerNaturalPoseLock); }
    return valid;
}
#pragma pop_macro("far")

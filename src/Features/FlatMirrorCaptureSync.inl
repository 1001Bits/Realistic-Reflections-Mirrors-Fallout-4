

#pragma push_macro("far")
#undef far 
RE::NiCamera* g_mirrorCaptureSourceCamera{};
RE::NiPoint3 g_mirrorCaptureEye{};

float MirrorCaptureSourceFarPlane() noexcept { return g_mirrorCaptureSourceCamera->viewFrustum.far; }

enum class MirrorSyncResult : unsigned
{
    Ready, CloneUnavailable, NativeUpdateBusy, SourceMismatch, CameraUnavailable,
    CameraChanged, InvalidEpoch, Count
};

bool EnsureMirrorCaptureSourceCamera() noexcept
{
    __try {
        if (g_mirrorCaptureSourceCamera) return true;
        auto allocate = reinterpret_cast<NiAVObject_new_t>(
            REL::Offset(ReflectionRuntime::Rva(kRVA_NiAVObject_new)).address());
        auto construct = reinterpret_cast<NativeCameraCtor_t>(
            REL::Offset(ReflectionRuntime::Rva(kRVA_NativeCameraCtor)).address());
        void* memory = allocate(0x1e0);
        if (!memory) return false;
        auto* camera = reinterpret_cast<RE::NiCamera*>(construct(memory, nullptr, nullptr));
        if (!camera || camera != memory) return false;
        camera->IncRefCount();
        g_mirrorCaptureSourceCamera = camera;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

MirrorSyncResult PrepareMirrorCaptureSync(RE::NiAVObject* playerRoot) noexcept
{
    g_mirrorCaptureSync.Reset();
    g_mirrorCaptureSyncClone = {};
    g_mirrorCaptureSyncCameraGeneration = g_flatReflectionDriveSnapshotGeneration;
    if (REL::Module::IsVR() || MirrorSceneRenderer::LoadBlocked() ||
        !g_mirrorCaptureSyncCameraGeneration || !EnsureMirrorCaptureSourceCamera())
        return MirrorSyncResult::InvalidEpoch;

    const bool playerRequired = !g_driveOmitPlayerBody && RE::PlayerCharacter::GetSingleton();
    __try {
        if (playerRequired) {
            RE::NiAVObject* face = nullptr;
            RE::NiAVObject* head = nullptr;
            if (!ReadPrivatePlayerFaceRoots(RE::PlayerCharacter::GetSingleton(), face, head) ||
                !TryArmPrivatePlayerBodyRenderClone(playerRoot, face, head, &g_mirrorCaptureSyncClone))
                return MirrorSyncResult::CloneUnavailable;
            if (g_armedCompletedPlayerVisual) {
                const auto& clone = g_mirrorCaptureSyncClone;
                if (!ApplyCompletedPlayerCamera(clone, g_mirrorCaptureSourceCamera, g_mirrorCaptureEye))
                    return MirrorSyncResult::CameraUnavailable;

                if (!g_mirrorCaptureSync.Seal(GetRenderFrame(), clone.loadGeneration,
                    g_mirrorCaptureSyncCameraGeneration,
                    {reinterpret_cast<std::uintptr_t>(clone.root), clone.stateGeneration,
                        clone.poseSerial, clone.sourceUpdateSerial}, clone.sourceUpdateSerial, true))
                    return MirrorSyncResult::InvalidEpoch;
                return MirrorSyncResult::Ready;
            }
        }

        if (g_playerRetargetWaiters.load(std::memory_order_acquire) ||
            !TryAcquireSRWLockShared(&g_playerNaturalPoseLock))
            return MirrorSyncResult::NativeUpdateBusy;
        __try {
            if (g_playerRetargetWaiters.load(std::memory_order_acquire))
                return MirrorSyncResult::NativeUpdateBusy;
            const auto nativeSource = g_playerNativeUpdateSerial.load(std::memory_order_acquire);
            const auto& clone = g_mirrorCaptureSyncClone;
            if (playerRequired && clone.sourceUpdateSerial != nativeSource)
                return MirrorSyncResult::SourceMismatch;
            auto* pc = RE::PlayerCamera::GetSingleton();
            auto* cameraRoot = pc ? pc->cameraRoot.get() : nullptr;
            auto getWorldCamera = reinterpret_cast<RE::NiCamera* (*)()>(
                REL::Offset(ReflectionRuntime::Rva(kRVA_WorldRootCamera)).address());
            const auto* source = getWorldCamera();
            if (!cameraRoot || !source) return MirrorSyncResult::CameraUnavailable;
            const auto playerCameraWorld = cameraRoot->world;
            auto* destination = g_mirrorCaptureSourceCamera;
            destination->local = source->local;
            destination->world = source->world;
            destination->previousWorld = source->previousWorld;
            std::memcpy(destination->worldToCam, source->worldToCam, sizeof(destination->worldToCam));
            destination->viewFrustum = source->viewFrustum;
            destination->port = source->port;
            destination->minNearPlaneDist = source->minNearPlaneDist;
            destination->maxFarNearRatio = source->maxFarNearRatio;
            destination->lodAdjust = source->lodAdjust;
            if (!FinitePrivatePlayerTransform(playerCameraWorld) ||
                !FinitePrivatePlayerTransform(destination->world) ||
                !std::isfinite(destination->viewFrustum.far) || destination->viewFrustum.far <= 0.0f)
                return MirrorSyncResult::CameraUnavailable;

            if (std::memcmp(&playerCameraWorld, &cameraRoot->world, sizeof(playerCameraWorld)) ||
                std::memcmp(&destination->world, &source->world, sizeof(destination->world)) ||
                std::memcmp(&destination->viewFrustum, &source->viewFrustum, sizeof(destination->viewFrustum)))
                return MirrorSyncResult::CameraChanged;
            g_mirrorCaptureEye = playerCameraWorld.translate;
            if (!g_mirrorCaptureSync.Seal(GetRenderFrame(), MirrorSceneRenderer::LoadGeneration(),
                    g_mirrorCaptureSyncCameraGeneration,
                    {reinterpret_cast<std::uintptr_t>(clone.root), clone.stateGeneration,
                        clone.poseSerial, clone.sourceUpdateSerial}, nativeSource, playerRequired))
                return MirrorSyncResult::InvalidEpoch;
            return MirrorSyncResult::Ready;
        } __finally {
            ReleaseSRWLockShared(&g_playerNaturalPoseLock);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return MirrorSyncResult::InvalidEpoch;
    }
}

bool DriveMirror(float ex, float ey, float ez, DriveKind driveKind, std::uint8_t* mirrorExclusionRoot,
    RE::NiAVObject* pinnedPlayerBody, RE::NiAVObject* pinnedFirstPersonRoot,
    DriveDiag* diag, NativeHeroDriveContext* nativeHeroContext) noexcept;

bool DriveSynchronizedMirror(float ex, float ey, float ez, DriveKind driveKind,
    std::uint8_t* mirrorExclusionRoot, RE::NiAVObject* pinnedPlayerBody,
    RE::NiAVObject* pinnedFirstPersonRoot, DriveDiag* diag,
    NativeHeroDriveContext* nativeHeroContext) noexcept
{
    
    if (g_mirrorCaptureSync.Active() || g_armPrivatePlayerBodyRenderLease) {
        diag->syncInputsDeferred = 1;
        return true;
    }
    MirrorSyncResult result = MirrorSyncResult::InvalidEpoch;
    bool ok = true;
    __try {
        result = PrepareMirrorCaptureSync(pinnedPlayerBody);
        if (result == MirrorSyncResult::Ready)
            ok = DriveMirror(ex, ey, ez, driveKind, mirrorExclusionRoot, pinnedPlayerBody,
                pinnedFirstPersonRoot, diag, nativeHeroContext);
        else {

            diag->syncInputsDeferred = 1;

            g_privatePlayerBodyAdmissionBusy = result != MirrorSyncResult::InvalidEpoch;
            if (result == MirrorSyncResult::CloneUnavailable)
                diag->privatePlayerCloneUnavailable = 1;
        }
    } __finally {

        ReleasePrivatePlayerBodyRenderLeaseArmed();
        g_mirrorCaptureSync.Reset();
        g_mirrorCaptureSyncClone = {};
        g_mirrorCaptureSyncCameraGeneration = 0;
    }
    static std::array<std::uint64_t, static_cast<unsigned>(MirrorSyncResult::Count)> counts{};
    static std::uint64_t lastLog{};
    ++counts[static_cast<unsigned>(result)];
    const auto now = GetTickCount64();
    if (!lastLog || now - lastLog >= 5000u) {
        logger::info("[MirrorSync] capture input pairs: ready={} cloneUnavailable={} nativeBusy={} "
            "sourceMismatch={} cameraUnavailable={} cameraChanged={} invalidEpoch={}; "
            "unmatched captures retain the last complete image",
            counts[0], counts[1], counts[2], counts[3], counts[4], counts[5], counts[6]);
        lastLog = now;
        counts = {};
    }
    return ok;
}
#pragma pop_macro("far")

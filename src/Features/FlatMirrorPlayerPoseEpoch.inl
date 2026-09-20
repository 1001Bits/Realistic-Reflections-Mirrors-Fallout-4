

bool PrivatePlayerRenderClonePoseEpochMatchesCurrent(
    const PrivatePlayerRenderClone& clone) noexcept
{
    const auto* player = RE::PlayerCharacter::GetSingleton();
    const auto* resources = g_armPrivatePlayerBodyRenderLease ? ArmedPrivatePlayerResources() : nullptr;
    const MirrorPlayerPoseBatch::Pose pose{reinterpret_cast<std::uintptr_t>(clone.root),
        clone.stateGeneration, clone.poseSerial, clone.sourceUpdateSerial};
    const bool leaseMatches = resources && resources->cloneRoot.get() == clone.root &&
        resources->stateGeneration == clone.stateGeneration && resources->poseSerial == clone.poseSerial &&
        resources->sourceUpdateSerial == clone.sourceUpdateSerial;

    const bool sourceMatches = g_mirrorCaptureSync.Active() ?
        g_mirrorCaptureSync.Matches(GetRenderFrame(), clone.loadGeneration,
            g_mirrorCaptureSyncCameraGeneration, pose) :
        g_privatePlayerPoseBatch.Accepts(GetRenderFrame(), clone.loadGeneration, pose,
            g_playerNativeUpdateSerial.load(std::memory_order_acquire),
            g_playerPrivateUpdatePendingSource.load(std::memory_order_acquire));
    const bool matches = leaseMatches && player &&
        clone.poseFromThirdPerson == PlayerPoseFromThirdPersonGraph(player) &&
        PrivatePlayerPoseEpochReady(clone.loadGeneration, clone.stateGeneration, clone.privateUpdateSerial,
            clone.poseSerial, clone.sourceUpdateSerial, clone.projectHash, clone.skeletonOrderHash,
            clone.poseContentHash, clone.poseBoneCount, clone.sourceGeometryCount, clone.geometryCount) &&
        !MirrorSceneRenderer::LoadBlocked() && clone.loadGeneration == MirrorSceneRenderer::LoadGeneration() &&
        clone.stateGeneration == g_privatePlayerBodyGeneration.load(std::memory_order_acquire) && sourceMatches;
    if (!matches) {
        static unsigned reports = 0;
        if (++reports <= 8 || reports % 300 == 0)
            logger::info("[ReflectionPlayerBody] render epoch deferred: frame={} clone/current pose={}/{} source={}/{} generation={}/{} perspective={}/{} shown={} lease={}",
                GetRenderFrame(), clone.poseSerial, resources ? resources->poseSerial : 0,
                clone.sourceUpdateSerial, g_playerNativeUpdateSerial.load(std::memory_order_acquire),
                clone.stateGeneration, g_privatePlayerBodyGeneration.load(std::memory_order_acquire),
                clone.poseFromThirdPerson, player && PlayerPoseFromThirdPersonGraph(player),
                player && PlayerThirdPersonModelShown(player), g_armPrivatePlayerBodyRenderLease);
    }
    return matches;
}



void SelectCompletedPlayerPose() noexcept
{
    static std::uint32_t selectedFrame{};
    const auto frame = GetRenderFrame();
    auto current = g_completedPlayerPoses.load(std::memory_order_acquire);
    if (!current) {
        if (g_selectedPlayerPoses) g_selectedPlayerPoses->exchange.Release(g_selectedPlayerPoseSlot);
        g_selectedPlayerPoseSlot = MirrorPoseExchange::None;
        g_selectedPlayerPoses.reset();
        return;
    }
    if (current == g_selectedPlayerPoses && selectedFrame == frame) return;
    const int previous = current == g_selectedPlayerPoses ? g_selectedPlayerPoseSlot : MirrorPoseExchange::None;
    const int next = current->exchange.AcquireLatest(previous);
    if (next == MirrorPoseExchange::None) return;
    if (current != g_selectedPlayerPoses || next != previous) {
        if (g_selectedPlayerPoses) g_selectedPlayerPoses->exchange.Release(g_selectedPlayerPoseSlot);
        g_selectedPlayerPoses = std::move(current);
        g_selectedPlayerPoseSlot = next;
    }
    selectedFrame = frame;
}

bool TryArmCompletedPlayerPose(RE::NiAVObject* liveRoot, RE::NiAVObject* liveFaceRoot,
    RE::NiAVObject* liveHeadRoot, PrivatePlayerRenderClone* out) noexcept
{
    if (g_armPrivatePlayerBodyRenderLease) return false;
    SelectCompletedPlayerPose();
    if (!g_selectedPlayerPoses || g_selectedPlayerPoseSlot == MirrorPoseExchange::None) return false;
    __try {
        auto* resources = g_selectedPlayerPoses->slots[g_selectedPlayerPoseSlot].visual.get();
        auto* player = RE::PlayerCharacter::GetSingleton();
        const auto generation = g_privatePlayerBodyGeneration.load(std::memory_order_acquire);
        if (!player || !player->loadedData || player->loadedData->data3D.get() != liveRoot ||
            MirrorSceneRenderer::LoadBlocked() || resources->loadGeneration != MirrorSceneRenderer::LoadGeneration() ||
            resources->stateGeneration != generation || resources->player != player ||
            !g_privatePlayerBodyDemand.AcceptsPose(GetTickCount64(), resources->demandGeneration) ||
            resources->sourceThirdPersonRoot.get() != liveRoot || resources->sourceFaceRoot.get() != liveFaceRoot ||
            resources->sourceHeadRoot.get() != liveHeadRoot ||
            resources->poseFromThirdPerson != PlayerPoseFromThirdPersonGraph(player) ||
            !resources->cloneRoot || resources->cloneRoot->GetAppCulled() || !resources->cloneTree ||
            !PrivatePlayerPoseEpochReady(resources->loadGeneration, resources->stateGeneration,
                resources->updateSerial, resources->poseSerial, resources->sourceUpdateSerial,
                resources->projectHash, resources->skeletonOrderHash, resources->poseContentHash,
                resources->poseBoneCount, resources->sourceGeometryCount, resources->cloneGeometryCount)) return false;

        PrivatePlayerGeometryMaterialCensus sourceCensus{}, cloneCensus{};
        const bool sourceRead = ReadPrivatePlayerGeometryMaterialCensus(liveRoot, liveFaceRoot, liveHeadRoot, &sourceCensus);
        const bool cloneRead = sourceRead && ReadPrivatePlayerGeometryMaterialCensus(
            resources->cloneRoot.get(), resources->cloneFaceRoot.get(), resources->cloneHeadRoot.get(), &cloneCensus, resources);
        if (!sourceRead || !cloneRead ||
            !PrivatePlayerGeometryMaterialCensusesMatch(sourceCensus, cloneCensus) ||
            cloneCensus.semanticHash != resources->geometryMaterialHash ||
            cloneCensus.geometryCount != resources->cloneGeometryCount ||
            cloneCensus.materialGeometryCount != resources->materialGeometryCount) {

            static unsigned censusMismatchLogs{};
            if (censusMismatchLogs++ < 16u)
                logger::warn("[MirrorPlayerAnim] completed pose geometry changed: sourceRead={} cloneRead={} "
                    "geometry={}/{}/{} material={}/{}/{} hash={:016X}/{:016X}/{:016X} generation={} pose={}",
                    sourceRead, cloneRead, sourceCensus.geometryCount, cloneCensus.geometryCount, resources->cloneGeometryCount,
                    sourceCensus.materialGeometryCount, cloneCensus.materialGeometryCount, resources->materialGeometryCount,
                    sourceCensus.semanticHash, cloneCensus.semanticHash, resources->geometryMaterialHash,
                    resources->stateGeneration, resources->poseSerial);
            g_privatePlayerBodyGeneration.fetch_add(1, std::memory_order_acq_rel);
            RequestPrivatePlayerBodyTeardown();
            return false;
        }
        std::uint64_t hash{};
        if (!ReadPrivatePlayerPoseContentHashNoexcept(resources->cloneTree, resources->cloneRoot.get(),
            resources->poseBoneCount, &hash) || hash != resources->poseContentHash) return false;
        out->root = resources->cloneRoot.get();
        out->faceRoot = resources->cloneFaceRoot.get();
        out->headRoot = resources->cloneHeadRoot.get();
        out->loadGeneration = resources->loadGeneration;
        out->stateGeneration = resources->stateGeneration;
        out->privateUpdateSerial = resources->updateSerial;
        out->poseSerial = resources->poseSerial;
        out->sourceUpdateSerial = resources->sourceUpdateSerial;
        out->projectHash = resources->projectHash;
        out->skeletonOrderHash = resources->skeletonOrderHash;
        out->poseContentHash = resources->poseContentHash;
        out->geometryMaterialHash = resources->geometryMaterialHash;
        out->paneClearanceGeometry = resources->paneClearanceGeometry;
        out->paneClearanceGeometryCount = resources->paneClearanceGeometryCount;
        out->poseTree = resources->cloneTree;
        out->poseBoneCount = resources->poseBoneCount;
        out->materialGeometryCount = resources->materialGeometryCount;
        out->drawRequirements = resources->drawRequirements;

        out->sourceGeometryCount = cloneCensus.renderGeometryCount;
        out->geometryCount = cloneCensus.renderGeometryCount;
        out->poseFromThirdPerson = resources->poseFromThirdPerson;
        g_armedCompletedPlayerVisual = resources;
        g_armPrivatePlayerBodyRenderLease = true;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool ApplyCompletedPlayerCamera(const PrivatePlayerRenderClone& clone, RE::NiCamera* destination,
    RE::NiPoint3& eye) noexcept
{
    if (!g_armedCompletedPlayerVisual || !g_selectedPlayerPoses || !destination ||
        g_selectedPlayerPoseSlot == MirrorPoseExchange::None) return false;
    const auto& slot = g_selectedPlayerPoses->slots[g_selectedPlayerPoseSlot];
    if (slot.visual.get() != g_armedCompletedPlayerVisual || slot.visual->cloneRoot.get() != clone.root ||
        slot.visual->poseSerial != clone.poseSerial || slot.camera.source != clone.sourceUpdateSerial) return false;
    const auto& camera = slot.camera;
    destination->local = camera.local;
    destination->world = camera.world;
    destination->previousWorld = camera.previousWorld;
    std::memcpy(destination->worldToCam, camera.worldToCam, sizeof(camera.worldToCam));
    destination->viewFrustum = camera.frustum;
    destination->port = camera.port;
    destination->minNearPlaneDist = camera.minNear;
    destination->maxFarNearRatio = camera.maxRatio;
    destination->lodAdjust = camera.lod;
    eye = camera.eye;
    return true;
}

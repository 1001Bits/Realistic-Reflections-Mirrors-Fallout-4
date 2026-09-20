

bool CopyCompletedPlayerPose(PrivatePlayerBodyResources* source,CompletedPlayerPosePool::Slot& slot) noexcept
{
    __try {
        auto* visual=slot.visual.get();
        CompletedMirrorCamera camera;
        if(!ReadCompletedMirrorCamera(camera,source->sourceUpdateSerial)) return false;
        for(const auto& pair:slot.nodes) {
            pair.target->local=pair.source->local;
            pair.target->world=pair.source->world;
            pair.target->previousWorld=pair.source->previousWorld;
            NiVirtualDispatch::SetAppCulled(pair.target,pair.source->GetAppCulled());
        }
        for(std::uint32_t i=0;i<source->poseBoneCount;++i) {
            const auto* from=ResolveFlattenedBoneLocal(source->cloneTree,static_cast<std::int32_t>(i));
            auto* to=ResolveFlattenedBoneLocal(visual->cloneTree,static_cast<std::int32_t>(i));
            if(!from || !to || from==to || !FinitePrivatePlayerTransform(*from)) return false;
            *to=*from;
        }
        if(source->cloneFaceRoot) {
            MirrorFaceGenSnapshot face;
            if(!visual->cloneFaceRoot || !face.Read(source->privateFaceAnimation.get())) return false;
            face.Apply(visual->privateFaceAnimation.get(),
                *reinterpret_cast<std::uint16_t*>(reinterpret_cast<std::byte*>(visual->cloneFaceRoot.get())+0x17C));
        }
        SynchronizePrivatePlayerWeaponBoneCull(visual);
        const auto* previousFlash=visual->muzzleFlashCloneArt.get();
        SynchronizePrivatePlayerMuzzleFlash(visual);
        visual->transientPaneGeometryDirty |= previousFlash!=visual->muzzleFlashCloneArt.get();
        if(!SynchronizePrivatePlayerThrowables(visual)) return false;
        if(visual->transientPaneGeometryDirty) {
            if(!RefreshPrivatePlayerPaneGeometry(visual)) return false;
            visual->transientPaneGeometryDirty=false;
        }
        RE::NiUpdateData update{};
        NiVirtualDispatch::UpdateDownwardPass(visual->cloneRoot,update,0);
        std::uint64_t hash{};
        if(!ReadPrivatePlayerPoseContentHashNoexcept(visual->cloneTree,visual->cloneRoot.get(),source->poseBoneCount,&hash) ||
            hash!=source->poseContentHash) return false;
        visual->demandGeneration=source->demandGeneration;visual->stateGeneration=source->stateGeneration;
        visual->updateSerial=source->updateSerial;visual->poseSerial=source->poseSerial;
        visual->sourceUpdateSerial=source->sourceUpdateSerial;visual->poseContentHash=hash;
        visual->poseBoneCount=source->poseBoneCount;visual->poseFromThirdPerson=source->poseFromThirdPerson;
        slot.camera=camera;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
void PublishCompletedPlayerPose(PrivatePlayerBodyResources* source) noexcept
{
    if(!source->completedPoses) return;
    auto* pool=source->completedPoses.get();
    const int slot=pool->exchange.BeginWrite();
    if(slot==MirrorPoseExchange::None) return;
    bool complete=false;
    __try {complete=CopyCompletedPlayerPose(source,pool->slots[slot]);}
    __finally {pool->exchange.EndWrite(slot,complete);}
    if(!complete) {
        static std::atomic<unsigned> failures{};
        const auto n=failures.fetch_add(1,std::memory_order_relaxed)+1;
        if(n<=4 || n%600==0) logger::info("[ReflectionPlayerBody] completed-pose publication postponed; previous complete visual stays available ({})",n);
    }
}

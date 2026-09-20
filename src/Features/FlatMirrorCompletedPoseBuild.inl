

bool BuildCompletedPlayerPosePool(PrivatePlayerBodyResources* source) try
{
    if(REL::Module::IsVR()) return true;
    auto pool=std::shared_ptr<CompletedPlayerPosePool>(new CompletedPlayerPosePool,
        [](CompletedPlayerPosePool* value) noexcept {

            if(MirrorSceneRenderer::ShutdownRequested()) return;
            if(auto* tasks=F4SE::GetTaskInterface()) {
                try { tasks->AddTask([value] {delete value;}); } catch(...) {}
            }
        });
    for(auto& slot:pool->slots) {
        slot.visual=std::make_unique<PrivatePlayerBodyResources>();
        auto* visual=slot.visual.get();
        visual->player=source->player;visual->loadGeneration=source->loadGeneration;
        visual->sourceThirdPersonRoot=source->sourceThirdPersonRoot;
        visual->sourceFirstPersonRoot=source->sourceFirstPersonRoot;
        visual->sourceFaceRoot=source->sourceFaceRoot;visual->sourceHeadRoot=source->sourceHeadRoot;
        visual->projectHash=source->projectHash;visual->skeletonOrderHash=source->skeletonOrderHash;
        RE::NiCloningProcess cloning{};
        cloning.copyType=RE::NiCloningProcess::CopyType::kCopyUnique;
        cloning.scale={1,1,1};
        visual->cloneRoot.reset(static_cast<RE::NiAVObject*>(ClonePlayerVisualTree(source->cloneRoot.get(),&cloning)));
        if(!visual->cloneRoot || visual->cloneRoot==source->cloneRoot) return false;
        for(const auto& pair:cloning.cloneMap) {
            auto* from=netimmerse_cast<RE::NiAVObject*>(pair.first);
            auto* to=netimmerse_cast<RE::NiAVObject*>(pair.second);
            if(!from || !to) continue;
            if(from==to) return false;
            slot.nodes.push_back({from,to});
            if(from==source->cloneFaceRoot.get()) visual->cloneFaceRoot.reset(to);
            if(from==source->cloneHeadRoot.get()) visual->cloneHeadRoot.reset(to);

            NiVirtualDispatch::SetAppCulled(to,from->GetAppCulled());
        }
        if(slot.nodes.empty() || bool(visual->cloneFaceRoot) != bool(source->cloneFaceRoot) ||
            bool(visual->cloneHeadRoot) != bool(source->cloneHeadRoot) ||
            !IsolatePrivatePlayerFaceAnimation(visual)) return false;
        visual->cloneTree=FindFlattenedBoneTreeFlat(visual->cloneRoot.get());
        const auto count=FlattenedBoneTreeEntryCount(source->cloneTree);
        std::uint64_t order{};
        if(!count || count>kMaxPlayerNaturalPoseBones || FlattenedBoneTreeEntryCount(visual->cloneTree)!=count ||
            !HashFlattenedBoneTreeNameOrder(visual->cloneTree,count,order) || order!=source->skeletonOrderHash) return false;
        PrivatePlayerGeometryMaterialCensus census{};
        if(!ReadPrivatePlayerGeometryMaterialCensus(visual->cloneRoot.get(),visual->cloneFaceRoot.get(),
            visual->cloneHeadRoot.get(),&census,visual) || census.semanticHash!=source->geometryMaterialHash ||
            census.geometryCount!=source->cloneGeometryCount) return false;
        visual->geometryMaterialHash=census.semanticHash;
        visual->materialGeometryCount=census.materialGeometryCount;
        visual->drawRequirements=census.drawRequirements;
        visual->sourceGeometryCount=source->sourceGeometryCount;visual->cloneGeometryCount=census.geometryCount;
        for(std::uint32_t i=0;i<source->paneClearanceGeometryCount;++i) {
            const auto found=cloning.cloneMap.find(source->paneClearanceGeometry[i]);
            auto* geometry=found==cloning.cloneMap.end()?nullptr:netimmerse_cast<RE::BSGeometry*>(found->second);
            if(!geometry || visual->paneClearanceGeometryCount==visual->paneClearanceGeometry.size()) return false;
            visual->paneClearanceGeometry[visual->paneClearanceGeometryCount++]=geometry;
        }
    }
    source->completedPoses=std::move(pool);
    logger::info("[ReflectionPlayerBody] three completed-pose visual slots prepared; capture does not lease the animation graph");
    return true;
} catch(...) {return false;}

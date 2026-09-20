

template<class T> T ReadMirrorClothField(const void* object, std::size_t offset) noexcept
{
    T value{};
    std::memcpy(&value, static_cast<const std::byte*>(object)+offset, sizeof(T));
    return value;
}

MirrorClothPose::Transform ReadMirrorClothTransform(const RE::NiTransform& value) noexcept
{
    MirrorClothPose::Transform out;
    for (unsigned i=0;i<3;++i) for (unsigned j=0;j<3;++j) out.r[i][j]=value.rotate.entry[i][j];
    out.p[0]=value.translate.x; out.p[1]=value.translate.y; out.p[2]=value.translate.z;
    out.scale=value.scale;
    return out;
}

RE::NiTransform WriteMirrorClothTransform(const MirrorClothPose::Transform& value) noexcept
{
    RE::NiTransform out{};
    for (unsigned i=0;i<3;++i) for (unsigned j=0;j<3;++j) out.rotate.entry[i][j]=value.r[i][j];
    out.translate={value.p[0],value.p[1],value.p[2]}; out.scale=value.scale;
    return out;
}

void CollectMirrorClothTransformSet(PrivatePlayerBodyResources* resources,
    RE::NiCloningProcess& cloning, const void* transformSet, std::uintptr_t expectedVtable)
{
    if (!transformSet || ReadMirrorClothField<std::uintptr_t>(transformSet,0)!=expectedVtable) return;
    const auto* skeleton=ReadMirrorClothField<const void*>(transformSet,0x30);
    const auto count=ReadMirrorClothField<std::uint32_t>(transformSet,0x98);
    const auto* nodes=ReadMirrorClothField<RE::NiAVObject* const*>(transformSet,0x88);
    const auto* written=ReadMirrorClothField<const std::uint32_t*>(transformSet,0x40);
    const auto wordCount=ReadMirrorClothField<std::uint32_t>(transformSet,0x48);
    const auto bitCount=ReadMirrorClothField<std::uint32_t>(transformSet,0x50);
    const auto* deformations=ReadMirrorClothField<const std::byte*>(transformSet,0xE8);
    const auto deformationCount=ReadMirrorClothField<std::uint32_t>(transformSet,0xF8);
    if (!skeleton || !nodes || !written || !deformations || count==0 || count>MirrorClothPose::MaxBones ||
        deformationCount>MirrorClothPose::MaxBones || bitCount!=count || wordCount<(count+31)/32 ||
        wordCount>(MirrorClothPose::MaxBones+31)/32 ||
        ReadMirrorClothField<std::uint32_t>(skeleton,0x20)!=count ||
        ReadMirrorClothField<std::uint32_t>(skeleton,0x30)!=count ||
        ReadMirrorClothField<std::uint32_t>(skeleton,0x40)!=count) return;
    const auto* parents=ReadMirrorClothField<const std::int16_t*>(skeleton,0x18);
    const auto* reference=ReadMirrorClothField<const MirrorClothPose::ReferenceTransform*>(skeleton,0x38);
    if (!parents || !reference) return;
    std::array<MirrorClothPose::Transform,MirrorClothPose::MaxBones> model;
    if (!MirrorClothPose::ModelPose({parents,count},{reference,count},{model.data(),count})) return;

    for (std::uint32_t d=0;d<deformationCount;++d) {
        const auto index=ReadMirrorClothField<std::uint32_t>(deformations+d*0x40,0x30);
        if (index>=count || !(written[index/32] & (1u<<(index%32)))) continue;
        auto* source=nodes[index];
        if (!source || source==resources->sourceThirdPersonRoot.get() || source==resources->sourceHeadRoot.get() ||
            source==resources->sourceFaceRoot.get() || !source->parent) continue;
        const auto found=cloning.cloneMap.find(source);
        auto* target=found==cloning.cloneMap.end()?nullptr:netimmerse_cast<RE::NiAVObject*>(found->second);
        if (!target || target==source || !target->parent) continue;
        const auto parent=cloning.cloneMap.find(source->parent);
        if (parent==cloning.cloneMap.end() || parent->second!=target->parent) continue;

        MirrorClothPose::Transform parentFromAnchor;
        auto* ancestor=source->parent;
        std::int32_t anchor=-1;
        for (unsigned depth=0;ancestor && depth<32;++depth) {
            for (std::uint32_t n=0;n<count;++n) if (nodes[n]==ancestor) {anchor=static_cast<std::int32_t>(n);break;}
            if (anchor>=0) break;
            const auto local=ReadMirrorClothTransform(ancestor->local);
            if (!MirrorClothPose::Finite(local)) break;
            parentFromAnchor=MirrorClothPose::Compose(local,parentFromAnchor);
            ancestor=ancestor->parent;
        }
        if (anchor<0 || anchor==static_cast<std::int32_t>(index)) continue;
        const auto authoredFromAnchor=MirrorClothPose::Relative(model[anchor],model[index]);
        const auto local=MirrorClothPose::Relative(parentFromAnchor,authoredFromAnchor);
        if (!MirrorClothPose::Finite(local)) continue;
        bool duplicate=false;
        for (const auto& entry:resources->clothPose) if (entry.target.get()==target) {duplicate=true;break;}
        if (duplicate) continue;
        if (resources->clothPose.size()==MirrorClothPose::MaxBones) return;
        PrivatePlayerClothPose binding;
        binding.source.reset(source); binding.target.reset(target);
        binding.sourceParent.reset(source->parent); binding.targetParent.reset(target->parent);
        binding.referenceLocal=WriteMirrorClothTransform(local);
        resources->clothPose.push_back(std::move(binding));
    }
}

void BuildMirrorClothPoseUnsafe(PrivatePlayerBodyResources* resources, RE::NiCloningProcess& cloning)
{
    if (!resources || REL::Module::IsVR() || !ReflectionRuntime::Supported() ||
        !resources->player || !resources->player->currentProcess || !resources->player->currentProcess->middleHigh) return;
    const auto version=REL::Module::get().version();
    const bool ae=version==REL::Version{1,11,240,0};
    if (!ae && version!=REL::Version{1,10,163,0}) return;
    const auto extraVtable=REL::Offset(ae?0x26B20F0u:0x2E42E48u).address();
    const auto setVtable=REL::Offset(ae?0x26B24E0u:0x2E43128u).address();
    const auto& cache=resources->player->currentProcess->middleHigh->clothExtraDataCache;
    if (cache.size()>64) return;
    std::array<const void*,128> seen{};
    std::uint32_t seenCount=0;
    for (const auto* cloth:cache) {
        if (!cloth || ReadMirrorClothField<std::uintptr_t>(cloth,0)!=extraVtable) continue;
        const auto* instances=ReadMirrorClothField<const void* const*>(cloth,0x60);
        const auto instanceCount=ReadMirrorClothField<std::uint32_t>(cloth,0x70);
        if (!instances || instanceCount>32) continue;
        for (std::uint32_t i=0;i<instanceCount;++i) {
            if (!instances[i]) continue;
            const auto* sets=ReadMirrorClothField<const void* const*>(instances[i],0x40);
            const auto setCount=ReadMirrorClothField<std::uint32_t>(instances[i],0x48);
            if (!sets || setCount>8) continue;
            for (std::uint32_t s=0;s<setCount;++s) {
                bool duplicate=false;
                for (std::uint32_t n=0;n<seenCount;++n) if (seen[n]==sets[s]) {duplicate=true;break;}
                if (duplicate) continue;
                if (seenCount==seen.size()) return;
                seen[seenCount++]=sets[s];
                CollectMirrorClothTransformSet(resources,cloning,sets[s],setVtable);
            }
        }
    }
}

bool BuildMirrorClothPoseGuarded(PrivatePlayerBodyResources* resources, RE::NiCloningProcess& cloning)
{
    __try {BuildMirrorClothPoseUnsafe(resources,cloning); return true;}
    __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
void BuildMirrorClothPose(PrivatePlayerBodyResources* resources, RE::NiCloningProcess& cloning) noexcept
{
    bool ready=false;
    try {ready=BuildMirrorClothPoseGuarded(resources,cloning);} catch (...) {}
    if (!ready) resources->clothPose.clear();
    static std::uint32_t logs=0;
    if (logs++<12) logger::info("[MirrorClothPose] authored cloth bindings={} ready={}; first person uses rest shape, third person copies native deformation",
        resources->clothPose.size(),ready);
}

void ApplyMirrorClothPose(PrivatePlayerBodyResources* resources, bool nativeThirdPerson) noexcept
{
    if (resources->clothPose.empty()) return;
    __try {
        const auto applied=MirrorClothPose::Apply(resources->clothPose,nativeThirdPerson,FinitePrivatePlayerTransform);
        if (applied) {
            RE::NiUpdateData update{};
            NiVirtualDispatch::UpdateDownwardPass(resources->cloneRoot,update,0);
        }
        const auto mode=nativeThirdPerson?1u:0u;
        if (resources->clothPoseMode!=mode) {
            resources->clothPoseMode=mode;
            if (resources->clothPoseLogs++<8) logger::info("[MirrorClothPose] mode={} changed={} bindings={}",
                nativeThirdPerson?"native-third-person":"first-person-rest",applied,resources->clothPose.size());
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

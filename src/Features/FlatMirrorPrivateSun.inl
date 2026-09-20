

struct PrivateSunState
{
    MirrorPrivateSun::DepthTarget wide, detail, playerMask;
    Microsoft::WRL::ComPtr<ID3D11Buffer> constants;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler;
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> rasterizer;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilState> depthState;
    ID3D11Device* device{};
    RE::NiCamera* camera{}; 
    alignas(16) std::uint8_t groupBytes[0x170]{};
    void* group{};
    std::uint32_t loadGeneration{},maskWidth{},maskHeight{};
    MirrorPrivateSun::Epoch publishedEpoch{};
    std::array<MirrorPrivateSun::Publication,2> maps{};
    MirrorShadowCasterVolume requestCasterVolume{};
    MirrorShadowCasterSet wideCasterSet,requestCasterSet;
    MirrorShadowBatchPlanner casterPlanner;
    MirrorPrivateSun::DepthTarget* target{};
    std::uint8_t* mainAccumulator{};
    RE::NiCamera* restoreCamera{};
    void* previousAccumulator{};
    void* previousCullCamera{};
    std::uint64_t previousTESFlags{};
    std::uint32_t previousMode{};
    void (*setMode)(std::uint32_t){};
    bool active{}, complete{}, cubeWasActive{};
    bool smallObjects{true};
    std::array<ID3D11RenderTargetView*,8> previousRTV{};
    std::array<ID3D11ShaderResourceView*,3> previousPS{},previousCS{};
    ID3D11DepthStencilView* previousDSV{};
    ID3D11RasterizerState* previousRS{};
    ID3D11DepthStencilState* previousDS{};
    UINT previousStencil{}, viewportCount{}, scissorCount{};
    std::array<D3D11_VIEWPORT,16> viewports{};
    std::array<D3D11_RECT,16> scissors{};
    MirrorPrivateSun::Constants published{};
    const char* failureStage{"not-started"};
    unsigned mapIndex{}, casterVisits{}, casterCount{};
    const char* casterFailure{"none"};
    const void* casterNode{};
    unsigned casterDepth{},casterChildSlots{};
    MirrorPrivateSun::Traversal::Storage casterPending; 
    std::vector<RE::NiPointer<RE::NiAVObject>> casterPins;
};
PrivateSunState g_privateSun;

bool RetainPrivateSunCaster(const void* node) noexcept
{

    try {
        g_privateSun.casterPins.emplace_back(static_cast<RE::NiAVObject*>(const_cast<void*>(node)));
        return true;
    } catch(...) { return false; }
}

bool PrivateSunLocationAllowed(bool outdoorsOnly) noexcept
{
    if(!outdoorsOnly) return true;

    __try {
        auto* player=RE::PlayerCharacter::GetSingleton();
        auto* cell=player ? player->GetParentCell() : nullptr;
        if(!cell || !cell->IsExterior()) return false;
        const auto position=player->GetPosition();
        return !MirrorSunLocation::InPrewarPlayerHouse(cell->GetFormID(), position.x, position.y, position.z);
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

std::atomic<unsigned> g_privateSunCasterCount{0};

std::atomic<float> g_privateSunLightLevel{-1.0f};

bool PrivateSunBrightEnough() noexcept
{
    const auto minimum = MirrorSettings::MinimumSunLight();
    const float level = g_privateSunLightLevel.load(std::memory_order_acquire);
    if (level == 0.0f) return false;
    if (minimum == 0u) return true;
    
    if (!(level >= 0.0f)) return true;
    return level * 100.0f >= float(minimum);
}

bool MirrorSunShadowsRequired() noexcept
{
    if (!MirrorSettings::ShadowRenderingEnabled() || !PrivateSunBrightEnough()) return false;
    return MirrorSettings::ShadowsEnabled() ||
        PrivateSunLocationAllowed(MirrorSettings::SunlightShadowsOutdoorsOnly());
}

MirrorSceneRange::Sphere OutdoorMirrorRange() noexcept
{
    const auto distance = MirrorSettings::OutdoorDistance();
    if (!distance || !PrivateSunLocationAllowed(true)) return {};
    __try {

        const auto& receiverRoute=ActivePlanarMirrorRoute(g_activeDriveKind);
        if (!receiverRoute.valid) return {};
        const auto& p=receiverRoute.center;
        const MirrorSceneRange::Sphere range{p.x,p.y,p.z,static_cast<float>(distance)};
        return range.Active() ? range : MirrorSceneRange::Sphere{};
    } __except(EXCEPTION_EXECUTE_HANDLER) { return {}; }
}

bool PrivateSunLightingAvailable() noexcept
{
    const auto& state = g_privateSun;
    const auto& epoch = state.publishedEpoch;
    return state.complete && state.published.contract.y &&
        g_privateSunCasterCount.load(std::memory_order_acquire) != 0u &&
        MirrorSunShadowsRequired() && !MirrorSceneRenderer::LoadBlocked() &&
        epoch.smallObjects == MirrorSettings::SmallObjectShadowsEnabled() &&
        epoch.lowShadowQuality == MirrorSettings::LowShadowQuality() &&
        epoch.wideResolution == MirrorSettings::ShadowResolution() &&
        epoch.detailResolution == MirrorSettings::ShadowDetailResolution() &&
        epoch.playerSunlightShadows == (MirrorSettings::PlayerSunlightShadowsEnabled() && MirrorSettings::PlayerShadowsEnabled()) &&
        epoch.outdoorsOnly == MirrorSettings::SunlightShadowsOutdoorsOnly() &&
        PrivateSunLocationAllowed(epoch.outdoorsOnly) &&
        epoch.range[3] == OutdoorMirrorRange().radius &&
        epoch.frame == GetRenderFrame() &&
        epoch.cameraGeneration == g_flatReflectionDriveSnapshotGeneration &&
        epoch.loadGeneration == MirrorSceneRenderer::LoadGeneration();
}

MirrorPrivateSun::Bindings PrivateSunBindings() noexcept
{
    auto& state=g_privateSun;
    const bool outdoorsOnly=MirrorSettings::SunlightShadowsOutdoorsOnly();
    if(!MirrorSunShadowsRequired() || state.publishedEpoch.smallObjects!=MirrorSettings::SmallObjectShadowsEnabled() ||
        state.publishedEpoch.lowShadowQuality!=MirrorSettings::LowShadowQuality() ||
        state.publishedEpoch.wideResolution!=MirrorSettings::ShadowResolution() ||
        state.publishedEpoch.detailResolution!=MirrorSettings::ShadowDetailResolution() ||
        state.publishedEpoch.playerSunlightShadows!=(MirrorSettings::PlayerSunlightShadowsEnabled() && MirrorSettings::PlayerShadowsEnabled()) ||
        state.publishedEpoch.outdoorsOnly!=outdoorsOnly || !PrivateSunLocationAllowed(outdoorsOnly) ||
        (state.publishedEpoch.range[3]>0)!=(OutdoorMirrorRange().radius>0) ||
        !state.complete || state.active || state.device!=globals::d3d::device ||
        state.publishedEpoch.frame!=GetRenderFrame() ||
        state.publishedEpoch.cameraGeneration!=g_flatReflectionDriveSnapshotGeneration ||
        state.publishedEpoch.loadGeneration!=MirrorSceneRenderer::LoadGeneration()) return {};
    return {state.constants.Get(),{state.wide.srv.Get(),state.detail.srv.Get(),
        state.published.contract.w ? state.playerMask.srv.Get() : nullptr},state.sampler.Get()};
}

bool BindPrivateSunDepth() noexcept
{
    auto& state=g_privateSun;
    if(!state.active || !state.target || !globals::d3d::context) return false;
    auto* context=globals::d3d::context;
    state.target->Bind(context);
    if(state.target==&state.playerMask) {
        const D3D11_VIEWPORT viewport{0,0,float(state.maskWidth),float(state.maskHeight),0,1};
        const D3D11_RECT scissor{0,0,LONG(state.maskWidth),LONG(state.maskHeight)};
        context->RSSetViewports(1,&viewport);context->RSSetScissorRects(1,&scissor);
    }

    context->RSSetState(state.rasterizer.Get());
    context->OMSetDepthStencilState(state.depthState.Get(),0);
    return true;
}

void RestorePrivateSunDepth() noexcept
{
    auto& sun=g_privateSun;
    if(!sun.active) {sun.casterPending.clear();sun.casterPins.clear();return;}

    __try {
        RestorePrivatePassPoolLeaseArmed();
        FinishMaterialAccumulatorPassesArmed();
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        PermanentlyQuarantineMaterialAccumulator();
        g_faulted=true;sun.complete=false;
    }
    RestorePrivateAppCullOverrides();
    DisarmMaterialPlayerFlattenedPassCleanup();
    g_suppressReflectionMTA=false;
    g_armMaterialAccumulator=sun.mainAccumulator;
    sun.mainAccumulator=nullptr;
    sun.active=false;
    sun.target=nullptr;
    g_mirrorRenderActive=sun.cubeWasActive;
    __try {
        if(sun.setMode) sun.setMode(sun.previousMode);
        if(g_armStateObj) *reinterpret_cast<std::uint64_t*>(g_armStateObj+0x108)=sun.previousTESFlags;
        if(g_armCullCamPtr) *g_armCullCamPtr=sun.previousCullCamera;
        reinterpret_cast<SetCurrentAccumulator_t>(REL::Offset(ReflectionRuntime::Rva(kRVA_SetCurrentAccumulator)).address())(sun.previousAccumulator);
        if(sun.restoreCamera) {
            reinterpret_cast<SetCameraData_t>(REL::Offset(ReflectionRuntime::Rva(kRVA_State_SetCameraData)).address())(
                reinterpret_cast<void*>(REL::Offset(ReflectionRuntime::Rva(kRVA_GfxState)).address()),sun.restoreCamera,false,0,1);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) { g_faulted=true; sun.complete=false; }
    auto* context=globals::d3d::context;
    if(context) {
        context->OMSetRenderTargets(8,sun.previousRTV.data(),sun.previousDSV);
        context->RSSetViewports(sun.viewportCount,sun.viewports.data());
        context->RSSetScissorRects(sun.scissorCount,sun.scissors.data());
        context->RSSetState(sun.previousRS);
        context->OMSetDepthStencilState(sun.previousDS,sun.previousStencil);
        context->PSSetShaderResources(MirrorPrivateSun::TextureSlot,3,sun.previousPS.data());
        context->CSSetShaderResources(MirrorPrivateSun::TextureSlot,3,sun.previousCS.data());
    }
    for(auto*& view:sun.previousRTV) {if(view) view->Release();view=nullptr;}
    for(auto*& view:sun.previousPS) {if(view) view->Release();view=nullptr;}
    for(auto*& view:sun.previousCS) {if(view) view->Release();view=nullptr;}
    if(sun.previousDSV) sun.previousDSV->Release(); sun.previousDSV=nullptr;
    if(sun.previousRS) sun.previousRS->Release(); sun.previousRS=nullptr;
    if(sun.previousDS) sun.previousDS->Release(); sun.previousDS=nullptr;
    ReleaseCullList();
    sun.casterPending.clear();sun.casterPins.clear();
}

bool PreparePrivateSunResources(unsigned width,unsigned height,bool playerDetail,bool playerMask)
{
    auto& sun=g_privateSun; auto* device=globals::d3d::device;
    if(!device || sun.active || ReflectionRuntime::IsVR()) return false;
    const auto generation=MirrorSceneRenderer::LoadGeneration();
    if(sun.loadGeneration!=generation) {

        sun.group=nullptr;sun.complete=false;sun.loadGeneration=generation;
        for(auto& map:sun.maps) map.Invalidate();
    }
    if(sun.device!=device) {
        sun.constants.Reset(); sun.sampler.Reset(); sun.rasterizer.Reset(); sun.depthState.Reset();
        sun.complete=false; sun.device=device;
        for(auto& map:sun.maps) map.Invalidate();
    }
    const auto wideResolution=MirrorSettings::ShadowResolution();
    const auto detailResolution=MirrorSettings::ShadowDetailResolution();
    if(!sun.wide.Create(device,wideResolution,wideResolution) ||
       (playerDetail && !sun.detail.Create(device,detailResolution,detailResolution)) ||
       (playerMask && !sun.playerMask.Create(device,(std::max)(width,sun.playerMask.width),(std::max)(height,sun.playerMask.height)))) return false;
    sun.maskWidth=width;sun.maskHeight=height;
    if(!sun.constants) {
        D3D11_BUFFER_DESC desc{};desc.ByteWidth=sizeof(MirrorPrivateSun::Constants);
        desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        if(FAILED(device->CreateBuffer(&desc,nullptr,&sun.constants))) return false;
    }
    if(!sun.sampler) {
        D3D11_SAMPLER_DESC desc{};desc.Filter=D3D11_FILTER_COMPARISON_MIN_MAG_MIP_POINT;
        desc.AddressU=desc.AddressV=desc.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;
        desc.ComparisonFunc=D3D11_COMPARISON_LESS_EQUAL;desc.MaxLOD=D3D11_FLOAT32_MAX;
        if(FAILED(device->CreateSamplerState(&desc,&sun.sampler))) return false;
    }
    if(!sun.rasterizer) {
        D3D11_RASTERIZER_DESC desc{};desc.FillMode=D3D11_FILL_SOLID;desc.CullMode=D3D11_CULL_NONE;
        desc.DepthClipEnable=TRUE;desc.ScissorEnable=TRUE;
        if(FAILED(device->CreateRasterizerState(&desc,&sun.rasterizer))) return false;
    }
    if(!sun.depthState) {
        D3D11_DEPTH_STENCIL_DESC desc{};desc.DepthEnable=TRUE;desc.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ALL;
        desc.DepthFunc=D3D11_COMPARISON_LESS_EQUAL;
        if(FAILED(device->CreateDepthStencilState(&desc,&sun.depthState))) return false;
    }
    if(!sun.camera) {
        auto allocate=reinterpret_cast<NiAVObject_new_t>(REL::Offset(ReflectionRuntime::Rva(kRVA_NiAVObject_new)).address());
        auto construct=reinterpret_cast<NativeCameraCtor_t>(REL::Offset(ReflectionRuntime::Rva(kRVA_NativeCameraCtor)).address());
        void* memory=allocate(0x1e0);
        if(!memory) return false;
        auto* camera=static_cast<RE::NiCamera*>(construct(memory,nullptr,nullptr));
        if(camera!=memory) return false;
        camera->IncRefCount();sun.camera=camera;
    }
    if(!sun.group) {
        struct {void* pad;void* data;} array{nullptr,sun.groupBytes};
        if(ReflectionRuntime::IsPort240()) ConstructCullingGroupsDefault240(&array,0,1);
        else reinterpret_cast<ConstructDefault_t>(REL::Offset(ReflectionRuntime::Rva(kRVA_Group_ConstructDefault)).address())(&array,0,1);
        sun.group=sun.groupBytes;
    }
    return true;
}

bool SetPrivateSunCamera(MirrorPrivateSun::View& fit)
{
    auto* camera=g_privateSun.camera;
    const auto rows=MirrorPrivateSun::CameraRows(fit);
    for(unsigned i=0;i<rows.size();++i)
        camera->local.rotate.entry[i]={rows[i].x,rows[i].y,rows[i].z,rows[i].w};
    camera->local.translate={fit.eye.x,fit.eye.y,fit.eye.z};camera->local.scale=1;
    const float limits[]{1,MirrorPrivateSun::Far,0,1,1,0,1};
    std::memcpy(reinterpret_cast<std::byte*>(camera)+0x17c,limits,sizeof(limits));
    const RE::NiFrustum frustum{-fit.extent,fit.extent,fit.extent,-fit.extent,
        MirrorPrivateSun::Near,MirrorPrivateSun::Far,true};
    reinterpret_cast<void (*)(RE::NiCamera*,const RE::NiFrustum*)>(
        ReflectionRuntime::Address(ReflectionRuntime::Get()->cameraSetViewFrustum))(camera,&frustum);
    RE::NiUpdateData update{};
    NiVirtualDispatch::UpdateWorldData(camera,&update);
    DirectX::XMFLOAT4X4 native{};DirectX::XMFLOAT3 origin{};
    if(!PlanarMirrors::GetUnclippedCameraView(camera,native,origin)) return false;

    using namespace DirectX;
    const auto texture=XMMatrixSet(.5f,0,0,0, 0,-.5f,0,0, 0,0,1,0, .5f,.5f,0,1);
    XMStoreFloat4x4(&fit.worldToTexture,XMMatrixTranslation(-origin.x,-origin.y,-origin.z)*XMLoadFloat4x4(&native)*texture);
    fit.relativeViewProjection=native;fit.eye=origin;
    return MirrorShadowMaps::Finite(fit.worldToTexture);
}

bool ArmPrivateSunDepth(RE::NiCamera* camera,RE::NiCamera* restoreCamera,
    void* scene,MirrorPrivateSun::DepthTarget& target,bool reflectedMask)
{
    auto& sun=g_privateSun;auto* context=globals::d3d::context;
    sun.failureStage="depth-entry";
    if(sun.active || g_armMaterialPasses || !g_armMaterialAccumulator || !context || !ReflectionPassPoolsIdle()) return false;
    const auto base=REL::Module::get().base();const bool port=ReflectionRuntime::IsPort240();
    auto* setModeBytes=reinterpret_cast<const std::uint8_t*>(base+(port?0x2182650:0x27D63C0));
    constexpr std::array<std::uint8_t,23> ogModeEntry{0x8b,0xc1,0x48,0x8d,0x15,0x37,0x9c,0x82,0xfd,
        0x89,0x0d,0xe9,0xb6,0xf4,0x03,0x48,0x8d,0x0c,0xc5,0,0,0,0};
    constexpr std::array<std::uint8_t,23> portModeEntry{0x8b,0xc1,0x48,0x8d,0x15,0xa7,0xd9,0xe7,0xfd,
        0x89,0x0d,0x89,0xb8,0xcd,0x01,0x48,0x8d,0x0c,0xc5,0,0,0,0};
    const auto& entry=port?portModeEntry:ogModeEntry;
    sun.failureStage="mode-entry";
    if(std::memcmp(setModeBytes,entry.data(),entry.size())!=0) return false;
    const auto modeAddress=reinterpret_cast<std::uintptr_t>(setModeBytes)+15+
        *reinterpret_cast<const std::int32_t*>(setModeBytes+11);
    sun.previousMode=*reinterpret_cast<const std::uint32_t*>(modeAddress);
    sun.failureStage="previous-mode";
    if(sun.previousMode>=40) return false; 
    sun.setMode=reinterpret_cast<void (*)(std::uint32_t)>(const_cast<std::uint8_t*>(setModeBytes));
    sun.mainAccumulator=g_armMaterialAccumulator;sun.restoreCamera=restoreCamera;
    sun.previousAccumulator=*reinterpret_cast<void**>(REL::Offset(ReflectionRuntime::Rva(kRVA_CurrentAccumulator)).address());
    sun.previousCullCamera=g_armCullCamPtr?*g_armCullCamPtr:nullptr;
    sun.previousTESFlags=g_armStateObj?*reinterpret_cast<std::uint64_t*>(g_armStateObj+0x108):0;
    context->OMGetRenderTargets(8,sun.previousRTV.data(),&sun.previousDSV);
    context->PSGetShaderResources(MirrorPrivateSun::TextureSlot,3,sun.previousPS.data());
    context->CSGetShaderResources(MirrorPrivateSun::TextureSlot,3,sun.previousCS.data());
    context->RSGetState(&sun.previousRS);context->OMGetDepthStencilState(&sun.previousDS,&sun.previousStencil);
    sun.viewportCount=sun.scissorCount=16;
    context->RSGetViewports(&sun.viewportCount,sun.viewports.data());context->RSGetScissorRects(&sun.scissorCount,sun.scissors.data());
    sun.cubeWasActive=g_mirrorRenderActive;sun.active=true;sun.target=&target;g_mirrorRenderActive=true;
    auto* accumulator=*reinterpret_cast<std::uint8_t**>(reinterpret_cast<std::uint8_t*>(sun.camera)+kCubeCam_Accumulator);
    sun.failureStage="depth-accumulator";
    if(!accumulator) return false;
    g_armMaterialAccumulator=accumulator;
    *reinterpret_cast<void**>(accumulator+0x558)=scene;
    *reinterpret_cast<std::uint32_t*>(accumulator+0x560)=0x10;
    *reinterpret_cast<std::uint32_t*>(accumulator+0x580)=0;
    *(accumulator+0xb1)=0;
    *reinterpret_cast<RE::NiPoint3*>(accumulator+0x570)=camera->world.translate;
    if(g_armStateObj && !reflectedMask) *reinterpret_cast<std::uint64_t*>(g_armStateObj+0x108)&=~std::uint64_t{0x800};
    if(g_armCullCamPtr) *g_armCullCamPtr=camera;
    sun.setMode(0x10);
    reinterpret_cast<Group2_t>(REL::Offset(ReflectionRuntime::Rva(kRVA_Accumulator_SetCamera)).address())(accumulator,camera);
    reinterpret_cast<SetCurrentAccumulator_t>(REL::Offset(ReflectionRuntime::Rva(kRVA_SetCurrentAccumulator)).address())(accumulator);
    reinterpret_cast<SetCameraData_t>(REL::Offset(ReflectionRuntime::Rva(kRVA_State_SetCameraData)).address())(
        reinterpret_cast<void*>(REL::Offset(ReflectionRuntime::Rva(kRVA_GfxState)).address()),camera,false,0,1);
    ID3D11ShaderResourceView* empty[3]{};
    context->PSSetShaderResources(MirrorPrivateSun::TextureSlot,3,empty);
    context->CSSetShaderResources(MirrorPrivateSun::TextureSlot,3,empty);
    BindPrivateSunDepth();context->ClearDepthStencilView(target.dsv.Get(),D3D11_CLEAR_DEPTH,1,0);
    return true;
}

bool ClearPrivateSunDepthPasses(std::uintptr_t clearAddress,bool player) noexcept
{
    __try {
        auto clear=reinterpret_cast<void (*)(RE::NiAVObject*)>(clearAddress);
        for(unsigned i=0;i<g_privateGroupSubmittedRootCount;++i) clear(g_privateGroupSubmittedRoots[i]);
        if(player) for(unsigned i=0;i<g_armMaterialPlayerFlattenedPropertyCount;++i)
            clear(g_armMaterialPlayerFlattenedRepresentatives[i]);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        PermanentlyQuarantineMaterialAccumulator();g_faulted=true;return false;
    }
}

bool RenderPrivateSunDepth(RE::NiCamera* camera,RE::NiCamera* reflectedCamera,void* scene,
    const PrivatePlayerRenderClone& clone,RE::NiAVObject* player,
    MirrorPrivateSun::DepthTarget& target,bool playerOnly)
{
    auto& sun=g_privateSun;
    {
        MirrorPerformance::Sample timing(MirrorPerformance::Stage::FlatSunArm,sun.mapIndex);
        if(!ArmPrivateSunDepth(camera,reflectedCamera,scene,target,playerOnly)) return false;
    }
    MirrorPerformance::Sample submitTiming(MirrorPerformance::Stage::FlatSunSubmit,sun.mapIndex);
    sun.failureStage="caster-submission";
    auto reset=reinterpret_cast<Group1_t>(REL::Offset(ReflectionRuntime::Rva(kRVA_Group_Reset)).address());
    auto start=reinterpret_cast<Group2_t>(REL::Offset(ReflectionRuntime::Rva(kRVA_Group_StartAdding)).address());
    auto add=reinterpret_cast<GroupAdd_t>(REL::Offset(ReflectionRuntime::Rva(kRVA_Group_Add)).address());
    reset(sun.group);ResetSubmittedNodes();

    MirrorPrivateSun::ConfigureFlatShadowGroup(sun.groupBytes);
    start(sun.group,camera);
    if(!playerOnly) for(auto& entry:g_cullList) {
        if(!MarkSubmittedNode(entry.node)) continue;
        if(!ArmPrivateAppCullOverride(entry)) return false;
        add(sun.group,entry.node,entry.bound,0);RecordPrivateGroupSubmission(entry.node,entry.bound);
    }
    if(player) {
        auto* bytes=reinterpret_cast<std::uint8_t*>(player);
        if(!MarkSubmittedNode(bytes)) return false;

        RE::NiUpdateData update{};update.camera=camera;
        NiVirtualDispatch::UpdateDownwardPass(player,update,0);
        add(sun.group,bytes,bytes+0xb0,0);RecordPrivateGroupSubmission(bytes,bytes+0xb0);
    }
    if(!g_privateGroupSubmittedRootCount || !PrivateGroupSubmissionsPairwiseSubtreeDisjoint()) return false;
    submitTiming.Finish();
    {
        MirrorPerformance::Sample timing(MirrorPerformance::Stage::FlatSunCull,sun.mapIndex);
        reinterpret_cast<GroupProcess_t>(REL::Offset(ReflectionRuntime::Rva(kRVA_Group_Process)).address())(sun.group,0);
    }
    sun.failureStage="post-cull-pools";
    if(!ReflectionPassPoolsIdle() || MirrorSceneRenderer::LoadBlocked()) return false;
    sun.failureStage="player-depth-properties";
    if(player && !ArmMaterialPlayerFlattenedPassCleanup(player,clone.stateGeneration)) return false;

    const auto clearAddress=REL::Offset(ReflectionRuntime::Rva(kRVA_BSShaderUtil_ClearRenderPasses)).address();
    sun.failureStage="depth-cache-contract";
    if(g_privateGroupSubmissionRecordFaulted || std::memcmp(reinterpret_cast<const void*>(clearAddress),
        ReflectionRuntime::Prologue(kRVA_BSShaderUtil_ClearRenderPasses,kBSShaderUtilClearRenderPassesBody).data(),
        kBSShaderUtilClearRenderPassesBody.size())!=0) return false;
    {
        MirrorPerformance::Sample timing(MirrorPerformance::Stage::FlatSunCache,sun.mapIndex);
        if(!ClearPrivateSunDepthPasses(clearAddress,player!=nullptr)) return false;
    }
    g_suppressReflectionMTA=true;g_armMaterialPasses=true;
    {
        MirrorPerformance::Sample timing(MirrorPerformance::Stage::FlatSunAccumulate,sun.mapIndex);
        reinterpret_cast<Group2_t>(REL::Offset(ReflectionRuntime::Rva(kRVA_Group_AccumulatePasses)).address())(sun.group,g_armMaterialAccumulator);
    }
    g_suppressReflectionMTA=false;RestorePrivateAppCullOverrides();
    sun.failureStage="post-accumulation-pools";
    if(!ReflectionPassPoolsIdle()) return false;
    BindPrivateSunDepth();
    {
        MirrorPerformance::Sample timing(MirrorPerformance::Stage::FlatSunRaster,sun.mapIndex);
        reinterpret_cast<RenderScene_t>(REL::Offset(ReflectionRuntime::Rva(kRVA_RenderScene)).address())(camera,g_armMaterialAccumulator,0);
    }

    {
        MirrorPerformance::Sample timing(MirrorPerformance::Stage::FlatSunRetire,sun.mapIndex);
        RestorePrivateSunDepth();
    }
    sun.failureStage="depth-cleanup";
    return !g_faulted && !g_materialAccumulatorPermanentlyQuarantined && ReflectionPassPoolsIdle();
}

bool CollectPrivateSunCasters(void* scene,const MirrorPrivateSun::View& fit,
    const std::uint8_t* livePlayer,const std::uint8_t* receiver,RE::NiAVObject* firstPerson)
{
    auto& sun=g_privateSun;

    (void)receiver;
    MirrorPrivateSun::Traversal traversal{sun.casterPending,firstPerson,&RetainPrivateSunCaster,
        sun.mapIndex==0?&sun.requestCasterVolume:nullptr,
        sun.mapIndex==0?&sun.requestCasterSet:nullptr};
    traversal.smallObjects=sun.smallObjects;
    const auto range=sun.published.range;
    traversal.range={range.x,range.y,range.z,range.w};
    auto* root=static_cast<RE::NiNode*>(scene);

    const bool interiorPinned=IsMirrorPlanarDrive(g_activeDriveKind) && PinInteriorAppCullRoots(g_activeDriveKind);
    traversal.QueueChildren(root->GetRuntimeData().children,0);
    for(auto entry=traversal.Next();entry.node;entry=traversal.Next()) {
        CollectNearRecursive(static_cast<std::uint8_t*>(const_cast<void*>(entry.node)),
            fit.eye.x,fit.eye.y,fit.eye.z,MirrorPrivateSun::Far,MirrorPrivateSun::Far*MirrorPrivateSun::Far,
            true,true,true,true,livePlayer,nullptr,nullptr,static_cast<int>(entry.depth),&traversal);
        if(g_cullListCapSkips) {traversal.Fail("submission-budget");break;}
    }
    unsigned interiorCasterRoots=0;
    if(interiorPinned && !g_cullListCapSkips && !InteriorFallbackDisabled()) {
        for(std::size_t i=0;i<g_interiorAppCullRoots.count;++i) {
            auto* pinnedRoot=g_interiorAppCullRoots.roots[i];
            if(!pinnedRoot || g_interiorAppCullReachedSet.count(pinnedRoot)!=0u || InteriorRootAncestorSubmittedSEH(pinnedRoot)) continue;
            ++interiorCasterRoots;
            CollectNearRecursive(reinterpret_cast<std::uint8_t*>(pinnedRoot),
                fit.eye.x,fit.eye.y,fit.eye.z,MirrorPrivateSun::Far,MirrorPrivateSun::Far*MirrorPrivateSun::Far,
                true,true,true,true,livePlayer,nullptr,nullptr,0,&traversal);
            if(g_cullListCapSkips) {traversal.Fail("submission-budget");break;}
        }
    }
    if(interiorPinned) {
        ReleaseLocalEnvironmentCellRoots(g_interiorAppCullRoots);
        g_interiorAppCullRoots={};g_interiorAppCullRootSet.clear();g_interiorAppCullAncestorSet.clear();g_interiorAppCullReachedSet.clear();
    }
    sun.casterVisits=traversal.visits;sun.casterCount=static_cast<unsigned>(g_cullList.size());

    if(sun.mapIndex==0) g_privateSunCasterCount.store(sun.casterCount,std::memory_order_release);
    sun.casterFailure=traversal.failure;sun.casterNode=traversal.currentNode;
    sun.casterDepth=traversal.deepest;sun.casterChildSlots=traversal.childSlots;
    static std::array<unsigned,2> reports{};
    if(sun.mapIndex<reports.size() && (++reports[sun.mapIndex]<=4 || reports[sun.mapIndex]%300==0))
        logger::info("[PlanarMirrors] private sunlight caster selection: map={} visited={} selected={} receiverHull={} rejectedBounds={} complete={} skippedSmall={} interiorFallbackRoots={}",
            sun.mapIndex,sun.casterVisits,sun.casterCount,traversal.casterVolume && traversal.casterVolume->valid,
            traversal.casterBoundsRejected,traversal.complete && !g_cullListCapSkips,traversal.smallCastersRejected,interiorCasterRoots);
    return traversal.complete && !g_cullListCapSkips;
}

MirrorShadowCasterVolume BuildPrivateSunCasterVolume(RE::NiCamera* camera,const DirectX::XMFLOAT3& rays)
{
    DirectX::XMFLOAT4X4 matrix{};DirectX::XMFLOAT3 origin{};
    if(!PlanarMirrors::GetUnclippedCameraView(camera,matrix,origin)) return {};

    return MirrorShadowCasterVolume::Build(matrix,origin,rays,MirrorPrivateSun::Far,
        3.f*(2.f*MirrorPrivateSun::WideExtent/MirrorPrivateSun::Resolution)+2.f);
}

[[nodiscard]] bool PlayerMaskViewRequested() noexcept
{
    static std::uint64_t s_lastPollTick=0u;
    static bool s_requested=false;
    const auto now=GetTickCount64();
    if(s_lastPollTick==0u || now-s_lastPollTick>=2000u) {
        s_lastPollTick=now;
        const bool requested=GetFileAttributesW(L"Data\\dynref_playermask")!=INVALID_FILE_ATTRIBUTES;
        if(requested!=s_requested)
            logger::info("[PlanarMirrors] player-mask debug view {} (green = treated as the player, magenta = in the mask but rejected by the depth test)",
                requested?"ON":"OFF");
        s_requested=requested;
    }
    return s_requested;
}

bool CapturePrivateSun(RE::NiCamera* reflectedCamera,void* scene,
    const PrivatePlayerRenderClone& clone,RE::NiAVObject* player,
    const std::uint8_t* livePlayer,const std::uint8_t* receiver,RE::NiAVObject* firstPerson)
{
    using namespace DirectX;
    auto& sun=g_privateSun;
    sun.complete=false;
    sun.published={};sun.published.contract.x=MirrorPrivateSun::kABI;
    const bool outdoorsOnly=MirrorSettings::SunlightShadowsOutdoorsOnly();
    const bool playerShadows=(MirrorSettings::PlayerSunlightShadowsEnabled() && MirrorSettings::PlayerShadowsEnabled());
    if(!MirrorSunShadowsRequired() || !PrivateSunLocationAllowed(outdoorsOnly)) {

        sun.maps={};sun.publishedEpoch={};
        return true;
    }
    MirrorPerformance::Sample timing(MirrorPerformance::Stage::FlatPrivateSun);
    sun.failureStage="resources";sun.mapIndex=0;sun.casterVisits=0;sun.casterCount=0;
    sun.casterFailure="none";sun.casterNode=nullptr;sun.casterDepth=sun.casterChildSlots=0;
    auto& target=ActivePlanarTarget(g_activeDriveKind);
    const auto range=OutdoorMirrorRange();
    const bool limitedRange=range.Active();

    const bool playerCandidate=player &&
        MirrorSceneRange::Finite(player->worldBound.fRadius) && player->worldBound.fRadius>0 &&
        MirrorPrivateSun::Finite({player->worldBound.center.x,player->worldBound.center.y,player->worldBound.center.z}) &&
        !range.Outside(player->worldBound.center.x,player->worldBound.center.y,player->worldBound.center.z,
            player->worldBound.fRadius);
    const bool playerDetailCandidate=!MirrorSettings::LowShadowQuality() && playerShadows && playerCandidate &&
        player->worldBound.fRadius<MirrorPrivateSun::DetailExtent;
    if(!PreparePrivateSunResources(target.Width(),target.Height(),playerDetailCandidate,
        playerCandidate && (!playerShadows || playerDetailCandidate))) return false;
    sun.published.range={range.x,range.y,range.z,range.radius};

    sun.published.sampling.w=static_cast<float>((playerShadows?0u:1u)|(MirrorSettings::PlayerShadowsEnabled()?0u:2u)|
        (PlayerMaskViewRequested()?4u:0u));
    MirrorPrivateSun::Epoch epoch{};
    epoch.playerSunlightShadows=playerShadows;
    epoch.lowShadowQuality=MirrorSettings::LowShadowQuality();
    epoch.wideResolution=MirrorSettings::ShadowResolution();
    epoch.detailResolution=MirrorSettings::ShadowDetailResolution();
    sun.published.sampling.x=1.f/static_cast<float>(epoch.wideResolution);
    sun.published.sampling.y=1.f/static_cast<float>(epoch.detailResolution);
    epoch.smallObjects=sun.smallObjects=MirrorSettings::SmallObjectShadowsEnabled();
    epoch.outdoorsOnly=outdoorsOnly;
    epoch.range={range.x,range.y,range.z,range.radius};
    epoch.device=globals::d3d::device;epoch.scene=scene;
    epoch.livePlayer=livePlayer;epoch.firstPerson=firstPerson;epoch.player=player;
    epoch.loadGeneration=sun.loadGeneration;epoch.frame=GetRenderFrame();
    epoch.cameraGeneration=g_flatReflectionDriveSnapshotGeneration;
    if(player) {
        epoch.poseSerial=clone.poseSerial;epoch.sourceSerial=clone.sourceUpdateSerial;
        epoch.playerGeneration=clone.stateGeneration;
        for(unsigned i=0;i<3;++i) {
            const auto& row=player->world.rotate.entry[i];
            epoch.playerTransform[i*3]=row.x;epoch.playerTransform[i*3+1]=row.y;epoch.playerTransform[i*3+2]=row.z;
        }
        epoch.playerTransform[9]=player->world.translate.x;epoch.playerTransform[10]=player->world.translate.y;
        epoch.playerTransform[11]=player->world.translate.z;epoch.playerTransform[12]=player->world.scale;
    }
    if(!epoch.Valid()) {sun.failureStage="source-epoch";return false;}
    FlatMirrorLighting::Constants lighting{};VRMirrorScene::Frustum lightSelection{};
    sun.failureStage="sun-direction";
    if(!SnapshotFlatMirrorLights(scene,lightSelection,ActivePlanarMirrorRoute(g_activeDriveKind).center,lighting)) return false;
    if(!lighting.extentLightsHistory.z || lighting.lights[0].positionRadius.w!=0) {
        sun.published={};sun.published.contract.x=MirrorPrivateSun::kABI;
        globals::d3d::context->UpdateSubresource(sun.constants.Get(),0,nullptr,&sun.published,0,0);
        sun.publishedEpoch=epoch;sun.complete=true;return true;
    }
    const auto direction=lighting.lights[0].positionRadius;
    const XMFLOAT3 rays{-direction.x,-direction.y,-direction.z};
    sun.requestCasterVolume=BuildPrivateSunCasterVolume(reflectedCamera,rays);
    const auto receiverReference=MirrorReferenceHandle(g_activeDriveKind).get();
    const auto receiverID=receiverReference ? receiverReference->GetFormID() : 0u;
    const auto focus=limitedRange ? XMFLOAT3{range.x,range.y,range.z} : ActivePlanarMirrorRoute(g_activeDriveKind).center;
    XMFLOAT3 playerFocus=focus;
    if(player) playerFocus={player->worldBound.center.x,player->worldBound.center.y,player->worldBound.center.z};
    std::array<MirrorPrivateSun::View,2> fits{};
    sun.failureStage="light-fit";

    constexpr float kUnlimitedWideReuseGuard=640.f;
    
    const float shadowCut=float(MirrorSettings::ShadowDistance());
    const float shadowDistance=(std::min)(limitedRange ? range.radius : shadowCut, shadowCut);
    const float wideExtent=MirrorSceneRange::ShadowExtent(shadowDistance)+(limitedRange ? 0.f : kUnlimitedWideReuseGuard);
    const float wideGuard=limitedRange ? MirrorSceneRange::ShadowReuseGuard : kUnlimitedWideReuseGuard;
    if(!MirrorPrivateSun::Fit(focus,rays,wideExtent,fits[0],wideGuard,epoch.wideResolution) ||
        !MirrorPrivateSun::Fit(playerFocus,rays,MirrorPrivateSun::DetailExtent,fits[1],0,epoch.detailResolution)) return false;
    sun.casterPlanner.Prepare(receiverID,epoch.frame,epoch.loadGeneration,
        fits[0],sun.requestCasterVolume,sun.requestCasterSet);

    const bool detail=playerDetailCandidate && MirrorPrivateSun::CoversSphere(fits[1].worldToTexture,
        playerFocus,player->worldBound.fRadius,epoch.detailResolution);
    const bool maskPlayer=detail || (playerCandidate && !playerShadows);
    const auto sourceCurrent=[&]() noexcept {
        return MirrorSunShadowsRequired() && epoch.smallObjects==MirrorSettings::SmallObjectShadowsEnabled() &&
            epoch.lowShadowQuality==MirrorSettings::LowShadowQuality() &&
            epoch.wideResolution==MirrorSettings::ShadowResolution() && epoch.detailResolution==MirrorSettings::ShadowDetailResolution() &&
            epoch.playerSunlightShadows==(MirrorSettings::PlayerSunlightShadowsEnabled() && MirrorSettings::PlayerShadowsEnabled()) &&
            epoch.outdoorsOnly==MirrorSettings::SunlightShadowsOutdoorsOnly() && PrivateSunLocationAllowed(epoch.outdoorsOnly) &&
            range.radius==OutdoorMirrorRange().radius &&
            !MirrorSceneRenderer::LoadBlocked() && epoch.loadGeneration==MirrorSceneRenderer::LoadGeneration() &&
            epoch.frame==GetRenderFrame() && epoch.cameraGeneration==g_flatReflectionDriveSnapshotGeneration &&
            (!player || PrivatePlayerRenderClonePoseEpochMatchesCurrent(clone));
    };
    std::array<bool,2> reused{};

    for(unsigned map=0;map<3;++map) {
        if((map==1 && !detail) || (map==2 && !maskPlayer)) continue;
        sun.mapIndex=map;
        const bool mask=map==2;MirrorPrivateSun::View fit{};
        auto* camera=reflectedCamera;
        ReleaseCullList();g_flatCollectionFrustumValid=false;g_cullListCapSkips=0;
        if(!mask) {
            fit=fits[map];
            auto& cached=sun.maps[map];
            if(cached.Matches(epoch,fit) && (map!=0 || sun.wideCasterSet.Covers(sun.requestCasterVolume)) && (map==0 ||
                MirrorPrivateSun::CoversSphere(cached.matrix,playerFocus,player->worldBound.fRadius,epoch.detailResolution))) {
                sun.published.worldToTexture[map]=cached.matrix;
                reused[map]=true;MirrorPerformance::ShadowMap(map,true);
                continue;
            }
            if(map==0) {
                static unsigned cacheReports=0;
                if(++cacheReports<=12 || cacheReports%600==0)
                    logger::info("[PlanarMirrors] private sunlight wide cache: receiver={:p} frame={} complete={} epoch={} fit={} casterCoverage={} currentValid={} requestedVolumes={} cachedVolumes={}",
                        static_cast<const void*>(receiver),epoch.frame,cached.complete,cached.epoch==epoch,
                        MirrorPrivateSun::Covers(cached.volume,fit),sun.wideCasterSet.Covers(sun.requestCasterVolume),
                        sun.requestCasterVolume.valid,sun.requestCasterSet.Size(),sun.wideCasterSet.Size());
            }

            cached.Invalidate();
            sun.failureStage="native-light-camera";
            if(!SetPrivateSunCamera(fit)) return false;
            sun.failureStage="native-detail-coverage";
            if(map==1 && !MirrorPrivateSun::CoversSphere(fit.worldToTexture,playerFocus,player->worldBound.fRadius,epoch.detailResolution)) return false;
            camera=sun.camera;
            g_flatCollectionFrustum.Set(fit.relativeViewProjection.m,fit.eye.x,fit.eye.y,fit.eye.z);
            g_flatCollectionFrustumValid=true;
            sun.failureStage="caster-collection";
            {
                MirrorPerformance::Sample collectTiming(MirrorPerformance::Stage::FlatSunCollect,map);
                if(!CollectPrivateSunCasters(scene,fit,livePlayer,receiver,firstPerson)) return false;
            }
            sun.published.worldToTexture[map]=fit.worldToTexture;
        }
        auto& depth=map==0?sun.wide:(map==1?sun.detail:sun.playerMask);
        {
            MirrorPerformance::Sample drawTiming(MirrorPerformance::Stage::FlatSunDraw,map);
            if(!RenderPrivateSunDepth(camera,reflectedCamera,scene,clone,player,depth,mask)) {
                RestorePrivateSunDepth();return false;
            }
        }
        if(!sourceCurrent()) {sun.failureStage="source-epoch";return false;}

        if(map==0 && !sun.wideCasterSet.Assign(sun.requestCasterSet)) {
            sun.failureStage="caster-coverage-storage";return false;
        }
        if(!mask && !sun.maps[map].Publish(epoch,fits[map],fit.worldToTexture)) {
            sun.failureStage="map-publication";return false;
        }
        MirrorPerformance::ShadowMap(map,false);
    }
    g_flatCollectionFrustumValid=false;
    if(player) {
        RE::NiUpdateData update{};update.camera=reflectedCamera;
        NiVirtualDispatch::UpdateDownwardPass(player,update,0);
        sun.failureStage="pose-epoch";
        if(!PrivatePlayerRenderClonePoseEpochMatchesCurrent(clone)) return false;
    }
    sun.failureStage="source-epoch";
    if(!sourceCurrent()) return false;
    sun.published.contract={MirrorPrivateSun::kABI,1,detail?1u:0u,maskPlayer?1u:0u};
    globals::d3d::context->UpdateSubresource(sun.constants.Get(),0,nullptr,&sun.published,0,0);
    sun.complete=true;
    sun.failureStage="complete";
    sun.publishedEpoch=epoch;
    static unsigned reports=0;
    if(++reports<=4 || reports%300==0) logger::info("[PlanarMirrors] private sunlight complete: wide={} detail={} playerMask={} coherentPose=true reusedWide={} reusedDetail={} distance={} extent={} wideTexel={} playerRadius={} perspective={} playerShadows={}",
        sun.wide.width,detail?sun.detail.width:0,maskPlayer,reused[0],reused[1],range.radius,fits[0].extent,
        fits[0].texel,player?player->worldBound.fRadius:0,clone.poseFromThirdPerson?"third-person":"first-person",playerShadows);
    return true;
}

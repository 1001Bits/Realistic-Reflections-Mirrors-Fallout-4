

float NativeFlatMirrorShadowBias(float authored,bool sun,unsigned cascade) noexcept
{
    using FlatMirrorLighting::Read;
    const auto base=REL::Module::get().base();
    const bool port=ReflectionRuntime::IsPort240();
    const float scale=Read<float>(reinterpret_cast<void*>(base),
        sun && cascade ? (port?0x2F99674:0x38CB3EC) : (port?0x3E71D90:0x67333E0));
    return authored*0.00025f*scale*((sun && cascade)?19.0f/30.0f:1.0f);
}

const void* NativeFlatMirrorShadowSourceCamera() noexcept
{

    return FlatMirrorLighting::Read<const void*>(reinterpret_cast<void*>(REL::Module::get().base()),
        ReflectionRuntime::IsPort240()?0x3E5E3D0:0x6721FC8);
}

float NativeFlatMirrorSunShadowBlend() noexcept
{
    
    return FlatMirrorLighting::Read<float>(reinterpret_cast<void*>(REL::Module::get().base()),
        ReflectionRuntime::IsPort240()?0x3E71D94:0x67333E4);
}

struct FlatMirrorShadowCache
{
    struct Texture
    {
        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
        D3D11_TEXTURE2D_DESC description{};
        DXGI_FORMAT viewFormat{};
        std::uint64_t bytes{};
    };
    struct Entry
    {
        const void* light{};
        const void* native{};
        std::uintptr_t type{};
        std::uint64_t generation{}, demandTick{};
        std::array<std::byte,64> transform{};
        float radius{};
        std::array<std::uint64_t,4> captureTick{};
        std::uint64_t requested{};
        std::array<std::uint64_t,4> fulfilled{};
        std::array<MirrorShadowMaps::Map,4> maps{};
        std::array<Texture,4> textures{};
        unsigned count{};
    };
    std::mutex mutex;
    std::array<Entry,32> entries{};
    ID3D11Device* device{}; 
    Microsoft::WRL::ComPtr<ID3D11Buffer> constants;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler;
    std::array<Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>,MirrorShadowMaps::kResources> resolveViews{};
    MirrorShadowMaps::Bindings bindings{};
    std::uint64_t generation{}, allocatedBytes{};
    static constexpr std::uint64_t kMemoryBudget = 128ull*1024*1024;

    void ClearBindings() noexcept { bindings={};resolveViews={}; }

    void RetireUnusedTextures(Entry& entry) noexcept
    {
        for (unsigned index=0;index<entry.textures.size();++index) {
            if (std::any_of(entry.maps.begin(),entry.maps.end(),[&](const auto& map) {
                return map.resource.w && map.resource.x==index;
            })) continue;
            allocatedBytes-=entry.textures[index].bytes;
            entry.textures[index]={};
        }
    }

    void ResetEntry(Entry& entry) noexcept
    {
        for (const auto& texture:entry.textures) allocatedBytes-=texture.bytes;
        entry={};
    }
    void Prepare(ID3D11Device* current, std::uint64_t load, std::uint64_t now) noexcept
    {
        if (device!=current || generation!=load) {
            for (auto& entry:entries) ResetEntry(entry);
            resolveViews={};bindings={};constants.Reset();sampler.Reset();
            device=current;generation=load;
        }
        for (auto& entry:entries)
            if (entry.light && now-entry.demandTick>3000) ResetEntry(entry);
    }
};
NeverDestroyed<FlatMirrorShadowCache> g_flatMirrorShadowCache;
std::uintptr_t g_flatSunShadowVtable{}, g_flatPointShadowVtable{};
bool g_flatMirrorShadowHookInstalled{};

bool CaptureFlatMirrorSunCoverage(const void* light,unsigned cascade,unsigned count,
    MirrorShadowMaps::Map& map) noexcept
{
    using FlatMirrorLighting::Read;
    const auto* camera=NativeFlatMirrorShadowSourceCamera();
    if (!FlatMirrorLighting::Pointer(camera) || !count || cascade>=count || count>4) return false;
    const auto eye=Read<DirectX::XMFLOAT3>(camera,0xA0);
    const auto forward=Read<DirectX::XMFLOAT3>(camera,0x70);
    const float length=std::sqrt(forward.x*forward.x+forward.y*forward.y+forward.z*forward.z);
    const float start=cascade?Read<float>(light,0x250+4*(cascade-1)):0;
    const float end=Read<float>(light,0x250+4*cascade);
    const float farDistance=Read<float>(light,0x250+4*(count-1));
    float previous=0;
    for (unsigned i=0;i<count;++i) {
        const float split=Read<float>(light,0x250+4*i);
        if (!std::isfinite(split) || split<=previous) return false;
        previous=split;
    }
    if (!std::isfinite(eye.x) || !std::isfinite(eye.y) || !std::isfinite(eye.z) ||
        !std::isfinite(length) || std::fabs(length-1)>0.01f || !std::isfinite(start) ||
        !std::isfinite(end) || !std::isfinite(farDistance) || start<0 || end<=start ||
        farDistance<end || farDistance>1.e8f) return false;
    map.sourceEyeFar={eye.x,eye.y,eye.z,farDistance};
    map.sourceForwardMin={forward.x/length,forward.y/length,forward.z/length,start};
    map.sourceRange={end,0,0,0};
    return true;
}

void CaptureFlatMirrorSunCasterHull(const void* light,MirrorShadowMaps::Map& map) noexcept
{
    using FlatMirrorLighting::Read;

    map.casterHull={};map.casterPlanes={};map.sourceRange.y=0;
    const auto* culler=Read<const void*>(light,0x2C8);
    if (!FlatMirrorLighting::Pointer(culler) || !Read<bool>(culler,0x11F)) return;
    const unsigned mask=Read<unsigned>(culler,0x10C);
    if (!mask || (mask&~0x3Fu)) return;
    auto planes=Read<std::array<DirectX::XMFLOAT4,6>>(culler,0xAC);
    unsigned count=0;
    for (unsigned i=0;i<planes.size();++i) {
        if (!(mask&(1u<<i))) {planes[i]={};continue;}
        auto& plane=planes[i];
        const float length=std::sqrt(plane.x*plane.x+plane.y*plane.y+plane.z*plane.z);
        if (!std::isfinite(length) || std::fabs(length-1)>0.01f || !std::isfinite(plane.w)) return;
        plane={plane.x/length,plane.y/length,plane.z/length,plane.w/length};
        ++count;
    }
    if (count<4) return;
    const auto& m=map.worldToShadow;
    const float uScale=std::sqrt(m._11*m._11+m._21*m._21+m._31*m._31);
    const float vScale=std::sqrt(m._12*m._12+m._22*m._22+m._32*m._32);
    if (!std::isfinite(uScale) || !std::isfinite(vScale) || uScale<1.e-12f || vScale<1.e-12f ||
        std::fabs(m._14)+std::fabs(m._24)+std::fabs(m._34)>1.e-6f || std::fabs(m._44-1)>1.e-5f) return;
    const float fade=2*std::max(map.sampling.z/uScale,map.sampling.w/vScale);
    if (!std::isfinite(fade) || fade<=0) return;
    map.casterPlanes=planes;map.casterHull={mask,0,0,0};map.sourceRange.y=fade;
}

bool SnapshotFlatMirrorSunView(const void* light,unsigned count,MirrorShadowMaps::SunView& view) noexcept
{
    view={};
    MirrorShadowMaps::Map current{};
    if (!CaptureFlatMirrorSunCoverage(light,0,count,current)) return false;
    const float blend=NativeFlatMirrorSunShadowBlend();
    if (!std::isfinite(blend) || blend<0 || blend>current.sourceEyeFar.w) return false;
    view.eyeFar=current.sourceEyeFar;
    view.forwardBlend=current.sourceForwardMin;
    view.forwardBlend.w=blend;
    std::array<float,4> splits{};
    for (unsigned i=0;i<count;++i) splits[i]=FlatMirrorLighting::Read<float>(light,0x250+4*i);
    view.splits={splits[0],splits[1],splits[2],splits[3]};
    view.metadata={count,1,0,0};
    return true;
}

void DemandFlatMirrorShadowsUnsafe(const std::array<const void*,FlatMirrorLighting::kMaximumLights>& lights,
    const FlatMirrorLighting::Constants& lighting) noexcept
{
    auto& cache=g_flatMirrorShadowCache.Get();
    const auto now=GetTickCount64();
    cache.Prepare(globals::d3d::device,MirrorSceneRenderer::LoadGeneration(),now);
    cache.bindings={};cache.resolveViews={};
    MirrorShadowMaps::Constants constants{};
    if (!g_flatMirrorShadowHookInstalled || !cache.device) return;
    for (unsigned i=0;i<std::min<unsigned>(lighting.extentLightsHistory.z,MirrorShadowMaps::kLights);++i) {
        const auto* light=lights[i];
        if (!light) continue;
        const auto type=FlatMirrorLighting::Read<std::uintptr_t>(light,0);
        if (type!=g_flatSunShadowVtable && type!=g_flatPointShadowVtable) continue;
        const auto* native=FlatMirrorLighting::Read<const void*>(light,0xB8);
        auto entry=std::find_if(cache.entries.begin(),cache.entries.end(),[&](const auto& value) {
            return value.light==light && value.native==native && value.type==type;
        });
        if (entry==cache.entries.end()) {
            entry=std::find_if(cache.entries.begin(),cache.entries.end(),[](const auto& value) {return !value.light;});
            if (entry==cache.entries.end()) continue;
            entry->light=light;entry->native=native;entry->type=type;entry->generation=cache.generation;
        }
        entry->demandTick=now;
        ++entry->requested; 
        const auto first=constants.contract.y;
        unsigned count=entry->count;
        if (!count || count>4 || count!=FlatMirrorLighting::Read<unsigned>(light,0x190) ||
            first+count>MirrorShadowMaps::kMaps) continue;
        
        const bool sun=type==g_flatSunShadowVtable;
        if (sun && !SnapshotFlatMirrorSunView(light,count,constants.sunViews[i])) continue;
        bool ready=entry->transform==FlatMirrorLighting::Read<std::array<std::byte,64>>(native,0x70) &&
            (sun || entry->radius==FlatMirrorLighting::Read<float>(native,0x138));

        if (!sun) for (unsigned m=0;m<count;++m)
            ready &= entry->maps[m].resource.w && entry->captureTick[m];
        if (!ready) continue;
        unsigned accepted=0;
        for (unsigned m=0;m<count;++m) {
            auto map=entry->maps[m];
            if (!map.resource.w || map.resource.x>=entry->textures.size()) continue;
            auto* srv=entry->textures[map.resource.x].srv.Get();
            unsigned slot=0;
            for (;slot<constants.contract.z && cache.resolveViews[slot].Get()!=srv;++slot) {}
            if (!srv || slot>=cache.resolveViews.size()) {ready=false;break;}
            if (slot==constants.contract.z) {cache.resolveViews[slot]=srv;++constants.contract.z;}
            map.resource.x=slot;
            constants.maps[first+accepted++]=map;
        }
        if (ready && accepted && (sun || accepted==count)) {
            constants.lights[i]={first,accepted,0,0};constants.contract.y+=accepted;
        }
    }
    if (!cache.constants) {
        D3D11_BUFFER_DESC description{};description.ByteWidth=sizeof(constants);description.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        if (FAILED(cache.device->CreateBuffer(&description,nullptr,&cache.constants))) return;
    }
    if (!cache.sampler) {
        D3D11_SAMPLER_DESC description{};
        description.Filter=D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
        description.AddressU=description.AddressV=description.AddressW=D3D11_TEXTURE_ADDRESS_BORDER;
        description.BorderColor[0]=description.BorderColor[1]=description.BorderColor[2]=description.BorderColor[3]=1;
        description.ComparisonFunc=D3D11_COMPARISON_LESS_EQUAL;
        description.MaxLOD=D3D11_FLOAT32_MAX;
        if (FAILED(cache.device->CreateSamplerState(&description,&cache.sampler))) return;
    }
    globals::d3d::context->UpdateSubresource(cache.constants.Get(),0,nullptr,&constants,0,0);
    cache.bindings.constants=cache.constants.Get();cache.bindings.sampler=cache.sampler.Get();
    for (unsigned slot=0;slot<cache.resolveViews.size();++slot)cache.bindings.resources[slot]=cache.resolveViews[slot].Get();
}

void DemandFlatMirrorShadows(const std::array<const void*,FlatMirrorLighting::kMaximumLights>& lights,
    const FlatMirrorLighting::Constants& lighting) noexcept
{
    auto& cache=g_flatMirrorShadowCache.Get();
    cache.mutex.lock();
    __try {
        if (!MirrorSettings::ShadowsEnabled()) {
            cache.ClearBindings();
            for(auto& entry:cache.entries) cache.ResetEntry(entry);
            return;
        }
        __try {DemandFlatMirrorShadowsUnsafe(lights,lighting);}
        __except(EXCEPTION_EXECUTE_HANDLER) {cache.ClearBindings();}
    } __finally {cache.mutex.unlock();}
}

void CaptureFlatMirrorShadowMapsUnsafe(const void* light, unsigned renderedMask)
{
    if (!g_enabled || !g_mirrorReflections || g_privateRenderActive || g_mirrorRenderActive || MirrorSceneRenderer::LoadBlocked()) return;
    auto& cache=g_flatMirrorShadowCache.Get();
    const auto now=GetTickCount64();
    cache.Prepare(globals::d3d::device,MirrorSceneRenderer::LoadGeneration(),now);
    auto entry=std::find_if(cache.entries.begin(),cache.entries.end(),[&](const auto& value){return value.light==light;});
    if (entry==cache.entries.end() || !cache.device || !renderedMask) return;
    using FlatMirrorLighting::Read;
    const auto type=Read<std::uintptr_t>(light,0);
    const auto* native=Read<const void*>(light,0xB8);
    if (type!=entry->type || native!=entry->native || entry->generation!=cache.generation) {cache.ResetEntry(*entry);return;}
    const auto transform=Read<std::array<std::byte,64>>(native,0x70);
    const float nativeRadius=type==g_flatSunShadowVtable?1:Read<float>(native,0x138);
    if (entry->transform!=transform || entry->radius!=nativeRadius) {
        entry->maps={};entry->captureTick={};entry->fulfilled={};entry->transform=transform;entry->radius=nativeRadius;
    }
    const unsigned count=Read<unsigned>(light,0x190);
    const auto* maps=Read<const std::byte*>(light,0x198);
    if (!count || count>4 || !FlatMirrorLighting::Pointer(maps) || (type==g_flatPointShadowVtable && count>2)) return;
    if (entry->count!=count) {entry->maps={};entry->captureTick={};entry->fulfilled={};entry->count=count;}
    std::uintptr_t renderState{},contextAddress{};
    ReadThreadRenderContextsNoexcept(renderState,contextAddress);
    auto* context=reinterpret_cast<ID3D11DeviceContext*>(contextAddress);
    if (!context) return; 
    Microsoft::WRL::ComPtr<ID3D11Device> contextDevice;context->GetDevice(&contextDevice);
    if (contextDevice.Get()!=cache.device) return;
    auto* manager=reinterpret_cast<RE::BSGraphics::RenderTargetManager*>(
        REL::Offset(ReflectionRuntime::Rva(kRVA_RTManager)).address());
    auto* renderer=globals::game::renderer;
    if (!manager || !renderer) return;
    const bool sun=type==g_flatSunShadowVtable;

    for (unsigned iteration=0;iteration<count;++iteration) {
        const unsigned m=sun?count-1-iteration:iteration;
        if (!(renderedMask&(1u<<m))) continue;
        if (entry->fulfilled[m]==entry->requested && entry->maps[m].resource.w) continue;

        const auto* record=maps+m*0xF0;
        const unsigned logical=Read<unsigned>(record,0x50),slice=Read<unsigned>(record,0x54);
        if (logical>=12) continue;
        const auto physical=ReflectionRuntime::DepthStencilTargetIds(manager)[logical];
        if (physical>=kFO4PhysicalDepthTargetCount) continue;
        auto* source=reinterpret_cast<ID3D11ShaderResourceView*>(renderer->data.depthStencilTargets[physical].srViewDepth);
        if (!source) continue;
        Microsoft::WRL::ComPtr<ID3D11Resource> resource;source->GetResource(&resource);
        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
        if (!resource || FAILED(resource.As(&texture))) continue;
        D3D11_TEXTURE2D_DESC description{};texture->GetDesc(&description);
        D3D11_SHADER_RESOURCE_VIEW_DESC view{};source->GetDesc(&view);
        if (!MirrorShadowMaps::DepthDescription(description,view) || slice>=description.ArraySize) continue;
        MirrorShadowMaps::Map map{};map.worldToShadow=Read<DirectX::XMFLOAT4X4>(record,0);
        if (!MirrorShadowMaps::Finite(map.worldToShadow)) continue;
        if (sun && !CaptureFlatMirrorSunCoverage(light,m,count,map)) continue;
        const unsigned left=Read<unsigned>(record,0xD0),right=Read<unsigned>(record,0xD4);
        const unsigned bottom=Read<unsigned>(record,0xD8),top=Read<unsigned>(record,0xDC);
        if (left>=right || top>=bottom || right>description.Width || bottom>description.Height) continue;

        if (!sun && (left || top || right-left!=bottom-top)) continue;
        const float width=static_cast<float>(description.Width),height=static_cast<float>(description.Height);
        map.bounds=sun?DirectX::XMFLOAT4{left/width,top/height,right/width,bottom/height}:
            DirectX::XMFLOAT4{0,1-bottom/height,right/width,1};
        const float radius=sun?1:Read<float>(native,0x138);
        const float bias=NativeFlatMirrorShadowBias(Read<float>(light,0x1D0),sun,m);
        if (!std::isfinite(radius) || radius<=0 || !std::isfinite(bias) || bias<0 || bias>.1f) continue;
        map.sampling={bias,1/radius,1/width,1/height};
        if (sun) {
            CaptureFlatMirrorSunCasterHull(light,map);
            if (!map.casterHull.x) continue;
            map.casterHull.y=m; 
        }
        auto targetDescription=description;
        targetDescription.ArraySize=sun?1:count;targetDescription.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        targetDescription.Usage=D3D11_USAGE_DEFAULT;targetDescription.CPUAccessFlags=targetDescription.MiscFlags=0;

        D3D11_BOX box{0,description.Height-bottom,0,right,description.Height,1};
        if (!sun) {
            targetDescription.Width=right;targetDescription.Height=bottom;
            map.bounds={0,0,1,1};map.sampling.z=1.0f/right;map.sampling.w=1.0f/bottom;
        }
        unsigned targetIndex=0;
        for (;targetIndex<entry->textures.size();++targetIndex) {
            const auto& t=entry->textures[targetIndex];
            const bool usedByOtherCascade=sun && std::any_of(entry->maps.begin(),entry->maps.end(),[&](const auto& other) {
                return &other!=&entry->maps[m] && other.resource.w && other.resource.x==targetIndex;
            });
            if (!usedByOtherCascade && t.texture && t.description.Width==targetDescription.Width &&
                t.description.Height==targetDescription.Height && t.description.Format==targetDescription.Format &&
                t.description.ArraySize==targetDescription.ArraySize && t.viewFormat==view.Format) break;
        }
        if (targetIndex==entry->textures.size()) {
            cache.RetireUnusedTextures(*entry);
            targetIndex=0;for(;targetIndex<entry->textures.size() && entry->textures[targetIndex].texture;++targetIndex) {}

            if (targetIndex==entry->textures.size() && sun && entry->maps[m].resource.w &&
                entry->maps[m].resource.x<entry->textures.size()) {
                const unsigned ownSlot=entry->maps[m].resource.x;
                const bool shared=std::any_of(entry->maps.begin(),entry->maps.end(),[&](const auto& other) {
                    return &other!=&entry->maps[m] && other.resource.w && other.resource.x==ownSlot;
                });
                if (!shared) targetIndex=ownSlot;
            }
            const auto bytes=MirrorShadowMaps::Bytes(targetDescription);
            if (targetIndex==entry->textures.size() || bytes>cache.kMemoryBudget-cache.allocatedBytes) continue;
            FlatMirrorShadowCache::Texture replacement{};
            if (FAILED(cache.device->CreateTexture2D(&targetDescription,nullptr,&replacement.texture))) continue;
            D3D11_SHADER_RESOURCE_VIEW_DESC targetView{};targetView.Format=view.Format;
            targetView.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
            targetView.Texture2DArray.MipLevels=1;targetView.Texture2DArray.ArraySize=targetDescription.ArraySize;
            if (FAILED(cache.device->CreateShaderResourceView(replacement.texture.Get(),&targetView,&replacement.srv))) continue;
            replacement.description=targetDescription;replacement.viewFormat=view.Format;replacement.bytes=bytes;
            cache.allocatedBytes-=entry->textures[targetIndex].bytes;
            entry->textures[targetIndex]=std::move(replacement);cache.allocatedBytes+=bytes;
        }
        
        const unsigned targetSlice=sun?0:m;
        context->CopySubresourceRegion(entry->textures[targetIndex].texture.Get(),targetSlice,0,0,0,texture.Get(),slice,sun?nullptr:&box);
        map.resource={targetIndex,targetSlice,sun?1u:(count==2?3u:2u),1};
        entry->maps[m]=map;entry->captureTick[m]=now;entry->fulfilled[m]=entry->requested;
        static std::atomic<unsigned> samples{0};
        if (samples.fetch_add(1,std::memory_order_relaxed)<8)
            logger::info("[PlanarMirrors] SHADOWMAP native {} slice={}/{} copied {}x{} on {} context; cache={} MiB; "
                "receiverDepth=[{},{}] sourceEye=({},{},{}) sourceForward=({},{},{}) casterHull=0x{:02X}",
                sun?"sun":"point",m,count,targetDescription.Width,targetDescription.Height,
                context->GetType()==D3D11_DEVICE_CONTEXT_DEFERRED?"deferred":"immediate",cache.allocatedBytes/(1024*1024),
                map.sourceForwardMin.w,map.sourceRange.x,map.sourceEyeFar.x,map.sourceEyeFar.y,map.sourceEyeFar.z,
                map.sourceForwardMin.x,map.sourceForwardMin.y,map.sourceForwardMin.z,map.casterHull.x);
    }
    cache.RetireUnusedTextures(*entry);
}
void CaptureFlatMirrorShadowMaps(const void* light,unsigned mask) noexcept
{
    if (!mask || !g_flatMirrorShadowHookInstalled || !MirrorSettings::ShadowsEnabled()) return;
    auto& cache=g_flatMirrorShadowCache.Get();
    cache.mutex.lock();
    __try {
        __try {CaptureFlatMirrorShadowMapsUnsafe(light,mask);}
        __except(EXCEPTION_EXECUTE_HANDLER) {
            for (auto& entry:cache.entries) if (entry.light==light) cache.ResetEntry(entry);
            logger::warn("[PlanarMirrors] SHADOWMAP native contract unavailable; retained ordinary lighting");
        }
    } __finally {cache.mutex.unlock();}
}
struct FlatSunShadowRenderHook
{
    static void thunk(void* light,unsigned mask)
    {
        func(light,mask);
        if (!MirrorToggle::Active() || !MirrorSettings::ShadowsEnabled()) return;
        MirrorPerformance::WorkerSample timing(MirrorPerformance::Stage::FlatShadowCopy);
        CaptureFlatMirrorShadowMaps(light,mask);
    }
    static inline REL::Relocation<decltype(thunk)> func;
};
struct FlatPointShadowRenderHook
{
    static void thunk(void* light,unsigned mask)
    {
        func(light,mask);
        if (!MirrorToggle::Active() || !MirrorSettings::ShadowsEnabled()) return;
        MirrorPerformance::WorkerSample timing(MirrorPerformance::Stage::FlatShadowCopy);
        CaptureFlatMirrorShadowMaps(light,mask);
    }
    static inline REL::Relocation<decltype(thunk)> func;
};

void InstallFlatMirrorShadowHooks()
{

    if (REL::Module::IsVR()) return;
    const auto base=REL::Module::get().base();
    const bool port=ReflectionRuntime::IsPort240();
    const auto sun=base+(port?0x291A440:0x309DB98);
    const auto point=base+(port?0x291B138:0x309E608);
    const auto matrix=base+(port?0x224FB30:0x28CA050);
    const auto sunRender=base+(port?0x2250340:0x28CA710);
    const auto pointRender=base+(port?0x2271350:0x28DB720);
    auto matches=[&](std::uintptr_t table,std::uintptr_t render) {
        const auto* entries=reinterpret_cast<const std::uintptr_t*>(table);
        return entries[4]==matrix && entries[10]==render;
    };
    if (!matches(sun,sunRender) || !matches(point,pointRender)) {
        logger::warn("[PlanarMirrors] native shadow-map vtable contract differs; reflected shadows unavailable");
        return;
    }
    g_flatSunShadowVtable=sun;g_flatPointShadowVtable=point;
    FlatSunShadowRenderHook::func=sunRender;
    FlatPointShadowRenderHook::func=pointRender;
    REL::safe_write(sun+10*sizeof(std::uintptr_t),reinterpret_cast<std::uintptr_t>(FlatSunShadowRenderHook::thunk));
    REL::safe_write(point+10*sizeof(std::uintptr_t),reinterpret_cast<std::uintptr_t>(FlatPointShadowRenderHook::thunk));
    g_flatMirrorShadowHookInstalled=true;
    logger::info("[PlanarMirrors] native point shadow-map reuse installed; sunlight uses private caster depth");
}

bool ResolveFlatMirrorMaterial(PlanarMirrors::RenderTarget& target, bool candidate,
    ID3D11DeviceContext* context, ID3D11ComputeShader* shader, ID3D11Buffer* lighting,
    ID3D11ShaderResourceView* previous, [[maybe_unused]] void* reflectedCamera)
{

    MirrorPerformance::Sample lightingTiming(MirrorPerformance::Stage::FlatResolve,
        IsSideMirrorPlanarDrive(g_activeDriveKind) ? 1u : (IsExtraMirrorPlanarDrive(g_activeDriveKind) ? 2u : 0u),
        context);
    MirrorShadowMaps::Lease shadows;
    if (MirrorSettings::ShadowsEnabled()) {
        auto& cache=g_flatMirrorShadowCache.Get();
        std::lock_guard guard(cache.mutex);
        shadows.Pin(cache.bindings);
    }
    shadows.bindings.privateSun=PrivateSunBindings();
    return candidate ? target.ResolveMaterialCaptureCandidate(context,shader,lighting,
        nullptr,nullptr,nullptr,previous,&shadows.bindings,g_planarMirrorLightTilesCS,g_mirrorNativeLightingBindings) :
        target.ResolveMaterialCapture(context,shader,lighting,
        nullptr,nullptr,nullptr,previous,&shadows.bindings,g_planarMirrorLightTilesCS,g_mirrorNativeLightingBindings);
}

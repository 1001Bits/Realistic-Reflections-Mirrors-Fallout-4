

std::atomic<std::uint64_t> g_landingTraceUntilMs{};

struct MirrorLandingTransforms
{
    RE::NiTransform rootLocal{},rootWorld{},pelvisLocal{},pelvisWorld{};
    bool pelvisPresent{};
};

bool ReadMirrorLandingTransforms(RE::NiAVObject* root,const RE::BSFixedString& pelvisName,
    MirrorLandingTransforms& out) noexcept
{
    __try {
        if(!root) return false;
        out.rootLocal=root->local;out.rootWorld=root->world;
        if(auto* pelvis=NiVirtualDispatch::GetObjectByName(root,pelvisName)) {
            out.pelvisLocal=pelvis->local;out.pelvisWorld=pelvis->world;out.pelvisPresent=true;
        }
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}

void TraceMirrorLandingPose(unsigned stage,RE::NiAVObject* root,std::uint64_t pose,
    std::uint64_t source,std::uint64_t hash,int paletteReady,bool submitted) noexcept
{
    const auto until=g_landingTraceUntilMs.load(std::memory_order_relaxed);
    if(!until || stage>1) return;
    const auto now=GetTickCount64();
    if(now>until) return;
    struct Window {std::uint64_t until{};unsigned frame{},count{};};
    static thread_local std::array<Window,2> windows{};
    auto& window=windows[stage];
    if(window.until!=until) window={until};
    const auto frame=GetRenderFrame();
    if(window.count>=120 || (window.count && window.frame==frame)) return;
    window.frame=frame;++window.count;
    try {
        const RE::BSFixedString pelvisName("Pelvis");
        MirrorLandingTransforms snapshot;
        const bool read=ReadMirrorLandingTransforms(root,pelvisName,snapshot);
        const auto transform=[](const RE::NiTransform& t) {
            return fmt::format("t({:.4f},{:.4f},{:.4f}) s({:.5f}) r({:.5f},{:.5f},{:.5f};{:.5f},{:.5f},{:.5f};{:.5f},{:.5f},{:.5f})",
                t.translate.x,t.translate.y,t.translate.z,t.scale,
                t.rotate.entry[0].x,t.rotate.entry[0].y,t.rotate.entry[0].z,
                t.rotate.entry[1].x,t.rotate.entry[1].y,t.rotate.entry[1].z,
                t.rotate.entry[2].x,t.rotate.entry[2].y,t.rotate.entry[2].z);
        };
        logger::info("[MirrorLanding] stage={} frame={} pose={} sourceUpdate={} hash={:016X} paletteReady={} submitted={} read={} pelvisPresent={} rootLocal={} rootWorld={} pelvisLocal={} pelvisWorld={}",
            stage==0?"completed-pose":"draw-complete",frame,pose,source,hash,paletteReady,submitted,read,snapshot.pelvisPresent,
            transform(snapshot.rootLocal),transform(snapshot.rootWorld),transform(snapshot.pelvisLocal),transform(snapshot.pelvisWorld));
    } catch(...) {}
}

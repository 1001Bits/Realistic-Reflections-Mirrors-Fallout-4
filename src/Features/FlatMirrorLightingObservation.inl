
struct MirrorLightingObservation
{
    MirrorLightingReadback::Readback readback;
    PlanarMirrors::RenderTarget* target{};
    std::uint64_t tick{}, markerTick{}, generation{};
    std::uint32_t frame{}, cell{}, samples{}, attempts{};
    bool enabled{}, sunBound{}, accepted{};
};
NeverDestroyed<MirrorLightingObservation> g_mirrorLightingObservation;

void PollMirrorLightingObservation() noexcept
{
    auto& state = g_mirrorLightingObservation.Get();
    try {
        const auto now = GetTickCount64();
        if (!state.markerTick || now - state.markerTick >= 2000) {
            state.markerTick = now;
            state.enabled = GetFileAttributesW(L"Data\\dynref_lightingprobe") != INVALID_FILE_ATTRIBUTES;
        }
        if (!state.enabled || state.generation != MirrorSceneRenderer::LoadGeneration() ||
            (state.readback.Pending() && !state.readback.Finished() && state.frame != GetRenderFrame())) {
            state.readback.Reset(); state.target = nullptr;
        }
        MirrorLightingReadback::Sample sample;
        if (!state.readback.Poll(globals::d3d::context, sample)) return;

        const std::array<std::uint32_t, 6> header{0x31504C4Du, sample.width, sample.height,
            static_cast<std::uint32_t>(sample.constants.size()), MirrorLightingReadback::Pixels, 5};
        const auto path = std::format("Data/F4SE/Plugins/MirrorLighting-{:08X}-{}-{}.bin",
            state.cell, state.generation, state.frame);
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output.write(reinterpret_cast<const char*>(header.data()), sizeof(header));
        output.write(reinterpret_cast<const char*>(sample.metadata.data()), sizeof(sample.metadata));
        output.write(reinterpret_cast<const char*>(sample.colors.data()), sizeof(sample.colors));
        output.write(reinterpret_cast<const char*>(sample.constants.data()), static_cast<std::streamsize>(sample.constants.size()));
        logger::info("[PlanarMirrors] LIGHTINGPIXELS cell={:08X} frame={} accepted={} sunBound={} samples={} file={} written={}; diagnostic frame excluded from performance acceptance",
            state.cell, state.frame, state.accepted, state.sunBound, ++state.samples, path, output.good());
        state.target = nullptr;
    } catch (...) { state.readback.Reset(); state.target = nullptr; }
}

void ObserveMirrorLightingResolve(PlanarMirrors::RenderTarget& target, ID3D11Buffer* lighting,
    ID3D11DeviceContext* context) noexcept
{
    auto& state = g_mirrorLightingObservation.Get();
    if (!state.enabled || state.readback.Pending() || !lighting) return;
    try {
        const auto identity = ReadPlayerCellIdentity();
        if (!identity.valid) return;
        const auto generation = MirrorSceneRenderer::LoadGeneration();
        if (state.cell != identity.formID || state.generation != generation) {
            state.cell = identity.formID; state.generation = generation;
            state.samples = state.attempts = 0; state.tick = 0;
        }
        const auto now = GetTickCount64();
        if (state.samples >= 3 || state.attempts >= 6 || (state.tick && now - state.tick < 3000)) return;
        state.tick = now; ++state.attempts;
        if (!state.readback.Begin(context, {target.MaterialDiffuseTexture(), target.MaterialNormalTexture(),
            target.MaterialPropertiesTexture(), target.ColorTexture()}, target.MaterialMetadataTexture(), lighting)) return;
        state.target = &target; state.frame = GetRenderFrame();
        state.sunBound = PrivateSunBindings().constants != nullptr;
        state.accepted = false;
    } catch (...) { state.readback.Reset(); state.target = nullptr; }
}

void FinishMirrorLightingObservation(PlanarMirrors::RenderTarget& target, ID3D11DeviceContext* context,
    bool accepted) noexcept
{
    auto& state = g_mirrorLightingObservation.Get();
    if (state.target != &target || state.frame != GetRenderFrame() || state.readback.Finished()) return;
    state.accepted = accepted;
    (void)state.readback.Finish(context, target.ColorTexture());
}

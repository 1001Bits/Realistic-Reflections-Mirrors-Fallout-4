

std::array<FlatMirrorLighting::Diagnostics, 3> g_flatLightDiagnostics{};

[[nodiscard]] bool EngineLightFadeForced() noexcept
{
	static std::uint64_t s_lastPollTick = 0u;
	static bool s_forced = false;
	const auto now = GetTickCount64();
	if (s_lastPollTick == 0u || now - s_lastPollTick >= 2000u) {
		s_lastPollTick = now;
		s_forced = GetFileAttributesW(L"Data\\dynref_enginelightfade") != INVALID_FILE_ATTRIBUTES;
	}
	return s_forced;
}

void LogFlatLightDrops(const FlatMirrorLighting::Diagnostics& diagnostics, const DirectX::XMFLOAT3& focus) noexcept
{
	static std::uint32_t s_logs = 0;
	static std::uint64_t s_lastTick = 0;
	if (diagnostics.droppedCount == 0 || s_logs >= 60u) return;
	const auto tick = GetTickCount64();
	if (tick - s_lastTick < 250u) return;
	s_lastTick = tick; ++s_logs;
	static constexpr const char* kReasons[]{ "accepted", "no-wrapper", "unusable-geometry", "flag17E", "app-culled", "fade<=0",
		"color", "frustum", "score", "budget", "absent-from-lists" };
	for (unsigned i = 0; i < diagnostics.droppedCount; ++i) {
		const auto reason = diagnostics.reasons[i];
		const auto& p = diagnostics.droppedPositions[i];
		logger::info("[PlanarMirrors] LIGHTSET drop native={} reason={} pos=({:.0f},{:.0f},{:.0f}) radius={:.0f} focus=({:.0f},{:.0f},{:.0f}) accepted={} visited={}",
			diagnostics.dropped[i], reason < 11u ? kReasons[reason] : "?", p.x, p.y, p.z, p.w,
			focus.x, focus.y, focus.z, diagnostics.accepted, diagnostics.visited);
	}
}

bool PrivateSunLocationAllowed(bool outdoorsOnly) noexcept;
bool PrivateSunLightingAvailable() noexcept;
extern std::atomic<unsigned> g_privateSunCasterCount;
extern std::atomic<float> g_privateSunLightLevel;

[[nodiscard]] bool DirectionalLightUnshadowable() noexcept
{
	return !PrivateSunLightingAvailable();
}

void LogRefusedLightShapes(const FlatMirrorLighting::Diagnostics& diagnostics) noexcept
{
	static std::uint64_t s_lastTick = 0u;
	static unsigned s_logs = 0;
	if (!diagnostics.shapeSampleCount || s_logs >= 12u) return;
	const auto now = GetTickCount64();
	if (s_lastTick != 0u && now - s_lastTick < 3000u) return;
	s_lastTick = now;
	++s_logs;
	const auto base = REL::Module::get().base();
	for (unsigned i = 0; i < diagnostics.shapeSampleCount; ++i) {
		const auto& sample = diagnostics.shapeSamples[i];
		const auto vtable = reinterpret_cast<std::uintptr_t>(sample.vtable);
		logger::info(
			"[PlanarMirrors] LIGHTSHAPE refused shape={} vtableRVA=0x{:X} pos=({:.0f},{:.0f},{:.0f}) radius={:.0f} "
			"diffuse=({:.3f},{:.3f},{:.3f}); refused for unusable geometry, not for its shape",
			sample.shape, vtable > base ? vtable - base : vtable,
			sample.positionRadius.x, sample.positionRadius.y, sample.positionRadius.z, sample.positionRadius.w,
			sample.diffuse.x, sample.diffuse.y, sample.diffuse.z);
	}
}

void LogMirrorLightBalance(const FlatMirrorLighting::Constants& constants, bool sunGated) noexcept
{
	static std::uint64_t s_lastTick = 0u;
	const auto now = GetTickCount64();
	if (s_lastTick != 0u && now - s_lastTick < 2000u) return;
	s_lastTick = now;
	const FlatMirrorLighting::Light* sun = nullptr;
	unsigned attenuated = 0;
	for (unsigned i = 0; i < constants.extentLightsHistory.z; ++i) {
		if (constants.lights[i].positionRadius.w > 0.0f) { ++attenuated; continue; }
		if (!sun) sun = &constants.lights[i];
	}
	const auto base = [&](unsigned channel) { return constants.ambient[channel].w; };
	const auto span = [&](unsigned channel) {
		const auto& a = constants.ambient[channel];
		return std::max({ std::fabs(a.x), std::fabs(a.y), std::fabs(a.z) });
	};
	logger::info(
		"[PlanarMirrors] LIGHTBALANCE lights={} attenuated={} sun={} sunColour=({:.4f},{:.4f},{:.4f}) "
		"sunToward=({:.2f},{:.2f},{:.2f}) ambientBase=({:.4f},{:.4f},{:.4f}) ambientSpan=({:.4f},{:.4f},{:.4f}) "
		"shadows={} budget={} sunGated={}; colours are linear (light diffuse and ambient are both gamma 2.2 "
		"expanded); a radius-0 light is unattenuated on every surface facing it, so it is dropped wherever it "
		"cannot be shadowed",
		constants.extentLightsHistory.z, attenuated, sun ? "present" : "absent",
		sun ? sun->color.x : 0.f, sun ? sun->color.y : 0.f, sun ? sun->color.z : 0.f,
		sun ? sun->positionRadius.x : 0.f, sun ? sun->positionRadius.y : 0.f, sun ? sun->positionRadius.z : 0.f,
		base(0), base(1), base(2), span(0), span(1), span(2),
		MirrorSettings::ShadowsEnabled(), MirrorSettings::LightBudget(), sunGated);
}

bool SnapshotFlatMirrorLights(void* scene, const VRMirrorScene::Frustum& frustum,
	const DirectX::XMFLOAT3& focus, FlatMirrorLighting::Constants& constants,
	FlatMirrorLighting::Diagnostics* diagnostics = nullptr) noexcept
{
	bool locked = false, result = false;
	auto* lock = reinterpret_cast<RE::BSSpinLock*>(static_cast<std::byte*>(scene) + 0x1D0);
	__try {
		__try {
			constexpr std::array<std::uint8_t, 14> entry{0x48,0x83,0xEC,0x68,0x41,0xB8,3,0,0,0,0x84,0xD2,0x74,0x49};
			const auto address = REL::Module::get().base() + ReflectionRuntime::Rva(0x27D67B0);
			const auto& expected = ReflectionRuntime::Prologue(0x27D67B0, entry);
			if (std::memcmp(reinterpret_cast<const void*>(address), expected.data(), expected.size()) != 0)
				return false;
			float ambient[18]{};
			reinterpret_cast<void (*)(void*, bool)>(address)(ambient, false);
			if (!FlatMirrorLighting::Ambient(ambient, constants)) return false;
			lock->lock("MirrorsOfFalloutFlatLightSnapshot");
			locked = true;
			std::array<const void*,FlatMirrorLighting::kMaximumLights> selected{};
			MirrorLightFade::g_useEngineFade = EngineLightFadeForced();
			result = FlatMirrorLighting::Snapshot(scene, frustum, focus, constants, &selected, diagnostics,
				MirrorSettings::LightBudget());
			if (result) {

				const auto* imagespace=RE::ImageSpaceManager::GetSingleton();
				const float sunScale=imagespace ? imagespace->currentEOFData.baseData.hdrData.sunlightScale : 1.f;
				if (!std::isfinite(sunScale) || sunScale<0) return false;
				FlatMirrorLighting::ScaleSunlight(constants,sunScale);

				{
					float level = -1.0f;
					for(unsigned i=0;i<constants.extentLightsHistory.z;++i) {
						const auto& light = constants.lights[i];
						if(light.positionRadius.w != 0) continue;
						level = .2126f*light.color.x + .7152f*light.color.y + .0722f*light.color.z;
						break;
					}
					g_privateSunLightLevel.store(level, std::memory_order_release);
				}
				const bool sunGated = DirectionalLightUnshadowable();
				
				g_privateForwardSunGated.store(sunGated, std::memory_order_release);
				constants.contract.w=sunGated?1.f:0.f;
				LogMirrorLightBalance(constants, sunGated);

				if (sunGated)
					for(unsigned i=0;i<constants.extentLightsHistory.z;++i)
						if(constants.lights[i].positionRadius.w==0) constants.lights[i].color={0,0,0,0};

				for(unsigned i=0;i<constants.extentLightsHistory.z;++i)
					if(constants.lights[i].positionRadius.w==0) selected[i]=nullptr;
				DemandFlatMirrorShadows(selected, constants);
			}
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			result = false;
		}
	} __finally {
		if (locked) lock->unlock();
	}
	if (result && diagnostics) { LogFlatLightDrops(*diagnostics, focus); LogRefusedLightShapes(*diagnostics); }
	if (result) {
		
		static unsigned s_spotLogs = 0;
		for (unsigned i = 0; i < constants.extentLightsHistory.z && s_spotLogs < 6u; ++i) {
			const auto& light = constants.lights[i];
			if (light.spot.w <= -1.5f) continue;
			++s_spotLogs;
			logger::info("[PlanarMirrors] SPOTLIGHT pos=({:.0f},{:.0f},{:.0f}) radius={:.0f} dir=({:.2f},{:.2f},{:.2f}) coneCos={:.6f} coneExponent={:.4f}",
				light.positionRadius.x, light.positionRadius.y, light.positionRadius.z, light.positionRadius.w,
				light.spot.x, light.spot.y, light.spot.z,
				light.spot.w, light.color.w);
		}
	}
	return result;
}

[[nodiscard]] ID3D11Buffer* UpdateMirrorMaterialResolveContract(
	RE::NiCamera* camera, void* scene, DriveKind kind) noexcept
{
	using namespace DirectX;
	MirrorPerformance::Sample lightingTiming(MirrorPerformance::Stage::FlatLightingPrepare);
	
	g_privateForwardSunGated.store(false, std::memory_order_release);
	auto* device = globals::d3d::device;
	auto* context = globals::d3d::context;
	auto& target = ActivePlanarTarget(kind);
	const auto tick = GetTickCount64();
	target.BeginMaterialHistory(tick);
	if (!device || !context || !scene || REL::Module::IsVR()) return nullptr;
	PlanarMirrors::PatchedCameraState state{};
	if (!PlanarMirrors::GetPatchedCameraStateForEye(camera, 0, state)) return nullptr;
	FlatMirrorLighting::Constants constants{};
	{
		std::lock_guard guard(g_nativeMirrorLightingMutex);
		g_mirrorNativeLightingBindings=g_nativeMirrorLighting.Get().Get(GetRenderFrame(),MirrorSceneRenderer::LoadGeneration());
		constants.contract.y=g_mirrorNativeLightingBindings ? 1.f : 0.f;
		constants.contract.z=g_mirrorNativeLightingBindings.directional ? 1.f : 0.f;
	}
	{
		static std::uint64_t lastGeneration=~std::uint64_t{},lastReport{};
		static bool lastNative{};
		const bool native=constants.contract.y>0.5f;
		const auto generation=MirrorSceneRenderer::LoadGeneration();
		if (generation!=lastGeneration || (native!=lastNative && tick-lastReport>=1000)) {
			logger::info("[PlanarMirrors] native world lighting: captured={} directional={} frame={} generation={}",
				native,constants.contract.z>0.5f,GetRenderFrame(),generation);
			lastGeneration=generation;lastNative=native;lastReport=tick;
		}
	}
	auto invert = [](const XMFLOAT4X4& input, XMFLOAT4X4& output) noexcept {
		XMVECTOR determinant{};
		const auto inverse = XMMatrixInverse(&determinant, XMLoadFloat4x4(&input));
		const float d = XMVectorGetX(determinant);
		if (!std::isfinite(d) || std::fabs(d) < 1.e-12f) return false;
		XMStoreFloat4x4(&output, inverse);
		for (const auto& row : output.m) for (float value : row) if (!std::isfinite(value)) return false;
		return true;
	};
	if (!invert(state.viewProjection, constants.inverseViewProjection) ||
		!invert(state.view, constants.normalToWorld)) return nullptr;
	
	constants.viewProjection = state.viewProjection;
	constants.eye = {state.cameraOrigin.x, state.cameraOrigin.y, state.cameraOrigin.z, 1};
	constants.extentLightsHistory = {target.Width(), target.Height(), 0, 0};
	VRMirrorScene::Frustum frustum{};
	frustum.Set(state.viewProjection.m, state.cameraOrigin.x, state.cameraOrigin.y, state.cameraOrigin.z);

	for (std::size_t plane = 0; plane < 6; ++plane)
		frustum.planeLengths[plane] = 0.0f;
	{
		const auto& route = ActivePlanarMirrorRoute(kind);
		const float nx = route.normal.x, ny = route.normal.y, nz = route.normal.z;
		const float length = std::sqrt(nx * nx + ny * ny + nz * nz);
		if (std::isfinite(length) && length > 1.0e-4f) {
			const float behind = (state.cameraOrigin.x - route.center.x) * nx +
				(state.cameraOrigin.y - route.center.y) * ny + (state.cameraOrigin.z - route.center.z) * nz;
			const float sign = behind > 0.0f ? -1.0f : 1.0f;  
			const float ux = sign * nx / length, uy = sign * ny / length, uz = sign * nz / length;
			constexpr float kWallClearance = 48.0f;  
			frustum.SetHalfSpace(ux, uy, uz,
				ux * route.center.x + uy * route.center.y + uz * route.center.z - kWallClearance);
		}
	}
	const std::size_t lightDiagnosticsIndex = IsSideMirrorPlanarDrive(kind) ? 1u : (IsExtraMirrorPlanarDrive(kind) ? 2u : 0u);
	auto& lightDiagnostics = g_flatLightDiagnostics[lightDiagnosticsIndex];
	if (!SnapshotFlatMirrorLights(scene, frustum, ActivePlanarMirrorRoute(kind).center, constants,
			&lightDiagnostics)) return nullptr;
	{

		static std::array<std::array<unsigned, 6>, 3> s_lastCensus{};
		static std::array<std::uint64_t, 3> s_lastCensusTick{};
		static unsigned s_censusLogs = 0;
		const auto receiverReference = MirrorReferenceHandle(kind).get();
		const unsigned receiverID = receiverReference ? receiverReference->GetFormID() : 0u;
		static constexpr const char* kKindNames[]{ "hero", "side", "extra" };
		const std::array<unsigned, 6> census{ lightDiagnostics.accepted, lightDiagnostics.frustumRejected,
			lightDiagnostics.budgetRejected, lightDiagnostics.scoreRejected,
			lightDiagnostics.flagRejected + 1000u * lightDiagnostics.fadeRejected, receiverID };
		if (census != s_lastCensus[lightDiagnosticsIndex] && s_censusLogs < 400u &&
			tick - s_lastCensusTick[lightDiagnosticsIndex] >= 300u) {
			s_lastCensus[lightDiagnosticsIndex] = census;
			s_lastCensusTick[lightDiagnosticsIndex] = tick;
			++s_censusLogs;
			const auto& pane = ActivePlanarMirrorRoute(kind).center;
			logger::info(
				"[PlanarMirrors] LIGHTCENSUS kind={} receiver={:08X} accepted={} frustumRejected={} budgetRejected={} scoreRejected={} flagRejected={} fadeRejected={} visited={} engineFade={} previsReject={} noWrapper={} unusableGeom={} appCulled={} badColour={} duplicate={} shapes=[{},{},{},{},{},{},{},{}] previsFlagged={} mainViewCulled={} engineFadeAvg={:.3f} reflectedEye=({:.0f},{:.0f},{:.0f}) pane=({:.0f},{:.0f},{:.0f}); visited counts list entries, and every one of them lands in exactly one of these buckets",
				kKindNames[lightDiagnosticsIndex], receiverID, census[0], census[1], census[2], census[3],
				lightDiagnostics.flagRejected, lightDiagnostics.fadeRejected, lightDiagnostics.visited,
				MirrorLightFade::g_useEngineFade ? 1 : 0, MirrorLightFade::g_rejectPrevisCulled ? 1 : 0,
				lightDiagnostics.rejectReasons[FlatMirrorLighting::kNoWrapper],
				lightDiagnostics.rejectReasons[FlatMirrorLighting::kType],
				lightDiagnostics.rejectReasons[FlatMirrorLighting::kAppCulled],
				lightDiagnostics.rejectReasons[FlatMirrorLighting::kColor],
				lightDiagnostics.duplicateSkipped,
				lightDiagnostics.rejectedShapes[0], lightDiagnostics.rejectedShapes[1],
				lightDiagnostics.rejectedShapes[2], lightDiagnostics.rejectedShapes[3],
				lightDiagnostics.rejectedShapes[4], lightDiagnostics.rejectedShapes[5],
				lightDiagnostics.rejectedShapes[6], lightDiagnostics.rejectedShapes[7],
				lightDiagnostics.previsFlagged, lightDiagnostics.mainViewCulled,
				lightDiagnostics.visited ? lightDiagnostics.engineFadeSum / static_cast<float>(lightDiagnostics.visited) : 0.f,
				state.cameraOrigin.x, state.cameraOrigin.y, state.cameraOrigin.z, pane.x, pane.y, pane.z);
		}
		
		{
			static std::array<std::array<std::pair<const void*, float>, FlatMirrorLighting::kMaximumLights>, 3> s_lastFades{};
			static unsigned s_fadeLogs = 0;
			for (unsigned i = 0; i < FlatMirrorLighting::kMaximumLights; ++i) {
				const void* native = lightDiagnostics.previous[i];
				auto& last = s_lastFades[lightDiagnosticsIndex][i];
				if (!native) { last = {}; continue; }
				const float fade = lightDiagnostics.fades[i];
				if (last.first == native && std::fabs(last.second - fade) > 0.05f && s_fadeLogs < 200u) {
					++s_fadeLogs;
					const auto& p = constants.lights[i].positionRadius;
					logger::info(
						"[PlanarMirrors] LIGHTFADE kind={} receiver={:08X} native={} fade {:.2f} -> {:.2f} pos=({:.0f},{:.0f},{:.0f}) radius={:.0f}",
						kKindNames[lightDiagnosticsIndex], receiverID, native, last.second, fade, p.x, p.y, p.z, p.w);
				}
				last = { native, fade };
			}
		}
		
		{
			static std::array<float, 3> s_lastAmbient{};
			static unsigned s_ambientLogs = 0;
			float sum = 0.0f;
			for (const auto& row : constants.ambient) sum += std::fabs(row.x) + std::fabs(row.y) + std::fabs(row.z) + std::fabs(row.w);
			const float previous = s_lastAmbient[lightDiagnosticsIndex];
			if (std::fabs(sum - previous) > 0.02f * (std::fabs(previous) + 0.05f) && s_ambientLogs < 100u) {
				++s_ambientLogs;
				logger::info("[PlanarMirrors] AMBIENT kind={} receiver={:08X} sum {:.3f} -> {:.3f}",
					kKindNames[lightDiagnosticsIndex], receiverID, previous, sum);
			}
			s_lastAmbient[lightDiagnosticsIndex] = sum;
		}
	}
	const auto historyTick = target.MaterialHistoryTick();
	if (historyTick && tick > historyTick && tick - historyTick <= 100u &&
		CommittedPlanarMirrorValid(kind) &&
		MirrorHybridRoutesMatch(ActivePlanarMirrorRoute(kind), CommittedPlanarMirrorRoute(kind))) {
		constants.previousViewProjection = CommittedPlanarMirrorViewProjection(kind);
		const auto& eye = CommittedPlanarMirrorReflectedEye(kind);
		constants.previousEye = {eye.x, eye.y, eye.z, 1};
		if (invert(constants.previousViewProjection, constants.previousInverseViewProjection)) {
			constants.extentLightsHistory.w = 1;
			constants.history.x = static_cast<float>(tick - historyTick);
		}
	}
	if (device != g_planarMaterialResolveContractDevice) {
		if (g_planarMaterialResolveContractCB) g_planarMaterialResolveContractCB->Release();
		g_planarMaterialResolveContractCB = nullptr;
		g_planarMaterialResolveContractDevice = device;
	}
	if (!g_planarMaterialResolveContractCB) {
		D3D11_BUFFER_DESC description{};
		description.ByteWidth = sizeof(constants);
		description.Usage = D3D11_USAGE_DEFAULT;
		description.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		if (FAILED(device->CreateBuffer(&description, nullptr, &g_planarMaterialResolveContractCB))) return nullptr;
	}
	context->UpdateSubresource(g_planarMaterialResolveContractCB, 0, nullptr, &constants, 0, 0);
	return g_planarMaterialResolveContractCB;
}

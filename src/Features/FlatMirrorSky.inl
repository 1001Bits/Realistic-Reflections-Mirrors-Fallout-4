

NeverDestroyed<MirrorSky::Pipeline> g_mirrorSkyPipeline;
thread_local MirrorSky::Lease g_mirrorSkyLease{};

struct MirrorSkyPass
{
	RE::BSRenderPass* pass{};
	unsigned order{};
};

bool CollectMirrorSkyPasses(RE::NiAVObject* object, void* accumulator,
	std::array<MirrorSkyPass,64>& passes, unsigned& count, unsigned& visits, unsigned depth) noexcept
{
	if (!object) return true;
	if (++visits > 4096 || depth > 32) return false;
	if ((*reinterpret_cast<const std::uint32_t*>(reinterpret_cast<const std::byte*>(object) + 0x108) & 1u) != 0)
		return true; 
	if (auto* node = netimmerse_cast<RE::NiNode*>(object)) {
		const auto& children = node->GetRuntimeData().children;
		if (children.size() > 4096) return false;
		for (decltype(children.size()) i = 0; i < children.size(); ++i)
			if (!CollectMirrorSkyPasses(children[i].get(), accumulator, passes, count, visits, depth + 1)) return false;
		return true;
	}
	auto* geometry = netimmerse_cast<RE::BSGeometry*>(object);
	auto* property = geometry ? geometry->properties[1].get() : nullptr;
	const auto* rtti = property ? property->GetRTTI() : nullptr;
	if (!geometry || !geometry->rendererData || !rtti || std::strcmp(rtti->GetName(), "BSSkyShaderProperty") != 0)
		return true;

	auto* shaderProperty = static_cast<RE::BSShaderProperty*>(property);
	auto* array = shaderProperty->GetRenderPasses(geometry, 0x18u, reinterpret_cast<RE::BSShaderAccumulator*>(accumulator));
	auto* pass = array ? array->passList : nullptr;
	if (!pass) return true;
	const auto* bytes = reinterpret_cast<const std::byte*>(pass);
	const auto* shader = *reinterpret_cast<RE::BSShader* const*>(bytes + 8);
	const auto technique = PassTechniqueUnsafe(pass);
	if (!shader || shader->shaderType != RE::BSShader::Sky || pass->geometry != geometry ||
		*reinterpret_cast<RE::BSShaderProperty* const*>(bytes + 0x10) != shaderProperty || technique < 1 || technique > 8)
		return false;
	for (unsigned i = 0; i < count; ++i) if (passes[i].pass == pass) return true;
	if (count == passes.size()) return false;
	const auto type = *reinterpret_cast<const unsigned*>(reinterpret_cast<const std::byte*>(property) + 0xA8);
	passes[count++] = {pass, type == 1 ? 0u : (type == 3 ? 2u : 1u)};
	return true;
}

bool DrawMirrorSkyPass(const MirrorSkyPass& item, RE::NiAVObject* root,
	const DirectX::XMFLOAT3& eye) noexcept
{
	MirrorSky::TranslationLease translation{};
	bool result=false;
	__try {
		__try {
			auto* geometry=item.pass->geometry;
			if(translation.Begin(&geometry->world.translate, &geometry->previousWorld.translate,
				&root->world.translate, &root->previousWorld.translate, &eye)) {
				auto draw=reinterpret_cast<bool (*)(RE::BSRenderPass*, std::uint32_t, bool)>(
					REL::Offset(ReflectionRuntime::Rva(kRVA_RenderPassImmediately)).address());
				result=draw(item.pass,PassTechniqueUnsafe(item.pass),false);
			}
		} __except(EXCEPTION_EXECUTE_HANDLER) { result=false; }
	} __finally {
		if(!translation.Restore()) result=false;
	}
	return result;
}

bool RenderMirrorSky(void* accumulator, PlanarMirrors::RenderTarget& target, RE::NiCamera* camera) noexcept
{
	if (REL::Module::IsVR()) return true;
	PlanarMirrors::PatchedCameraState reflected{};
	if(!PlanarMirrors::GetPatchedCameraStateForEye(camera,0,reflected)) return false;
	auto& pipeline = g_mirrorSkyPipeline.Get();
	if (!pipeline.Ensure(globals::d3d::device)) return false;
	std::array<MirrorSkyPass,64> passes{};
	unsigned count = 0, visits = 0;
	bool result = false, attempted = false;
	__try {
		__try {
			auto* root = *reinterpret_cast<RE::NiAVObject**>(REL::Offset(ReflectionRuntime::Rva(kRVA_SkyRoot)).address());
			result = CollectMirrorSkyPasses(root, accumulator, passes, count, visits, 0);
			if (result && count) {

				for (unsigned i = 1; i < count; ++i) {
					const auto item = passes[i]; unsigned j = i;
					while (j && passes[j-1].order > item.order) {passes[j] = passes[j-1]; --j;}
					passes[j] = item;
				}
				result = g_mirrorSkyLease.Begin(globals::d3d::context, pipeline, target.Width(), target.Height());
				for (unsigned i = 0; result && i < count; ++i) {
					attempted = true;
					result = DrawMirrorSkyPass(passes[i],root,reflected.cameraOrigin);
				}
			}
		} __except (EXCEPTION_EXECUTE_HANDLER) { result = false; }
	} __finally {

		auto lease = g_mirrorSkyLease;
		g_mirrorSkyLease = {};
		if (attempted && !EndImmediatePassSequenceNoexcept()) result = false;
		if (!lease.Restore()) result = false;
	}
	static unsigned logs = 0;
	if (++logs <= 6 || (!result && logs % 120 == 0))
		logger::info("[PlanarMirrors] native sky suffix: passes={} nodes={} reflectedOrigin=({:.1f},{:.1f},{:.1f}) complete={}",
			count, visits, reflected.cameraOrigin.x, reflected.cameraOrigin.y, reflected.cameraOrigin.z, result);
	return result;
}

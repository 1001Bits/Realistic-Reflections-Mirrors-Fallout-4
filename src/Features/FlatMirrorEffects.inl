NeverDestroyed<MirrorEffectPasses::Snapshot> g_mirrorEffectPasses;
NeverDestroyed<MirrorEffectState::Pipeline> g_mirrorEffectPipeline;
thread_local MirrorEffectState::Lease g_mirrorEffectLease{};

bool CollectMirrorEffects(void* accumulator,const PlanarMirrors::PatchedCameraState& camera,
	const RE::NiAVObject* face,MirrorEffectPasses::Snapshot& snapshot) noexcept
{
	snapshot.Reset();
	auto next=[](void* value) {return static_cast<void*>(static_cast<RE::BSRenderPass*>(value)->nextSibling);};
	auto select=[&](void* value,float& depth) {
		auto* pass=static_cast<RE::BSRenderPass*>(value);
		if(!pass->shader||pass->shader->shaderType!=RE::BSShader::Effect||!pass->geometry||!pass->property) return false;
		const auto* rtti=pass->property->GetRTTI();
		if(!rtti||std::strcmp(rtti->GetName(),"BSEffectShaderProperty")!=0) return false;
		
		if(face&&IsGeometryUnderRootSEH(pass->geometry,face)) return false;
		const auto& center=pass->geometry->worldBound.center;
		const float x=center.x-camera.cameraOrigin.x,y=center.y-camera.cameraOrigin.y,z=center.z-camera.cameraOrigin.z;
		depth=x*camera.view._13+y*camera.view._23+z*camera.view._33;
		return std::isfinite(depth);
	};
	auto readGroup=[&](const std::uint8_t* group) {
		if(!group||!(group[0x20]&1u)) return true;
		auto* pass=*reinterpret_cast<void* const*>(group+8);
		return snapshot.Chain(pass,next,select);
	};
	auto* batch=static_cast<const std::uint8_t*>(accumulator)+0xC8;
	if(!readGroup(*reinterpret_cast<std::uint8_t* const*>(batch+0x410))) return false;
	const auto count=*reinterpret_cast<const std::uint32_t*>(batch+0x418);
	auto* groups=*reinterpret_cast<const std::uint8_t* const*>(batch+0x420);
	if(count>512||(count&&!groups)) return false;
	for(std::uint32_t i=0;i<count;++i) if(!readGroup(groups+i*0x28)) return false;

	return true;
}

bool RenderMirrorEffects(void* accumulator,PlanarMirrors::RenderTarget& target,RE::NiCamera* camera,
	const RE::NiAVObject* face) noexcept
{
	if(REL::Module::IsVR()) return true;
	PlanarMirrors::PatchedCameraState reflected{};
	if(!PlanarMirrors::GetPatchedCameraStateForEye(camera,0,reflected)) return false;
	auto& snapshot=g_mirrorEffectPasses.Get();
	auto& pipeline=g_mirrorEffectPipeline.Get();
	bool result=false,attempted=false,collected=false;
	std::uint32_t accepted=0;
	__try {
		__try {
			collected=CollectMirrorEffects(accumulator,reflected,face,snapshot);
			
			result=true;
			if(collected&&snapshot.count) {
				snapshot.Sort();
				result=pipeline.Ensure(globals::d3d::device,target.DepthDSV())&&
					g_mirrorEffectLease.Begin(globals::d3d::context,pipeline,target.ColorRTV(),target.DepthSRV());
				auto draw=reinterpret_cast<bool (*)(RE::BSRenderPass*,std::uint32_t,bool)>(
					REL::Offset(ReflectionRuntime::Rva(kRVA_RenderPassImmediately)).address());
				for(std::size_t i=0;result&&i<snapshot.count;++i) {
					auto* pass=static_cast<RE::BSRenderPass*>(snapshot.items[i].pass);
					attempted=true;
					if(draw(pass,PassTechniqueUnsafe(pass),true)) ++accepted;
				}
			}
		} __except(EXCEPTION_EXECUTE_HANDLER) {result=false;}
	} __finally {
		auto lease=g_mirrorEffectLease;g_mirrorEffectLease={};
		if(attempted&&!EndImmediatePassSequenceNoexcept()) result=false;
		if(!lease.Restore()) result=false;
	}
	static std::uint32_t logs=0;
	if(++logs<=6||logs%600u==0||!result)
		logger::info("[PlanarMirrors] effect suffix: visited={} selected={} accepted={} snapshot={} complete={} depth=private-read-only",
			snapshot.visits,snapshot.count,accepted,collected,result);
	return result;
}

	struct NativeAmbientObservation
	{
		std::mutex mutex;
		MirrorNativeAmbientProbe::Readback readback;
		std::uint64_t generation{}, lastTick{};
		std::uint32_t cell{};
		std::array<std::uint32_t, 2> samples{};
	};
	NeverDestroyed<NativeAmbientObservation> g_nativeAmbientObservation;

	bool ReadNativeDirectionalAmbient(MirrorNativeAmbientProbe::Sample& sample) noexcept
	{
		__try {
			const auto rva = ReflectionRuntime::Rva(0x27D67B0);
			if (!rva) return false;
			constexpr std::array<std::uint8_t, 14> entry{0x48,0x83,0xEC,0x68,0x41,0xB8,3,0,0,0,0x84,0xD2,0x74,0x49};
			const auto address = REL::Module::get().base() + rva;
			const auto& expected = ReflectionRuntime::Prologue(0x27D67B0, entry);
			if (std::memcmp(reinterpret_cast<const void*>(address), expected.data(), expected.size()) != 0) return false;
			float colors[18]{};
			reinterpret_cast<void (*)(void*, bool)>(address)(colors, false);
			for (float value : colors) if (!std::isfinite(value)) return false;
			for (unsigned c = 0; c < 3; ++c)
				sample.directionalAmbient[c] = {(colors[3+c]-colors[c])*.5f,
					(colors[9+c]-colors[6+c])*.5f, (colors[15+c]-colors[12+c])*.5f,
					(colors[3+c]+colors[c])*.5f};
			return true;
		} __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}

	void PollNativeAmbientObservation()
	{
		auto& state = g_nativeAmbientObservation.Get();
		std::unique_lock guard(state.mutex, std::try_to_lock);
		if (!guard.owns_lock()) return;
		MirrorNativeAmbientProbe::Sample sample;
		if (!state.readback.Poll(globals::d3d::context, sample) || sample.generation != LoadGeneration()) return;
		float maximumBaseDifference = 0;
		for (unsigned row = 0; row < 3; ++row)
			maximumBaseDifference = (std::max)(maximumBaseDifference,
				std::fabs(sample.constants[6+row][3]-sample.directionalAmbient[row][3]));
		logger::info("[PlanarMirrors] NATIVEAMBIENT cell={:08X} descriptor={:08X} generation={} tick={} b2Bytes={} firstConstant={} boundConstants={} baseMaxDifference={:.6f} exposureFormat={} exposureBound={} exposureAvailable={}; cb2[6..8] is view-space, directionalAmbient is world-space; 04000000 is actor fill, bit17 is world ambient",
			sample.cell, sample.descriptor, sample.generation, sample.tick, sample.bufferBytes, sample.firstConstant, sample.constantCount,
			maximumBaseDifference, static_cast<unsigned>(sample.exposureFormat), sample.exposureBound, sample.exposureAvailable);
		for (unsigned row = 0; row < sample.constants.size(); ++row) {
			const auto& value = sample.constants[row];
			logger::info("[PlanarMirrors] NATIVEAMBIENT cb2[{}]=({:.8f},{:.8f},{:.8f},{:.8f})",
				row, value[0], value[1], value[2], value[3]);
		}
		for (unsigned row = 0; row < sample.directionalAmbient.size(); ++row) {
			const auto& value = sample.directionalAmbient[row];
			logger::info("[PlanarMirrors] NATIVEAMBIENT directionalAmbient[{}]=({:.8f},{:.8f},{:.8f},{:.8f})",
				row, value[0], value[1], value[2], value[3]);
		}
		std::array<std::uint32_t, 4> words{};
		std::memcpy(words.data(), sample.exposure.data(), sizeof(words));
		logger::info("[PlanarMirrors] NATIVEAMBIENT t8PixelRaw={:08X}/{:08X}/{:08X}/{:08X}",
			words[0], words[1], words[2], words[3]);
	}

	void ObserveNativeAmbientCommit() noexcept
	{
		if (REL::Module::IsVR() || PrivateRenderActive() || LoadBlocked() || ShutdownRequested()) return;
		std::uintptr_t nativeState{}, nativeContext{};
		ReadThreadRenderContextsNoexcept(nativeState,nativeContext);
		{
			std::unique_lock guard(g_nativeMirrorLightingMutex,std::try_to_lock);
			if (guard.owns_lock())
				g_nativeMirrorLighting.Get().Capture(reinterpret_cast<ID3D11DeviceContext*>(nativeContext),
					MirrorNativeAmbientProbe::techniqueDescriptor,GetRenderFrame(),LoadGeneration());
		}
		auto& state = g_nativeAmbientObservation.Get();
		std::unique_lock guard(state.mutex, std::try_to_lock);
		if (!guard.owns_lock()) return;
		const auto identity = ReadPlayerCellIdentity();
		if (!identity.valid) return;
		const auto generation = LoadGeneration();
		if (state.generation != generation || state.cell != identity.formID) {
			state.readback.Reset();
			state.generation = generation; state.cell = identity.formID;
			state.samples = {}; state.lastTick = 0;
		}
		const auto now = GetTickCount64();
		const auto category = MirrorNativeAmbientProbe::techniqueDescriptor == 0x04000000u ? 1u : 0u;
		if (state.samples[category] >= 3 || state.readback.Pending() || (state.lastTick && now-state.lastTick < 2000)) return;
		std::uintptr_t renderState{}, contextAddress{};
		ReadThreadRenderContextsNoexcept(renderState, contextAddress);
		auto* context = reinterpret_cast<ID3D11DeviceContext*>(contextAddress);
		if (!context) return;
		MirrorNativeAmbientProbe::Sample sample{};
		sample.generation = generation; sample.cell = identity.formID; sample.tick = now;
		sample.descriptor = MirrorNativeAmbientProbe::techniqueDescriptor;
		state.lastTick = now;
		if (ReadNativeDirectionalAmbient(sample) && state.readback.Queue(context, sample)) ++state.samples[category];
	}

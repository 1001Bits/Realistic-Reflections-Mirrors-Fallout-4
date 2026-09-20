
	void PruneFlatMirrorFleet() noexcept
	{
		if (REL::Module::IsVR()) return;
		auto& fleet = FlatFleet();
		if (fleet.device != globals::d3d::device ||
			fleet.loadGeneration != MirrorSceneRenderer::LoadGeneration()) {
			ClearFlatMirrorFleet();
			return;
		}
		auto& entries = fleet.images.Entries();
		for (auto it = entries.begin(); it != entries.end();) {
			const auto reference = it->second.identity.handle.get();
			const auto* cell = reference ? reference->GetParentCell() : nullptr;
			const auto* definition = reference ? GetRegisteredMirrorDefinition(reference.get()) : nullptr;
			if (!reference || reference->GetFormID() != it->first || reference->IsDeleted() ||
				reference->IsDisabled() || !cell || cell->cellDetached ||
				cell->cellState != RE::TESObjectCELL::CELL_STATE::kAttached || !definition ||
				definition->id != it->second.identity.definitionId ||
				!g_workshopPlacement.CaptureAllowed(it->first)) {
				fleet.schedule.Forget(it->first);
				fleet.visibility.erase(it->first);
				it = entries.erase(it);
			} else ++it;
		}
	}

	NeverDestroyed<std::vector<RegisteredMirrorCandidate>> g_flatMirrorFrameCandidates;

	bool FlatMirrorWorkerBusy(unsigned worker) noexcept
	{
		return worker == 0 && (NativeHeroCaptureBusy() || std::any_of(
			g_heroMirrorResolvedContentProofRing.begin(), g_heroMirrorResolvedContentProofRing.end(),
			[](const auto& proof) { return proof.occupied &&
				proof.candidate.receiverFormID == FlatFleet().assigned[0]; }));
	}

	void PublishFlatMirrorWorker(const RegisteredMirrorCandidate& candidate, unsigned worker) noexcept
	{
		const auto id = candidate.reference->GetFormID();

		static std::array<std::uint64_t, 3> lastReport{};
		const auto now = GetTickCount64();
		if (FlatFleet().assigned[worker] != id && (!lastReport[worker] || now - lastReport[worker] >= 2000u)) {
			lastReport[worker] = now;
			logger::info("[PlanarMirrors] fleet worker {} <- receiver {:08X} ({}) capture {}x{}", worker, id,
				candidate.definition->name, MirrorDefinitionRegistry::CaptureExtent(candidate.definition, id,
					kPlanarMirrorSize, kSidePlanarMirrorSize,
					[](std::uint32_t authored) { return MirrorSettings::CaptureSize(authored); }),
				MirrorDefinitionRegistry::CaptureExtent(candidate.definition, id, kPlanarMirrorSize,
					kSidePlanarMirrorSize, [](std::uint32_t authored) { return MirrorSettings::CaptureSize(authored); }));
		}
		FlatFleet().assigned[worker] = id;
		if (worker) PublishRegisteredMirrorCandidate(candidate, worker == 2);
		else {
			const auto previous = g_cachedMirrorHandle.get();
			if (!previous || previous->GetFormID() != id) g_mirrorGrazingVisible = false;
			g_cachedMirrorHandle = candidate.reference->GetHandle();
			g_mirrorCandidateDistSq = FLT_MAX;
			g_mirrorCandidatePriority = 0;
			PublishMirrorCandidate(candidate.surface.center, candidate.surface.selection.plane,
				candidate.surface.route, candidate.surface.radius, candidate.surface.distanceSq, 2u);
		}
	}

	int SelectFlatMirrorCaptureWorker(const RegisteredMirrorCandidate& candidate) noexcept
	{
		const auto id = candidate.reference->GetFormID();
		const auto& assigned = FlatFleet().assigned;

		for (unsigned worker = 0; worker < assigned.size(); ++worker)
			if (assigned[worker] == id)
				return FlatMirrorWorkerBusy(worker) ? -1 : static_cast<int>(worker);
		const bool hero = candidate.definition->captureClass == MirrorDefinitionRegistry::CaptureClass::kHero4096;
		const auto size = ScreenSizedCaptureExtent(id,
			MirrorDefinitionRegistry::CaptureExtent(candidate.definition, id, kPlanarMirrorSize,
				kSidePlanarMirrorSize, [](std::uint32_t authored) { return MirrorSettings::CaptureSize(authored); }));
		const std::array kinds{ DriveKind::kPlanarMirror, DriveKind::kPlanarSideMirror, DriveKind::kPlanarExtraMirror };
		const std::array<unsigned, 3> order = hero ? std::array{0u, 1u, 2u} : std::array{1u, 2u, 0u};

		for (const auto worker : order) {
			const auto& target = ActivePlanarTarget(kinds[worker]);
			if (!FlatMirrorWorkerBusy(worker) && target.Ready() && target.Width() == size.width && target.Height() == size.height)
				return static_cast<int>(worker);
		}
		
		for (const auto worker : order) {
			const auto& target = ActivePlanarTarget(kinds[worker]);
			if (!FlatMirrorWorkerBusy(worker) && !target.Ready())
				return static_cast<int>(worker);
		}
		for (const auto worker : order)
			if (!FlatMirrorWorkerBusy(worker)) return static_cast<int>(worker);
		return -1;
	}

	bool SameMirrorVisibilityRoute(const MirrorRouteState& a, const MirrorRouteState& b) noexcept
	{
		const auto same=[](const auto& x,const auto& y) { return x.x==y.x && x.y==y.y && x.z==y.z; };
		return a.valid==b.valid && same(a.center,b.center) && same(a.normal,b.normal) &&
			same(a.tangent,b.tangent) && same(a.bitangent,b.bitangent) &&
			a.halfWidth==b.halfWidth && a.halfHeight==b.halfHeight && a.halfThickness==b.halfThickness &&
			a.selfDepthMargin==b.selfDepthMargin && a.ellipseMask==b.ellipseMask &&
			a.firstVertex==b.firstVertex && a.vertexCount==b.vertexCount && a.authoredContour==b.authoredContour;
	}

	void SelectFlatMirrorReferences(std::vector<RegisteredMirrorCandidate>& heroes,
		std::vector<RegisteredMirrorCandidate>& standards) noexcept
	{
		PruneFlatMirrorFleet();
		auto& fleet = FlatFleet();
		auto& candidates = g_flatMirrorFrameCandidates.Get();
		candidates.clear();
		candidates.reserve(heroes.size() + standards.size());
		candidates.insert(candidates.end(), heroes.begin(), heroes.end());
		candidates.insert(candidates.end(), standards.begin(), standards.end());
		std::sort(candidates.begin(), candidates.end(), [](const auto& a, const auto& b) {
			if (a.definition->captureClass != b.definition->captureClass)
				return a.definition->captureClass < b.definition->captureClass;
			return RegisteredMirrorCandidateRanksBefore(a, b);
		});
		for (const auto& candidate : candidates) {
			const auto id = candidate.reference->GetFormID();
			auto& resident = fleet.images.Observe(id);
			resident.identity = { candidate.reference->GetHandle(), candidate.definition->id };
			fleet.schedule.Observe(id);
			auto& visibility = fleet.visibility[id];
			if (!SameMirrorVisibilityRoute(visibility.route, candidate.surface.route)) visibility.query.Invalidate();
			visibility.route = candidate.surface.route;
		}

		for (unsigned worker = 0; worker < fleet.assigned.size(); ++worker) {
			const auto found = std::find_if(candidates.begin(), candidates.end(), [&](const auto& candidate) {
				return candidate.reference->GetFormID() == fleet.assigned[worker];
			});
			if (found != candidates.end()) PublishFlatMirrorWorker(*found, worker);
			else if (!FlatMirrorWorkerBusy(worker)) {
				fleet.assigned[worker] = 0;
				if (worker) ClearRegisteredMirrorCandidate(worker == 2);
				else {
					g_cachedMirrorHandle.reset();
					g_mirrorCandidatePlaneValid = false;
					g_mirrorCandidateRoute.valid = false;
				}
			}
		}
		static std::size_t loggedCount = ~std::size_t{};
		if (loggedCount != candidates.size()) {
			loggedCount = candidates.size();
			logger::info("[PlanarMirrors] flat fleet: {} receivers; all due visible mirrors can capture in the same frame; independent images and per-receiver MCM clocks", candidates.size());
		}
	}

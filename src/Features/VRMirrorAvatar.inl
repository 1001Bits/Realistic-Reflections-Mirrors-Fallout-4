

namespace VRMirrorAvatar
{
	constexpr std::size_t kObjects = 2048, kBones = 512;
	template <class T> T& Field(void* object, std::size_t offset) noexcept
	{
		return *reinterpret_cast<T*>(static_cast<std::byte*>(object) + offset);
	}
	bool Type(RE::NiObject* object, const char* name) noexcept
	{
		const auto* type = object ? object->GetRTTI() : nullptr;
		for (unsigned depth = 0; type && depth < 32; ++depth, type = type->GetBaseRTTI())
			if (type->GetName() && std::strcmp(type->GetName(), name) == 0) return true;
		return false;
	}
	struct Pair
	{
		RE::NiPointer<RE::NiAVObject> source, clone;
		void* sourceSkin{};
		void* sourceMaterial{};
		VRMirrorSkin::Array sourceWorlds{};
	};
	struct Tree
	{
		RE::NiAVObject *source{}, *clone{};
		std::byte *sourceEntries{}, *cloneEntries{};
		std::uint32_t count{};
	};
	struct Face
	{
		RE::NiAVObject *source{}, *clone{};
		RE::NiPointer<RE::NiObject> animation;
	};
	struct State
	{
		RE::NiPointer<RE::NiAVObject> source, clone;
		std::vector<Pair> pairs;
		std::vector<Tree> trees;
		std::vector<Face> faces;
		std::vector<VRMirrorPose::LocalPair<RE::NiTransform>> locals;
		RE::NiAVObject* head{};
		RE::NiAVObject* sourceHead{};
		RE::NiPoint3 headAttachment{};
		float headScale{ 1.0f };
		RE::NiMatrix3 headBind{};  
		RE::NiPoint3 headBindTranslate{};
		std::array<RE::NiAVObject*, 4> sourceFeet{};  
		std::uint64_t generation{}, poses{}, legChanges{}, lastLegHash{}, rejectedPoses{}, hmdHeadPoses{}, hmdHeadRejected{}, groundPoses{};
		float groundDelta{};
		bool attachmentValid{}, headBindValid{};
	};
	State* g_state{}; 
	bool g_disabled{};
	RE::NiAVObject* g_retrySource{};
	std::uint64_t g_retryGeneration{};
	std::chrono::steady_clock::time_point g_retryAfter{};
	unsigned g_buildFailures{};
	const char* g_stage{ "idle" };

	bool Collect(RE::NiAVObject* root, std::array<RE::NiAVObject*, kObjects>& objects,
		std::size_t& count) noexcept
	{
		count = 0;
		std::array<RE::NiAVObject*, kObjects> pending{};
		std::size_t size = 0;
		if (root) pending[size++] = root;
		while (size) {
			auto* object = pending[--size];
			if (count == objects.size()) return false;
			objects[count++] = object;
			if (auto* node = object->IsNode()) {
				auto& children = node->GetRuntimeData().children;
				for (std::size_t i = 0; i < children.capacity(); ++i) {
					auto* child = children[static_cast<std::uint16_t>(i)].get();
					if (!child) continue;
					if (child->parent != node || size == pending.size()) return false;
					pending[size++] = child;
				}
			}
		}
		return count != 0;
	}

	RE::NiAVObject* Peer(RE::NiCloningProcess& process, RE::NiAVObject* object)
	{
		if (!object) return nullptr;
		const auto found = process.cloneMap.find(object);
		return found != process.cloneMap.end() && Type(found->second, "NiAVObject") ?
			static_cast<RE::NiAVObject*>(found->second) : nullptr;
	}

	bool PrivateWorld(const State& state, const RE::NiTransform* transform) noexcept
	{
		if (!transform) return false;
		for (const auto& pair : state.pairs)
			if (transform == &pair.clone->world) return true;
		for (const auto& tree : state.trees) {
			const auto address = reinterpret_cast<std::uintptr_t>(transform);
			const auto first = reinterpret_cast<std::uintptr_t>(tree.cloneEntries) + 0x40;
			if (address >= first && address - first < tree.count * 0xA0u && (address - first) % 0xA0u == 0)
				return true;
		}
		return false;
	}

	bool SkinBindingsPrivate(const State& state) noexcept
	{
		for (const auto& pair : state.pairs) {
			if (!pair.sourceSkin) continue;
			auto* skin = Field<void*>(pair.clone.get(), 0x180);
			if (!VRMirrorSkin::PrivateBindings<RE::NiTransform>(pair.sourceSkin, skin, kBones,
				[&](const auto* world) { return PrivateWorld(state, world); })) {
				if (g_buildFailures < 4u || (g_buildFailures % 12u) == 11u) {
					const auto live = VRMirrorSkin::Worlds(pair.sourceSkin), detached = VRMirrorSkin::Worlds(skin);
					logger::warn("[VRReflections] avatar skin rejected: geometry={} liveCount={}/{} cloneCount={}/{} sharedInstance={}",
						pair.source->name.c_str(), live.count, live.capacity, detached.count, detached.capacity, skin == pair.sourceSkin);
				}
				return false;
			}
		}
		return true;
	}

	bool FindHeadAttachment(State& state, RE::NiAVObject* head) noexcept
	{
		if (!head || !head->parent) return false;
		for (const auto& pair : state.pairs) {
			auto* skin = pair.sourceSkin;
			if (!skin) continue;
			const auto worldArray = VRMirrorSkin::Worlds(skin);
			const auto count = worldArray.count;
			const auto* worlds = static_cast<const RE::NiTransform* const*>(worldArray.data);
			auto* data = Field<void*>(skin, 0x40);
			const auto bindArray = VRMirrorSkin::Binds(data);
			if (!count || !worldArray.Valid(kBones) || !bindArray.Valid(kBones) || bindArray.count < count) continue;
			const auto* binds = static_cast<const std::byte*>(bindArray.data);
			std::uint32_t child = count, parent = count;
			for (std::uint32_t i = 0; i < count; ++i) {
				if (worlds[i] == &head->world) child = i;
				if (worlds[i] == &head->parent->world) parent = i;
			}
			if (child < count && parent < count) {
				const auto childBind = VRMirrorSkin::Read<RE::NiTransform>(binds, child * 0x50u + 0x10u);
				if (VRMirrorPose::BindAttachment(
						VRMirrorSkin::Read<RE::NiTransform>(binds, parent * 0x50u + 0x10u),
						childBind, state.headAttachment, state.headScale)) {
					state.headBind = childBind.rotate;
					state.headBindTranslate = childBind.translate;
					state.headBindValid = VRMirrorPose::Finite(childBind);
					return true;
				}
			}
		}
		return false;
	}

	struct Trigger
	{
		const wchar_t* path;
		bool present{};
		std::uint32_t poll{};
		bool Present() noexcept
		{
			if ((poll++ % 256u) == 0u) present = GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
			return present;
		}
	};
	Trigger g_frikHeadTrigger{ L"Data\\dynref_vrfrikhead" };
	Trigger g_noGroundTrigger{ L"Data\\dynref_vrnoground" };
	Trigger g_hideWeaponTrigger{ L"Data\\dynref_vrhideweapon" };

	bool KeepFrikHeadRotation() noexcept { return g_frikHeadTrigger.Present(); }

	std::string MatrixText(const RE::NiMatrix3& m)
	{
		return fmt::format("[{:.4f},{:.4f},{:.4f}|{:.4f},{:.4f},{:.4f}|{:.4f},{:.4f},{:.4f}]",
			m.entry[0][0], m.entry[0][1], m.entry[0][2], m.entry[1][0], m.entry[1][1], m.entry[1][2],
			m.entry[2][0], m.entry[2][1], m.entry[2][2]);
	}

	void Multiply(const RE::NiMatrix3& a, const RE::NiMatrix3& b, RE::NiMatrix3& out) noexcept
	{
		RE::NiMatrix3 result{};
		for (unsigned row = 0; row < 3; ++row)
			for (unsigned col = 0; col < 3; ++col) {
				float sum = 0.0f;
				for (unsigned k = 0; k < 3; ++k) sum += a.entry[row][k] * b.entry[k][col];
				result.entry[row][col] = sum;
			}
		out = result;
	}

	void Transpose(const RE::NiMatrix3& a, RE::NiMatrix3& out) noexcept
	{
		RE::NiMatrix3 result{};
		for (unsigned row = 0; row < 3; ++row)
			for (unsigned col = 0; col < 3; ++col) result.entry[row][col] = a.entry[col][row];
		out = result;
	}

	bool FiniteMatrix(const RE::NiMatrix3& m) noexcept
	{
		for (unsigned row = 0; row < 3; ++row)
			for (unsigned col = 0; col < 3; ++col)
				if (!std::isfinite(m.entry[row][col])) return false;
		return true;
	}

	float AngleDegrees(const float* a, const float* b) noexcept
	{
		const float dot = a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
		const float la = std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
		const float lb = std::sqrt(b[0] * b[0] + b[1] * b[1] + b[2] * b[2]);
		if (la < 1.0e-6f || lb < 1.0e-6f) return 180.0f;
		return std::acos(std::clamp(dot / (la * lb), -1.0f, 1.0f)) * 57.2957795f;
	}

	bool HmdHeadLocalRotation(const State& state, RE::NiMatrix3& localRotate, RE::NiMatrix3& hmdFrame) noexcept
	{
		if (!state.sourceHead || !state.sourceHead->parent) return false;
		const auto& basis = g_mainEyeBasis[0];  
		const float f[3]{ basis.entry[0][0], basis.entry[0][1], basis.entry[0][2] };
		const float u[3]{ basis.entry[1][0], basis.entry[1][1], basis.entry[1][2] };
		const float r[3]{ f[1] * u[2] - f[2] * u[1], f[2] * u[0] - f[0] * u[2], f[0] * u[1] - f[1] * u[0] };
		const float lf = f[0] * f[0] + f[1] * f[1] + f[2] * f[2];
		const float lu = u[0] * u[0] + u[1] * u[1] + u[2] * u[2];
		const float lr = r[0] * r[0] + r[1] * r[1] + r[2] * r[2];
		const float fu = f[0] * u[0] + f[1] * u[1] + f[2] * u[2];
		if (!std::isfinite(lf) || !std::isfinite(lu) || !std::isfinite(lr) || !std::isfinite(fu) ||
			std::fabs(lf - 1.0f) > 0.05f || std::fabs(lu - 1.0f) > 0.05f || lr < 0.9f || std::fabs(fu) > 0.05f)
			return false;
		RE::NiMatrix3 frame{};
		frame.entry[0] = { r[0], r[1], r[2], 0.0f };  
		frame.entry[1] = { f[0], f[1], f[2], 0.0f };  
		frame.entry[2] = { u[0], u[1], u[2], 0.0f };  

		RE::NiMatrix3 headWorld{}, parentInverse{}, local{};
		headWorld.entry[0] = { u[0], u[1], u[2], 0.0f };
		headWorld.entry[1] = { f[0], f[1], f[2], 0.0f };
		headWorld.entry[2] = { -r[0], -r[1], -r[2], 0.0f };
		const auto& parentWorld = state.sourceHead->parent->world.rotate;  
		if (!FiniteMatrix(parentWorld)) return false;
		Transpose(parentWorld, parentInverse);
		Multiply(headWorld, parentInverse, local);                        
		if (!FiniteMatrix(local)) return false;
		localRotate = local;
		hmdFrame = frame;
		return true;
	}

	bool CopyFace(Face& face) noexcept
	{
		auto* live = Field<void*>(face.source, 0x1B0);
		if (!live || !face.animation || live == face.animation.get() ||
			Field<void*>(face.clone, 0x1B0) != face.animation.get()) return false;
		MirrorFaceGenSnapshot snapshot;
		auto* lock = reinterpret_cast<RE::BSSpinLock*>(static_cast<std::byte*>(live) + 0x2B4);
		bool valid = false;
		lock->lock("MirrorsOfFalloutVRFaceSnapshot");
		__try { valid = snapshot.Read(live); }
		__finally { lock->unlock(); }
		if (valid) snapshot.Apply(face.animation.get(), Field<std::uint16_t>(face.clone, 0x1BC));
		return valid;
	}

	bool Build(State& state, RE::NiAVObject* source, std::uint64_t generation)
	{
		g_stage = "clone";
		state.source.reset(source);
		state.generation = generation;
		RE::NiCloningProcess process{};
		process.copyType = RE::NiCloningProcess::CopyType::kCopyUnique;
		process.scale = { 1.0f, 1.0f, 1.0f };
		auto* object = NiVirtualDispatch::CreateClone(source, process);
		if (!Type(object, "NiAVObject") || object == source) return false;
		state.clone.reset(static_cast<RE::NiAVObject*>(object));
		NiVirtualDispatch::ProcessClone(source, process);
		if (state.clone->parent) return false;
		g_stage = "clone-map";
		std::array<RE::NiAVObject*, kObjects> objects{};
		std::size_t count = 0;
		if (!Collect(source, objects, count)) return false;
		state.pairs.reserve(count);
		state.locals.reserve(count + kBones);
		RE::NiAVObject* sourceHead = nullptr;
		for (std::size_t i = 0; i < count; ++i) {
			auto* live = objects[i];
			auto* clone = Peer(process, live);
			if (!clone || clone == live || clone->GetRTTI() != live->GetRTTI() ||
				(live != source && clone->parent != Peer(process, live->parent))) return false;
			Pair pair{ RE::NiPointer<RE::NiAVObject>{live}, RE::NiPointer<RE::NiAVObject>{clone} };
			if (live->IsGeometry()) {
				pair.sourceSkin = Field<void*>(live, 0x180);
				pair.sourceMaterial = Field<void*>(live, 0x178);
				pair.sourceWorlds = VRMirrorSkin::Worlds(pair.sourceSkin);
			}
			state.pairs.push_back(std::move(pair));
			if (live != source) state.locals.push_back({ &live->local, &clone->local });
			if (NodeNameIs(live, "Head")) { sourceHead = live; state.head = clone; state.sourceHead = live; }
			if (NodeNameIs(live, "LLeg_Toe1")) state.sourceFeet[0] = live;
			if (NodeNameIs(live, "RLeg_Toe1")) state.sourceFeet[1] = live;
			if (NodeNameIs(live, "LLeg_Foot")) state.sourceFeet[2] = live;
			if (NodeNameIs(live, "RLeg_Foot")) state.sourceFeet[3] = live;
			if (Type(live, "BSFlattenedBoneTree")) {
				Tree tree{ live, clone, Field<std::byte*>(live, 0x188), Field<std::byte*>(clone, 0x188),
					Field<std::uint32_t>(live, 0x180) };
				if (!tree.count || tree.count > kBones || tree.count != Field<std::uint32_t>(clone, 0x180) ||
					!tree.sourceEntries || !tree.cloneEntries || tree.sourceEntries == tree.cloneEntries) return false;
				for (std::uint32_t bone = 0; bone < tree.count; ++bone) {
					auto* entry = tree.sourceEntries + bone * 0xA0u;
					auto* target = tree.cloneEntries + bone * 0xA0u;
					auto* node = Field<RE::NiAVObject*>(entry, 0x88);
					if (Field<void*>(entry, 0x90) != Field<void*>(target, 0x90) ||
						Field<RE::NiAVObject*>(target, 0x88) != Peer(process, node)) return false;

					if (!node) state.locals.push_back({ reinterpret_cast<RE::NiTransform*>(entry),
						reinterpret_cast<RE::NiTransform*>(target) });
				}
				state.trees.push_back(tree);
			}
			if (Type(live, "BSDynamicTriShape") && Field<void*>(live, 0x1C0) &&
				Field<void*>(live, 0x1C0) == Field<void*>(clone, 0x1C0)) return false;
			if (Type(live, "BSFaceGenNiNode")) {
				g_stage = "face-animation";
				constexpr std::uintptr_t ctor = 0x6567C0;
				constexpr std::uint8_t entry[]{0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57};
				const auto address = ReflectionRuntime::Address(ctor);
				if (std::memcmp(reinterpret_cast<const void*>(address), entry, sizeof(entry)) != 0) return false;
				auto* storage = RE::malloc(0x2E8);
				if (!storage) return false;
				using Construct = RE::NiObject* (*)(void*);
				Face face{ live, clone, RE::NiPointer<RE::NiObject>{reinterpret_cast<Construct>(address)(storage)} };
				Field<RE::NiPointer<RE::NiObject>>(clone, 0x1B0).reset(face.animation.get());
				state.faces.push_back(std::move(face));
			}
		}
		g_stage = "skin-remap";
		if (state.trees.empty() || !SkinBindingsPrivate(state)) return false;
		g_stage = "pose-mapping";
		if (!VRMirrorPose::ValidateMapping<RE::NiTransform>(state.locals)) return false;
		state.attachmentValid = FindHeadAttachment(state, sourceHead);
		if (g_frik.present && sourceHead && !state.attachmentValid) { g_stage = "head-bind-attachment"; return false; }
		logger::info("[VRReflections] private avatar built: source={} clone={} objects={} trees={} locals={} faces={} headBind={} generation={}",
			fmt::ptr(source), fmt::ptr(state.clone.get()), state.pairs.size(), state.trees.size(), state.locals.size(),
			state.faces.size(), state.attachmentValid, generation);
		return true;
	}

	bool SourceMatches(const State& state, RE::NiAVObject* source, std::uint64_t generation) noexcept
	{
		if (source != state.source.get() || generation != state.generation) return false;
		std::array<RE::NiAVObject*, kObjects> objects{};
		std::size_t count = 0;
		if (!Collect(source, objects, count) || count != state.pairs.size()) return false;
		for (std::size_t i = 0; i < count; ++i) {
			const auto& pair = state.pairs[i];
			if (objects[i] != pair.source.get()) return false;
			if (objects[i]->IsGeometry() && (Field<void*>(objects[i], 0x180) != pair.sourceSkin ||
				Field<void*>(objects[i], 0x178) != pair.sourceMaterial)) return false;
			if (pair.sourceSkin) {
				const auto worlds = VRMirrorSkin::Worlds(pair.sourceSkin);

				if (worlds.data != pair.sourceWorlds.data || worlds.count != pair.sourceWorlds.count) return false;
			}
		}
		for (const auto& tree : state.trees)
			if (Field<std::uint32_t>(tree.source, 0x180) != tree.count ||
				Field<std::byte*>(tree.source, 0x188) != tree.sourceEntries) return false;
		return true;
	}

	bool Update(State& state) noexcept
	{
		g_stage = "complete-pose";
		if (!VRMirrorPose::Finite(state.source->world) ||
			!VRMirrorPose::CopyValidatedLocals<RE::NiTransform>(state.locals)) return false;

		if (g_frik.present) {
			for (const auto& pair : state.pairs) {
				if (pair.source.get() == state.source.get() || !pair.source->parent) continue;

				const char* name = pair.source->name.c_str();
				if (name && std::strstr(name, "Finger")) continue;
				const auto& parent = pair.source->parent->world;
				if (parent.scale >= 0.0001f &&
					!VRMirrorPose::LocalFromWorld(parent, pair.source->world, pair.clone->local)) return false;
			}
		}
		state.clone->local = state.source->world;
		state.clone->world = state.source->world;

		if (g_frik.present && state.head && state.attachmentValid) {
			state.head->local.translate = state.headAttachment;
			state.head->local.scale = state.headScale;
		}

		if (state.head && state.sourceHead) {
			const bool apply = !KeepFrikHeadRotation();
			RE::NiMatrix3 localRotate{}, hmdFrame{};
			if (HmdHeadLocalRotation(state, localRotate, hmdFrame)) {
				const bool report = state.hmdHeadPoses < 3u || (state.hmdHeadPoses % 1800u) == 0u;
				if (report) {

					auto* player = RE::PlayerCharacter::GetSingleton();
					const auto& rootWorld = state.source->world;
					const RE::NiAVObject* rootChild = nullptr;
					if (auto* rootNode = state.source->IsNode()) {
						auto& children = rootNode->GetRuntimeData().children;
						rootChild = children.capacity() ? children[0].get() : nullptr;
					}
					logger::info("[VRReflections] HMDHEAD-RAW apply={} bind={} liveHeadWorld={} liveHeadLocal={} neckWorld={} basis={} bindT=({:.3f},{:.3f},{:.3f}) attachT=({:.3f},{:.3f},{:.3f}) headLocalT=({:.3f},{:.3f},{:.3f})",
						apply, MatrixText(state.headBind), MatrixText(state.sourceHead->world.rotate),
						MatrixText(state.sourceHead->local.rotate), MatrixText(state.sourceHead->parent->world.rotate),
						MatrixText(g_mainEyeBasis[0]), state.headBindTranslate.x, state.headBindTranslate.y, state.headBindTranslate.z,
						state.headAttachment.x, state.headAttachment.y, state.headAttachment.z,
						state.sourceHead->local.translate.x, state.sourceHead->local.translate.y, state.sourceHead->local.translate.z);
					logger::info("[VRReflections] HEIGHT rootWorld=({:.1f},{:.1f},{:.1f}) rootScale={:.3f} rootChild='{}' childWorldZ={:.1f} childLocalZ={:.1f} childScale={:.3f} neckWorldZ={:.1f} headWorldZ={:.1f} playerLocZ={:.1f} eyeZ={:.1f} frikRig={}",
						rootWorld.translate.x, rootWorld.translate.y, rootWorld.translate.z, rootWorld.scale,
						rootChild && rootChild->name.c_str() ? rootChild->name.c_str() : "?",
						rootChild ? rootChild->world.translate.z : 0.0f, rootChild ? rootChild->local.translate.z : 0.0f,
						rootChild ? rootChild->local.scale : 0.0f,
						state.sourceHead->parent->world.translate.z, state.sourceHead->world.translate.z,
						player ? player->data.location.z : 0.0f, g_mainEye[0].z, static_cast<const void*>(g_frikRigRoot));

					RE::NiMatrix3 ourWorld{};
					Multiply(localRotate, state.sourceHead->parent->world.rotate, ourWorld);
					const RE::NiMatrix3& liveMeshToWorld = state.sourceHead->world.rotate;
					const RE::NiMatrix3& ourMeshToWorld = ourWorld;
					const float hmdForward[3]{ hmdFrame.entry[1][0], hmdFrame.entry[1][1], hmdFrame.entry[1][2] };
					const float liveForward[3]{ liveMeshToWorld.entry[1][0], liveMeshToWorld.entry[1][1], liveMeshToWorld.entry[1][2] };
					const float ourForward[3]{ ourMeshToWorld.entry[1][0], ourMeshToWorld.entry[1][1], ourMeshToWorld.entry[1][2] };
					logger::info("[VRReflections] HMDHEAD pose={} hmdForward=({:.3f},{:.3f},{:.3f}) liveHeadError={:.1f}deg ourHeadError={:.1f}deg frik={} liveHeadForward=({:.3f},{:.3f},{:.3f})",
						state.hmdHeadPoses, hmdForward[0], hmdForward[1], hmdForward[2],
						AngleDegrees(hmdForward, liveForward), AngleDegrees(hmdForward, ourForward), g_frik.present,
						liveForward[0], liveForward[1], liveForward[2]);
				}
				if (apply)
					state.head->local.rotate = localRotate;
				++state.hmdHeadPoses;
			} else if ((++state.hmdHeadRejected % 300u) == 1u) {
				logger::warn("[VRReflections] HMDHEAD headset basis unusable; live head rotation kept (count={})", state.hmdHeadRejected);
			}
		}

		auto* weaponOwner = RE::PlayerCharacter::GetSingleton();
		const bool hideHandWeapon = g_hideWeaponTrigger.Present() || !weaponOwner || !weaponOwner->GetWeaponMagicDrawn();
		for (const auto& pair : state.pairs) {
			const bool hiddenWeapon = IsFrikHiddenHandWeapon(pair.source.get());
			NiVirtualDispatch::SetAppCulled(pair.clone, (hiddenWeapon && hideHandWeapon) || pair.source.get() == g_frikRigRoot);
		}

		state.groundDelta = 0.0f;
		if (!g_noGroundTrigger.Present()) {
			auto* player = RE::PlayerCharacter::GetSingleton();
			float lowest = (std::numeric_limits<float>::max)();
			for (std::size_t i = 0; i < state.sourceFeet.size(); ++i) {
				const auto* foot = state.sourceFeet[i];
				if (!foot) continue;
				if (i >= 2 && (state.sourceFeet[0] || state.sourceFeet[1])) continue;  
				const float sole = foot->world.translate.z - (i >= 2 ? 6.5f : 0.0f);
				if (std::isfinite(sole)) lowest = (std::min)(lowest, sole);
			}
			if (player && lowest != (std::numeric_limits<float>::max)() && std::isfinite(player->data.location.z)) {
				const float delta = std::clamp(lowest - player->data.location.z, -40.0f, 40.0f);
				if (std::fabs(delta) > 0.5f) {
					state.groundDelta = delta;
					state.clone->local.translate.z -= delta;
					state.clone->world.translate.z -= delta;
				}
				if (state.groundPoses < 3u || (state.groundPoses % 1800u) == 0u)
					logger::info("[VRReflections] GROUND lowestSole={:.1f} actorZ={:.1f} delta={:.1f} rootZ={:.1f} toes={}/{} feet={}/{}",
						lowest, player->data.location.z, delta, state.source->world.translate.z,
						state.sourceFeet[0] != nullptr, state.sourceFeet[1] != nullptr,
						state.sourceFeet[2] != nullptr, state.sourceFeet[3] != nullptr);
				++state.groundPoses;
			}
		}
		g_stage = "face-snapshot";
		for (auto& face : state.faces) if (!CopyFace(face)) return false;
		g_stage = "private-world-update";
		RE::NiUpdateData update{};
		NiVirtualDispatch::UpdateDownwardPass(state.clone, update, 0);
		MirrorSceneRenderer::SuppressPrivateGoreCapsForSubmission(state.clone.get());
		std::uint64_t legHash = 14695981039346656037ull;
		for (const auto& pair : state.pairs) {
			const char* name = pair.clone->name.c_str();
			if (!name || (!std::strstr(name, "Leg_") && !NodeNameIs(pair.clone.get(), "COM"))) continue;
			const auto* bytes = reinterpret_cast<const std::uint8_t*>(&pair.clone->local);
			for (std::size_t i = 0; i < sizeof(RE::NiTransform); ++i) legHash = (legHash ^ bytes[i]) * 1099511628211ull;
		}
		if (state.poses && legHash != state.lastLegHash) ++state.legChanges;
		state.lastLegHash = legHash;
		if ((++state.poses % 100u) == 1u)
			logger::info("[VRReflections] private avatar pose: updates={} legChanges={} locals={} headAttached={} livePoseUntouched=true",
				state.poses, state.legChanges, state.locals.size(), state.attachmentValid);
		g_stage = "ready";
		return true;
	}

	void Reset() noexcept
	{

		if (g_disabled) return;
		__try { delete g_state; g_state = nullptr; }
		__except (EXCEPTION_EXECUTE_HANDLER) { g_disabled = true; }
	}
	RE::NiAVObject* PrepareUnsafe(RE::NiAVObject* source)
	{
		if (!source || g_disabled) return nullptr;
		const auto generation = MirrorSceneRenderer::LoadGeneration();
		if (source != g_retrySource || generation != g_retryGeneration) {
			g_retrySource = source;
			g_retryGeneration = generation;
			g_buildFailures = 0;
			g_retryAfter = {};
		}
		if (g_state && !SourceMatches(*g_state, source, generation)) Reset();
		if (g_disabled) return nullptr;
		if (!g_state) {

			const auto now = std::chrono::steady_clock::now();
			if (now < g_retryAfter) return nullptr;
			g_state = new State();
			if (!Build(*g_state, source, generation)) {
				if (++g_buildFailures <= 4u || (g_buildFailures % 12u) == 0u)
					logger::warn("[VRReflections] private avatar incomplete at {}; attempt={} retry in 5 seconds", g_stage, g_buildFailures);
				Reset();
				g_retryAfter = now + std::chrono::seconds(5);
				return nullptr;
			}
			g_buildFailures = 0;
		}
		if (!Update(*g_state)) {
			if ((++g_state->rejectedPoses % 100u) == 1u)
				logger::warn("[VRReflections] private avatar pose rejected at {}; count={}", g_stage, g_state->rejectedPoses);
			return nullptr;
		}
		return g_state->clone.get();
	}
	RE::NiAVObject* Prepare(RE::NiAVObject* source) noexcept
	{
		__try { return PrepareUnsafe(source); }
		__except (EXCEPTION_EXECUTE_HANDLER) {
			g_disabled = true;
			logger::error("[VRReflections] private avatar native fault at {}; clone quarantined", g_stage);
			return nullptr;
		}
	}
}

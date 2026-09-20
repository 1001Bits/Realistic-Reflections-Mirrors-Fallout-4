
bool ReadPrivatePlayerTransientRoots(RE::NiAVObject* root, PrivatePlayerTransientRoots& roots) noexcept
{
    roots = {};
    if (REL::Module::IsVR()) return true;
    __try {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player || !player->loadedData || player->loadedData->data3D.get() != root) return true;

        std::size_t count = 0;
        if (const auto* biped = player->biped.get()) {
            for (const auto& part : biped->object) {
                if (!part.partClone || !part.parent.object ||
                    part.parent.object->GetFormType() != RE::ENUM_FORM_ID::kWEAP) continue;
                const auto type = static_cast<RE::TESObjectWEAP*>(part.parent.object)->weaponData.type.get();
                if (type != RE::WEAPON_TYPE::kGrenade && type != RE::WEAPON_TYPE::kMine) continue;
                const auto* art = part.partClone.get();
                if (art == root) return false;
                if (art == roots[0] || art == roots[1]) continue;
                if (count == 2) return false;
                roots[count++] = art;
            }
        }
        PlayerMuzzleFlashState flash{};
        if (ReadPlayerMuzzleFlashSEH(player, &flash) < 0) return false;
        roots[2] = flash.art;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { roots = {}; return false; }
}

bool RemoveInitialPrivatePlayerTransientArt(RE::NiAVObject* source, RE::NiAVObject* clone,
    RE::NiCloningProcess& process) noexcept
{
    PrivatePlayerTransientRoots roots{};
    if (!ReadPrivatePlayerTransientRoots(source, roots)) return false;
    for (const auto* art : roots) {
        if (!art) continue;
        const auto found = process.cloneMap.find(const_cast<RE::NiAVObject*>(art));
        if (found == process.cloneMap.end()) continue;
        auto* copied = netimmerse_cast<RE::NiAVObject*>(found->second);
        if (!copied || copied == art || copied == clone) return false;
        RE::NiPointer<RE::NiAVObject> detached;
        if (copied->parent && DetachMuzzleFlashCloneSEH(copied->parent, copied,
            reinterpret_cast<RE::NiAVObject**>(&detached)) != 1) return false;
    }
    return true;
}

bool ReleasePrivatePlayerThrowable(PrivatePlayerThrowable& part) noexcept
{
    if (part.clone && part.clone->parent) {
        RE::NiPointer<RE::NiAVObject> detached;
        if (DetachMuzzleFlashCloneSEH(part.parent.get(), part.clone.get(),
            reinterpret_cast<RE::NiAVObject**>(&detached)) != 1) return false;
    }
    part = {};
    return true;
}

bool SynchronizePrivatePlayerThrowables(PrivatePlayerBodyResources* visual) try
{
    PrivatePlayerTransientRoots roots{};
    if (!ReadPrivatePlayerTransientRoots(visual->sourceThirdPersonRoot.get(), roots)) return false;
    for (std::size_t index = 0; index < visual->throwables.size(); ++index) {
        auto& part = visual->throwables[index];
        auto* art = const_cast<RE::NiAVObject*>(roots[index]);
        if (art && !PrivatePlayerNodeDescendsFromNoexcept(art, visual->sourceThirdPersonRoot.get())) art = nullptr;
        if (part.source.get() != art || (part.clone && !part.clone->parent)) {
            visual->transientPaneGeometryDirty = true;
            if (!ReleasePrivatePlayerThrowable(part)) return false;
            if (art) {
                RE::NiAVObject* parent = nullptr;
                RE::NiCloningProcess process{};
                process.copyType = RE::NiCloningProcess::CopyType::kCopyUnique;
                process.scale = {1, 1, 1};

                auto* liveTree = FindFlattenedBoneTreeFlat(visual->sourceThirdPersonRoot.get());
                const auto bones = FlattenedBoneTreeEntryCount(visual->cloneTree);
                std::uint64_t order{};
                if (!bones || !HashFlattenedBoneTreeNameOrder(liveTree, bones, order) ||
                    order != visual->skeletonOrderHash) return false;
                const auto* liveEntries = *reinterpret_cast<const std::uint8_t* const*>(
                    reinterpret_cast<const std::uint8_t*>(liveTree) + 0x148);
                const auto* cloneEntries = *reinterpret_cast<const std::uint8_t* const*>(
                    reinterpret_cast<const std::uint8_t*>(visual->cloneTree) + 0x148);
                process.cloneMap.insert({visual->sourceThirdPersonRoot.get(), visual->cloneRoot.get()});
                for (std::uint32_t bone = 0; bone < bones; ++bone) {
                    auto* from = *reinterpret_cast<RE::NiAVObject* const*>(liveEntries + bone * 0xA0u + 0x88u);
                    auto* to = *reinterpret_cast<RE::NiAVObject* const*>(cloneEntries + bone * 0xA0u + 0x88u);
                    if (from && to) process.cloneMap.insert({from, to});
                }
                const auto parentIt = process.cloneMap.find(art->parent);
                if (parentIt != process.cloneMap.end()) parent = netimmerse_cast<RE::NiAVObject*>(parentIt->second);
                if (!parent && (art->parent->name.empty() ||
                    ResolvePrivatePlayerNodeByNameSEH(visual->cloneRoot.get(), art->parent->name, &parent) != 1)) return false;
                if (!parent) return false;
                RE::NiPointer<RE::NiAVObject> copy(static_cast<RE::NiAVObject*>(ClonePlayerVisualTree(art, &process)));
                if (!copy || copy.get() == art) return false;
                std::uint32_t mappedGeometry{};
                for (const auto& pair : process.cloneMap) {
                    auto* from = netimmerse_cast<RE::BSGeometry*>(pair.first);
                    if (!from || !PrivatePlayerNodeDescendsFromNoexcept(from, art)) continue;
                    if (pair.first == pair.second || !netimmerse_cast<RE::BSGeometry*>(pair.second)) return false;
                    ++mappedGeometry;
                }
                PrivatePlayerGeometryMaterialCensus liveCensus{}, copyCensus{};
                if (!ReadPrivatePlayerGeometryMaterialCensus(art, nullptr, nullptr, &liveCensus) ||
                    !ReadPrivatePlayerGeometryMaterialCensus(copy.get(), nullptr, nullptr, &copyCensus) ||
                    !PrivatePlayerGeometryMaterialCensusesMatch(liveCensus, copyCensus) ||
                    mappedGeometry != liveCensus.geometryCount) return false;
                if (AttachMuzzleFlashCloneSEH(parent, copy.get()) != 1) return false;
                part.source.reset(art); part.clone = std::move(copy); part.parent.reset(parent);
            }
            if (visual->throwableLogs++ < 8u)
                logger::info("[MirrorPlayerAnim] transient thrown-weapon visual {}: slot={} generation={}; behavior graph retained",
                    art ? "attached" : "detached", index, visual->stateGeneration);
        }
        if (art && part.clone) {
            part.clone->local = art->local;
            NiVirtualDispatch::SetAppCulled(part.clone.get(), art->GetAppCulled());
        }
    }
    return true;
} catch (...) { return false; }

bool RefreshPrivatePlayerPaneGeometry(PrivatePlayerBodyResources* visual) noexcept
{
    static thread_local std::array<PrivatePlayerGeometrySample, kMaxPrivatePlayerGeometrySamples> samples{};
    bool truncated{};
    const auto count = CollectPrivatePlayerGeometryInventory(visual->cloneRoot.get(), visual->cloneFaceRoot.get(),
        visual->cloneHeadRoot.get(), samples.data(), static_cast<std::uint32_t>(samples.size()), nullptr, &truncated);
    if (truncated || !count || count > visual->paneClearanceGeometry.size()) return false;
    visual->paneClearanceGeometryCount = count;
    for (std::uint32_t index = 0; index < count; ++index)
        visual->paneClearanceGeometry[index] = const_cast<RE::BSGeometry*>(static_cast<const RE::BSGeometry*>(samples[index].geometry));
    return true;
}

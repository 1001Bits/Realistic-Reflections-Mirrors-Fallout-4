

constexpr float kPaneClearanceMargin = 6.0f;

bool AccumulatePaneSkinClearance(RE::BSGeometry* geometry,
    const PlanarMirrorMath::Plane& plane, float& required) noexcept
{
    __try {
        if (!geometry || geometry->GetAppCulled()) return true;
        const auto accumulate = [&](const RE::NiBound& local, const RE::NiTransform& world) noexcept {
            if (!std::isfinite(local.fRadius) || local.fRadius < 0.0f || !FinitePrivatePlayerTransform(world)) return false;
            if (local.fRadius == 0.0f) return true; 

            const auto center = world.rotate.Transpose() * local.center * world.scale + world.translate;
            float distance = 0.0f;
            if (!PlanarMirrorMath::SphereClearance(plane, {center.x, center.y, center.z},
                    local.fRadius * std::abs(world.scale), kPaneClearanceMargin, distance)) return false;
            required = (std::max)(required, distance);
            return true;
        };
        auto* skin = geometry->skinInstance.get();
        if (!skin) return accumulate(geometry->modelBound, geometry->world);
        const auto worlds = VRMirrorSkin::Worlds(skin);
        const auto binds = VRMirrorSkin::Binds(VRMirrorSkin::Read<const void*>(skin, 0x40u));
        if (!worlds.Valid(kMaxPlayerNaturalPoseBones) || !binds.Valid(kMaxPlayerNaturalPoseBones) ||
            !worlds.count || worlds.count > binds.count) return false;
        const auto* transforms = static_cast<const RE::NiTransform* const*>(worlds.data);
        for (std::uint32_t index = 0; index < worlds.count; ++index) {
            if (!transforms[index]) continue; 
            const auto bound = VRMirrorSkin::Read<RE::NiBound>(binds.data, index * 0x50u);
            if (!accumulate(bound, *transforms[index])) return false;
        }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

RE::NiAVObject* g_armPaneClearanceRoot = nullptr;
RE::NiTransform g_armPaneClearanceLocal{};
RE::NiTransform g_armPaneClearanceWorld{};

void RestorePrivatePaneClearance() noexcept
{
    auto* root = g_armPaneClearanceRoot;
    g_armPaneClearanceRoot = nullptr;
    if (!root) return;
    __try {
        root->local = g_armPaneClearanceLocal;
        root->world = g_armPaneClearanceWorld;
        RE::NiUpdateData update{};
        NiVirtualDispatch::UpdateDownwardPass(root, update, 0);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_privatePlayerBodyTeardownRequested.store(true, std::memory_order_release);
        g_privatePlayerBodyGeneration.fetch_add(1u, std::memory_order_acq_rel);
    }
}

bool ArmPrivatePaneClearance(const PrivatePlayerRenderClone& clone,
    PlanarMirrorMath::Plane plane, RE::NiUpdateData& update) noexcept
{
    if (!clone.root || !g_armPrivatePlayerBodyRenderLease || g_armPaneClearanceRoot ||
        !PlanarMirrorMath::NormalizePlane(plane)) return false;
    float required = 0.0f;
    for (std::uint32_t i = 0; i < clone.paneClearanceGeometryCount; ++i) {
        if (!AccumulatePaneSkinClearance(clone.paneClearanceGeometry[i], plane, required)) return false;
    }
    if (required == 0.0f) return true;
    __try {
        const RE::NiPoint3 shift{plane.normal.x * required, plane.normal.y * required, plane.normal.z * required};
        RE::NiPoint3 localShift = shift;
        if (const auto* parent = clone.root->parent) {
            if (!FinitePrivatePlayerTransform(parent->world) || std::abs(parent->world.scale) < 1.0e-5f) return false;

            localShift = parent->world.rotate * shift / parent->world.scale;
        }

        g_armPaneClearanceLocal = clone.root->local;
        g_armPaneClearanceWorld = clone.root->world;
        g_armPaneClearanceRoot = clone.root;
        clone.root->local.translate += localShift;
        clone.root->world.translate += shift;
        NiVirtualDispatch::UpdateDownwardPass(clone.root, update, 0);

        float remaining = 0.0f;
        for (std::uint32_t i = 0; i < clone.paneClearanceGeometryCount; ++i) {
            if (!AccumulatePaneSkinClearance(clone.paneClearanceGeometry[i], plane, remaining)) {
                RestorePrivatePaneClearance();
                return false;
            }
        }
        if (remaining > 0.02f) {
            static std::uint32_t failedLogs = 0u;
            if (failedLogs++ < 8u) logger::info("[ReflectionPlayerBody] PANECLR pose did not follow private root: remaining={:.3f}; capture withheld", remaining);
            RestorePrivatePaneClearance();
            return false;
        }
        static std::uint32_t logs = 0u;
        if (logs++ < 8u) logger::info("[ReflectionPlayerBody] PANECLR animated head clearance={:.3f} parts={}; private placement only",
            required, clone.paneClearanceGeometryCount);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        RestorePrivatePaneClearance();
        return false;
    }
}



struct CompletedMirrorCamera
{
    RE::NiTransform local{},world{},previousWorld{};
    float worldToCam[4][4]{};
    RE::NiFrustum frustum{};
    RE::NiRect<float> port{};
    float minNear{},maxRatio{},lod{};
    RE::NiPoint3 eye{};
    std::uint32_t frame{};
    std::uint64_t source{};
};

struct CompletedPlayerPosePool
{
    struct NodePair { RE::NiAVObject* source{};RE::NiAVObject* target{}; };
    struct Slot {
        std::unique_ptr<PrivatePlayerBodyResources> visual;
        std::vector<NodePair> nodes;
        CompletedMirrorCamera camera;
    };
    MirrorPoseExchange exchange;
    std::array<Slot,MirrorPoseExchange::Slots> slots;
};
std::atomic<std::shared_ptr<CompletedPlayerPosePool>> g_completedPlayerPoses;

std::shared_ptr<CompletedPlayerPosePool> g_selectedPlayerPoses;
std::shared_ptr<CompletedPlayerPosePool> g_materialQuarantinedCompletedPoses;
int g_selectedPlayerPoseSlot=MirrorPoseExchange::None;
PrivatePlayerBodyResources* g_armedCompletedPlayerVisual{};

bool ReadCompletedMirrorCamera(CompletedMirrorCamera& camera,std::uint64_t source) noexcept;
bool BuildCompletedPlayerPosePool(PrivatePlayerBodyResources* source);
void PublishCompletedPlayerPose(PrivatePlayerBodyResources* source) noexcept;

PrivatePlayerBodyResources* ArmedPrivatePlayerResources() noexcept
{
    return g_armedCompletedPlayerVisual ? g_armedCompletedPlayerVisual : g_privatePlayerBody.resources;
}

void RetireCompletedPlayerPoses(PrivatePlayerBodyResources* resources) noexcept
{

    auto retired = std::move(resources->completedPoses);
    if (!retired) return;
    auto expected = retired;
    g_completedPlayerPoses.compare_exchange_strong(expected, {}, std::memory_order_acq_rel);
}

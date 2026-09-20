// Native, detached cloth: definitions are shared read-only; instances, transform
// sets, particles and scratch storage belong exclusively to the animation clone.
// The world is never registered with bhkWorld or the game's cloth job lists.
#include "MirrorClothSimulation.h"

namespace MirrorPrivateCloth
{
    template<class T> T Read(const void* object, std::size_t offset = 0) noexcept
    {
        T value{};
        std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof(value));
        return value;
    }

    struct API
    {
        using Create = bool (*)(void*, RE::NiAVObject*, const RE::NiTransform*, RE::NiAVObject*);
        using Construct = void* (*)(void*, const bool*);
        using Unary = void (*)(void*);
        using Binary = void (*)(void*, void*);
        using Begin = void (*)(void*, float, float, float, float, std::uint32_t);
        using Step = void (*)(void*, float, void*);
        using Size = std::uint32_t (*)(void*, int);
        Create create{};
        Construct construct{};
        Unary destroy{}, reset{};
        Binary add{}, removeAction{};
        Begin begin{};
        Step step{};
        Size size{};
        std::uintptr_t extraVtable{}, setVtable{};

        static API Resolve() noexcept
        {
            API api;
            if (REL::Module::IsVR()) return api;
            const auto version = REL::Module::get().version();
            const bool ae = version == REL::Version{1,11,240,0};
            if (!ae && version != REL::Version{1,10,163,0}) return api;
            struct Entry {
                std::uint32_t og, ae;
                std::array<unsigned char,16> ogBytes, aeBytes;
                unsigned length;
            };
            // Verified against both executable implementations, including the
            // six-argument BeginFrame ABI and non-deleting world destructor.
            static constexpr Entry entries[] = {
                {0x1DA6890,0x18A0900,{0x4c,0x8b,0xdc,0x4d,0x89,0x4b,0x20,0x4d,0x89,0x43,0x18,0x49,0x89,0x53,0x10,0x49},
                    {0x4c,0x8b,0xdc,0x4d,0x89,0x4b,0x20,0x4d,0x89,0x43,0x18,0x49,0x89,0x53,0x10,0x49},16},
                {0x1DCED70,0x18BE160,{0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20,0xc7,0x41,0x08,0x01,0x00,0xff},
                    {0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20,0xc7,0x41,0x08,0x01,0x00,0xff},16},
                {0x1DCEE20,0x18BE210,{0x48,0x89,0x5c,0x24,0x18,0x55,0x48,0x83,0xec,0x20,0x33,0xed},
                    {0x48,0x89,0x5c,0x24,0x18,0x55,0x48,0x83,0xec,0x20,0x33,0xed},12},
                {0x1DCF1C0,0x18BE5A0,{0x40,0x53,0x55,0x41,0x55,0x41,0x56,0x48,0x83,0xec,0x38,0x66,0x83,0x7a,0x0a,0x00},
                    {0x40,0x53,0x55,0x41,0x55,0x41,0x56,0x48,0x83,0xec,0x38,0x66,0x83,0x7a,0x0a,0x00},16},
                {0x1DCFAE0,0x18BEEC0,{0x40,0x53,0x55,0x57,0x41,0x54,0x41,0x57,0x48,0x83,0xec,0x60,0x49,0x8b,0xd0,0x41},
                    {0x40,0x53,0x55,0x57,0x41,0x54,0x48,0x83,0xec,0x68,0x49,0x8b,0xd0,0x0f,0x29,0x74},16},
                {0x1DCFD90,0x18BF170,{0x48,0x83,0xec,0x28,0x4c,0x8d,0x4c,0x24,0x48,0x4c,0x8d,0x44,0x24,0x40},
                    {0x48,0x83,0xec,0x28,0x4c,0x8d,0x4c,0x24,0x48,0x4c,0x8d,0x44,0x24,0x40},14},
                {0x1DAD830,0x18A5DF0,{0x8b,0x44,0x24,0x30,0xf3,0x0f,0x10,0x44,0x24,0x28,0xf3,0x0f,0x11,0x99,0xa4,0x01},
                    {0xf3,0x0f,0x10,0x44,0x24,0x28,0x8b,0x44,0x24,0x30,0xf3,0x0f,0x11,0x81,0xa8,0x01},16},
                {0x1DADA50,0x18A6070,{0x66,0xc7,0x81,0xb4,0x01,0x00,0x00,0x01,0x01,0xc3},
                    {0x66,0xc7,0x81,0xb4,0x01,0x00,0x00,0x01,0x01,0xc3},10},
                {0x1DDC8D0,0x18C8730,{0x4c,0x63,0x59,0x58,0x45,0x33,0xc9,0x4c,0x8b,0xc2,0x41,0x8b,0xd1,0x45,0x85,0xdb},
                    {0x4c,0x63,0x59,0x58,0x45,0x33,0xc9,0x4c,0x8b,0xc2,0x45,0x85,0xdb,0x7e,0x1b,0x48},16}
            };
            std::array<std::uintptr_t,9> addresses{};
            for (unsigned i=0;i<addresses.size();++i) {
                const auto& entry=entries[i];
                addresses[i]=REL::Offset(ae?entry.ae:entry.og).address();
                if (std::memcmp(reinterpret_cast<const void*>(addresses[i]),
                    (ae?entry.aeBytes:entry.ogBytes).data(),entry.length)!=0) {
                    logger::warn("[MirrorClothSimulation] native entry {} differs; simulation unavailable",i);
                    return {};
                }
            }
            api.create=reinterpret_cast<Create>(addresses[0]);
            api.construct=reinterpret_cast<Construct>(addresses[1]);
            api.destroy=reinterpret_cast<Unary>(addresses[2]);
            api.add=reinterpret_cast<Binary>(addresses[3]);
            api.step=reinterpret_cast<Step>(addresses[4]);
            api.size=reinterpret_cast<Size>(addresses[5]);
            api.begin=reinterpret_cast<Begin>(addresses[6]);
            api.reset=reinterpret_cast<Unary>(addresses[7]);
            api.removeAction=reinterpret_cast<Binary>(addresses[8]);
            api.extraVtable=REL::Offset(ae?0x26B20F0u:0x2E42E48u).address();
            api.setVtable=REL::Offset(ae?0x26B24E0u:0x2E43128u).address();
            return api;
        }
    };

    class Simulation
    {
        API api_{};
        alignas(16) std::array<std::byte,0xA0> world_{};
        bool constructed_{}, ready_{}, faulted_{};
        RE::NiPointer<RE::NiAVObject> root_;
        std::vector<RE::NiPointer<RE::NiObject>> extras_;
        std::vector<void*> sets_;
        std::vector<RE::NiAVObject*> writers_;
        std::vector<RE::NiTransform> savedLocals_;
        std::vector<std::byte> scratch_;
        void* stepBuffer_{};
        MirrorClothSimulation::Clock clock_;
        unsigned discovered_{}, skipped_{}, rejectedDefinitions_{}, rejectedBindings_{}, particles_{}, instances_{}, logs_{};
        std::uint64_t frames_{}, changedFrames_{};

        bool CheckDefinitions(const void* extra, std::vector<const void*>& definitions)
        {
            using namespace MirrorClothSimulation;
            const auto count=Read<std::uint32_t>(extra,0x28);
            const auto* data=Read<const void* const*>(extra,0x18);
            if (!count || count>MaxInstances-instances_ || !data) return false;
            unsigned particles=0;
            for (unsigned i=0;i<count;++i) {
                const auto* cloth=data[i];
                if (!cloth || std::find(definitions.begin(),definitions.end(),cloth)!=definitions.end()) return false;
                const auto buffers=Read<std::uint32_t>(cloth,0x30);
                const auto* buffer=Read<const void* const*>(cloth,0x28);
                const auto sims=Read<std::uint32_t>(cloth,0x20);
                const auto* sim=Read<const void* const*>(cloth,0x18);
                if (!sims || sims>MaxInstances || !sim || buffers>128 || (buffers && !buffer)) return false;
                for (unsigned b=0;b<buffers;++b)
                    if (!buffer[b] || !PrivateBufferType(Read<std::uint32_t>(buffer[b],0x18))) return false;
                for (unsigned s=0;s<sims;++s) {
                    if (!sim[s]) return false;
                    const auto size=Read<std::uint32_t>(sim[s],0x40);
                    if (size>MaxParticles || particles>MaxParticles-size) return false;
                    particles+=size;
                }
            }
            if (particles>MaxParticles-particles_) return false;
            for (unsigned i=0;i<count;++i) definitions.push_back(data[i]);
            particles_+=particles;
            return true;
        }

        bool CollectInstance(void* instance, const std::unordered_set<RE::NiAVObject*>& owned,
            std::vector<void*>& sets, std::vector<RE::NiAVObject*>& writers)
        {
            using namespace MirrorClothSimulation;
            if (!instance) return false;
            const auto* definition=Read<const void*>(instance,0x10);
            const auto buffers=Read<std::uint32_t>(instance,0x38);
            const auto* buffer=Read<const void* const*>(instance,0x30);
            if (!definition || buffers>128 || buffers!=Read<unsigned>(definition,0x30) || (buffers && !buffer) ||
                Read<unsigned>(instance,0x18)>=Read<unsigned>(definition,0x60)) return false;
            for (unsigned b=0;b<buffers;++b) if (!buffer[b]) return false;
            const auto count=Read<std::uint32_t>(instance,0x48);
            auto* const* data=Read<void* const*>(instance,0x40);
            if (!count || count>8 || !data || sets.size()+count>MaxTransformSets) return false;
            for (unsigned s=0;s<count;++s) {
                auto* set=data[s];
                if (!set || Read<std::uintptr_t>(set)!=api_.setVtable) return false;
                const auto bones=Read<std::uint32_t>(set,0x98);
                auto* const* nodes=Read<RE::NiAVObject* const*>(set,0x88);
                const auto* written=Read<const std::uint32_t*>(set,0x40);
                if (!bones || bones>MaxBones || !nodes || !written ||
                    Read<std::uint32_t>(set,0x50)!=bones ||
                    Read<std::uint32_t>(set,0x48)<(bones+31)/32) return false;
                for (unsigned b=0;b<bones;++b) {
                    if (!nodes[b]) continue; // Native transform sets support authored, unmapped inputs.
                    if (!owned.contains(nodes[b])) return false;
                    if ((written[b/32]&(1u<<(b%32))) && nodes[b]==root_.get()) return false;
                    if (written[b/32]&(1u<<(b%32)))
                        writers.push_back(nodes[b]);
                }
                sets.push_back(set);
            }
            // CreateInstance attaches a pooled global wind action. Detach it:
            // the private world must never advance a shared action concurrently.
            const auto actions=Read<unsigned>(instance,0x58);
            if (actions>32) return false;
            for (unsigned a=0;a<actions;++a) {
                auto** items=Read<void**>(instance,0x50);
                if (!items || !items[0]) return false;
                api_.removeAction(instance,items[0]);
                if (Read<unsigned>(instance,0x58)!=actions-a-1) return false;
            }
            return true;
        }

        void PrepareUnsafe(RE::NiAVObject* root, RE::PlayerCharacter* player)
        {
            api_=API::Resolve();
            if (!api_.create || !root || !player) return;
            root_.reset(root);
            std::unordered_set<RE::NiAVObject*> owned;
            std::vector<RE::NiAVObject*> pending{root};
            std::vector<RE::NiObject*> candidates;
            auto add=[&](RE::NiObject* extra) {
                if (candidates.size()>=64) return;
                if (extra && Read<std::uintptr_t>(extra)==api_.extraVtable &&
                    std::find(candidates.begin(),candidates.end(),extra)==candidates.end()) candidates.push_back(extra);
            };
            for (unsigned i=0;i<pending.size();++i) {
                auto* node=pending[i];
                if (!node || !owned.insert(node).second) continue;
                if (owned.size()>16384) return;
                if (node->extra) {
                    if (node->extra->size()>128) return;
                    for (auto* extra:*node->extra) add(extra);
                }
                if (auto* parent=node->IsNode()) for (const auto& child:parent->GetRuntimeData().children) {
                    if (!child) continue;
                    const char* name=child->name.c_str();
                    if (name && _stricmp(name,"TFP_ShadowTwin")==0) continue;
                    pending.push_back(child.get());
                }
            }
            if (player->currentProcess && player->currentProcess->middleHigh) {
                const auto& cache=player->currentProcess->middleHigh->clothExtraDataCache;
                if (cache.size()<=64) for (auto* extra:cache) add(reinterpret_cast<RE::NiObject*>(extra));
            }
            discovered_=static_cast<unsigned>(candidates.size());
            if (candidates.empty()) return;
            const bool onTheFlyNotifications=true;
            api_.construct(world_.data(),&onTheFlyNotifications);
            constructed_=true;
            std::vector<const void*> definitions;
            for (auto* source:candidates) {
                if (!CheckDefinitions(source,definitions)) {++skipped_;++rejectedDefinitions_;continue;}
                // Clone the metadata independently even if the visual clone
                // shares an extra-data object. Never initialize the live CED.
                RE::NiCloningProcess process{};
                process.copyType=RE::NiCloningProcess::CopyType::kCopyUnique;
                process.scale={1.0f,1.0f,1.0f};
                RE::NiPointer<RE::NiObject> extra(NiVirtualDispatch::CreateClone(source,process));
                if (!extra || extra.get()==source || Read<std::uintptr_t>(extra.get())!=api_.extraVtable ||
                    Read<unsigned>(extra.get(),0x70)!=0 || Read<void*>(extra.get(),0x78)) {++skipped_;continue;}
                NiVirtualDispatch::ProcessClone(source,process);
                extras_.push_back(extra);
                if (!api_.create(extra.get(),root,&root->world,root)) {++skipped_;continue;}
                const auto count=Read<unsigned>(extra.get(),0x70);
                auto** data=Read<void**>(extra.get(),0x60);
                std::vector<void*> sets;
                std::vector<RE::NiAVObject*> writers;
                bool valid=count && count<=MirrorClothSimulation::MaxInstances-instances_ && data;
                for (unsigned i=0;valid && i<count;++i) valid=CollectInstance(data[i],owned,sets,writers);
                if (!valid || writers.empty() || writers_.size()+writers.size()>MirrorClothSimulation::MaxBones ||
                    sets_.size()+sets.size()>MirrorClothSimulation::MaxTransformSets) {
                    ++skipped_;++rejectedBindings_;continue;
                }
                for (unsigned i=0;i<count;++i) api_.add(world_.data(),data[i]);
                instances_+=count;
                sets_.insert(sets_.end(),sets.begin(),sets.end());
                for (auto* writer:writers)
                    if (std::find(writers_.begin(),writers_.end(),writer)==writers_.end()) writers_.push_back(writer);
            }
            if (!instances_) return;
            const auto size=api_.size(world_.data(),1);
            if (!size || size>MirrorClothSimulation::MaxStepBytes) return;
            scratch_.resize(static_cast<std::size_t>(size)+127);
            savedLocals_.resize(writers_.size());
            stepBuffer_=reinterpret_cast<void*>((reinterpret_cast<std::uintptr_t>(scratch_.data())+127)&~std::uintptr_t{127});
            ready_=true;
        }

        std::uint64_t HashWriters() const noexcept
        {
            std::uint64_t hash=14695981039346656037ull;
            for (const auto* node:writers_) {
                // Semantic members only: NiTransform contains padding.
                auto bytes=[&](const void* value,unsigned size) {
                    const auto* p=static_cast<const unsigned char*>(value);
                    for (unsigned i=0;i<size;++i) {hash^=p[i];hash*=1099511628211ull;}
                };
                for (unsigned r=0;r<3;++r) for (unsigned c=0;c<3;++c) bytes(&node->local.rotate.entry[r][c],4);
                bytes(&node->local.translate,12); bytes(&node->local.scale,4);
            }
            return hash;
        }

        bool PrepareGuarded(RE::NiAVObject* root, RE::PlayerCharacter* player)
        {
            __try {PrepareUnsafe(root,player);return true;}
            __except(EXCEPTION_EXECUTE_HANDLER) {faulted_=true;ready_=false;return false;}
        }
        bool StepGuarded(const MirrorClothSimulation::Frame& frame)
        {
            __try {
                for (unsigned i=0;i<writers_.size();++i) savedLocals_[i]=writers_[i]->local;
                for (auto* set:sets_) {
                    if (frame.resetPrediction) api_.reset(set);
                    api_.begin(set,0.0f,0.0f,frame.delta,frame.step,frame.steps);
                }
                const auto before=HashWriters();
                for (unsigned i=0;i<frame.steps;++i) api_.step(world_.data(),frame.step,stepBuffer_);
                bool finite=true;
                for (auto* node:writers_) {
                    if (!std::isfinite(node->local.scale) || !std::isfinite(node->local.translate.x) ||
                        !std::isfinite(node->local.translate.y) || !std::isfinite(node->local.translate.z)) finite=false;
                    for (unsigned r=0;r<3;++r) for (unsigned c=0;c<3;++c)
                        if (!std::isfinite(node->local.rotate.entry[r][c])) finite=false;
                }
                if (!finite) {ready_=false;return false;}
                ++frames_;
                if (HashWriters()!=before) ++changedFrames_;
                return true;
            } __except(EXCEPTION_EXECUTE_HANDLER) {faulted_=true;ready_=false;return false;}
        }
        void RestoreLocals() noexcept
        {
            __try {for (unsigned i=0;i<writers_.size();++i) writers_[i]->local=savedLocals_[i];}
            __except(EXCEPTION_EXECUTE_HANDLER) {faulted_=true;}
        }
    public:
        Simulation()=default;
        Simulation(const Simulation&)=delete;
        Simulation& operator=(const Simulation&)=delete;
        ~Simulation() {if (constructed_) api_.destroy(world_.data());}
        bool Faulted() const noexcept {return faulted_;}
        bool Ready() const noexcept {return ready_;}

        void Prepare(RE::NiAVObject* root, RE::PlayerCharacter* player) noexcept
        {
            try {PrepareGuarded(root,player);} catch (...) {ready_=false;}
            logger::info("[MirrorClothSimulation] prepared: metadata={} instances={} transformSets={} writableBones={} particles={} skipped={} definitionSkips={} bindingSkips={} ready={} faulted={}",
                discovered_,instances_,sets_.size(),writers_.size(),particles_,skipped_,rejectedDefinitions_,rejectedBindings_,ready_,faulted_);
        }
        bool Update(float delta, std::uint64_t serial, std::uint64_t generation, bool thirdPerson) noexcept
        {
            if (!ready_) return false;
            const auto frame=clock_.Advance(serial,generation,delta);
            if (!frame.steps) return true;
            if (!StepGuarded(frame)) {
                RestoreLocals();
                logger::warn("[MirrorClothSimulation] stopped: frames={} faulted={}",frames_,faulted_);
                return false;
            }
            if (frames_==1 || (frames_%300==0 && logs_++<12))
                logger::info("[MirrorClothSimulation] advancing: frames={} changedFrames={} instances={} substeps={} thirdPerson={}",
                    frames_,changedFrames_,instances_,frame.steps,thirdPerson);
            return true;
        }
    };
}

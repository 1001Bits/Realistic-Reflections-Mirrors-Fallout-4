#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <span>

namespace MirrorClothPose
{
    constexpr std::size_t MaxBones = 512;
    struct Transform
    {
        float r[3][3]{{1,0,0},{0,1,0},{0,0,1}};
        float p[3]{};
        float scale = 1;
    };
    struct ReferenceTransform { float position[4], rotation[4], scale[4]; };
    static_assert(sizeof(ReferenceTransform) == 0x30);

    inline bool Finite(const Transform& value) noexcept
    {
        for (const auto& row : value.r) for (float x : row) if (!std::isfinite(x)) return false;
        for (float x : value.p) if (!std::isfinite(x)) return false;
        return std::isfinite(value.scale) && value.scale > 0.00001f && value.scale < 10000.f;
    }
    inline bool Decode(const ReferenceTransform& ref, Transform& result) noexcept
    {
        float length = 0;
        for (float v : ref.rotation) { if (!std::isfinite(v)) return false; length += v*v; }
        if (!std::isfinite(length) || length < 0.01f || length > 100.f) return false;
        const float norm = 1.f / std::sqrt(length);
        const float x=ref.rotation[0]*norm, y=ref.rotation[1]*norm;
        const float z=ref.rotation[2]*norm, w=ref.rotation[3]*norm;
        result.r[0][0]=1-2*(y*y+z*z); result.r[0][1]=2*(x*y-z*w); result.r[0][2]=2*(x*z+y*w);
        result.r[1][0]=2*(x*y+z*w); result.r[1][1]=1-2*(x*x+z*z); result.r[1][2]=2*(y*z-x*w);
        result.r[2][0]=2*(x*z-y*w); result.r[2][1]=2*(y*z+x*w); result.r[2][2]=1-2*(x*x+y*y);
        for (unsigned i=0;i<3;++i) {
            result.p[i]=ref.position[i];

            if (!std::isfinite(ref.scale[i]) || std::abs(ref.scale[i]-ref.scale[0])>0.0001f) return false;
        }
        result.scale=ref.scale[0];
        return Finite(result);
    }
    inline Transform Compose(const Transform& a, const Transform& b) noexcept
    {
        Transform out;
        for (unsigned i=0;i<3;++i) {
            out.p[i]=a.p[i];
            for (unsigned j=0;j<3;++j) {
                out.p[i]+=a.r[i][j]*b.p[j]*a.scale;
                out.r[i][j]=0;
                for (unsigned k=0;k<3;++k) out.r[i][j]+=a.r[i][k]*b.r[k][j];
            }
        }
        out.scale=a.scale*b.scale;
        return out;
    }
    inline Transform Relative(const Transform& parent, const Transform& world) noexcept
    {
        Transform inverse;
        inverse.scale=1.f/parent.scale;
        for (unsigned i=0;i<3;++i) {
            for (unsigned j=0;j<3;++j) {
                inverse.r[i][j]=parent.r[j][i];
                inverse.p[i]-=parent.r[j][i]*parent.p[j]*inverse.scale;
            }
        }
        return Compose(inverse,world);
    }

    inline bool ModelPose(std::span<const std::int16_t> parents,
        std::span<const ReferenceTransform> reference, std::span<Transform> output) noexcept
    {
        if (parents.empty() || parents.size()>MaxBones || parents.size()!=reference.size() || output.size()!=parents.size()) return false;
        std::array<std::uint8_t,MaxBones> state{};
        std::array<std::uint16_t,MaxBones> chain{};
        for (std::size_t start=0;start<parents.size();++start) {
            std::size_t count=0;
            auto index=static_cast<std::int32_t>(start);
            while (index!=-1 && state[index]!=2) {
                if (state[index]==1 || count==MaxBones) return false;
                state[index]=1;
                chain[count++]=static_cast<std::uint16_t>(index);
                index=parents[index];
                if (index < -1 || index>=static_cast<std::int32_t>(parents.size())) return false;
            }
            while (count) {
                const auto bone=chain[--count];
                Transform local;
                if (!Decode(reference[bone],local)) return false;
                output[bone]=parents[bone]==-1 ? local : Compose(output[parents[bone]],local);
                if (!Finite(output[bone])) return false;
                state[bone]=2;
            }
        }
        return true;
    }

    template<class Bindings, class IsFinite>
    std::uint32_t Apply(Bindings& bindings, bool nativeThirdPerson, IsFinite&& finite) noexcept
    {
        std::uint32_t applied=0;
        for (auto& binding : bindings) {
            if (!binding.source || !binding.target || binding.source==binding.target ||
                binding.source->parent!=binding.sourceParent.get() ||
                binding.target->parent!=binding.targetParent.get()) continue;
            const auto& local=nativeThirdPerson ? binding.source->local : binding.referenceLocal;
            if (!finite(local)) continue;
            const auto& old=binding.target->local;
            bool same=old.scale==local.scale && old.translate.x==local.translate.x &&
                old.translate.y==local.translate.y && old.translate.z==local.translate.z;
            for (unsigned i=0;same && i<3;++i) for (unsigned j=0;j<3;++j)
                if (old.rotate.entry[i][j]!=local.rotate.entry[i][j]) same=false;
            if (same) continue;
            binding.target->local=local;
            ++applied;
        }
        return applied;
    }
}

#pragma once

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

namespace MirrorAnimationInputs
{
    enum class Type { kInt, kFloat };
    struct Input { const char* name; Type type; bool thirdPersonIntent{false}; };
    inline constexpr std::array kInputs{
        Input{"iSyncIdleLocomotion", Type::kInt},
        Input{"iSyncSprintState", Type::kInt},
        Input{"iIsInSneak", Type::kInt},
        Input{"iSyncSightedState", Type::kInt},
        Input{"iSyncGunDown", Type::kInt},
        Input{"iWeaponChargeMode", Type::kInt},
        Input{"weaponSpeedMult", Type::kFloat},
        Input{"reloadSpeedMult", Type::kFloat},

        Input{"camerafromx", Type::kFloat},
        Input{"camerafromy", Type::kFloat},
        Input{"camerafromz", Type::kFloat},
        Input{"fSpeedWalk", Type::kFloat},
        Input{"fSpeedRun", Type::kFloat},

        Input{"iLocomotionSpeedState", Type::kInt, true},
        Input{"iSyncDirection", Type::kInt, true},
        Input{"iSyncRunDirection", Type::kInt, true},
        Input{"iSyncWalkRun", Type::kInt, true},
        Input{"iSyncSneakWalkRun", Type::kInt, true},
        Input{"iSyncReadyAlertRelaxed", Type::kInt, true}
    };

    constexpr bool EqualName(std::string_view a, std::string_view b) noexcept
    {
        if (a.size() != b.size()) return false;
        for (std::size_t i = 0; i < a.size(); ++i) {
            auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c; };
            if (lower(a[i]) != lower(b[i])) return false;
        }
        return true;
    }

    inline const Input* Find(const char* name) noexcept
    {
        if (name) for (const auto& input : kInputs)
            if (EqualName(name, input.name)) return &input;
        return nullptr;
    }

    inline bool ThirdPersonIntent(const char* name) noexcept
    {
        const auto* input = Find(name);
        return input && input->thirdPersonIntent;
    }

    constexpr bool AcceptRequest(bool sourceAccepted, unsigned source, unsigned active,
        bool thirdPersonIntent) noexcept
    {
        return sourceAccepted || (source == 1 && active == 1 && thirdPersonIntent);
    }

    constexpr bool ShouldReplay(unsigned source, unsigned active, bool shared, bool matchingType,
        bool thirdPersonIntent = false) noexcept
    {
        if (source > 1 || active > 1) return false;
        if (thirdPersonIntent) return matchingType && (source == 0 || active == 1);
        if (shared) return matchingType && source == active;
        return source == 0;
    }

    constexpr bool Valid(Type type, std::uint32_t value) noexcept
    {
        
        return type == Type::kInt || std::isfinite(std::bit_cast<float>(value));
    }

    struct IndexCache
    {
        const void* project = nullptr;
        const void* entries = nullptr;
        const void* name = nullptr;
        std::uint32_t capacity = 0;
        std::int32_t index = -1;
    };

    template <class T> T Read(const void* base, std::size_t offset) noexcept
    {
        T value;
        std::memcpy(&value, static_cast<const std::byte*>(base) + offset, sizeof(value));
        return value;
    }

    inline bool ReadWordUnsafe(const void* graph, const void* internedName,
        IndexCache& cache, std::uint32_t& value) noexcept
    {
        if (!graph || !internedName) return false;
        const auto* project = Read<const void*>(graph, 0x370);
        const auto* behavior = Read<const void*>(graph, 0x378);
        if (!project || !behavior) return false;
        const auto* values = Read<const void*>(behavior, 0x110);
        if (!values) return false;
        const auto* words = Read<const void*>(values, 0x10);
        const auto count = Read<std::int32_t>(values, 0x18);
        if (!words || count <= 0 || count > 4096) return false;
        const auto capacity = Read<std::uint32_t>(project, 0x84);
        const auto* entries = Read<const std::byte*>(project, 0xA0);
        if (!entries || capacity == 0 || capacity > 4096) return false;
        if (cache.project != project || cache.entries != entries || cache.capacity != capacity || cache.name != internedName) {
            cache = {project, entries, internedName, capacity, -1};
            for (std::uint32_t i = 0; i < capacity; ++i) {
                const auto* entry = entries + i * 24u;
                if (Read<const void*>(entry, 16) && Read<const void*>(entry, 0) == internedName) {
                    cache.index = Read<std::int32_t>(entry, 8);
                    break;
                }
            }
        }
        if (cache.index < 0 || cache.index >= count) return false;
        value = Read<std::uint32_t>(words, static_cast<std::size_t>(cache.index) * 4u);
        return true;
    }
}

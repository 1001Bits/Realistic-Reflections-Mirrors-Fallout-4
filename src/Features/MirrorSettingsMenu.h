#pragma once

#include "MirrorQualityControls.h"
#include <algorithm>
#include <array>
#include <cstdint>

namespace MirrorSettingsMenu
{
    enum Setting : unsigned {
        Mode, TargetFPS, Preset, ObjectDistance, ShadowDistance, Shadows,
        ShadowResolution, Resolution, EveryFrame, RefreshRate, DebugKeys,
        PresetBaseline, Count
    };
    enum class Kind { Choice, Slider, Toggle, Internal };
    struct Control {
        const wchar_t* key;
        const char* id;
        const char* label;
        const char* help;
        Kind kind;
        int minimum, maximum, step, fallback;
        const char* choices{};
        bool Boolean() const { return key[0] == L'b'; }
    };
    inline constexpr std::array<Control, Count> controls{{
        {L"iQualityMode", "iQualityMode:Mirrors", "Quality mode", "Automatic adjusts quality to performance. Preset selects a quality level. Manual lets you choose each setting.", Kind::Choice, 0, 2, 1, 1, "Automatic\0Preset\0Manual\0"},
        {L"iAutomaticTargetFPS", "iAutomaticTargetFPS:Mirrors", "Target game FPS", "Automatic lowers quality below this target and restores it gradually when performance improves.", Kind::Slider, 30, 144, 1, 60},
        {L"iQualityLevel", "iQualityLevel:Mirrors", "Quality preset", "Preset for resolution, refresh rate, shadows and both distances.", Kind::Choice, 0, 2, 1, 1, "Low\0Medium\0High\0"},
        {L"iOutdoorDistance", "iOutdoorDistance:Mirrors", "Object distance", "How far from each mirror objects are drawn into the reflection, in game units.", Kind::Slider, 500, 5000, 250, 2000},
        {L"iShadowDistance", "iShadowDistance:Mirrors", "Shadow distance", "How far from each mirror shadows are cast, in game units.", Kind::Slider, 500, 5000, 250, 2000},
        {L"bShadows", "bShadows:Mirrors", "Shadows", "Enable or disable sunlight and local-light shadows in the reflection, including the player's shadows.", Kind::Toggle, 0, 1, 1, 1},
        {L"iShadowResolution", "iShadowResolution:Mirrors", "Shadow map resolution", "Resolution of both reflected sunlight shadow maps. Local-light maps use the game's resolution.", Kind::Slider, 512, 4096, 512, 2048},
        {L"iResolution", "iResolution:Mirrors", "Resolution", "Higher reflection resolution is sharper and costs more GPU time.", Kind::Choice, 0, 4, 1, 0, "Default\0" "512\0" "1024\0" "2048\0" "4096\0"},
        {L"bEveryFrameRefresh", "bEveryFrameRefresh:Mirrors", "Update every frame", "Refresh reflections every game frame instead of using a target refresh rate.", Kind::Toggle, 0, 1, 1, 0},
        {L"iRefreshRate", "iRefreshRate:Mirrors", "Refresh rate", "Target updates per mirror per second, limited by available frame time.", Kind::Slider, 5, 180, 1, 60},
        {L"bDebugKeys", "bDebugKeys:Mirrors", "Debug keys", "F7: optimisations. F8: benchmark. F11: performance overlay. F12: reflections on/off.", Kind::Toggle, 0, 1, 1, 0},
        {L"bAutomaticQuality", "bAutomaticQuality:Mirrors", "", "", Kind::Internal, 0, 1, 1, 1}
    }};

    struct Values {
        std::array<int, Count> values{};
        Values() { for (unsigned i = 0; i < Count; ++i) values[i] = controls[i].fallback; }
        int& operator[](unsigned setting) { return values[setting]; }
        int operator[](unsigned setting) const { return values[setting]; }
        bool operator==(const Values&) const = default;
        void Set(unsigned setting, int value) {
            const auto& control = controls[setting];
            value = std::clamp(value, control.minimum, control.maximum);
            values[setting] = control.minimum +
                (value - control.minimum + control.step / 2) / control.step * control.step;
            if (setting == Mode && value != 0) values[PresetBaseline] = value == 1;
        }
        bool Hidden(unsigned setting) const {
            return controls[setting].kind == Kind::Internal ||
                MirrorQualityControls::Hidden(controls[setting].id,
                    {MirrorQualityMode::Clamp(values[Mode]), values[EveryFrame] != 0, values[Shadows] != 0});
        }
        std::uint32_t ChangesFrom(const Values& original) const {
            std::uint32_t result{};
            for (unsigned i = 0; i < Count; ++i) if (values[i] != original[i]) result |= 1u << i;
            return result;
        }
    };

    struct Synchronization {
        Values values;
        std::uint32_t changed{};
        std::uint64_t revision{};
    };

    template<class Cache> bool Synchronize(const Synchronization& changes, Cache& cache) {
        if (changes.changed >> Count) return false;
        for (unsigned i = 0; i < Count; ++i) {
            if (!(changes.changed & (1u << i))) continue;
            int current{};
            if (!cache.Read(controls[i], current)) return false;
            if (current != changes.values[i] && !cache.Write(controls[i], changes.values[i])) return false;
        }
        return true;
    }
}

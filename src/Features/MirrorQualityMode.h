#pragma once

namespace MirrorQualityMode
{
	
	enum class Mode : int { Automatic = 0, Preset = 1, Manual = 2 };
	inline constexpr Mode kDefault = Mode::Preset;
	constexpr Mode Clamp(int value) noexcept
	{
		return value >= 0 && value <= 2 ? static_cast<Mode>(value) : kDefault;
	}
	constexpr Mode Legacy(bool adjustment, bool presets) noexcept
	{
		return adjustment ? Mode::Automatic : presets ? Mode::Preset : Mode::Manual;
	}
	constexpr bool UsesPreset(Mode mode, bool automaticUsesPreset) noexcept
	{
		return mode == Mode::Preset || (mode == Mode::Automatic && automaticUsesPreset);
	}
	constexpr const char* Name(Mode mode) noexcept
	{
		switch (mode) {
		case Mode::Automatic: return "automatic";
		case Mode::Manual: return "manual";
		default: return "preset";
		}
	}
}

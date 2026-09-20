#pragma once

#include "MirrorSettingsPolicy.h"

#include <cstdint>

namespace MirrorQualityPreset
{
	enum class Level : std::uint32_t
	{
		kLow = 0,
		kMedium = 1,
		kHigh = 2
	};

	struct Settings
	{
		std::uint32_t resolution{};
		std::uint32_t refreshRate{};
		std::uint32_t objectDistance{};
		std::uint32_t shadowDistance{};
		bool shadows{};
	};

	inline constexpr Settings kLowSettings{ 1024u, 30u, 2500u, 1500u, false };
	
	inline constexpr Settings kMediumSettings{ 2048u, 60u, 5000u, 2500u, true };

	inline constexpr Settings kHighSettings{ 4096u, MirrorSettingsPolicy::kEveryFrame, 5000u, 2500u, true };

	inline constexpr Settings For(Level level) noexcept
	{
		switch (level) {
		case Level::kLow:
			return kLowSettings;
		case Level::kHigh:
			return kHighSettings;
		case Level::kMedium:
		default:
			return kMediumSettings;
		}
	}

	inline constexpr Level Clamp(int value) noexcept
	{
		return value < 0 || value > static_cast<int>(Level::kHigh) ?
			Level::kMedium :
			static_cast<Level>(static_cast<std::uint32_t>(value));
	}

	inline constexpr const char* Name(Level level) noexcept
	{
		switch (level) {
		case Level::kLow:
			return "low";
		case Level::kHigh:
			return "high";
		case Level::kMedium:
		default:
			return "medium";
		}
	}
}

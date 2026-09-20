#pragma once
#include "MirrorQualityMode.h"
#include <string_view>

namespace MirrorQualityControls
{
	struct State { MirrorQualityMode::Mode mode{MirrorQualityMode::kDefault}; bool everyFrame{}; bool shadows{true}; };
	constexpr bool Owned(std::string_view id) noexcept
	{
		return id == "iQualityMode:Mirrors" || id == "iAutomaticTargetFPS:Mirrors" ||
			id == "iQualityLevel:Mirrors" || id == "iResolution:Mirrors" ||
			id == "iOutdoorDistance:Mirrors" || id == "iShadowDistance:Mirrors" ||
			id == "bShadows:Mirrors" || id == "iShadowResolution:Mirrors" ||
			id == "bEveryFrameRefresh:Mirrors" || id == "iRefreshRate:Mirrors";
	}
	constexpr bool Hidden(std::string_view id, State state) noexcept
	{
		using enum MirrorQualityMode::Mode;
		if (!Owned(id) || id == "iQualityMode:Mirrors") return false;
		if (id == "iAutomaticTargetFPS:Mirrors") return state.mode != Automatic;
		if (id == "iQualityLevel:Mirrors") return state.mode != Preset;
		if (state.mode == Manual && !state.shadows &&
			(id == "iShadowResolution:Mirrors" || id == "iShadowDistance:Mirrors")) return true;
		return state.mode != Manual || (id == "iRefreshRate:Mirrors" && state.everyFrame);
	}

	constexpr unsigned FilterFlag(std::string_view id, State state) noexcept
	{
		return Hidden(id, state) ? 0u : 1u;
	}
}

#pragma once

#include <algorithm>
#include <cstdint>
#include <cstddef>
#include "MirrorDefinitionRegistry.h"
#include "MirrorSettingsPolicy.h"

namespace VRMirrorPolicy
{

	constexpr bool Enabled(MirrorDefinitionRegistry::Family family) noexcept
	{
		using Family = MirrorDefinitionRegistry::Family;
		return family != Family::kPlayerHouseBathroomSink &&
			family != Family::kPlayerHouseBathroomShaveDrawer &&
			family != Family::kPlayerHouseRuinBathroomSink;
	}

	constexpr bool LargerPane(float area, std::uint32_t id, float otherArea, std::uint32_t otherId) noexcept
	{
		return area != otherArea ? area > otherArea : id < otherId;
	}

	struct Extent { std::uint32_t width, height; };

	inline constexpr std::uint32_t kDefaultLongestEdge = 512u;
	constexpr Extent CaptureExtent(std::uint32_t eyeWidth, std::uint32_t height, std::uint32_t resolution = 0) noexcept
	{
		resolution = MirrorSettingsPolicy::Resolution(resolution);
		const auto maximum = resolution ? resolution : kDefaultLongestEdge;
		const auto longest = (std::max)(eyeWidth, height);
		if (longest == 0 || (!resolution && longest <= maximum))
			return { eyeWidth, height };
		return { (std::max)(1u, static_cast<std::uint32_t>(static_cast<std::uint64_t>(eyeWidth) * maximum / longest)),
			(std::max)(1u, static_cast<std::uint32_t>(static_cast<std::uint64_t>(height) * maximum / longest)) };
	}

	inline constexpr std::uint64_t kCaptureIntervalMicroseconds = 16667u;
	inline constexpr std::uint64_t kPublicationLifetimeMicroseconds = 250000u;
	inline constexpr std::size_t kPendingFrameCount = 3u;

	using CaptureClock = MirrorSettingsPolicy::CaptureClock;
}

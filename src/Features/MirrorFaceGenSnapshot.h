#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

struct MirrorFaceGenSnapshot
{
	std::array<float, 54> weights{};

	[[nodiscard]] bool Read(const void* animation) noexcept
	{
		if (!animation)
			return false;
		std::memcpy(weights.data(), static_cast<const std::byte*>(animation) + 0x18, sizeof(weights));
		for (float weight : weights) {
			if (!std::isfinite(weight))
				return false;
		}
		return true;
	}

	void Apply(void* privateAnimation, std::uint16_t& privateFaceFlags) const noexcept
	{
		auto* data = static_cast<std::byte*>(privateAnimation);
		std::memcpy(data + 0x18, weights.data(), sizeof(weights));
		data[0x2D8] = std::byte{ 1 }; 
		data[0x2D9] = std::byte{ 1 }; 

		privateFaceFlags = static_cast<std::uint16_t>((privateFaceFlags & ~0x84u) | 0x100u);
	}
};

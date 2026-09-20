#pragma once

#include <cstdint>

struct MirrorForwardPassProof
{
	std::uintptr_t pass{}, shader{}, property{}, material{};
	std::uint32_t technique{}, matches{}, firstWrongTechnique{};
	std::int32_t firstWrongGroup{};
	bool wrongGroup{}, wrongTechnique{};

	void Observe(std::uintptr_t expectedGeometry, std::uintptr_t geometry,
		std::int32_t group, bool allowedTechnique, std::uintptr_t observedPass,
		std::uintptr_t observedShader, std::uintptr_t observedProperty,
		std::uintptr_t observedMaterial, std::uint32_t observedTechnique) noexcept
	{
		if (!expectedGeometry || geometry != expectedGeometry)
			return;
		if (group != 8) {
			if (!wrongGroup)
				firstWrongGroup = group;
			wrongGroup = true;
			return;
		}
		if (!allowedTechnique) {
			if (!wrongTechnique)
				firstWrongTechnique = observedTechnique;
			wrongTechnique = true;
			return;
		}
		if (++matches == 1u) {
			pass = observedPass;
			shader = observedShader;
			property = observedProperty;
			material = observedMaterial;
			technique = observedTechnique;
		}
	}

	[[nodiscard]] bool Matched() const noexcept
	{
		return matches == 1u && pass && shader && property && material;
	}
};

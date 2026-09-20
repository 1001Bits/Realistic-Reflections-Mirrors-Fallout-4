#pragma once
#include <cstddef>
#include <cstdint>

template <class Evidence, std::size_t Capacity = 16>
struct MirrorPassSelection
{
	Evidence evidence[Capacity]{};
	std::uint32_t tripleOccurrences[Capacity]{};
	bool materialCollision[Capacity]{};
	std::uint32_t count{};
	std::uint32_t group4Passes{};
	bool overflow{};

	void Select(const Evidence& value) noexcept
	{
		if (count == Capacity) {
			overflow = true;
			return;
		}
		evidence[count++] = value;
	}

	void ObserveGroup4(std::uintptr_t pass, std::uintptr_t shader,
		std::uintptr_t material, std::uint32_t technique) noexcept
	{
		++group4Passes;
		for (std::uint32_t i = 0; i < count; ++i) {
			const auto& target = evidence[i];
			if (target.geometryGroup != 4 || target.material != material)
				continue;
			materialCollision[i] |= target.pass != pass;
			if (target.shader == shader && target.technique == technique)
				++tripleOccurrences[i];
		}
	}
};

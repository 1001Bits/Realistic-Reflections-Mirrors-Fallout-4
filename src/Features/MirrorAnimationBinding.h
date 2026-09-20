#pragma once

#include <cstddef>
#include <cstdint>

struct MirrorAnimationBindingSetView
{
	std::byte object[0x10];
	const void* bindings;
	std::int32_t count;
	std::uint32_t capacityAndFlags;
	std::int32_t enableIndex;
};
static_assert(offsetof(MirrorAnimationBindingSetView, bindings) == 0x10);
static_assert(offsetof(MirrorAnimationBindingSetView, count) == 0x18);
static_assert(offsetof(MirrorAnimationBindingSetView, enableIndex) == 0x20);

[[nodiscard]] inline bool MirrorHasAnimationEnableBinding(const MirrorAnimationBindingSetView* set) noexcept
{
	return set && set->bindings && set->count > 0 &&
		set->enableIndex >= 0 && set->enableIndex < set->count;
}

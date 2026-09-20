#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace VRMirrorSkin
{
	template <class T> T Read(const void* object, std::size_t offset) noexcept
	{
		T value{};
		std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof(value));
		return value;
	}

	struct Array
	{
		const void* data{};
		std::uint32_t count{}, capacity{};
		bool Valid(std::uint32_t limit) const noexcept
		{
			return count <= capacity && count <= limit && (!count || data);
		}
	};

	inline Array Worlds(const void* skin) noexcept
	{
		return skin ? Array{ Read<const void*>(skin, 0x28), Read<std::uint32_t>(skin, 0x38),
			Read<std::uint32_t>(skin, 0x30) } : Array{};
	}
	inline Array Binds(const void* data) noexcept
	{
		return data ? Array{ Read<const void*>(data, 0x10), Read<std::uint32_t>(data, 0x20),
			Read<std::uint32_t>(data, 0x18) } : Array{};
	}

	template <class Transform, class IsPrivate>
	bool PrivateBindings(const void* source, const void* clone, std::uint32_t limit,
		IsPrivate&& isPrivate) noexcept
	{
		if (!source || !clone || source == clone) return false;
		const auto live = Worlds(source), detached = Worlds(clone);
		if (!live.Valid(limit) || !detached.Valid(limit) || live.count != detached.count ||
			(detached.count && live.data == detached.data)) return false;
		const auto* worlds = static_cast<const Transform* const*>(detached.data);
		for (std::uint32_t i = 0; i < detached.count; ++i)
			if (!worlds[i] || !isPrivate(worlds[i])) return false;
		return true;
	}
}

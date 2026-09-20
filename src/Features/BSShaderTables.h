#pragma once

#include <cstdint>
#include <type_traits>

#include "RE/Bethesda/BSShader.h"

#include "Features/ReflectionRuntime.h"

namespace BSShaderTables
{
	inline constexpr std::uintptr_t kVertexShadersFlat = 0x20;
	inline constexpr std::uintptr_t kPixelShadersFlat = 0xB0;
	inline constexpr std::uintptr_t kPort240MemberShift = 0x78;

	[[nodiscard]] inline std::uintptr_t MemberAddress(
		const RE::BSShader* shader,
		std::uintptr_t flatOffset) noexcept
	{
		const std::uintptr_t offset =
			ReflectionRuntime::IsPort240() ? flatOffset + kPort240MemberShift : flatOffset;
		return reinterpret_cast<std::uintptr_t>(shader) + offset;
	}

	[[nodiscard]] inline auto& VertexShaders(RE::BSShader* shader) noexcept
	{
		using Table = std::remove_reference_t<decltype(shader->vertexShaders)>;
		return *reinterpret_cast<Table*>(MemberAddress(shader, kVertexShadersFlat));
	}

	[[nodiscard]] inline auto& PixelShaders(RE::BSShader* shader) noexcept
	{
		using Table = std::remove_reference_t<decltype(shader->pixelShaders)>;
		return *reinterpret_cast<Table*>(MemberAddress(shader, kPixelShadersFlat));
	}
}

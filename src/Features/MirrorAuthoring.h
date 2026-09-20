#pragma once
#include "MirrorDefinitionRegistry.h"
#include <cstdint>
struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11ShaderResourceView;
namespace RE { class TESObjectREFR; }

namespace MirrorAuthoring
{
	inline constexpr auto kPlugin = "Realistic Reflections - Mirrors.esm";
	inline constexpr std::uint32_t kSurfaceFormID = 0xB00;
	inline constexpr auto kEnableFile = L"Data\\MirrorsOfFallout_CreationKit.enable";
	
	void Initialize() noexcept;
	[[nodiscard]] bool Tagged(RE::TESObjectREFR* reference) noexcept;
	struct Pane {
		MirrorDefinitionRegistry::LocalPane world;
		std::uint64_t contour{};
		std::uint32_t vertexCount{};
		float area{};
	};
	
	[[nodiscard]] bool Read(RE::TESObjectREFR* reference, ID3D11Device* device,
		ID3D11DeviceContext* context, Pane& output) noexcept;
	[[nodiscard]] ID3D11ShaderResourceView* Vertices(std::uint64_t token, std::uint32_t count,
		ID3D11Device* device) noexcept;
	void Reset() noexcept;
	void Forget(std::uint32_t formID) noexcept;
	void Prune() noexcept;
}

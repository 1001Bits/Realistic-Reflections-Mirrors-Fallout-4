#pragma once
#include <array>
#include <d3d11.h>

namespace MirrorMaterialLayout
{
	using TargetArray = std::array<ID3D11RenderTargetView*, 5>;

	inline TargetArray Targets(
		ID3D11RenderTargetView* diffuse, ID3D11RenderTargetView* normal,
		ID3D11RenderTargetView* properties, ID3D11RenderTargetView* legacyFourth,
		bool nativeProperties) noexcept
	{
		return nativeProperties ? TargetArray{diffuse,normal,nullptr,properties,legacyFourth} :
			TargetArray{diffuse,normal,properties,legacyFourth,nullptr};
	}
	inline UINT Count(const TargetArray& targets) noexcept
	{
		UINT count = static_cast<UINT>(targets.size());
		while (count && !targets[count - 1]) --count;
		return count;
	}

	inline void Bind(ID3D11DeviceContext* context, const TargetArray& targets,
		ID3D11DepthStencilView* depth) noexcept
	{
		context->OMSetRenderTargets(Count(targets), targets.data(), depth);
	}
	inline bool Matches(const std::array<ID3D11RenderTargetView*, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT>& observed,
		const TargetArray& targets) noexcept
	{
		for (std::size_t slot=0;slot<observed.size();++slot)
			if (observed[slot] != (slot<targets.size() ? targets[slot] : nullptr)) return false;
		return true;
	}
}

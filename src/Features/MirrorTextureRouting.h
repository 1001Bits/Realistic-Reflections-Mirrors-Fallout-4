#pragma once

namespace RE
{
	class BSRenderPass;
	class NiAVObject;
}

namespace MirrorTextureRouting
{

	bool IsNamedMirrorGeometry(const RE::NiAVObject* geometry) noexcept;
	
	bool IsLikelyMirrorSurface(const RE::BSRenderPass* pass) noexcept;
}

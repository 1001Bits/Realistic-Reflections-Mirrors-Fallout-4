#include "MirrorTextureRouting.h"

#include <string_view>

namespace MirrorTextureRouting
{
	namespace
	{
		char LowerASCII(char value) noexcept
		{
			return value >= 'A' && value <= 'Z' ? static_cast<char>(value + ('a' - 'A')) : value;
		}

		bool ContainsNoCase(std::string_view text, std::string_view token) noexcept
		{
			if (token.empty() || token.size() > text.size())
				return false;
			for (std::size_t start = 0; start + token.size() <= text.size(); ++start) {
				bool matches = true;
				for (std::size_t i = 0; i < token.size(); ++i) {
					if (LowerASCII(text[start + i]) != LowerASCII(token[i])) {
						matches = false;
						break;
					}
				}
				if (matches)
					return true;
			}
			return false;
		}

		bool NodeChainContains(const RE::NiAVObject* geometry, std::string_view token) noexcept
		{
			const RE::NiAVObject* object = geometry;
			for (int depth = 0; object && depth < 12; ++depth, object = object->parent) {
				if (ContainsNoCase(object->GetName(), token))
					return true;
			}
			return false;
		}
	}

	bool IsNamedMirrorGeometry(const RE::NiAVObject* geometry) noexcept
	{
		const RE::NiAVObject* object = geometry;
		for (int depth = 0; object && depth < 12; ++depth, object = object->parent) {
			const std::string_view name = object->GetName();
			if (name.empty())
				continue;
			if (ContainsNoCase(name, "mirrorball") || ContainsNoCase(name, "mirrored") ||
				ContainsNoCase(name, "marker") || ContainsNoCase(name, "cameraattach"))
				continue;
			if (ContainsNoCase(name, "mirror"))
				return true;
		}
		return false;
	}

	bool IsLikelyMirrorSurface(const RE::BSRenderPass* pass) noexcept
	{
		if (!pass || !pass->geometry || !IsNamedMirrorGeometry(pass->geometry))
			return false;

		if (pass->shaderProperty) {
			if (auto* lighting = netimmerse_cast<RE::BSLightingShaderProperty*>(pass->shaderProperty)) {
				const std::string_view material = lighting->rootName;
				if (ContainsNoCase(material, "playerhouse_bathroommirrorenv01.bgsm"))
					return true;
				if (ContainsNoCase(material, "playerhouse_bathroommirror01.bgsm"))
					return NodeChainContains(pass->geometry, "bathroommirror01") &&
					       !NodeChainContains(pass->geometry, "bathroommirror02");
			}
		}

		return true;
	}
}

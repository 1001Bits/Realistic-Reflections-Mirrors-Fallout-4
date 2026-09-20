#pragma once
#include <string_view>
namespace MirrorAuthoringPath
{
	inline constexpr std::string_view kMaterial="mirrorsoffallout\\authoring\\mof_mirrorsurface.bgsm";
	inline constexpr std::string_view kTexture="mirrorsoffallout\\authoring\\mirror_surface.dds";
	constexpr char Fold(char c) noexcept { return c=='/' ? '\\' : (c>='A' && c<='Z' ? c-'A'+'a' : c); }
	constexpr bool Prefix(const char* s,std::string_view p) noexcept {
		for (auto c:p) { if (Fold(*s)!=c) return false; ++s; } return true;
	}
	
	constexpr bool Match(const char* s,std::string_view target,std::string_view root) noexcept {
		if (!s) return false;
		if (Prefix(s,"data\\")) s+=5;
		if (Prefix(s,root)) s+=root.size();
		if (!Prefix(s,target)) return false;
		return s[target.size()]=='\0';
	}
	constexpr bool Material(const char* s) noexcept { return Match(s,kMaterial,"materials\\"); }
	constexpr bool Texture(const char* s) noexcept { return Match(s,kTexture,"textures\\"); }
}

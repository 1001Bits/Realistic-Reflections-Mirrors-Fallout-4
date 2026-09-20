#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace VRMirrorRenderState
{

	inline constexpr std::size_t kStart = 0x1F88;
	inline constexpr std::size_t kSize = 0x34;
	inline constexpr std::size_t kDirty = 0x1EE0;
	inline constexpr std::size_t kDepth = 0x1F88;
	inline constexpr std::size_t kCull = 0x1F9C;
	inline constexpr std::size_t kBlend = 0x1FA8;
	inline constexpr std::size_t kWriteMode = 0x1FB0;

	inline void Set(void* renderer, std::size_t offset, std::uint32_t value, std::uint32_t dirty) noexcept
	{
		auto* bytes = static_cast<std::byte*>(renderer);
		std::memcpy(bytes + offset, &value, sizeof(value));
		*reinterpret_cast<std::uint32_t*>(bytes + kDirty) |= dirty;
	}

	inline void Begin(void* renderer) noexcept
	{
		Set(renderer, kDepth, 3, 4);       
		Set(renderer, 0x1F8C, 0, 4);      
		Set(renderer, 0x1F90, 0, 4);      
		Set(renderer, 0x1F94, 0, 4);      
		Set(renderer, kCull, 1, 8);        
		Set(renderer, 0x1FA0, 0, 8);      
		Set(renderer, kBlend, 0, 0x10);    
		Set(renderer, 0x1FAC, 0, 0x10);   
		Set(renderer, kWriteMode, 1, 0x10); 
	}
}

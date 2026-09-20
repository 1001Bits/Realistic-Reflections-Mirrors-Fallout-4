#pragma once

#include <shared_mutex>
#include <new>
void* operator new[](size_t size, const char* pName, int flags, unsigned debugFlags, const char* file, int line);
void* operator new[](size_t size, size_t alignment, size_t alignmentOffset, const char* pName, int flags,
	unsigned debugFlags, const char* file, int line);

#include <RE/Fallout.h>
#include <F4SE/F4SE.h>
#include "MirrorsOfFalloutCompat.h"
#include <xbyak/xbyak.h>

#include <detours/detours.h>
#include "CheckedDetours.h"

#ifdef NDEBUG
#	include <spdlog/sinks/basic_file_sink.h>
#else
#	include <spdlog/sinks/msvc_sink.h>
#endif

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#ifndef TRACY_SUPPORT
	#undef TRACY_ENABLE
#endif

using namespace std::literals;

namespace stl
{
	using namespace F4SE::stl;

	template <class T, std::size_t Size = 5>
	void write_thunk_call(std::uintptr_t a_src)
	{
		F4SE::AllocTrampoline(14);
		auto& trampoline = F4SE::GetTrampoline();
		if (Size == 6) {
			T::func = *(uintptr_t*)trampoline.write_call<6>(a_src, T::thunk);
		} else {
			T::func = trampoline.write_call<Size>(a_src, T::thunk);
		}
	}

	template <class F, size_t index, class T>
	void write_vfunc()
	{
		REL::Relocation<std::uintptr_t> vtbl{ F::VTABLE[index] };
		T::func = vtbl.write_vfunc(T::size, T::thunk);
	}

	template <std::size_t idx, class T>
	void write_vfunc(REL::VariantID id)
	{
		REL::Relocation<std::uintptr_t> vtbl{ id };
		T::func = vtbl.write_vfunc(idx, T::thunk);
	}

	template <std::size_t idx, class T>
	void write_vfunc(REL::ID id)
	{
		REL::Relocation<std::uintptr_t> vtbl{ id };
		T::func = vtbl.write_vfunc(idx, T::thunk);
	}

	template <std::size_t idx, class T>
	void write_vfunc(REL::VariantOffset offset)
	{
		REL::Relocation<std::uintptr_t> vtbl{ offset };
		T::func = vtbl.write_vfunc(idx, T::thunk);
	}

	template <class T>
	void write_thunk_jmp(std::uintptr_t a_src)
	{
		F4SE::AllocTrampoline(14);
		auto& trampoline = F4SE::GetTrampoline();
		T::func = trampoline.write_branch<5>(a_src, T::thunk);
	}

	template <class F, class T>
	void write_vfunc()
	{
		write_vfunc<F, 0, T>();
	}

	inline bool checked_detour(PVOID* original, PVOID hook)
	{
		const LONG status = InstallDetours({ { original, hook } });
		if (status != NO_ERROR)
			F4SE::log::error("[MirrorsOfFallout] required detour failed ({}); mirror producers disabled", status);
		return status == NO_ERROR;
	}

	template <class T>
	[[nodiscard]] bool detour_thunk(std::uintptr_t address)
	{
		T::func = address;
		return checked_detour(reinterpret_cast<PVOID*>(&T::func), reinterpret_cast<PVOID>(T::thunk));
	}

	template <class T>
	[[nodiscard]] bool detour_thunk(REL::RelocationID id) { return detour_thunk<T>(id.address()); }

	template <class T>
	[[nodiscard]] bool detour_thunk(REL::Offset offset) { return detour_thunk<T>(offset.address()); }

	template <class T>
	[[nodiscard]] bool detour_thunk_ignore_func(REL::RelocationID id)
	{
		auto target = reinterpret_cast<PVOID>(id.address());
		return checked_detour(&target, reinterpret_cast<PVOID>(T::thunk));
	}

	template <std::size_t idx, class T>
	[[nodiscard]] bool detour_vfunc(void* target)
	{
		return detour_thunk<T>(target ? (*reinterpret_cast<std::uintptr_t**>(target))[idx] : 0);
	}

}

namespace logger = F4SE::log;

namespace util
{
	using F4SE::stl::report_and_fail;
}

#include "Plugin.h"
#include <wrl/client.h>
#include <wrl/event.h>

#include <DirectXColors.h>
#include <DirectXMath.h>

namespace DX
{
	
	class com_exception : public std::exception
	{
	public:
		explicit com_exception(HRESULT hr) noexcept :
			result(hr) {}

		const char* what() const override
		{
			static char s_str[64] = {};
			sprintf_s(s_str, "Failure with HRESULT of %08X", static_cast<unsigned int>(result));
			return s_str;
		}

	private:
		HRESULT result;
	};

	inline void ThrowIfFailed(HRESULT hr)
	{
		if (FAILED(hr)) {
			throw com_exception(hr);
		}
	}
}

#include <imgui.h>

#include <nlohmann/json.hpp>
using json = nlohmann::json;

#include <magic_enum/magic_enum.hpp>

#include <EASTL/algorithm.h>
#include <EASTL/array.h>
#include <EASTL/bitset.h>
#include <EASTL/bonus/fixed_ring_buffer.h>
#include <EASTL/fixed_list.h>
#include <EASTL/fixed_slist.h>
#include <EASTL/fixed_vector.h>
#include <EASTL/functional.h>
#include <EASTL/hash_set.h>
#include <EASTL/map.h>
#include <EASTL/numeric_limits.h>
#include <EASTL/set.h>
#include <EASTL/shared_ptr.h>
#include <EASTL/string.h>
#include <EASTL/tuple.h>
#include <EASTL/unique_ptr.h>
#include <EASTL/unordered_map.h>
#include <EASTL/vector.h>

#include <ankerl/unordered_dense.h>
template <>
struct ankerl::unordered_dense::hash<std::string>
{
	using is_transparent = void;  
	using is_avalanching = void;  

	[[nodiscard]] auto operator()(std::string_view str) const noexcept -> uint64_t
	{
		return ankerl::unordered_dense::hash<std::string_view>{}(str);
	}
};

#include "SimpleMath.h"

using float2 = DirectX::SimpleMath::Vector2;
using float3 = DirectX::SimpleMath::Vector3;
using float4 = DirectX::SimpleMath::Vector4;
using float4x4 = DirectX::SimpleMath::Matrix;
using Matrix = DirectX::SimpleMath::Matrix;
using uint = uint32_t;

#include "Globals.h"

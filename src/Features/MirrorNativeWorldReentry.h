#pragma once

#include "ReflectionRuntime.h"

#include <atomic>
#include <cstdint>
#include <windows.h>

namespace MirrorNativeWorldReentry
{

	inline constexpr std::uintptr_t kRVA_DoUmbraQuery = 0x284F1D0;
	inline constexpr std::uintptr_t kRVA_LightUpdate = 0x284DDD0;
	inline constexpr std::uintptr_t kRVA_MainAccum = 0x284F350;
	inline constexpr std::uintptr_t kRVA_MainRenderSetup = 0x28503F0;
	inline constexpr std::uintptr_t kRVA_DeferredPrePass = 0x2850CB0;
	inline constexpr std::uintptr_t kRVA_DeferredDecals = 0x2852590;
	inline constexpr std::uintptr_t kRVA_ImagespaceSAO = 0x2851540;
	inline constexpr std::uintptr_t kRVA_DeferredLightsImpl = 0x28529B0;
	inline constexpr std::uintptr_t kRVA_DeferredComposite = 0x2855E60;
	inline constexpr std::uintptr_t kRVA_Forward = 0x2856B70;
	inline constexpr std::uintptr_t kRVA_Refraction = 0x2856FA0;

	inline constexpr std::uintptr_t kRVA_pCamera = 0x6723238;
	inline constexpr std::uintptr_t kRVA_pVisCamera = 0x6723240;

	inline std::atomic<std::uint64_t> g_attempts{}, g_completed{}, g_faults{};

	[[nodiscard]] inline bool Available() noexcept
	{
		for (const auto rva : { kRVA_DoUmbraQuery, kRVA_LightUpdate, kRVA_MainAccum, kRVA_MainRenderSetup,
				 kRVA_DeferredPrePass, kRVA_DeferredDecals, kRVA_ImagespaceSAO, kRVA_DeferredLightsImpl,
				 kRVA_DeferredComposite, kRVA_Forward, kRVA_Refraction, kRVA_pCamera, kRVA_pVisCamera })
			if (ReflectionRuntime::Rva(rva) == 0)
				return false;
		return true;
	}

	inline std::atomic<bool> g_enabled{ false };
	[[nodiscard]] inline bool Enabled() noexcept { return g_enabled.load(std::memory_order_acquire); }

	bool RenderReflectedInto(void* a_reflectedCamera, struct ID3D11UnorderedAccessView* a_destination,
		std::uint32_t a_width, std::uint32_t a_height) noexcept;

	void ReleaseResources() noexcept;
}

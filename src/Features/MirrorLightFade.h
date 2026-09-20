#pragma once
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>

namespace MirrorLightFade
{
	constexpr std::uint32_t kCulledMarker = 0xFFu;   

	inline bool g_useEngineFade = false;

	inline bool g_rejectPrevisCulled = false;
	constexpr std::uint64_t kMemoryMs = 10u * 60u * 1000u;

	constexpr std::uint64_t kPrevisGraceMs = 1000u;

	struct Entry
	{
		const void* native{};
		float fade{};
		std::uint64_t seenMs{};
	};
	inline std::array<Entry, 64> g_entries{};   
	inline std::size_t g_next = 0;

	inline std::uint64_t NowMs() noexcept
	{
		using namespace std::chrono;
		return static_cast<std::uint64_t>(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
	}

	inline void Remember(const void* native, float fade) noexcept
	{
		const auto now = NowMs();
		for (auto& entry : g_entries) {
			if (entry.native == native) { entry.fade = fade; entry.seenMs = now; return; }
		}
		g_entries[g_next] = { native, fade, now };
		g_next = (g_next + 1) % g_entries.size();
	}

	struct PrevisEntry
	{
		const void* native{};
		std::uint64_t visibleMs{};
	};
	inline std::array<PrevisEntry, 256> g_previsEntries{};   
	inline std::size_t g_previsNext = 0;

	inline bool PrevisAllows(const void* native, bool culled) noexcept
	{
		const auto now = NowMs();
		if (!culled) {
			for (auto& entry : g_previsEntries)
				if (entry.native == native) { entry.visibleMs = now; return true; }
			g_previsEntries[g_previsNext] = { native, now };
			g_previsNext = (g_previsNext + 1) % g_previsEntries.size();
			return true;
		}
		for (const auto& entry : g_previsEntries)
			if (entry.native == native) return now - entry.visibleMs <= kPrevisGraceMs;
		return false;
	}

	inline float Resolve(const void* native, float visibilityFade, bool culled) noexcept
	{
		if (visibilityFade > 0.0f) {
			if (!culled) Remember(native, visibilityFade);
			return visibilityFade;
		}
		if (!culled) return visibilityFade;   
		const auto now = NowMs();
		for (const auto& entry : g_entries) {
			if (entry.native == native && now - entry.seenMs <= kMemoryMs)
				return entry.fade > 0.0f ? entry.fade : 1.0f;
		}
		return 1.0f;
	}
}

#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include "MirrorCaptureExtent.h"

namespace MirrorCaptureOptimizations
{
	inline std::atomic_bool master{ true };

	inline std::atomic_bool roomReuse{ false };
	
	inline std::atomic_bool screenSizedCapture{ true };
	inline std::atomic_bool rectangularCapture{ true };
	
	inline std::atomic_bool tinyObjectSkip{ true };
	
	inline std::atomic_bool paneCrop{ true };

	inline constexpr float kTinyObjectPixels = 1.5f;
	
	inline constexpr float kScreenSizedOversample = 1.5f;
	inline constexpr std::uint32_t kScreenSizedMinimum = 512u;
	
	inline constexpr float kPaneCropMargin = 0.25f;

	inline bool On(const std::atomic_bool& option) noexcept
	{
		return master.load(std::memory_order_relaxed) && option.load(std::memory_order_relaxed);
	}

	constexpr std::uint32_t ScreenSizedCapture(double panePixels, std::uint32_t chosen) noexcept
	{
		if (chosen <= kScreenSizedMinimum || !(panePixels > 0.0) || panePixels > 1.0e6)
			return chosen;
		const double wanted = panePixels * kScreenSizedOversample;
		std::uint32_t size = kScreenSizedMinimum;
		while (size < chosen && double(size) < wanted)
			size += kScreenSizedMinimum;
		return (std::min)(size, chosen);
	}
	static_assert(ScreenSizedCapture(200.0, 4096u) == 512u);
	static_assert(ScreenSizedCapture(400.0, 4096u) == 1024u);
	static_assert(ScreenSizedCapture(800.0, 4096u) == 1536u);
	static_assert(ScreenSizedCapture(1500.0, 2048u) == 2048u);
	static_assert(ScreenSizedCapture(0.0, 2048u) == 2048u);
	static_assert(ScreenSizedCapture(100.0, 512u) == 512u);

	struct ScreenSizeState
	{
		std::uint32_t pixels{}, current{}, ceiling{}, lower{};
		std::uint64_t lowerSince{};
		std::uint32_t Choose(std::uint32_t chosen, std::uint64_t now) noexcept
		{
			const auto wanted = ScreenSizedCapture(double(pixels), chosen);
			if (!current || chosen != ceiling || wanted >= current || !pixels) {
				current = wanted; ceiling = chosen; lower = 0; lowerSince = 0;
				return current;
			}
			if (double(pixels) * kScreenSizedOversample > double(wanted) * .85) {
				lower = 0; lowerSince = 0;
				return current;
			}
			if (lower != wanted || now < lowerSince) { lower = wanted; lowerSince = now; }
			else if (now - lowerSince >= 1000) { current = wanted; lower = 0; lowerSince = 0; }
			return current;
		}
	};


	// Automatic and presets share spatial cadence; manual rates remain explicit.
	// A limit is independent of the selected rate, so Low and Automatic have the
	// same movement policy at the same projected size, even when Low starts at 30 Hz.
	struct ScreenCadenceState
	{
		static constexpr std::uint32_t kUncapped = 0xFFFFu;
		std::uint32_t current{kUncapped}, pending{};
		std::uint64_t lowerSince{};
		bool Limited() const noexcept { return current != kUncapped; }
		std::uint32_t Choose(std::uint32_t hz, std::uint32_t width, std::uint32_t height,
			std::uint64_t now, bool enabled) noexcept
		{
			if (!enabled || !width || !height) { *this = {}; return hz; }
			const auto pixels = (std::max)(width, height);
			const auto wanted = pixels <= 64u || (current == 15u && pixels <= 96u) ? 15u :
				pixels <= 192u || (current == 30u && pixels <= 256u) ? 30u : kUncapped;
			if (wanted >= current) { current = wanted; pending = 0; }
			else if (pending != wanted || now < lowerSince) { pending = wanted; lowerSince = now; }
			else if (now - lowerSince >= 1000u) { current = wanted; pending = 0; }
			return (std::min)(hz, current);
		}
	};

	struct ScreenExtentState
	{
		ScreenSizeState x{}, y{};
		ScreenCadenceState cadence;
		MirrorCaptureExtent Choose(unsigned ceiling, std::uint64_t now, bool rectangular) noexcept
		{
			if (!x.pixels || !y.pixels) {
				x.lower = y.lower = 0; x.lowerSince = y.lowerSince = 0;
				if (!x.current || !y.current || x.ceiling != ceiling || y.ceiling != ceiling) {
					x.current = y.current = ceiling; x.ceiling = y.ceiling = ceiling;
				}
				return rectangular ? MirrorCaptureExtent{x.current,y.current} : MirrorCaptureExtent{(std::max)(x.current,y.current)};
			}
			const auto width=x.Choose(ceiling,now), height=y.Choose(ceiling,now);
			return rectangular ? MirrorCaptureExtent{width,height} : MirrorCaptureExtent{(std::max)(width,height)};
		}
	};

	struct WorkerExtentState
	{
		std::uint64_t xSince{}, ySince{};
		static unsigned Axis(unsigned current, unsigned wanted, std::uint64_t now, std::uint64_t& since) noexcept
		{
			if (wanted >= current || !current) { since = 0; return wanted; }
			if (!since || now < since) since = now;
			if (now - since >= 3000) { since = 0; return wanted; }
			return current;
		}
		MirrorCaptureExtent Choose(MirrorCaptureExtent current, MirrorCaptureExtent wanted, std::uint64_t now) noexcept
		{ return {Axis(current.width,wanted.width,now,xSince), Axis(current.height,wanted.height,now,ySince)}; }
	};

	constexpr bool Tiny(float radius, float w, float pixelScale) noexcept
	{
		if (!(w > radius) || !(pixelScale > 0.0f) || !(radius >= 0.0f)) return false;
		return 2.0f * radius * pixelScale / (2.0f * w) < kTinyObjectPixels;
	}
	static_assert(Tiny(1.0f, 5000.0f, 2048.0f));
	static_assert(!Tiny(10.0f, 100.0f, 2048.0f));
	static_assert(!Tiny(1.0f, 0.5f, 2048.0f));
}

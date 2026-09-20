#pragma once

#include <algorithm>
#include <array>
#include <cstdint>

namespace MirrorSettingsPolicy
{
	inline constexpr std::array<std::uint32_t, 4> kResolutions{ 512, 1024, 2048, 4096 };

	inline constexpr std::array<std::uint32_t, 1> kLightBudgets{ 256 };
	inline constexpr std::uint32_t kDefaultLightBudget = 256u;
	inline constexpr int kDefaultLightBudgetChoice = 0;
	inline constexpr std::uint32_t kMaximumRefreshRate = 180u;
	
	inline constexpr std::uint32_t kEveryFrame = 0xFFFFu;
	
	constexpr std::uint32_t Resolution(std::uint32_t value) noexcept
	{
		for (const auto size : kResolutions) if (value == size) return value;
		return 0;
	}
	
	constexpr std::uint32_t LightBudget(std::uint32_t value) noexcept
	{
		for (const auto budget : kLightBudgets) if (value == budget) return value;
		return kDefaultLightBudget;
	}
	
	constexpr std::uint32_t LightBudgetFromChoice(int choice) noexcept
	{
		return choice >= 0 && choice < static_cast<int>(kLightBudgets.size()) ?
			kLightBudgets[static_cast<std::size_t>(choice)] : kDefaultLightBudget;
	}
	constexpr std::uint32_t RefreshRate(std::uint32_t value) noexcept
	{
		if (value == kEveryFrame) return value;
		return value == 0 ? 0 : std::clamp(value, 5u, kMaximumRefreshRate);
	}
	constexpr std::uint64_t Interval(std::uint32_t hz, std::uint64_t fallback = 16667u) noexcept
	{

		if (hz == kEveryFrame) return 1u;
		return hz ? (1000000u + RefreshRate(hz) / 2u) / RefreshRate(hz) : fallback;
	}
	constexpr std::uint64_t PublicationLifetime(std::uint32_t hz) noexcept
	{
		return (std::max)(std::uint64_t{ 250000 }, 3u * Interval(hz));
	}

	class CaptureClock
	{
	public:
		bool Ready(std::uint64_t now, std::uint64_t interval) const noexcept
		{
			return !started_ || interval != interval_ || now < last_ || now >= next_;
		}
		bool Claim(std::uint64_t now, std::uint64_t interval = 16667u) noexcept
		{
			interval = (std::max)(std::uint64_t{ 1 }, interval);
			if (!started_ || interval != interval_ || now < last_) {
				started_ = true;
				next_ = now;
				interval_ = interval;
			}
			last_ = now;
			if (now < next_) return false;
			next_ += ((now - next_) / interval + 1u) * interval;
			return true;
		}
		void Reset() noexcept { *this = {}; }
	private:
		std::uint64_t last_{}, next_{}, interval_{};
		bool started_{};
	};
}

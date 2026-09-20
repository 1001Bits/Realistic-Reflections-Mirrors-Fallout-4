#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace MirrorPerformance
{

	struct FrameHistogram
	{
		static constexpr std::size_t kBuckets = 2501;
		static constexpr double kBucketMs = 0.1;

		std::array<std::uint32_t, kBuckets> counts{};
		std::uint64_t total{};

		void Add(double milliseconds) noexcept
		{
			if (!(milliseconds >= 0.0) || !std::isfinite(milliseconds)) return;
			const auto bucket = (std::min)(static_cast<std::size_t>(milliseconds / kBucketMs), kBuckets - 1);
			++counts[bucket];
			++total;
		}
		void Clear() noexcept
		{
			counts = {};
			total = 0;
		}
		
		double Percentile(double fraction) const noexcept
		{
			if (!total) return 0.0;
			const auto wanted = static_cast<std::uint64_t>(std::ceil(std::clamp(fraction, 0.0, 1.0) * double(total)));
			const auto rank = std::clamp<std::uint64_t>(wanted, 1u, total);
			std::uint64_t seen = 0;
			for (std::size_t bucket = 0; bucket < kBuckets; ++bucket) {
				seen += counts[bucket];
				if (seen >= rank) return double(bucket + 1) * kBucketMs;
			}
			return double(kBuckets) * kBucketMs;
		}
	};
}

#pragma once

#include <algorithm>
#include <cstdint>
#include "MirrorFrameHistogram.h"

namespace MirrorPerformance
{
	struct FrameTiming
	{
		double previousMs{ -1.0 }, totalMs{}, maximumMs{};
		std::uint64_t count{}, over33ms{}, over50ms{};
		FrameHistogram histogram{};

		bool Observe(double nowMs, bool active) noexcept
		{
			if (!active || nowMs < previousMs) {
				*this = {};
				return false;
			}
			const double previous = previousMs;
			previousMs = nowMs;
			if (previous < 0.0)
				return false;
			const double elapsed = nowMs - previous;
			totalMs += elapsed;
			maximumMs = (std::max)(maximumMs, elapsed);
			histogram.Add(elapsed);
			++count;
			over33ms += elapsed > 33.333333;
			over50ms += elapsed > 50.0;
			return totalMs >= 5000.0;
		}

		void NextWindow() noexcept
		{
			const auto previous = previousMs;
			*this = {};
			previousMs = previous;
		}
	};

	struct PhaseFrameTiming
	{
		FrameTiming frames;
		std::uint64_t phase{}, configuration{};
		double resumedMs{ -1.0 }, lastMs{ -1.0 };
		bool Observe(double nowMs, bool worldActive, std::uint64_t currentPhase, std::uint64_t currentConfiguration = 0) noexcept
		{
			if (phase != currentPhase || configuration != currentConfiguration || !worldActive || nowMs < lastMs) {
				frames = {};
				resumedMs = -1.0;
				phase = currentPhase;
				configuration = currentConfiguration;
			}
			lastMs = nowMs;
			if (!worldActive || !phase) return false;
			if (resumedMs < 0.0) resumedMs = nowMs;
			if (nowMs - resumedMs < 5000.0) return false;
			return frames.Observe(nowMs, true);
		}
	};
}

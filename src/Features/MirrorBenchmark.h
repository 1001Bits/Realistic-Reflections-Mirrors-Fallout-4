#pragma once

#include "MirrorFrameHistogram.h"
#include <array>
#include <cstdint>
#include <string_view>

namespace MirrorPerformance
{
	enum class StepKind : std::uint8_t { kOff, kPreset, kManual };
	struct BenchmarkStep
	{
		std::string_view name;
		StepKind kind{};
		std::uint8_t level{};         
		std::uint32_t resolution{};   
		std::uint32_t refreshHz{};    
		bool roomReuse{ false };
		bool screenSizedCapture{ true };
		bool tinyObjectSkip{ true };
		bool paneCrop{ true };
		bool rectangularCapture{ true };
	};
	inline constexpr std::uint32_t kBenchmarkEveryFrame = 0xFFFFu;
	inline constexpr std::array<BenchmarkStep, 12> kBenchmarkSteps{ {
		{ "Mirrors off", StepKind::kOff },
		{ "Low", StepKind::kPreset, 0 },
		{ "Medium", StepKind::kPreset, 1 },
		{ "High", StepKind::kPreset, 2 },
		{ "4096 at 30 Hz", StepKind::kManual, 1, 4096, 30 },
		{ "2048 every frame", StepKind::kManual, 1, 2048, kBenchmarkEveryFrame },
		{ "Medium, full-size capture", StepKind::kPreset, 1, 0, 0, false, false, true, true },
		{ "Medium, no tiny skip", StepKind::kPreset, 1, 0, 0, false, true, false, true },
		{ "Medium, whole-pane capture", StepKind::kPreset, 1, 0, 0, false, true, true, false },
		{ "Medium, square capture", StepKind::kPreset, 1, 0, 0, false, true, true, true, false },
		{ "Medium, no optimizations", StepKind::kPreset, 1, 0, 0, false, false, false, false },
		
		{ "Medium again (drift check)", StepKind::kPreset, 1 },
	} };

	class BenchmarkClock
	{
	public:
		static constexpr std::uint64_t kCountdown = 5'000'000, kSettle = 3'000'000, kMeasure = 8'000'000;
		enum class Event : std::uint8_t { kNone, kApplyStep, kBeginMeasure, kEndMeasure, kFinished };
		void Start() noexcept { running_ = true; step_ = 0; phase_ = Phase::kCountdown; elapsed_ = 0; }
		void Stop() noexcept { running_ = false; phase_ = Phase::kIdle; }
		bool Running() const noexcept { return running_; }
		std::size_t Step() const noexcept { return step_; }
		bool Measuring() const noexcept { return running_ && phase_ == Phase::kMeasure; }
		bool CountingDown() const noexcept { return running_ && phase_ == Phase::kCountdown; }
		std::uint64_t Remaining() const noexcept
		{
			const auto length = phase_ == Phase::kCountdown ? kCountdown : (phase_ == Phase::kSettle ? kSettle : kMeasure);
			return elapsed_ < length ? length - elapsed_ : 0;
		}
		static constexpr std::uint64_t TotalMicroseconds() noexcept
		{
			return kCountdown + kBenchmarkSteps.size() * (kSettle + kMeasure);
		}
		
		Event Advance(std::uint64_t delta, bool eligible) noexcept
		{
			if (!running_) return Event::kNone;
			if (eligible) elapsed_ += delta;
			switch (phase_) {
			case Phase::kCountdown:
				if (elapsed_ < kCountdown) return Event::kNone;
				phase_ = Phase::kSettle; elapsed_ = 0; step_ = 0;
				return Event::kApplyStep;
			case Phase::kSettle:
				if (elapsed_ < kSettle) return Event::kNone;
				phase_ = Phase::kMeasure; elapsed_ = 0;
				return Event::kBeginMeasure;
			case Phase::kMeasure:
				if (elapsed_ < kMeasure) return Event::kNone;
				phase_ = Phase::kNext; elapsed_ = 0;
				return Event::kEndMeasure;
			case Phase::kNext:
				if (step_ + 1 >= kBenchmarkSteps.size()) { running_ = false; phase_ = Phase::kIdle; return Event::kFinished; }
				++step_; phase_ = Phase::kSettle; elapsed_ = 0;
				return Event::kApplyStep;
			default:
				return Event::kNone;
			}
		}
	private:
		enum class Phase : std::uint8_t { kIdle, kCountdown, kSettle, kMeasure, kNext };
		Phase phase_{ Phase::kIdle };
		std::size_t step_{};
		std::uint64_t elapsed_{};
		bool running_{};
	};

	struct StepFrames
	{
		FrameHistogram histogram;
		double milliseconds{};
		std::uint64_t frames{};
		void Add(double interval) noexcept { histogram.Add(interval); milliseconds += interval; ++frames; }
		double Average() const noexcept { return frames ? milliseconds / double(frames) : 0.0; }
	};
}

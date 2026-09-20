#pragma once

#include "MirrorFrameHistogram.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace MirrorAdaptiveQuality
{
	struct Quality
	{
		std::uint32_t resolution, refreshRate, objectDistance, shadowDistance, shadowResolution;
		bool lowSceneCost{};
	};

	inline constexpr std::array<Quality, 5> kLevels{{
		{2048, 60, 5000, 2500, 2048},
		{1024, 60, 5000, 2500, 2048},
		{1024, 30, 2500, 1500, 1024, true},
		{ 512, 30, 2500, 1500, 1024, true},
		{2048, 30, 2500, 1500, 1024, true},
	}};
	inline constexpr unsigned kInitialLevel = 0;
	inline constexpr unsigned kDefaultTargetFPS = 60;
	constexpr unsigned TargetFPS(int value) noexcept { return static_cast<unsigned>(std::clamp(value, 30, 144)); }
	constexpr Quality At(unsigned level) noexcept { return kLevels[level < kLevels.size() ? level : kInitialLevel]; }


	struct SceneSettings
	{
		std::uint32_t objectDistance{}, shadowDistance{}, shadowResolution{};
		bool shadows{}, lowShadows{};
	};
	constexpr SceneSettings ConstrainScene(Quality quality, SceneSettings configured) noexcept
	{
		if (!quality.lowSceneCost) return configured;
		const auto cap = [](std::uint32_t value, std::uint32_t ceiling) {
			return value ? (std::min)(value, ceiling) : ceiling;
		};
		configured.objectDistance = cap(configured.objectDistance, quality.objectDistance);
		configured.shadowDistance = cap(configured.shadowDistance, quality.shadowDistance);
		configured.shadowResolution = cap(configured.shadowResolution, quality.shadowResolution);
		// Retain an explicit master-off setting. Low's reduced sunlight remains
		// available when the configured scene already permits reflection shadows.
		configured.lowShadows = configured.lowShadows || configured.shadows;
		configured.shadows = false;
		return configured;
	}

	enum class Decision : unsigned { Initial, ResolutionTrial, ResolutionHelped, ResolutionRestored, CadenceReduced, Recovery, Inconclusive };
	constexpr const char* Name(Decision decision) noexcept
	{
		switch (decision) {
		case Decision::ResolutionTrial: return "measuring lower resolution";
		case Decision::ResolutionHelped: return "lower resolution improved frame times";
		case Decision::ResolutionRestored: return "no measured gain; restored resolution";
		case Decision::CadenceReduced: return "sustained pressure; reduced scene cost; nearby movement cadence retained";
		case Decision::Recovery: return "stable frame times; recovering quality";
		case Decision::Inconclusive: return "scene workload changed; restored detail; retry deferred";
		default: return "initial quality";
		}
	}

	class Controller
	{
	public:
		void Reset(unsigned targetFPS) noexcept
		{
			*this = Controller{};
			targetMs_ = 1000.0 / TargetFPS(static_cast<int>(targetFPS));
		}
		unsigned Level() const noexcept { return level_; }
		Decision LastDecision() const noexcept { return decision_; }
		bool Trial() const noexcept { return trial_; }
		struct Evidence { double baselineMean{}, mean{}, baselineP95{}, p95{}, baselineWork{}, work{}; bool comparable{}; };
		const Evidence& LastEvidence() const noexcept { return evidence_; }
		unsigned EvidenceSerial() const noexcept { return evidenceSerial_; }
		unsigned Observe(double nowMs, bool active, std::uint64_t publications, std::uint64_t resourceGeneration = 0,
			std::uint64_t sceneCaptures = 0, std::uint64_t sceneNodes = 0, std::uint64_t bodyFaults = 0) noexcept
		{
			if (!std::isfinite(nowMs)) return level_;
			const auto captures = sceneCaptures >= previousCaptures_ ? sceneCaptures - previousCaptures_ : 0;
			const auto nodes = sceneNodes >= previousNodes_ ? sceneNodes - previousNodes_ : 0;
			previousCaptures_ = sceneCaptures; previousNodes_ = sceneNodes;
			const double elapsed = nowMs - previousMs_;
			const bool published = publications != publications_;
			publications_ = publications;
			if (published) lastPublicationMs_ = nowMs;
			previousMs_ = nowMs;
			if (bodyFaults != bodyFaults_) {
				bodyFaults_ = bodyFaults;

				if (trial_) {level_ = trialFrom_;trial_ = false;decision_ = Decision::Inconclusive;}
				nextTrialMs_[level_] = nowMs + 15000;
				Settle(nowMs,2000);
				return level_;
			}
			if (!active || (!published && (lastPublicationMs_ < 0 || nowMs - lastPublicationMs_ > 500)) ||
				elapsed <= 0 || elapsed > 250) {
				
				if (trial_) { level_ = trialFrom_; trial_ = false; decision_ = Decision::ResolutionRestored; }
				Settle(nowMs, 2000);
				return level_;
			}
			if (resourceGeneration != resourceGeneration_) {
				resourceGeneration_ = resourceGeneration;
				Settle(nowMs, 2000);
			}
			if (nowMs < warmUntilMs_) return level_;
			measuredCaptures_ += captures; measuredNodes_ += nodes;
			windowMs_ += elapsed; ++frames_;
			slowFrames_ += elapsed > targetMs_ * 1.15;
			measurementMs_ += elapsed; histogram_.Add(elapsed);
			if (windowMs_ < 500 || frames_ < 8) return level_;
			const double average = windowMs_ / frames_;
			const bool pressure = average > targetMs_ * 1.10 && slowFrames_ * 5 >= frames_;
			const bool stable = average <= targetMs_ * 1.05 && slowFrames_ * 20 <= frames_;
			slowWindows_ = pressure ? slowWindows_ + 1 : 0;
			stableMs_ = stable ? stableMs_ + windowMs_ : 0;
			windowMs_ = 0; frames_ = slowFrames_ = 0;
			
			const bool measured = measurementMs_ >= (level_ == 2 || level_ == 3 ? 4000 : 2000) && histogram_.total >= 60;
			if (trial_ && measured) {
				const double mean = measurementMs_ / double(histogram_.total);
				const double tail = histogram_.Percentile(.95);
				const double workload = measuredCaptures_ ? double(measuredNodes_) / measuredCaptures_ : 0;
				const bool comparable = baselineWorkload_ > 0 && workload > 0 &&
					std::abs(workload / baselineWorkload_ - 1) <= .10;
				evidence_ = {baselineMean_, mean, baselineTail_, tail, baselineWorkload_, workload, comparable};
				++evidenceSerial_;

				const double minimumMeanGain = level_ == 3 ? .10 : .05;
				const double minimumTailGain = level_ == 3 ? .15 : .10;
				const bool helped = comparable && ((mean <= baselineMean_ - (std::max)(.5, baselineMean_ * minimumMeanGain) && tail <= baselineTail_ * 1.02) ||
					(tail <= baselineTail_ - (std::max)(1.0, baselineTail_ * minimumTailGain) && mean <= baselineMean_ * 1.02));
				trial_ = false;
				decision_ = helped ? Decision::ResolutionHelped : (comparable ? Decision::ResolutionRestored : Decision::Inconclusive);
				if (!helped) {

					level_ = trialFrom_ == 0 ? 4 : trialFrom_;
					nextTrialMs_[trialFrom_] = nowMs + (comparable ? 60000 : 15000);
					if (!comparable) nextTrialMs_[level_] = nowMs + 15000;
				}
				nextRaiseMs_ = nowMs + 60000;
				Settle(nowMs, 1000);
			} else if (!trial_ && slowWindows_ >= 2 && measured) {
				unsigned next = level_;
				if (level_ == 1) next = 2;
				else if (nowMs >= nextTrialMs_[level_]) {
					if (level_ == 0) next = 1;
					else if (level_ == 4) next = 2;
					else if (level_ == 2) next = 3;
				} else if (level_ == 0) next = 4;
				if (next != level_) {
					trial_ = At(next).resolution < At(level_).resolution;
					trialFrom_ = level_;
					baselineMean_ = measurementMs_ / double(histogram_.total);
					baselineTail_ = histogram_.Percentile(.95);
					baselineWorkload_ = measuredCaptures_ ? double(measuredNodes_) / measuredCaptures_ : 0;
					level_ = next;
					decision_ = trial_ ? Decision::ResolutionTrial : Decision::CadenceReduced;
					nextRaiseMs_ = nowMs + 60000;
					Settle(nowMs, 1000);
				} else ClearMeasurement();
			} else if (!trial_ && stableMs_ >= 20000 && nowMs >= nextRaiseMs_ && level_ != 0) {
				level_ = level_ == 4 ? 0 : level_ - 1;
				decision_ = Decision::Recovery;
				Settle(nowMs, 4000);
			} else if (!trial_ && measured) ClearMeasurement();
			return level_;
		}
	private:
		void ClearMeasurement() noexcept { measurementMs_ = 0; measuredCaptures_ = measuredNodes_ = 0; histogram_.Clear(); }
		void Settle(double nowMs, double delay) noexcept
		{
			warmUntilMs_ = nowMs + delay;
			windowMs_ = stableMs_ = 0; frames_ = slowFrames_ = slowWindows_ = 0;
			ClearMeasurement();
		}
		unsigned level_{kInitialLevel}, frames_{}, slowFrames_{}, slowWindows_{}, trialFrom_{};
		Decision decision_{Decision::Initial};
		bool trial_{};
		std::uint64_t publications_{}, resourceGeneration_{}, bodyFaults_{};
		std::uint64_t previousCaptures_{}, previousNodes_{}, measuredCaptures_{}, measuredNodes_{};
		double baselineWorkload_{};
		Evidence evidence_{};
		unsigned evidenceSerial_{};
		double targetMs_{1000.0 / kDefaultTargetFPS}, previousMs_{-1}, lastPublicationMs_{-1};
		double warmUntilMs_{2000}, windowMs_{}, stableMs_{}, nextRaiseMs_{}, measurementMs_{};
		double baselineMean_{}, baselineTail_{};
		std::array<double, kLevels.size()> nextTrialMs_{};
		MirrorPerformance::FrameHistogram histogram_;
	};
}

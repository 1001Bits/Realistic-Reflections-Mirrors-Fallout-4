#pragma once

#include "MirrorSettingsPolicy.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <span>
#include <unordered_map>
#include <vector>

namespace MirrorFleet
{
	struct Candidate
	{
		std::uint32_t id{};
		bool visible{};
	};

	struct FrameRequest
	{
		std::uint32_t id{};
		std::uint64_t interval{};
		std::size_t candidate{};
		std::uint32_t hz{};
		bool screenLimited{};
	};

	class Schedule
	{
	public:
		void Observe(std::uint32_t id) { if (id) entries_.try_emplace(id); }
		void Forget(std::uint32_t id) { entries_.erase(id); }
		void Clear() { entries_.clear(); order_ = 0; frameSeen_ = false; }
		bool BeginFrame(std::uint32_t frame) noexcept
		{
			if (frameSeen_ && frame == lastFrame_) return false;
			lastFrame_ = frame;
			frameSeen_ = true;
			return true;
		}
		bool Ready(std::uint32_t id, std::uint64_t now, std::uint64_t interval) const noexcept
		{
			const auto entry = entries_.find(id);
			return entry == entries_.end() || !interval || entry->second.clock.Ready(now, interval);
		}

		void Deferred(std::uint32_t id) noexcept
		{
			const auto entry = entries_.find(id);
			if (entry != entries_.end()) entry->second.clock.Reset();
		}

		void Attempted(std::uint32_t id, std::uint64_t now, std::uint64_t interval) noexcept
		{
			const auto entry = entries_.find(id);
			if (entry == entries_.end()) return;
			entry->second.order = ++order_;
			if (interval) (void)entry->second.clock.Claim(now, interval);
			else entry->second.clock.Reset();
		}

		template<class Area>
		void PrioritizeDue(std::vector<FrameRequest>& requests, std::uint64_t now,
			std::size_t budget, Area&& area) const
		{

			std::stable_sort(requests.begin(), requests.end(), [](const auto& a, const auto& b) {
				return a.id < b.id;
			});
			requests.erase(std::unique(requests.begin(), requests.end(), [](const auto& a, const auto& b) {
				return a.id == b.id;
			}), requests.end());
			const auto starvationTurns = 4ull * (std::max)(std::size_t{1}, requests.size());
			std::erase_if(requests, [&](const auto& request) {
				return !request.id || entries_.find(request.id) == entries_.end() ||
					!Ready(request.id, now, (std::max)(std::uint64_t{1}, request.interval));
			});
			const auto screenArea = [&](const auto& request) {
				const float value = area(request);
				return std::isfinite(value) ? (std::max)(0.0f, value) : 0.0f;
			};
			float dominantArea = 0.0f;
			for (const auto& request : requests)
				dominantArea = (std::max)(dominantArea, screenArea(request));
			const auto rank = [&](const auto& request) {

				if (order_ - Order(request.id) > starvationTurns) return 3;
				if (!(dominantArea > 0.0f)) return 0;
				const float share = screenArea(request) / dominantArea;
				return share >= 0.5f ? 2 : (share >= 0.125f ? 1 : 0);
			};
			std::stable_sort(requests.begin(), requests.end(), [&](const auto& a, const auto& b) {
				const auto rankA = rank(a), rankB = rank(b);
				return rankA != rankB ? rankA > rankB : Order(a.id) < Order(b.id);
			});
			if (requests.size() > budget) requests.resize(budget);
		}

		template<class Capture>
		void VisitDue(std::span<const FrameRequest> requests, std::uint64_t now, Capture&& capture)
		{
			for (const auto& request : requests) {
				const auto interval = (std::max)(std::uint64_t{ 1 }, request.interval);
				if (!request.id || entries_.find(request.id) == entries_.end() ||
					!Ready(request.id, now, interval)) continue;
				Attempted(request.id, now, interval);
				if (!capture(request)) break;
			}
		}

		std::uint64_t Order(std::uint32_t id) const noexcept
		{
			const auto entry = entries_.find(id);
			return entry == entries_.end() ? 0 : entry->second.order;
		}
		std::uint64_t Turn() const noexcept { return order_; }

		std::uint32_t Select(std::span<const Candidate> candidates,
			std::span<const std::uint32_t> reserved, std::uint32_t incumbent,
			bool inFlight, std::uint64_t now, std::uint64_t interval) const noexcept
		{
			const auto available = [&](const Candidate& candidate) {
				return candidate.id && candidate.visible &&
					std::find(reserved.begin(), reserved.end(), candidate.id) == reserved.end();
			};
			if (inFlight) {
				for (const auto& candidate : candidates)
					if (candidate.id == incumbent && candidate.id &&
						std::find(reserved.begin(), reserved.end(), candidate.id) == reserved.end()) return incumbent;
			}
			std::uint32_t selected = 0;
			std::uint64_t oldest = ~std::uint64_t{};
			for (const auto& candidate : candidates) {
				if (!available(candidate) || !Ready(candidate.id, now, interval)) continue;
				const auto entry = entries_.find(candidate.id);
				const auto age = entry == entries_.end() ? 0u : entry->second.order;
				if (!selected || age < oldest || (age == oldest && candidate.id == incumbent)) {
					selected = candidate.id;
					oldest = age;
				}
			}

			if (!selected)
				for (const auto& candidate : candidates)
					if (candidate.id == incumbent && available(candidate)) return incumbent;
			return selected;
		}
	private:
		struct Entry { std::uint64_t order{}; MirrorSettingsPolicy::CaptureClock clock; };
		std::unordered_map<std::uint32_t, Entry> entries_;
		std::uint64_t order_{};
		std::uint32_t lastFrame_{};
		bool frameSeen_{};
	};

	class MotionFocus
	{
	public:

		static constexpr std::uint64_t kHold = 150000u;

		static constexpr float kSwitchRatio = 1.25f;

		void Observe(bool cameraChanged, std::uint64_t now) noexcept
		{
			if (!cameraChanged) return;
			movedAt_ = now;
			moved_ = true;
		}
		bool Moving(std::uint64_t now) const noexcept
		{
			return moved_ && now >= movedAt_ && now - movedAt_ <= kHold;
		}
		std::uint32_t Focus() const noexcept { return focus_; }
		void Clear() noexcept { *this = {}; }

		template<class Area>
		std::uint32_t Apply(std::vector<FrameRequest>& requests, std::uint64_t now, Area&& area, bool preserveCadence = false)
		{
			if (!Moving(now)) return focus_ = 0;
			const auto screenArea = [&](const FrameRequest& request) {
				const float value = area(request);
				return std::isfinite(value) ? (std::max)(0.0f, value) : 0.0f;
			};
			std::uint32_t largest = 0;
			float largestArea = 0.0f, focusArea = 0.0f;
			for (const auto& request : requests) {
				const float value = screenArea(request);
				if (request.id && value > largestArea) {
					largest = request.id;
					largestArea = value;
				}
				if (focus_ && request.id == focus_) focusArea = (std::max)(focusArea, value);
			}
			if (!largest) return focus_ = 0;
			if (!(focusArea > 0.0f) || largestArea > focusArea * kSwitchRatio) focus_ = largest;
			for (auto& request : requests)
				if (request.id == focus_ && !preserveCadence && !request.screenLimited) request.interval = MirrorSettingsPolicy::Interval(MirrorSettingsPolicy::kEveryFrame);
			return focus_;
		}
	private:
		std::uint64_t movedAt_{};
		bool moved_{};
		std::uint32_t focus_{};
	};
}

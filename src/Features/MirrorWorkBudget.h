#pragma once
#include <atomic>
#include <cstdint>
#include <utility>

class MirrorDemandLease
{
public:
	static constexpr std::uint64_t GraceMs = 1500;
	void Publish(std::uint64_t now, bool visible) noexcept
	{
		if (visible)
		{
			const auto previous = deadline_.load(std::memory_order_relaxed);
			if (previous == 0 || now >= previous)
				generation_.fetch_add(1, std::memory_order_acq_rel);
			deadline_.store(now + GraceMs, std::memory_order_release);
		}
	}
	void Clear() noexcept { deadline_.store(0, std::memory_order_release); }
	std::uint64_t Generation() const noexcept { return generation_.load(std::memory_order_acquire); }
	bool Needed(std::uint64_t now) const noexcept
	{
		const auto deadline = deadline_.load(std::memory_order_acquire);
		return deadline != 0 && now < deadline;
	}
	bool AcceptsPose(std::uint64_t now, std::uint64_t poseGeneration) const noexcept
	{
		return poseGeneration != 0 && Needed(now) && poseGeneration == Generation();
	}
private:
	std::atomic<std::uint64_t> deadline_{};
	std::atomic<std::uint64_t> generation_{};
};

template <std::uint32_t Capacity>
class MirrorRetirementBudget
{
	static_assert(Capacity > 0 && Capacity < 32);
public:
	class Lease
	{
	public:
		Lease() = default;
		Lease(MirrorRetirementBudget* owner, std::uint32_t bit) noexcept : owner_(owner), bit_(bit) {}
		Lease(const Lease&) = delete;
		Lease& operator=(const Lease&) = delete;
		Lease(Lease&& other) noexcept : owner_(std::exchange(other.owner_, nullptr)), bit_(other.bit_) {}
		Lease& operator=(Lease&& other) noexcept
		{
			if (this != &other) {
				Reset();
				owner_ = std::exchange(other.owner_, nullptr);
				bit_ = other.bit_;
			}
			return *this;
		}
		~Lease() { Reset(); }
		explicit operator bool() const noexcept { return owner_ != nullptr; }
	private:
		void Reset() noexcept
		{
			if (owner_)
				owner_->occupied_.fetch_and(~bit_, std::memory_order_acq_rel);
			owner_ = nullptr;
		}
		MirrorRetirementBudget* owner_{};
		std::uint32_t bit_{};
	};

	bool Available() const noexcept { return occupied_.load(std::memory_order_acquire) != Mask; }
	Lease Acquire() noexcept
	{
		auto used = occupied_.load(std::memory_order_acquire);
		while (used != Mask) {
			const auto free = (~used) & Mask;
			const auto bit = free & (0u - free);
			if (occupied_.compare_exchange_weak(used, used | bit, std::memory_order_acq_rel))
				return { this, bit };
		}
		return {};
	}
private:
	static constexpr std::uint32_t Mask = (1u << Capacity) - 1u;
	std::atomic<std::uint32_t> occupied_{};
};

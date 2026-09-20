#pragma once

#include <atomic>
#include <cstdint>

namespace MirrorToggle
{
	
	inline std::atomic_bool active{ true };
	inline bool Active() noexcept { return active.load(std::memory_order_acquire); }

	class Requests
	{
	public:
		explicit Requests(std::uintptr_t key = 0x7Bu) noexcept : key_(key) {} 
		bool OnKeyMessage(std::uint32_t message, std::uintptr_t key, std::uintptr_t detail) noexcept
		{
			if (message == 0x0008u) { 
				pending_.store(0u, std::memory_order_release);
				return false;
			}
			const bool down = message == 0x0100u || message == 0x0104u;
			const bool up = message == 0x0101u || message == 0x0105u;
			if (key != key_ || (!down && !up) || (detail & (1u << 29u)) != 0u)
				return false;
			if (down && (detail & (1u << 30u)) == 0u)
				pending_.fetch_xor(1u, std::memory_order_release);
			
			return true;
		}

		void ObserveKey(bool down, bool focused, bool modified) noexcept
		{
			if (!focused || modified) {
				pollArmed_ = false;
				pending_.store(0u, std::memory_order_release);
				return;
			}
			if (!down) {
				pollArmed_ = true;
			} else if (pollArmed_) {
				pollArmed_ = false;
				pending_.fetch_xor(1u, std::memory_order_release);
			}
		}

		bool Consume(bool& enabled) noexcept
		{
			const bool changed = ConsumeToggle(enabled);
			active.store(enabled, std::memory_order_release);
			return changed;
		}
		
		void Clear() noexcept { pending_.store(0u, std::memory_order_release); }
		bool ConsumeToggle(bool& enabled) noexcept
		{
			const bool changed = pending_.exchange(0u, std::memory_order_acq_rel) != 0u;
			if (changed) enabled = !enabled;
			return changed;
		}
	private:
		const std::uintptr_t key_;
		std::atomic_uint32_t pending_{};
		bool pollArmed_{}; 
	};
}

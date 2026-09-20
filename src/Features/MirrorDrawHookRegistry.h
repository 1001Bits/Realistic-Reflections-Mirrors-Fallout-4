#pragma once

#include <windows.h>
#include <atomic>
#include <cstdint>
#include <cstring>

namespace MirrorDrawHooks
{

	class Registry
	{
	public:
		static constexpr unsigned kCallbacksPerSlot = 5; 

		bool Register(unsigned slot, const void* callback) noexcept
		{
			const int index = SlotIndex(slot);
			if (index < 0 || !callback) return false;
			for (auto& entry : callbacks_[index]) {
				const void* expected = nullptr;
				if (entry.compare_exchange_strong(expected, callback,
					std::memory_order_release, std::memory_order_relaxed) || expected == callback)
					return true;
			}
			return false;
		}

		bool Covers(unsigned slot, const void* entry) const noexcept
		{
			const int index = SlotIndex(slot);
			if (index < 0) return false;

			for (unsigned hop = 0; entry && hop != 8; ++hop) {
				for (const auto& callback : callbacks_[index])
					if (callback.load(std::memory_order_acquire) == entry) return true;
				entry = JumpTarget(entry);
			}
			return false;
		}

	private:
		static int SlotIndex(unsigned slot) noexcept
		{
			return slot == 12 ? 0 : slot == 13 ? 1 : slot == 20 ? 2 : -1;
		}
		static const void* JumpTarget(const void* entry) noexcept
		{
			__try {
				const auto* code = static_cast<const std::uint8_t*>(entry);
				const auto address = reinterpret_cast<std::uintptr_t>(entry);
				if (code[0] == 0xE9) {
					std::int32_t displacement;
					std::memcpy(&displacement, code + 1, sizeof(displacement));
					return reinterpret_cast<const void*>(address + 5 + displacement);
				}
				if (code[0] == 0xEB)
					return reinterpret_cast<const void*>(address + 2 + static_cast<std::int8_t>(code[1]));
				const unsigned prefix = code[0] == 0x48 ? 1 : 0;
				if (code[prefix] == 0xFF && code[prefix + 1] == 0x25) {
					std::int32_t displacement;
					std::memcpy(&displacement, code + prefix + 2, sizeof(displacement));
					const void* target;
					std::memcpy(&target, reinterpret_cast<const void*>(address + prefix + 6 + displacement), sizeof(target));
					return target;
				}
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				
			}
			return nullptr;
		}
		std::atomic<const void*> callbacks_[3][kCallbacksPerSlot]{};
	};
	inline Registry registry;
}

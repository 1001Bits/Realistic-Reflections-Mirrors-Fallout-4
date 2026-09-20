#pragma once

#include <Windows.h>
#include <detours/detours.h>
#include <atomic>
#include <initializer_list>
#include <mutex>
#include <span>

namespace stl
{
	inline std::atomic<bool> hookInstallationFailed{ false };

	inline std::mutex& DetourTransactionMutex() noexcept
	{
		static std::mutex mutex;
		return mutex;
	}
	using DetourTransactionLock = std::lock_guard<std::mutex>;

	struct DetourBinding
	{
		PVOID* original;
		PVOID hook;
	};

	inline LONG InstallDetours(std::span<const DetourBinding> bindings) noexcept
	{
		DetourTransactionLock transactionLock{ DetourTransactionMutex() };
		LONG status = DetourTransactionBegin();

		const bool opened = status != ERROR_INVALID_OPERATION;
		if (status == NO_ERROR)
			status = DetourUpdateThread(GetCurrentThread());
		for (const auto& binding : bindings) {
			if (status != NO_ERROR)
				break;
			status = binding.original && *binding.original && binding.hook ?
				DetourAttach(binding.original, binding.hook) : ERROR_INVALID_ADDRESS;
		}
		if (status == NO_ERROR) {
			
			status = DetourTransactionCommit();
		} else if (opened) {
			(void)DetourTransactionAbort();
		}
		if (status != NO_ERROR)
			hookInstallationFailed.store(true, std::memory_order_release);
		return status;
	}

	inline LONG InstallDetours(std::initializer_list<DetourBinding> bindings) noexcept
	{
		return InstallDetours(std::span<const DetourBinding>{ bindings.begin(), bindings.size() });
	}
}

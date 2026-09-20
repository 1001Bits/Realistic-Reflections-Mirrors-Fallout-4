#include "NativePassPoolRecovery.h"
#include "MirrorPassPoolLease.h"
#include "ReflectionRuntime.h"

namespace
{
	thread_local MirrorPassPoolLease g_lease;
	const ReflectionRuntime::RecoveryContract* g_contract = nullptr;
	NativePassPoolRecovery::OwnsScope g_ownsScope = nullptr;
	bool g_installed = false;

	struct Pools
	{
		std::byte* passes;
		std::byte* lights;
		long passCount, lightCount, mtaMode;
		bool valid;
	};

	Pools ReadPools()
	{
		Pools result{};
		if (!g_contract)
			return result;
		result.passes = *reinterpret_cast<std::byte**>(REL::Offset(g_contract->passPoolSlot).address());
		result.lights = *reinterpret_cast<std::byte**>(REL::Offset(g_contract->lightPoolSlot).address());
		if (reinterpret_cast<std::uintptr_t>(result.passes) <= 0x10000u ||
			reinterpret_cast<std::uintptr_t>(result.lights) <= 0x10000u)
			return result;
		result.passCount = _InterlockedCompareExchange(reinterpret_cast<volatile long*>(result.passes + 0x34), 0, 0);
		result.lightCount = _InterlockedCompareExchange(reinterpret_cast<volatile long*>(result.lights + 0x34), 0, 0);
		result.mtaMode = _InterlockedCompareExchange(
			reinterpret_cast<volatile long*>(REL::Offset(g_contract->mtaMode).address()), 0, 0);
		result.valid = result.passCount >= 0 && result.lightCount >= 0 && result.mtaMode >= 0 && result.mtaMode <= 3;
		return result;
	}

	struct BatchScopeHook
	{
		static void thunk(bool entering)
		{
			if (!g_ownsScope || !g_ownsScope()) {
				func(entering);
				return;
			}
			auto* passes = *reinterpret_cast<void**>(REL::Offset(g_contract->passPoolSlot).address());
			auto* lights = *reinterpret_cast<void**>(REL::Offset(g_contract->lightPoolSlot).address());
			g_lease.RunNative(func.get(), entering, passes, lights);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};
}

namespace NativePassPoolRecovery
{
	bool Install(OwnsScope ownsScope) noexcept
	{
		if (g_installed)
			return g_ownsScope == ownsScope;
		g_contract = ReflectionRuntime::Recovery();
		if (!g_contract || !ownsScope)
			return false;
		const auto address = REL::Offset(g_contract->poolScope).address();
		if (std::memcmp(reinterpret_cast<const void*>(address),
			g_contract->poolScopeEntry.data(), g_contract->poolScopeEntry.size()) != 0) {
			logger::error("[Mirrors] native pass/light pool helper bytes mismatch at {}; capture disabled",
				reinterpret_cast<void*>(address));
			return false;
		}
		g_ownsScope = ownsScope;
		BatchScopeHook::func = address;
		stl::DetourTransactionLock detourTransactionLock{ stl::DetourTransactionMutex() };
		LONG status = DetourTransactionBegin();
		const bool started = status == NO_ERROR;
		if (status == NO_ERROR)
			status = DetourUpdateThread(GetCurrentThread());
		if (status == NO_ERROR)
			status = DetourAttach(reinterpret_cast<PVOID*>(&BatchScopeHook::func),
				reinterpret_cast<PVOID>(BatchScopeHook::thunk));
		if (status == NO_ERROR)
			status = DetourTransactionCommit();
		else if (started)
			(void)DetourTransactionAbort();
		g_installed = status == NO_ERROR;
		logger::info("[Mirrors] native pass/light pool exception cleanup: installed={} status={} address={}",
			g_installed, status, reinterpret_cast<void*>(address));
		return g_installed;
	}

	bool PoolsIdle() noexcept
	{
		if (!g_installed || !g_lease.Valid())
			return false;
		__try {
			const auto first = ReadPools();
			if (!first.valid || first.passCount || first.lightCount || first.mtaMode)
				return false;
			const auto second = ReadPools();
			return second.valid && second.passes == first.passes && second.lights == first.lights &&
				second.passCount == 0 && second.lightCount == 0 && second.mtaMode == 0;
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			return false;
		}
	}

	bool Restore() noexcept
	{
		const auto owed = g_lease.Outstanding();
		if (owed == 0)
			return g_lease.Valid();
		bool restored = false;
		__try {
			const auto pools = ReadPools();
			restored = pools.valid && g_lease.Unwind(BatchScopeHook::func.get(), pools.passes, pools.lights,
				pools.passCount, pools.lightCount, pools.mtaMode);
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			restored = false;
		}
		logger::warn("[Mirrors] interrupted accumulation pool lease: owned={} restored={}", owed, restored);
		return restored;
	}
}

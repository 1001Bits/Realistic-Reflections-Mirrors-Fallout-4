#pragma once

#include <cstdint>
#include <windows.h>

namespace MirrorNativeWorldPhase
{
	inline SRWLOCK worldLock = SRWLOCK_INIT;
	inline thread_local unsigned worldDepth{};
	inline thread_local std::uint64_t postWorldGeneration{};
	inline thread_local bool reentryActive{};

	inline void BeginWorld() noexcept
	{
		if (worldDepth++ == 0) AcquireSRWLockShared(&worldLock);
	}
	inline void EndWorld() noexcept
	{
		if (worldDepth && --worldDepth == 0) ReleaseSRWLockShared(&worldLock);
	}
	inline bool BeginPostWorld(std::uint64_t generation) noexcept
	{
		if (!generation || worldDepth || postWorldGeneration || reentryActive) return false;
		postWorldGeneration = generation;
		return true;
	}
	inline void EndPostWorld() noexcept { postWorldGeneration = 0; }
	inline bool TryEnter() noexcept
	{

		if (!postWorldGeneration || worldDepth || reentryActive || !TryAcquireSRWLockExclusive(&worldLock))
			return false;
		reentryActive = true;
		return true;
	}
	inline void Leave() noexcept
	{
		if (!reentryActive) return;
		reentryActive = false;
		ReleaseSRWLockExclusive(&worldLock);
	}
}

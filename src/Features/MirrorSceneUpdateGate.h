#pragma once

#include <cstdint>
#include <Windows.h>

class MirrorSceneUpdateGate
{
public:
	using ActorUpdate = void (*)(std::uint32_t);
	using ActorDataUpdate = void (*)(void*);
	using Capture = void (*)(const void*);

	void RunActorUpdate(ActorUpdate update, std::uint32_t index)
	{
		AcquireSRWLockShared(&_lock);
		__try {
			update(index);
		} __finally {
			ReleaseSRWLockShared(&_lock);
		}
	}

	void RunActorUpdate(ActorDataUpdate update, void* actorData)
	{
		AcquireSRWLockShared(&_lock);
		__try {
			update(actorData);
		} __finally {
			ReleaseSRWLockShared(&_lock);
		}
	}

	[[nodiscard]] bool TryCapture(Capture capture, const void* context)
	{
		if (!TryAcquireSRWLockExclusive(&_lock))
			return false;
		__try {
			capture(context);
		} __finally {
			ReleaseSRWLockExclusive(&_lock);
		}
		return true;
	}

	MirrorSceneUpdateGate() = default;
	MirrorSceneUpdateGate(const MirrorSceneUpdateGate&) = delete;
	MirrorSceneUpdateGate& operator=(const MirrorSceneUpdateGate&) = delete;

private:
	SRWLOCK _lock = SRWLOCK_INIT;
};

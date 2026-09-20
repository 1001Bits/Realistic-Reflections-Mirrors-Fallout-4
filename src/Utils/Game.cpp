#include "Game.h"

namespace Util
{
	RE::NiPoint3 GetAverageEyePosition() noexcept
	{
		auto* graphicsState = globals::game::graphicsState;
		return graphicsState ? graphicsState->cameraState.posAdjust : RE::NiPoint3{};
	}

	RE::NiPoint3 GetEyePosition(int) noexcept
	{

		return GetAverageEyePosition();
	}
}

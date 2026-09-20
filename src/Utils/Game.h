#pragma once

namespace Util
{
	[[nodiscard]] RE::NiPoint3 GetAverageEyePosition() noexcept;
	[[nodiscard]] RE::NiPoint3 GetEyePosition(int eyeIndex) noexcept;
}

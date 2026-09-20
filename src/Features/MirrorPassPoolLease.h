#pragma once

#include <cstdint>
#include <utility>

class MirrorPassPoolLease
{
public:
	using NativeBatch = void (*)(bool);

	void RunNative(NativeBatch native, bool entering, const void* passes, const void* lights)
	{
		if (entering) {
			const bool wasValid = _valid;
			_valid = false; 
			native(true);
			if (_depth == 0) {
				_passes = passes;
				_lights = lights;
			}
			_valid = wasValid && passes && lights && _passes == passes && _lights == lights;
			++_depth;
		} else {

			const bool wasValid = _valid && _depth != 0 && _passes == passes && _lights == lights;
			_valid = false;
			if (_depth != 0)
				--_depth;
			native(false);
			_valid = wasValid; 
		}
	}

	[[nodiscard]] bool Unwind(NativeBatch native, const void* passes, const void* lights,
		long passCount, long lightCount, long mtaMode)
	{
		const auto owned = std::exchange(_depth, 0u);
		if (owned == 0)
			return _valid;
		if (!_valid || !native || passes != _passes || lights != _lights ||
			mtaMode != 0 || passCount < 0 || lightCount < 0 ||
			static_cast<std::uint32_t>(passCount) < owned ||
			static_cast<std::uint32_t>(lightCount) < owned) {
			_valid = false;
			return false;
		}
		_valid = false;
		for (std::uint32_t i = 0; i < owned; ++i)
			native(false);
		_valid = true;
		return true;
	}

	[[nodiscard]] std::uint32_t Outstanding() const noexcept { return _depth; }
	[[nodiscard]] bool Valid() const noexcept { return _valid; }

private:
	const void* _passes = nullptr;
	const void* _lights = nullptr;
	std::uint32_t _depth = 0;
	bool _valid = true;
};

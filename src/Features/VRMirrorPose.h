#pragma once

#include <cmath>
#include <cstddef>
#include <span>

namespace VRMirrorPose
{
	template <class Transform>
	bool Finite(const Transform& value) noexcept
	{
		for (unsigned row = 0; row < 3; ++row)
			for (unsigned col = 0; col < 3; ++col)
				if (!std::isfinite(value.rotate.entry[row][col])) return false;
		return std::isfinite(value.translate.x) && std::isfinite(value.translate.y) &&
			std::isfinite(value.translate.z) && std::isfinite(value.scale) && value.scale >= 0.0f;
	}

	template <class Transform>
	struct LocalPair
	{
		const Transform* source{};
		Transform* target{};
	};

	template <class Transform>
	bool ValidateMapping(std::span<const LocalPair<Transform>> pairs) noexcept
	{
		for (const auto& pair : pairs) {
			if (!pair.source || !pair.target) return false;
			for (const auto& live : pairs)
				if (pair.target == live.source) return false;
		}
		return true;
	}

	template <class Transform>
	bool CopyValidatedLocals(std::span<const LocalPair<Transform>> pairs) noexcept
	{
		for (const auto& pair : pairs)
			if (!Finite(*pair.source)) return false;
		for (const auto& pair : pairs) *pair.target = *pair.source;
		return true;
	}

	template <class Transform>
	bool CopyLocals(std::span<const LocalPair<Transform>> pairs) noexcept
	{
		return ValidateMapping(pairs) && CopyValidatedLocals(pairs);
	}

	template <class Transform>
	bool LocalFromWorld(const Transform& parent, const Transform& child, Transform& local) noexcept
	{
		if (!Finite(parent) || !Finite(child) || parent.scale < 0.0001f) return false;
		Transform result{};
		for (unsigned row = 0; row < 3; ++row)
			for (unsigned col = 0; col < 3; ++col) {
				result.rotate.entry[row][col] = 0;
				for (unsigned k = 0; k < 3; ++k)
					result.rotate.entry[row][col] += child.rotate.entry[row][k] * parent.rotate.entry[col][k];
			}
		const float delta[]{ child.translate.x-parent.translate.x, child.translate.y-parent.translate.y,
			child.translate.z-parent.translate.z };
		float t[3]{};
		for (unsigned row = 0; row < 3; ++row)
			for (unsigned col = 0; col < 3; ++col)
				t[row] += parent.rotate.entry[row][col] * delta[col] / parent.scale;
		result.translate = { t[0], t[1], t[2] };
		result.scale = child.scale / parent.scale;
		if (!Finite(result)) return false;
		local = result;
		return true;
	}

	template <class Transform, class Point>
	bool BindAttachment(const Transform& parent, const Transform& child, Point& translation, float& scale) noexcept
	{
		if (!Finite(parent) || !Finite(child) || parent.scale < 0.0001f || child.scale < 0.0001f)
			return false;
		float rotation[3][3]{};
		for (unsigned row = 0; row < 3; ++row)
			for (unsigned col = 0; col < 3; ++col)
				for (unsigned k = 0; k < 3; ++k)
					rotation[row][col] += parent.rotate.entry[k][row] * child.rotate.entry[k][col];
		const float relativeScale = parent.scale / child.scale;
		const float t[3]{ child.translate.x, child.translate.y, child.translate.z };
		float result[3]{ parent.translate.x, parent.translate.y, parent.translate.z };
		for (unsigned row = 0; row < 3; ++row)
			for (unsigned col = 0; col < 3; ++col)
				result[row] -= rotation[row][col] * t[col] * relativeScale;
		if (!std::isfinite(relativeScale) || relativeScale < 0.01f || relativeScale > 100.0f) return false;
		for (float value : result)
			if (!std::isfinite(value) || std::fabs(value) > 100.0f) return false;
		translation = { result[0], result[1], result[2] };
		scale = relativeScale;
		return true;
	}
}

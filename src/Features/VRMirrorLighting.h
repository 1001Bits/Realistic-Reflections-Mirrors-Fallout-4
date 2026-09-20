#pragma once

#include "VRMirrorDeferred.h"
#include "VRMirrorScene.h"
#include "MirrorLightFade.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <limits>

namespace VRMirrorLighting
{

	inline constexpr std::size_t kLightLock = 0x210;
	inline constexpr std::size_t kSun = 0x238;
	inline constexpr std::array<std::size_t, 2> kLightArrays{ 0x198, 0x1B0 };
	inline constexpr std::size_t kMaximumSceneLights = 4096;

	template <class T> T Read(const void* base, std::size_t offset) noexcept
	{
		T result{};
		std::memcpy(&result, static_cast<const std::byte*>(base) + offset, sizeof(result));
		return result;
	}
	inline bool Pointer(const void* value) noexcept { return reinterpret_cast<std::uintptr_t>(value) > 0x10000u; }

	inline bool Ambient(const float (&matrix)[16], VRMirrorDeferred::Constants& output) noexcept
	{
		for (const auto value : matrix) if (!std::isfinite(value)) return false;
		for (std::size_t row = 0; row < 3; ++row)
			output.ambientRows[row] = { matrix[row] * matrix[15], matrix[4+row] * matrix[15],
				matrix[8+row] * matrix[15], matrix[12+row] };
		return true;
	}
	inline bool ContextAmbient(const void* context, VRMirrorDeferred::Constants& output) noexcept
	{
		if (!context) return false;
		const auto* frame = Read<const void*>(context, 0);
		if (!Pointer(frame)) return false;
		float matrix[16]{};

		std::memcpy(matrix, static_cast<const std::byte*>(frame) + 0x100, sizeof(matrix));
		return Ambient(matrix, output);
	}

	inline bool Light(const void* wrapper, bool sun, VRMirrorDeferred::Light& output) noexcept
	{
		if (!Pointer(wrapper)) return false;
		const auto* native = Read<const void*>(wrapper, 0xB8);
		if (!Pointer(native) || (!sun && Read<std::uint32_t>(wrapper, 0x180) != 2u)) return false;
		
		if (Read<std::uint8_t>(wrapper, 0x17E) || Read<std::uint8_t>(wrapper, 0x17C) ||
			(Read<std::uint32_t>(native, 0x108) & 1u)) return false;

		const float visibility = Read<float>(wrapper, 0x10);
		const bool culledByMainView = Read<std::uint32_t>(wrapper, 0x18) == MirrorLightFade::kCulledMarker;
		const float fade = Read<float>(native, 0x184) *
			(sun || !std::isfinite(visibility) ? visibility : MirrorLightFade::Resolve(native, visibility, culledByMainView));
		const auto diffuse = Read<DirectX::XMFLOAT3>(native, 0x16C);
		if (!std::isfinite(fade) || fade <= 0 || !std::isfinite(diffuse.x) ||
			!std::isfinite(diffuse.y) || !std::isfinite(diffuse.z) ||
			diffuse.x < 0 || diffuse.y < 0 || diffuse.z < 0) return false;
		output.color = { std::pow(diffuse.x, 2.2f)*fade, std::pow(diffuse.y, 2.2f)*fade,
			std::pow(diffuse.z, 2.2f)*fade, 0 };
		if (!std::isfinite(output.color.x) || !std::isfinite(output.color.y) || !std::isfinite(output.color.z))
			return false;
		if (sun) {
			const auto direction = Read<DirectX::XMFLOAT3>(native, 0x1B0);
			const float length = std::sqrt(direction.x*direction.x + direction.y*direction.y + direction.z*direction.z);
			if (!std::isfinite(length) || length < 1.e-6f) return false;
			output.positionRadius = { -direction.x/length, -direction.y/length, -direction.z/length, 0 };
		} else {
			const auto position = Read<DirectX::XMFLOAT3>(native, 0xA0);
			const float radius = Read<float>(native, 0x178);
			if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z) ||
				!std::isfinite(radius) || radius <= 0 || radius > 1.e6f) return false;
			output.positionRadius = { position.x, position.y, position.z, radius };
		}
		return true;
	}

	inline bool Snapshot(const void* scene, const VRMirrorScene::Frustum (&frusta)[2],
		const DirectX::XMFLOAT4& focus, VRMirrorDeferred::Constants& output) noexcept
	{
		output.extentAndLights.z = 0;
		output.lights = {};
		std::array<float, VRMirrorDeferred::kMaximumLights> scores{};
		std::array<const void*, VRMirrorDeferred::kMaximumLights> selected{};
		const auto* sun = Read<const void*>(scene, kSun);
		VRMirrorDeferred::Light light{};
		if (Light(sun, true, light)) {
			output.lights[0] = light;
			selected[0] = Read<const void*>(sun, 0xB8);
			scores[0] = (std::numeric_limits<float>::max)();
			output.extentAndLights.z = 1;
		}
		std::size_t visited = 0;
		for (const auto offset : kLightArrays) {
			const auto* array = Read<const void* const*>(scene, offset);
			const auto capacity = Read<std::uint32_t>(scene, offset + 8);
			const auto count = Read<std::uint32_t>(scene, offset + 16);
			if (count > capacity || count > kMaximumSceneLights || (count && !Pointer(array))) return false;
			for (std::uint32_t index = 0; index < count; ++index) {
				if (++visited > kMaximumSceneLights) return false;
				const auto* wrapper = array[index];
				if (wrapper == sun || !Light(wrapper, false, light)) continue;
				const auto* native = Read<const void*>(wrapper, 0xB8);
				if (std::find(selected.begin(), selected.end(), native) != selected.end()) continue;
				const auto& p = light.positionRadius;
				if (!frusta[0].Intersects(p.x, p.y, p.z, p.w) && !frusta[1].Intersects(p.x, p.y, p.z, p.w)) continue;
				const float dx = p.x-focus.x, dy = p.y-focus.y, dz = p.z-focus.z;
				const float luminance = .2126f*light.color.x + .7152f*light.color.y + .0722f*light.color.z;
				const float score = luminance / (1.0f + (dx*dx + dy*dy + dz*dz)/(p.w*p.w));
				if (!(score > 0) || !std::isfinite(score)) continue;
				std::size_t slot = output.extentAndLights.z;
				if (slot == output.lights.size()) {
					slot = static_cast<std::size_t>(std::min_element(scores.begin(), scores.end()) - scores.begin());
					if (score <= scores[slot]) continue;
				} else ++output.extentAndLights.z;
				output.lights[slot] = light;
				selected[slot] = native;
				scores[slot] = score;
			}
		}
		return true;
	}
}

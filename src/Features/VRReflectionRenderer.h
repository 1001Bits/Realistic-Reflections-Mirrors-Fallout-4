#pragma once

#include <cstdint>

#include <DirectXMath.h>

struct ID3D11ShaderResourceView;

namespace RE
{
}

namespace VRReflectionRenderer
{
	enum class CaptureKind : std::uint8_t
	{
		kNone,
		kMirror,
	};

	void Install() noexcept;

	void CapturePostRenderPreUI() noexcept;

	[[nodiscard]] bool PrivateCaptureActive() noexcept;
	[[nodiscard]] CaptureKind ActiveCaptureKind() noexcept;
	[[nodiscard]] bool MirrorCaptureActive() noexcept;

	void BindTargetAfterSetDirty() noexcept;

	[[nodiscard]] bool ProjectPaneForSelection(
		const DirectX::XMFLOAT3& center, const DirectX::XMFLOAT3& tangent,
		const DirectX::XMFLOAT3& bitangent, float halfWidth, float halfHeight,
		float& projectedArea) noexcept;

	[[nodiscard]] ID3D11ShaderResourceView* GetMirrorColorSRV() noexcept;
	[[nodiscard]] bool GetMirrorViewProjection(
		std::uint32_t eye,
		DirectX::XMFLOAT4X4& output) noexcept;
	[[nodiscard]] std::uint32_t MirrorPerEyeWidth() noexcept;
	[[nodiscard]] std::uint32_t MirrorHeight() noexcept;

	void Release() noexcept;
}

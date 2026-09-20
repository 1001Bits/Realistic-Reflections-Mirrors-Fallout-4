#pragma once

#include <cstdint>

#include <DirectXMath.h>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11ShaderResourceView;
struct ID3D11Texture2D;

namespace PlanarMirrorLookup
{

	enum class EyePixelDumpStage : std::uint32_t
	{
		kMaterialAfterGroup4 = 0,
		kResolvedBeforeGroup8 = 1,
		kAfterGroup8BeforeBatch8 = 2,
		kFinalAfterGroup8 = 3
	};

	enum class MirrorLayerDumpStage : std::uint32_t
	{
		kEnvironmentBeforeNativePlayer = 0,
		kNativePlayerColor = 1,
		kNativePlayerDepth = 2,
		kCombinedAfterNativePlayer = 3
	};

	bool CapturePresentedPaneDump(
		ID3D11Device* device,
		ID3D11DeviceContext* context,
		ID3D11Texture2D* texture) noexcept;

	bool CaptureMirrorLayerDump(
		ID3D11Device* device,
		ID3D11DeviceContext* context,
		ID3D11Texture2D* texture,
		MirrorLayerDumpStage stage) noexcept;

	bool BeginEyePixelDumpCapture(std::uint64_t currentExactProofSerial) noexcept;

	bool EyePixelDumpCaptureActive() noexcept;

	bool EyePixelDumpNeedsFreshExactProof(
		std::uint64_t currentExactProofSerial, bool currentProofMatchesCandidate) noexcept;

	bool ConsumeEyePixelDumpOcclusionQueryBudget(std::uint64_t nowMs) noexcept;

	bool BeginQualifiedEyePixelDumpTransaction(std::uint64_t exactProofSerial) noexcept;

	void CancelQualifiedEyePixelDumpTransaction() noexcept;

	bool CaptureEyePixelDumpStage(
		ID3D11Device* device,
		ID3D11DeviceContext* context,
		ID3D11Texture2D* texture,
		EyePixelDumpStage stage) noexcept;

	bool SaveTextureDiagnosticPNG(
		ID3D11Device* device,
		ID3D11DeviceContext* context,
		ID3D11Texture2D* texture,
		const char* label,
		const wchar_t* outputPath) noexcept;

	void ReleaseResources() noexcept;

	bool ApplyReflectedRasterizer(
		ID3D11Device* device,
		ID3D11DeviceContext* context) noexcept;

	bool RestoreOrdinaryRasterizer(
		ID3D11Device* device,
		ID3D11DeviceContext* context) noexcept;
}

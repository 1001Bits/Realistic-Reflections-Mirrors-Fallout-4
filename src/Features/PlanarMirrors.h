#pragma once
#include "MirrorNativeLighting.h"

#include <array>
#include <cstdint>

#include <d3d11.h>
#include <wrl/client.h>

#include "PlanarMirrorMath.h"
#include "MirrorShadowMaps.h"
#include "MirrorMaterialLayout.h"

namespace RE
{
	class NiCamera;

	namespace BSGraphics
	{
		struct CameraStateData;
	}
}

namespace PlanarMirrors
{
	struct PatchedCameraState
	{
		DirectX::XMFLOAT4X4 view{};
		DirectX::XMFLOAT4X4 viewProjection{};
		DirectX::XMFLOAT4X4 unclippedViewProjection{};
		DirectX::XMFLOAT3 cameraOrigin{};
		bool obliqueApplied{};
	};

	bool SupportsNativeCameraReflection() noexcept;

	bool ReflectCamera(
		const RE::NiCamera* source,
		const PlanarMirrorMath::Plane& plane,
		RE::NiCamera* destination) noexcept;

	struct CaptureCropView
	{
		const RE::NiCamera* camera{ nullptr };
		DirectX::XMFLOAT3 originOffset{};
	};

	bool FitCaptureToPane(
		RE::NiCamera* camera,
		const DirectX::XMFLOAT3& center,
		const DirectX::XMFLOAT3& tangent,
		const DirectX::XMFLOAT3& bitangent,
		float halfWidth, float halfHeight,
		const CaptureCropView* cropView = nullptr) noexcept;

	bool GetUnclippedCameraView(const RE::NiCamera* camera,
		DirectX::XMFLOAT4X4& viewProjection, DirectX::XMFLOAT3& origin) noexcept;

	bool BeginCameraOverride(
		const RE::NiCamera* reflectedCamera,
		const PlanarMirrorMath::Plane& plane,
		float clipBias = 1.5f,
		bool applyObliqueClip = true) noexcept;

	void EndCameraOverride(const RE::NiCamera* reflectedCamera = nullptr) noexcept;

	bool CameraOverrideActive(const RE::NiCamera* camera = nullptr) noexcept;

	bool PatchCameraStateData(
		const RE::NiCamera* camera,
		RE::BSGraphics::CameraStateData& stateData) noexcept;

	bool PatchVRCameraStateData(
		const RE::NiCamera* camera,
		void* stateData) noexcept;

	bool GetPatchedViewProjection(
		const RE::NiCamera* reflectedCamera,
		DirectX::XMFLOAT4X4& output,
		bool* obliqueApplied = nullptr) noexcept;

	bool GetPatchedViewProjectionForEye(
		const RE::NiCamera* reflectedCamera,
		std::uint32_t eye,
		DirectX::XMFLOAT4X4& output,
		bool* obliqueApplied = nullptr) noexcept;

	bool GetPatchedCameraStateForEye(
		const RE::NiCamera* reflectedCamera,
		std::uint32_t eye,
		PatchedCameraState& output) noexcept;

	class CameraOverrideScope
	{
	public:
		CameraOverrideScope(
			const RE::NiCamera* reflectedCamera,
			const PlanarMirrorMath::Plane& plane,
			float clipBias = 1.5f) noexcept;
		~CameraOverrideScope();

		CameraOverrideScope(const CameraOverrideScope&) = delete;
		CameraOverrideScope& operator=(const CameraOverrideScope&) = delete;

		[[nodiscard]] bool Active() const noexcept { return active; }

	private:
		const RE::NiCamera* camera{ nullptr };
		bool active{ false };
	};

	enum class MaterialCoverageProofStatus : std::uint8_t
	{
		kUnavailable = 0,
		kPending,
		kReadyZero,
		kReadyNonZero,
		kFault
	};

	struct MaterialCoverageProofResult
	{
		MaterialCoverageProofStatus status{ MaterialCoverageProofStatus::kUnavailable };
		std::uint64_t serial{ 0 };
		std::uint64_t samples{ 0 };
		HRESULT result{ S_FALSE };
	};

	class RenderTarget
	{
	public:

		struct WritableColorBundle
		{
			Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
			Microsoft::WRL::ComPtr<ID3D11RenderTargetView> rtv;
			Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
			Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> uav;

			[[nodiscard]] bool Complete() const noexcept
			{
				return texture && rtv && srv && uav;
			}
		};

		RenderTarget() = default;
		~RenderTarget();

		RenderTarget(const RenderTarget&) = delete;
		RenderTarget& operator=(const RenderTarget&) = delete;

		bool Create(
			ID3D11Device* device,
			std::uint32_t perEyeWidth,
			std::uint32_t height,
			DXGI_FORMAT colorFormat = DXGI_FORMAT_R16G16B16A16_FLOAT,
			bool createMaterialCaptureResources = false,
			bool stereoSideBySide = false,
			bool createMaterialProperties = false,
			bool createNativeLitOverlayResources = false,
			bool createDurableMaterialEnvironment = false,
			bool createSplitOverlayDepth = true,
			bool createMaterialHistory = false,
			bool nativeMaterialProperties = false) noexcept;
		void Release(ID3D11DeviceContext* context = nullptr) noexcept;
		
		bool SwapUnbound(RenderTarget& other) noexcept;
		bool PrewarmOptionalResources() noexcept { return EnsureTileLightBuffer(); }
		
		void BeginMaterialHistory(std::uint64_t tick) noexcept;
		void CommitMaterialHistory(ID3D11DeviceContext* context) noexcept;
		void InvalidateMaterialHistory() noexcept;
		[[nodiscard]] std::uint64_t MaterialHistoryTick() const noexcept { return historyCommittedTick; }

		bool Begin(ID3D11DeviceContext* context, const float clearColor[4]) noexcept;
		bool BeginMaterialCapture(ID3D11DeviceContext* context, const float clearColor[4]) noexcept;

		bool BeginMaterialOverlayCapture(
			ID3D11DeviceContext* context,
			const float clearColor[4]) noexcept;
		void BindMaterialGBuffer(ID3D11DeviceContext* context) noexcept;

		bool ResolveMaterialCapture(
			ID3D11DeviceContext* context,
			ID3D11ComputeShader* resolveShader,
			ID3D11Buffer* lightingConstants = nullptr,
			ID3D11ShaderResourceView* backgroundCube = nullptr,
			ID3D11SamplerState* backgroundSampler = nullptr,
			ID3D11Buffer* backgroundConstants = nullptr,
			ID3D11ShaderResourceView* previousMirrorColor = nullptr,
			const MirrorShadowMaps::Bindings* shadows = nullptr,
			ID3D11ComputeShader* tileCullShader = nullptr,
			MirrorNativeLighting::Bindings nativeLighting = {}) noexcept;
		
		bool ResolveMaterialOverlayCapture(
			ID3D11DeviceContext* context,
			ID3D11ComputeShader* resolveShader,
			std::uint64_t* depthQualifiedSamples = nullptr) noexcept;

		bool BeginMaterialOverlayCoverageProof(ID3D11DeviceContext* context) noexcept;
		bool BeginRetainedMaterialCoverageProof(
			ID3D11DeviceContext* context,
			std::uint64_t& serial) noexcept;
		bool EndMaterialOverlayCoverageProof(ID3D11DeviceContext* context) noexcept;
		[[nodiscard]] MaterialCoverageProofResult PollMaterialCoverageProof(
			ID3D11DeviceContext* context) noexcept;
		bool ReadMaterialCoverageProof(
			ID3D11DeviceContext* context,
			std::uint64_t& samples) noexcept;
		
		bool ResolveMaterialCaptureCandidate(
			ID3D11DeviceContext* context,
			ID3D11ComputeShader* resolveShader,
			ID3D11Buffer* lightingConstants = nullptr,
			ID3D11ShaderResourceView* backgroundCube = nullptr,
			ID3D11SamplerState* backgroundSampler = nullptr,
			ID3D11Buffer* backgroundConstants = nullptr,
			ID3D11ShaderResourceView* previousMirrorColor = nullptr,
			const MirrorShadowMaps::Bindings* shadows = nullptr,
			ID3D11ComputeShader* tileCullShader = nullptr,
			MirrorNativeLighting::Bindings nativeLighting = {}) noexcept;
		
		bool CommitRetainedMaterialEnvironment(
			ID3D11DeviceContext* context,
			std::uint64_t serial) noexcept;
		
		bool ConsumeRetainedMaterialCoverageProof(std::uint64_t serial) noexcept;
		
		bool DiscardRetainedMaterialCoverageProof(std::uint64_t serial) noexcept;
		
		bool CreateCompatibleWritableColorBundle(
			ID3D11Device* device,
			WritableColorBundle& bundle) const noexcept;
		
		[[nodiscard]] bool WritableColorBundleCompatible(
			const WritableColorBundle& bundle) const noexcept;

		void ExchangeWritableColorBundle(WritableColorBundle& bundle) noexcept;

		bool ResolveMaterialOverlayCaptureWithProperties(
			ID3D11DeviceContext* context,
			ID3D11ComputeShader* resolveShader,
			ID3D11Buffer* lightingConstants = nullptr) noexcept;

		bool ResolveNativeLitPlayerOverlay(
			ID3D11DeviceContext* context,
			ID3D11ComputeShader* resolveShader,
			ID3D11ShaderResourceView* playerColor,
			ID3D11ShaderResourceView* playerDepth) noexcept;
		bool BindResolvedColor(ID3D11DeviceContext* context) noexcept;

		bool RestoreDurableMaterialEnvironment(ID3D11DeviceContext* context) noexcept;
		void Rebind(ID3D11DeviceContext* context) noexcept;
		void End(ID3D11DeviceContext* context) noexcept;

		[[nodiscard]] bool Ready() const noexcept { return colorRTV && depthDSV && colorSRV; }
		[[nodiscard]] bool MaterialCaptureReady() const noexcept
		{
			return materialDiffuseRTV && materialDiffuseSRV && materialNormalRTV && materialNormalSRV && depthSRV &&
				colorUAV && (!materialPropertiesEnabled || (materialPropertiesRTV && materialPropertiesSRV));
		}
		[[nodiscard]] bool MaterialOverlayCaptureReady() const noexcept
		{
			return MaterialCaptureReady() && overlayDepthDSV && overlayDepthSRV &&
				materialOverlayCoverageQuery && colorTypedUAVLoadSupported;
		}
		[[nodiscard]] bool MaterialPropertiesOverlayCaptureReady() const noexcept
		{
			return MaterialCaptureReady() && materialPropertiesEnabled && materialPropertiesRTV &&
				materialPropertiesSRV && overlayDepthDSV && overlayDepthSRV &&
				(!durableMaterialEnvironmentEnabled ||
					(environmentColorTexture && environmentColorSRV && environmentDepthTexture));
		}
		[[nodiscard]] bool DurableMaterialEnvironmentAllocated() const noexcept
		{
			return durableMaterialEnvironmentEnabled &&
				environmentColorTexture && environmentColorSRV && environmentDepthTexture;
		}
		[[nodiscard]] bool DurableMaterialEnvironmentReady() const noexcept
		{
			return environmentSnapshotValid && DurableMaterialEnvironmentAllocated();
		}
		[[nodiscard]] bool NativeLitOverlayReady() const noexcept
		{
			return Ready() && colorUAV && depthSRV && colorTypedUAVLoadSupported;
		}
		[[nodiscard]] bool Bound() const noexcept { return bound; }
		[[nodiscard]] bool UsesDevice(ID3D11Device* device) const noexcept;
		
		[[nodiscard]] std::uint32_t Width() const noexcept { return width; }
		[[nodiscard]] std::uint32_t Height() const noexcept { return height; }
		[[nodiscard]] std::uint32_t EyeCount() const noexcept { return eyeCount; }
		[[nodiscard]] std::uint32_t PerEyeWidth() const noexcept { return perEyeWidth; }
		[[nodiscard]] bool StereoSideBySide() const noexcept { return eyeCount == 2; }
		[[nodiscard]] ID3D11Texture2D* ColorTexture() const noexcept { return colorTexture.Get(); }
		[[nodiscard]] ID3D11RenderTargetView* ColorRTV() const noexcept { return colorRTV.Get(); }
		[[nodiscard]] ID3D11ShaderResourceView* ColorSRV() const noexcept { return colorSRV.Get(); }
		[[nodiscard]] ID3D11UnorderedAccessView* ColorUAV() const noexcept { return colorUAV.Get(); }
		[[nodiscard]] ID3D11RenderTargetView* MaterialPropertiesRTV() const noexcept
		{
			return materialPropertiesRTV.Get();
		}
		[[nodiscard]] ID3D11RenderTargetView* MaterialDiffuseRTV() const noexcept
		{
			return materialDiffuseRTV.Get();
		}
		[[nodiscard]] ID3D11RenderTargetView* MaterialNormalRTV() const noexcept
		{
			return materialNormalRTV.Get();
		}
		[[nodiscard]] ID3D11DepthStencilView* DepthDSV() const noexcept
		{
			return depthDSV.Get();
		}
		[[nodiscard]] ID3D11DepthStencilView* OverlayDepthDSV() const noexcept
		{
			return overlayDepthDSV.Get();
		}
		[[nodiscard]] bool MaterialPropertiesEnabled() const noexcept
		{
			return materialPropertiesEnabled;
		}

		[[nodiscard]] UINT MaterialPropertiesSlot() const noexcept { return nativeMaterialProperties ? 3u : 2u; }
		[[nodiscard]] MirrorMaterialLayout::TargetArray MaterialTargets() const noexcept
		{
			return MirrorMaterialLayout::Targets(MaterialDiffuseRTV(), MaterialNormalRTV(),
				materialPropertiesEnabled ? MaterialPropertiesRTV() : nullptr,
				materialEmissiveEnabled ? MaterialEmissiveRTV() : nullptr, nativeMaterialProperties);
		}
		[[nodiscard]] ID3D11RenderTargetView* MaterialTarget(UINT slot) const noexcept
		{
			const auto targets = MaterialTargets();
			return slot < targets.size() ? targets[slot] : nullptr;
		}
		[[nodiscard]] UINT MaterialBoundTargetCount() const noexcept
		{
			return 2u + unsigned(materialPropertiesEnabled) + unsigned(materialEmissiveEnabled);
		}
		
		[[nodiscard]] bool MaterialEmissiveEnabled() const noexcept { return materialEmissiveEnabled; }
		[[nodiscard]] ID3D11RenderTargetView* MaterialEmissiveRTV() const noexcept { return materialEmissiveRTV.Get(); }
		[[nodiscard]] ID3D11ShaderResourceView* MaterialEmissiveSRV() const noexcept { return materialEmissiveSRV.Get(); }
		
		[[nodiscard]] UINT MaterialRenderTargetCount() const noexcept
		{
			return MirrorMaterialLayout::Count(MaterialTargets());
		}
		[[nodiscard]] ID3D11Texture2D* MaterialDiffuseTexture() const noexcept
		{
			return materialDiffuseTexture.Get();
		}
		[[nodiscard]] ID3D11Texture2D* MaterialNormalTexture() const noexcept { return materialNormalTexture.Get(); }
		[[nodiscard]] ID3D11Texture2D* MaterialPropertiesTexture() const noexcept { return materialPropertiesTexture.Get(); }
		[[nodiscard]] ID3D11Texture2D* MaterialMetadataTexture() const noexcept { return historyCandidate.Get(); }
		[[nodiscard]] ID3D11ShaderResourceView* MaterialDiffuseSRV() const noexcept
		{
			return materialDiffuseSRV.Get();
		}
		[[nodiscard]] ID3D11ShaderResourceView* MaterialNormalSRV() const noexcept { return materialNormalSRV.Get(); }
		[[nodiscard]] ID3D11ShaderResourceView* MaterialPropertiesSRV() const noexcept { return materialPropertiesSRV.Get(); }
		[[nodiscard]] ID3D11Texture2D* DepthTexture() const noexcept { return depthTexture.Get(); }
		[[nodiscard]] ID3D11ShaderResourceView* DepthSRV() const noexcept { return depthSRV.Get(); }

	private:
		RenderTarget(RenderTarget&&) noexcept = default;
		RenderTarget& operator=(RenderTarget&&) noexcept = default;
		enum class BindPhase : std::uint8_t
		{
			kFinalColor,
			kMaterialDepth,
			kMaterialGBuffer,
			kMaterialResolved,
			kMaterialOverlayDepth,
			kMaterialOverlayGBuffer,
			kMaterialOverlayResolved,
			kMaterialOverlayForward
		};

		bool BeginInternal(
			ID3D11DeviceContext* context,
			const float clearColor[4],
			bool materialCapture,
			bool preserveForwardColor = false) noexcept;
		bool BeginMaterialCoverageProofInternal(
			ID3D11DeviceContext* context,
			bool retainTarget,
			std::uint64_t* serial) noexcept;
		void ClearMaterialCoverageProofResult() noexcept;
		void ResetSavedState() noexcept;

		Microsoft::WRL::ComPtr<ID3D11Texture2D> colorTexture;
		Microsoft::WRL::ComPtr<ID3D11Texture2D> historyCandidate, historyCommitted;
		Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> historyCandidateUAV, historyCommittedUAV;
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> historyCandidateSRV, historyCommittedSRV;
		std::uint64_t historyCandidateTick{}, historyCommittedTick{};
		bool historyCandidateResolved{};
		Microsoft::WRL::ComPtr<ID3D11RenderTargetView> colorRTV;
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> colorSRV;
		Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> colorUAV;
		Microsoft::WRL::ComPtr<ID3D11Texture2D> materialDiffuseTexture;
		Microsoft::WRL::ComPtr<ID3D11RenderTargetView> materialDiffuseRTV;
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> materialDiffuseSRV;
		Microsoft::WRL::ComPtr<ID3D11Texture2D> materialNormalTexture;
		Microsoft::WRL::ComPtr<ID3D11RenderTargetView> materialNormalRTV;
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> materialNormalSRV;
		Microsoft::WRL::ComPtr<ID3D11Texture2D> materialPropertiesTexture;
		Microsoft::WRL::ComPtr<ID3D11RenderTargetView> materialPropertiesRTV;
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> materialPropertiesSRV;
		Microsoft::WRL::ComPtr<ID3D11Texture2D> materialEmissiveTexture;
		Microsoft::WRL::ComPtr<ID3D11RenderTargetView> materialEmissiveRTV;
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> materialEmissiveSRV;
		Microsoft::WRL::ComPtr<ID3D11Texture2D> depthTexture;
		Microsoft::WRL::ComPtr<ID3D11DepthStencilView> depthDSV;
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> depthSRV;

		static constexpr std::uint32_t kTileLightSize = 32, kTileLightCapacity = 8;
		Microsoft::WRL::ComPtr<ID3D11Buffer> tileLightBuffer;
		Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> tileLightUAV;
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> tileLightSRV;
		std::uint32_t tileLightWidth = 0, tileLightHeight = 0;
		[[nodiscard]] bool EnsureTileLightBuffer() noexcept;
		Microsoft::WRL::ComPtr<ID3D11Texture2D> overlayDepthTexture;
		Microsoft::WRL::ComPtr<ID3D11DepthStencilView> overlayDepthDSV;
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> overlayDepthSRV;

		Microsoft::WRL::ComPtr<ID3D11Texture2D> environmentColorTexture;
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> environmentColorSRV;
		Microsoft::WRL::ComPtr<ID3D11Texture2D> environmentDepthTexture;
		Microsoft::WRL::ComPtr<ID3D11Query> materialOverlayCoverageQuery;

		std::array<Microsoft::WRL::ComPtr<ID3D11RenderTargetView>, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> savedRTVs;
		Microsoft::WRL::ComPtr<ID3D11DepthStencilView> savedDSV;
		std::array<D3D11_VIEWPORT, D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> savedViewports{};
		std::uint32_t savedViewportCount{ 0 };
		std::array<D3D11_RECT, D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> savedScissorRects{};
		std::uint32_t savedScissorRectCount{ 0 };
		std::uint32_t width{ 0 };
		std::uint32_t height{ 0 };
		std::uint32_t eyeCount{ 0 };
		std::uint32_t perEyeWidth{ 0 };

		bool colorTypedUAVLoadSupported{ false };
		bool materialPropertiesEnabled{ false };
		bool nativeMaterialProperties{ false };
		bool materialEmissiveEnabled{ false };
		bool durableMaterialEnvironmentEnabled{ false };
		bool environmentSnapshotValid{ false };
		bool materialOverlayCoverageQueryActive{ false };
		bool materialOverlayCoverageQueryPending{ false };
		bool materialCoverageProofRetainsTarget{ false };
		bool materialCoverageProofResultReady{ false };
		bool materialCaptureCandidateResolved{ false };
		std::uint64_t materialCoverageProofNextSerial{ 0 };
		std::uint64_t materialCoverageProofSerial{ 0 };
		std::uint64_t materialCoverageProofSamples{ 0 };
		HRESULT materialCoverageProofResult{ S_FALSE };
		BindPhase bindPhase{ BindPhase::kFinalColor };
		bool bound{ false };
	};
}

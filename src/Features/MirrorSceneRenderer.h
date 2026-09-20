#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>

struct ID3D11DeviceContext;

namespace RE
{
	class NiAVObject;
	class BSRenderPass;
}

namespace MirrorSceneRenderer
{
	
	void ObserveNativeAmbientCommit() noexcept;

	enum class MirrorVisibilityProof : std::uint8_t
	{
		kNone = 0,
		kUnsafeReflectedCameraUmbra = 1,
		kReflectedCameraFrustumCull = 2
	};

	namespace ZPrepassFilterDetail
	{

		inline constexpr std::size_t kMaximumRecordBytes = 0x48;

		struct RemovedRecord
		{
			std::uint32_t index{ 0 };
			std::array<std::byte, kMaximumRecordBytes> bytes{};
		};

		inline bool StableEraseRecord(
			std::byte* records,
			std::uint32_t& activeCount,
			std::uint32_t capacity,
			std::size_t stride,
			std::uint32_t index,
			RemovedRecord& removed) noexcept
		{
			if (!records || stride == 0 || stride > kMaximumRecordBytes ||
				activeCount > capacity || index >= activeCount)
				return false;

			removed = {};
			removed.index = index;
			std::memcpy(removed.bytes.data(), records + static_cast<std::size_t>(index) * stride, stride);
			const std::uint32_t trailingCount = activeCount - index - 1u;
			if (trailingCount != 0) {
				std::memmove(
					records + static_cast<std::size_t>(index) * stride,
					records + static_cast<std::size_t>(index + 1u) * stride,
					static_cast<std::size_t>(trailingCount) * stride);
			}
			--activeCount;
			return true;
		}

		inline bool StableRestoreRecord(
			std::byte* records,
			std::uint32_t& activeCount,
			std::uint32_t capacity,
			std::size_t stride,
			const RemovedRecord& removed) noexcept
		{
			if (!records || stride == 0 || stride > kMaximumRecordBytes ||
				activeCount >= capacity || removed.index > activeCount)
				return false;

			const std::uint32_t trailingCount = activeCount - removed.index;
			if (trailingCount != 0) {
				std::memmove(
					records + static_cast<std::size_t>(removed.index + 1u) * stride,
					records + static_cast<std::size_t>(removed.index) * stride,
					static_cast<std::size_t>(trailingCount) * stride);
			}
			std::memcpy(
				records + static_cast<std::size_t>(removed.index) * stride,
				removed.bytes.data(),
				stride);
			++activeCount;
			return true;
		}
	}

	enum class LoadSource : std::uint32_t
	{
		kSaveGame = 1u,
		kLoadingMenu = 2u,
		kMainMenu = 4u
	};

	enum class InteractiveMenuSource : std::uint32_t
	{
		kConsole = 1u,
		kPauseMenu = 2u,
		kCursorMenu = 4u,

		kPipboyMenu = 8u
	};

	void Install() noexcept;

	bool& Enabled() noexcept;

	void NotifyLoadSource(LoadSource source, bool loading) noexcept;

	void NotifyInteractiveMenu(InteractiveMenuSource source, bool opening) noexcept;
	bool InteractiveMenuOpen() noexcept;
	
	void SuppressPrivateGoreCapsForSubmission(RE::NiAVObject* root) noexcept;
	void RestorePrivateGoreCaps() noexcept;

	void NotifyShutdown() noexcept;
	bool LoadBlocked() noexcept;
	bool ShutdownRequested() noexcept;
	std::uint32_t LoadGeneration() noexcept;
	
	[[nodiscard]] bool ServiceVRReflectionLoadGate() noexcept;

	void ResetVRReflectionState() noexcept;

	float& ReflectExposure() noexcept;

	bool& ReflectPlayerBody() noexcept;

	bool& LivePlayerPoseFallback() noexcept;

	bool& LateBlendedDecals() noexcept;

	bool& EyeGroup4NoPixelRef() noexcept;
	bool EyeGroup4NoPixelArmed() noexcept;
	bool ApplyEyeGroup4NoPixelPS(ID3D11DeviceContext* context) noexcept;
	void BeginEyeGroup4NoPixelEffectDraw(RE::BSRenderPass* pass) noexcept;
	void EndEyeGroup4NoPixelEffectDraw(RE::BSRenderPass* pass) noexcept;
	void RestoreEyeGroup4NoPixelPSBeforeSetDirty(ID3D11DeviceContext* context) noexcept;

	void BeginEyeGroup4AlbedoLightingDraw(RE::BSRenderPass* pass) noexcept;
	void EndEyeGroup4AlbedoLightingDraw(RE::BSRenderPass* pass) noexcept;
	bool BeginEyeGroup4AlbedoFinalDraw(
		RE::BSRenderPass* pass,
		ID3D11DeviceContext* context) noexcept;
	void EndEyeGroup4AlbedoFinalDraw(
		RE::BSRenderPass* pass,
		ID3D11DeviceContext* context,
		bool acceptanceKnown,
		bool accepted) noexcept;
	void RestoreEyeGroup4AlbedoPSBeforeSetDirty(ID3D11DeviceContext* context) noexcept;
	bool PrepareEyeGroup4AlbedoPSOverride(ID3D11DeviceContext* context) noexcept;
	void RecordEyeGroup4AlbedoPSOverrideApplied() noexcept;

	bool BeginMirrorEyeGroup8DrawOverride(ID3D11DeviceContext* context) noexcept;
	bool EndMirrorEyeGroup8DrawOverride(
		ID3D11DeviceContext* context, bool drawCompleted) noexcept;
	bool MirrorEyeGroup8WindingOverrideActive() noexcept;

	bool& PoseBridgeDisabled() noexcept;
	bool& PoseBridgeIncludeHead() noexcept;

	struct PlanarMirrorRoute
	{
		bool sliceReserved{ false };     
		bool classifierValid{ false };   
		float center[3]{};               
		float normal[3]{};
		float tangent[3]{};
		float bitangent[3]{};
		float halfThickness{ 0.0f };
		float halfWidth{ 0.0f };
		float halfHeight{ 0.0f };
		float selfDepthMargin{ 0.0f };
		float ellipseMask{ 0.0f };
		std::uint32_t firstVertex{ 0 };
		std::uint32_t vertexCount{ 6 };
		std::uint64_t authoredContour{ 0 };
	};

	struct VRMirrorCaptureRequest
	{
		std::uint64_t loadGeneration{ 0 };
		PlanarMirrorRoute route{};
		float plane[4]{};  
		RE::NiAVObject* exclusionRoot{ nullptr };
		std::uint32_t receiverFormID{ 0 };
		std::uint32_t cellFormID{ 0 };
	};

	inline constexpr std::uint32_t kVRMirrorSlotCount = 1u;
	bool PrepareVRMirrorCapture(VRMirrorCaptureRequest& request, std::uint32_t slot = 0u) noexcept;

	[[nodiscard]] bool MirrorOnlyProducerMode() noexcept;
	bool& MirrorReflections() noexcept;

	std::uint32_t CycleMirrorDiagnosticMode() noexcept;
	std::uint32_t MirrorDiagnosticMode() noexcept;
	const char* MirrorDiagnosticModeName(std::uint32_t mode) noexcept;

	bool MirrorCaptureActive() noexcept;

	struct MirrorGalleryCleanupReport
	{
		std::uint32_t deleteRequested{ 0 };
		std::uint32_t alreadyDeleted{ 0 };
		std::uint32_t unresolved{ 0 };
		std::uint32_t rejectedIdentity{ 0 };

		std::uint32_t deletePending{ 0 };

		[[nodiscard]] std::uint32_t Retained() const noexcept
		{
			return unresolved + rejectedIdentity + deletePending;
		}

		[[nodiscard]] bool Complete() const noexcept { return Retained() == 0; }
	};

	void RequestCycleMirrorGallery(int direction) noexcept;
	void RequestRemoveMirrorGallery() noexcept;
	bool MirrorGalleryActiveOrPending() noexcept;
	bool MirrorGalleryTransitionPending() noexcept;
	std::uint32_t MirrorGalleryDisplayedDefinitionIndex() noexcept;
	const char* MirrorGalleryDefinitionName(std::uint32_t definitionIndex) noexcept;
	MirrorGalleryCleanupReport CleanupMirrorGalleryForLifecycle() noexcept;

	std::uint32_t ResetMirrorGalleryTrackingForWorldChange() noexcept;

	void NoteD3DDrawSeamWrapper(bool present) noexcept;

	void InstallDeferredPlayerNaturalPoseSlot() noexcept;

	void ObserveRegisteredMirrorReference(RE::BSRenderPass* pass) noexcept;
	void ObserveMirrorGeometry(RE::BSRenderPass* pass) noexcept;
	bool PlanarMirrorReadyForRouting() noexcept;
	bool GetPlanarMirrorRoute(PlanarMirrorRoute& route) noexcept;
	
	void ReconcileEngineShadowBeforeCommit() noexcept;
	
	void ReconcileEngineShadowAfterDrive() noexcept;
	void BindPlanarTarget() noexcept;  
	
	void RecordTripwireDrawState(ID3D11DeviceContext* context) noexcept;
	
	void RecordTripwireDraw(ID3D11DeviceContext* context, std::uint32_t indexCount) noexcept;
	void RecordTripwireDrawPost(ID3D11DeviceContext* context, std::uint32_t indexCount) noexcept;
	
	bool DrawHookPassthrough() noexcept;
	
	bool DrawRebindSkipped() noexcept;
	
	extern std::atomic<std::uint32_t> g_tripwireRebindCalls;
	extern std::atomic<std::uint32_t> g_tripwireRebindEyeEarlyReturns;
	extern std::atomic<std::uint32_t> g_tripwireRebindTargetRebinds;
	extern std::atomic<std::uint32_t> g_tripwireRebindRasterizerChanges;
	extern std::atomic<std::uint32_t> g_tripwireRebindDepthStencilChanges;
	extern thread_local bool g_tripwireBindFromDrawSeam;

	bool& OcclusionCull() noexcept;

	bool& CaptureRemainingGeometryGroupsRef() noexcept;

	bool& DrawStrandedFacePassesRef() noexcept;

	bool& SkinTintRewriteEnabledRef() noexcept;

	bool EyeAlbedoOverrideArmed() noexcept;

	int ClassifyCurrentArmedDrawMaterial() noexcept;
	bool PrepareFaceSkinTintPSOverride(ID3D11DeviceContext* context) noexcept;
	void RestoreFaceSkinTintBeforeSetDirty(ID3D11DeviceContext* context) noexcept;

	bool BeginFaceSkinTintDrawOverride(ID3D11DeviceContext* context) noexcept;
	bool EndFaceSkinTintDrawOverride(
		ID3D11DeviceContext* context, bool drawCompleted) noexcept;
	bool FaceSkinTintDrawOverrideActive() noexcept;

	bool MirrorRenderActive() noexcept;   

	void NoteDFPrepassSetupGeometry() noexcept;
	std::uint64_t DFPrepassSetupGeometryCalls() noexcept;
	bool PrivateRenderActive() noexcept;  
}

bool RegisterMirrorMenuSink() noexcept;

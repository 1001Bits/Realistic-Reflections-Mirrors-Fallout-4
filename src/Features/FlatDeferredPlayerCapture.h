#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <d3d11.h>
#include <wrl/client.h>

#include "RE/NetImmerse/NiAVObject.h"
#include "RE/NetImmerse/NiCamera.h"
#include "RE/NetImmerse/NiSmartPointer.h"

namespace RE::Interface3D
{
	class Renderer;
}

namespace RE
{
	class BSRenderPass;
	class BSShader;
}

namespace FlatDeferredPlayerCapture
{

	enum class Status : std::uint32_t
	{
		kSuccess = 0,
		kUnsupportedRuntime,
		kContractMismatch,
		kInvalidRequest,
		kWrongThread,
		kBusy,
		kAliasedMainAccumulator,
		kAliasedMainLightingContext,
		kInvalidPrivateRenderer,
		kMissingRenderContext,
		kUnsupportedPipelineState,
		kInvalidLogicalTarget,
		kIncompatibleTargetExtent,
		
		kResourcePoolWarming,
		kResourceBackupFailed,
		kNativeException,
		kNativeDidNotComplete,
		
		kIncompleteForwardGeometryAuthority,
		kNoDetachedModelCoverage,
		
		kDegenerateNativeColor,
		kProjectionAttestationFailed,
		
		kAuthorityPending,
		kResultCopyFailed,
		
		kOwnerEntryAttestationFailed,
		
		kLightingPreparationFailed,
		
		kOwnerPostconditionFailed,
		kCleanupFailed,
		kRestoreFailed,
		kDeviceRemoved
	};

	enum class CoverageRule : std::uint32_t
	{
		kNone = 0,
		
		kForwardZDepthLessThanOne
	};

	enum class DepthEncoding : std::uint32_t
	{
		kNone = 0,
		
		kFlatForwardDeviceDepth
	};

	enum class CameraHandedness : std::uint32_t
	{
		kUnspecified = 0,
		kOrdinary,
		kReflected
	};

	enum class VisibilityStatus : std::uint32_t
	{
		
		kNotReadBack = 0,
		kDetachedModelDepthObserved
	};

	struct FrozenRange
	{
		std::uint32_t beginRVA{};
		std::uint32_t endRVAInclusive{};
		std::array<std::uint8_t, 32> sha256{};
	};

	struct FrozenContract
	{
		std::array<std::uint16_t, 4> runtimeVersion{};
		std::array<std::uint8_t, 32> executableSha256{};

		FrozenRange renderSceneDeferred{};
		FrozenRange interface3DDrawModel{};
		FrozenRange dfLightSetupGeometry{};
		
		FrozenRange interface3DRenderMain{};
		
		FrozenRange interface3DDrawModelForward{};
		
		FrozenRange interface3DFlattenedEntryLambda{};
		
		FrozenRange interface3DUpdateLights{};
		FrozenRange processQueuedLights{};
		FrozenRange setSunLight{};
		FrozenRange cullingProcessConstructor{};
		FrozenRange cullingProcessSetAccumulator{};
		FrozenRange cullingProcessDestructor{};
		
		FrozenRange clearGroupPasses{};
		FrozenRange clearActivePasses{};
		FrozenRange niAVObjectUpdate{};
		
		FrozenRange niNodeAttachChild{};
		
		FrozenRange niNodeDetachChild{};
		FrozenRange acquireDepthStencil{};
		
		FrozenRange acquireDepthStencilTarget{};
		FrozenRange releaseDepthStencil{};
		
		FrozenRange setCurrentDepthStencil{};

		std::uint32_t outputColorLogicalRT{};
		std::uint32_t outputDepthLogicalDS{};
		std::array<std::uint32_t, 4> dfLightInputs{};
		std::array<std::uint32_t, 6> dfPrepassMRTs{};
		
		std::array<std::uint32_t, 8> logicalColorWriteSet{};
		
		std::array<std::uint32_t, 2> conditionalLogicalColorWriteSet{};
	};

	[[nodiscard]] const FrozenContract& Contract() noexcept;

	struct CameraCacheWarmupResult
	{
		Status status{ Status::kInvalidRequest };
		std::uint64_t identity{};
		std::uintptr_t captureCamera{};
		std::uintptr_t restoreCamera{};
		std::uintptr_t entryShaderCamera{};
		std::uintptr_t entryStateReferenceCamera{};
		std::uintptr_t afterShaderCamera{};
		std::uintptr_t afterStateReferenceCamera{};
		std::uintptr_t verifiedShaderCamera{};
		std::uintptr_t verifiedStateReferenceCamera{};
		std::uintptr_t cacheData{};
		std::uint32_t cacheCapacity{};
		std::uint32_t cacheSize{};
		std::uint32_t verifiedCacheCapacity{};
		std::uint32_t verifiedCacheSize{};
		std::uint32_t renderThreadId{};
		bool runtimeAttested{};
		
		bool installedHookIdentityAttested{};
		
		bool executableHashAttested{};
		
		bool bodyHashesAttested{};
		bool restoreWasCurrentOnEntry{};
		bool captureEntryReady{};
		bool restoreEntryReady{};
		bool restoreRepublished{};
		bool metadataStableAfterRepublish{};

		[[nodiscard]] explicit operator bool() const noexcept
		{
			return status == Status::kSuccess && identity != 0 && captureCamera != 0 &&
			       restoreCamera != 0 && cacheData != 0 && cacheSize != 0 && renderThreadId != 0 &&
			       runtimeAttested && installedHookIdentityAttested &&
			       executableHashAttested && bodyHashesAttested &&
			       restoreWasCurrentOnEntry && captureEntryReady && restoreEntryReady &&
			       restoreRepublished && metadataStableAfterRepublish;
		}
	};

	[[nodiscard]] RE::NiCamera* CurrentShaderCamera(std::uint32_t renderThreadId) noexcept;

	[[nodiscard]] CameraCacheWarmupResult PrewarmCameraCache(
		RE::NiCamera* captureCamera, RE::NiCamera* restoreCamera,
		std::uint32_t renderThreadId) noexcept;

	enum class NativeWorldOwnerDisposition : std::uint32_t
	{
		kEntryUnchanged = 0,
		kReusable,
		kQuarantine
	};

	enum class NativeWorldOwnerAttestationPhase : std::uint32_t
	{
		
		kSealedPreparedEntry = 0,
		
		kRestoredPostCleanup
	};

	struct NativeWorldOwnerAttestationView
	{
		NativeWorldOwnerAttestationPhase phase{
			NativeWorldOwnerAttestationPhase::kSealedPreparedEntry
		};
		void* exclusiveAccumulator{};
		RE::NiCamera* captureCamera{};
		RE::NiAVObject* privateShadowSceneNode{};
		RE::NiAVObject* const* submittedRoots{};
		std::uint32_t submittedRootCount{};
		std::uint64_t ownerIdentitySerial{};
		std::uint64_t preparedAccumulatorSerial{};
		std::uint64_t frameSerial{};
		bool nativeMutationBegan{};
		bool nativeCallReturned{};
		bool lightingPreparationReturned{};
		bool privateSunDetached{};
		bool accumulatorActivePassesCleared{};
		bool sortedMode18CurrentRecordRepaired{};
		bool nativeWorldRasterizerPolicyAttested{};
		bool contextConstantGroupsRestored{};
		bool declaredResourcesRestored{};
		bool engineStateRestored{};
		bool pipelineStateRestored{};
	};

	using NativeWorldOwnerAttestor = bool (*)(
		void* opaqueContext, const NativeWorldOwnerAttestationView& view,
		std::uint32_t* failureCode) noexcept;

	struct NativeWorldLightingPreparationView
	{
		void* exclusiveAccumulator{};
		RE::NiCamera* captureCamera{};
		RE::NiAVObject* privateShadowSceneNode{};
		void* cullingProcess{};
		std::uint64_t ownerIdentitySerial{};
		std::uint64_t preparedAccumulatorSerial{};
		std::uint64_t frameSerial{};
	};

	using NativeWorldLightingPreparer = bool (*)(
		void* opaqueContext, const NativeWorldLightingPreparationView& view,
		std::uint32_t* failureCode) noexcept;

	struct NativeWorldPassAccumulationView
	{
		void* exclusiveAccumulator{};
		RE::NiCamera* captureCamera{};
		RE::NiAVObject* privateShadowSceneNode{};
		void* cullingProcess{};
		std::uint64_t ownerIdentitySerial{};
		std::uint64_t preparedAccumulatorSerial{};
		std::uint64_t frameSerial{};
	};

	using NativeWorldPassAccumulator = bool (*)(
		void* opaqueContext, const NativeWorldPassAccumulationView& view,
		std::uint32_t* failureCode) noexcept;

	struct NativeWorldResourcePrewarmRequest
	{
		ID3D11Device* device{};
		ID3D11DeviceContext* immediateContext{};
		ID3D11Buffer* cameraConstantBuffer{};
		std::uint32_t renderThreadId{};
		std::uint64_t frameSerial{};
		std::uint32_t requiredWidth{};
		std::uint32_t requiredHeight{};
	};

	struct NativeWorldResourcePrewarmResult
	{
		Status status{ Status::kInvalidRequest };
		std::uint64_t resourceGenerationIdentity{};
		std::uint32_t colorWidth{};
		std::uint32_t colorHeight{};
		std::uint32_t depthWidth{};
		std::uint32_t depthHeight{};
		std::uint32_t resourcesCreated{};
		
		std::uint32_t dynamicBufferBackupCount{};
		
		std::uint32_t contextConstantGroupCount{};
		bool runtimeAttested{};
		
		bool installedHookIdentityAttested{};
		
		bool executableHashAttested{};
		
		bool bodyHashesAttested{};
		bool exactRequiredExtentSatisfied{};
		bool declaredLogicalWriteSetPrepared{};
		bool nonBlockingRollbackPrepared{};
		bool contextConstantGroupRollbackPrepared{};
		bool rollbackContentsStaged{};
		bool engineOrPipelineMutationPerformed{};

		[[nodiscard]] explicit operator bool() const noexcept
		{
			return status == Status::kSuccess && resourceGenerationIdentity != 0 &&
				colorWidth != 0 && colorHeight != 0 && depthWidth != 0 && depthHeight != 0;
		}
	};

	[[nodiscard]] NativeWorldResourcePrewarmResult PrewarmNativeWorldResources(
		const NativeWorldResourcePrewarmRequest& request) noexcept;

	struct NativeWorldRequest
	{
		
		static constexpr std::size_t kMaxSubmittedRoots = 8192;
		
		RE::Interface3D::Renderer* privateRenderer{};
		void* exclusiveAccumulator{};
		RE::NiPointer<RE::NiCamera> captureCamera{};
		RE::NiPointer<RE::NiCamera> restoreCamera{};
		
		RE::NiPointer<RE::NiAVObject> privateShadowSceneNode{};

		RE::NiPointer<RE::NiAVObject> modelRoot{};

		ID3D11Device* device{};
		ID3D11DeviceContext* immediateContext{};
		ID3D11Buffer* cameraConstantBuffer{};
		std::uintptr_t expectedCameraStateReference{};
		std::uint32_t renderThreadId{};

		std::uint64_t ownerIdentitySerial{};
		std::uint64_t preparedAccumulatorSerial{};
		std::uint64_t frameSerial{};
		
		std::uint64_t resourceGenerationIdentity{};

		std::uint32_t requiredWidth{};
		std::uint32_t requiredHeight{};
		
		CameraHandedness cameraHandedness{ CameraHandedness::kUnspecified };

		RE::NiAVObject* const* submittedRoots{};
		std::uint32_t submittedRootCount{};

		RE::NiAVObject* privateMutableRoot{};

		NativeWorldOwnerAttestor ownerAttestor{};
		void* ownerAttestorContext{};
		NativeWorldLightingPreparer lightingPreparer{};
		void* lightingPreparerContext{};
		NativeWorldPassAccumulator passAccumulator{};
		void* passAccumulatorContext{};
	};

	struct NativeWorldCompletionProof
	{
		Status status{ Status::kInvalidRequest };
		NativeWorldOwnerDisposition ownerDisposition{
			NativeWorldOwnerDisposition::kEntryUnchanged
		};
		std::uint32_t sehExceptionCode{};
		std::uintptr_t nativeExceptionInstruction{};
		std::uintptr_t nativeExceptionReturnAddress{};
		std::uint64_t completionIdentity{};
		std::uint64_t pendingIdentity{};
		std::uint64_t frameSerial{};
		std::uint64_t ownerIdentitySerial{};
		std::uint64_t preparedAccumulatorSerial{};
		std::uint64_t resourceGenerationIdentity{};

		std::uintptr_t accumulator{};
		std::uintptr_t privateRenderer{};
		std::uintptr_t captureCamera{};
		std::uintptr_t restoreCamera{};
		std::uintptr_t shadowSceneNode{};
		std::uintptr_t modelRoot{};
		std::uintptr_t cameraConstantBuffer{};
		std::uintptr_t sourceColorResource{};
		std::uintptr_t sourceDepthResource{};
		std::uintptr_t resultColorResource{};
		std::uintptr_t resultDepthResource{};

		std::array<std::uint32_t, 10> backedLogicalColorTargets{};
		std::uint32_t backedLogicalColorTargetCount{};
		std::uint32_t outputLogicalColorTarget{};
		std::uint32_t outputLogicalDepthTarget{};
		std::uint32_t colorPhysicalTarget{};
		std::uint32_t depthPhysicalTarget{};
		std::uint32_t colorWidth{};
		std::uint32_t colorHeight{};
		std::uint32_t depthWidth{};
		std::uint32_t depthHeight{};
		std::uint32_t requiredWidth{};
		std::uint32_t requiredHeight{};
		DXGI_FORMAT colorResourceFormat{ DXGI_FORMAT_UNKNOWN };
		DXGI_FORMAT colorViewFormat{ DXGI_FORMAT_UNKNOWN };
		DXGI_FORMAT depthResourceFormat{ DXGI_FORMAT_UNKNOWN };
		DXGI_FORMAT depthViewFormat{ DXGI_FORMAT_UNKNOWN };
		D3D11_SRV_DIMENSION colorViewDimension{ D3D11_SRV_DIMENSION_UNKNOWN };
		D3D11_SRV_DIMENSION depthViewDimension{ D3D11_SRV_DIMENSION_UNKNOWN };
		std::uint32_t sampleCount{};

		std::uint64_t coveredPixelCount{};
		std::uint64_t mappedDepthPixelCount{};

		std::uint64_t informativeColorPixelCount{};
		
		std::uint64_t spatiallyVariantColorPixelCount{};
		std::uint64_t requiredInformativeColorPixelCount{};
		std::uint64_t requiredSpatiallyVariantColorPixelCount{};

		std::uint32_t resourcePrepareStage{};
		std::uint32_t resourcePrepareDetail{};
		std::uint32_t resultCopyStage{};
		std::uint32_t mappingMismatchKind{};
		std::uint32_t mappingMismatchLogical{};
		std::uint32_t mappingMismatchExpected{};
		std::uint32_t mappingMismatchObserved{};
		std::uint32_t ownerEntryFailureCode{};
		std::uint32_t lightingPreparationFailureCode{};
		std::uint32_t passAccumulationFailureCode{};
		std::uint32_t ownerPostconditionFailureCode{};
		std::uint32_t hotPathResourceCreations{};
		std::uint32_t dynamicBufferBackupCount{};
		std::uint32_t dynamicAsyncRestoreCopyCount{};
		std::uint32_t contextConstantGroupCount{};
		
		std::uint32_t nativeWorldSetDirtyIssuedCount{};
		std::uint32_t nativeWorldSetDirtyCommitCount{};
		
		std::uint32_t nativeWorldGeometryRequiredCommitCount{};
		
		std::uint32_t nativeWorldOrdinaryRequiredCommitCount{};
		
		std::uint32_t nativeWorldForeignCommitCount{};
		std::uint32_t nativeWorldPlanarTargetIsolationCount{};
		std::uint32_t nativeWorldPlanarTargetIsolationFailures{};
		std::uint32_t nativeWorldRasterizerApplyAttempts{};
		std::uint32_t nativeWorldRasterizerApplySuccesses{};
		std::uint32_t nativeWorldRasterizerApplyFailures{};
		std::uint32_t nativeWorldOrdinaryRasterizerApplyAttempts{};
		std::uint32_t nativeWorldOrdinaryRasterizerApplySuccesses{};
		std::uint32_t nativeWorldOrdinaryRasterizerApplyFailures{};
		std::uint32_t submittedRootCount{};
		std::uint32_t drawModelExpandedRootCount{};
		std::uint32_t submittedRootsCleared{};
		std::uintptr_t depth3ReservationHandle{};
		std::int32_t depth3ReservationPhysical{ -1 };
		std::int32_t depth3ReservationRefCount{ -1 };
		std::int32_t depth3ReservationLogicalOwner{ -1 };
		std::uint32_t resultSlot{};
		std::uint32_t gpuAuthorityDispatchCount{};
		std::uint32_t authorityPollCount{};
		std::uint32_t blockingReadbackCount{};
		std::uint32_t authorityReadbackBytes{};

		bool runtimeAttested{};
		
		bool installedHookIdentityAttested{};
		
		bool executableHashAttested{};
		
		bool bodyHashesAttested{};
		bool abiCallsiteAttested{};
		bool captureCameraCachePreexisting{};
		bool restoreCameraCachePreexisting{};
		bool accumulatorWasExclusive{};
		bool preparedAccumulatorTupleAttested{};
		bool activeShadowSceneNodeAliasesRejected{};
		bool ownerEntryAttestorInvoked{};
		bool ownerEntryAttestorReturned{};
		bool ownerEntryAttested{};
		bool lightingPreparerInvoked{};
		bool lightingPreparerReturned{};
		
		bool callerPreparedLightingAttested{};
		bool passAccumulatorInvoked{};
		bool passAccumulatorReturned{};
		
		bool passAccumulatorAttested{};
		bool ownerPostconditionAttestorInvoked{};
		bool ownerPostconditionAttestorReturned{};
		bool ownerPostconditionAttested{};
		bool mrt4TargetRequired{};
		bool secondLightTargetRequired{};
		bool declaredLogicalWriteSetBacked{};
		
		bool nonBlockingRollbackAttested{};
		
		bool contextConstantGroupsRestored{};
		bool resourcePoolWarmOnEntry{};
		bool resourceGenerationMatched{};
		
		bool doubleBufferedResultLifetime{};
		bool sourceExtentReported{};
		bool exactRequiredExtentSatisfied{};
		bool implicitResamplePerformed{};
		bool prepassDepthHookInstalled{};
		bool drawModelAccumulateHookInstalled{};
		bool drawModelAccumulateHookArmed{};
		bool drawModelAccumulateHookEntered{};
		bool drawModelRootExpansionAttested{};
		bool prepassDepthCaptureArmed{};
		bool prepassDepthCaptured{};
		bool prepassDepthCaptureTLSRestored{};
		CameraHandedness cameraHandedness{ CameraHandedness::kUnspecified };
		bool nativeWorldRasterizerTLSArmed{};
		bool nativeWorldRasterizerTLSRestored{};
		bool nativeWorldCommitIdentitiesAttested{};
		bool nativeWorldPlanarTargetsIsolated{};
		bool nativeWorldReflectedRasterizerAttested{};
		bool nativeWorldOrdinaryRasterizerAttested{};
		bool cullerConstructed{};
		bool cullerAccumulatorSet{};
		bool cullerCameraSet{};
		
		bool drawModelCullerAuthorityRevoked{};
		bool canonicalPrefixBound{};
		bool defaultViewportSelected{};
		bool rendererFlushed{};
		bool nativeMutationBegan{};
		bool nativeCallReturned{};
		bool nativeCompleted{};
		bool captureCameraDetached{};
		bool modelRootDetached{};
		bool depth3ReservationAttested{};
		bool depth3ReservationRestored{};
		bool privateSunDetached{};
		bool accumulatorGroup5Cleared{};
		bool submittedRootPassesCleared{};
		bool accumulatorActivePassesCleared{};
		bool sortedMode18CurrentRecordValidated{};
		bool sortedMode18CurrentRecordRepaired{};
		bool colorCopied{};
		bool depthCopied{};
		bool resultResourcesDisjoint{};
		bool depthCoverageReducedOnGPU{};
		
		bool colorAuthorityReducedOnGPU{};
		
		bool nonClearColorWriteObserved{};

		bool nondegenerateDepthQualifiedColor{};
		bool logicalMappingsStable{};
		bool declaredResourcesRestored{};
		bool engineRawStateVerified{};
		bool accumulatorGlobalRestored{};
		bool shadowSceneNodeGlobalRestored{};
		bool graphicsCameraCacheStable{};
		bool hbaoDerivedStateRestored{};
		bool cameraConstantsRestored{};
		bool pipelineStateRestored{};
		
		bool pendingRetained{};
		
		bool pendingTerminal{};
		
		bool publicationPerformed{};
	};

	struct NativeWorldResult
	{
		Status status{ Status::kInvalidRequest };
		Microsoft::WRL::ComPtr<ID3D11Texture2D> color{};
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> colorSRV{};
		Microsoft::WRL::ComPtr<ID3D11Texture2D> depth{};
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> depthSRV{};
		NativeWorldCompletionProof proof{};

		[[nodiscard]] explicit operator bool() const noexcept
		{
			return status == Status::kSuccess && color && colorSRV && depth && depthSRV &&
				proof.completionIdentity != 0 && !proof.pendingRetained && proof.pendingTerminal;
		}

		[[nodiscard]] bool IsPending() const noexcept
		{
			return status == Status::kAuthorityPending && proof.pendingIdentity != 0 &&
				proof.pendingRetained && !proof.pendingTerminal && !color && !colorSRV &&
				!depth && !depthSRV;
		}
	};

	[[nodiscard]] NativeWorldResult CaptureNativeWorld(
		const NativeWorldRequest& request) noexcept;

	[[nodiscard]] NativeWorldResult PollNativeWorldCapture(
		std::uint64_t pendingIdentity, std::uint32_t renderThreadId) noexcept;

	enum class NativeWorldPendingDiscardReason : std::uint32_t
	{
		kDeviceRemoved = 0,
		kRenderThreadGenerationRetired,
		kLoadGenerationRetired
	};

	[[nodiscard]] bool DiscardPendingNativeWorldCapture(
		std::uint64_t pendingIdentity, ID3D11Device* expectedDevice,
		ID3D11DeviceContext* expectedContext, std::uint32_t retiredRenderThreadId,
		NativeWorldPendingDiscardReason reason) noexcept;

	struct Request
	{
		static constexpr std::size_t kMaxPrivatePointLights = 8;
		static constexpr std::size_t kMaxRequiredEyeGeometries = 16;
		struct PrivatePointLightValues
		{
			std::array<float, 3> translation{};
			std::array<float, 3> diffuse{};
			float radius{};
			float dimmer{};
		};
		struct PrivateDirectionalLightValues
		{
			
			std::array<float, 12> localRotation{};

			std::array<float, 10> lightPOD{};
		};
		static_assert(sizeof(PrivateDirectionalLightValues) == 0x58);

		RE::NiPointer<RE::NiAVObject> playerRoot{};

		std::array<RE::NiAVObject*, kMaxRequiredEyeGeometries> requiredEyeGeometries{};
		std::uint32_t requiredEyeGeometryCount{};

		RE::NiPointer<RE::NiCamera> captureCamera{};
		
		RE::NiPointer<RE::NiCamera> restoreCamera{};

		RE::Interface3D::Renderer* privateRenderer{};
		
		void* exclusiveAccumulator{};

		RE::NiPointer<RE::NiAVObject> privateShadowSceneNode{};
		
		RE::NiPointer<RE::NiAVObject> privateDirectionalLight{};
		
		PrivateDirectionalLightValues privateDirectionalLightValues{};
		
		std::array<RE::NiPointer<RE::NiAVObject>, kMaxPrivatePointLights> privatePointLights{};
		
		std::array<PrivatePointLightValues, kMaxPrivatePointLights> privatePointLightValues{};
		std::uint32_t privatePointLightCount{};

		ID3D11Device* device{};
		ID3D11DeviceContext* immediateContext{};

		ID3D11Buffer* cameraConstantBuffer{};
		
		std::uintptr_t expectedCameraStateReference{};

		std::uint32_t renderThreadId{};

		std::uint64_t externalPlayerStateGeneration{};
		
		std::uint64_t privateRendererIdentitySerial{};
		std::uint64_t frameSerial{};

		std::uint32_t expectedWidth{};
		std::uint32_t expectedHeight{};

		bool compositeFlag{ false };
		
		CameraHandedness cameraHandedness{ CameraHandedness::kUnspecified };
	};

	struct CompletionProof
	{
		Status status{ Status::kInvalidRequest };
		std::uint32_t sehExceptionCode{};
		
		std::uintptr_t nativeExceptionInstruction{};
		std::uintptr_t nativeExceptionReturnAddress{};
		std::uintptr_t nativeExceptionObject{};
		std::uintptr_t nativeExceptionParent{};
		std::uintptr_t nativeExceptionVtable{};
		std::uint64_t completionIdentity{};
		std::uint64_t frameSerial{};
		
		std::uint64_t externalPlayerStateGeneration{};
		std::uint64_t privateRendererIdentitySerial{};

		std::uintptr_t playerRoot{};
		std::uintptr_t captureCamera{};
		std::uintptr_t restoreCamera{};
		std::uintptr_t privateRenderer{};
		std::uintptr_t accumulator{};
		std::uintptr_t shadowSceneNode{};
		std::uintptr_t cameraConstantBuffer{};
		std::uintptr_t sourceColorResource{};
		std::uintptr_t sourceDepthResource{};

		std::uint32_t colorPhysicalTarget{};
		std::uint32_t depthPhysicalTarget{};
		std::uint32_t width{};
		std::uint32_t height{};
		DXGI_FORMAT colorResourceFormat{ DXGI_FORMAT_UNKNOWN };
		DXGI_FORMAT colorViewFormat{ DXGI_FORMAT_UNKNOWN };
		DXGI_FORMAT depthResourceFormat{ DXGI_FORMAT_UNKNOWN };
		DXGI_FORMAT depthViewFormat{ DXGI_FORMAT_UNKNOWN };
		D3D11_SRV_DIMENSION colorViewDimension{ D3D11_SRV_DIMENSION_UNKNOWN };
		D3D11_SRV_DIMENSION depthViewDimension{ D3D11_SRV_DIMENSION_UNKNOWN };
		std::uint32_t sampleCount{};
		std::uint32_t privateLightCount{};
		std::uint64_t coveredPixelCount{};
		
		std::uint64_t mappedDepthPixelCount{};

		std::uint64_t informativeColorPixelCount{};
		
		std::uint64_t spatiallyVariantColorPixelCount{};
		std::uint64_t requiredInformativeColorPixelCount{};
		std::uint64_t requiredSpatiallyVariantColorPixelCount{};
		std::uint64_t forwardOcclusionSamples{};
		std::uint64_t forwardVSInvocations{};
		std::uint64_t forwardPSInvocations{};
		std::uint64_t flattenedGeometryIdentity{};
		
		std::uint32_t flattenedGeometryCount{};
		
		std::uint32_t flattenedDrawableCount{};
		std::uint32_t requiredEyeGeometryCount{};
		std::uint32_t authoritativeEyeGeometryCount{};
		std::uint32_t forwardExpectedGeometryCount{};
		std::uint32_t forwardObservedGeometryCount{};
		
		std::uint32_t forwardOrdinaryGeometryCount{};
		
		std::uint32_t forwardSpecialAlphaGeometryCount{};
		
		std::uint32_t forwardNoPassGeometryCount{};
		
		std::uint32_t forwardRoutedEyeGeometryCount{};
		
		std::uint32_t forwardSelectorResultCalls{};
		std::uint32_t forwardGeometrySetupCalls{};
		std::uint32_t forwardGeometryRestoreCalls{};
		std::uint32_t forwardForeignGeometryCallbacks{};
		std::uint32_t forwardNullGeometryCallbacks{};
		std::uint32_t forwardUnbalancedGeometryCallbacks{};
		std::uint32_t forwardForeignOutputCallbacks{};
		std::uint32_t reflectedRasterizerApplyAttempts{};
		std::uint32_t reflectedRasterizerApplySuccesses{};
		std::uint32_t reflectedRasterizerApplyFailures{};
		std::uint32_t ordinaryRasterizerPolicyCommits{};
		std::uint32_t unattributedRasterizerPolicyCommits{};
		std::uintptr_t depth3ReservationHandle{};
		std::int32_t depth3ReservationPhysical{ -1 };
		std::int32_t depth3ReservationRefCount{ -1 };
		std::int32_t depth3ReservationLogicalOwner{ -1 };
		std::uint32_t hotPathResourceCreations{};
		std::uint32_t coverageReadbackBytes{};
		
		std::uint32_t colorAuthorityReadbackBytes{};
		
		std::uint32_t resourcePrepareStage{};
		
		std::uint32_t resourcePrepareDetail{};
		
		std::uint32_t resultCopyStage{};
		
		std::uint32_t mappingMismatchKind{};
		std::uint32_t mappingMismatchLogical{};
		std::uint32_t mappingMismatchExpected{};
		std::uint32_t mappingMismatchObserved{};
		CoverageRule coverageRule{ CoverageRule::kNone };
		DepthEncoding depthEncoding{ DepthEncoding::kNone };
		CameraHandedness cameraHandedness{ CameraHandedness::kUnspecified };
		float clearDepth{};
		D3D11_COMPARISON_FUNC nativeOpaqueDepthComparison{ D3D11_COMPARISON_NEVER };
		VisibilityStatus visibilityStatus{ VisibilityStatus::kNotReadBack };
		std::uint64_t projectionIdentity{};
		std::uintptr_t cameraStateReference{};
		std::array<float, 16> captureViewProjection{};
		std::array<float, 16> captureViewProjectionUnjittered{};
		float viewportTopLeftX{};
		float viewportTopLeftY{};
		float viewportWidth{};
		float viewportHeight{};
		float viewportMinDepth{};
		float viewportMaxDepth{};
		std::uint32_t viewportCount{};
		std::uint32_t scissorCount{};

		bool runtimeAttested{};
		bool executableHashAttested{};
		bool bodyHashesAttested{};
		bool abiCallsiteAttested{};
		bool dfLightMappingAttested{};
		bool forwardSelectorAttested{};
		bool depth3ReservationAttested{};
		bool depth3ReservationRestored{};
		bool flattenedModelCensusAttested{};
		bool flattenedModelCensusStable{};
		
		bool eyeGeometryAuthorityAttested{};
		
		bool forwardSpecializedBranchAttested{};
		bool forwardGeometryPassesAttested{};
		bool logicalResourcesStable{};
		bool logicalWriteSetAttested{};
		
		bool resourcePoolWarmOnEntry{};
		
		bool doubleBufferedResultPublication{};
		bool coverageReducedOnGPU{};
		
		bool colorAuthorityReducedOnGPU{};
		
		bool nondegenerateColorAuthority{};
		bool forwardDrawQueryReadBack{};
		
		bool reflectedRasterizerTransactionArmed{};
		bool reflectedRasterizerTransactionRestored{};
		bool reflectedRasterizerCommitAttested{};
		bool finalForwardDepthCaptured{};
		bool fullResolutionDepthMapped{};
		bool strongRootPinHeld{};
		bool captureCameraDetachedOnEntry{};
		bool captureCameraDetachedAfterReturn{};
		bool playerRootDetachedOnEntry{};
		bool playerRootDetachedAfterReturn{};
		
		bool lightingPreparationAttested{};
		bool captureCameraBoundForLighting{};
		bool captureCameraAttachedForLighting{};
		bool playerRootAttachedForLighting{};
		bool privateLightsUpdated{};
		bool queuedLightsProcessed{};
		bool privateShadowSceneNodeUpdated{};
		bool privateSunAttached{};
		bool privateSunDetached{};
		
		bool soleModelArgumentAttested{};
		
		bool detachedSoleModelAccumulated{};
		bool accumulatorWasExclusive{};
		bool privateLightingContextAttested{};
		bool privateRendererDisabledOnEntry{};
		bool privateRendererIdleOnEntry{};
		
		bool privateLightingAuthorityAttested{};
		
		bool privateSceneGraphAttested{};
		bool privateLightPinsHeld{};
		
		bool privateRendererPostconditionAttested{};

		std::uint32_t privateRendererEntryFailureCode{};

		std::uint32_t privateRendererPostconditionFailureCode{};
		
		bool privateRendererReusable{};
		
		bool privateRendererQuarantineRequired{};
		bool drawModelCompleted{};
		bool nativeCompleted{};
		
		bool forwardSuffixCompleted{};
		bool colorCopied{};
		bool depthCopied{};
		
		bool cullerDestroyed{};
		bool renderPassesCleared{};
		bool sortedMode18RecordRepaired{};
		bool engineTargetsRestored{};
		bool engineStateRestored{};
		
		bool engineRawStateVerified{};
		bool accumulatorGlobalRestored{};
		bool shadowSceneNodeGlobalRestored{};
		bool pipelineStateRestored{};
		
		bool entryBoundConstantBufferContentsRestored{};
		bool dynamicBufferRoundTripsRestored{};
		bool contextConstantBuffersRestored{};
		
		bool captureCameraCachePreexisting{};
		bool restoreCameraCachePreexisting{};
		bool graphicsCameraCacheStable{};
		bool hbaoDerivedStateRestored{};
		bool cameraConstantsRestored{};
		bool captureProjectionAttested{};
		bool fullOriginViewportAttested{};
		bool noRestrictiveScissorAttested{};

		bool directPlanarDepthComparable{};
		
		bool sourceExtentReported{};
		
		bool colorAlphaIsCoverage{};
	};

	struct Result
	{
		Status status{ Status::kInvalidRequest };
		Microsoft::WRL::ComPtr<ID3D11Texture2D> color{};
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> colorSRV{};
		Microsoft::WRL::ComPtr<ID3D11Texture2D> depth{};
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> depthSRV{};
		CompletionProof proof{};

		[[nodiscard]] explicit operator bool() const noexcept
		{
			return status == Status::kSuccess && color && colorSRV && depth && depthSRV &&
			       proof.completionIdentity != 0 && proof.runtimeAttested &&
			       proof.executableHashAttested && proof.bodyHashesAttested &&
			       proof.abiCallsiteAttested && proof.dfLightMappingAttested &&
			       proof.forwardSelectorAttested && proof.flattenedModelCensusAttested &&
			       proof.depth3ReservationAttested && proof.depth3ReservationRestored &&
			       proof.flattenedModelCensusStable && proof.flattenedGeometryIdentity != 0 &&
			       proof.flattenedGeometryCount != 0 && proof.flattenedDrawableCount != 0 &&
			       proof.flattenedDrawableCount <= proof.flattenedGeometryCount &&
			       proof.eyeGeometryAuthorityAttested && proof.requiredEyeGeometryCount != 0 &&
			       proof.authoritativeEyeGeometryCount == proof.requiredEyeGeometryCount &&
			       proof.forwardRoutedEyeGeometryCount != 0 &&
			       proof.forwardRoutedEyeGeometryCount <= proof.requiredEyeGeometryCount &&
			       proof.forwardSpecializedBranchAttested &&
			       proof.forwardGeometryPassesAttested &&
			       proof.forwardExpectedGeometryCount == proof.flattenedDrawableCount &&
			       proof.forwardObservedGeometryCount == proof.forwardExpectedGeometryCount &&
			       proof.forwardOrdinaryGeometryCount + proof.forwardSpecialAlphaGeometryCount +
			               proof.forwardNoPassGeometryCount ==
			           proof.forwardExpectedGeometryCount &&
			       proof.forwardSelectorResultCalls == proof.forwardExpectedGeometryCount &&
			       proof.forwardGeometrySetupCalls >= proof.forwardOrdinaryGeometryCount &&
			       proof.forwardGeometrySetupCalls == proof.forwardGeometryRestoreCalls &&
			       proof.forwardForeignGeometryCallbacks == 0 &&
			       proof.forwardNullGeometryCallbacks == 0 &&
			       proof.forwardUnbalancedGeometryCallbacks == 0 &&
			       proof.forwardForeignOutputCallbacks == 0 &&
			       proof.logicalResourcesStable && proof.logicalWriteSetAttested &&
			       proof.resourcePoolWarmOnEntry && proof.hotPathResourceCreations == 0 &&
			       proof.doubleBufferedResultPublication && proof.coverageReducedOnGPU &&
			       proof.coverageReadbackBytes == sizeof(std::uint32_t) &&
			       proof.colorAuthorityReducedOnGPU && proof.nondegenerateColorAuthority &&
			       proof.colorAuthorityReadbackBytes == 3 * sizeof(std::uint32_t) &&
			       proof.mappedDepthPixelCount != 0 &&
			       proof.informativeColorPixelCount >= proof.requiredInformativeColorPixelCount &&
			       proof.spatiallyVariantColorPixelCount >=
			           proof.requiredSpatiallyVariantColorPixelCount &&
			       proof.requiredInformativeColorPixelCount != 0 &&
			       proof.requiredSpatiallyVariantColorPixelCount != 0 &&
			       proof.forwardDrawQueryReadBack && proof.forwardVSInvocations != 0 &&
			       proof.forwardPSInvocations != 0 && proof.forwardOcclusionSamples != 0 &&
			       proof.mappedDepthPixelCount <= proof.forwardOcclusionSamples &&
			       proof.reflectedRasterizerTransactionArmed &&
			       proof.reflectedRasterizerTransactionRestored &&
			       proof.reflectedRasterizerCommitAttested &&
			       proof.unattributedRasterizerPolicyCommits == 0 &&
			       ((proof.cameraHandedness == CameraHandedness::kReflected &&
			            proof.reflectedRasterizerApplyAttempts >= proof.forwardGeometrySetupCalls &&
			            proof.reflectedRasterizerApplySuccesses ==
			                proof.reflectedRasterizerApplyAttempts &&
			            proof.reflectedRasterizerApplyFailures == 0 &&
			            proof.ordinaryRasterizerPolicyCommits == 0) ||
			           (proof.cameraHandedness == CameraHandedness::kOrdinary &&
			            proof.reflectedRasterizerApplyAttempts == 0 &&
			            proof.reflectedRasterizerApplySuccesses == 0 &&
			            proof.reflectedRasterizerApplyFailures == 0 &&
			            proof.ordinaryRasterizerPolicyCommits >=
			                proof.forwardGeometrySetupCalls)) &&
			       proof.finalForwardDepthCaptured &&
			       !proof.fullResolutionDepthMapped &&
			       proof.strongRootPinHeld && proof.captureCameraDetachedOnEntry &&
			       proof.captureCameraDetachedAfterReturn && proof.playerRootDetachedOnEntry &&
			       proof.playerRootDetachedAfterReturn && proof.lightingPreparationAttested &&
			       proof.captureCameraBoundForLighting && proof.captureCameraAttachedForLighting &&
			       proof.playerRootAttachedForLighting && proof.privateLightsUpdated &&
			       proof.queuedLightsProcessed && proof.privateShadowSceneNodeUpdated &&
			       proof.privateSunAttached && proof.privateSunDetached &&
			       proof.soleModelArgumentAttested &&
			       proof.detachedSoleModelAccumulated &&
			       proof.accumulatorWasExclusive && proof.privateLightingContextAttested &&
			       proof.privateLightingAuthorityAttested && proof.privateSceneGraphAttested &&
			       proof.privateLightPinsHeld && proof.privateRendererPostconditionAttested &&
			       proof.privateRendererReusable &&
			       proof.drawModelCompleted && proof.nativeCompleted && proof.forwardSuffixCompleted &&
			       proof.visibilityStatus == VisibilityStatus::kDetachedModelDepthObserved &&
			       proof.coveredPixelCount != 0 && proof.colorCopied && proof.depthCopied &&
			       proof.cullerDestroyed && proof.renderPassesCleared &&
			       proof.sortedMode18RecordRepaired && proof.engineTargetsRestored &&
			       proof.engineStateRestored && proof.pipelineStateRestored &&
			       proof.entryBoundConstantBufferContentsRestored &&
			       proof.dynamicBufferRoundTripsRestored && proof.contextConstantBuffersRestored &&
			       proof.captureCameraCachePreexisting && proof.restoreCameraCachePreexisting &&
			       proof.graphicsCameraCacheStable && proof.hbaoDerivedStateRestored &&
			       proof.cameraConstantsRestored && proof.captureProjectionAttested &&
			       proof.fullOriginViewportAttested && proof.noRestrictiveScissorAttested &&
			       proof.sourceExtentReported && !proof.directPlanarDepthComparable &&
			       !proof.colorAlphaIsCoverage;
		}
	};

	[[nodiscard]] Result Capture(const Request& request) noexcept;

	void InstallPrepassDepthCaptureHook() noexcept;

	void NotePrepassGeometrySetup(RE::BSRenderPass* pass) noexcept;
	void NotePrepassGeometryRestore(RE::BSRenderPass* pass) noexcept;

	[[nodiscard]] bool TryBeginOrdinaryForwardPass(RE::BSRenderPass* pass) noexcept;

	[[nodiscard]] bool PrimeOrdinaryForwardPassCommit(RE::BSRenderPass* pass) noexcept;

	[[nodiscard]] bool TrySubstituteEssentialForwardVertexShader(
		RE::BSShader* shader, std::uint32_t requestedVertexDescriptor,
		std::uint32_t hullDescriptor, std::uint32_t domainDescriptor,
		std::uint32_t pixelDescriptor, const void* outputStruct,
		std::uint32_t& nativeVertexDescriptor) noexcept;

	void NoteEssentialForwardVertexShaderResult(
		std::uint32_t requestedVertexDescriptor, std::uint32_t nativeVertexDescriptor,
		std::uint32_t pixelDescriptor, bool accepted) noexcept;

	void EndOrdinaryForwardPass(
		RE::BSRenderPass* pass, bool nativeCompleted, bool accepted) noexcept;

	void NoteForwardGetRenderPassesStage(const char* stage, const void* shaderProperty,
		const void* geometry, std::uint32_t renderMode, const void* accumulator,
		const void* result) noexcept;

	void NoteForwardGetRenderPassesResult(const void* shaderProperty, const void* geometry,
		std::uint32_t renderMode, const void* accumulator, const void* result) noexcept;

	[[nodiscard]] bool PrepassTransactionActive() noexcept;

	[[nodiscard]] bool ReflectedRasterizerRequired() noexcept;

	[[nodiscard]] bool NativeWorldTransactionActive() noexcept;

	enum class NativeWorldPassWindingScope : std::uint8_t
	{
		kInactive,
		kGeometry,
		kOrdinary
	};

	[[nodiscard]] NativeWorldPassWindingScope BeginNativeWorldGeometryPass(
		RE::BSRenderPass* pass) noexcept;

	void EndNativeWorldGeometryPass(NativeWorldPassWindingScope scope) noexcept;

	[[nodiscard]] bool NativeWorldGeometryPassActive() noexcept;

	void NoteNativeWorldTechnique(RE::BSShader* shader) noexcept;

	[[nodiscard]] bool NativeWorldCurrentTechniqueNeedsReflectedWinding() noexcept;

	struct NativeWorldSetDirtyCommitToken
	{
		std::uintptr_t transactionIdentity{};
		std::uint32_t commitIdentity{};

		[[nodiscard]] explicit operator bool() const noexcept
		{
			return transactionIdentity != 0 && commitIdentity != 0;
		}
	};

	[[nodiscard]] NativeWorldSetDirtyCommitToken BeginNativeWorldSetDirtyCommit() noexcept;

	[[nodiscard]] bool AttestNativeWorldTargetsAfterSetDirty(
		const NativeWorldSetDirtyCommitToken& token) noexcept;

	void CompleteNativeWorldSetDirtyCommit(const NativeWorldSetDirtyCommitToken& token,
		bool exactNativeTargetsObserved, bool reflectedWindingRequired,
		bool rasterizerPolicyApplied) noexcept;

	[[nodiscard]] bool RebindForwardOutputsAfterSetDirty() noexcept;

	void NoteTransactionCommit() noexcept;

	void NoteReflectedRasterizerCommit(bool applied) noexcept;

	void NoteOrdinaryRasterizerCommit() noexcept;
}

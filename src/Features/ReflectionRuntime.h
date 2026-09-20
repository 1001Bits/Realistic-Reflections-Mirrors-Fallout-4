#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

#include <cstddef>
#include <cstdint>

#include "REL/Relocation.h"

namespace ReflectionRuntime
{

	struct Contract
	{
		REL::Version version{};
		bool vr{ false };

		std::uintptr_t drawWorldDeferredPrePass{};
		std::uintptr_t drawWorldDeferredLightsImpl{};
		std::uintptr_t drawWorldDeferredComposite{};
		std::uintptr_t drawWorldForward{};
		std::uintptr_t drawWorldRenderPreUI{};

		std::uintptr_t reflectCameraAboutPlane{};
		std::uintptr_t buildCameraStateData{};
		std::uintptr_t setDirtyStates{};
		std::uintptr_t cubeCameraCtor{};
		std::uintptr_t niCameraCtor{};
		std::size_t niCameraSize{};
		std::size_t cubeCameraSize{};
		std::size_t cubeCameraAccumulatorOffset{};

		std::uintptr_t cullingGroupConstruct{};
		std::uintptr_t cullingGroupReset{};
		std::uintptr_t cullingGroupStartAdding{};
		std::uintptr_t cullingGroupAdd{};
		std::uintptr_t cullingGroupProcess{};
		std::uintptr_t cullingGroupAccumulate{};
		std::uintptr_t cullingGroupCleanup{};
		std::uintptr_t cullingGroupArenaDtor{};
		std::uintptr_t cullingGroupSubgroupArenaDtor{};
		std::uintptr_t cullingBatchQEnabled{};
		std::uintptr_t accumulationMTAQEnabled{};
		std::size_t cullingGroupSize{};

		std::uintptr_t shaderRenderScene{};
		std::uintptr_t initializeRenderContext{};
		std::size_t renderContextSize{};
		std::uintptr_t cameraEyePosition{};
		std::uintptr_t cameraSetViewFrustum{};
		std::uintptr_t graphicsState{};
		std::uintptr_t setCameraData{};
		std::uintptr_t activeShadowSceneNode{};
		std::uintptr_t vrRendererPointer{};
		std::uintptr_t mainRendererPointer{};
		std::uintptr_t renderTargetManager{};
		std::uintptr_t setCubeRenderTarget{};
		std::uintptr_t setDepthRenderTarget{};
		std::uintptr_t forceViewport{};
		std::uintptr_t rendererFlush{};
		std::uintptr_t resetZPrePass{};
		std::size_t accumulatorSetCameraVtableOffset{};
		std::size_t accumulatorActiveShadowSceneNodeOffset{};
		std::size_t accumulatorRenderModeOffset{};
		std::size_t accumulatorEye0Offset{};
		std::size_t accumulatorEye1Offset{};
		std::size_t rendererDirtyFlagsOffset{};
		std::size_t rendererDepthStencilModeOffset{};
		std::size_t rendererCullModeOffset{};

		std::uintptr_t lightingPropertyGetRenderPasses{};
		std::uintptr_t effectPropertyGetRenderPasses{};
		std::size_t propertyGetRenderPassesSlot{};
		std::uintptr_t effectShaderSetupGeometry{};
		std::uintptr_t effectShaderRestoreGeometry{};
		std::size_t effectShaderSetupGeometrySlot{};
		std::size_t effectShaderRestoreGeometrySlot{};
	};

	[[nodiscard]] const Contract* Get() noexcept;
	[[nodiscard]] bool Supported() noexcept;
	[[nodiscard]] bool IsVR() noexcept;
	[[nodiscard]] std::uintptr_t Address(std::uintptr_t rva) noexcept;
	
	struct Port240IdEntry
	{
		std::uint64_t id;  
		std::uint32_t ng;  
	};
	struct Port240Entry
	{
		std::uint32_t og;
		std::uint32_t ng;
	};
	
	struct PortVREntry
	{
		std::uint32_t og;
		std::uint32_t vr;
	};
	
	struct Port240Prologue
	{
		std::uint32_t og;
		std::uint8_t size;
		std::uint8_t bytes[48];
	};
	
	struct Port240Range
	{
		std::uint32_t ogBegin;
		std::uint32_t ngBegin;
		std::uint32_t ngEndInclusive;
		std::uint8_t sha256[32];
	};
	
	[[nodiscard]] const Port240Range* Range240(std::uintptr_t ogBegin) noexcept;
	
	[[nodiscard]] const std::uint8_t* Port240ExecutableSha256() noexcept;

	[[nodiscard]] std::uintptr_t Rva(std::uintptr_t ogRva) noexcept;

	[[nodiscard]] bool IsPort240() noexcept;

	[[nodiscard]] std::uintptr_t Actor3DUpdateJobAddress() noexcept;

	struct RecoveryContract
	{
		std::uintptr_t poolScope, passPoolSlot, lightPoolSlot, mtaMode;
		std::array<std::uint8_t, 50> poolScopeEntry;
		std::uintptr_t cellMusicRead, playerRegionSlot;
		std::array<std::uint8_t, 36> cellMusicEntry;
		
		std::uintptr_t faceAnimationCtor, faceAnimationVtable, faceNodeVtable;
		std::array<std::uint8_t, 24> faceAnimationCtorEntry;
		std::uintptr_t subtitleHide;
		std::array<std::uint8_t, 20> subtitleHideEntry;
		std::uintptr_t animationCopyEnable;
		std::array<std::uint8_t, 54> animationCopyEnableEntry;
	};
	[[nodiscard]] const RecoveryContract* Recovery() noexcept;
	[[nodiscard]] bool Port240Complete() noexcept;

	[[nodiscard]] std::uint32_t* RenderTargetIds(void* manager) noexcept;
	[[nodiscard]] std::uint32_t* DepthStencilTargetIds(void* manager) noexcept;
	
	[[nodiscard]] std::uintptr_t PortId(std::uint64_t ogId) noexcept;
	
	template <std::size_t N>
	[[nodiscard]] const std::array<std::uint8_t, N>& Prologue(std::uintptr_t ogRva, const std::array<std::uint8_t, N>& ogBytes) noexcept;
	bool ReadPrologueBytes(std::uintptr_t ogRva, std::uint8_t* out, std::size_t size) noexcept;
	template <std::size_t N>
	const std::array<std::uint8_t, N>& Prologue(std::uintptr_t ogRva, const std::array<std::uint8_t, N>& ogBytes) noexcept
	{
		if (!IsPort240() && !IsVR())
			return ogBytes;
		static thread_local std::array<std::uint8_t, N> buffer{};
		if (!ReadPrologueBytes(ogRva, buffer.data(), N))
			buffer.fill(0xCCu);
		return buffer;
	}
}

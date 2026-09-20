#include "ReflectionRuntime.h"
#include <cstring>
#include <windows.h>

#include <memory>

namespace ReflectionRuntime
{
	namespace
	{
		constexpr Contract kFlat163{
			.version = { 1, 10, 163, 0 },
			.vr = false,
			.drawWorldDeferredPrePass = 0x2850CB0,
			.drawWorldDeferredLightsImpl = 0x28529B0,
			.drawWorldDeferredComposite = 0x2855E60,
			.drawWorldForward = 0x2856B70,
			.drawWorldRenderPreUI = 0x2857480,
			.reflectCameraAboutPlane = 0x1C94140,
			.buildCameraStateData = 0x1D22BD0,
			.setDirtyStates = 0x1D16C00,
			.cubeCameraCtor = 0x288ABA0,
			.niCameraCtor = 0,
			.niCameraSize = 0,
			.cubeCameraSize = 0x1E0,
			.cubeCameraAccumulatorOffset = 0x1B8,
			.cullingGroupConstruct = 0x288B9C0,
			.cullingGroupReset = 0x1CCD740,
			.cullingGroupStartAdding = 0x064B980,
			.cullingGroupAdd = 0x1CCCC30,
			.cullingGroupProcess = 0x1CCCE70,
			.cullingGroupAccumulate = 0x1CCD520,
			.cullingGroupCleanup = 0,
			.cullingGroupArenaDtor = 0,
			.cullingGroupSubgroupArenaDtor = 0,
			.cullingBatchQEnabled = 0,
			.accumulationMTAQEnabled = 0,
			.cullingGroupSize = 0x170,
			.shaderRenderScene = 0x281B340,
			.initializeRenderContext = 0,
			.renderContextSize = 0,
			.cameraEyePosition = 0,
			.cameraSetViewFrustum = 0x1BAC520,
			.graphicsState = 0x6541EF0,
			.setCameraData = 0x1D215C0,
			.activeShadowSceneNode = 0x6721B70,
			.vrRendererPointer = 0,
			.mainRendererPointer = 0,
			.renderTargetManager = 0x384FD30,
			.setCubeRenderTarget = 0x1D319C0,
			.setDepthRenderTarget = 0x1D31940,
			.forceViewport = 0x1D31B20,
			.rendererFlush = 0x1D0B760,
			.resetZPrePass = 0x1D125E0,
			.accumulatorSetCameraVtableOffset = 0x140,
			.accumulatorActiveShadowSceneNodeOffset = 0x558,
			.accumulatorRenderModeOffset = 0x560,
			.accumulatorEye0Offset = 0x570,
			.accumulatorEye1Offset = 0x570,
			.rendererDirtyFlagsOffset = 0x1B70,
			.rendererDepthStencilModeOffset = 0x1C18,
			.rendererCullModeOffset = 0x1C2C,
			.lightingPropertyGetRenderPasses = 0x27CD790,
			.effectPropertyGetRenderPasses = 0x27CAE10,
			.propertyGetRenderPassesSlot = 0x2B,
			.effectShaderSetupGeometry = 0x2886600,
			.effectShaderRestoreGeometry = 0x2887CA0,
			.effectShaderSetupGeometrySlot = 0x07,
			.effectShaderRestoreGeometrySlot = 0x08
		};

		constexpr Contract kVR172{
			.version = { 1, 2, 72, 0 },
			.vr = true,
			.drawWorldDeferredPrePass = 0x2844B60,
			.drawWorldDeferredLightsImpl = 0x284BD00,
			.drawWorldDeferredComposite = 0x284D680,
			.drawWorldForward = 0x284E060,
			.drawWorldRenderPreUI = 0x284E9E0,
			.reflectCameraAboutPlane = 0x1D13F60,
			.buildCameraStateData = 0x1DAA370,
			.setDirtyStates = 0x1D9B5D0,
			.cubeCameraCtor = 0x28988F0,
			.niCameraCtor = 0x1C2BAD0,
			.niCameraSize = 0x230,
			.cubeCameraSize = 0x270,
			.cubeCameraAccumulatorOffset = 0x248,
			.cullingGroupConstruct = 0x289AD80,
			.cullingGroupReset = 0x1D4D150,
			.cullingGroupStartAdding = 0x06384E0,
			.cullingGroupAdd = 0x1D4FAB0,
			.cullingGroupProcess = 0x1D4C860,
			.cullingGroupAccumulate = 0x1D4CF30,
			.cullingGroupCleanup = 0x1D4D0C0,
			.cullingGroupArenaDtor = 0x0637660,
			.cullingGroupSubgroupArenaDtor = 0x0637570,
			.cullingBatchQEnabled = 0x1D4B6C0,
			.accumulationMTAQEnabled = 0x28751D0,
			.cullingGroupSize = 0x180,
			.shaderRenderScene = 0x27FF820,
			.initializeRenderContext = 0x2812BE0,
			.renderContextSize = 0x2D8,
			.cameraEyePosition = 0x010EF50,
			.cameraSetViewFrustum = 0x1C2BFA0,
			.graphicsState = 0x65A2AB0,
			.setCameraData = 0x1DA8BF0,
			.activeShadowSceneNode = 0x6879520,
			.vrRendererPointer = 0x6235AC8,
			.mainRendererPointer = 0x6235AC0,
			.renderTargetManager = 0x38AC010,
			.setCubeRenderTarget = 0x1DB9EA0,
			.setDepthRenderTarget = 0x1DB9E40,
			.forceViewport = 0x1DB9FE0,
			.rendererFlush = 0x1D8DC70,
			.resetZPrePass = 0x1D95240,
			.accumulatorSetCameraVtableOffset = 0x150,
			.accumulatorActiveShadowSceneNodeOffset = 0xF680,
			.accumulatorRenderModeOffset = 0xF688,
			.accumulatorEye0Offset = 0xF690,
			.accumulatorEye1Offset = 0xF6A0,
			.rendererDirtyFlagsOffset = 0x1EE0,
			.rendererDepthStencilModeOffset = 0x1F88,
			.rendererCullModeOffset = 0x1F9C,
			.lightingPropertyGetRenderPasses = 0x27A3250,
			.effectPropertyGetRenderPasses = 0x279E950,
			.propertyGetRenderPassesSlot = 0x2D,
			.effectShaderSetupGeometry = 0x28CF8C0,
			.effectShaderRestoreGeometry = 0x28D1650,
			.effectShaderSetupGeometrySlot = 0x09,
			.effectShaderRestoreGeometrySlot = 0x0A
		};
	}

	namespace
	{
#include "ReflectionRuntimePort240.inl"
#include "ReflectionRuntimePortVR.inl"
		constexpr REL::Version kVersion240{ 1, 11, 240, 0 };
		bool Version240Cached() noexcept
		{
			static const bool is240 = !REL::Module::IsVR() && REL::Module::get().version() == kVersion240;
			return is240;
		}
		
		Contract kNG240 = kFlat163;
		bool g_ng240ContractReady = false;

		const Contract* NG240Contract() noexcept
		{
			if (!g_ng240ContractReady) {
				kNG240.version = kVersion240;
				g_ng240ContractReady = true;
			}
			return std::addressof(kNG240);
		}
	}

	std::uintptr_t Actor3DUpdateJobAddress() noexcept
	{

		if (IsPort240()) return REL::Offset(0xDB9CA0u).address();

		return !REL::Module::IsVR() && REL::Module::get().version() == kFlat163.version ?
			REL::Offset(0xF0D1D0u).address() : 0u;
	}

	const RecoveryContract* Recovery() noexcept
	{

		static constexpr RecoveryContract flat163{
			0x287D770u, 0x6732B70u, 0x6732B78u, 0x6723A44u,
			{ 0x84, 0xC9, 0x74, 0x17, 0x48, 0x8B, 0x05, 0xF5, 0x53, 0xEB, 0x03, 0xF0, 0xFF, 0x40, 0x34, 0x48, 0x8B, 0x05, 0xF2, 0x53, 0xEB, 0x03, 0xF0, 0xFF, 0x40, 0x34, 0xC3, 0x48, 0x8B, 0x05, 0xE6, 0x53, 0xEB, 0x03, 0xF0, 0xFF, 0x48, 0x34, 0x48, 0x8B, 0x05, 0xD3, 0x53, 0xEB, 0x03, 0xF0, 0xFF, 0x48, 0x34, 0xC3 },
			0x3B519Cu, 0x58D3300u,
			{ 0x48, 0x8B, 0x05, 0x5D, 0xE1, 0x51, 0x05, 0x48, 0x8B, 0x78, 0x50, 0x48, 0x85, 0xFF, 0x75, 0x14, 0x48, 0x8B, 0x8E, 0xC8, 0x00, 0x00, 0x00, 0x48, 0x85, 0xC9, 0x74, 0x08, 0xE8, 0x13, 0xE8, 0x0D, 0x00, 0x48, 0x8B, 0xF8 },
			0x668430u, 0x2CE9C58u, 0x2CEB648u,
			{ 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x8B, 0x05, 0xAE, 0x63 },
			0x12B3220u,
			{ 0x48, 0x89, 0x5C, 0x24, 0x18, 0x56, 0x57, 0x41, 0x56, 0x48, 0x83, 0xEC, 0x20, 0x44, 0x8B, 0x05, 0x80, 0x15, 0x48, 0x05 },
			0x19B61C0u,
			{ 0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x48, 0x63, 0x41, 0x20, 0x4C, 0x8B, 0xDA, 0x49, 0x8B, 0xD9, 0x4C, 0x8D, 0x14, 0x80, 0x48, 0x8B, 0x41, 0x10, 0x49, 0x8B, 0xF8, 0x4D, 0x8B, 0xC8, 0x4A, 0x8D, 0x14, 0xD0, 0x49, 0x8B, 0xCB, 0x41, 0xB0, 0x01, 0xC6, 0x44, 0x24, 0x20, 0x00, 0xE8, 0x5A, 0xB5, 0xF1, 0xFF }
		};
		static constexpr RecoveryContract flat240{
			0x221D940u, 0x3E71580u, 0x3E71588u, 0x3E6245Cu,
			{ 0x84, 0xC9, 0x74, 0x17, 0x48, 0x8B, 0x05, 0x35, 0x3C, 0xC5, 0x01, 0xF0, 0xFF, 0x40, 0x34, 0x48, 0x8B, 0x05, 0x32, 0x3C, 0xC5, 0x01, 0xF0, 0xFF, 0x40, 0x34, 0xC3, 0x48, 0x8B, 0x05, 0x26, 0x3C, 0xC5, 0x01, 0xF0, 0xFF, 0x48, 0x34, 0x48, 0x8B, 0x05, 0x13, 0x3C, 0xC5, 0x01, 0xF0, 0xFF, 0x48, 0x34, 0xC3 },
			0x4CB26Cu, 0x30EB310u,
			{ 0x48, 0x8B, 0x05, 0x9D, 0x00, 0xC2, 0x02, 0x48, 0x8B, 0x78, 0x50, 0x48, 0x85, 0xFF, 0x75, 0x14, 0x48, 0x8B, 0x8E, 0xC8, 0x00, 0x00, 0x00, 0x48, 0x85, 0xC9, 0x74, 0x08, 0xE8, 0x73, 0x95, 0x0A, 0x00, 0x48, 0x8B, 0xF8 },
			0x6D1030u, 0x2506020u, 0x25072F0u,
			{ 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x20 },
			0x107E1B0u,
			{ 0x48, 0x89, 0x5C, 0x24, 0x18, 0x56, 0x57, 0x41, 0x56, 0x48, 0x83, 0xEC, 0x20, 0x44, 0x8B, 0x05, 0x54, 0x4E, 0xDF, 0x02 },
			0x155EC80u,
			{ 0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x48, 0x63, 0x41, 0x20, 0x4C, 0x8B, 0xDA, 0x49, 0x8B, 0xD9, 0xC6, 0x44, 0x24, 0x20, 0x00, 0x49, 0x8B, 0xF8, 0x4D, 0x8B, 0xC8, 0x41, 0xB0, 0x01, 0x4C, 0x8D, 0x14, 0x80, 0x48, 0x8B, 0x41, 0x10, 0x49, 0x8B, 0xCB, 0x4A, 0x8D, 0x14, 0xD0, 0xE8, 0x9A, 0xAD, 0xF6, 0xFF }
		};
		static constexpr RecoveryContract vr172{
			0x2890780u, 0x689ACD0u, 0x689ACD8u, 0x6886C5Cu,
			{ 0x84, 0xC9, 0x74, 0x17, 0x48, 0x8B, 0x05, 0x45, 0xA5, 0x00, 0x04, 0xF0, 0xFF, 0x40, 0x34, 0x48, 0x8B, 0x05, 0x42, 0xA5, 0x00, 0x04, 0xF0, 0xFF, 0x40, 0x34, 0xC3, 0x48, 0x8B, 0x05, 0x36, 0xA5, 0x00, 0x04, 0xF0, 0xFF, 0x48, 0x34, 0x48, 0x8B, 0x05, 0x23, 0xA5, 0x00, 0x04, 0xF0, 0xFF, 0x48, 0x34, 0xC3 },
			0x39B86Cu, 0x5935420u,
			{ 0x48, 0x8B, 0x05, 0xAD, 0x9B, 0x59, 0x05, 0x48, 0x8B, 0x78, 0x50, 0x48, 0x85, 0xFF, 0x75, 0x14, 0x48, 0x8B, 0x8E, 0xC8, 0x00, 0x00, 0x00, 0x48, 0x85, 0xC9, 0x74, 0x08, 0xE8, 0x83, 0x12, 0x0E, 0x00, 0x48, 0x8B, 0xF8 },
			0x0u, 0x0u, 0x0u,
			{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
			0x13306B0u,
			{ 0x48, 0x89, 0x5C, 0x24, 0x18, 0x56, 0x57, 0x41, 0x56, 0x48, 0x83, 0xEC, 0x20, 0x44, 0x8B, 0x05, 0x08, 0xC4, 0x56, 0x05 },
			0x1A38AC0u,
			{ 0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x48, 0x63, 0x41, 0x20, 0x4C, 0x8B, 0xDA, 0x49, 0x8B, 0xD9, 0x4C, 0x8D, 0x14, 0x80, 0x48, 0x8B, 0x41, 0x10, 0x49, 0x8B, 0xF8, 0x4D, 0x8B, 0xC8, 0x4A, 0x8D, 0x14, 0xD0, 0x49, 0x8B, 0xCB, 0x41, 0xB0, 0x01, 0xC6, 0x44, 0x24, 0x20, 0x00, 0xE8, 0x0A, 0xAF, 0xF1, 0xFF }
		};
		const auto* runtime = Get();
		if (!runtime)
			return nullptr;
		if (runtime->vr)
			return &vr172;
		return IsPort240() ? &flat240 : &flat163;
	}

	std::uint32_t* RenderTargetIds(void* manager) noexcept
	{
		const std::uintptr_t offset = Version240Cached() ? 0xDF4u : 0xDC4u;
		return reinterpret_cast<std::uint32_t*>(reinterpret_cast<std::uintptr_t>(manager) + offset);
	}

	std::uint32_t* DepthStencilTargetIds(void* manager) noexcept
	{
		const std::uintptr_t offset = Version240Cached() ? 0xF84u : 0xF54u;
		return reinterpret_cast<std::uint32_t*>(reinterpret_cast<std::uintptr_t>(manager) + offset);
	}

	bool Port240Complete() noexcept
	{
		return kPort240UnmappedCount == 0;
	}

	namespace
	{

		void ReportPort240Once() noexcept
		{
			static bool reported = false;
			if (reported)
				return;
			reported = true;
			if (Port240Complete()) {
				logger::info("[Port240] address map complete: {} RVAs, {} address-library ids", kPort240TableCount, kPort240IdTableCount);
				return;
			}
			logger::error("[Port240] address map INCOMPLETE ({} of {} RVAs unmapped) - 1.11.240 runtime disabled until every address is mapped", kPort240UnmappedCount, kPort240UnmappedCount + kPort240TableCount);
			for (std::size_t i = 0; i < kPort240UnmappedCount; ++i)
				logger::error("[Port240]   unmapped 1.10.163 RVA 0x{:X}", kPort240Unmapped[i]);
		}
	}

	bool IsPort240() noexcept
	{
		if (!Version240Cached())
			return false;
		ReportPort240Once();
		return Port240Complete();
	}

	std::uintptr_t Rva(std::uintptr_t ogRva) noexcept
	{
		if (REL::Module::IsVR()) {

			std::size_t lo = 0, hi = kPortVRTableCount;
			while (lo < hi) {
				const std::size_t mid = (lo + hi) / 2;
				if (kPortVRTable[mid].og < ogRva)
					lo = mid + 1;
				else
					hi = mid;
			}
			if (lo < kPortVRTableCount && kPortVRTable[lo].og == ogRva)
				return kPortVRTable[lo].vr;

			return ogRva;
		}
		if (!Version240Cached())
			return ogRva;
		std::size_t lo = 0, hi = kPort240TableCount;
		while (lo < hi) {
			const std::size_t mid = (lo + hi) / 2;
			if (kPort240Table[mid].og < ogRva)
				lo = mid + 1;
			else
				hi = mid;
		}
		if (lo < kPort240TableCount && kPort240Table[lo].og == ogRva)
			return kPort240Table[lo].ng;
		return 0;
	}

	const Port240Range* Range240(std::uintptr_t ogBegin) noexcept
	{
		if (!Version240Cached())
			return nullptr;
		for (std::size_t i = 0; i < kPort240RangeCount; ++i) {
			if (kPort240Ranges[i].ogBegin == ogBegin)
				return std::addressof(kPort240Ranges[i]);
		}
		return nullptr;
	}

	const std::uint8_t* Port240ExecutableSha256() noexcept
	{
		return kPort240ExecutableSha256;
	}

	std::uintptr_t PortId(std::uint64_t ogId) noexcept
	{
		if (!IsPort240())
			return 0;
		std::size_t lo = 0, hi = kPort240IdTableCount;
		while (lo < hi) {
			const auto mid = lo + (hi - lo) / 2;
			if (kPort240IdTable[mid].id < ogId)
				lo = mid + 1;
			else
				hi = mid;
		}
		if (lo < kPort240IdTableCount && kPort240IdTable[lo].id == ogId)
			return kPort240IdTable[lo].ng;
		return 0;
	}

	bool ReadPrologueBytes(std::uintptr_t ogRva, std::uint8_t* out, std::size_t size) noexcept
	{
		if (!out || size == 0)
			return false;
		if (REL::Module::IsVR()) {
			for (std::size_t index = 0; index < kPortVRPrologueCount; ++index) {
				const auto& entry = kPortVRPrologues[index];
				if (entry.og != ogRva)
					continue;
				if (entry.size < size)
					return false;
				std::memcpy(out, entry.bytes, size);
				return true;
			}
			return false;
		}
		for (std::size_t index = 0; index < kPort240PrologueCount; ++index) {
			const auto& entry = kPort240Prologues[index];
			if (entry.og != ogRva)
				continue;
			if (entry.size < size)
				return false;
			std::memcpy(out, entry.bytes, size);
			return true;
		}
		return false;
	}

	const Contract* Get() noexcept
	{
		const auto version = REL::Module::get().version();
		if (REL::Module::IsVR())
			return version == kVR172.version ? std::addressof(kVR172) : nullptr;
		if (version == kVersion240)
			return IsPort240() ? NG240Contract() : nullptr;
		if (!REL::Module::IsNG())
			return version == kFlat163.version ? std::addressof(kFlat163) : nullptr;
		return nullptr;
	}

	bool Supported() noexcept
	{
		return Get() != nullptr;
	}

	bool IsVR() noexcept
	{
		const auto* contract = Get();
		return contract && contract->vr;
	}

	std::uintptr_t Address(std::uintptr_t rva) noexcept
	{

		if (REL::Module::IsVR())
			return rva ? REL::Offset(rva).address() : 0;
		const auto mapped = Rva(rva);
		return mapped ? REL::Offset(mapped).address() : 0;
	}
}

extern "C" std::size_t DynRefPortIdFallback(std::uint64_t a_id) noexcept
{
	return ReflectionRuntime::PortId(a_id);
}

extern "C" bool DynRefPortIdSpaceActive() noexcept
{
	return ReflectionRuntime::IsPort240();
}

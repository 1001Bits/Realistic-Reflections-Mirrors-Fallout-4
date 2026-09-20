#include "Features/BSShaderTables.h"
#include "Features/ReflectionRuntime.h"
#include "FlatDeferredPlayerCapture.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

#include <bcrypt.h>
#include <d3d11_1.h>
#include <d3dcompiler.h>
#include <intrin.h>

#include "RE/Bethesda/BSGraphics.h"
#include "RE/Bethesda/Interface3D.h"
#include "RE/NetImmerse/NiNode.h"
#include "RE/NetImmerse/NiUpdateData.h"
#include "RE/VTABLE_IDs.h"

#include "PlanarMirrorLookup.h"

#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "d3dcompiler.lib")

namespace FlatDeferredPlayerCapture
{
	namespace
	{
		using Microsoft::WRL::ComPtr;

		constexpr std::uintptr_t kRVA_RenderSceneDeferred = 0x281B3C0;
		constexpr std::uintptr_t kRVA_Interface3DDrawModel = 0x0AE9020;
		constexpr std::uintptr_t kRVA_Interface3DDrawModelAmbientBlackCall = 0x0AE92CC;
		constexpr std::uintptr_t kRVA_Interface3DDrawModelAccumulateSceneCall = 0x0AE93C8;
		constexpr std::uintptr_t kRVA_Interface3DRenderMain = 0x0AE8780;
		constexpr std::uintptr_t kRVA_Interface3DDrawModelForward = 0x0AE97E0;
		constexpr std::uintptr_t kRVA_Interface3DFlattenedEntryLambda = 0x0AEA750;
		constexpr std::uintptr_t kRVA_Interface3DUpdateLights = 0x0AE9FB0;
		constexpr std::uintptr_t kRVA_ProcessQueuedLights = 0x28101E0;
		constexpr std::uintptr_t kRVA_SetSunLight = 0x2811360;
		constexpr std::uintptr_t kRVA_NiAVObjectUpdate = 0x1BA3BE0;
		constexpr std::uintptr_t kRVA_NiNodeAttachChild = 0x1B98A10;
		constexpr std::uintptr_t kRVA_NiNodeDetachChild = 0x1B98B60;
		constexpr std::uintptr_t kRVA_AcquireDepthStencil = 0x1D32AE0;
		constexpr std::uintptr_t kRVA_ReleaseDepthStencil = 0x1D32B50;

		constexpr std::uintptr_t kRVA_DeferredPrepassDepthClearCall = 0x281C96C;
		constexpr std::uintptr_t kRVA_AccumulateScene = 0x281ADE0;
		constexpr std::uintptr_t kRVA_ClearRenderPasses = 0x28201D0;
		constexpr std::uintptr_t kRVA_ClearActivePasses = 0x282F080;
		constexpr std::uintptr_t kRVA_ClearGroupPasses = 0x282EAD0;
		constexpr std::uintptr_t kRVA_FinishAccumulating = 0x282DCB0;
		constexpr std::uintptr_t kRVA_AccumulatorSetCamera = 0x282CEC0;
		constexpr std::uintptr_t kRVA_CullerCtor = 0x1CCDE90;
		constexpr std::uintptr_t kRVA_CullerDtor = 0x1CCDF10;
		constexpr std::uintptr_t kRVA_CullerSetAccumulator = 0x1CCDF70;

		constexpr std::uintptr_t kRVA_CullerSetCameraFrustum = 0x1BC4B70;
		constexpr std::uintptr_t kRVA_SetCurrentAccumulator = 0x27D7000;
		constexpr std::uintptr_t kRVA_SetShadowSceneNode = 0x27D6190;
		constexpr std::uintptr_t kRVA_CacheCameraData = 0x1D21650;
		constexpr std::uintptr_t kRVA_SetCameraData = 0x1D215C0;
		constexpr std::uintptr_t kRVA_SetCurrentRenderTarget = 0x1D318B0;
		constexpr std::uintptr_t kRVA_SetCurrentDepthStencil = 0x1D31940;
		constexpr std::uintptr_t kRVA_SetViewportDefault = 0x1D31AC0;
		constexpr std::uintptr_t kRVA_SetViewportForce = 0x1D31B20;
		constexpr std::uintptr_t kRVA_RendererFlush = 0x1D0B760;
		constexpr std::uintptr_t kRVA_ResetZPrePass = 0x1D125E0;
		constexpr std::uintptr_t kRVA_ClearDepthStencil = 0x1D0B970;
		constexpr std::uintptr_t kRVA_DistantRendererSingleton = 0x01E09A0;
		constexpr std::uintptr_t kRVA_HBAOSettingUpdated = 0x1C91E10;
		constexpr std::uintptr_t kRVA_SetDirectionalAmbientColors = 0x27D64E0;
		constexpr std::uintptr_t kRVA_GetDirectionalAmbientColors = 0x27D67B0;

		constexpr std::uintptr_t kRVA_RenderTargetManager = 0x384FD30;
		constexpr std::uintptr_t kRVA_GraphicsState = 0x6541EF0;
		constexpr std::uintptr_t kRVA_Renderer = 0x61E0900;
		constexpr std::uintptr_t kRVA_CurrentAccumulator = 0x6721AB0;
		constexpr std::uintptr_t kRVA_ShaderCamera = 0x6721AE0;
		constexpr std::uintptr_t kRVA_ActiveShadowSceneNode = 0x6721B70;
		constexpr std::uintptr_t kRVA_GameTLSIndex = 0x67347B4;
		constexpr std::uintptr_t kRVA_DefaultContext = 0x61DDC68;
		constexpr std::uintptr_t kRVA_CameraFlushScalar = 0x2C48D60;
		constexpr std::uintptr_t kRVA_ImageSpaceManager = 0x67220E8;
		constexpr std::uintptr_t kRVA_HBAOSetting = 0x384F648;
		constexpr std::uintptr_t kRVA_HBAOEnabled = 0x384F650;
		constexpr std::uintptr_t kRVA_AsyncAO = 0x6723BE0;
		constexpr std::uintptr_t kRVA_Global1CDC = 0x6721CDC;
		constexpr std::uintptr_t kRVA_Global1BC6 = 0x6721BC6;
		constexpr std::uintptr_t kRVA_Global1BC7 = 0x6721BC7;
		constexpr std::uintptr_t kRVA_Global1BC8 = 0x6721BC8;
		constexpr std::uintptr_t kRVA_Global1BCC = 0x6721BCC;
		constexpr std::uintptr_t kRVA_Global1C19 = 0x6721C19;
		constexpr std::uintptr_t kRVA_Global1F55 = 0x6721F55;
		constexpr std::uintptr_t kRVA_DirectionalAmbientState = 0x6721CB0;
		constexpr std::uintptr_t kRVA_DepthStencilReservationPointers = 0x6542980;
		constexpr std::uintptr_t kRVA_DepthStencilPersistency = 0x6542AA8;
		constexpr std::uintptr_t kRVA_InterfaceDisplayGeometry = 0x6721D30;
		constexpr std::uintptr_t kRVA_InterfaceGlobal1D34 = 0x6721D34;
		constexpr std::uintptr_t kRVA_InterfaceGlobal1D34Source = 0x377C768;
		constexpr std::uintptr_t kRVA_InterfacePostAA = 0x6721D38;
		constexpr std::uintptr_t kRVA_InterfaceOpacityAlpha = 0x6721D3C;
		constexpr std::uintptr_t kRVA_InterfaceMenuEmitIntensity = 0x6721BE0;
		constexpr std::uintptr_t kRVA_InterfaceMenuDiffuseIntensity = 0x6721BE4;
		constexpr std::uintptr_t kRVA_ShadowSceneNodeVtable = 0x3095148;

		constexpr std::uint32_t kOutputLogicalColor = 0x3C;

		constexpr std::uint32_t kNativeWorldOutputLogicalDepth = 1;
		
		constexpr std::uint32_t kOutputLogicalDepth = 3;
		constexpr std::uint32_t kLogicalTargetNone = 0xFFFFFFFFu;
		constexpr std::uint32_t kPhysicalColorCount = 101;
		constexpr std::uint32_t kPhysicalDepthCount = 13;
		constexpr std::size_t kRenderTargetMappingCount = 100;
		constexpr std::size_t kDepthTargetMappingCount = 12;
		constexpr std::size_t kContextShadowStateOffset = 0x1B70;
		constexpr std::size_t kContextLastDrawStateOffset = 0x2480;
		constexpr std::size_t kContextStateSize = 0x910;
		constexpr std::size_t kCullerSize = 0x1A0;
		constexpr std::size_t kCullerCameraOffset = 0x18;
		constexpr std::size_t kCullerModeOffset = 0x158;
		constexpr std::size_t kAccumulatorCameraOffset = 0x10;
		constexpr std::size_t kAccumulatorForwardFlagOffset = 0xB1;
		constexpr std::size_t kAccumulatorShadowSceneNodeOffset = 0x558;
		constexpr std::size_t kAccumulatorRenderModeOffset = 0x560;
		constexpr std::size_t kAccumulatorCameraEyeOffset = 0x570;
		constexpr std::size_t kDistantRendererEnabledOffset = 0x70;
		constexpr std::size_t kEffectActiveOffset = 0x08;
		constexpr std::size_t kEffect47PrimaryOffset = 0x120;
		constexpr std::size_t kEffect47SecondaryOffset = 0x121;
		constexpr std::size_t kBSStringPoolEntryFlagsOffset = 0x08;
		constexpr std::size_t kBSStringPoolEntryValueOffset = 0x10;
		constexpr std::size_t kMaxBSFixedStringShallowDepth = 64;
		constexpr std::size_t kImageSpaceEffectArrayOffset = 0x18;
		constexpr std::size_t kDirectionalAmbientStateSize = 0x50;
		constexpr std::size_t kCameraStateOffset = 0x160;
		constexpr std::size_t kCameraStateSize = 0x250;
		constexpr std::size_t kCameraCacheOffset = 0x140;
		constexpr std::size_t kCameraCacheEntrySize = 0x250;
		constexpr std::size_t kMaxCameraCacheEntries = 32;
		constexpr std::size_t kShadowSceneNodeSlotCount = 5;
		constexpr std::size_t kInterfaceRendererEnabledOffset = 0x4C;
		constexpr std::size_t kInterfaceRendererOffscreenEnabledOffset = 0x4D;
		constexpr std::size_t kInterfaceRendererClearDepthMainOffset = 0x51;
		constexpr std::size_t kInterfaceRendererPostAAOffset = 0x53;
		constexpr std::size_t kInterfaceRendererDeferredMainOffset = 0x56;
		constexpr std::size_t kInterfaceRendererNeedsLightSetupOffset = 0x59;
		constexpr std::size_t kInterfaceRendererEnableAOOffset = 0x57;
		constexpr std::size_t kInterfaceRendererPostEffectOffset = 0x60;
		constexpr std::size_t kInterfaceRendererOffscreenSizeOffset = 0x68;
		constexpr std::size_t kInterfaceRendererWorldRootOffset = 0x78;
		constexpr std::size_t kInterfaceRendererScreenRootOffset = 0x80;
		constexpr std::size_t kInterfaceRendererOffscreenRootOffset = 0x88;
		constexpr std::size_t kInterfaceRendererAccumulatorOffset = 0x1A0;
		constexpr std::size_t kInterfaceRendererMainLightsSizeOffset = 0x1D8;
		constexpr std::size_t kInterfaceRendererDirectionalLightOffset = 0x230;
		constexpr std::size_t kInterfaceRendererScreenSSNOffset = 0x238;
		constexpr std::size_t kInterfaceRendererOffscreenSSNOffset = 0x240;
		constexpr std::size_t kMaxClassInstances = 256;

		constexpr std::size_t kMaxUAVSlots = D3D11_PS_CS_UAV_REGISTER_COUNT;
		constexpr std::size_t kMaxTextureBackups = 12;
		constexpr std::size_t kMaxBufferBackups = 1024;
		constexpr std::size_t kMaxColorBindings = 10;
		constexpr std::size_t kMaxContextBufferIdentities = 1024;
		constexpr std::size_t kContextConstantGroupCount = 16;
		constexpr std::size_t kMaxFlattenedGeometryEntries = 256;
		constexpr std::uint32_t kAppCulledFlag = 1u;
		constexpr std::size_t kRenderPassShaderOffset = 0x08;
		constexpr std::size_t kRenderPassShaderPropertyOffset = 0x10;
		constexpr std::size_t kRenderPassGeometryOffset = 0x18;
		constexpr std::size_t kRenderPassTechniqueOffset = 0x48;
		constexpr std::uint32_t kForwardSpecialAlphaTechniqueBit = 0x100000u;
		constexpr std::size_t kGeometrySkinInstanceOffset = 0x140;
		constexpr std::size_t kGeometryRendererDataOffset = 0x148;
		constexpr std::size_t kGeometryVertexDescriptorOffset = 0x150;
		constexpr std::size_t kGeometryTypeOffset = 0x158;
		constexpr std::size_t kEffectCFPODOffset = 0x240;

		struct EssentialForwardVertexShaderPermutation
		{
			std::uint32_t rawTechnique;
			std::uint32_t requestedVertexDescriptor;
			std::uint32_t pixelDescriptor;
			std::uint32_t nativeVertexDescriptor;
			bool requiresVertexColor;
		};

		constexpr std::array kEssentialForwardVertexShaderPermutations{
			EssentialForwardVertexShaderPermutation{ 0x502u, 0x502u, 0x501u, 0x002u, false },
			EssentialForwardVertexShaderPermutation{ 0x643u, 0x603u, 0x641u, 0x003u, true },
			EssentialForwardVertexShaderPermutation{ 0x402u, 0x402u, 0x401u, 0x002u, false },
			EssentialForwardVertexShaderPermutation{ 0x203u, 0x203u, 0x201u, 0x003u, true }
		};

		template <std::size_t N>
		consteval auto HexBytes(const char (&text)[N])
		{
			static_assert((N - 1) % 2 == 0);
			std::array<std::uint8_t, (N - 1) / 2> result{};
			auto nibble = [](char c) consteval -> std::uint8_t {
				if (c >= '0' && c <= '9')
					return static_cast<std::uint8_t>(c - '0');
				if (c >= 'a' && c <= 'f')
					return static_cast<std::uint8_t>(c - 'a' + 10);
				if (c >= 'A' && c <= 'F')
					return static_cast<std::uint8_t>(c - 'A' + 10);
				return 0xFF;
			};
			for (std::size_t i = 0; i < result.size(); ++i)
				result[i] = static_cast<std::uint8_t>((nibble(text[i * 2]) << 4) | nibble(text[i * 2 + 1]));
			return result;
		}

		constexpr FrozenContract kContract{
			.runtimeVersion = { 1, 10, 163, 0 },
			.executableSha256 = HexBytes("5B2A58004F1856E51235132AB304B20C1434017067703E4282E8B0D5BC539623"),
			.renderSceneDeferred = {
				0x281B3C0, 0x281CB5F,
				HexBytes("8cf5cde99d5cf50fbc0b93f9846d39f68be2cafdf9ea8ae9a25264ec18665be2") },
			.interface3DDrawModel = { 0x0AE9020, 0x0AE97D0, HexBytes("e8d78ce4e583544bd8b9e9f3e11ba7db0ea8e99dc6c2e3f752a25de157905d18") },
			.dfLightSetupGeometry = { 0x28C0780, 0x28C3191, HexBytes("59f0431ca098189dd83f3239af6b85f2aebf50a53065efe62fb3f32c5694a779") },
			.interface3DRenderMain = { 0x0AE8780, 0x0AE8B40, HexBytes("af5ecfe4a68fd3bf90a1529cdb2466cf1612d3ab999c4f441261f2048d5612e7") },
			.interface3DDrawModelForward = { 0x0AE97E0, 0x0AE99A7, HexBytes("8b7ad2006c0a6e13a6f415f8c62c1927217c6b8ab9f42befb45a79be5ed389bb") },
			.interface3DFlattenedEntryLambda = { 0x0AEA750, 0x0AEA8D6, HexBytes("504aa8cefc5ab490d21c3deccd389f915eda8fc828ff3f9b4cb61302a6372310") },
			.interface3DUpdateLights = { 0x0AE9FB0, 0x0AEA562, HexBytes("cca2e66ed83d38f92592075bb636c83c5d2086481ff1f79196a87c690d26a2d8") },
			.processQueuedLights = { 0x28101E0, 0x28103B2, HexBytes("f75efc89c2b3918b8144b36000ed8caf5163ee5dd5105c91e2c891801b1c295f") },
			.setSunLight = { 0x2811360, 0x2811503, HexBytes("5822d3b65711c97a012165132de44cbb93ef19a23e6ab256aebdaf1554020322") },
			.cullingProcessConstructor = { 0x1CCDE90, 0x1CCDEFC, HexBytes("fac915072dbfdf773ce2a3379b6301522e4d1189111cde57f8a02e8816c41e22") },
			.cullingProcessSetAccumulator = { 0x1CCDF70, 0x1CCDFA2, HexBytes("f9a854813c72e5c2ffb4c1f87239e79a6f9b65836858ae4c81f849b3c0e3fbce") },
			.cullingProcessDestructor = { 0x1CCDF10, 0x1CCDF69, HexBytes("2323f43a7bbcfeafe8784c70428ed7b52c10616b3eac012215594b10ed3175fc") },
			.clearGroupPasses = { 0x282EAD0, 0x282EAF4, HexBytes("aade1b6d053558c82efd41f2b1cc958d2449bf5980a1e2471c92d4667f1dde5a") },
			.clearActivePasses = { 0x282F080, 0x282F105, HexBytes("8c4cc4f38d91b45e54981eb8c91a8be28051dcd72755970ced206e39f7a16e1b") },
			.niAVObjectUpdate = { 0x1BA3BE0, 0x1BA3C2E, HexBytes("c54128e28786951396b6064f863b275ad3125c50e2db7fc681e5bbb6c1c6dd6f") },
			.niNodeAttachChild = { 0x1B98A10, 0x1B98AD9, HexBytes("b6d245af805ca513d2d539c4b7274651c247c30ef295202684ecdc96d685464f") },
			.niNodeDetachChild = { 0x1B98B60, 0x1B98B95, HexBytes("f5426a6fe38471cb2a7081507c6d0d64bf3837393c2bf128f7b05eb0b0f93907") },
			.acquireDepthStencil = { 0x1D32AE0, 0x1D32B3F, HexBytes("b88fef0060aa1ccbe92ec2e0ed32a8357522b60f5eb2b9c1892f4e064d4a85c4") },
			.acquireDepthStencilTarget = { 0x1D33720, 0x1D338E4, HexBytes("387bf7027f763e6253ac8478b79f2fa6c584a59b2d9573f7987696f5a02f982d") },
			.releaseDepthStencil = { 0x1D32B50, 0x1D32BE3, HexBytes("98d96390a13cea50cb242ecb462dc1b13352e60557c393c8b3724d9f39acd7fe") },
			.setCurrentDepthStencil = { 0x1D31940, 0x1D319AB, HexBytes("d752e936280772ddbb13dd143f451eed6a9213e833dff6e19fd07a9fd94d0039") },
			.outputColorLogicalRT = kOutputLogicalColor,
			.outputDepthLogicalDS = kOutputLogicalDepth,
			.dfLightInputs = { 0x1A, 0x1B, 0x1E, 1 },
			.dfPrepassMRTs = { 0x1A, 0x1B, 0x1D, 0x1E, 0x1F, 0x20 },
			.logicalColorWriteSet = { 1, 0x1A, 0x1B, 0x1D, 0x1E, 0x20, 0x21, kOutputLogicalColor },
			.conditionalLogicalColorWriteSet = { 0x1F, 0x22 }
		};

		constexpr auto kSetCurrentRTPrologue = HexBytes(
			"40534883EC30448B15F72EA00465488B042558000000448B");
		constexpr auto kSetCurrentDSPrologue = HexBytes(
			"448B156D2EA00465488B04255800000041BB200B00004A8B");
		constexpr auto kSetViewportDefaultPrologue = HexBytes(
			"4883EC388B15EA2CA00465488B04255800000041B8200B00");
		constexpr auto kRendererFlushPrologue = HexBytes("33D2B101E997B40000CCCCCCCCCCCCCC");
		constexpr auto kClearRenderPassesPrologue = HexBytes(
			"488D1519280000E974360000CCCCCCCCCCCCCCCCCCCCCCCC");
		constexpr auto kSetCurrentAccumulatorPrologue = HexBytes(
			"48890DA9AAF403C3CCCCCCCCCCCCCCCC48895C2408574883");
		constexpr auto kDistantRendererSingletonPrologue = HexBytes(
			"48895C2408574883EC2065488B0425580000008B0DFB3D5506BAC0090000488B");
		constexpr auto kCacheCameraDataPrologue = HexBytes(
			"48896C24104889742418574883EC20410FB6F0488BEA488BF9E8821B00004885");
		constexpr auto kFloatOne = HexBytes("0000803F");

		std::atomic_flag g_captureLock = ATOMIC_FLAG_INIT;
		std::atomic<std::uint64_t> g_completionSerial{ 1 };
		std::atomic<int> g_executableHashState{ 0 };
		std::atomic<int> g_frozenCodeContractState{ 0 };
		enum class CaptureEscapePhase : std::uint8_t
		{
			kOutside,
			kPreMutation,
			kPostMutation,
			kReservationAmbiguous
		};

		thread_local CaptureEscapePhase g_captureEscapePhase = CaptureEscapePhase::kOutside;

		constexpr auto kDeferredPrepassDepthClearCallBytes = HexBytes("E83F320600");
		
		constexpr auto kDrawModelAccumulateSceneCallBytes = HexBytes("E8131AD301");
		
		constexpr auto kDrawModelAmbientBlackCallBytes = HexBytes("E80FD2CE01");
		std::atomic<bool> g_prepassDepthHookInstalled{ false };
		std::array<std::uint8_t, 5> g_prepassDepthHookPatchedBytes{};
		std::atomic<bool> g_drawModelAccumulateHookInstalled{ false };
		std::array<std::uint8_t, 5> g_drawModelAccumulateHookPatchedBytes{};
		std::atomic<bool> g_drawModelAmbientPreservationHookInstalled{ false };
		std::array<std::uint8_t, 5> g_drawModelAmbientPreservationHookPatchedBytes{};

		[[nodiscard]] std::uintptr_t Address(std::uintptr_t rva) noexcept
		{
			const auto mapped = ReflectionRuntime::Rva(rva);
			return mapped ? REL::Module::get().base() + mapped : 0;
		}

		[[nodiscard]] bool SafeCopy(void* destination, const void* source, std::size_t size) noexcept
		{
			__try {
				std::memcpy(destination, source, size);
				return true;
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				return false;
			}
		}

		[[nodiscard]] bool SafeStore(void* destination, const void* source, std::size_t size) noexcept
		{
			__try {
				std::memcpy(destination, source, size);
				return true;
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				return false;
			}
		}

		template <class T>
		[[nodiscard]] bool SafeLoad(std::uintptr_t address, T& value) noexcept
		{
			static_assert(std::is_trivially_copyable_v<T>);
			return SafeCopy(std::addressof(value), reinterpret_cast<const void*>(address), sizeof(T));
		}

		template <std::size_t N>
		[[nodiscard]] bool InstalledHookCallsiteMatches(
			std::uintptr_t rva, const std::array<std::uint8_t, N>& installedSnapshot,
			const std::array<std::uint8_t, N>& pristineCallsite) noexcept
		{
			std::array<std::uint8_t, N> live{};
			return installedSnapshot[0] == 0xE8 && installedSnapshot != pristineCallsite &&
				SafeCopy(live.data(), reinterpret_cast<const void*>(Address(rva)), live.size()) &&
				live == installedSnapshot;
		}

		[[nodiscard]] bool NativeWorldInstalledHookIdentityMatches() noexcept
		{
			if (!g_prepassDepthHookInstalled.load(std::memory_order_acquire) ||
				!g_drawModelAccumulateHookInstalled.load(std::memory_order_acquire) ||
				!g_drawModelAmbientPreservationHookInstalled.load(std::memory_order_acquire))
				return false;
			return InstalledHookCallsiteMatches(kRVA_DeferredPrepassDepthClearCall,
				g_prepassDepthHookPatchedBytes, kDeferredPrepassDepthClearCallBytes) &&
				InstalledHookCallsiteMatches(kRVA_Interface3DDrawModelAccumulateSceneCall,
					g_drawModelAccumulateHookPatchedBytes,
					kDrawModelAccumulateSceneCallBytes) &&
				InstalledHookCallsiteMatches(kRVA_Interface3DDrawModelAmbientBlackCall,
					g_drawModelAmbientPreservationHookPatchedBytes,
					kDrawModelAmbientBlackCallBytes);
		}

		[[nodiscard]] bool SafeBSFixedStringLength(
			std::uintptr_t fixedStringAddress, std::uint32_t& length) noexcept
		{
			static_assert(sizeof(RE::BSFixedString) == sizeof(void*));
			static_assert(sizeof(RE::BSStringPool::Entry) == 0x18);
			length = 0;
			std::uintptr_t entry = 0;
			if (!SafeLoad(fixedStringAddress, entry))
				return false;
			if (entry == 0)
				return true;

			std::array<std::uintptr_t, kMaxBSFixedStringShallowDepth> visited{};
			for (std::size_t depth = 0; depth < visited.size(); ++depth) {
				if (std::find(visited.begin(), visited.begin() + depth, entry) !=
					visited.begin() + depth)
					return false;
				visited[depth] = entry;

				std::uint16_t flags = 0;
				if (!SafeLoad(entry + kBSStringPoolEntryFlagsOffset, flags))
					return false;
				if ((flags & RE::BSStringPool::Entry::kShallow) == 0)
					return SafeLoad(entry + kBSStringPoolEntryValueOffset, length);

				std::uintptr_t next = 0;
				if (!SafeLoad(entry + kBSStringPoolEntryValueOffset, next) || next == 0)
					return false;
				entry = next;
			}
			return false;
		}

		template <class T>
		[[nodiscard]] bool SafeWrite(std::uintptr_t address, const T& value) noexcept
		{
			static_assert(std::is_trivially_copyable_v<T>);
			return SafeStore(reinterpret_cast<void*>(address), std::addressof(value), sizeof(T));
		}

		[[nodiscard]] bool Sha256(std::span<const std::uint8_t> bytes,
			std::array<std::uint8_t, 32>& digest)
		{
			BCRYPT_ALG_HANDLE algorithm = nullptr;
			BCRYPT_HASH_HANDLE hash = nullptr;
			DWORD objectLength = 0;
			DWORD resultLength = 0;
			std::vector<std::uint8_t> object;
			bool success = false;

			if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(
					std::addressof(algorithm), BCRYPT_SHA256_ALGORITHM, nullptr, 0)))
				goto done;
			if (!BCRYPT_SUCCESS(BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
					reinterpret_cast<PUCHAR>(std::addressof(objectLength)), sizeof(objectLength),
					std::addressof(resultLength), 0)))
				goto done;
			object.resize(objectLength);
			if (!BCRYPT_SUCCESS(BCryptCreateHash(algorithm, std::addressof(hash), object.data(),
					static_cast<ULONG>(object.size()), nullptr, 0, 0)))
				goto done;
			if (bytes.size() > std::numeric_limits<ULONG>::max())
				goto done;
			if (!BCRYPT_SUCCESS(BCryptHashData(
					hash, const_cast<PUCHAR>(bytes.data()), static_cast<ULONG>(bytes.size()), 0)))
				goto done;
			if (!BCRYPT_SUCCESS(BCryptFinishHash(hash, digest.data(),
					static_cast<ULONG>(digest.size()), 0)))
				goto done;
			success = true;

done:
			if (hash)
				BCryptDestroyHash(hash);
			if (algorithm)
				BCryptCloseAlgorithmProvider(algorithm, 0);
			return success;
		}

		[[nodiscard]] bool MatchExecutableFileHash()
		{
			const int cached = g_executableHashState.load(std::memory_order_acquire);
			if (cached != 0)
				return cached > 0;

			std::array<wchar_t, 32768> path{};
			const DWORD pathLength = GetModuleFileNameW(nullptr, path.data(),
				static_cast<DWORD>(path.size()));
			if (pathLength == 0 || pathLength >= path.size()) {
				g_executableHashState.store(-1, std::memory_order_release);
				return false;
			}

			HANDLE file = CreateFileW(path.data(), GENERIC_READ,
				FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
				FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
			if (file == INVALID_HANDLE_VALUE) {
				g_executableHashState.store(-1, std::memory_order_release);
				return false;
			}

			BCRYPT_ALG_HANDLE algorithm = nullptr;
			BCRYPT_HASH_HANDLE hash = nullptr;
			DWORD objectLength = 0;
			DWORD resultLength = 0;
			std::vector<std::uint8_t> object;
			std::vector<std::uint8_t> buffer(1024 * 1024);
			std::array<std::uint8_t, 32> digest{};
			bool success = false;
			if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(
					std::addressof(algorithm), BCRYPT_SHA256_ALGORITHM, nullptr, 0)))
				goto done;
			if (!BCRYPT_SUCCESS(BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
					reinterpret_cast<PUCHAR>(std::addressof(objectLength)), sizeof(objectLength),
					std::addressof(resultLength), 0)))
				goto done;
			object.resize(objectLength);
			if (!BCRYPT_SUCCESS(BCryptCreateHash(algorithm, std::addressof(hash), object.data(),
					static_cast<ULONG>(object.size()), nullptr, 0, 0)))
				goto done;
			for (;;) {
				DWORD bytesRead = 0;
				if (!ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()),
						std::addressof(bytesRead), nullptr))
					goto done;
				if (bytesRead == 0)
					break;
				if (!BCRYPT_SUCCESS(BCryptHashData(hash, buffer.data(), bytesRead, 0)))
					goto done;
			}
			if (!BCRYPT_SUCCESS(BCryptFinishHash(hash, digest.data(),
					static_cast<ULONG>(digest.size()), 0)))
				goto done;
			if (ReflectionRuntime::IsPort240()) {
				const auto* expected = ReflectionRuntime::Port240ExecutableSha256();
				success = std::equal(digest.begin(), digest.end(), expected);
			} else {
				success = digest == kContract.executableSha256;
			}

done:
			if (hash)
				BCryptDestroyHash(hash);
			if (algorithm)
				BCryptCloseAlgorithmProvider(algorithm, 0);
			CloseHandle(file);
			g_executableHashState.store(success ? 1 : -1, std::memory_order_release);
			return success;
		}

		[[nodiscard]] bool MatchBytes(std::uintptr_t rva, std::span<const std::uint8_t> expected)
		{
			std::vector<std::uint8_t> actual(expected.size());
			if (!SafeCopy(actual.data(), reinterpret_cast<const void*>(Address(rva)), actual.size()))
				return false;
			if (ReflectionRuntime::IsPort240()) {
				
				std::vector<std::uint8_t> ported(expected.size());
				return ReflectionRuntime::ReadPrologueBytes(rva, ported.data(), ported.size()) &&
				       std::equal(actual.begin(), actual.end(), ported.begin(), ported.end());
			}
			return std::equal(actual.begin(), actual.end(), expected.begin(), expected.end());
		}

		[[nodiscard]] bool MatchRangeHash(const FrozenRange& range)
		{
			if (ReflectionRuntime::IsPort240()) {

				const auto* mapped = ReflectionRuntime::Range240(range.beginRVA);
				if (!mapped || mapped->ngEndInclusive < mapped->ngBegin)
					return false;
				const auto mappedSize = static_cast<std::size_t>(mapped->ngEndInclusive - mapped->ngBegin + 1);
				std::vector<std::uint8_t> mappedBytes(mappedSize);
				if (!SafeCopy(mappedBytes.data(),
						reinterpret_cast<const void*>(REL::Module::get().base() + mapped->ngBegin), mappedSize))
					return false;
				std::array<std::uint8_t, 32> mappedDigest{};
				return Sha256(mappedBytes, mappedDigest) &&
				       std::equal(mappedDigest.begin(), mappedDigest.end(), mapped->sha256);
			}
			if (range.endRVAInclusive < range.beginRVA)
				return false;
			const auto size = static_cast<std::size_t>(range.endRVAInclusive - range.beginRVA + 1);
			std::vector<std::uint8_t> bytes(size);
			if (!SafeCopy(bytes.data(), reinterpret_cast<const void*>(Address(range.beginRVA)), size))
				return false;
			std::array<std::uint8_t, 32> digest{};
			return Sha256(bytes, digest) && digest == range.sha256;
		}

		[[nodiscard]] bool AttestHelperPrologues()
		{
			std::uint32_t failures = 0;
			auto check = [&failures](std::uint32_t bit, bool matched) {
				if (!matched)
					failures |= 1u << bit;
			};
			check(0, MatchBytes(kRVA_SetCurrentRenderTarget, kSetCurrentRTPrologue));
			check(1, MatchBytes(kRVA_SetCurrentDepthStencil, kSetCurrentDSPrologue));
			check(2, MatchBytes(kRVA_SetViewportDefault, kSetViewportDefaultPrologue));
			check(3, MatchBytes(kRVA_RendererFlush, kRendererFlushPrologue));
			check(4, MatchBytes(kRVA_ClearRenderPasses, kClearRenderPassesPrologue));
			check(5, MatchBytes(kRVA_SetCurrentAccumulator, kSetCurrentAccumulatorPrologue));
			check(6, MatchBytes(kRVA_DistantRendererSingleton, kDistantRendererSingletonPrologue));

			check(7, MatchBytes(kRVA_CacheCameraData, kCacheCameraDataPrologue));
			check(8, MatchBytes(kRVA_CameraFlushScalar, kFloatOne));
			if (failures != 0)
				spdlog::error("[FlatDeferredPlayerCapture] helper attestation failures=0x{:08X}", failures);
			return failures == 0;
		}

		[[nodiscard]] bool AttestFrozenCodeContract()
		{
			const int cached = g_frozenCodeContractState.load(std::memory_order_acquire);
			if (cached != 0)
				return cached > 0;
			const bool interface3DRenderMain = MatchRangeHash(kContract.interface3DRenderMain);
			const bool interface3DDrawModelForward =
				MatchRangeHash(kContract.interface3DDrawModelForward);
			const bool flattenedEntryLambda =
				MatchRangeHash(kContract.interface3DFlattenedEntryLambda);
			const bool updateLights = MatchRangeHash(kContract.interface3DUpdateLights);
			const bool processQueuedLights = MatchRangeHash(kContract.processQueuedLights);
			const bool setSunLight = MatchRangeHash(kContract.setSunLight);
			const bool cullerConstructor = MatchRangeHash(kContract.cullingProcessConstructor);
			const bool cullerSetAccumulator =
				MatchRangeHash(kContract.cullingProcessSetAccumulator);
			const bool cullerDestructor = MatchRangeHash(kContract.cullingProcessDestructor);
			const bool niAVObjectUpdate = MatchRangeHash(kContract.niAVObjectUpdate);
			const bool niNodeAttachChild = MatchRangeHash(kContract.niNodeAttachChild);
			const bool niNodeDetachChild = MatchRangeHash(kContract.niNodeDetachChild);
			const bool acquireDepthStencil = MatchRangeHash(kContract.acquireDepthStencil);
			const bool acquireDepthStencilTarget =
				MatchRangeHash(kContract.acquireDepthStencilTarget);
			const bool releaseDepthStencil = MatchRangeHash(kContract.releaseDepthStencil);
			const bool setCurrentDepthStencil =
				MatchRangeHash(kContract.setCurrentDepthStencil);
			const bool helperPrologues = AttestHelperPrologues();
			const bool attested = interface3DRenderMain && interface3DDrawModelForward &&
				flattenedEntryLambda && updateLights && processQueuedLights && setSunLight &&
				cullerConstructor && cullerSetAccumulator && cullerDestructor && niAVObjectUpdate &&
				niNodeAttachChild && niNodeDetachChild && acquireDepthStencil &&
				acquireDepthStencilTarget && releaseDepthStencil && setCurrentDepthStencil &&
				helperPrologues;
			if (!attested) {
				spdlog::error(
					"[FlatDeferredPlayerCapture] forward contract mismatch: renderMain={} "
					"drawModelForward={} flattenedLambda={} updateLights={} processLights={} setSun={} "
					"culler={}/{}/{} update={} attach={} detach={} acquireDepth={}/{} releaseDepth={} setDepth={} helperPrologues={}",
					interface3DRenderMain, interface3DDrawModelForward, flattenedEntryLambda,
					updateLights, processQueuedLights, setSunLight, cullerConstructor,
					cullerSetAccumulator, cullerDestructor, niAVObjectUpdate,
					niNodeAttachChild, niNodeDetachChild,
					acquireDepthStencil, acquireDepthStencilTarget, releaseDepthStencil,
					setCurrentDepthStencil,
					helperPrologues);
			}
			g_frozenCodeContractState.store(attested ? 1 : -1, std::memory_order_release);
			return attested;
		}

		template <class T, std::size_t N>
		void AttachArray(std::array<ComPtr<T>, N>& destination, std::array<T*, N>& source) noexcept
		{
			for (std::size_t index = 0; index < N; ++index)
				destination[index].Attach(source[index]);
		}

		template <class T, std::size_t N>
		[[nodiscard]] bool EqualComArray(
			const std::array<ComPtr<T>, N>& lhs, const std::array<ComPtr<T>, N>& rhs) noexcept
		{
			for (std::size_t index = 0; index < N; ++index) {
				if (lhs[index].Get() != rhs[index].Get())
					return false;
			}
			return true;
		}

		template <class Shader>
		struct ShaderStageState
		{
			ComPtr<Shader> shader;
			std::array<ComPtr<ID3D11ClassInstance>, kMaxClassInstances> classInstances{};
			UINT classInstanceCount{};
			std::array<ComPtr<ID3D11Buffer>, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT> constantBuffers{};
			std::array<UINT, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT> firstConstants{};
			std::array<UINT, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT> numConstants{};
			std::array<ComPtr<ID3D11ShaderResourceView>, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT> resources{};
			std::array<ComPtr<ID3D11SamplerState>, D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT> samplers{};
		};

		struct PipelineState
		{
			ShaderStageState<ID3D11VertexShader> vs{};
			ShaderStageState<ID3D11HullShader> hs{};
			ShaderStageState<ID3D11DomainShader> ds{};
			ShaderStageState<ID3D11GeometryShader> gs{};
			ShaderStageState<ID3D11PixelShader> ps{};
			ShaderStageState<ID3D11ComputeShader> cs{};

			ComPtr<ID3D11InputLayout> inputLayout;
			D3D11_PRIMITIVE_TOPOLOGY topology{ D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED };
			std::array<ComPtr<ID3D11Buffer>, D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT> vertexBuffers{};
			std::array<UINT, D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT> vertexStrides{};
			std::array<UINT, D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT> vertexOffsets{};
			ComPtr<ID3D11Buffer> indexBuffer;
			DXGI_FORMAT indexFormat{ DXGI_FORMAT_UNKNOWN };
			UINT indexOffset{};

			std::array<ComPtr<ID3D11RenderTargetView>, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> renderTargets{};
			ComPtr<ID3D11DepthStencilView> depthView;
			ComPtr<ID3D11BlendState> blendState;
			std::array<FLOAT, 4> blendFactor{};
			UINT sampleMask{};
			ComPtr<ID3D11DepthStencilState> depthState;
			UINT stencilRef{};

			ComPtr<ID3D11RasterizerState> rasterizerState;
			std::array<D3D11_VIEWPORT, D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> viewports{};
			UINT viewportCount{};
			std::array<D3D11_RECT, D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> scissors{};
			UINT scissorCount{};

			ComPtr<ID3D11Predicate> predicate;
			BOOL predicateValue{};
			bool usesContext1{};
			bool captured{};

			[[nodiscard]] bool Capture(ID3D11DeviceContext* context) noexcept;
			void Restore(ID3D11DeviceContext* context) const noexcept;
			[[nodiscard]] bool Equivalent(const PipelineState& other) const noexcept;
		};

#define FD_CAPTURE_STAGE(PREFIX, FIELD, TYPE)                                                            \
	do {                                                                                                 \
		std::array<ID3D11ClassInstance*, kMaxClassInstances> classes{};                                  \
		FIELD.classInstanceCount = static_cast<UINT>(classes.size());                                    \
		TYPE* shader = nullptr;                                                                          \
		context->PREFIX##GetShader(std::addressof(shader), classes.data(),                               \
			std::addressof(FIELD.classInstanceCount));                                                   \
		FIELD.shader.Attach(shader);                                                                     \
		AttachArray(FIELD.classInstances, classes);                                                      \
		std::array<ID3D11Buffer*, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT> buffers{};          \
		context->PREFIX##GetConstantBuffers(0, static_cast<UINT>(buffers.size()), buffers.data());       \
		AttachArray(FIELD.constantBuffers, buffers);                                                     \
		std::array<ID3D11ShaderResourceView*, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT> resources{}; \
		context->PREFIX##GetShaderResources(0, static_cast<UINT>(resources.size()), resources.data());   \
		AttachArray(FIELD.resources, resources);                                                         \
		std::array<ID3D11SamplerState*, D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT> samplers{};               \
		context->PREFIX##GetSamplers(0, static_cast<UINT>(samplers.size()), samplers.data());            \
		AttachArray(FIELD.samplers, samplers);                                                           \
	} while (false)

#define FD_CAPTURE_CB1(PREFIX, FIELD)                                                               \
	do {                                                                                            \
		std::array<ID3D11Buffer*, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT> buffers{};     \
		context1->PREFIX##GetConstantBuffers1(0, static_cast<UINT>(buffers.size()), buffers.data(), \
			FIELD.firstConstants.data(), FIELD.numConstants.data());                                \
		for (std::size_t index = 0; index < buffers.size(); ++index) {                              \
			if (buffers[index] != FIELD.constantBuffers[index].Get()) {                             \
				if (buffers[index])                                                                 \
					buffers[index]->Release();                                                      \
				return false;                                                                       \
			}                                                                                       \
			if (buffers[index])                                                                     \
				buffers[index]->Release();                                                          \
		}                                                                                           \
	} while (false)

		bool PipelineState::Capture(ID3D11DeviceContext* context) noexcept
		{
			if (!context)
				return false;

			std::array<ID3D11UnorderedAccessView*, kMaxUAVSlots> computeUAVs{};
			context->CSGetUnorderedAccessViews(0, static_cast<UINT>(computeUAVs.size()), computeUAVs.data());
			bool unsupported = false;
			for (auto* view : computeUAVs) {
				unsupported |= view != nullptr;
				if (view)
					view->Release();
			}

			std::array<ID3D11UnorderedAccessView*, kMaxUAVSlots> outputUAVs{};
			context->OMGetRenderTargetsAndUnorderedAccessViews(
				0, nullptr, nullptr, 0, static_cast<UINT>(outputUAVs.size()), outputUAVs.data());
			for (auto* view : outputUAVs) {
				unsupported |= view != nullptr;
				if (view)
					view->Release();
			}

			std::array<ID3D11Buffer*, D3D11_SO_BUFFER_SLOT_COUNT> streamOutput{};
			context->SOGetTargets(static_cast<UINT>(streamOutput.size()), streamOutput.data());
			for (auto* buffer : streamOutput) {
				unsupported |= buffer != nullptr;
				if (buffer)
					buffer->Release();
			}
			if (unsupported)
				return false;

			FD_CAPTURE_STAGE(VS, vs, ID3D11VertexShader);
			FD_CAPTURE_STAGE(HS, hs, ID3D11HullShader);
			FD_CAPTURE_STAGE(DS, ds, ID3D11DomainShader);
			FD_CAPTURE_STAGE(GS, gs, ID3D11GeometryShader);
			FD_CAPTURE_STAGE(PS, ps, ID3D11PixelShader);
			FD_CAPTURE_STAGE(CS, cs, ID3D11ComputeShader);

			ComPtr<ID3D11DeviceContext1> context1;
			if (SUCCEEDED(context->QueryInterface(IID_PPV_ARGS(context1.GetAddressOf()))) && context1) {
				usesContext1 = true;
				FD_CAPTURE_CB1(VS, vs);
				FD_CAPTURE_CB1(HS, hs);
				FD_CAPTURE_CB1(DS, ds);
				FD_CAPTURE_CB1(GS, gs);
				FD_CAPTURE_CB1(PS, ps);
				FD_CAPTURE_CB1(CS, cs);
			}

			ID3D11InputLayout* rawInputLayout = nullptr;
			context->IAGetInputLayout(std::addressof(rawInputLayout));
			inputLayout.Attach(rawInputLayout);
			context->IAGetPrimitiveTopology(std::addressof(topology));
			std::array<ID3D11Buffer*, D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT> rawVertexBuffers{};
			context->IAGetVertexBuffers(0, static_cast<UINT>(rawVertexBuffers.size()), rawVertexBuffers.data(),
				vertexStrides.data(), vertexOffsets.data());
			AttachArray(vertexBuffers, rawVertexBuffers);
			ID3D11Buffer* rawIndexBuffer = nullptr;
			context->IAGetIndexBuffer(std::addressof(rawIndexBuffer), std::addressof(indexFormat),
				std::addressof(indexOffset));
			indexBuffer.Attach(rawIndexBuffer);

			std::array<ID3D11RenderTargetView*, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> rawTargets{};
			ID3D11DepthStencilView* rawDepth = nullptr;
			context->OMGetRenderTargets(static_cast<UINT>(rawTargets.size()), rawTargets.data(),
				std::addressof(rawDepth));
			AttachArray(renderTargets, rawTargets);
			depthView.Attach(rawDepth);
			ID3D11BlendState* rawBlend = nullptr;
			context->OMGetBlendState(std::addressof(rawBlend), blendFactor.data(), std::addressof(sampleMask));
			blendState.Attach(rawBlend);
			ID3D11DepthStencilState* rawDepthState = nullptr;
			context->OMGetDepthStencilState(std::addressof(rawDepthState), std::addressof(stencilRef));
			depthState.Attach(rawDepthState);

			ID3D11RasterizerState* rawRasterizer = nullptr;
			context->RSGetState(std::addressof(rawRasterizer));
			rasterizerState.Attach(rawRasterizer);
			viewportCount = static_cast<UINT>(viewports.size());
			context->RSGetViewports(std::addressof(viewportCount), viewports.data());
			scissorCount = static_cast<UINT>(scissors.size());
			context->RSGetScissorRects(std::addressof(scissorCount), scissors.data());

			ID3D11Predicate* rawPredicate = nullptr;
			context->GetPredication(std::addressof(rawPredicate), std::addressof(predicateValue));
			predicate.Attach(rawPredicate);
			captured = true;
			return true;
		}

#define FD_RESTORE_STAGE(PREFIX, FIELD)                                                                  \
	do {                                                                                                 \
		std::array<ID3D11ClassInstance*, kMaxClassInstances> classes{};                                  \
		for (std::size_t index = 0; index < classes.size(); ++index)                                     \
			classes[index] = FIELD.classInstances[index].Get();                                          \
		context->PREFIX##SetShader(FIELD.shader.Get(), classes.data(), FIELD.classInstanceCount);        \
		std::array<ID3D11ShaderResourceView*, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT> resources{}; \
		for (std::size_t index = 0; index < resources.size(); ++index)                                   \
			resources[index] = FIELD.resources[index].Get();                                             \
		context->PREFIX##SetShaderResources(0, static_cast<UINT>(resources.size()), resources.data());   \
		std::array<ID3D11SamplerState*, D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT> samplers{};               \
		for (std::size_t index = 0; index < samplers.size(); ++index)                                    \
			samplers[index] = FIELD.samplers[index].Get();                                               \
		context->PREFIX##SetSamplers(0, static_cast<UINT>(samplers.size()), samplers.data());            \
	} while (false)

#define FD_RESTORE_CB(PREFIX, FIELD)                                                                    \
	do {                                                                                                \
		std::array<ID3D11Buffer*, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT> buffers{};         \
		for (std::size_t index = 0; index < buffers.size(); ++index)                                    \
			buffers[index] = FIELD.constantBuffers[index].Get();                                        \
		if (context1)                                                                                   \
			context1->PREFIX##SetConstantBuffers1(0, static_cast<UINT>(buffers.size()), buffers.data(), \
				FIELD.firstConstants.data(), FIELD.numConstants.data());                                \
		else                                                                                            \
			context->PREFIX##SetConstantBuffers(0, static_cast<UINT>(buffers.size()), buffers.data());  \
	} while (false)

		void PipelineState::Restore(ID3D11DeviceContext* context) const noexcept
		{
			if (!context || !captured)
				return;

			ComPtr<ID3D11DeviceContext1> context1;
			if (usesContext1 && FAILED(context->QueryInterface(IID_PPV_ARGS(context1.GetAddressOf()))))
				return;

			std::array<ID3D11UnorderedAccessView*, kMaxUAVSlots> nullUAVs{};
			context->CSSetUnorderedAccessViews(
				0, static_cast<UINT>(nullUAVs.size()), nullUAVs.data(), nullptr);
			context->OMSetRenderTargetsAndUnorderedAccessViews(
				0, nullptr, nullptr, 0, static_cast<UINT>(nullUAVs.size()), nullUAVs.data(), nullptr);
			std::array<ID3D11Buffer*, D3D11_SO_BUFFER_SLOT_COUNT> nullStreamOutput{};
			std::array<UINT, D3D11_SO_BUFFER_SLOT_COUNT> nullStreamOffsets{};
			context->SOSetTargets(static_cast<UINT>(nullStreamOutput.size()),
				nullStreamOutput.data(), nullStreamOffsets.data());

			FD_RESTORE_STAGE(VS, vs);
			FD_RESTORE_STAGE(HS, hs);
			FD_RESTORE_STAGE(DS, ds);
			FD_RESTORE_STAGE(GS, gs);
			FD_RESTORE_STAGE(PS, ps);
			FD_RESTORE_STAGE(CS, cs);
			FD_RESTORE_CB(VS, vs);
			FD_RESTORE_CB(HS, hs);
			FD_RESTORE_CB(DS, ds);
			FD_RESTORE_CB(GS, gs);
			FD_RESTORE_CB(PS, ps);
			FD_RESTORE_CB(CS, cs);

			std::array<ID3D11Buffer*, D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT> rawVertexBuffers{};
			for (std::size_t index = 0; index < rawVertexBuffers.size(); ++index)
				rawVertexBuffers[index] = vertexBuffers[index].Get();
			context->IASetInputLayout(inputLayout.Get());
			context->IASetPrimitiveTopology(topology);
			context->IASetVertexBuffers(0, static_cast<UINT>(rawVertexBuffers.size()), rawVertexBuffers.data(),
				vertexStrides.data(), vertexOffsets.data());
			context->IASetIndexBuffer(indexBuffer.Get(), indexFormat, indexOffset);

			std::array<ID3D11RenderTargetView*, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> rawTargets{};
			for (std::size_t index = 0; index < rawTargets.size(); ++index)
				rawTargets[index] = renderTargets[index].Get();
			context->OMSetRenderTargets(static_cast<UINT>(rawTargets.size()), rawTargets.data(), depthView.Get());
			context->OMSetBlendState(blendState.Get(), blendFactor.data(), sampleMask);
			context->OMSetDepthStencilState(depthState.Get(), stencilRef);
			context->RSSetState(rasterizerState.Get());
			context->RSSetViewports(viewportCount, viewports.data());
			context->RSSetScissorRects(scissorCount, scissors.data());
			context->SetPredication(predicate.Get(), predicateValue);
		}

		template <class Shader>
		[[nodiscard]] bool EqualStage(
			const ShaderStageState<Shader>& lhs, const ShaderStageState<Shader>& rhs) noexcept
		{
			return lhs.shader.Get() == rhs.shader.Get() &&
			       lhs.classInstanceCount == rhs.classInstanceCount &&
			       EqualComArray(lhs.classInstances, rhs.classInstances) &&
			       EqualComArray(lhs.constantBuffers, rhs.constantBuffers) &&
			       lhs.firstConstants == rhs.firstConstants && lhs.numConstants == rhs.numConstants &&
			       EqualComArray(lhs.resources, rhs.resources) && EqualComArray(lhs.samplers, rhs.samplers);
		}

		bool PipelineState::Equivalent(const PipelineState& other) const noexcept
		{
			return captured && other.captured && usesContext1 == other.usesContext1 &&
			       EqualStage(vs, other.vs) && EqualStage(hs, other.hs) && EqualStage(ds, other.ds) &&
			       EqualStage(gs, other.gs) && EqualStage(ps, other.ps) && EqualStage(cs, other.cs) &&
			       inputLayout.Get() == other.inputLayout.Get() && topology == other.topology &&
			       EqualComArray(vertexBuffers, other.vertexBuffers) && vertexStrides == other.vertexStrides &&
			       vertexOffsets == other.vertexOffsets && indexBuffer.Get() == other.indexBuffer.Get() &&
			       indexFormat == other.indexFormat && indexOffset == other.indexOffset &&
			       EqualComArray(renderTargets, other.renderTargets) && depthView.Get() == other.depthView.Get() &&
			       blendState.Get() == other.blendState.Get() && blendFactor == other.blendFactor &&
			       sampleMask == other.sampleMask && depthState.Get() == other.depthState.Get() &&
			       stencilRef == other.stencilRef && rasterizerState.Get() == other.rasterizerState.Get() &&
			       viewportCount == other.viewportCount && scissorCount == other.scissorCount &&
			       std::equal(viewports.begin(), viewports.begin() + viewportCount, other.viewports.begin(),
					   [](const D3D11_VIEWPORT& a, const D3D11_VIEWPORT& b) {
						   return std::memcmp(std::addressof(a), std::addressof(b), sizeof(a)) == 0;
					   }) &&
			       std::equal(scissors.begin(), scissors.begin() + scissorCount, other.scissors.begin(),
					   [](const D3D11_RECT& a, const D3D11_RECT& b) {
						   return std::memcmp(std::addressof(a), std::addressof(b), sizeof(a)) == 0;
					   }) &&
			       predicate.Get() == other.predicate.Get() && predicateValue == other.predicateValue;
		}

		[[nodiscard]] bool ResourceUsesDevice(ID3D11Resource* resource, ID3D11Device* device) noexcept
		{
			if (!resource || !device)
				return false;
			ComPtr<ID3D11Device> owner;
			resource->GetDevice(owner.GetAddressOf());
			return owner.Get() == device;
		}

		template <class View>
		[[nodiscard]] bool ViewUsesResource(View* view, ID3D11Resource* expected) noexcept
		{
			if (!view || !expected)
				return false;
			ComPtr<ID3D11Resource> resource;
			view->GetResource(resource.GetAddressOf());
			return resource.Get() == expected;
		}

		struct TextureBackup
		{
			ComPtr<ID3D11Texture2D> engineTexture;
			ComPtr<ID3D11Texture2D> backupTexture;
			ComPtr<ID3D11RenderTargetView> clearView;
		};

		struct BufferBackup
		{
			ComPtr<ID3D11Buffer> engineBuffer;
			ComPtr<ID3D11Buffer> backupBuffer;
			D3D11_BUFFER_DESC description{};
			bool dynamicRoundTrip{};
		};

		struct PooledTextureBackup
		{
			ComPtr<ID3D11Texture2D> backupTexture;
			D3D11_TEXTURE2D_DESC sourceDescription{};
			bool configured{};
		};

		struct PooledBufferBackup
		{
			ComPtr<ID3D11Buffer> backupBuffer;
			D3D11_BUFFER_DESC sourceDescription{};
			bool configured{};
		};

		template <class T, std::size_t Capacity>
		struct FixedCollection
		{
			std::array<T, Capacity> values{};
			std::size_t count{};

			[[nodiscard]] T* begin() noexcept { return values.data(); }
			[[nodiscard]] T* end() noexcept { return values.data() + count; }
			[[nodiscard]] const T* begin() const noexcept { return values.data(); }
			[[nodiscard]] const T* end() const noexcept { return values.data() + count; }
			[[nodiscard]] std::size_t size() const noexcept { return count; }
			[[nodiscard]] bool full() const noexcept { return count == Capacity; }
			[[nodiscard]] T& operator[](std::size_t index) noexcept { return values[index]; }
			[[nodiscard]] const T& operator[](std::size_t index) const noexcept { return values[index]; }

			[[nodiscard]] bool Push(T value) noexcept
			{
				if (full())
					return false;
				values[count++] = std::move(value);
				return true;
			}
		};

		struct PooledResultSlot
		{
			ComPtr<ID3D11Texture2D> color;
			ComPtr<ID3D11ShaderResourceView> colorView;
			ComPtr<ID3D11Texture2D> depth;
			ComPtr<ID3D11ShaderResourceView> depthView;
			D3D11_TEXTURE2D_DESC colorDescription{};
			D3D11_SHADER_RESOURCE_VIEW_DESC colorViewDescription{};
			D3D11_TEXTURE2D_DESC depthDescription{};
			D3D11_SHADER_RESOURCE_VIEW_DESC depthViewDescription{};
			bool configured{};
		};

		struct CoverageReductionResources
		{
			ComPtr<ID3D11ComputeShader> depthShader;
			ComPtr<ID3D11ComputeShader> mappedDepthShader;
			ComPtr<ID3D11ComputeShader> informativeColorShader;
			ComPtr<ID3D11ComputeShader> spatialVariationShader;
			ComPtr<ID3D11Buffer> counter;
			ComPtr<ID3D11UnorderedAccessView> counterView;
			ComPtr<ID3D11Buffer> readback;
			ComPtr<ID3D11Query> forwardOcclusion;
			ComPtr<ID3D11Query> forwardStatistics;
			
			ComPtr<ID3D11ComputeShader> nativeWorldAuthorityShader;
			ComPtr<ID3D11Buffer> nativeWorldCounters;
			ComPtr<ID3D11UnorderedAccessView> nativeWorldCounterView;
			ComPtr<ID3D11Buffer> nativeWorldReadback;
		};

		struct ResourcePool
		{
			ComPtr<ID3D11Device> device;
			std::uint64_t privateRendererIdentitySerial{};
			FixedCollection<PooledTextureBackup, kMaxTextureBackups> textureBackups{};
			FixedCollection<PooledBufferBackup, kMaxBufferBackups> bufferBackups{};
			std::array<PooledResultSlot, 2> resultSlots{};
			std::uint32_t committedResultSlot{ std::numeric_limits<std::uint32_t>::max() };
			
			std::uint32_t avoidNextResultSlot{ std::numeric_limits<std::uint32_t>::max() };
			CoverageReductionResources coverage{};

			void Reset(ID3D11Device* newDevice, std::uint64_t newIdentity) noexcept
			{
				*this = ResourcePool{};
				device = newDevice;
				privateRendererIdentitySerial = newIdentity;
			}

			[[nodiscard]] bool EnsureIdentity(ID3D11Device* expectedDevice,
				std::uint64_t expectedIdentity) noexcept
			{
				if (!expectedDevice || expectedIdentity == 0)
					return false;
				if (device.Get() != expectedDevice ||
					privateRendererIdentitySerial != expectedIdentity)
					Reset(expectedDevice, expectedIdentity);
				return device.Get() == expectedDevice &&
				       privateRendererIdentitySerial == expectedIdentity;
			}

			[[nodiscard]] bool MatchesIdentity(ID3D11Device* expectedDevice,
				std::uint64_t expectedIdentity) const noexcept
			{
				return expectedDevice && expectedIdentity != 0 &&
					device.Get() == expectedDevice &&
					privateRendererIdentitySerial == expectedIdentity;
			}
		};

		auto& g_resourcePool = *new ResourcePool();

		auto& g_nativeWorldResourcePool = *new ResourcePool();

		struct PendingNativeWorldAuthority
		{
			NativeWorldResult result{};
			ComPtr<ID3D11Device> device;
			ComPtr<ID3D11DeviceContext> context;
			ResourcePool* pool{};
			std::uint64_t identity{};
			std::uint64_t colorPixelCount{};
			std::uint64_t depthPixelCount{};
			std::uint32_t resultSlot{ std::numeric_limits<std::uint32_t>::max() };
			std::uint32_t renderThreadId{};
			bool valid{};
		};

		auto& g_pendingNativeWorldAuthority = *new PendingNativeWorldAuthority();
		std::atomic<std::uint64_t> g_pendingNativeWorldSerial{ 1 };

		struct ColorBindingIdentity
		{
			std::uint32_t logical{};
			std::uint32_t physical{};
			ComPtr<ID3D11Texture2D> texture;
			ComPtr<ID3D11RenderTargetView> renderView;
		};

		struct ContextBufferIdentity
		{
			ID3D11Buffer** slot{};
			ComPtr<ID3D11Buffer> buffer;
			
			std::uint32_t tag{};

			bool rotating{};
		};

		struct ContextConstantGroupIdentity
		{
			RE::BSGraphics::ConstantGroup* slot{};
			std::array<std::byte, sizeof(RE::BSGraphics::ConstantGroup)> pod{};
			std::uint32_t tag{};
		};

		struct PrepassDrawDiag
		{
			static constexpr std::uint32_t kMaxDraws = 8;
			struct Draw
			{
				ComPtr<ID3D11Query> occlusion;
				ComPtr<ID3D11Query> statistics;
				bool began{};
				bool ended{};
				char geometryName[48]{};
				float worldPosition[3]{};
				float boundRadius{};
				std::uint32_t technique{};
				std::array<std::uintptr_t, 4> callerRVAs{};
				BOOL depthEnable{ -1 };
				D3D11_DEPTH_WRITE_MASK depthWriteMask{ D3D11_DEPTH_WRITE_MASK_ZERO };
				D3D11_COMPARISON_FUNC depthFunc{ D3D11_COMPARISON_NEVER };
				std::uint32_t dsvFlags{ 0xFFFFFFFFu };
				bool boundDepthIsLogical{};
				bool stateCaptured{};
				BOOL scissorEnable{ -1 };
				std::uint32_t cullMode{};
				BOOL frontCounterClockwise{ -1 };

				std::uint32_t commitsAtSetup{};
				std::uint32_t commitsAtRestore{};
				D3D11_VIEWPORT viewport{};
				std::uint64_t occlusionSamples{};
				D3D11_QUERY_DATA_PIPELINE_STATISTICS statisticsData{};
				bool occlusionValid{};
				bool statisticsValid{};
			};
			std::array<Draw, kMaxDraws> draws{};
			std::uint32_t drawsSeen{};
			
			std::uint32_t commitsSeen{};
		};

		enum class MappingNextState : std::uint32_t
		{
			kRestoredToPre = 1,
			kPersistedPost,
			kAdvancedAgain,
			kManagerChanged
		};

		struct PendingMappingTransition
		{
			RE::BSGraphics::RenderTargetManager* manager{};
			std::uint64_t frameSerial{};
			std::uint64_t sequence{};
			std::uint32_t kind{};
			std::uint32_t logical{};
			std::uint32_t pre{};
			std::uint32_t post{};
		};

		PendingMappingTransition g_pendingMappingTransition{};
		std::atomic<std::uint64_t> g_mappingTransitionSequence{ 1 };
		std::atomic<std::uint32_t> g_mappingTransitionLogs{ 0 };

		[[nodiscard]] MappingNextState ClassifyMappingNextState(
			bool sameManager,
			std::uint32_t pre,
			std::uint32_t post,
			std::uint32_t next) noexcept
		{
			if (!sameManager)
				return MappingNextState::kManagerChanged;
			if (next == pre)
				return MappingNextState::kRestoredToPre;
			if (next == post)
				return MappingNextState::kPersistedPost;
			return MappingNextState::kAdvancedAgain;
		}

		void RecordMappingTransition(
			RE::BSGraphics::RenderTargetManager* manager,
			std::uint64_t frameSerial,
			std::uint32_t kind,
			std::uint32_t logical,
			std::uint32_t pre,
			std::uint32_t post) noexcept
		{
			if (!manager || (kind != 2u && kind != 3u) || pre == post)
				return;
			const auto sequence = g_mappingTransitionSequence.fetch_add(1, std::memory_order_relaxed);
			g_pendingMappingTransition = {
				manager, frameSerial, sequence ? sequence : 1u, kind, logical, pre, post
			};
		}

		void ObservePendingMappingTransition(
			RE::BSGraphics::RenderTargetManager* manager,
			std::uint64_t nextFrameSerial,
			const std::array<std::uint32_t, kRenderTargetMappingCount>& nextColor,
			const std::array<std::uint32_t, kDepthTargetMappingCount>& nextDepth) noexcept
		{
			const auto pending = g_pendingMappingTransition;
			if (!pending.manager)
				return;
			std::uint32_t next = ~0u;
			if (pending.kind == 2u && pending.logical < nextColor.size())
				next = nextColor[pending.logical];
			else if (pending.kind == 3u && pending.logical < nextDepth.size())
				next = nextDepth[pending.logical];
			else {
				g_pendingMappingTransition = {};
				return;
			}
			const bool sameManager = pending.manager == manager;
			const auto state = ClassifyMappingNextState(
				sameManager, pending.pre, pending.post, next);
			g_pendingMappingTransition = {};
			const auto logOrdinal = g_mappingTransitionLogs.fetch_add(1, std::memory_order_relaxed) + 1u;
			if (logOrdinal > 12u && (logOrdinal % 240u) != 0u)
				return;
			const char* stateName = "advanced-again";
			switch (state) {
			case MappingNextState::kRestoredToPre:
				stateName = "restored-to-pre";
				break;
			case MappingNextState::kPersistedPost:
				stateName = "persisted-post";
				break;
			case MappingNextState::kManagerChanged:
				stateName = "manager-changed";
				break;
			default:
				break;
			}
			try {
				spdlog::warn(
					"[FlatDeferredPlayerCapture] logical mapping transition seq={} kind={} logical={} "
					"pre={} post={} next={} nextState={} transitionFrame={} nextFrame={} "
					"entryManager={:p} nextManager={:p}",
					pending.sequence, pending.kind, pending.logical, pending.pre, pending.post, next,
					stateName, pending.frameSerial, nextFrameSerial,
					static_cast<void*>(pending.manager), static_cast<void*>(manager));
			} catch (...) {
			}
		}

		struct ResourceTransaction
		{
			ID3D11Device* device{};
			ID3D11DeviceContext* context{};
			RE::BSGraphics::RendererData* rendererData{};
			RE::BSGraphics::RenderTargetManager* targetManager{};
			std::array<std::uint32_t, kRenderTargetMappingCount> colorMappings{};
			std::array<std::uint32_t, kDepthTargetMappingCount> depthMappings{};
			ResourcePool* pool{};
			FixedCollection<TextureBackup, kMaxTextureBackups> textures{};
			FixedCollection<BufferBackup, kMaxBufferBackups> buffers{};
			FixedCollection<ColorBindingIdentity, kMaxColorBindings> colorBindings{};
			FixedCollection<ContextBufferIdentity, kMaxContextBufferIdentities> contextBufferIdentities{};
			FixedCollection<ContextConstantGroupIdentity, kContextConstantGroupCount>
				contextConstantGroups{};

			ComPtr<ID3D11Texture2D> sourceColor;
			ComPtr<ID3D11ShaderResourceView> sourceColorView;
			ComPtr<ID3D11Texture2D> sourceDepth;
			ComPtr<ID3D11ShaderResourceView> sourceDepthView;
			ComPtr<ID3D11DepthStencilView> writableDepthView;
			ComPtr<ID3D11RenderTargetView> sourceColorRTV;
			ComPtr<ID3D11Texture2D> resultColor;
			ComPtr<ID3D11ShaderResourceView> resultColorView;
			ComPtr<ID3D11Texture2D> resultDepth;
			ComPtr<ID3D11ShaderResourceView> resultDepthView;
			std::uint32_t candidateResultSlot{ std::numeric_limits<std::uint32_t>::max() };
			D3D11_TEXTURE2D_DESC sourceColorDesc{};
			D3D11_TEXTURE2D_DESC sourceDepthDesc{};
			D3D11_SHADER_RESOURCE_VIEW_DESC sourceColorViewDesc{};
			D3D11_SHADER_RESOURCE_VIEW_DESC sourceDepthViewDesc{};
			D3D11_RENDER_TARGET_VIEW_DESC sourceColorRTVDesc{};
			D3D11_DEPTH_STENCIL_VIEW_DESC writableDepthViewDesc{};
			std::uint32_t colorPhysical{};
			std::uint32_t depthPhysical{};
			bool prepared{};
			bool mutated{};
			bool restoreCopiesIssued{};
			bool restoreMappingsStable{};
			bool dynamicBufferRoundTripsRestored{};
			bool contextConstantGroupsRestored{};
			std::uint32_t dynamicBufferBackupCount{};
			std::uint32_t dynamicAsyncRestoreCopyCount{};
			bool requireStableContextBufferIdentities{};
			bool prepassDepthCaptured{};
			bool finalForwardDepthCaptured{};
			bool forwardQueriesBegan{};
			bool forwardQueriesEnded{};
			bool forwardOcclusionQueryBegan{};
			bool forwardStatisticsQueryBegan{};
			bool forwardOcclusionQueryEnded{};
			bool forwardStatisticsQueryEnded{};
			bool forwardQueriesReadBack{};
			std::uint64_t forwardOcclusionSamples{};
			D3D11_QUERY_DATA_PIPELINE_STATISTICS forwardStatistics{};
			std::uint32_t poolResourcesCreated{};
			std::uint32_t mappingMismatchKind{};
			std::uint32_t mappingMismatchLogical{};
			std::uint32_t mappingMismatchExpected{};
			std::uint32_t mappingMismatchObserved{};
			std::uint64_t frameSerial{};
			std::array<void*, kMaxFlattenedGeometryEntries> forwardExpectedGeometries{};
			std::array<void*, kMaxFlattenedGeometryEntries> forwardExpectedShaderProperties{};
			std::array<std::uint32_t, kMaxFlattenedGeometryEntries> forwardSetupCounts{};
			std::array<std::uint32_t, kMaxFlattenedGeometryEntries> forwardRestoreCounts{};
			std::array<std::uint32_t, kMaxFlattenedGeometryEntries> forwardPinnedRestoreCounts{};
			std::array<std::uint32_t, kMaxFlattenedGeometryEntries> forwardPolicyCompletedCounts{};
			std::array<std::uint32_t, kMaxFlattenedGeometryEntries> forwardOrdinarySelectionCounts{};
			std::array<std::uint32_t, kMaxFlattenedGeometryEntries> forwardSpecialAlphaSelectionCounts{};
			std::array<std::uint32_t, kMaxFlattenedGeometryEntries> forwardNoPassSelectionCounts{};
			std::array<std::uint32_t, kMaxFlattenedGeometryEntries> forwardSpecialAlphaCompletionCounts{};
			std::array<std::uint32_t, kMaxFlattenedGeometryEntries> forwardSpecialAlphaPinnedCounts{};
			std::array<std::uint8_t, kMaxFlattenedGeometryEntries> forwardExpectedRequiredEyes{};
			std::uint32_t forwardExpectedGeometryCount{};
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
			std::uint32_t essentialVertexShaderRepairAttempts{};
			std::uint32_t essentialVertexShaderRepairAcceptances{};
			std::uint32_t essentialVertexShaderRepairRejections{};
			std::uint32_t essentialVertexShaderRepairOutcomeMismatches{};
			RE::BSRenderPass* forwardActivePass{};
			void* forwardActiveGeometry{};
			void* forwardActiveShaderProperty{};
			void* forwardSelectorAccumulator{};
			std::uint32_t forwardActiveExpectedIndex{
				(std::numeric_limits<std::uint32_t>::max)()
			};
			std::uint32_t forwardActiveSpecialAlphaIndex{
				(std::numeric_limits<std::uint32_t>::max)()
			};
			bool forwardActivePassArmed{};
			bool forwardActivePolicySeen{};
			bool forwardActivePolicyAllSucceeded{};
			bool forwardActiveCommitOutputsPinned{};
			bool forwardActivePrimeAttempted{};
			bool forwardActivePrimeSucceeded{};
			bool forwardActivePrimeOutputsRebound{};
			bool forwardActivePrimeRasterizerApplied{};
			bool reflectedHandednessRequired{};
			bool nativeWorldRasterizerPolicyArmed{};
			bool forwardGeometryProofArmed{};
			bool forwardGeometryCounterOverflow{};
			bool forwardSelectorObservationActive{};
			bool forwardSelectorSuffixCompleted{};
			bool forwardActiveSpecialAlphaArmed{};
			bool forwardActiveSpecialAlphaPolicySeen{};
			bool forwardActiveSpecialAlphaPolicyAllSucceeded{};
			bool forwardActiveSpecialAlphaOutputsPinned{};

			[[nodiscard]] bool Prepare(const Request& request, const PipelineState& pipeline,
				void* renderContext, bool mrt4Enabled, bool secondLightTargetEnabled,
				CompletionProof& proof);
			[[nodiscard]] bool PrepareNativeWorld(const NativeWorldRequest& request,
				const PipelineState& pipeline, void* renderContext, bool mrt4Enabled,
				bool secondLightTargetEnabled, bool stageBackupContents, bool allowPoolReset,
				NativeWorldCompletionProof& proof);
			void ClearForCapture() noexcept;
			[[nodiscard]] bool CopyResult(Result& result, std::uint64_t& coveredPixels,
				std::uint64_t& mappedDepthPixels, std::uint64_t& informativeColorPixels,
				std::uint64_t& spatiallyVariantColorPixels, bool& depthReadbackCompleted,
				bool& colorReadbackCompleted, std::uint32_t& copyStage) noexcept;
			[[nodiscard]] bool CopyNativeWorldResult(NativeWorldResult& result,
				std::uint64_t& coveredPixels, std::uint64_t& mappedDepthPixels,
				std::uint64_t& informativeColorPixels,
				std::uint64_t& spatiallyVariantColorPixels,
				bool& depthReadbackCompleted, bool& colorReadbackCompleted,
				std::uint32_t& copyStage) noexcept;
			void CommitResult() noexcept;
			[[nodiscard]] bool Restore() noexcept;
			[[nodiscard]] bool OutputResourcesStable() const noexcept;
			[[nodiscard]] bool MappingsStable() noexcept;
			void CapturePrepassDepth() noexcept;
			void CaptureFinalForwardDepth() noexcept;
			void ReadForwardQueries() noexcept;
			void BeginPrepassDrawDiag(RE::BSRenderPass* pass) noexcept;
			void EndPrepassDrawDiag() noexcept;
			void ReadPrepassDrawDiag() noexcept;
			[[nodiscard]] bool ArmForwardGeometryProof(
				void* const* geometries, void* const* shaderProperties,
				const std::uint8_t* selectorEligible, std::uint32_t rawCount,
				RE::NiAVObject* const* requiredEyeGeometries,
				std::uint32_t requiredEyeGeometryCount) noexcept;
			[[nodiscard]] bool BeginForwardSelectorObservation(void* accumulator) noexcept;
			[[nodiscard]] bool EndForwardSelectorObservation(bool suffixCompleted) noexcept;
			void ObserveForwardSelectorResult(const void* shaderProperty, const void* geometry,
				std::uint32_t renderMode, const void* accumulator, const void* result) noexcept;
			void CompleteActiveSpecialAlpha(bool suffixBoundaryCompleted) noexcept;
			[[nodiscard]] bool ForwardOutputsPinned() const noexcept;
			[[nodiscard]] bool RebindForwardOutputs() noexcept;
			[[nodiscard]] bool AttestNativeWorldTargetsAfterSetDirty() noexcept;
			[[nodiscard]] bool TryBeginOrdinaryForwardPass(RE::BSRenderPass* pass) noexcept;
			[[nodiscard]] bool PrimeOrdinaryForwardPassCommit(RE::BSRenderPass* pass) noexcept;
			[[nodiscard]] bool TrySubstituteEssentialForwardVertexShader(
				RE::BSShader* shader, std::uint32_t requestedVertexDescriptor,
				std::uint32_t hullDescriptor, std::uint32_t domainDescriptor,
				std::uint32_t pixelDescriptor, const void* outputStruct,
				std::uint32_t& nativeVertexDescriptor) noexcept;
			void NoteEssentialForwardVertexShaderResult(
				std::uint32_t requestedVertexDescriptor,
				std::uint32_t nativeVertexDescriptor,
				std::uint32_t pixelDescriptor, bool accepted) noexcept;
			void NoteForwardGeometrySetup(RE::BSRenderPass* pass) noexcept;
			void NoteForwardGeometryRestore(
				RE::BSRenderPass* pass, bool nativeCompleted = true, bool accepted = true) noexcept;
			void NoteReflectedRasterizerCommit(bool applied) noexcept;
			void NoteOrdinaryRasterizerCommit() noexcept;
			[[nodiscard]] bool AttestForwardGeometryProof(
				std::uint32_t& observedGeometryCount,
				std::uint32_t& ordinaryGeometryCount,
				std::uint32_t& specialAlphaGeometryCount,
				std::uint32_t& noPassGeometryCount,
				std::uint32_t& routedEyeGeometryCount) const noexcept;

			PrepassDrawDiag prepassDrawDiag{};
		};

		struct NativeWorldTexturePlanFact
		{
			std::uintptr_t texture{};
			D3D11_TEXTURE2D_DESC description{};
		};

		struct NativeWorldBufferPlanFact
		{
			std::uintptr_t buffer{};
			D3D11_BUFFER_DESC description{};
		};

		struct NativeWorldContextBufferPlanFact
		{
			std::uintptr_t slot{};
			std::uintptr_t buffer{};
			std::uint32_t tag{};
			bool rotating{};
		};

		struct NativeWorldContextConstantGroupPlanFact
		{
			std::uintptr_t slot{};
			std::array<std::byte, sizeof(RE::BSGraphics::ConstantGroup)> pod{};
			std::uint32_t tag{};
		};

		struct NativeWorldColorBindingPlanFact
		{
			std::uint32_t logical{};
			std::uint32_t physical{};
			std::uintptr_t texture{};
			std::uintptr_t renderView{};
		};

		struct NativeWorldResultSlotPlanFact
		{
			std::uintptr_t color{};
			std::uintptr_t colorView{};
			std::uintptr_t depth{};
			std::uintptr_t depthView{};
			D3D11_TEXTURE2D_DESC colorDescription{};
			D3D11_SHADER_RESOURCE_VIEW_DESC colorViewDescription{};
			D3D11_TEXTURE2D_DESC depthDescription{};
			D3D11_SHADER_RESOURCE_VIEW_DESC depthViewDescription{};
			bool configured{};
		};

		struct NativeWorldResourceGenerationPlan
		{
			static constexpr std::size_t kStageCount = 6;
			static constexpr std::size_t kStageConstantBufferCount =
				D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT;
			std::uint64_t identity{};
			std::uint64_t frameSerial{};
			std::uint32_t renderThreadId{};
			std::uintptr_t device{};
			std::uintptr_t context{};
			std::uintptr_t cameraConstantBuffer{};
			std::uintptr_t rendererData{};
			std::uintptr_t targetManager{};
			std::uintptr_t renderContext{};
			std::array<std::uint32_t, kRenderTargetMappingCount> colorMappings{};
			std::array<std::uint32_t, kDepthTargetMappingCount> depthMappings{};
			std::array<std::array<std::uintptr_t, kStageConstantBufferCount>, kStageCount>
				stageConstantBuffers{};
			std::array<NativeWorldTexturePlanFact, kMaxTextureBackups> textures{};
			std::array<NativeWorldBufferPlanFact, kMaxBufferBackups> buffers{};
			std::array<NativeWorldContextBufferPlanFact, kMaxContextBufferIdentities>
				contextBuffers{};
			std::array<NativeWorldContextConstantGroupPlanFact, kContextConstantGroupCount>
				contextConstantGroups{};
			std::array<NativeWorldColorBindingPlanFact, kMaxColorBindings> colorBindings{};
			std::array<NativeWorldResultSlotPlanFact, 2> resultSlots{};
			std::uint32_t textureCount{};
			std::uint32_t bufferCount{};
			std::uint32_t contextBufferCount{};
			std::uint32_t contextConstantGroupCount{};
			std::uint32_t colorBindingCount{};
			D3D11_TEXTURE2D_DESC sourceColorDescription{};
			D3D11_TEXTURE2D_DESC sourceDepthDescription{};
			D3D11_SHADER_RESOURCE_VIEW_DESC sourceColorViewDescription{};
			D3D11_SHADER_RESOURCE_VIEW_DESC sourceDepthViewDescription{};
			D3D11_RENDER_TARGET_VIEW_DESC sourceColorRTVDescription{};
			D3D11_DEPTH_STENCIL_VIEW_DESC writableDepthViewDescription{};
			std::uintptr_t sourceColor{};
			std::uintptr_t sourceColorView{};
			std::uintptr_t sourceColorRTV{};
			std::uintptr_t sourceDepth{};
			std::uintptr_t sourceDepthView{};
			std::uintptr_t writableDepthView{};
			bool mrt4Enabled{};
			bool secondLightTargetEnabled{};
			bool valid{};
		};

		auto& g_nativeWorldResourceGeneration =
			*new NativeWorldResourceGenerationPlan();
		std::atomic<std::uint64_t> g_nativeWorldResourceGenerationSerial{ 1 };

		thread_local ResourceTransaction* g_activeForwardTransaction = nullptr;

		thread_local ResourceTransaction* g_activeNativeWorldDepthTransaction = nullptr;

		thread_local ResourceTransaction* g_activeNativeWorldRasterizerTransaction = nullptr;
		thread_local std::uint32_t g_nativeWorldGeometryPassDepth = 0;
		thread_local std::uint32_t g_nativeWorldOrdinaryPassDepth = 0;

		struct ActiveForwardTransactionGuard
		{
			ResourceTransaction* transaction{};
			bool* restored{};
			bool armed{};

			ActiveForwardTransactionGuard(ResourceTransaction& requested, bool& restoredFlag) noexcept :
				transaction(std::addressof(requested)), restored(std::addressof(restoredFlag))
			{
				restoredFlag = false;
				if (!g_activeForwardTransaction) {
					g_activeForwardTransaction = transaction;
					armed = true;
				}
			}

			~ActiveForwardTransactionGuard() noexcept
			{
				if (!armed || !restored)
					return;
				*restored = g_activeForwardTransaction == transaction;
				g_activeForwardTransaction = nullptr;
			}

			ActiveForwardTransactionGuard(const ActiveForwardTransactionGuard&) = delete;
			ActiveForwardTransactionGuard& operator=(const ActiveForwardTransactionGuard&) = delete;
		};

		struct DeferredPrepassDepthClearHook
		{
			static void thunk() noexcept
			{
				func();
				if (g_activeForwardTransaction)
					g_activeForwardTransaction->CapturePrepassDepth();
				if (g_activeNativeWorldDepthTransaction)
					g_activeNativeWorldDepthTransaction->CapturePrepassDepth();
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		[[nodiscard]] bool AddTextureBackup(ResourceTransaction& transaction,
			ID3D11Texture2D* source, ID3D11RenderTargetView* clearView,
			bool stageContents = true)
		{
			if (!source || !transaction.device || !transaction.context || !transaction.pool ||
				!ResourceUsesDevice(source, transaction.device))
				return false;
			for (auto& existing : transaction.textures) {
				if (existing.engineTexture.Get() == source) {
					if (clearView && !existing.clearView)
						existing.clearView = clearView;
					return true;
				}
			}
			if (transaction.textures.full())
				return false;

			D3D11_TEXTURE2D_DESC description{};
			source->GetDesc(std::addressof(description));
			TextureBackup backup{};
			backup.engineTexture = source;
			backup.clearView = clearView;
			auto backupInUse = [&](ID3D11Texture2D* candidate) noexcept {
				return candidate && std::any_of(transaction.textures.begin(), transaction.textures.end(),
					[&](const TextureBackup& existing) {
						return existing.backupTexture.Get() == candidate;
					});
			};
			for (const auto& pooled : transaction.pool->textureBackups) {
				if (pooled.configured && pooled.backupTexture &&
					!backupInUse(pooled.backupTexture.Get()) &&
					ResourceUsesDevice(pooled.backupTexture.Get(), transaction.device) &&
					std::memcmp(std::addressof(pooled.sourceDescription), std::addressof(description),
						sizeof(description)) == 0) {
					backup.backupTexture = pooled.backupTexture;
					break;
				}
			}
			if (!backup.backupTexture) {
				D3D11_TEXTURE2D_DESC backupDescription = description;
				backupDescription.Usage = D3D11_USAGE_DEFAULT;
				backupDescription.BindFlags = 0;
				backupDescription.CPUAccessFlags = 0;
				backupDescription.MiscFlags = 0;
				if (FAILED(transaction.device->CreateTexture2D(std::addressof(backupDescription), nullptr,
						backup.backupTexture.GetAddressOf())) ||
					!backup.backupTexture)
					return false;
				PooledTextureBackup pooled{};
				pooled.backupTexture = backup.backupTexture;
				pooled.sourceDescription = description;
				pooled.configured = true;
				if (!transaction.pool->textureBackups.full()) {
					if (!transaction.pool->textureBackups.Push(std::move(pooled)))
						return false;
				} else {
					auto replace = std::find_if(transaction.pool->textureBackups.begin(),
						transaction.pool->textureBackups.end(), [&](const PooledTextureBackup& candidate) {
							return !backupInUse(candidate.backupTexture.Get());
						});
					if (replace == transaction.pool->textureBackups.end())
						return false;
					*replace = std::move(pooled);
				}
				++transaction.poolResourcesCreated;
			}
			if (stageContents)
				transaction.context->CopyResource(backup.backupTexture.Get(), source);
			return transaction.textures.Push(std::move(backup));
		}

		[[nodiscard]] bool AddBufferBackup(ResourceTransaction& transaction, ID3D11Buffer* source,
			bool stageContents = true)
		{
			if (!source || !transaction.device || !transaction.context || !transaction.pool ||
				!ResourceUsesDevice(source, transaction.device))
				return false;
			for (const auto& existing : transaction.buffers) {
				if (existing.engineBuffer.Get() == source)
					return true;
			}

			D3D11_BUFFER_DESC description{};
			source->GetDesc(std::addressof(description));
			
			if (description.Usage == D3D11_USAGE_IMMUTABLE)
				return true;
			if (transaction.buffers.full())
				return false;
			if ((description.Usage != D3D11_USAGE_DEFAULT && description.Usage != D3D11_USAGE_DYNAMIC) ||
				(description.Usage == D3D11_USAGE_DEFAULT && description.CPUAccessFlags != 0) ||
				(description.Usage == D3D11_USAGE_DYNAMIC &&
					(description.CPUAccessFlags & D3D11_CPU_ACCESS_WRITE) == 0))
				return false;
			const bool dynamic = description.Usage == D3D11_USAGE_DYNAMIC;

			BufferBackup backup{};
			backup.engineBuffer = source;
			backup.description = description;
			backup.dynamicRoundTrip = dynamic;
			auto backupInUse = [&](ID3D11Buffer* candidate) noexcept {
				return candidate && std::any_of(transaction.buffers.begin(), transaction.buffers.end(),
					[&](const BufferBackup& existing) {
						return existing.backupBuffer.Get() == candidate;
					});
			};
			for (const auto& pooled : transaction.pool->bufferBackups) {
				if (pooled.configured && pooled.backupBuffer &&
					!backupInUse(pooled.backupBuffer.Get()) &&
					ResourceUsesDevice(pooled.backupBuffer.Get(), transaction.device) &&
					std::memcmp(std::addressof(pooled.sourceDescription), std::addressof(description),
						sizeof(description)) == 0) {
					backup.backupBuffer = pooled.backupBuffer;
					break;
				}
			}
			if (!backup.backupBuffer) {
				D3D11_BUFFER_DESC backupDescription = description;
				backupDescription.Usage = dynamic ? D3D11_USAGE_STAGING : D3D11_USAGE_DEFAULT;
				backupDescription.BindFlags = 0;
				backupDescription.CPUAccessFlags = dynamic ? D3D11_CPU_ACCESS_READ : 0;
				backupDescription.MiscFlags = 0;
				backupDescription.StructureByteStride = 0;
				if (FAILED(transaction.device->CreateBuffer(std::addressof(backupDescription), nullptr,
						backup.backupBuffer.GetAddressOf())) ||
					!backup.backupBuffer)
					return false;
				PooledBufferBackup pooled{};
				pooled.backupBuffer = backup.backupBuffer;
				pooled.sourceDescription = description;
				pooled.configured = true;
				if (!transaction.pool->bufferBackups.full()) {
					if (!transaction.pool->bufferBackups.Push(std::move(pooled)))
						return false;
				} else {
					auto replace = std::find_if(transaction.pool->bufferBackups.begin(),
						transaction.pool->bufferBackups.end(), [&](const PooledBufferBackup& candidate) {
							return !backupInUse(candidate.backupBuffer.Get());
						});
					if (replace == transaction.pool->bufferBackups.end())
						return false;
					*replace = std::move(pooled);
				}
				++transaction.poolResourcesCreated;
			}
			if (stageContents)
				transaction.context->CopyResource(backup.backupBuffer.Get(), source);
			if (!transaction.buffers.Push(std::move(backup)))
				return false;
			if (dynamic)
				++transaction.dynamicBufferBackupCount;
			return true;
		}

		template <class Shader>
		[[nodiscard]] bool AddStageBufferBackups(ResourceTransaction& transaction,
			const ShaderStageState<Shader>& stage, bool stageContents = true)
		{
			for (const auto& buffer : stage.constantBuffers) {
				if (buffer && !AddBufferBackup(transaction, buffer.Get(), stageContents))
					return false;
			}
			return true;
		}

		template <class BufferPointer>
		[[nodiscard]] bool AddContextBufferSlot(ResourceTransaction& transaction,
			BufferPointer* slot, std::uint32_t tag, bool stageContents = true)
		{
			auto** d3dSlot = reinterpret_cast<ID3D11Buffer**>(slot);
			auto* buffer = reinterpret_cast<ID3D11Buffer*>(*slot);
			if (!buffer)
				return true;
			if (!AddBufferBackup(transaction, buffer, stageContents))
				return false;
			ContextBufferIdentity identity{};
			identity.slot = d3dSlot;
			identity.buffer = buffer;
			identity.tag = tag;
			identity.rotating = (tag >> 8) == 20u;
			return transaction.contextBufferIdentities.Push(std::move(identity));
		}

		template <class BufferPointer, std::size_t N>
		[[nodiscard]] bool AddContextBufferArray(ResourceTransaction& transaction,
			BufferPointer (&slots)[N], std::uint32_t fieldOrdinal,
			bool stageContents = true)
		{
			for (std::size_t index = 0; index < N; ++index) {
				if (!AddContextBufferSlot(transaction, std::addressof(slots[index]),
						(fieldOrdinal << 8) | static_cast<std::uint32_t>(index), stageContents))
					return false;
			}
			return true;
		}

		[[nodiscard]] bool AddContextConstantGroupBackup(
			ResourceTransaction& transaction, RE::BSGraphics::ConstantGroup* group,
			std::uint32_t tag, bool stageContents = true)
		{
			if (!group || transaction.contextConstantGroups.full())
				return false;

			auto* buffer = reinterpret_cast<ID3D11Buffer*>(group->buffer);
			if (buffer && !AddBufferBackup(transaction, buffer, stageContents))
				return false;
			ContextConstantGroupIdentity identity{};
			identity.slot = group;
			identity.tag = tag;
			std::memcpy(identity.pod.data(), group, identity.pod.size());
			return transaction.contextConstantGroups.Push(std::move(identity));
		}

		template <std::size_t N>
		[[nodiscard]] bool AddContextConstantGroupArray(
			ResourceTransaction& transaction, RE::BSGraphics::ConstantGroup (&groups)[N],
			std::uint32_t fieldOrdinal, bool stageContents = true)
		{
			for (std::size_t index = 0; index < N; ++index) {
				if (!AddContextConstantGroupBackup(transaction, std::addressof(groups[index]),
						(fieldOrdinal << 8) | static_cast<std::uint32_t>(index), stageContents))
					return false;
			}
			return true;
		}

		[[nodiscard]] bool AddContextBufferBackups(ResourceTransaction& transaction,
			RE::BSGraphics::Context* renderContext, bool stageContents = true)
		{
			if (!renderContext)
				return false;

			return AddContextBufferArray(transaction, renderContext->shaderConstantBuffer, 0u, stageContents) &&
			       AddContextBufferArray(transaction, renderContext->vertexShaderConstantBufferTechnique, 1u, stageContents) &&
			       AddContextBufferArray(transaction, renderContext->vertexShaderConstantBufferMaterial, 2u, stageContents) &&
			       AddContextBufferArray(transaction, renderContext->vertexShaderConstantBufferGeometry, 3u, stageContents) &&
			       AddContextBufferArray(transaction, renderContext->hullShaderConstantBufferTechnique, 4u, stageContents) &&
			       AddContextBufferArray(transaction, renderContext->hullShaderConstantBufferMaterial, 5u, stageContents) &&
			       AddContextBufferArray(transaction, renderContext->hullShaderConstantBufferGeometry, 6u, stageContents) &&
			       AddContextBufferArray(transaction, renderContext->domainShaderConstantBufferTechnique, 7u, stageContents) &&
			       AddContextBufferArray(transaction, renderContext->domainShaderConstantBufferMaterial, 8u, stageContents) &&
			       AddContextBufferArray(transaction, renderContext->domainShaderConstantBufferGeometry, 9u, stageContents) &&
			       AddContextBufferArray(transaction, renderContext->pixelShaderConstantBufferTechnique, 10u, stageContents) &&
			       AddContextBufferArray(transaction, renderContext->pixelShaderConstantBufferMaterial, 11u, stageContents) &&
			       AddContextBufferArray(transaction, renderContext->pixelShaderConstantBufferGeometry, 12u, stageContents) &&
			       AddContextBufferArray(transaction, renderContext->computeShaderConstantBufferTechnique, 13u, stageContents) &&
			       AddContextBufferArray(transaction, renderContext->computeShaderConstantBufferMaterial, 14u, stageContents) &&
			       AddContextBufferArray(transaction, renderContext->computeShaderConstantBufferGeometry, 15u, stageContents) &&
			       AddContextBufferSlot(transaction, std::addressof(renderContext->alphaTestConstantBuffer), 16u << 8, stageContents) &&
			       AddContextBufferSlot(transaction, std::addressof(renderContext->perFrameConstantBuffer), 17u << 8, stageContents) &&
			       AddContextBufferSlot(transaction, std::addressof(renderContext->computeConstantBuffer), 18u << 8, stageContents) &&
			       AddContextBufferSlot(transaction, std::addressof(renderContext->instanceTransformConstantBuffer), 19u << 8, stageContents) &&
			       AddContextConstantGroupBackup(transaction, std::addressof(renderContext->miscConstantGroup), 20u << 8, stageContents) &&
			       AddContextConstantGroupArray(transaction, renderContext->vertexConstantBuffersA, 21u, stageContents) &&
			       AddContextConstantGroupArray(transaction, renderContext->pixelConstantBuffersA, 22u, stageContents) &&
			       AddContextConstantGroupArray(transaction, renderContext->domainConstantBuffersA, 23u, stageContents) &&
			       AddContextConstantGroupArray(transaction, renderContext->hullConstantBuffersA, 24u, stageContents) &&
			       AddContextConstantGroupArray(transaction, renderContext->computeConstantBuffersA, 25u, stageContents) &&
			       transaction.contextConstantGroups.size() == kContextConstantGroupCount;
		}

		template <class Shader>
		void CopyNativeWorldStageConstantBufferFacts(const ShaderStageState<Shader>& stage,
			std::array<std::uintptr_t,
				NativeWorldResourceGenerationPlan::kStageConstantBufferCount>& destination) noexcept
		{
			static_assert(std::tuple_size_v<decltype(stage.constantBuffers)> ==
				NativeWorldResourceGenerationPlan::kStageConstantBufferCount);
			for (std::size_t index = 0; index < destination.size(); ++index)
				destination[index] =
					reinterpret_cast<std::uintptr_t>(stage.constantBuffers[index].Get());
		}

		[[nodiscard]] bool SealNativeWorldResourceGeneration(
			const NativeWorldResourcePrewarmRequest& request, const PipelineState& pipeline,
			void* renderContext, bool mrt4Enabled, bool secondLightTargetEnabled,
			const ResourceTransaction& transaction, std::uint64_t& identity) noexcept
		{
			identity = 0;
			if (!transaction.prepared || !transaction.pool || !transaction.rendererData ||
				!transaction.targetManager || !renderContext ||
				transaction.textures.size() > kMaxTextureBackups ||
				transaction.buffers.size() > kMaxBufferBackups ||
				transaction.contextBufferIdentities.size() > kMaxContextBufferIdentities ||
				transaction.contextConstantGroups.size() != kContextConstantGroupCount ||
				transaction.colorBindings.size() > kMaxColorBindings)
				return false;

			auto& plan = g_nativeWorldResourceGeneration;
			plan = {};
			plan.frameSerial = request.frameSerial;
			plan.renderThreadId = request.renderThreadId;
			plan.device = reinterpret_cast<std::uintptr_t>(request.device);
			plan.context = reinterpret_cast<std::uintptr_t>(request.immediateContext);
			plan.cameraConstantBuffer =
				reinterpret_cast<std::uintptr_t>(request.cameraConstantBuffer);
			plan.rendererData = reinterpret_cast<std::uintptr_t>(transaction.rendererData);
			plan.targetManager = reinterpret_cast<std::uintptr_t>(transaction.targetManager);
			plan.renderContext = reinterpret_cast<std::uintptr_t>(renderContext);
			plan.colorMappings = transaction.colorMappings;
			plan.depthMappings = transaction.depthMappings;
			CopyNativeWorldStageConstantBufferFacts(pipeline.vs, plan.stageConstantBuffers[0]);
			CopyNativeWorldStageConstantBufferFacts(pipeline.hs, plan.stageConstantBuffers[1]);
			CopyNativeWorldStageConstantBufferFacts(pipeline.ds, plan.stageConstantBuffers[2]);
			CopyNativeWorldStageConstantBufferFacts(pipeline.gs, plan.stageConstantBuffers[3]);
			CopyNativeWorldStageConstantBufferFacts(pipeline.ps, plan.stageConstantBuffers[4]);
			CopyNativeWorldStageConstantBufferFacts(pipeline.cs, plan.stageConstantBuffers[5]);
			plan.textureCount = static_cast<std::uint32_t>(transaction.textures.size());
			for (std::uint32_t index = 0; index < plan.textureCount; ++index) {
				const auto& source = transaction.textures[index];
				if (!source.engineTexture)
					return false;
				plan.textures[index].texture =
					reinterpret_cast<std::uintptr_t>(source.engineTexture.Get());
				source.engineTexture->GetDesc(
					std::addressof(plan.textures[index].description));
			}
			plan.bufferCount = static_cast<std::uint32_t>(transaction.buffers.size());
			for (std::uint32_t index = 0; index < plan.bufferCount; ++index) {
				const auto& source = transaction.buffers[index];
				plan.buffers[index].buffer =
					reinterpret_cast<std::uintptr_t>(source.engineBuffer.Get());
				plan.buffers[index].description = source.description;
			}
			plan.contextBufferCount =
				static_cast<std::uint32_t>(transaction.contextBufferIdentities.size());
			for (std::uint32_t index = 0; index < plan.contextBufferCount; ++index) {
				const auto& source = transaction.contextBufferIdentities[index];
				plan.contextBuffers[index] = {
					reinterpret_cast<std::uintptr_t>(source.slot),
					reinterpret_cast<std::uintptr_t>(source.buffer.Get()), source.tag,
					source.rotating
				};
			}
			plan.contextConstantGroupCount =
				static_cast<std::uint32_t>(transaction.contextConstantGroups.size());
			for (std::uint32_t index = 0; index < plan.contextConstantGroupCount; ++index) {
				const auto& source = transaction.contextConstantGroups[index];
				plan.contextConstantGroups[index].slot =
					reinterpret_cast<std::uintptr_t>(source.slot);
				plan.contextConstantGroups[index].pod = source.pod;
				plan.contextConstantGroups[index].tag = source.tag;
			}
			plan.colorBindingCount =
				static_cast<std::uint32_t>(transaction.colorBindings.size());
			for (std::uint32_t index = 0; index < plan.colorBindingCount; ++index) {
				const auto& source = transaction.colorBindings[index];
				plan.colorBindings[index] = {
					source.logical, source.physical,
					reinterpret_cast<std::uintptr_t>(source.texture.Get()),
					reinterpret_cast<std::uintptr_t>(source.renderView.Get())
				};
			}
			for (std::size_t index = 0; index < plan.resultSlots.size(); ++index) {
				const auto& source = transaction.pool->resultSlots[index];
				auto& destination = plan.resultSlots[index];
				destination.color = reinterpret_cast<std::uintptr_t>(source.color.Get());
				destination.colorView = reinterpret_cast<std::uintptr_t>(source.colorView.Get());
				destination.depth = reinterpret_cast<std::uintptr_t>(source.depth.Get());
				destination.depthView = reinterpret_cast<std::uintptr_t>(source.depthView.Get());
				destination.colorDescription = source.colorDescription;
				destination.colorViewDescription = source.colorViewDescription;
				destination.depthDescription = source.depthDescription;
				destination.depthViewDescription = source.depthViewDescription;
				destination.configured = source.configured;
			}
			plan.sourceColorDescription = transaction.sourceColorDesc;
			plan.sourceDepthDescription = transaction.sourceDepthDesc;
			plan.sourceColorViewDescription = transaction.sourceColorViewDesc;
			plan.sourceDepthViewDescription = transaction.sourceDepthViewDesc;
			plan.sourceColorRTVDescription = transaction.sourceColorRTVDesc;
			plan.writableDepthViewDescription = transaction.writableDepthViewDesc;
			plan.sourceColor = reinterpret_cast<std::uintptr_t>(transaction.sourceColor.Get());
			plan.sourceColorView =
				reinterpret_cast<std::uintptr_t>(transaction.sourceColorView.Get());
			plan.sourceColorRTV =
				reinterpret_cast<std::uintptr_t>(transaction.sourceColorRTV.Get());
			plan.sourceDepth = reinterpret_cast<std::uintptr_t>(transaction.sourceDepth.Get());
			plan.sourceDepthView =
				reinterpret_cast<std::uintptr_t>(transaction.sourceDepthView.Get());
			plan.writableDepthView =
				reinterpret_cast<std::uintptr_t>(transaction.writableDepthView.Get());
			plan.mrt4Enabled = mrt4Enabled;
			plan.secondLightTargetEnabled = secondLightTargetEnabled;
			identity = g_nativeWorldResourceGenerationSerial.fetch_add(
				1, std::memory_order_relaxed);
			if (identity == 0)
				identity = g_nativeWorldResourceGenerationSerial.fetch_add(
					1, std::memory_order_relaxed);
			plan.identity = identity;
			plan.valid = identity != 0;
			return plan.valid;
		}

		[[nodiscard]] bool NativeWorldResourceGenerationEntryMatches(
			const NativeWorldRequest& request) noexcept
		{
			const auto& plan = g_nativeWorldResourceGeneration;
			return plan.valid && request.resourceGenerationIdentity != 0 &&
				plan.identity == request.resourceGenerationIdentity &&
				plan.frameSerial == request.frameSerial &&
				plan.renderThreadId == request.renderThreadId &&
				plan.device == reinterpret_cast<std::uintptr_t>(request.device) &&
				plan.context == reinterpret_cast<std::uintptr_t>(request.immediateContext) &&
				plan.cameraConstantBuffer ==
					reinterpret_cast<std::uintptr_t>(request.cameraConstantBuffer);
		}

		[[nodiscard]] bool NativeWorldResourceGenerationMatches(
			const NativeWorldRequest& request, const PipelineState& pipeline, void* renderContext,
			bool mrt4Enabled, bool secondLightTargetEnabled,
			const ResourceTransaction& transaction) noexcept
		{
			const auto& plan = g_nativeWorldResourceGeneration;
			if (!NativeWorldResourceGenerationEntryMatches(request) || !transaction.prepared ||
				plan.rendererData != reinterpret_cast<std::uintptr_t>(transaction.rendererData) ||
				plan.targetManager != reinterpret_cast<std::uintptr_t>(transaction.targetManager) ||
				plan.renderContext != reinterpret_cast<std::uintptr_t>(renderContext) ||
				plan.colorMappings != transaction.colorMappings ||
				plan.depthMappings != transaction.depthMappings ||
				plan.mrt4Enabled != mrt4Enabled ||
				plan.secondLightTargetEnabled != secondLightTargetEnabled ||
				plan.textureCount != transaction.textures.size() ||
				plan.bufferCount != transaction.buffers.size() ||
				plan.contextBufferCount != transaction.contextBufferIdentities.size() ||
				plan.contextConstantGroupCount != transaction.contextConstantGroups.size() ||
				plan.colorBindingCount != transaction.colorBindings.size() ||
				std::memcmp(std::addressof(plan.sourceColorDescription),
					std::addressof(transaction.sourceColorDesc), sizeof(transaction.sourceColorDesc)) != 0 ||
				std::memcmp(std::addressof(plan.sourceDepthDescription),
					std::addressof(transaction.sourceDepthDesc), sizeof(transaction.sourceDepthDesc)) != 0 ||
				std::memcmp(std::addressof(plan.sourceColorViewDescription),
					std::addressof(transaction.sourceColorViewDesc), sizeof(transaction.sourceColorViewDesc)) != 0 ||
				std::memcmp(std::addressof(plan.sourceDepthViewDescription),
					std::addressof(transaction.sourceDepthViewDesc), sizeof(transaction.sourceDepthViewDesc)) != 0 ||
				std::memcmp(std::addressof(plan.sourceColorRTVDescription),
					std::addressof(transaction.sourceColorRTVDesc), sizeof(transaction.sourceColorRTVDesc)) != 0 ||
				std::memcmp(std::addressof(plan.writableDepthViewDescription),
					std::addressof(transaction.writableDepthViewDesc), sizeof(transaction.writableDepthViewDesc)) != 0 ||
				plan.sourceColor != reinterpret_cast<std::uintptr_t>(transaction.sourceColor.Get()) ||
				plan.sourceColorView != reinterpret_cast<std::uintptr_t>(transaction.sourceColorView.Get()) ||
				plan.sourceColorRTV != reinterpret_cast<std::uintptr_t>(transaction.sourceColorRTV.Get()) ||
				plan.sourceDepth != reinterpret_cast<std::uintptr_t>(transaction.sourceDepth.Get()) ||
				plan.sourceDepthView != reinterpret_cast<std::uintptr_t>(transaction.sourceDepthView.Get()) ||
				plan.writableDepthView != reinterpret_cast<std::uintptr_t>(transaction.writableDepthView.Get()))
				return false;

			std::array<std::array<std::uintptr_t,
				NativeWorldResourceGenerationPlan::kStageConstantBufferCount>,
				NativeWorldResourceGenerationPlan::kStageCount> currentStages{};
			CopyNativeWorldStageConstantBufferFacts(pipeline.vs, currentStages[0]);
			CopyNativeWorldStageConstantBufferFacts(pipeline.hs, currentStages[1]);
			CopyNativeWorldStageConstantBufferFacts(pipeline.ds, currentStages[2]);
			CopyNativeWorldStageConstantBufferFacts(pipeline.gs, currentStages[3]);
			CopyNativeWorldStageConstantBufferFacts(pipeline.ps, currentStages[4]);
			CopyNativeWorldStageConstantBufferFacts(pipeline.cs, currentStages[5]);
			if (plan.stageConstantBuffers != currentStages)
				return false;
			for (std::uint32_t index = 0; index < plan.textureCount; ++index) {
				const auto& source = transaction.textures[index];
				D3D11_TEXTURE2D_DESC description{};
				if (!source.engineTexture)
					return false;
				source.engineTexture->GetDesc(std::addressof(description));
				if (plan.textures[index].texture !=
					reinterpret_cast<std::uintptr_t>(source.engineTexture.Get()) ||
					std::memcmp(std::addressof(plan.textures[index].description),
						std::addressof(description), sizeof(description)) != 0)
					return false;
			}
			for (std::uint32_t index = 0; index < plan.bufferCount; ++index) {
				const auto& source = transaction.buffers[index];
				if (plan.buffers[index].buffer !=
					reinterpret_cast<std::uintptr_t>(source.engineBuffer.Get()) ||
					std::memcmp(std::addressof(plan.buffers[index].description),
						std::addressof(source.description), sizeof(source.description)) != 0)
					return false;
			}
			for (std::uint32_t index = 0; index < plan.contextBufferCount; ++index) {
				const auto& source = transaction.contextBufferIdentities[index];
				const auto& expected = plan.contextBuffers[index];
				if (expected.slot != reinterpret_cast<std::uintptr_t>(source.slot) ||
					expected.buffer != reinterpret_cast<std::uintptr_t>(source.buffer.Get()) ||
					expected.tag != source.tag || expected.rotating != source.rotating)
					return false;
			}
			for (std::uint32_t index = 0; index < plan.contextConstantGroupCount; ++index) {
				const auto& source = transaction.contextConstantGroups[index];
				const auto& expected = plan.contextConstantGroups[index];
				if (expected.slot != reinterpret_cast<std::uintptr_t>(source.slot) ||
					expected.pod != source.pod || expected.tag != source.tag)
					return false;
			}
			for (std::uint32_t index = 0; index < plan.colorBindingCount; ++index) {
				const auto& source = transaction.colorBindings[index];
				const auto& expected = plan.colorBindings[index];
				if (expected.logical != source.logical || expected.physical != source.physical ||
					expected.texture != reinterpret_cast<std::uintptr_t>(source.texture.Get()) ||
					expected.renderView != reinterpret_cast<std::uintptr_t>(source.renderView.Get()))
					return false;
			}
			for (std::size_t index = 0; index < plan.resultSlots.size(); ++index) {
				const auto& source = transaction.pool->resultSlots[index];
				const auto& expected = plan.resultSlots[index];
				if (expected.configured != source.configured ||
					expected.color != reinterpret_cast<std::uintptr_t>(source.color.Get()) ||
					expected.colorView != reinterpret_cast<std::uintptr_t>(source.colorView.Get()) ||
					expected.depth != reinterpret_cast<std::uintptr_t>(source.depth.Get()) ||
					expected.depthView != reinterpret_cast<std::uintptr_t>(source.depthView.Get()) ||
					std::memcmp(std::addressof(expected.colorDescription),
						std::addressof(source.colorDescription), sizeof(source.colorDescription)) != 0 ||
					std::memcmp(std::addressof(expected.colorViewDescription),
						std::addressof(source.colorViewDescription), sizeof(source.colorViewDescription)) != 0 ||
					std::memcmp(std::addressof(expected.depthDescription),
						std::addressof(source.depthDescription), sizeof(source.depthDescription)) != 0 ||
					std::memcmp(std::addressof(expected.depthViewDescription),
						std::addressof(source.depthViewDescription), sizeof(source.depthViewDescription)) != 0)
					return false;
			}
			return true;
		}

		[[nodiscard]] bool EnsurePooledResultSlot(ResourcePool& pool, PooledResultSlot& slot,
			const D3D11_TEXTURE2D_DESC& colorDescription,
			const D3D11_SHADER_RESOURCE_VIEW_DESC& colorViewDescription,
			const D3D11_TEXTURE2D_DESC& depthDescription,
			const D3D11_SHADER_RESOURCE_VIEW_DESC& depthViewDescription,
			std::uint32_t& resourcesCreated) noexcept
		{
			if (!pool.device)
				return false;
			const bool descriptorMatch = slot.configured &&
				std::memcmp(std::addressof(slot.colorDescription), std::addressof(colorDescription),
					sizeof(colorDescription)) == 0 &&
				std::memcmp(std::addressof(slot.colorViewDescription), std::addressof(colorViewDescription),
					sizeof(colorViewDescription)) == 0 &&
				std::memcmp(std::addressof(slot.depthDescription), std::addressof(depthDescription),
					sizeof(depthDescription)) == 0 &&
				std::memcmp(std::addressof(slot.depthViewDescription), std::addressof(depthViewDescription),
					sizeof(depthViewDescription)) == 0;
			if (descriptorMatch && slot.color && slot.colorView && slot.depth && slot.depthView &&
				ResourceUsesDevice(slot.color.Get(), pool.device.Get()) &&
				ResourceUsesDevice(slot.depth.Get(), pool.device.Get()) &&
				ViewUsesResource(slot.colorView.Get(), slot.color.Get()) &&
				ViewUsesResource(slot.depthView.Get(), slot.depth.Get()))
				return true;

			slot = {};
			if (FAILED(pool.device->CreateTexture2D(std::addressof(colorDescription), nullptr,
					slot.color.GetAddressOf())) ||
				!slot.color ||
				FAILED(pool.device->CreateShaderResourceView(slot.color.Get(),
					std::addressof(colorViewDescription), slot.colorView.GetAddressOf())) ||
				!slot.colorView ||
				FAILED(pool.device->CreateTexture2D(std::addressof(depthDescription), nullptr,
					slot.depth.GetAddressOf())) ||
				!slot.depth ||
				FAILED(pool.device->CreateShaderResourceView(slot.depth.Get(),
					std::addressof(depthViewDescription), slot.depthView.GetAddressOf())) ||
				!slot.depthView) {
				slot = {};
				return false;
			}
			slot.colorDescription = colorDescription;
			slot.colorViewDescription = colorViewDescription;
			slot.depthDescription = depthDescription;
			slot.depthViewDescription = depthViewDescription;
			slot.configured = true;
			resourcesCreated += 4;
			return true;
		}

		[[nodiscard]] bool EnsureCoverageReductionResources(ResourcePool& pool,
			std::uint32_t& resourcesCreated) noexcept
		{
			if (!pool.device)
				return false;
			auto& coverage = pool.coverage;
			if (coverage.depthShader && coverage.mappedDepthShader &&
				coverage.informativeColorShader && coverage.spatialVariationShader &&
				coverage.counter && coverage.counterView && coverage.readback &&
				coverage.forwardOcclusion && coverage.forwardStatistics &&
				ResourceUsesDevice(coverage.counter.Get(), pool.device.Get()) &&
				ResourceUsesDevice(coverage.readback.Get(), pool.device.Get()) &&
				ViewUsesResource(coverage.counterView.Get(), coverage.counter.Get()))
				return true;

			coverage = {};
			static constexpr char depthCoverageShaderSource[] = R"(
Texture2D<float> SourceDepth : register(t0);
RWByteAddressBuffer CoveredPixelCounter : register(u0);
groupshared uint GroupCoveredPixels;

[numthreads(16, 16, 1)]
void main(uint3 dispatchThread : SV_DispatchThreadID, uint groupIndex : SV_GroupIndex)
{
    if (groupIndex == 0)
        GroupCoveredPixels = 0;
    GroupMemoryBarrierWithGroupSync();
    uint width;
    uint height;
    SourceDepth.GetDimensions(width, height);
    if (dispatchThread.x < width && dispatchThread.y < height &&
        SourceDepth.Load(int3(dispatchThread.xy, 0)) < 1.0f) {
        uint ignored;
        InterlockedAdd(GroupCoveredPixels, 1, ignored);
    }
    GroupMemoryBarrierWithGroupSync();
    if (groupIndex == 0 && GroupCoveredPixels != 0) {
        uint ignored;
        CoveredPixelCounter.InterlockedAdd(0, GroupCoveredPixels, ignored);
    }
}
)";
			static constexpr char mappedDepthShaderSource[] = R"(
Texture2D<float4> SourceColor : register(t0);
Texture2D<float> SourceDepth : register(t1);
RWByteAddressBuffer MappedDepthCounter : register(u0);
groupshared uint GroupMappedDepthPixels;

[numthreads(16, 16, 1)]
void main(uint3 dispatchThread : SV_DispatchThreadID, uint groupIndex : SV_GroupIndex)
{
    if (groupIndex == 0)
        GroupMappedDepthPixels = 0;
    GroupMemoryBarrierWithGroupSync();
    uint colorWidth;
    uint colorHeight;
    uint depthWidth;
    uint depthHeight;
    SourceColor.GetDimensions(colorWidth, colorHeight);
    SourceDepth.GetDimensions(depthWidth, depthHeight);
    if (dispatchThread.x < colorWidth && dispatchThread.y < colorHeight) {
        uint2 colorPixel = dispatchThread.xy;
        uint2 depthPixel = min(((colorPixel * 2u + 1u) * uint2(depthWidth, depthHeight)) /
            (uint2(colorWidth, colorHeight) * 2u),
            uint2(depthWidth - 1, depthHeight - 1));
        if (SourceDepth.Load(int3(depthPixel, 0)) < 1.0f) {
            uint ignored;
            InterlockedAdd(GroupMappedDepthPixels, 1, ignored);
        }
    }
    GroupMemoryBarrierWithGroupSync();
    if (groupIndex == 0 && GroupMappedDepthPixels != 0) {
        uint ignored;
        MappedDepthCounter.InterlockedAdd(0, GroupMappedDepthPixels, ignored);
    }
}
)";
			static constexpr char informativeColorShaderSource[] = R"(
Texture2D<float4> SourceColor : register(t0);
Texture2D<float> SourceDepth : register(t1);
RWByteAddressBuffer InformativeColorCounter : register(u0);
groupshared uint GroupInformativePixels;

[numthreads(16, 16, 1)]
void main(uint3 dispatchThread : SV_DispatchThreadID, uint groupIndex : SV_GroupIndex)
{
    if (groupIndex == 0)
        GroupInformativePixels = 0;
    GroupMemoryBarrierWithGroupSync();
    uint colorWidth;
    uint colorHeight;
    uint depthWidth;
    uint depthHeight;
    SourceColor.GetDimensions(colorWidth, colorHeight);
    SourceDepth.GetDimensions(depthWidth, depthHeight);
    if (dispatchThread.x < colorWidth && dispatchThread.y < colorHeight) {
        uint2 colorPixel = dispatchThread.xy;
        uint2 depthPixel = min(((colorPixel * 2u + 1u) * uint2(depthWidth, depthHeight)) /
            (uint2(colorWidth, colorHeight) * 2u),
            uint2(depthWidth - 1, depthHeight - 1));
		// The RGBA8 target was cleared exactly to (0,0,0,0). Opaque alpha proves a
		// uniform-black material write; RGB is the fallback for passes that preserve alpha.
		// This is write evidence, not a brightness or spatial-variance heuristic.
		float4 color = SourceColor.Load(int3(colorPixel, 0));
		bool colorWriteObserved = color.a != 0.0f || any(color.rgb != float3(0.0f, 0.0f, 0.0f));
		if (SourceDepth.Load(int3(depthPixel, 0)) < 1.0f && colorWriteObserved) {
            uint ignored;
            InterlockedAdd(GroupInformativePixels, 1, ignored);
        }
    }
    GroupMemoryBarrierWithGroupSync();
    if (groupIndex == 0 && GroupInformativePixels != 0) {
        uint ignored;
        InformativeColorCounter.InterlockedAdd(0, GroupInformativePixels, ignored);
    }
}
)";
			static constexpr char spatialVariationShaderSource[] = R"(
Texture2D<float4> SourceColor : register(t0);
Texture2D<float> SourceDepth : register(t1);
RWByteAddressBuffer SpatialVariationCounter : register(u0);
groupshared uint GroupVariantPixels;

bool IsCovered(uint2 colorPixel, uint2 colorDimensions, uint2 depthDimensions)
{
    uint2 depthPixel = min(((colorPixel * 2u + 1u) * depthDimensions) /
        (colorDimensions * 2u), depthDimensions - uint2(1, 1));
    return SourceDepth.Load(int3(depthPixel, 0)) < 1.0f;
}

bool IsCoveredPair(uint2 firstPixel, uint2 secondPixel, uint2 colorDimensions,
    uint2 depthDimensions)
{
    return IsCovered(firstPixel, colorDimensions, depthDimensions) &&
        IsCovered(secondPixel, colorDimensions, depthDimensions);
}

[numthreads(16, 16, 1)]
void main(uint3 dispatchThread : SV_DispatchThreadID, uint groupIndex : SV_GroupIndex)
{
    if (groupIndex == 0)
        GroupVariantPixels = 0;
    GroupMemoryBarrierWithGroupSync();
    uint colorWidth;
    uint colorHeight;
    uint depthWidth;
    uint depthHeight;
    SourceColor.GetDimensions(colorWidth, colorHeight);
    SourceDepth.GetDimensions(depthWidth, depthHeight);
    uint2 colorDimensions = uint2(colorWidth, colorHeight);
    uint2 depthDimensions = uint2(depthWidth, depthHeight);
    if (dispatchThread.x < colorWidth && dispatchThread.y < colorHeight) {
        uint2 pixel = dispatchThread.xy;
        bool variant = pixel.x + 1 < colorWidth &&
            IsCoveredPair(pixel, pixel + uint2(1, 0), colorDimensions, depthDimensions);
        variant = variant || (pixel.y + 1 < colorHeight &&
            IsCoveredPair(pixel, pixel + uint2(0, 1), colorDimensions, depthDimensions));
        if (variant) {
            uint ignored;
            InterlockedAdd(GroupVariantPixels, 1, ignored);
        }
    }
    GroupMemoryBarrierWithGroupSync();
    if (groupIndex == 0 && GroupVariantPixels != 0) {
        uint ignored;
        SpatialVariationCounter.InterlockedAdd(0, GroupVariantPixels, ignored);
    }
}
)";
			auto compileShader = [&](const char* source, std::size_t sourceSize, const char* name,
				ComPtr<ID3D11ComputeShader>& shader) noexcept {
				ComPtr<ID3DBlob> shaderBlob;
				ComPtr<ID3DBlob> shaderErrors;
				return SUCCEEDED(D3DCompile(source, sourceSize, name, nullptr, nullptr, "main", "cs_5_0",
					D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
					shaderBlob.GetAddressOf(), shaderErrors.GetAddressOf())) && shaderBlob &&
				       SUCCEEDED(pool.device->CreateComputeShader(shaderBlob->GetBufferPointer(),
						   shaderBlob->GetBufferSize(), nullptr, shader.GetAddressOf())) && shader;
			};
			if (!compileShader(depthCoverageShaderSource, sizeof(depthCoverageShaderSource) - 1,
					"FlatForwardDepthCoverage", coverage.depthShader) ||
				!compileShader(mappedDepthShaderSource, sizeof(mappedDepthShaderSource) - 1,
					"FlatForwardMappedDepth", coverage.mappedDepthShader) ||
				!compileShader(informativeColorShaderSource, sizeof(informativeColorShaderSource) - 1,
					"FlatForwardInformativeColor", coverage.informativeColorShader) ||
				!compileShader(spatialVariationShaderSource, sizeof(spatialVariationShaderSource) - 1,
					"FlatForwardSpatialVariation", coverage.spatialVariationShader))
				return false;

			D3D11_BUFFER_DESC counterDescription{};
			counterDescription.ByteWidth = sizeof(std::uint32_t);
			counterDescription.Usage = D3D11_USAGE_DEFAULT;
			counterDescription.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
			counterDescription.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
			if (FAILED(pool.device->CreateBuffer(std::addressof(counterDescription), nullptr,
					coverage.counter.GetAddressOf())) ||
				!coverage.counter)
				return false;

			D3D11_UNORDERED_ACCESS_VIEW_DESC counterViewDescription{};
			counterViewDescription.Format = DXGI_FORMAT_R32_TYPELESS;
			counterViewDescription.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
			counterViewDescription.Buffer.FirstElement = 0;
			counterViewDescription.Buffer.NumElements = 1;
			counterViewDescription.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
			if (FAILED(pool.device->CreateUnorderedAccessView(coverage.counter.Get(),
					std::addressof(counterViewDescription), coverage.counterView.GetAddressOf())) ||
				!coverage.counterView)
				return false;

			D3D11_BUFFER_DESC readbackDescription = counterDescription;
			readbackDescription.Usage = D3D11_USAGE_STAGING;
			readbackDescription.BindFlags = 0;
			readbackDescription.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
			readbackDescription.MiscFlags = 0;
			if (FAILED(pool.device->CreateBuffer(std::addressof(readbackDescription), nullptr,
					coverage.readback.GetAddressOf())) ||
				!coverage.readback)
				return false;

			D3D11_QUERY_DESC occlusionDescription{ D3D11_QUERY_OCCLUSION, 0 };
			D3D11_QUERY_DESC statisticsDescription{ D3D11_QUERY_PIPELINE_STATISTICS, 0 };
			if (FAILED(pool.device->CreateQuery(std::addressof(occlusionDescription),
					coverage.forwardOcclusion.GetAddressOf())) || !coverage.forwardOcclusion ||
				FAILED(pool.device->CreateQuery(std::addressof(statisticsDescription),
					coverage.forwardStatistics.GetAddressOf())) || !coverage.forwardStatistics)
				return false;
			resourcesCreated += 9;
			return true;
		}

		[[nodiscard]] bool EnsureNativeWorldAuthorityResources(ResourcePool& pool,
			std::uint32_t& resourcesCreated) noexcept
		{
			if (!pool.device)
				return false;
			auto& coverage = pool.coverage;
			if (coverage.nativeWorldAuthorityShader && coverage.nativeWorldCounters &&
				coverage.nativeWorldCounterView && coverage.nativeWorldReadback &&
				ResourceUsesDevice(coverage.nativeWorldCounters.Get(), pool.device.Get()) &&
				ResourceUsesDevice(coverage.nativeWorldReadback.Get(), pool.device.Get()) &&
				ViewUsesResource(coverage.nativeWorldCounterView.Get(),
					coverage.nativeWorldCounters.Get()))
				return true;

			coverage.nativeWorldAuthorityShader.Reset();
			coverage.nativeWorldCounters.Reset();
			coverage.nativeWorldCounterView.Reset();
			coverage.nativeWorldReadback.Reset();
			static constexpr char shaderSource[] = R"(
Texture2D<float4> SourceColor : register(t0);
Texture2D<float> SourceDepth : register(t1);
RWByteAddressBuffer AuthorityCounters : register(u0);
groupshared uint GroupCounters[4];

uint2 MapDepth(uint2 colorPixel, uint2 depthDimensions)
{
    // Native DrawModel uses a zero-origin color-sized viewport in the larger logical-depth1
    // surface. Pixel coordinates therefore correspond directly; normalizing across the full
    // depth texture samples unrelated clear depth whenever the target extents differ.
    return min(colorPixel, depthDimensions - uint2(1, 1));
}

bool CoveredColor(uint2 colorPixel, uint2 colorDimensions, uint2 depthDimensions)
{
    return SourceDepth.Load(int3(MapDepth(colorPixel, depthDimensions), 0)) < 1.0f;
}

bool CoveredPair(uint2 firstPixel, uint2 secondPixel, uint2 colorDimensions,
    uint2 depthDimensions)
{
    return CoveredColor(firstPixel, colorDimensions, depthDimensions) &&
        CoveredColor(secondPixel, colorDimensions, depthDimensions);
}

[numthreads(16, 16, 1)]
void main(uint3 dispatchThread : SV_DispatchThreadID, uint groupIndex : SV_GroupIndex)
{
    if (groupIndex < 4)
        GroupCounters[groupIndex] = 0;
    GroupMemoryBarrierWithGroupSync();

    uint colorWidth;
    uint colorHeight;
    uint depthWidth;
    uint depthHeight;
    SourceColor.GetDimensions(colorWidth, colorHeight);
    SourceDepth.GetDimensions(depthWidth, depthHeight);
    uint2 pixel = dispatchThread.xy;
    if (pixel.x < depthWidth && pixel.y < depthHeight &&
        SourceDepth.Load(int3(pixel, 0)) < 1.0f) {
        uint ignored;
        InterlockedAdd(GroupCounters[0], 1, ignored);
    }
    if (pixel.x < colorWidth && pixel.y < colorHeight) {
        uint2 colorDimensions = uint2(colorWidth, colorHeight);
        uint2 depthDimensions = uint2(depthWidth, depthHeight);
        bool covered = CoveredColor(pixel, colorDimensions, depthDimensions);
		// Native publication needs actual scene RGB. Alpha can be written by a clear or
		// fullscreen pass, and a neighboring depth footprint alone says nothing about the
		// resolved color. Counter 2 therefore rejects RGB-black pixels regardless of alpha;
		// counter 3 requires a covered neighbor whose RGB value really differs.
		float3 color = SourceColor.Load(int3(pixel, 0)).rgb;
		bool colorWriteObserved = covered &&
			any(color != float3(0.0f, 0.0f, 0.0f));
		bool variant = false;
		if (covered && pixel.x + 1 < colorWidth &&
			CoveredPair(pixel, pixel + uint2(1, 0), colorDimensions, depthDimensions)) {
			float3 rightColor = SourceColor.Load(int3(pixel + uint2(1, 0), 0)).rgb;
			variant = any(color != rightColor);
		}
		if (!variant && covered && pixel.y + 1 < colorHeight &&
			CoveredPair(pixel, pixel + uint2(0, 1), colorDimensions, depthDimensions)) {
			float3 downColor = SourceColor.Load(int3(pixel + uint2(0, 1), 0)).rgb;
			variant = any(color != downColor);
		}
        uint ignored;
        if (covered)
            InterlockedAdd(GroupCounters[1], 1, ignored);
		if (colorWriteObserved)
			InterlockedAdd(GroupCounters[2], 1, ignored);
        if (variant)
            InterlockedAdd(GroupCounters[3], 1, ignored);
    }
    GroupMemoryBarrierWithGroupSync();
    if (groupIndex < 4 && GroupCounters[groupIndex] != 0) {
        uint ignored;
        AuthorityCounters.InterlockedAdd(groupIndex * 4, GroupCounters[groupIndex], ignored);
    }
}
)";
			ComPtr<ID3DBlob> shaderBlob;
			ComPtr<ID3DBlob> shaderErrors;
			if (FAILED(D3DCompile(shaderSource, sizeof(shaderSource) - 1,
					"FlatNativeWorldAuthority", nullptr, nullptr, "main", "cs_5_0",
					D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
					shaderBlob.GetAddressOf(), shaderErrors.GetAddressOf())) || !shaderBlob ||
				FAILED(pool.device->CreateComputeShader(shaderBlob->GetBufferPointer(),
					shaderBlob->GetBufferSize(), nullptr,
					coverage.nativeWorldAuthorityShader.GetAddressOf())) ||
				!coverage.nativeWorldAuthorityShader)
				return false;

			D3D11_BUFFER_DESC counterDescription{};
			counterDescription.ByteWidth = 4u * sizeof(std::uint32_t);
			counterDescription.Usage = D3D11_USAGE_DEFAULT;
			counterDescription.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
			counterDescription.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
			if (FAILED(pool.device->CreateBuffer(std::addressof(counterDescription), nullptr,
					coverage.nativeWorldCounters.GetAddressOf())) ||
				!coverage.nativeWorldCounters)
				return false;

			D3D11_UNORDERED_ACCESS_VIEW_DESC counterViewDescription{};
			counterViewDescription.Format = DXGI_FORMAT_R32_TYPELESS;
			counterViewDescription.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
			counterViewDescription.Buffer.FirstElement = 0;
			counterViewDescription.Buffer.NumElements = 4;
			counterViewDescription.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
			if (FAILED(pool.device->CreateUnorderedAccessView(
					coverage.nativeWorldCounters.Get(), std::addressof(counterViewDescription),
					coverage.nativeWorldCounterView.GetAddressOf())) ||
				!coverage.nativeWorldCounterView)
				return false;

			D3D11_BUFFER_DESC readbackDescription = counterDescription;
			readbackDescription.Usage = D3D11_USAGE_STAGING;
			readbackDescription.BindFlags = 0;
			readbackDescription.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
			readbackDescription.MiscFlags = 0;
			if (FAILED(pool.device->CreateBuffer(std::addressof(readbackDescription), nullptr,
					coverage.nativeWorldReadback.GetAddressOf())) ||
				!coverage.nativeWorldReadback)
				return false;
			resourcesCreated += 4;
			return true;
		}

		bool ResourceTransaction::Prepare(const Request& request, const PipelineState& pipeline,
			void* renderContext, bool mrt4Enabled, bool secondLightTargetEnabled,
			CompletionProof& proof)
		{
			(void)mrt4Enabled;
			(void)secondLightTargetEnabled;
			proof.resourcePrepareStage = 1u;
			frameSerial = request.frameSerial;
			reflectedHandednessRequired =
				request.cameraHandedness == CameraHandedness::kReflected;
			device = request.device;
			context = request.immediateContext;
			rendererData = RE::BSGraphics::RendererData::GetSingleton();
			targetManager = reinterpret_cast<RE::BSGraphics::RenderTargetManager*>(
				Address(kRVA_RenderTargetManager));
			if (!device || !context || !rendererData || !targetManager ||
				reinterpret_cast<ID3D11Device*>(rendererData->device) != device ||
				reinterpret_cast<ID3D11DeviceContext*>(rendererData->context) != context)
				return false;
			pool = std::addressof(g_resourcePool);
			if (!pool->EnsureIdentity(device, request.privateRendererIdentitySerial))
				return false;

			proof.resourcePrepareStage = 2u;
			if (!SafeCopy(colorMappings.data(), ReflectionRuntime::RenderTargetIds(targetManager), sizeof(colorMappings)) ||
				!SafeCopy(depthMappings.data(), ReflectionRuntime::DepthStencilTargetIds(targetManager), sizeof(depthMappings)))
				return false;
			ObservePendingMappingTransition(
				targetManager, frameSerial, colorMappings, depthMappings);

			constexpr std::array<std::uint32_t, 1> logicalTargets{ kOutputLogicalColor };

			proof.resourcePrepareStage = 3u;
			for (const auto logical : logicalTargets) {
				if (logical == kLogicalTargetNone)
					continue;
				proof.resourcePrepareDetail = logical;
				if (logical >= colorMappings.size())
					return false;
				const auto physical = colorMappings[logical];
				if (physical >= kPhysicalColorCount)
					return false;
				auto& target = rendererData->renderTargets[physical];
				auto* texture = reinterpret_cast<ID3D11Texture2D*>(target.texture);
				auto* renderView = reinterpret_cast<ID3D11RenderTargetView*>(target.rtView);
				if (!texture || !renderView || !ViewUsesResource(renderView, texture) ||
					!AddTextureBackup(*this, texture, renderView))
					return false;
				ColorBindingIdentity binding{};
				binding.logical = logical;
				binding.physical = physical;
				binding.texture = texture;
				binding.renderView = renderView;
				if (!colorBindings.Push(std::move(binding)))
					return false;
				if (logical == kOutputLogicalColor) {
					colorPhysical = physical;
					sourceColor = texture;
					sourceColorView = reinterpret_cast<ID3D11ShaderResourceView*>(target.srView);
					sourceColorRTV = renderView;
				}
			}

			proof.resourcePrepareStage = 4u;
			proof.resourcePrepareDetail = kOutputLogicalDepth;
			if (kOutputLogicalDepth >= depthMappings.size()) {
				proof.resourcePrepareDetail = 1u;  
				return false;
			}
			depthPhysical = depthMappings[kOutputLogicalDepth];
			if (depthPhysical >= kPhysicalDepthCount) {
				proof.resourcePrepareDetail = 2u;  
				return false;
			}
			auto& depthTarget = rendererData->depthStencilTargets[depthPhysical];
			sourceDepth = reinterpret_cast<ID3D11Texture2D*>(depthTarget.texture);
			sourceDepthView = reinterpret_cast<ID3D11ShaderResourceView*>(depthTarget.srViewDepth);
			writableDepthView = reinterpret_cast<ID3D11DepthStencilView*>(depthTarget.dsView[0]);
			auto rejectDepthPreparation = [&](std::uint32_t detail) noexcept {
				proof.resourcePrepareDetail = detail;
				static std::atomic_uint32_t rejectLogs{};
				if (rejectLogs.fetch_add(1, std::memory_order_relaxed) < 4u) {
					try {
						spdlog::warn(
							"[FlatDeferredPlayerCapture] logical-depth3 preparation rejected: detail={} physical={} texture=0x{:X} srv=0x{:X} dsv=0x{:X} color=0x{:X} colorSRV=0x{:X}",
							detail, depthPhysical, reinterpret_cast<std::uintptr_t>(sourceDepth.Get()),
							reinterpret_cast<std::uintptr_t>(sourceDepthView.Get()),
							reinterpret_cast<std::uintptr_t>(writableDepthView.Get()),
							reinterpret_cast<std::uintptr_t>(sourceColor.Get()),
							reinterpret_cast<std::uintptr_t>(sourceColorView.Get()));
					} catch (...) {
					}
				}
				return false;
			};
			if (!sourceColor) {
				return rejectDepthPreparation(3u);
			}
			if (!sourceColorView) {
				return rejectDepthPreparation(4u);
			}
			if (!sourceDepth) {
				return rejectDepthPreparation(5u);
			}
			if (!sourceDepthView) {
				return rejectDepthPreparation(6u);
			}
			if (!writableDepthView) {
				return rejectDepthPreparation(7u);
			}
			if (sourceColor.Get() == sourceDepth.Get()) {
				return rejectDepthPreparation(8u);
			}
			if (!ViewUsesResource(sourceColorView.Get(), sourceColor.Get())) {
				return rejectDepthPreparation(9u);
			}
			if (!ViewUsesResource(sourceDepthView.Get(), sourceDepth.Get())) {
				return rejectDepthPreparation(10u);
			}
			if (!ViewUsesResource(writableDepthView.Get(), sourceDepth.Get())) {
				return rejectDepthPreparation(11u);
			}
			if (!AddTextureBackup(*this, sourceDepth.Get(), nullptr)) {
				return rejectDepthPreparation(12u);
			}

			proof.resourcePrepareStage = 5u;
			proof.resourcePrepareDetail = 0u;
			sourceColor->GetDesc(std::addressof(sourceColorDesc));
			sourceDepth->GetDesc(std::addressof(sourceDepthDesc));
			sourceColorView->GetDesc(std::addressof(sourceColorViewDesc));
			sourceDepthView->GetDesc(std::addressof(sourceDepthViewDesc));
			reinterpret_cast<ID3D11RenderTargetView*>(
				rendererData->renderTargets[colorPhysical].rtView)
				->GetDesc(
					std::addressof(sourceColorRTVDesc));
			writableDepthView->GetDesc(std::addressof(writableDepthViewDesc));
			if (sourceColorDesc.Width == 0 || sourceColorDesc.Height == 0 ||
				sourceDepthDesc.Width < sourceColorDesc.Width ||
				sourceDepthDesc.Height < sourceColorDesc.Height ||
				sourceColorDesc.MipLevels != 1 || sourceDepthDesc.MipLevels != 1 ||
				sourceColorDesc.ArraySize != 1 || sourceDepthDesc.ArraySize != 1 ||
				sourceColorDesc.SampleDesc.Count != 1 || sourceDepthDesc.SampleDesc.Count != 1 ||
				sourceColorDesc.Format != DXGI_FORMAT_R8G8B8A8_UNORM ||
				sourceColorViewDesc.Format != DXGI_FORMAT_R8G8B8A8_UNORM ||
				sourceColorRTVDesc.Format != DXGI_FORMAT_R8G8B8A8_UNORM ||
				sourceDepthDesc.Format != DXGI_FORMAT_R24G8_TYPELESS ||
				sourceDepthViewDesc.Format != DXGI_FORMAT_R24_UNORM_X8_TYPELESS ||
				writableDepthViewDesc.Format != DXGI_FORMAT_D24_UNORM_S8_UINT ||
				sourceColorViewDesc.ViewDimension != D3D11_SRV_DIMENSION_TEXTURE2D ||
				sourceDepthViewDesc.ViewDimension != D3D11_SRV_DIMENSION_TEXTURE2D ||
				sourceColorRTVDesc.ViewDimension != D3D11_RTV_DIMENSION_TEXTURE2D ||
				writableDepthViewDesc.ViewDimension != D3D11_DSV_DIMENSION_TEXTURE2D ||
				sourceColorViewDesc.Texture2D.MostDetailedMip != 0 ||
				sourceColorViewDesc.Texture2D.MipLevels != 1 ||
				sourceDepthViewDesc.Texture2D.MostDetailedMip != 0 ||
				sourceDepthViewDesc.Texture2D.MipLevels != 1 ||
				sourceColorRTVDesc.Texture2D.MipSlice != 0 ||
				writableDepthViewDesc.Texture2D.MipSlice != 0 || writableDepthViewDesc.Flags != 0) {
				static std::atomic_uint32_t descriptorRejectLogs{};
				if (descriptorRejectLogs.fetch_add(1, std::memory_order_relaxed) < 4u) {
					spdlog::warn(
						"[FlatDeferredPlayerCapture] resource descriptor reject: color={}x{} fmt={} mips={} array={} samples={} bind=0x{:X} srvFmt={} srvDim={} rtvFmt={} rtvDim={} depth={}x{} fmt={} mips={} array={} samples={} bind=0x{:X} srvFmt={} srvDim={} dsvFmt={} dsvDim={} dsvFlags=0x{:X}",
						sourceColorDesc.Width, sourceColorDesc.Height,
						static_cast<std::uint32_t>(sourceColorDesc.Format), sourceColorDesc.MipLevels,
						sourceColorDesc.ArraySize, sourceColorDesc.SampleDesc.Count, sourceColorDesc.BindFlags,
						static_cast<std::uint32_t>(sourceColorViewDesc.Format),
						static_cast<std::uint32_t>(sourceColorViewDesc.ViewDimension),
						static_cast<std::uint32_t>(sourceColorRTVDesc.Format),
						static_cast<std::uint32_t>(sourceColorRTVDesc.ViewDimension),
						sourceDepthDesc.Width, sourceDepthDesc.Height,
						static_cast<std::uint32_t>(sourceDepthDesc.Format), sourceDepthDesc.MipLevels,
						sourceDepthDesc.ArraySize, sourceDepthDesc.SampleDesc.Count, sourceDepthDesc.BindFlags,
						static_cast<std::uint32_t>(sourceDepthViewDesc.Format),
						static_cast<std::uint32_t>(sourceDepthViewDesc.ViewDimension),
						static_cast<std::uint32_t>(writableDepthViewDesc.Format),
						static_cast<std::uint32_t>(writableDepthViewDesc.ViewDimension),
						writableDepthViewDesc.Flags);
				}
				return false;
			}
			if ((request.expectedWidth != 0 && request.expectedWidth != sourceColorDesc.Width) ||
				(request.expectedHeight != 0 && request.expectedHeight != sourceColorDesc.Height))
				return false;

			proof.resourcePrepareStage = 6u;
			if (!AddBufferBackup(*this, request.cameraConstantBuffer) ||
				!AddStageBufferBackups(*this, pipeline.vs) ||
				!AddStageBufferBackups(*this, pipeline.hs) ||
				!AddStageBufferBackups(*this, pipeline.ds) ||
				!AddStageBufferBackups(*this, pipeline.gs) ||
				!AddStageBufferBackups(*this, pipeline.ps) ||
				!AddStageBufferBackups(*this, pipeline.cs) ||
				!AddContextBufferBackups(*this,
					reinterpret_cast<RE::BSGraphics::Context*>(renderContext)))
				return false;

			proof.resourcePrepareStage = 7u;
			D3D11_TEXTURE2D_DESC colorResultDescription = sourceColorDesc;
			colorResultDescription.Usage = D3D11_USAGE_DEFAULT;
			colorResultDescription.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			colorResultDescription.CPUAccessFlags = 0;
			colorResultDescription.MiscFlags = 0;
			D3D11_TEXTURE2D_DESC depthResultDescription = sourceDepthDesc;
			depthResultDescription.Usage = D3D11_USAGE_DEFAULT;
			depthResultDescription.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			depthResultDescription.CPUAccessFlags = 0;
			depthResultDescription.MiscFlags = 0;
			for (auto& slot : pool->resultSlots) {
				if (!EnsurePooledResultSlot(*pool, slot, colorResultDescription,
						sourceColorViewDesc, depthResultDescription, sourceDepthViewDesc,
						poolResourcesCreated))
					return false;
			}
			candidateResultSlot = pool->avoidNextResultSlot < pool->resultSlots.size() ?
				(pool->avoidNextResultSlot == 0 ? 1u : 0u) :
				(pool->committedResultSlot == 0 ? 1u : 0u);
			auto& candidate = pool->resultSlots[candidateResultSlot];
			const auto& firstResult = pool->resultSlots[0];
			const auto& secondResult = pool->resultSlots[1];
			if (firstResult.color.Get() == secondResult.color.Get() ||
				firstResult.depth.Get() == secondResult.depth.Get() ||
				firstResult.color.Get() == firstResult.depth.Get() ||
				secondResult.color.Get() == secondResult.depth.Get() ||
				firstResult.color.Get() == sourceColor.Get() ||
				secondResult.color.Get() == sourceColor.Get() ||
				firstResult.depth.Get() == sourceDepth.Get() ||
				secondResult.depth.Get() == sourceDepth.Get())
				return false;
			resultColor = candidate.color;
			resultColorView = candidate.colorView;
			resultDepth = candidate.depth;
			resultDepthView = candidate.depthView;
			proof.resourcePrepareStage = 8u;
			if (!EnsureCoverageReductionResources(*pool, poolResourcesCreated))
				return false;

			proof.sourceColorResource = reinterpret_cast<std::uintptr_t>(sourceColor.Get());
			proof.sourceDepthResource = reinterpret_cast<std::uintptr_t>(sourceDepth.Get());
			proof.colorPhysicalTarget = colorPhysical;
			proof.depthPhysicalTarget = depthPhysical;
			proof.width = sourceColorDesc.Width;
			proof.height = sourceColorDesc.Height;
			proof.colorResourceFormat = sourceColorDesc.Format;
			proof.colorViewFormat = sourceColorViewDesc.Format;
			proof.depthResourceFormat = sourceDepthDesc.Format;
			proof.depthViewFormat = sourceDepthViewDesc.Format;
			proof.colorViewDimension = sourceColorViewDesc.ViewDimension;
			proof.depthViewDimension = sourceDepthViewDesc.ViewDimension;
			proof.sampleCount = sourceColorDesc.SampleDesc.Count;
			proof.sourceExtentReported = true;
			proof.doubleBufferedResultPublication = true;
			proof.resourcePrepareStage = 9u;
			proof.resourcePrepareDetail = 0u;

			prepared = true;
			return true;
		}

		bool ResourceTransaction::PrepareNativeWorld(const NativeWorldRequest& request,
			const PipelineState& pipeline, void* renderContext, bool mrt4Enabled,
			bool secondLightTargetEnabled, bool stageBackupContents, bool allowPoolReset,
			NativeWorldCompletionProof& proof)
		{
			proof.resourcePrepareStage = 1u;
			requireStableContextBufferIdentities = true;
			frameSerial = request.frameSerial;
			device = request.device;
			context = request.immediateContext;
			rendererData = RE::BSGraphics::RendererData::GetSingleton();
			targetManager = reinterpret_cast<RE::BSGraphics::RenderTargetManager*>(
				Address(kRVA_RenderTargetManager));
			if (!device || !context || !rendererData || !targetManager ||
				reinterpret_cast<ID3D11Device*>(rendererData->device) != device ||
				reinterpret_cast<ID3D11DeviceContext*>(rendererData->context) != context)
				return false;
			pool = std::addressof(g_nativeWorldResourcePool);
			constexpr std::uint64_t kNativeWorldPoolIdentity = 0x4E41544956455731ULL;
			if (!(allowPoolReset ? pool->EnsureIdentity(device, kNativeWorldPoolIdentity) :
				pool->MatchesIdentity(device, kNativeWorldPoolIdentity)))
				return false;

			proof.resourcePrepareStage = 2u;
			if (!SafeCopy(colorMappings.data(), ReflectionRuntime::RenderTargetIds(targetManager), sizeof(colorMappings)) ||
				!SafeCopy(depthMappings.data(), ReflectionRuntime::DepthStencilTargetIds(targetManager),
					sizeof(depthMappings)))
				return false;

			std::array<std::uint32_t, 10> logicalTargets{
				1, 0x1A, 0x1B, 0x1D, 0x1E, 0x20, 0x21, kOutputLogicalColor,
				kLogicalTargetNone, kLogicalTargetNone
			};
			if (mrt4Enabled)
				logicalTargets[8] = 0x1F;
			if (secondLightTargetEnabled)
				logicalTargets[9] = 0x22;
			proof.mrt4TargetRequired = mrt4Enabled;
			proof.secondLightTargetRequired = secondLightTargetEnabled;
			proof.outputLogicalColorTarget = kOutputLogicalColor;
			proof.outputLogicalDepthTarget = kNativeWorldOutputLogicalDepth;

			proof.resourcePrepareStage = 3u;
			for (const auto logical : logicalTargets) {
				if (logical == kLogicalTargetNone)
					continue;
				proof.resourcePrepareDetail = logical;
				if (logical >= colorMappings.size() ||
					proof.backedLogicalColorTargetCount >=
						proof.backedLogicalColorTargets.size())
					return false;
				const auto physical = colorMappings[logical];
				if (physical >= kPhysicalColorCount)
					return false;
				auto& target = rendererData->renderTargets[physical];
				auto* texture = reinterpret_cast<ID3D11Texture2D*>(target.texture);
				auto* renderView = reinterpret_cast<ID3D11RenderTargetView*>(target.rtView);
				if (!texture || !renderView || !ViewUsesResource(renderView, texture) ||
					!AddTextureBackup(*this, texture, renderView, stageBackupContents))
					return false;
				ColorBindingIdentity binding{};
				binding.logical = logical;
				binding.physical = physical;
				binding.texture = texture;
				binding.renderView = renderView;
				if (!colorBindings.Push(std::move(binding)))
					return false;
				proof.backedLogicalColorTargets[proof.backedLogicalColorTargetCount++] = logical;
				if (logical == kOutputLogicalColor) {
					colorPhysical = physical;
					sourceColor = texture;
					sourceColorView =
						reinterpret_cast<ID3D11ShaderResourceView*>(target.srView);
					sourceColorRTV = renderView;
				}
			}

			const std::uint32_t expectedColorTargetCount =
				8u + (mrt4Enabled ? 1u : 0u) + (secondLightTargetEnabled ? 1u : 0u);
			if (proof.backedLogicalColorTargetCount != expectedColorTargetCount)
				return false;

			proof.resourcePrepareStage = 4u;
			proof.resourcePrepareDetail = kNativeWorldOutputLogicalDepth;
			if (kNativeWorldOutputLogicalDepth >= depthMappings.size())
				return false;
			depthPhysical = depthMappings[kNativeWorldOutputLogicalDepth];
			if (depthPhysical >= kPhysicalDepthCount)
				return false;
			auto& depthTarget = rendererData->depthStencilTargets[depthPhysical];
			sourceDepth = reinterpret_cast<ID3D11Texture2D*>(depthTarget.texture);
			sourceDepthView =
				reinterpret_cast<ID3D11ShaderResourceView*>(depthTarget.srViewDepth);
			writableDepthView =
				reinterpret_cast<ID3D11DepthStencilView*>(depthTarget.dsView[0]);
			if (!sourceColor || !sourceColorView || !sourceColorRTV || !sourceDepth ||
				!sourceDepthView || !writableDepthView || sourceColor.Get() == sourceDepth.Get() ||
				!ViewUsesResource(sourceColorView.Get(), sourceColor.Get()) ||
				!ViewUsesResource(sourceDepthView.Get(), sourceDepth.Get()) ||
				!ViewUsesResource(writableDepthView.Get(), sourceDepth.Get()) ||
				!AddTextureBackup(*this, sourceDepth.Get(), nullptr, stageBackupContents))
				return false;

			proof.resourcePrepareStage = 5u;
			proof.resourcePrepareDetail = 0u;
			sourceColor->GetDesc(std::addressof(sourceColorDesc));
			sourceDepth->GetDesc(std::addressof(sourceDepthDesc));
			sourceColorView->GetDesc(std::addressof(sourceColorViewDesc));
			sourceDepthView->GetDesc(std::addressof(sourceDepthViewDesc));
			sourceColorRTV->GetDesc(std::addressof(sourceColorRTVDesc));
			writableDepthView->GetDesc(std::addressof(writableDepthViewDesc));
			if (sourceColorDesc.Width == 0 || sourceColorDesc.Height == 0 ||
				sourceDepthDesc.Width < sourceColorDesc.Width ||
				sourceDepthDesc.Height < sourceColorDesc.Height ||
				sourceColorDesc.MipLevels != 1 || sourceDepthDesc.MipLevels != 1 ||
				sourceColorDesc.ArraySize != 1 || sourceDepthDesc.ArraySize != 1 ||
				sourceColorDesc.SampleDesc.Count != 1 || sourceDepthDesc.SampleDesc.Count != 1 ||
				sourceColorDesc.Format != DXGI_FORMAT_R8G8B8A8_UNORM ||
				sourceColorViewDesc.Format != DXGI_FORMAT_R8G8B8A8_UNORM ||
				sourceColorRTVDesc.Format != DXGI_FORMAT_R8G8B8A8_UNORM ||
				sourceDepthDesc.Format != DXGI_FORMAT_R24G8_TYPELESS ||
				sourceDepthViewDesc.Format != DXGI_FORMAT_R24_UNORM_X8_TYPELESS ||
				writableDepthViewDesc.Format != DXGI_FORMAT_D24_UNORM_S8_UINT ||
				sourceColorViewDesc.ViewDimension != D3D11_SRV_DIMENSION_TEXTURE2D ||
				sourceDepthViewDesc.ViewDimension != D3D11_SRV_DIMENSION_TEXTURE2D ||
				sourceColorRTVDesc.ViewDimension != D3D11_RTV_DIMENSION_TEXTURE2D ||
				writableDepthViewDesc.ViewDimension != D3D11_DSV_DIMENSION_TEXTURE2D ||
				sourceColorViewDesc.Texture2D.MostDetailedMip != 0 ||
				sourceColorViewDesc.Texture2D.MipLevels != 1 ||
				sourceDepthViewDesc.Texture2D.MostDetailedMip != 0 ||
				sourceDepthViewDesc.Texture2D.MipLevels != 1 ||
				sourceColorRTVDesc.Texture2D.MipSlice != 0 ||
				writableDepthViewDesc.Texture2D.MipSlice != 0 || writableDepthViewDesc.Flags != 0)
				return false;

			proof.requiredWidth = request.requiredWidth;
			proof.requiredHeight = request.requiredHeight;
			proof.colorWidth = sourceColorDesc.Width;
			proof.colorHeight = sourceColorDesc.Height;
			proof.depthWidth = sourceDepthDesc.Width;
			proof.depthHeight = sourceDepthDesc.Height;
			proof.sourceExtentReported = true;
			proof.implicitResamplePerformed = false;
			proof.exactRequiredExtentSatisfied =
				(request.requiredWidth == 0 ||
					(request.requiredWidth == sourceColorDesc.Width &&
						request.requiredWidth == sourceDepthDesc.Width)) &&
				(request.requiredHeight == 0 ||
					(request.requiredHeight == sourceColorDesc.Height &&
						request.requiredHeight == sourceDepthDesc.Height));
			if (!proof.exactRequiredExtentSatisfied)
				return false;

			proof.resourcePrepareStage = 6u;
			if (!AddBufferBackup(*this, request.cameraConstantBuffer, stageBackupContents) ||
				!AddStageBufferBackups(*this, pipeline.vs, stageBackupContents) ||
				!AddStageBufferBackups(*this, pipeline.hs, stageBackupContents) ||
				!AddStageBufferBackups(*this, pipeline.ds, stageBackupContents) ||
				!AddStageBufferBackups(*this, pipeline.gs, stageBackupContents) ||
				!AddStageBufferBackups(*this, pipeline.ps, stageBackupContents) ||
				!AddStageBufferBackups(*this, pipeline.cs, stageBackupContents) ||
				!AddContextBufferBackups(*this,
					reinterpret_cast<RE::BSGraphics::Context*>(renderContext),
					stageBackupContents))
				return false;
			proof.contextConstantGroupCount =
				static_cast<std::uint32_t>(contextConstantGroups.size());
			if (proof.contextConstantGroupCount != kContextConstantGroupCount)
				return false;

			proof.resourcePrepareStage = 7u;
			D3D11_TEXTURE2D_DESC colorResultDescription = sourceColorDesc;
			colorResultDescription.Usage = D3D11_USAGE_DEFAULT;
			colorResultDescription.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			colorResultDescription.CPUAccessFlags = 0;
			colorResultDescription.MiscFlags = 0;
			D3D11_TEXTURE2D_DESC depthResultDescription = sourceDepthDesc;
			depthResultDescription.Usage = D3D11_USAGE_DEFAULT;
			depthResultDescription.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			depthResultDescription.CPUAccessFlags = 0;
			depthResultDescription.MiscFlags = 0;
			for (auto& slot : pool->resultSlots) {
				if (!EnsurePooledResultSlot(*pool, slot, colorResultDescription,
						sourceColorViewDesc, depthResultDescription, sourceDepthViewDesc,
						poolResourcesCreated))
					return false;
			}
			candidateResultSlot = pool->avoidNextResultSlot < pool->resultSlots.size() ?
				(pool->avoidNextResultSlot == 0 ? 1u : 0u) :
				(pool->committedResultSlot == 0 ? 1u : 0u);
			auto& candidate = pool->resultSlots[candidateResultSlot];
			const auto& first = pool->resultSlots[0];
			const auto& second = pool->resultSlots[1];
			if (first.color.Get() == second.color.Get() ||
				first.depth.Get() == second.depth.Get() ||
				first.color.Get() == first.depth.Get() ||
				second.color.Get() == second.depth.Get() ||
				first.color.Get() == sourceColor.Get() ||
				second.color.Get() == sourceColor.Get() ||
				first.depth.Get() == sourceDepth.Get() ||
				second.depth.Get() == sourceDepth.Get())
				return false;
			resultColor = candidate.color;
			resultColorView = candidate.colorView;
			resultDepth = candidate.depth;
			resultDepthView = candidate.depthView;

			proof.resourcePrepareStage = 8u;
			if (!EnsureNativeWorldAuthorityResources(*pool, poolResourcesCreated))
				return false;

			proof.sourceColorResource =
				reinterpret_cast<std::uintptr_t>(sourceColor.Get());
			proof.sourceDepthResource =
				reinterpret_cast<std::uintptr_t>(sourceDepth.Get());
			proof.colorPhysicalTarget = colorPhysical;
			proof.depthPhysicalTarget = depthPhysical;
			proof.colorResourceFormat = sourceColorDesc.Format;
			proof.colorViewFormat = sourceColorViewDesc.Format;
			proof.depthResourceFormat = sourceDepthDesc.Format;
			proof.depthViewFormat = sourceDepthViewDesc.Format;
			proof.colorViewDimension = sourceColorViewDesc.ViewDimension;
			proof.depthViewDimension = sourceDepthViewDesc.ViewDimension;
			proof.sampleCount = sourceColorDesc.SampleDesc.Count;
			proof.declaredLogicalWriteSetBacked = true;
			proof.resourcePrepareStage = 9u;
			proof.resourcePrepareDetail = 0u;
			prepared = true;
			return true;
		}

		void ResourceTransaction::ClearForCapture() noexcept
		{
			if (!prepared || !context)
				return;
			constexpr std::array<FLOAT, 4> clearColor{ 0.0f, 0.0f, 0.0f, 0.0f };
			for (const auto& texture : textures) {
				if (texture.clearView)
					context->ClearRenderTargetView(texture.clearView.Get(), clearColor.data());
			}
			UINT flags = D3D11_CLEAR_DEPTH;
			if (writableDepthViewDesc.Format == DXGI_FORMAT_D24_UNORM_S8_UINT ||
				writableDepthViewDesc.Format == DXGI_FORMAT_D32_FLOAT_S8X24_UINT)
				flags |= D3D11_CLEAR_STENCIL;
			context->ClearDepthStencilView(writableDepthView.Get(), flags, 1.0f, 0);
			mutated = true;
		}

		void ResourceTransaction::CapturePrepassDepth() noexcept
		{
			if (prepassDepthCaptured || !prepared || !mutated || !context || !sourceDepth || !resultDepth ||
				!OutputResourcesStable() || sourceDepth.Get() == resultDepth.Get())
				return;

			ComPtr<ID3D11DepthStencilView> boundDepthView;
			context->OMGetRenderTargets(0, nullptr, boundDepthView.GetAddressOf());
			ComPtr<ID3D11Resource> boundDepthResource;
			ComPtr<ID3D11Texture2D> boundDepth;
			if (!boundDepthView)
				return;
			boundDepthView->GetResource(boundDepthResource.GetAddressOf());
			if (!boundDepthResource || FAILED(boundDepthResource.As(&boundDepth)) || !boundDepth ||
				!ResourceUsesDevice(boundDepth.Get(), device) || boundDepth.Get() == resultDepth.Get() ||
				boundDepth.Get() != sourceDepth.Get())
				return;
			D3D11_TEXTURE2D_DESC boundDescription{};
			boundDepth->GetDesc(std::addressof(boundDescription));
			if (std::memcmp(std::addressof(boundDescription), std::addressof(sourceDepthDesc),
					sizeof(boundDescription)) != 0)
				return;
			static std::atomic<std::uint32_t> boundDepthLogs{ 0 };
			if (boundDepthLogs.fetch_add(1, std::memory_order_relaxed) < 8u)
				spdlog::info(
					"[FlatDeferredPlayerCapture] prepass bound depth: logical=0x{:X} bound=0x{:X} match={}",
					reinterpret_cast<std::uintptr_t>(sourceDepth.Get()),
					reinterpret_cast<std::uintptr_t>(boundDepth.Get()),
					boundDepth.Get() == sourceDepth.Get());
			context->CopyResource(resultDepth.Get(), boundDepth.Get());
			prepassDepthCaptured = true;
		}

		void ResourceTransaction::CaptureFinalForwardDepth() noexcept
		{
			if (!prepared || !mutated || !context || !sourceDepth || !resultDepth ||
				!OutputResourcesStable() || sourceDepth.Get() == resultDepth.Get())
				return;
			D3D11_TEXTURE2D_DESC currentDescription{};
			sourceDepth->GetDesc(std::addressof(currentDescription));
			if (std::memcmp(std::addressof(currentDescription), std::addressof(sourceDepthDesc),
					sizeof(currentDescription)) != 0)
				return;

			context->CopyResource(resultDepth.Get(), sourceDepth.Get());
			finalForwardDepthCaptured = true;
		}

		void ResourceTransaction::ReadForwardQueries() noexcept
		{
			if (!forwardQueriesEnded || !context || !pool)
				return;

			const bool occlusionReady = context->GetData(pool->coverage.forwardOcclusion.Get(),
				std::addressof(forwardOcclusionSamples), sizeof(forwardOcclusionSamples),
				D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK;
			const bool statisticsReady = context->GetData(pool->coverage.forwardStatistics.Get(),
				std::addressof(forwardStatistics), sizeof(forwardStatistics),
				D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK;
			forwardQueriesReadBack = occlusionReady && statisticsReady;
		}

		bool ResourceTransaction::ArmForwardGeometryProof(
			void* const* geometries, void* const* shaderProperties,
			const std::uint8_t* selectorEligible, std::uint32_t rawCount,
			RE::NiAVObject* const* requiredEyeGeometries,
			std::uint32_t requiredEyeGeometryCount) noexcept
		{
			if (!prepared || forwardGeometryProofArmed || !geometries || !shaderProperties ||
				!selectorEligible || rawCount == 0 || rawCount > forwardExpectedGeometries.size() ||
				!requiredEyeGeometries || requiredEyeGeometryCount == 0 ||
				requiredEyeGeometryCount > Request::kMaxRequiredEyeGeometries)
				return false;
			std::uint32_t count = 0;
			for (std::uint32_t rawIndex = 0; rawIndex < rawCount; ++rawIndex) {
				void* geometry = geometries[rawIndex];
				void* shaderProperty = shaderProperties[rawIndex];
				if (!geometry || !shaderProperty)
					return false;
				if (selectorEligible[rawIndex] > 1u)
					return false;
				if (selectorEligible[rawIndex] == 0u)
					continue;
				for (std::uint32_t prior = 0; prior < count; ++prior) {
					if (forwardExpectedGeometries[prior] == geometry &&
						forwardExpectedShaderProperties[prior] == shaderProperty)
						return false;
				}
				forwardExpectedGeometries[count] = geometry;
				forwardExpectedShaderProperties[count] = shaderProperty;
				++count;
			}
			if (count == 0)
				return false;

			std::uint32_t eligibleRequiredEyes = 0;
			for (std::uint32_t eyeIndex = 0; eyeIndex < requiredEyeGeometryCount; ++eyeIndex) {
				auto* requiredEye = requiredEyeGeometries[eyeIndex];
				if (!requiredEye)
					return false;
				for (std::uint32_t prior = 0; prior < eyeIndex; ++prior) {
					if (requiredEyeGeometries[prior] == requiredEye)
						return false;
				}
				for (std::uint32_t index = 0; index < count; ++index) {
					if (forwardExpectedGeometries[index] != requiredEye)
						continue;
					forwardExpectedRequiredEyes[index] = 1u;
					++eligibleRequiredEyes;
					break;
				}
			}

			if (eligibleRequiredEyes == 0)
				return false;
			forwardExpectedGeometryCount = count;
			forwardGeometryProofArmed = true;
			return true;
		}

		bool ResourceTransaction::BeginForwardSelectorObservation(void* accumulator) noexcept
		{
			if (!forwardGeometryProofArmed || !accumulator || forwardSelectorObservationActive ||
				forwardSelectorSuffixCompleted || forwardActiveSpecialAlphaArmed)
				return false;
			forwardSelectorAccumulator = accumulator;
			forwardSelectorObservationActive = true;
			return true;
		}

		void ResourceTransaction::CompleteActiveSpecialAlpha(
			bool suffixBoundaryCompleted) noexcept
		{
			if (!forwardActiveSpecialAlphaArmed)
				return;
			auto increment = [&](std::uint32_t& value) noexcept {
				if (value == (std::numeric_limits<std::uint32_t>::max)()) {
					forwardGeometryCounterOverflow = true;
					return false;
				}
				++value;
				return true;
			};
			const auto index = forwardActiveSpecialAlphaIndex;
			const bool indexValid = index < forwardExpectedGeometryCount;

			const bool outputsPinned = suffixBoundaryCompleted &&
				forwardActiveSpecialAlphaOutputsPinned;
			const bool completed = suffixBoundaryCompleted && indexValid &&
				forwardActiveSpecialAlphaPolicySeen &&
				forwardActiveSpecialAlphaPolicyAllSucceeded && outputsPinned;
			if (completed) {
				(void)increment(forwardSpecialAlphaCompletionCounts[index]);
				(void)increment(forwardSpecialAlphaPinnedCounts[index]);
			} else {
				(void)increment(forwardUnbalancedGeometryCallbacks);
				if (suffixBoundaryCompleted && !outputsPinned)
					(void)increment(forwardForeignOutputCallbacks);
			}
			forwardActiveSpecialAlphaIndex =
				(std::numeric_limits<std::uint32_t>::max)();
			forwardActiveSpecialAlphaArmed = false;
			forwardActiveSpecialAlphaPolicySeen = false;
			forwardActiveSpecialAlphaPolicyAllSucceeded = false;
			forwardActiveSpecialAlphaOutputsPinned = false;
		}

		bool ResourceTransaction::EndForwardSelectorObservation(bool suffixCompleted) noexcept
		{
			if (!forwardSelectorObservationActive)
				return false;
			CompleteActiveSpecialAlpha(suffixCompleted);
			forwardSelectorObservationActive = false;
			forwardSelectorAccumulator = nullptr;
			forwardSelectorSuffixCompleted = suffixCompleted;
			return suffixCompleted;
		}

		void ResourceTransaction::ObserveForwardSelectorResult(
			const void* shaderProperty, const void* geometry, std::uint32_t renderMode,
			const void* accumulator, const void* result) noexcept
		{
			if (!forwardSelectorObservationActive)
				return;

			CompleteActiveSpecialAlpha(true);
			auto increment = [&](std::uint32_t& value) noexcept {
				if (value == (std::numeric_limits<std::uint32_t>::max)()) {
					forwardGeometryCounterOverflow = true;
					return false;
				}
				++value;
				return true;
			};
			if (!increment(forwardSelectorResultCalls))
				return;
			if (!shaderProperty || !geometry) {
				(void)increment(forwardNullGeometryCallbacks);
				return;
			}
			if (renderMode != 0 || accumulator != forwardSelectorAccumulator) {
				(void)increment(forwardForeignGeometryCallbacks);
				return;
			}

			std::uint32_t expectedIndex =
				(std::numeric_limits<std::uint32_t>::max)();
			for (std::uint32_t index = 0; index < forwardExpectedGeometryCount; ++index) {
				if (forwardExpectedGeometries[index] == geometry &&
					forwardExpectedShaderProperties[index] == shaderProperty) {
					expectedIndex = index;
					break;
				}
			}
			if (expectedIndex == (std::numeric_limits<std::uint32_t>::max)()) {
				(void)increment(forwardForeignGeometryCallbacks);
				return;
			}
			if (forwardOrdinarySelectionCounts[expectedIndex] != 0 ||
				forwardSpecialAlphaSelectionCounts[expectedIndex] != 0 ||
				forwardNoPassSelectionCounts[expectedIndex] != 0) {
				(void)increment(forwardUnbalancedGeometryCallbacks);
				return;
			}

			if (!result) {
				(void)increment(forwardNoPassSelectionCounts[expectedIndex]);
				return;
			}

			void* firstPass = nullptr;
			void* passGeometry = nullptr;
			void* passShaderProperty = nullptr;
			std::uint32_t technique = 0;
			if (!SafeLoad(reinterpret_cast<std::uintptr_t>(result), firstPass)) {
				(void)increment(forwardNullGeometryCallbacks);
				return;
			}
			if (!firstPass) {
				(void)increment(forwardNoPassSelectionCounts[expectedIndex]);
				return;
			}
			if (!SafeLoad(reinterpret_cast<std::uintptr_t>(firstPass) +
					kRenderPassGeometryOffset, passGeometry) || !passGeometry ||
				!SafeLoad(reinterpret_cast<std::uintptr_t>(firstPass) +
					kRenderPassShaderPropertyOffset, passShaderProperty) || !passShaderProperty ||
				!SafeLoad(reinterpret_cast<std::uintptr_t>(firstPass) +
					kRenderPassTechniqueOffset, technique)) {
				(void)increment(forwardNullGeometryCallbacks);
				return;
			}
			if (passGeometry != geometry || passShaderProperty != shaderProperty) {
				(void)increment(forwardForeignGeometryCallbacks);
				return;
			}
			if ((technique & kForwardSpecialAlphaTechniqueBit) == 0) {
				(void)increment(forwardOrdinarySelectionCounts[expectedIndex]);
				return;
			}

			(void)increment(forwardSpecialAlphaSelectionCounts[expectedIndex]);
			forwardActiveSpecialAlphaIndex = expectedIndex;
			forwardActiveSpecialAlphaArmed = true;
			forwardActiveSpecialAlphaPolicySeen = false;
			forwardActiveSpecialAlphaPolicyAllSucceeded = true;

			forwardActiveSpecialAlphaOutputsPinned = true;
		}

		bool ResourceTransaction::ForwardOutputsPinned() const noexcept
		{
			if (!context || !sourceColorRTV || !writableDepthView)
				return false;
			ID3D11RenderTargetView* boundColor = nullptr;
			ID3D11DepthStencilView* boundDepth = nullptr;
			context->OMGetRenderTargets(1, std::addressof(boundColor), std::addressof(boundDepth));
			const bool pinned = boundColor == sourceColorRTV.Get() &&
				boundDepth == writableDepthView.Get();
			if (boundDepth)
				boundDepth->Release();
			if (boundColor)
				boundColor->Release();
			return pinned;
		}

		bool ResourceTransaction::RebindForwardOutputs() noexcept
		{

			if (!forwardSelectorObservationActive ||
				(!forwardActivePassArmed && !forwardActiveSpecialAlphaArmed) || !context ||
				!sourceColorRTV || !writableDepthView)
				return false;
			ID3D11RenderTargetView* renderTarget = sourceColorRTV.Get();
			context->OMSetRenderTargets(1, std::addressof(renderTarget), writableDepthView.Get());
			D3D11_VIEWPORT viewport{};
			viewport.Width = static_cast<float>(sourceColorDesc.Width);
			viewport.Height = static_cast<float>(sourceColorDesc.Height);
			viewport.MinDepth = 0.0f;
			viewport.MaxDepth = 1.0f;
			context->RSSetViewports(1, std::addressof(viewport));
			D3D11_RECT fullTargetScissor{};
			fullTargetScissor.right = static_cast<LONG>(sourceColorDesc.Width);
			fullTargetScissor.bottom = static_cast<LONG>(sourceColorDesc.Height);
			context->RSSetScissorRects(1, std::addressof(fullTargetScissor));
			return ForwardOutputsPinned();
		}

		bool ResourceTransaction::AttestNativeWorldTargetsAfterSetDirty() noexcept
		{
			if (!prepared || !mutated || !nativeWorldRasterizerPolicyArmed || !context ||
				colorBindings.size() == 0 || !sourceDepth || sourceColorDesc.Width == 0 ||
				sourceColorDesc.Height == 0)
				return false;

			std::array<ID3D11RenderTargetView*, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT>
				rawColors{};
			ID3D11DepthStencilView* rawDepth = nullptr;
			context->OMGetRenderTargets(static_cast<UINT>(rawColors.size()), rawColors.data(),
				std::addressof(rawDepth));
			std::array<ComPtr<ID3D11RenderTargetView>,
				D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> observedColors{};
			for (std::size_t index = 0; index < rawColors.size(); ++index)
				observedColors[index].Attach(rawColors[index]);
			ComPtr<ID3D11DepthStencilView> observedDepth;
			observedDepth.Attach(rawDepth);

			const auto poisonForeignOutputs = [&]() noexcept {

				context->OMSetRenderTargets(0u, nullptr, nullptr);
				return false;
			};
			bool backedColorObserved = false;
			for (const auto& observedColor : observedColors) {
				if (!observedColor)
					continue;
				bool declaredResource = false;
				for (const auto& binding : colorBindings) {
					if (binding.texture &&
						ViewUsesResource(observedColor.Get(), binding.texture.Get())) {
						declaredResource = true;
						break;
					}
				}
				if (!declaredResource)
					return poisonForeignOutputs();
				backedColorObserved = true;
			}
			if (observedDepth && !ViewUsesResource(observedDepth.Get(), sourceDepth.Get()))
				return poisonForeignOutputs();

			if (!backedColorObserved && !observedDepth)
				return true;

			UINT viewportCount = 0u;
			context->RSGetViewports(std::addressof(viewportCount), nullptr);
			if (viewportCount != 1u)
				return false;
			D3D11_VIEWPORT observedViewport{};
			context->RSGetViewports(
				std::addressof(viewportCount), std::addressof(observedViewport));
			return viewportCount == 1u &&
				std::isfinite(observedViewport.TopLeftX) &&
				std::isfinite(observedViewport.TopLeftY) &&
				std::isfinite(observedViewport.Width) &&
				std::isfinite(observedViewport.Height) &&
				std::isfinite(observedViewport.MinDepth) &&
				std::isfinite(observedViewport.MaxDepth) &&
				observedViewport.Width > 0.0f && observedViewport.Height > 0.0f &&
				observedViewport.MinDepth >= 0.0f &&
				observedViewport.MaxDepth <= 1.0f &&
				observedViewport.MinDepth <= observedViewport.MaxDepth;
		}

		bool ResourceTransaction::TryBeginOrdinaryForwardPass(
			RE::BSRenderPass* pass) noexcept
		{

			if (!forwardGeometryProofArmed || !forwardSelectorObservationActive ||
				forwardActiveSpecialAlphaArmed || forwardActivePassArmed || !pass)
				return false;

			void* geometry = nullptr;
			void* shaderProperty = nullptr;
			if (!SafeLoad(reinterpret_cast<std::uintptr_t>(pass) +
					kRenderPassGeometryOffset, geometry) || !geometry ||
				!SafeLoad(reinterpret_cast<std::uintptr_t>(pass) +
					kRenderPassShaderPropertyOffset, shaderProperty) || !shaderProperty)
				return false;

			for (std::uint32_t index = 0; index < forwardExpectedGeometryCount; ++index) {
				if (forwardExpectedGeometries[index] != geometry ||
					forwardExpectedShaderProperties[index] != shaderProperty)
					continue;
				if (forwardOrdinarySelectionCounts[index] != 1 ||
					forwardSpecialAlphaSelectionCounts[index] != 0 ||
					forwardNoPassSelectionCounts[index] != 0)
					return false;
				NoteForwardGeometrySetup(pass);
				return forwardActivePassArmed && forwardActivePass == pass &&
					forwardActiveExpectedIndex == index;
			}
			return false;
		}

		bool ResourceTransaction::PrimeOrdinaryForwardPassCommit(
			RE::BSRenderPass* pass) noexcept
		{
			forwardActivePrimeAttempted = true;
			forwardActivePrimeSucceeded = false;
			forwardActivePrimeOutputsRebound = false;
			forwardActivePrimeRasterizerApplied = false;

			if (!forwardSelectorObservationActive || forwardActiveSpecialAlphaArmed ||
				!forwardActivePassArmed || !pass || pass != forwardActivePass ||
				forwardActiveExpectedIndex >= forwardExpectedGeometryCount)
				return false;
			void* geometry = nullptr;
			void* shaderProperty = nullptr;
			if (!SafeLoad(reinterpret_cast<std::uintptr_t>(pass) +
					kRenderPassGeometryOffset, geometry) || !geometry ||
				!SafeLoad(reinterpret_cast<std::uintptr_t>(pass) +
					kRenderPassShaderPropertyOffset, shaderProperty) || !shaderProperty ||
				geometry != forwardActiveGeometry || shaderProperty != forwardActiveShaderProperty)
				return false;

			const bool outputsRebound = RebindForwardOutputs();
			forwardActivePrimeOutputsRebound = outputsRebound;
			if (reflectedHandednessRequired) {
				const bool rasterizerApplied =
					PlanarMirrorLookup::ApplyReflectedRasterizer(device, context);
				forwardActivePrimeRasterizerApplied = rasterizerApplied;
				NoteReflectedRasterizerCommit(rasterizerApplied);
				forwardActivePrimeSucceeded = outputsRebound && rasterizerApplied && forwardActivePolicySeen &&
					forwardActivePolicyAllSucceeded && forwardActiveCommitOutputsPinned;
				return forwardActivePrimeSucceeded;
			}
			NoteOrdinaryRasterizerCommit();
			forwardActivePrimeSucceeded = outputsRebound && forwardActivePolicySeen &&
				forwardActivePolicyAllSucceeded && forwardActiveCommitOutputsPinned;
			return forwardActivePrimeSucceeded;
		}

		bool ResourceTransaction::TrySubstituteEssentialForwardVertexShader(
			RE::BSShader* shader, std::uint32_t requestedVertexDescriptor,
			std::uint32_t hullDescriptor, std::uint32_t domainDescriptor,
			std::uint32_t pixelDescriptor, const void* outputStruct,
			std::uint32_t& nativeVertexDescriptor) noexcept
		{

			if (!prepared || !mutated || !device || !context || !shader || !outputStruct ||
				hullDescriptor != 0u || domainDescriptor != 0u ||
				nativeVertexDescriptor != requestedVertexDescriptor ||
				!forwardSelectorObservationActive || forwardActiveSpecialAlphaArmed ||
				!forwardActivePassArmed || !forwardActivePrimeAttempted ||
				!forwardActivePrimeSucceeded || !forwardActivePass ||
				forwardActiveExpectedIndex >= forwardExpectedGeometryCount)
				return false;

			std::uintptr_t shaderVtable = 0;
			static const REL::Relocation<std::uintptr_t> lightingShaderVtable{
				RE::VTABLE::BSLightingShader[0]
			};
			if (!SafeLoad(reinterpret_cast<std::uintptr_t>(shader), shaderVtable) ||
				shaderVtable != lightingShaderVtable.address() ||
				!shader->fxpFilename || std::strcmp(shader->fxpFilename, "Lighting") != 0)
				return false;

			RE::BSShader* passShader = nullptr;
			void* passGeometry = nullptr;
			void* passShaderProperty = nullptr;
			std::uint32_t rawTechnique = 0;
			if (!SafeLoad(reinterpret_cast<std::uintptr_t>(forwardActivePass) +
					kRenderPassShaderOffset, passShader) || passShader != shader ||
				!SafeLoad(reinterpret_cast<std::uintptr_t>(forwardActivePass) +
					kRenderPassGeometryOffset, passGeometry) || !passGeometry ||
				!SafeLoad(reinterpret_cast<std::uintptr_t>(forwardActivePass) +
					kRenderPassShaderPropertyOffset, passShaderProperty) || !passShaderProperty ||
				!SafeLoad(reinterpret_cast<std::uintptr_t>(forwardActivePass) +
					kRenderPassTechniqueOffset, rawTechnique) ||
				passGeometry != forwardActiveGeometry ||
				passShaderProperty != forwardActiveShaderProperty ||
				forwardExpectedGeometries[forwardActiveExpectedIndex] != passGeometry ||
				forwardExpectedShaderProperties[forwardActiveExpectedIndex] != passShaderProperty ||
				forwardOrdinarySelectionCounts[forwardActiveExpectedIndex] != 1u ||
				forwardSpecialAlphaSelectionCounts[forwardActiveExpectedIndex] != 0u ||
				forwardNoPassSelectionCounts[forwardActiveExpectedIndex] != 0u)
				return false;

			const auto reducedVertexDescriptor = rawTechnique & 0x3F0Fu;
			auto reducedPixelDescriptor = rawTechnique;
			if ((reducedPixelDescriptor & 0x4u) == 0u)
				reducedPixelDescriptor &= ~0x2u;
			reducedPixelDescriptor |= 0x1u;
			if (requestedVertexDescriptor != reducedVertexDescriptor ||
				pixelDescriptor != reducedPixelDescriptor)
				return false;

			const EssentialForwardVertexShaderPermutation* permutation = nullptr;
			for (const auto& candidate : kEssentialForwardVertexShaderPermutations) {
				if (candidate.rawTechnique == rawTechnique &&
					candidate.requestedVertexDescriptor == requestedVertexDescriptor &&
					candidate.pixelDescriptor == pixelDescriptor) {
					permutation = std::addressof(candidate);
					break;
				}
			}
			if (!permutation)
				return false;

			void* skinInstance = nullptr;
			void* rendererData = nullptr;
			std::uint64_t geometryVertexDescriptor = 0;
			std::uint8_t geometryType = 0;
			const auto geometryAddress = reinterpret_cast<std::uintptr_t>(passGeometry);
			if (!SafeLoad(geometryAddress + kGeometrySkinInstanceOffset, skinInstance) ||
				!skinInstance ||
				!SafeLoad(geometryAddress + kGeometryRendererDataOffset, rendererData) ||
				!rendererData ||
				!SafeLoad(geometryAddress + kGeometryVertexDescriptorOffset,
					geometryVertexDescriptor) ||
				!SafeLoad(geometryAddress + kGeometryTypeOffset, geometryType) ||
				(geometryType != 4u && geometryType != 8u))
				return false;

			RE::BSGraphics::VertexDesc vertexDescription{};
			vertexDescription.desc = geometryVertexDescriptor;
			using Vertex = RE::BSGraphics::Vertex;

			if (!vertexDescription.HasFlag(Vertex::VF_UV) ||
				!vertexDescription.HasFlag(Vertex::VF_NORMAL) ||
				!vertexDescription.HasFlag(Vertex::VF_TANGENT) ||
				!vertexDescription.HasFlag(Vertex::VF_SKINNED) ||
				vertexDescription.HasFlag(Vertex::VF_LANDDATA) ||
				vertexDescription.HasFlag(Vertex::VF_EYEDATA) ||
				vertexDescription.HasFlag(Vertex::VF_COLORS) !=
					permutation->requiresVertexColor)
				return false;

			auto findVertexShader = [&](std::uint32_t descriptor) noexcept {
				RE::BSGraphics::VertexShader probe{};
				probe.id = descriptor;
				auto* key = std::addressof(probe);
				auto& vertexShaders = BSShaderTables::VertexShaders(shader);
				const auto it = vertexShaders.find(key);
				return it != vertexShaders.end() ? *it : nullptr;
			};
			auto findPixelShader = [&](std::uint32_t descriptor) noexcept {
				RE::BSGraphics::PixelShader probe{};
				probe.id = descriptor;
				auto* key = std::addressof(probe);
				auto& pixelShaders = BSShaderTables::PixelShaders(shader);
				const auto it = pixelShaders.find(key);
				return it != pixelShaders.end() ? *it : nullptr;
			};
			auto* requestedWrapper = findVertexShader(requestedVertexDescriptor);
			auto* nativeWrapper = findVertexShader(permutation->nativeVertexDescriptor);
			auto* pixelWrapper = findPixelShader(pixelDescriptor);
			if ((requestedWrapper && requestedWrapper->shader) || !nativeWrapper ||
				!nativeWrapper->shader || !pixelWrapper || !pixelWrapper->shader)
				return false;

			ComPtr<ID3D11Device> vertexShaderDevice;
			reinterpret_cast<ID3D11VertexShader*>(nativeWrapper->shader)->GetDevice(
				vertexShaderDevice.GetAddressOf());
			ComPtr<ID3D11Device> pixelShaderDevice;
			reinterpret_cast<ID3D11PixelShader*>(pixelWrapper->shader)->GetDevice(
				pixelShaderDevice.GetAddressOf());
			if (vertexShaderDevice.Get() != device || pixelShaderDevice.Get() != device)
				return false;

			if (essentialVertexShaderRepairAttempts ==
				(std::numeric_limits<std::uint32_t>::max)()) {
				forwardGeometryCounterOverflow = true;
				return false;
			}
			++essentialVertexShaderRepairAttempts;
			nativeVertexDescriptor = permutation->nativeVertexDescriptor;
			return true;
		}

		void ResourceTransaction::NoteEssentialForwardVertexShaderResult(
			std::uint32_t requestedVertexDescriptor,
			std::uint32_t nativeVertexDescriptor,
			std::uint32_t pixelDescriptor, bool accepted) noexcept
		{
			auto increment = [&](std::uint32_t& value) noexcept {
				if (value == (std::numeric_limits<std::uint32_t>::max)()) {
					forwardGeometryCounterOverflow = true;
					return false;
				}
				++value;
				return true;
			};
			std::uint32_t rawTechnique = 0;
			const bool activeOutcome = forwardSelectorObservationActive &&
				forwardActivePassArmed && !forwardActiveSpecialAlphaArmed &&
				forwardActiveExpectedIndex < forwardExpectedGeometryCount &&
				forwardActivePass &&
				SafeLoad(reinterpret_cast<std::uintptr_t>(forwardActivePass) +
					kRenderPassTechniqueOffset, rawTechnique);
			const EssentialForwardVertexShaderPermutation* permutation = nullptr;
			if (activeOutcome) {
				for (const auto& candidate : kEssentialForwardVertexShaderPermutations) {
					if (candidate.rawTechnique == rawTechnique &&
						candidate.requestedVertexDescriptor == requestedVertexDescriptor &&
						candidate.nativeVertexDescriptor == nativeVertexDescriptor &&
						candidate.pixelDescriptor == pixelDescriptor) {
						permutation = std::addressof(candidate);
						break;
					}
				}
			}
			if (!permutation) {
				(void)increment(essentialVertexShaderRepairOutcomeMismatches);
				return;
			}
			if (accepted)
				(void)increment(essentialVertexShaderRepairAcceptances);
			else
				(void)increment(essentialVertexShaderRepairRejections);

			static std::atomic<std::uint32_t> outcomeLogs{ 0 };
			const auto ordinal = outcomeLogs.fetch_add(1u, std::memory_order_relaxed) + 1u;
			if (ordinal <= 32u) {
				try {
					spdlog::info(
						"[FlatDeferredPlayerCapture] essential Lighting VS repair: ordinal={}/32 "
						"index={}/{} raw=0x{:X} requestedVS=0x{:X} nativeVS=0x{:X} "
						"pixel=0x{:X} accepted={} totals={}/{}/{}",
						ordinal, forwardActiveExpectedIndex, forwardExpectedGeometryCount,
						rawTechnique, requestedVertexDescriptor, nativeVertexDescriptor,
						pixelDescriptor, accepted, essentialVertexShaderRepairAttempts,
						essentialVertexShaderRepairAcceptances,
						essentialVertexShaderRepairRejections);
				} catch (...) {
				}
			}
		}

		void ResourceTransaction::NoteForwardGeometrySetup(RE::BSRenderPass* pass) noexcept
		{
			auto increment = [&](std::uint32_t& value) noexcept {
				if (value == (std::numeric_limits<std::uint32_t>::max)()) {
					forwardGeometryCounterOverflow = true;
					return false;
				}
				++value;
				return true;
			};
			if (!increment(forwardGeometrySetupCalls))
				return;
			if (forwardActivePassArmed) {
				(void)increment(forwardUnbalancedGeometryCallbacks);
				return;
			}

			forwardActivePassArmed = true;
			forwardActivePass = pass;
			forwardActiveExpectedIndex = (std::numeric_limits<std::uint32_t>::max)();
			forwardActivePolicySeen = false;
			forwardActivePolicyAllSucceeded = true;
			forwardActiveCommitOutputsPinned = true;
			forwardActivePrimeAttempted = false;
			forwardActivePrimeSucceeded = false;
			forwardActivePrimeOutputsRebound = false;
			forwardActivePrimeRasterizerApplied = false;
			void* geometry = nullptr;
			void* shaderProperty = nullptr;
			if (!pass ||
				!SafeLoad(reinterpret_cast<std::uintptr_t>(pass) +
					kRenderPassGeometryOffset, geometry) || !geometry ||
				!SafeLoad(reinterpret_cast<std::uintptr_t>(pass) +
					kRenderPassShaderPropertyOffset, shaderProperty) || !shaderProperty) {
				forwardActiveGeometry = geometry;
				forwardActiveShaderProperty = shaderProperty;
				(void)increment(forwardNullGeometryCallbacks);
				return;
			}
			forwardActiveGeometry = geometry;
			forwardActiveShaderProperty = shaderProperty;
			for (std::uint32_t index = 0; index < forwardExpectedGeometryCount; ++index) {
				if (forwardExpectedGeometries[index] != geometry ||
					forwardExpectedShaderProperties[index] != shaderProperty)
					continue;
				forwardActiveExpectedIndex = index;
				(void)increment(forwardSetupCounts[index]);
				if (forwardOrdinarySelectionCounts[index] != 1 ||
					forwardSpecialAlphaSelectionCounts[index] != 0)
					(void)increment(forwardUnbalancedGeometryCallbacks);
				return;
			}
			(void)increment(forwardForeignGeometryCallbacks);
		}

		void ResourceTransaction::NoteForwardGeometryRestore(
			RE::BSRenderPass* pass, bool nativeCompleted, bool accepted) noexcept
		{
			auto increment = [&](std::uint32_t& value) noexcept {
				if (value == (std::numeric_limits<std::uint32_t>::max)()) {
					forwardGeometryCounterOverflow = true;
					return false;
				}
				++value;
				return true;
			};
			if (!increment(forwardGeometryRestoreCalls))
				return;
			void* geometry = nullptr;
			void* shaderProperty = nullptr;
			const bool pairLoaded = pass &&
				SafeLoad(reinterpret_cast<std::uintptr_t>(pass) +
					kRenderPassGeometryOffset, geometry) && geometry &&
				SafeLoad(reinterpret_cast<std::uintptr_t>(pass) +
					kRenderPassShaderPropertyOffset, shaderProperty) && shaderProperty;
			const bool activePairMatches = pairLoaded && forwardActivePassArmed &&
				pass == forwardActivePass && geometry == forwardActiveGeometry &&
				shaderProperty == forwardActiveShaderProperty;
			std::uint32_t expectedIndex = (std::numeric_limits<std::uint32_t>::max)();
			if (pairLoaded) {
				for (std::uint32_t index = 0; index < forwardExpectedGeometryCount; ++index) {
					if (forwardExpectedGeometries[index] == geometry &&
						forwardExpectedShaderProperties[index] == shaderProperty) {
						expectedIndex = index;
						break;
					}
				}
			}

			if (!pairLoaded)
				(void)increment(forwardNullGeometryCallbacks);
			else if (expectedIndex == (std::numeric_limits<std::uint32_t>::max)())
				(void)increment(forwardForeignGeometryCallbacks);
			else {
				(void)increment(forwardRestoreCounts[expectedIndex]);
				const bool intervalAttested = nativeCompleted && accepted && activePairMatches &&
					forwardActiveExpectedIndex == expectedIndex && forwardActivePolicySeen &&
					forwardActivePolicyAllSucceeded && forwardActiveCommitOutputsPinned;
				if (intervalAttested) {
					(void)increment(forwardPinnedRestoreCounts[expectedIndex]);
					(void)increment(forwardPolicyCompletedCounts[expectedIndex]);
				} else {
					(void)increment(forwardUnbalancedGeometryCallbacks);
					static std::atomic<std::uint32_t> intervalFailureLogs{ 0 };
					if (intervalFailureLogs.fetch_add(1, std::memory_order_relaxed) < 64u) {
						std::uint32_t technique = 0;
						(void)SafeLoad(reinterpret_cast<std::uintptr_t>(pass) +
							kRenderPassTechniqueOffset, technique);
						try {
							spdlog::info(
								"[FlatDeferredPlayerCapture] ordinary interval rejected: index={}/{} "
								"pass=0x{:X} geometry=0x{:X} property=0x{:X} technique=0x{:X} "
								"prime={}/{} rebind={} reflectedRequired={} rasterizerApplied={} "
								"policy={}/{} outputs={} nativeCompleted={} accepted={} pair={} indexMatch={}",
								expectedIndex, forwardExpectedGeometryCount,
								reinterpret_cast<std::uintptr_t>(pass),
								reinterpret_cast<std::uintptr_t>(geometry),
								reinterpret_cast<std::uintptr_t>(shaderProperty), technique,
								forwardActivePrimeAttempted, forwardActivePrimeSucceeded,
								forwardActivePrimeOutputsRebound, reflectedHandednessRequired,
								forwardActivePrimeRasterizerApplied, forwardActivePolicySeen,
								forwardActivePolicyAllSucceeded, forwardActiveCommitOutputsPinned,
								nativeCompleted, accepted, activePairMatches,
								forwardActiveExpectedIndex == expectedIndex);
						} catch (...) {
						}
					}
				}

				if (!forwardActiveCommitOutputsPinned)
					(void)increment(forwardForeignOutputCallbacks);
			}
			if (!activePairMatches)
				(void)increment(forwardUnbalancedGeometryCallbacks);

			forwardActivePass = nullptr;
			forwardActiveGeometry = nullptr;
			forwardActiveShaderProperty = nullptr;
			forwardActiveExpectedIndex = (std::numeric_limits<std::uint32_t>::max)();
			forwardActivePassArmed = false;
			forwardActivePolicySeen = false;
			forwardActivePolicyAllSucceeded = false;
			forwardActiveCommitOutputsPinned = false;
			forwardActivePrimeAttempted = false;
			forwardActivePrimeSucceeded = false;
			forwardActivePrimeOutputsRebound = false;
			forwardActivePrimeRasterizerApplied = false;
		}

		void ResourceTransaction::NoteReflectedRasterizerCommit(bool applied) noexcept
		{
			auto increment = [&](std::uint32_t& value) noexcept {
				if (value == (std::numeric_limits<std::uint32_t>::max)()) {
					forwardGeometryCounterOverflow = true;
					return;
				}
				++value;
			};
			const bool specialRouteActive = forwardSelectorObservationActive &&
				forwardActiveSpecialAlphaArmed &&
				forwardActiveSpecialAlphaIndex < forwardExpectedGeometryCount;
			const bool ordinaryRouteActive = forwardSelectorObservationActive &&
				forwardActivePassArmed &&
				forwardActiveExpectedIndex < forwardExpectedGeometryCount;

			if (!specialRouteActive && !ordinaryRouteActive)
				return;
			increment(reflectedRasterizerApplyAttempts);
			if (applied)
				increment(reflectedRasterizerApplySuccesses);
			else
				increment(reflectedRasterizerApplyFailures);
			if (specialRouteActive) {
				if (!reflectedHandednessRequired) {
					increment(unattributedRasterizerPolicyCommits);
					forwardActiveSpecialAlphaPolicyAllSucceeded = false;
					return;
				}
				forwardActiveSpecialAlphaPolicySeen = true;
				forwardActiveSpecialAlphaPolicyAllSucceeded &= applied;
				forwardActiveSpecialAlphaOutputsPinned &= ForwardOutputsPinned();
				return;
			}
			if (!reflectedHandednessRequired) {
				increment(unattributedRasterizerPolicyCommits);
				return;
			}
			forwardActivePolicySeen = true;
			forwardActivePolicyAllSucceeded &= applied;
			forwardActiveCommitOutputsPinned &= ForwardOutputsPinned();
		}

		void ResourceTransaction::NoteOrdinaryRasterizerCommit() noexcept
		{
			auto increment = [&](std::uint32_t& value) noexcept {
				if (value == (std::numeric_limits<std::uint32_t>::max)()) {
					forwardGeometryCounterOverflow = true;
					return;
				}
				++value;
			};
			const bool specialRouteActive = forwardSelectorObservationActive &&
				forwardActiveSpecialAlphaArmed &&
				forwardActiveSpecialAlphaIndex < forwardExpectedGeometryCount;
			const bool ordinaryRouteActive = forwardSelectorObservationActive &&
				forwardActivePassArmed &&
				forwardActiveExpectedIndex < forwardExpectedGeometryCount;
			if (!specialRouteActive && !ordinaryRouteActive)
				return;
			increment(ordinaryRasterizerPolicyCommits);
			if (specialRouteActive) {
				if (reflectedHandednessRequired) {
					increment(unattributedRasterizerPolicyCommits);
					forwardActiveSpecialAlphaPolicyAllSucceeded = false;
					return;
				}
				forwardActiveSpecialAlphaPolicySeen = true;
				forwardActiveSpecialAlphaOutputsPinned &= ForwardOutputsPinned();
				return;
			}
			if (reflectedHandednessRequired) {
				increment(unattributedRasterizerPolicyCommits);
				return;
			}
			forwardActivePolicySeen = true;
			forwardActiveCommitOutputsPinned &= ForwardOutputsPinned();
		}

		bool ResourceTransaction::AttestForwardGeometryProof(
			std::uint32_t& observedGeometryCount,
			std::uint32_t& ordinaryGeometryCount,
			std::uint32_t& specialAlphaGeometryCount,
			std::uint32_t& noPassGeometryCount,
			std::uint32_t& routedEyeGeometryCount) const noexcept
		{
			observedGeometryCount = 0;
			ordinaryGeometryCount = 0;
			specialAlphaGeometryCount = 0;
			noPassGeometryCount = 0;
			routedEyeGeometryCount = 0;
			const bool policyTotalsAttested = reflectedHandednessRequired ?
				(reflectedRasterizerApplyAttempts >= forwardGeometrySetupCalls &&
					(forwardGeometrySetupCalls == 0 || reflectedRasterizerApplyAttempts != 0) &&
					reflectedRasterizerApplySuccesses == reflectedRasterizerApplyAttempts &&
					reflectedRasterizerApplyFailures == 0 &&
					ordinaryRasterizerPolicyCommits == 0) :
				(reflectedRasterizerApplyAttempts == 0 &&
					reflectedRasterizerApplySuccesses == 0 &&
					reflectedRasterizerApplyFailures == 0 &&
					ordinaryRasterizerPolicyCommits >= forwardGeometrySetupCalls);
			if (!forwardGeometryProofArmed || forwardExpectedGeometryCount == 0 ||
				!forwardSelectorSuffixCompleted || forwardSelectorObservationActive ||
				forwardSelectorResultCalls != forwardExpectedGeometryCount ||
				forwardGeometryCounterOverflow || forwardActivePassArmed ||
				forwardActiveSpecialAlphaArmed ||
				unattributedRasterizerPolicyCommits != 0 || !policyTotalsAttested ||
				forwardForeignGeometryCallbacks != 0 || forwardNullGeometryCallbacks != 0 ||
				forwardUnbalancedGeometryCallbacks != 0 || forwardForeignOutputCallbacks != 0 ||
				forwardGeometrySetupCalls != forwardGeometryRestoreCalls)
				return false;
			for (std::uint32_t index = 0; index < forwardExpectedGeometryCount; ++index) {
				if (!forwardExpectedGeometries[index] || !forwardExpectedShaderProperties[index])
					return false;
				const bool ordinaryRoute = forwardOrdinarySelectionCounts[index] == 1 &&
					forwardSpecialAlphaSelectionCounts[index] == 0 &&
					forwardNoPassSelectionCounts[index] == 0;
				const bool specialAlphaRoute = forwardOrdinarySelectionCounts[index] == 0 &&
					forwardSpecialAlphaSelectionCounts[index] == 1 &&
					forwardNoPassSelectionCounts[index] == 0;
				const bool noPassRoute = forwardOrdinarySelectionCounts[index] == 0 &&
					forwardSpecialAlphaSelectionCounts[index] == 0 &&
					forwardNoPassSelectionCounts[index] == 1;
				if (ordinaryRoute) {
					if (forwardSetupCounts[index] == 0 || forwardRestoreCounts[index] == 0 ||
						forwardSetupCounts[index] != forwardRestoreCounts[index] ||
						forwardPinnedRestoreCounts[index] != forwardRestoreCounts[index] ||
						forwardPolicyCompletedCounts[index] != forwardRestoreCounts[index] ||
						forwardSpecialAlphaCompletionCounts[index] != 0 ||
						forwardSpecialAlphaPinnedCounts[index] != 0)
						return false;
					++ordinaryGeometryCount;
					if (forwardExpectedRequiredEyes[index] != 0)
						++routedEyeGeometryCount;
				} else if (specialAlphaRoute) {
					if (forwardSetupCounts[index] != 0 || forwardRestoreCounts[index] != 0 ||
						forwardPinnedRestoreCounts[index] != 0 ||
						forwardPolicyCompletedCounts[index] != 0 ||
						forwardSpecialAlphaCompletionCounts[index] != 1 ||
						forwardSpecialAlphaPinnedCounts[index] != 1)
						return false;
					++specialAlphaGeometryCount;
					if (forwardExpectedRequiredEyes[index] != 0)
						++routedEyeGeometryCount;
				} else if (noPassRoute) {
					if (forwardSetupCounts[index] != 0 || forwardRestoreCounts[index] != 0 ||
						forwardPinnedRestoreCounts[index] != 0 ||
						forwardPolicyCompletedCounts[index] != 0 ||
						forwardSpecialAlphaCompletionCounts[index] != 0 ||
						forwardSpecialAlphaPinnedCounts[index] != 0)
						return false;
					++noPassGeometryCount;
				} else {
					return false;
				}
				++observedGeometryCount;
			}
			return observedGeometryCount == forwardExpectedGeometryCount &&
				ordinaryGeometryCount + specialAlphaGeometryCount + noPassGeometryCount ==
					forwardExpectedGeometryCount &&
				routedEyeGeometryCount != 0;
		}

		void ResourceTransaction::BeginPrepassDrawDiag(RE::BSRenderPass* pass) noexcept
		{
			if (!prepared || !context)
				return;
			const std::uint32_t index = prepassDrawDiag.drawsSeen;
			++prepassDrawDiag.drawsSeen;
			if (index >= PrepassDrawDiag::kMaxDraws)
				return;
			auto& draw = prepassDrawDiag.draws[index];
			if (pass) {
				draw.technique = pass->technique;
				if (auto* geometry = pass->geometry) {
					if (const char* name = geometry->name.c_str())
						std::snprintf(draw.geometryName, sizeof(draw.geometryName), "%s", name);
					draw.worldPosition[0] = geometry->world.translate.x;
					draw.worldPosition[1] = geometry->world.translate.y;
					draw.worldPosition[2] = geometry->world.translate.z;
					draw.boundRadius = geometry->worldBound.fRadius;
				}
			}
			std::array<void*, 4> frames{};
			const auto captured = RtlCaptureStackBackTrace(1, static_cast<ULONG>(frames.size()),
				frames.data(), nullptr);
			const auto moduleBase = REL::Module::get().base();
			for (std::uint16_t frame = 0; frame < captured && frame < frames.size(); ++frame) {
				const auto address = reinterpret_cast<std::uintptr_t>(frames[frame]);
				draw.callerRVAs[frame] = address >= moduleBase && address < moduleBase + 0x8000000 ?
				                             address - moduleBase :
				                             address;
			}
			draw.commitsAtSetup = prepassDrawDiag.commitsSeen;
			if (draw.occlusion)
				context->Begin(draw.occlusion.Get());
			if (draw.statistics)
				context->Begin(draw.statistics.Get());
			draw.began = draw.occlusion || draw.statistics;
		}

		void ResourceTransaction::EndPrepassDrawDiag() noexcept
		{
			if (!prepared || !context || prepassDrawDiag.drawsSeen == 0)
				return;
			const std::uint32_t index =
				std::min(prepassDrawDiag.drawsSeen, PrepassDrawDiag::kMaxDraws) - 1u;
			auto& draw = prepassDrawDiag.draws[index];
			if (draw.ended)
				return;
			draw.commitsAtRestore = prepassDrawDiag.commitsSeen;
			if (draw.began) {
				if (draw.statistics)
					context->End(draw.statistics.Get());
				if (draw.occlusion)
					context->End(draw.occlusion.Get());
				draw.ended = true;
			}
			ComPtr<ID3D11DepthStencilState> depthStencilState;
			UINT stencilReference = 0;
			context->OMGetDepthStencilState(depthStencilState.GetAddressOf(),
				std::addressof(stencilReference));
			if (depthStencilState) {
				D3D11_DEPTH_STENCIL_DESC stateDesc{};
				depthStencilState->GetDesc(std::addressof(stateDesc));
				draw.depthEnable = stateDesc.DepthEnable;
				draw.depthWriteMask = stateDesc.DepthWriteMask;
				draw.depthFunc = stateDesc.DepthFunc;
			}
			ComPtr<ID3D11DepthStencilView> boundDepthView;
			context->OMGetRenderTargets(0, nullptr, boundDepthView.GetAddressOf());
			if (boundDepthView) {
				D3D11_DEPTH_STENCIL_VIEW_DESC viewDesc{};
				boundDepthView->GetDesc(std::addressof(viewDesc));
				draw.dsvFlags = viewDesc.Flags;
				ComPtr<ID3D11Resource> boundResource;
				boundDepthView->GetResource(boundResource.GetAddressOf());
				ComPtr<ID3D11Texture2D> boundDepth;
				if (boundResource && SUCCEEDED(boundResource.As(&boundDepth)) && boundDepth) {
					draw.boundDepthIsLogical = boundDepth.Get() == sourceDepth.Get();

					if (mutated && resultDepth && boundDepth.Get() != resultDepth.Get() &&
						boundDepth.Get() == sourceDepth.Get()) {
						D3D11_TEXTURE2D_DESC boundDescription{};
						boundDepth->GetDesc(std::addressof(boundDescription));
						if (std::memcmp(std::addressof(boundDescription),
								std::addressof(sourceDepthDesc), sizeof(boundDescription)) == 0) {
							context->CopyResource(resultDepth.Get(), boundDepth.Get());
							prepassDepthCaptured = true;
						}
					}
				}
			}
			UINT viewportCount = 1;
			context->RSGetViewports(std::addressof(viewportCount), std::addressof(draw.viewport));
			ComPtr<ID3D11RasterizerState> rasterizerState;
			context->RSGetState(rasterizerState.GetAddressOf());
			if (rasterizerState) {
				D3D11_RASTERIZER_DESC rasterizerDesc{};
				rasterizerState->GetDesc(std::addressof(rasterizerDesc));
				draw.scissorEnable = rasterizerDesc.ScissorEnable;
				draw.cullMode = static_cast<std::uint32_t>(rasterizerDesc.CullMode);
				draw.frontCounterClockwise = rasterizerDesc.FrontCounterClockwise;
			}
			draw.stateCaptured = true;
		}

		void ResourceTransaction::ReadPrepassDrawDiag() noexcept
		{
			if (!context)
				return;
			const std::uint32_t drawCount =
				std::min(prepassDrawDiag.drawsSeen, PrepassDrawDiag::kMaxDraws);
			for (std::uint32_t index = 0; index < drawCount; ++index) {
				auto& draw = prepassDrawDiag.draws[index];
				if (!draw.ended)
					continue;

				if (!draw.occlusionValid && draw.occlusion &&
					context->GetData(draw.occlusion.Get(),
						std::addressof(draw.occlusionSamples),
						sizeof(draw.occlusionSamples), D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK)
					draw.occlusionValid = true;
				if (!draw.statisticsValid && draw.statistics &&
					context->GetData(draw.statistics.Get(),
						std::addressof(draw.statisticsData),
						sizeof(draw.statisticsData), D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK)
					draw.statisticsValid = true;
			}
			static std::atomic<std::uint32_t> diagLogs{ 0 };
			if (diagLogs.fetch_add(1, std::memory_order_relaxed) >= 12u)
				return;
			spdlog::info("[FlatDeferredPlayerCapture] prepass draw diag: drawsSeen={} commits={}",
				prepassDrawDiag.drawsSeen, prepassDrawDiag.commitsSeen);
			for (std::uint32_t index = 0; index < drawCount; ++index) {
				const auto& draw = prepassDrawDiag.draws[index];
				spdlog::info(
					"[FlatDeferredPlayerCapture] prepass draw[{}]: geo='{}' pos=({:.1f},{:.1f},{:.1f}) "
					"r={:.1f} tech=0x{:X} callers=0x{:X},0x{:X},0x{:X},0x{:X} "
					"occl={}({}) ia={}/{} vs={} c={}/{} ps={}({}) "
					"dss={}:{}:{} dsvFlags=0x{:X} dsvLogical={} vp={:.0f}x{:.0f}@{:.0f},{:.0f} "
					"scissor={} cull={} ccw={} commits={}..{} state={}",
					index, draw.geometryName, draw.worldPosition[0], draw.worldPosition[1],
					draw.worldPosition[2], draw.boundRadius, draw.technique, draw.callerRVAs[0],
					draw.callerRVAs[1], draw.callerRVAs[2], draw.callerRVAs[3],
					draw.occlusionSamples, draw.occlusionValid,
					draw.statisticsData.IAVertices, draw.statisticsData.IAPrimitives,
					draw.statisticsData.VSInvocations, draw.statisticsData.CInvocations,
					draw.statisticsData.CPrimitives, draw.statisticsData.PSInvocations,
					draw.statisticsValid, static_cast<int>(draw.depthEnable),
					static_cast<int>(draw.depthWriteMask), static_cast<int>(draw.depthFunc),
					draw.dsvFlags, draw.boundDepthIsLogical, draw.viewport.Width,
					draw.viewport.Height, draw.viewport.TopLeftX, draw.viewport.TopLeftY,
					static_cast<int>(draw.scissorEnable), draw.cullMode,
					static_cast<int>(draw.frontCounterClockwise), draw.commitsAtSetup,
					draw.commitsAtRestore, draw.stateCaptured);
			}
		}

		bool ResourceTransaction::CopyResult(Result& result, std::uint64_t& coveredPixels,
			std::uint64_t& mappedDepthPixels, std::uint64_t& informativeColorPixels,
			std::uint64_t& spatiallyVariantColorPixels, bool& depthReadbackCompleted,
			bool& colorReadbackCompleted, std::uint32_t& copyStage) noexcept
		{
			coveredPixels = 0;
			mappedDepthPixels = 0;
			informativeColorPixels = 0;
			spatiallyVariantColorPixels = 0;
			depthReadbackCompleted = false;
			colorReadbackCompleted = false;
			copyStage = 1u;
			if (!prepared || !mutated || !device || !context || !pool || !sourceColor || !sourceDepth ||
				!resultColor || !resultColorView || !resultDepth || !resultDepthView ||
				!finalForwardDepthCaptured || !pool->coverage.depthShader ||
				!pool->coverage.mappedDepthShader || !pool->coverage.informativeColorShader ||
				!pool->coverage.spatialVariationShader || !pool->coverage.counter ||
				!pool->coverage.counterView || !pool->coverage.readback || !OutputResourcesStable())
				return false;

			if (resultColor.Get() == sourceColor.Get() || resultDepth.Get() == sourceDepth.Get() ||
				resultColor.Get() == resultDepth.Get() || !ResourceUsesDevice(resultColor.Get(), device) ||
				!ResourceUsesDevice(resultDepth.Get(), device))
				return false;

			const auto depthPixelCount = static_cast<std::uint64_t>(sourceDepthDesc.Width) *
			                             static_cast<std::uint64_t>(sourceDepthDesc.Height);
			const auto colorPixelCount = static_cast<std::uint64_t>(sourceColorDesc.Width) *
			                             static_cast<std::uint64_t>(sourceColorDesc.Height);
			if (depthPixelCount == 0 || colorPixelCount == 0 ||
				depthPixelCount > std::numeric_limits<std::uint32_t>::max() ||
				colorPixelCount > std::numeric_limits<std::uint32_t>::max())
				return false;

			copyStage = 2u;
			context->CopyResource(resultColor.Get(), sourceColor.Get());
			constexpr std::array<UINT, 4> zero{};
			auto reduce = [&](ID3D11ComputeShader* shader,
				std::array<ID3D11ShaderResourceView*, 2> shaderResources,
				std::uint32_t width, std::uint32_t height, std::uint64_t& value) noexcept {
				context->ClearUnorderedAccessViewUint(pool->coverage.counterView.Get(), zero.data());
				ID3D11UnorderedAccessView* counterView = pool->coverage.counterView.Get();
				context->CSSetShader(shader, nullptr, 0);
				context->CSSetShaderResources(0, static_cast<UINT>(shaderResources.size()),
					shaderResources.data());
				context->CSSetUnorderedAccessViews(0, 1, std::addressof(counterView), nullptr);
				context->Dispatch((width + 15u) / 16u, (height + 15u) / 16u, 1);
				ID3D11UnorderedAccessView* nullCounter = nullptr;
				std::array<ID3D11ShaderResourceView*, 2> nullResources{};
				context->CSSetUnorderedAccessViews(0, 1, std::addressof(nullCounter), nullptr);
				context->CSSetShaderResources(0, static_cast<UINT>(nullResources.size()),
					nullResources.data());
				context->CSSetShader(nullptr, nullptr, 0);
				context->CopyResource(pool->coverage.readback.Get(), pool->coverage.counter.Get());
				D3D11_MAPPED_SUBRESOURCE mapped{};
				if (FAILED(context->Map(pool->coverage.readback.Get(), 0, D3D11_MAP_READ, 0,
						std::addressof(mapped))))
					return false;
				value = *static_cast<const std::uint32_t*>(mapped.pData);
				context->Unmap(pool->coverage.readback.Get(), 0);
				return true;
			};

			ID3D11ShaderResourceView* colorView = resultColorView.Get();
			ID3D11ShaderResourceView* depthView = resultDepthView.Get();
			copyStage = 3u;
			if (!reduce(pool->coverage.depthShader.Get(), { depthView, nullptr },
					sourceDepthDesc.Width, sourceDepthDesc.Height, coveredPixels))
				return false;
			depthReadbackCompleted = true;
			copyStage = 4u;
			if (!reduce(pool->coverage.mappedDepthShader.Get(), { colorView, depthView },
					sourceColorDesc.Width, sourceColorDesc.Height, mappedDepthPixels))
				return false;
			copyStage = 5u;
			if (!reduce(pool->coverage.informativeColorShader.Get(), { colorView, depthView },
					sourceColorDesc.Width, sourceColorDesc.Height, informativeColorPixels))
				return false;
			copyStage = 6u;
			if (!reduce(pool->coverage.spatialVariationShader.Get(), { colorView, depthView },
					sourceColorDesc.Width, sourceColorDesc.Height, spatiallyVariantColorPixels))
				return false;
			colorReadbackCompleted = true;
			copyStage = 7u;
			ReadForwardQueries();
			if (coveredPixels > depthPixelCount || mappedDepthPixels > colorPixelCount ||
				informativeColorPixels > mappedDepthPixels ||
				spatiallyVariantColorPixels > mappedDepthPixels)
				return false;

			const bool coveredDrawAuthority = forwardQueriesReadBack && coveredPixels != 0 &&
				forwardOcclusionSamples != 0 && forwardStatistics.VSInvocations != 0 &&
				forwardStatistics.PSInvocations != 0 &&
				mappedDepthPixels <= forwardOcclusionSamples;
			const bool structuralAbsenceAuthority = forwardQueriesReadBack && coveredPixels == 0 &&
				mappedDepthPixels == 0 && informativeColorPixels == 0 &&
				spatiallyVariantColorPixels == 0 && forwardOcclusionSamples == 0 &&
				forwardStatistics.VSInvocations != 0 && forwardStatistics.PSInvocations == 0;
			if (!coveredDrawAuthority && !structuralAbsenceAuthority)
				return false;
			result.color = resultColor;
			result.colorSRV = resultColorView;
			result.depth = resultDepth;
			result.depthSRV = resultDepthView;
			copyStage = 8u;
			return true;
		}

		bool ResourceTransaction::CopyNativeWorldResult(NativeWorldResult& result,
			std::uint64_t& coveredPixels, std::uint64_t& mappedDepthPixels,
			std::uint64_t& informativeColorPixels,
			std::uint64_t& spatiallyVariantColorPixels,
			bool& depthReadbackCompleted, bool& colorReadbackCompleted,
			std::uint32_t& copyStage) noexcept
		{
			coveredPixels = 0;
			mappedDepthPixels = 0;
			informativeColorPixels = 0;
			spatiallyVariantColorPixels = 0;
			depthReadbackCompleted = false;
			colorReadbackCompleted = false;
			copyStage = 1u;
			if (!prepared || !mutated || !prepassDepthCaptured || !device || !context || !pool || !sourceColor ||
				!sourceDepth || !resultColor || !resultColorView || !resultDepth ||
				!resultDepthView || !pool->coverage.nativeWorldAuthorityShader ||
				!pool->coverage.nativeWorldCounters ||
				!pool->coverage.nativeWorldCounterView ||
				!pool->coverage.nativeWorldReadback || !OutputResourcesStable())
				return false;
			if (resultColor.Get() == sourceColor.Get() || resultDepth.Get() == sourceDepth.Get() ||
				resultColor.Get() == resultDepth.Get() ||
				!ResourceUsesDevice(resultColor.Get(), device) ||
				!ResourceUsesDevice(resultDepth.Get(), device) ||
				!ViewUsesResource(resultColorView.Get(), resultColor.Get()) ||
				!ViewUsesResource(resultDepthView.Get(), resultDepth.Get()))
				return false;

			const auto colorPixels = static_cast<std::uint64_t>(sourceColorDesc.Width) *
			                         static_cast<std::uint64_t>(sourceColorDesc.Height);
			const auto depthPixels = static_cast<std::uint64_t>(sourceDepthDesc.Width) *
			                         static_cast<std::uint64_t>(sourceDepthDesc.Height);
			if (colorPixels == 0 || depthPixels == 0 ||
				colorPixels > std::numeric_limits<std::uint32_t>::max() ||
				depthPixels > std::numeric_limits<std::uint32_t>::max())
				return false;

			copyStage = 2u;
			context->CopyResource(resultColor.Get(), sourceColor.Get());

			constexpr std::array<UINT, 4> zero{};
			context->ClearUnorderedAccessViewUint(
				pool->coverage.nativeWorldCounterView.Get(), zero.data());
			std::array<ID3D11ShaderResourceView*, 2> shaderResources{
				resultColorView.Get(), resultDepthView.Get()
			};
			ID3D11UnorderedAccessView* counterView =
				pool->coverage.nativeWorldCounterView.Get();
			context->CSSetShader(pool->coverage.nativeWorldAuthorityShader.Get(), nullptr, 0);
			context->CSSetShaderResources(0, static_cast<UINT>(shaderResources.size()),
				shaderResources.data());
			context->CSSetUnorderedAccessViews(0, 1, std::addressof(counterView), nullptr);
			const auto dispatchWidth = (std::max)(sourceColorDesc.Width, sourceDepthDesc.Width);
			const auto dispatchHeight = (std::max)(sourceColorDesc.Height, sourceDepthDesc.Height);
			context->Dispatch((dispatchWidth + 15u) / 16u, (dispatchHeight + 15u) / 16u, 1);
			ID3D11UnorderedAccessView* nullCounter = nullptr;
			std::array<ID3D11ShaderResourceView*, 2> nullResources{};
			context->CSSetUnorderedAccessViews(0, 1, std::addressof(nullCounter), nullptr);
			context->CSSetShaderResources(0, static_cast<UINT>(nullResources.size()),
				nullResources.data());
			context->CSSetShader(nullptr, nullptr, 0);

			copyStage = 3u;
			context->CopyResource(pool->coverage.nativeWorldReadback.Get(),
				pool->coverage.nativeWorldCounters.Get());
			result.color = resultColor;
			result.colorSRV = resultColorView;
			result.depth = resultDepth;
			result.depthSRV = resultDepthView;
			copyStage = 4u;
			return true;
		}

		enum class NativeWorldAuthorityPollState : std::uint32_t
		{
			kPending,
			kReady,
			kFailed
		};

		[[nodiscard]] NativeWorldAuthorityPollState TryPollNativeWorldAuthority(
			PendingNativeWorldAuthority& pending,
			std::array<std::uint32_t, 4>& counters) noexcept
		{
			counters = {};
			if (!pending.valid || !pending.context || !pending.pool ||
				!pending.pool->coverage.nativeWorldReadback)
				return NativeWorldAuthorityPollState::kFailed;
			D3D11_MAPPED_SUBRESOURCE mapped{};
			const HRESULT mappedResult = pending.context->Map(
				pending.pool->coverage.nativeWorldReadback.Get(), 0, D3D11_MAP_READ,
				D3D11_MAP_FLAG_DO_NOT_WAIT, std::addressof(mapped));
			if (mappedResult == DXGI_ERROR_WAS_STILL_DRAWING)
				return NativeWorldAuthorityPollState::kPending;
			if (FAILED(mappedResult) || !mapped.pData)
				return NativeWorldAuthorityPollState::kFailed;
			std::memcpy(counters.data(), mapped.pData, sizeof(counters));
			pending.context->Unmap(pending.pool->coverage.nativeWorldReadback.Get(), 0);
			if (counters[0] > pending.depthPixelCount ||
				counters[1] > pending.colorPixelCount || counters[2] > counters[1] ||
				counters[3] > counters[1])
				return NativeWorldAuthorityPollState::kFailed;
			return NativeWorldAuthorityPollState::kReady;
		}

		void ResourceTransaction::CommitResult() noexcept
		{
			if (pool && candidateResultSlot < pool->resultSlots.size()) {
				pool->committedResultSlot = candidateResultSlot;
				pool->avoidNextResultSlot = std::numeric_limits<std::uint32_t>::max();
			}
		}

		bool ResourceTransaction::OutputResourcesStable() const noexcept
		{
			if (!prepared || !rendererData || !sourceColor || !sourceColorView || !sourceColorRTV ||
				!sourceDepth || !sourceDepthView || !writableDepthView ||
				colorPhysical >= kPhysicalColorCount || depthPhysical >= kPhysicalDepthCount)
				return false;
			const auto& color = rendererData->renderTargets[colorPhysical];
			const auto& depth = rendererData->depthStencilTargets[depthPhysical];
			return reinterpret_cast<ID3D11Texture2D*>(color.texture) == sourceColor.Get() &&
			       reinterpret_cast<ID3D11ShaderResourceView*>(color.srView) == sourceColorView.Get() &&
			       reinterpret_cast<ID3D11RenderTargetView*>(color.rtView) == sourceColorRTV.Get() &&
			       reinterpret_cast<ID3D11Texture2D*>(depth.texture) == sourceDepth.Get() &&
			       reinterpret_cast<ID3D11ShaderResourceView*>(depth.srViewDepth) == sourceDepthView.Get() &&
			       reinterpret_cast<ID3D11DepthStencilView*>(depth.dsView[0]) == writableDepthView.Get();
		}

		bool ResourceTransaction::MappingsStable() noexcept
		{
			mappingMismatchKind = 0;
			mappingMismatchLogical = 0;
			mappingMismatchExpected = 0;
			mappingMismatchObserved = 0;
			if (!prepared || !targetManager)
				return false;
			std::array<std::uint32_t, kRenderTargetMappingCount> currentColor{};
			std::array<std::uint32_t, kDepthTargetMappingCount> currentDepth{};
			if (!SafeCopy(currentColor.data(), ReflectionRuntime::RenderTargetIds(targetManager), sizeof(currentColor)) ||
				!SafeCopy(currentDepth.data(), ReflectionRuntime::DepthStencilTargetIds(targetManager), sizeof(currentDepth))) {
				mappingMismatchKind = 1;
				return false;
			}
			for (std::uint32_t logical = 0; logical < currentColor.size(); ++logical) {
				if (currentColor[logical] == colorMappings[logical])
					continue;
				mappingMismatchKind = 2;
				mappingMismatchLogical = logical;
				mappingMismatchExpected = colorMappings[logical];
				mappingMismatchObserved = currentColor[logical];
				RecordMappingTransition(targetManager, frameSerial, mappingMismatchKind,
					mappingMismatchLogical, mappingMismatchExpected, mappingMismatchObserved);
				return false;
			}
			for (std::uint32_t logical = 0; logical < currentDepth.size(); ++logical) {
				if (currentDepth[logical] == depthMappings[logical])
					continue;
				mappingMismatchKind = 3;
				mappingMismatchLogical = logical;
				mappingMismatchExpected = depthMappings[logical];
				mappingMismatchObserved = currentDepth[logical];
				RecordMappingTransition(targetManager, frameSerial, mappingMismatchKind,
					mappingMismatchLogical, mappingMismatchExpected, mappingMismatchObserved);
				return false;
			}
			for (const auto& binding : colorBindings) {
				if (binding.logical >= currentColor.size() || currentColor[binding.logical] != binding.physical ||
					binding.physical >= kPhysicalColorCount) {
					mappingMismatchKind = 4;
					mappingMismatchLogical = binding.logical;
					mappingMismatchExpected = binding.physical;
					mappingMismatchObserved = binding.logical < currentColor.size() ? currentColor[binding.logical] : ~0u;
					return false;
				}
				const auto& target = rendererData->renderTargets[binding.physical];
				if (reinterpret_cast<ID3D11Texture2D*>(target.texture) != binding.texture.Get() ||
					reinterpret_cast<ID3D11RenderTargetView*>(target.rtView) != binding.renderView.Get()) {
					mappingMismatchKind = 5;
					mappingMismatchLogical = binding.logical;
					return false;
				}
			}
			if (depthPhysical >= kPhysicalDepthCount ||
				reinterpret_cast<ID3D11Texture2D*>(rendererData->depthStencilTargets[depthPhysical].texture) !=
					sourceDepth.Get() ||
				reinterpret_cast<ID3D11ShaderResourceView*>(
					rendererData->depthStencilTargets[depthPhysical].srViewDepth) != sourceDepthView.Get() ||
				reinterpret_cast<ID3D11DepthStencilView*>(
					rendererData->depthStencilTargets[depthPhysical].dsView[0]) != writableDepthView.Get() ||
				reinterpret_cast<ID3D11ShaderResourceView*>(
					rendererData->renderTargets[colorPhysical].srView) != sourceColorView.Get() ||
				reinterpret_cast<ID3D11RenderTargetView*>(
					rendererData->renderTargets[colorPhysical].rtView) != sourceColorRTV.Get())
			{
				mappingMismatchKind = 6;
				return false;
			}
			for (const auto& identity : contextBufferIdentities) {
				ID3D11Buffer* current = nullptr;
				if (!identity.slot || !SafeLoad(reinterpret_cast<std::uintptr_t>(identity.slot), current) ||
					current != identity.buffer.Get())
				{

					if (!requireStableContextBufferIdentities && identity.rotating &&
						identity.slot && current &&
						ResourceUsesDevice(current, device)) {
						static std::atomic<std::uint32_t> rotationLogs{ 0 };
						if (rotationLogs.fetch_add(1, std::memory_order_relaxed) < 8u)
							spdlog::info(
								"[FlatDeferredPlayerCapture] rotating context slot tag=0x{:X} accepted: old=0x{:X} new=0x{:X}",
								identity.tag,
								reinterpret_cast<std::uintptr_t>(identity.buffer.Get()),
								reinterpret_cast<std::uintptr_t>(current));
						continue;
					}

					mappingMismatchKind = 7;
					mappingMismatchLogical = identity.tag;
					mappingMismatchExpected = static_cast<std::uint32_t>(
						reinterpret_cast<std::uintptr_t>(identity.buffer.Get()));
					mappingMismatchObserved = static_cast<std::uint32_t>(
						reinterpret_cast<std::uintptr_t>(current));
					return false;
				}
			}
			return true;
		}

		bool ResourceTransaction::Restore() noexcept
		{
			if (!prepared || !context)
				return false;

			restoreMappingsStable = MappingsStable();
			bool resourcesRestored = true;
			for (const auto& texture : textures)
				context->CopyResource(texture.engineTexture.Get(), texture.backupTexture.Get());
			dynamicBufferRoundTripsRestored = true;
			dynamicAsyncRestoreCopyCount = 0;
			for (const auto& buffer : buffers) {
				if (!buffer.dynamicRoundTrip) {
					context->CopyResource(buffer.engineBuffer.Get(), buffer.backupBuffer.Get());
					continue;
				}

				context->CopyResource(buffer.engineBuffer.Get(), buffer.backupBuffer.Get());
				++dynamicAsyncRestoreCopyCount;
			}
			dynamicBufferRoundTripsRestored =
				dynamicAsyncRestoreCopyCount == dynamicBufferBackupCount;
			contextConstantGroupsRestored =
				contextConstantGroups.size() == kContextConstantGroupCount;
			for (const auto& identity : contextConstantGroups) {
				ID3D11Buffer* entryBuffer = nullptr;
				ID3D11Buffer* currentBuffer = nullptr;
				std::memcpy(std::addressof(entryBuffer), identity.pod.data(), sizeof(entryBuffer));
				const bool currentRead = identity.slot &&
					SafeLoad(reinterpret_cast<std::uintptr_t>(identity.slot), currentBuffer);
				const bool rotatedBufferBacked = !currentBuffer || currentBuffer == entryBuffer ||
					std::any_of(buffers.begin(), buffers.end(), [&](const BufferBackup& backup) {
						return backup.engineBuffer.Get() == currentBuffer;
					});

				if (!currentRead || !rotatedBufferBacked ||
					!SafeStore(identity.slot, identity.pod.data(), identity.pod.size())) {
					contextConstantGroupsRestored = false;
					continue;
				}
				std::array<std::byte, sizeof(RE::BSGraphics::ConstantGroup)> verified{};
				if (!SafeCopy(verified.data(), identity.slot, verified.size()) ||
					verified != identity.pod)
					contextConstantGroupsRestored = false;
			}
			restoreCopiesIssued = resourcesRestored && contextConstantGroupsRestored;
			return restoreMappingsStable && resourcesRestored &&
				contextConstantGroupsRestored;
		}

		struct EngineStateSnapshot
		{
			void* renderContext{};
			void* distantRenderer{};
			void* imageSpaceManager{};
			void* effect45{};
			void* effect46{};
			void* effect47{};
			void* effect43{};
			void* effectCF{};
			std::array<std::uint32_t, 4> effectCFAlphaPOD{};
			std::array<std::byte, kContextStateSize> shadowState{};
			std::array<std::byte, kContextStateSize> lastDrawState{};
			std::array<std::byte, kCameraStateSize> cameraState{};
			std::array<std::byte, kCameraCacheEntrySize * kMaxCameraCacheEntries> cameraCacheEntries{};
			void* cameraCacheData{};
			std::uint32_t cameraCacheCapacity{};
			std::uint32_t cameraCacheSize{};
			std::array<void*, 16> defaultTextureIdentities{};
			std::array<float, 18> directionalAmbientColors{};
			std::array<float, 3> directionalAmbientMainColor{};
			float directionalAmbientScale{};
			void* cameraStateReference{};
			void* accumulatorCamera{};
			void* accumulatorShadowSceneNode{};
			std::array<float, 3> accumulatorCameraEye{};
			std::uint32_t accumulatorRenderMode{};
			std::uint8_t accumulatorForwardFlag{};
			void* currentAccumulator{};
			void* shaderCamera{};
			std::array<void*, kShadowSceneNodeSlotCount> activeSSNSlots{};
			float global1CDC{};
			std::uint8_t global1BC6{};
			std::uint8_t global1BC7{};
			std::uint8_t global1BC8{};
			std::uint32_t global1BCC{};
			std::uint8_t global1C19{};
			std::uint8_t global1F55{};
			std::uint32_t interfaceDisplayGeometry{};
			std::uint32_t interfaceGlobal1D34{};
			float interfacePostAA{};
			float interfaceOpacityAlpha{};
			float interfaceMenuEmitIntensity{};
			float interfaceMenuDiffuseIntensity{};
			std::uint8_t asyncAO{};
			std::uint8_t hbaoEnabled{};
			std::uint8_t effect45Active{};
			std::uint8_t effect46Active{};
			std::uint8_t effect47Active{};
			std::uint8_t effect47Primary{};
			std::uint8_t effect47Secondary{};
			std::uint8_t distantEnabled{};
			bool captured{};
			bool hbaoCallbackCompleted{};
			bool directionalAmbientCallbackCompleted{};

			[[nodiscard]] bool Capture(void* accumulator) noexcept;
			[[nodiscard]] bool ContainsCachedCamera(const void* camera, bool selector) const noexcept;
			[[nodiscard]] bool RestoreRaw(void* accumulator) noexcept;
			[[nodiscard]] bool VerifyRaw(void* accumulator) const noexcept;
		};

		[[nodiscard]] bool CaptureDirectionalAmbientLeaf(EngineStateSnapshot& state) noexcept
		{
			__try {
				reinterpret_cast<void (*)(void*, bool)>(Address(kRVA_GetDirectionalAmbientColors))(
					state.directionalAmbientColors.data(), true);
				const auto* raw = reinterpret_cast<const float*>(Address(kRVA_DirectionalAmbientState));
				state.directionalAmbientMainColor = { raw[0], raw[1], raw[2] };
				state.directionalAmbientScale = raw[3];
				return true;
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				return false;
			}
		}

		[[nodiscard]] bool RestoreDirectionalAmbientLeaf(EngineStateSnapshot& state) noexcept
		{
			__try {
				reinterpret_cast<void (*)(void*, void*, float)>(Address(kRVA_SetDirectionalAmbientColors))(
					state.directionalAmbientColors.data(), state.directionalAmbientMainColor.data(),
					state.directionalAmbientScale);
				state.directionalAmbientCallbackCompleted = true;
				return true;
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				return false;
			}
		}

		[[nodiscard]] bool NotifyHBAORestoreLeaf(EngineStateSnapshot& state) noexcept
		{
			__try {
				reinterpret_cast<void (*)(void*)>(Address(kRVA_HBAOSettingUpdated))(
					reinterpret_cast<void*>(Address(kRVA_HBAOSetting)));
				state.hbaoCallbackCompleted = true;
				return true;
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				return false;
			}
		}

		[[nodiscard]] void* ResolveRenderContextLeaf() noexcept
		{
			__try {
				const auto tlsIndex = *reinterpret_cast<const std::uint32_t*>(Address(kRVA_GameTLSIndex));
				auto* tlsArray = reinterpret_cast<void**>(__readgsqword(0x58));
				void* gameTLS = tlsArray ? tlsArray[tlsIndex] : nullptr;
				void* context = gameTLS ?
				                    *reinterpret_cast<void**>(reinterpret_cast<std::byte*>(gameTLS) + 0xB20) :
				                    nullptr;
				if (!context)
					context = *reinterpret_cast<void**>(Address(kRVA_DefaultContext));
				return context;
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				return nullptr;
			}
		}

		[[nodiscard]] void* ResolveDistantRendererLeaf(std::uint32_t& exceptionCode) noexcept
		{
			__try {
				using Function = void* (*)();
				return reinterpret_cast<Function>(Address(kRVA_DistantRendererSingleton))();
			} __except (exceptionCode = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER) {
				return nullptr;
			}
		}

		bool EngineStateSnapshot::Capture(void* accumulator) noexcept
		{
			if (!accumulator)
				return false;
			renderContext = ResolveRenderContextLeaf();
			std::uint32_t exceptionCode = 0;
			distantRenderer = ResolveDistantRendererLeaf(exceptionCode);
			if (!renderContext || !distantRenderer || exceptionCode != 0)
				return false;

			if (!SafeCopy(shadowState.data(),
					reinterpret_cast<std::byte*>(renderContext) + kContextShadowStateOffset,
					shadowState.size()) ||
				!SafeCopy(lastDrawState.data(),
					reinterpret_cast<std::byte*>(renderContext) + kContextLastDrawStateOffset,
					lastDrawState.size()) ||
				!SafeCopy(cameraState.data(),
					reinterpret_cast<void*>(Address(kRVA_GraphicsState) + kCameraStateOffset),
					cameraState.size()) ||
				!SafeLoad(Address(kRVA_GraphicsState) + kCameraCacheOffset, cameraCacheData) ||
				!SafeLoad(Address(kRVA_GraphicsState) + kCameraCacheOffset + 0x08,
					cameraCacheCapacity) ||
				!SafeLoad(Address(kRVA_GraphicsState) + kCameraCacheOffset + 0x10,
					cameraCacheSize) ||
				!SafeCopy(defaultTextureIdentities.data(),
					reinterpret_cast<void*>(Address(kRVA_GraphicsState) + 0xB8),
					sizeof(defaultTextureIdentities)) ||
				!SafeLoad(reinterpret_cast<std::uintptr_t>(accumulator) + kAccumulatorCameraOffset,
					accumulatorCamera) ||
				!SafeLoad(reinterpret_cast<std::uintptr_t>(accumulator) +
							  kAccumulatorShadowSceneNodeOffset,
					accumulatorShadowSceneNode) ||
				!SafeLoad(reinterpret_cast<std::uintptr_t>(accumulator) +
							  kAccumulatorRenderModeOffset,
					accumulatorRenderMode) ||
				!SafeCopy(accumulatorCameraEye.data(),
					reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(accumulator) +
						kAccumulatorCameraEyeOffset), sizeof(accumulatorCameraEye)) ||
				!SafeLoad(reinterpret_cast<std::uintptr_t>(accumulator) +
							  kAccumulatorForwardFlagOffset,
					accumulatorForwardFlag) ||
				!SafeLoad(Address(kRVA_CurrentAccumulator), currentAccumulator) ||
				!SafeLoad(Address(kRVA_ShaderCamera), shaderCamera) ||
				!SafeCopy(activeSSNSlots.data(), reinterpret_cast<void*>(Address(kRVA_ActiveShadowSceneNode)),
					sizeof(activeSSNSlots)) ||
				!SafeLoad(Address(kRVA_Global1CDC), global1CDC) ||
				!SafeLoad(Address(kRVA_Global1BC6), global1BC6) ||
				!SafeLoad(Address(kRVA_Global1BC7), global1BC7) ||
				!SafeLoad(Address(kRVA_Global1BC8), global1BC8) ||
				!SafeLoad(Address(kRVA_Global1BCC), global1BCC) ||
				!SafeLoad(Address(kRVA_Global1C19), global1C19) ||
				!SafeLoad(Address(kRVA_Global1F55), global1F55) ||
				!SafeLoad(Address(kRVA_InterfaceDisplayGeometry), interfaceDisplayGeometry) ||
				!SafeLoad(Address(kRVA_InterfaceGlobal1D34), interfaceGlobal1D34) ||
				!SafeLoad(Address(kRVA_InterfacePostAA), interfacePostAA) ||
				!SafeLoad(Address(kRVA_InterfaceOpacityAlpha), interfaceOpacityAlpha) ||
				!SafeLoad(Address(kRVA_InterfaceMenuEmitIntensity), interfaceMenuEmitIntensity) ||
				!SafeLoad(Address(kRVA_InterfaceMenuDiffuseIntensity), interfaceMenuDiffuseIntensity) ||
				!SafeLoad(Address(kRVA_AsyncAO), asyncAO) ||
				!SafeLoad(Address(kRVA_HBAOEnabled), hbaoEnabled) ||
				!SafeLoad(reinterpret_cast<std::uintptr_t>(distantRenderer) + kDistantRendererEnabledOffset,
					distantEnabled))
				return false;
			if (cameraCacheSize > cameraCacheCapacity ||
				cameraCacheSize > kMaxCameraCacheEntries ||
				(cameraCacheSize != 0 && (!cameraCacheData ||
											 !SafeCopy(cameraCacheEntries.data(), cameraCacheData,
												 static_cast<std::size_t>(cameraCacheSize) * kCameraCacheEntrySize))))
				return false;
			if (!SafeLoad(Address(kRVA_GraphicsState) + kCameraStateOffset + 0x238,
					cameraStateReference) ||
				!CaptureDirectionalAmbientLeaf(*this))
				return false;

			if (!SafeLoad(Address(kRVA_ImageSpaceManager), imageSpaceManager) || !imageSpaceManager)
				return false;
			void** effectArray = nullptr;
			if (!SafeLoad(reinterpret_cast<std::uintptr_t>(imageSpaceManager) +
							  kImageSpaceEffectArrayOffset,
					effectArray) ||
				!effectArray ||
				!SafeLoad(reinterpret_cast<std::uintptr_t>(effectArray + 0x43), effect43) || !effect43 ||
				!SafeLoad(reinterpret_cast<std::uintptr_t>(effectArray + 0x45), effect45) || !effect45 ||
				!SafeLoad(reinterpret_cast<std::uintptr_t>(effectArray + 0x46), effect46) || !effect46 ||
				!SafeLoad(reinterpret_cast<std::uintptr_t>(effectArray + 0x47), effect47) || !effect47 ||
				!SafeLoad(reinterpret_cast<std::uintptr_t>(effectArray + 0xCF), effectCF) || !effectCF ||
				!SafeCopy(effectCFAlphaPOD.data(),
					reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(effectCF) + kEffectCFPODOffset),
					sizeof(effectCFAlphaPOD)) ||
				!SafeLoad(reinterpret_cast<std::uintptr_t>(effect45) + kEffectActiveOffset,
					effect45Active) ||
				!SafeLoad(reinterpret_cast<std::uintptr_t>(effect46) + kEffectActiveOffset,
					effect46Active) ||
				!SafeLoad(reinterpret_cast<std::uintptr_t>(effect47) + kEffectActiveOffset,
					effect47Active) ||
				!SafeLoad(reinterpret_cast<std::uintptr_t>(effect47) + kEffect47PrimaryOffset,
					effect47Primary) ||
				!SafeLoad(reinterpret_cast<std::uintptr_t>(effect47) + kEffect47SecondaryOffset,
					effect47Secondary))
				return false;

			captured = true;
			return true;
		}

		bool EngineStateSnapshot::ContainsCachedCamera(const void* camera, bool selector) const noexcept
		{
			if (!captured || !camera)
				return false;
			for (std::uint32_t index = 0; index < cameraCacheSize; ++index) {
				void* cachedCamera = nullptr;
				std::memcpy(std::addressof(cachedCamera),
					cameraCacheEntries.data() + static_cast<std::size_t>(index) * kCameraCacheEntrySize + 0x238,
					sizeof(cachedCamera));
				const auto cachedSelector = static_cast<std::uint8_t>(cameraCacheEntries[static_cast<std::size_t>(index) * kCameraCacheEntrySize + 0x240]);
				if (cachedCamera == camera && cachedSelector == static_cast<std::uint8_t>(selector))
					return true;
			}
			return false;
		}

		bool EngineStateSnapshot::RestoreRaw(void* accumulator) noexcept
		{
			if (!captured || !accumulator || !renderContext || !distantRenderer)
				return false;
			bool restored = true;

			std::uint32_t restoreStep = 0;
			std::uint32_t firstFailedStep = 0;
			auto track = [&](bool ok) noexcept {
				++restoreStep;
				if (!ok && firstFailedStep == 0)
					firstFailedStep = restoreStep;
				return ok;
			};
			void* currentCacheData = nullptr;
			std::uint32_t currentCacheCapacity = 0;
			std::uint32_t currentCacheSize = 0;
			restored &= track(SafeLoad(Address(kRVA_GraphicsState) + kCameraCacheOffset, currentCacheData));
			restored &= track(SafeLoad(Address(kRVA_GraphicsState) + kCameraCacheOffset + 0x08,
				currentCacheCapacity));
			restored &= track(SafeLoad(Address(kRVA_GraphicsState) + kCameraCacheOffset + 0x10,
				currentCacheSize));
			const bool cacheStable = currentCacheData == cameraCacheData &&
			                         currentCacheCapacity == cameraCacheCapacity && currentCacheSize == cameraCacheSize;
			restored &= track(cacheStable);
			if (cacheStable && cameraCacheSize != 0)
				restored &= track(SafeStore(cameraCacheData, cameraCacheEntries.data(),
					static_cast<std::size_t>(cameraCacheSize) * kCameraCacheEntrySize));
			restored &= track(SafeStore(reinterpret_cast<void*>(Address(kRVA_GraphicsState) + kCameraStateOffset),
				cameraState.data(), cameraState.size()));
			restored &= track(SafeStore(reinterpret_cast<std::byte*>(renderContext) + kContextShadowStateOffset,
				shadowState.data(), shadowState.size()));
			restored &= track(SafeStore(reinterpret_cast<std::byte*>(renderContext) + kContextLastDrawStateOffset,
				lastDrawState.data(), lastDrawState.size()));

			restored &= track(SafeWrite(
				reinterpret_cast<std::uintptr_t>(accumulator) + kAccumulatorCameraOffset,
				accumulatorCamera));
			restored &= track(SafeWrite(
				reinterpret_cast<std::uintptr_t>(accumulator) + kAccumulatorShadowSceneNodeOffset,
				accumulatorShadowSceneNode));
			restored &= track(SafeWrite(
				reinterpret_cast<std::uintptr_t>(accumulator) + kAccumulatorRenderModeOffset,
				accumulatorRenderMode));
			restored &= track(SafeStore(
				reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(accumulator) +
					kAccumulatorCameraEyeOffset), accumulatorCameraEye.data(),
				sizeof(accumulatorCameraEye)));
			restored &= track(SafeWrite(
				reinterpret_cast<std::uintptr_t>(accumulator) + kAccumulatorForwardFlagOffset,
				accumulatorForwardFlag));
			restored &= track(SafeWrite(Address(kRVA_CurrentAccumulator), currentAccumulator));
			restored &= track(SafeWrite(Address(kRVA_ShaderCamera), shaderCamera));
			restored &= track(SafeStore(reinterpret_cast<void*>(Address(kRVA_ActiveShadowSceneNode)),
				activeSSNSlots.data(), sizeof(activeSSNSlots)));
			restored &= track(SafeWrite(Address(kRVA_Global1CDC), global1CDC));
			restored &= track(SafeWrite(Address(kRVA_Global1BC6), global1BC6));
			restored &= track(SafeWrite(Address(kRVA_Global1BC7), global1BC7));
			restored &= track(SafeWrite(Address(kRVA_Global1BC8), global1BC8));
			restored &= track(SafeWrite(Address(kRVA_Global1BCC), global1BCC));
			restored &= track(SafeWrite(Address(kRVA_Global1C19), global1C19));
			restored &= track(SafeWrite(Address(kRVA_Global1F55), global1F55));
			restored &= track(SafeWrite(Address(kRVA_InterfaceDisplayGeometry), interfaceDisplayGeometry));
			restored &= track(SafeWrite(Address(kRVA_InterfaceGlobal1D34), interfaceGlobal1D34));
			restored &= track(SafeWrite(Address(kRVA_InterfacePostAA), interfacePostAA));
			restored &= track(SafeWrite(Address(kRVA_InterfaceOpacityAlpha), interfaceOpacityAlpha));
			restored &= track(SafeWrite(Address(kRVA_InterfaceMenuEmitIntensity), interfaceMenuEmitIntensity));
			restored &= track(SafeWrite(Address(kRVA_InterfaceMenuDiffuseIntensity), interfaceMenuDiffuseIntensity));
			restored &= track(SafeWrite(Address(kRVA_AsyncAO), asyncAO));
			restored &= track(SafeWrite(Address(kRVA_HBAOEnabled), hbaoEnabled));
			restored &= track(NotifyHBAORestoreLeaf(*this));
			restored &= track(RestoreDirectionalAmbientLeaf(*this));
			restored &= track(SafeWrite(reinterpret_cast<std::uintptr_t>(effect45) + kEffectActiveOffset,
				effect45Active));
			restored &= track(SafeWrite(reinterpret_cast<std::uintptr_t>(effect46) + kEffectActiveOffset,
				effect46Active));
			restored &= track(SafeWrite(reinterpret_cast<std::uintptr_t>(effect47) + kEffectActiveOffset,
				effect47Active));
			restored &= track(SafeWrite(reinterpret_cast<std::uintptr_t>(effect47) + kEffect47PrimaryOffset,
				effect47Primary));
			restored &= track(SafeWrite(reinterpret_cast<std::uintptr_t>(effect47) + kEffect47SecondaryOffset,
				effect47Secondary));
			restored &= track(SafeStore(
				reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(effectCF) + kEffectCFPODOffset),
				effectCFAlphaPOD.data(), sizeof(effectCFAlphaPOD)));
			restored &= track(SafeWrite(reinterpret_cast<std::uintptr_t>(distantRenderer) +
									  kDistantRendererEnabledOffset,
				distantEnabled));
			if (!restored) {
				static std::atomic<std::uint32_t> restoreRawFailLogs{ 0 };
				if (restoreRawFailLogs.fetch_add(1, std::memory_order_relaxed) < 16u)
					spdlog::warn(
						"[FlatDeferredPlayerCapture] RestoreRaw first failed step={} cacheStable={} "
						"cacheData={:X}/{:X} cacheCap={}/{} cacheSize={}/{}",
						firstFailedStep, cacheStable,
						reinterpret_cast<std::uintptr_t>(currentCacheData),
						reinterpret_cast<std::uintptr_t>(cameraCacheData),
						currentCacheCapacity, cameraCacheCapacity, currentCacheSize, cameraCacheSize);
			}
			return restored;
		}

		bool EngineStateSnapshot::VerifyRaw(void* accumulator) const noexcept
		{
			auto verifyEarlyFail = [](const char* reason) noexcept {
				static std::atomic<std::uint32_t> verifyEarlyLogs{ 0 };
				if (verifyEarlyLogs.fetch_add(1, std::memory_order_relaxed) < 16u)
					spdlog::warn("[FlatDeferredPlayerCapture] VerifyRaw early exit: {}", reason);
				return false;
			};
			if (!captured || !accumulator)
				return verifyEarlyFail("precondition");
			EngineStateSnapshot current{};
			if (!current.Capture(accumulator))
				return verifyEarlyFail("re-capture failed");
			struct MemberCheck
			{
				bool equal;
				const char* name;
			};
			const MemberCheck checks[] = {
				{ current.renderContext == renderContext, "renderContext" },
				{ current.distantRenderer == distantRenderer, "distantRenderer" },
				{ current.imageSpaceManager == imageSpaceManager, "imageSpaceManager" },
				{ current.effect43 == effect43, "effect43" },
				{ current.effect45 == effect45, "effect45" },
				{ current.effect46 == effect46, "effect46" },
				{ current.effect47 == effect47, "effect47" },
				{ current.effectCF == effectCF, "effectCF" },
				{ current.effectCFAlphaPOD == effectCFAlphaPOD, "effectCFAlphaPOD" },
				{ current.shadowState == shadowState, "shadowState" },
				{ current.lastDrawState == lastDrawState, "lastDrawState" },
				{ current.cameraState == cameraState, "cameraState" },
				{ current.cameraCacheData == cameraCacheData, "cameraCacheData" },
				{ current.cameraCacheCapacity == cameraCacheCapacity, "cameraCacheCapacity" },
				{ current.cameraCacheSize == cameraCacheSize, "cameraCacheSize" },
				{ current.cameraCacheEntries == cameraCacheEntries, "cameraCacheEntries" },
				{ current.defaultTextureIdentities == defaultTextureIdentities, "defaultTextureIdentities" },
				{ current.cameraStateReference == cameraStateReference, "cameraStateReference" },
				{ current.directionalAmbientColors == directionalAmbientColors, "directionalAmbientColors" },
				{ current.directionalAmbientMainColor == directionalAmbientMainColor, "directionalAmbientMainColor" },
				{ current.directionalAmbientScale == directionalAmbientScale, "directionalAmbientScale" },
				{ current.accumulatorCamera == accumulatorCamera, "accumulatorCamera" },
				{ current.accumulatorShadowSceneNode == accumulatorShadowSceneNode,
					"accumulatorShadowSceneNode" },
				{ current.accumulatorCameraEye == accumulatorCameraEye, "accumulatorCameraEye" },
				{ current.accumulatorRenderMode == accumulatorRenderMode, "accumulatorRenderMode" },
				{ current.accumulatorForwardFlag == accumulatorForwardFlag,
					"accumulatorForwardFlag" },
				{ current.currentAccumulator == currentAccumulator, "currentAccumulator" },
				{ current.shaderCamera == shaderCamera, "shaderCamera" },
				{ current.activeSSNSlots == activeSSNSlots, "activeSSNSlots" },
				{ current.global1CDC == global1CDC, "global1CDC" },
				{ current.global1BC6 == global1BC6, "global1BC6" },
				{ current.global1BC7 == global1BC7, "global1BC7" },
				{ current.global1BC8 == global1BC8, "global1BC8" },
				{ current.global1BCC == global1BCC, "global1BCC" },
				{ current.global1C19 == global1C19, "global1C19" },
				{ current.global1F55 == global1F55, "global1F55" },
				{ current.interfaceDisplayGeometry == interfaceDisplayGeometry, "interfaceDisplayGeometry" },
				{ current.interfaceGlobal1D34 == interfaceGlobal1D34, "interfaceGlobal1D34" },
				{ current.interfacePostAA == interfacePostAA, "interfacePostAA" },
				{ current.interfaceOpacityAlpha == interfaceOpacityAlpha, "interfaceOpacityAlpha" },
				{ current.interfaceMenuEmitIntensity == interfaceMenuEmitIntensity, "interfaceMenuEmitIntensity" },
				{ current.interfaceMenuDiffuseIntensity == interfaceMenuDiffuseIntensity, "interfaceMenuDiffuseIntensity" },
				{ current.asyncAO == asyncAO, "asyncAO" },
				{ current.hbaoEnabled == hbaoEnabled, "hbaoEnabled" },
				{ current.effect45Active == effect45Active, "effect45Active" },
				{ current.effect46Active == effect46Active, "effect46Active" },
				{ current.effect47Active == effect47Active, "effect47Active" },
				{ current.effect47Primary == effect47Primary, "effect47Primary" },
				{ current.effect47Secondary == effect47Secondary, "effect47Secondary" },
				{ current.distantEnabled == distantEnabled, "distantEnabled" },
			};
			for (const auto& check : checks) {
				if (check.equal)
					continue;
				static std::atomic<std::uint32_t> verifyRawMismatchLogs{ 0 };
				if (verifyRawMismatchLogs.fetch_add(1, std::memory_order_relaxed) < 16u)
					spdlog::warn("[FlatDeferredPlayerCapture] VerifyRaw first mismatch: {}", check.name);
				return false;
			}
			return true;
		}

		struct PrivateRendererFacts
		{
			void* accumulator{};
			void* screenSSN{};
			void* offscreenSSN{};
			void* directionalLight{};
			std::uint32_t mainLightCount{};
			std::uint32_t maskedGeometryNameLength{};
			std::uint32_t maskedMaterialNameLength{};
			std::uint32_t postEffect{};
			std::uint32_t interfaceGlobal1D34Source{};
			float opacityAlpha{};
			float menuDiffuseIntensity{};
			float menuEmitIntensity{};
			bool enabled{};
			bool offscreenEnabled{};
			bool enableAO{};
			bool postAA{};
			bool rootsIdle{};
			bool exactSSNVtable{};
			bool privateSceneGraphAttested{};
			bool lightPinsHeld{};
			bool accumulatorIdle{};
			bool maskedGeometryNameLengthReadable{};
			bool maskedMaterialNameLengthReadable{};
			
			std::uint32_t failureCode{};
		};

		struct RawLightParams
		{
			std::int32_t type{};
			float spotFOV{};
			std::array<float, 3> lookAtPosition{};
			std::uint32_t pad14{};
			void* lookAtObject{};
			void* light{};
		};
		static_assert(sizeof(RawLightParams) == 0x28);

		[[nodiscard]] bool AttestPrivateDirectionalNode(const Request& request, void* node) noexcept
		{
			if (!node || node == request.playerRoot.get() || node == request.captureCamera.get() ||
				node == request.restoreCamera.get() || node == request.privateShadowSceneNode.get())
				return false;
			const auto& expected = request.privateDirectionalLightValues;
			std::array<float, 12> localRotation{};
			std::array<float, 12> worldRotation{};
			std::array<float, 10> lightPOD{};
			void* parent = nullptr;
			std::uint64_t flags = 0;
			const auto address = reinterpret_cast<std::uintptr_t>(node);
			if (!SafeLoad(address + 0x28, parent) || parent != nullptr ||
				!SafeCopy(localRotation.data(), reinterpret_cast<const void*>(address + 0x30),
					sizeof(localRotation)) ||
				!SafeCopy(worldRotation.data(), reinterpret_cast<const void*>(address + 0x70),
					sizeof(worldRotation)) ||
				!SafeLoad(address + 0x108, flags) || (flags & 1ull) != 0 ||
				!SafeCopy(lightPOD.data(), reinterpret_cast<const void*>(address + 0x120),
					sizeof(lightPOD)) ||
				std::memcmp(localRotation.data(), expected.localRotation.data(), sizeof(localRotation)) != 0 ||
				std::memcmp(worldRotation.data(), expected.localRotation.data(), sizeof(worldRotation)) != 0 ||
				std::memcmp(lightPOD.data(), expected.lightPOD.data(), sizeof(lightPOD)) != 0)
				return false;

			if (!std::all_of(localRotation.begin(), localRotation.end(),
					[](float value) { return std::isfinite(value); }) ||
				!std::all_of(lightPOD.begin(), lightPOD.end(),
					[](float value) { return std::isfinite(value) && value >= 0.0f; }) ||
				std::any_of(lightPOD.begin(), lightPOD.end(),
					[](float value) { return value > 65504.0f; }))
				return false;
			auto isSignedZero = [](float value) noexcept {
				return (std::bit_cast<std::uint32_t>(value) & 0x7FFFFFFFu) == 0;
			};
			if (!isSignedZero(localRotation[3]) || !isSignedZero(localRotation[7]) ||
				!isSignedZero(localRotation[11]))
				return false;
			const bool directionalContributes =
				lightPOD[9] > 1.0e-4f && (lightPOD[3] + lightPOD[4] + lightPOD[5]) > 1.0e-4f;

			auto dot = [&](std::size_t first, std::size_t second) noexcept {
				return localRotation[first] * localRotation[second] +
				       localRotation[first + 1] * localRotation[second + 1] +
				       localRotation[first + 2] * localRotation[second + 2];
			};
			constexpr float epsilon = 1.0e-3f;
			if (std::abs(dot(0, 0) - 1.0f) > epsilon ||
				std::abs(dot(4, 4) - 1.0f) > epsilon ||
				std::abs(dot(8, 8) - 1.0f) > epsilon || std::abs(dot(0, 4)) > epsilon ||
				std::abs(dot(0, 8)) > epsilon || std::abs(dot(4, 8)) > epsilon)
				return false;
			const float determinant =
				localRotation[0] * (localRotation[5] * localRotation[10] - localRotation[6] * localRotation[9]) -
				localRotation[1] * (localRotation[4] * localRotation[10] - localRotation[6] * localRotation[8]) +
				localRotation[2] * (localRotation[4] * localRotation[9] - localRotation[5] * localRotation[8]);
			return std::isfinite(determinant) && std::abs(std::abs(determinant) - 1.0f) <= epsilon &&
			       (directionalContributes || request.privatePointLightCount != 0);
		}

		[[nodiscard]] bool AttestDetachedObject(const RE::NiAVObject* object) noexcept
		{
			if (!object)
				return false;
			void* parent = nullptr;
			std::uintptr_t vtable = 0;
			const auto address = reinterpret_cast<std::uintptr_t>(object);
			const auto rdata = REL::Module::get().segment(REL::Segment::rdata);
			return SafeLoad(address, vtable) && vtable >= rdata.address() &&
			       vtable < rdata.address() + rdata.size() &&
			       SafeLoad(address + 0x28, parent) && parent == nullptr;
		}

		[[nodiscard]] bool AttestDetachedPlayerRoot(const Request& request) noexcept
		{
			if (!request.playerRoot || request.playerRoot.get() == request.captureCamera.get() ||
				request.playerRoot.get() == request.restoreCamera.get() ||
				request.playerRoot.get() == request.privateShadowSceneNode.get() ||
				request.playerRoot.get() == request.privateDirectionalLight.get())
				return false;
			return AttestDetachedObject(request.playerRoot.get());
		}

		struct FlattenedModelCensus
		{
			void* owner{};
			void* entries{};
			std::array<void*, kMaxFlattenedGeometryEntries> geometries{};
			std::array<void*, kMaxFlattenedGeometryEntries> shaderProperties{};

			std::array<std::uint8_t, kMaxFlattenedGeometryEntries> selectorEligible{};
			std::uint64_t identity{};
			std::uint32_t count{};
			std::uint32_t drawableCount{};
		};

		[[nodiscard]] void* ResolveFlattenedOwnerLeaf(
			RE::NiAVObject* root, std::uint32_t& exceptionCode) noexcept
		{
			__try {
				void** vtable = *reinterpret_cast<void***>(root);
				using ResolveFlattenedOwner = void* (*)(RE::NiAVObject*);
				return reinterpret_cast<ResolveFlattenedOwner>(vtable[0x30 / sizeof(void*)])(root);
			} __except (exceptionCode = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER) {
				return nullptr;
			}
		}

		[[nodiscard]] bool CaptureFlattenedModelCensus(
			RE::NiAVObject* root, FlattenedModelCensus& census) noexcept
		{
			census = {};
			if (!root)
				return false;
			const auto rootAddress = reinterpret_cast<std::uintptr_t>(root);
			std::uint64_t rootFlags = 0;
			void** rootVtable = nullptr;
			void* resolver = nullptr;
			const auto rdata = REL::Module::get().segment(REL::Segment::rdata);
			const auto text = REL::Module::get().segment(REL::Segment::text);
			if (!SafeLoad(rootAddress, rootVtable) || !rootVtable ||
				!SafeLoad(rootAddress + 0x108, rootFlags) || (rootFlags & 0x4000ull) == 0 ||
				!SafeLoad(reinterpret_cast<std::uintptr_t>(rootVtable) + 0x30, resolver) ||
				reinterpret_cast<std::uintptr_t>(rootVtable) < rdata.address() ||
				reinterpret_cast<std::uintptr_t>(rootVtable) >= rdata.address() + rdata.size() ||
				reinterpret_cast<std::uintptr_t>(resolver) < text.address() ||
				reinterpret_cast<std::uintptr_t>(resolver) >= text.address() + text.size())
				return false;

			std::uint32_t exceptionCode = 0;
			census.owner = ResolveFlattenedOwnerLeaf(root, exceptionCode);
			if (!census.owner || exceptionCode != 0)
				return false;
			const auto ownerAddress = reinterpret_cast<std::uintptr_t>(census.owner);
			if (!SafeLoad(ownerAddress + 0x168, census.entries) ||
				!SafeLoad(ownerAddress + 0x178, census.count) || !census.entries ||
				census.count == 0 || census.count > kMaxFlattenedGeometryEntries)
				return false;

			std::uint64_t hash = 1469598103934665603ull;
			auto addHash = [&](const void* value, std::size_t size) noexcept {
				const auto* bytes = static_cast<const std::uint8_t*>(value);
				for (std::size_t index = 0; index < size; ++index) {
					hash ^= bytes[index];
					hash *= 1099511628211ull;
				}
			};
			addHash(std::addressof(census.owner), sizeof(census.owner));
			addHash(std::addressof(census.entries), sizeof(census.entries));
			addHash(std::addressof(census.count), sizeof(census.count));

			for (std::uint32_t index = 0; index < census.count; ++index) {
				const auto entry = reinterpret_cast<std::uintptr_t>(census.entries) +
				                   static_cast<std::uintptr_t>(index) * 0x20u;
				std::uint32_t entryFlags = 0;
				std::uint64_t geometryFlags = 0;
				void* geometry = nullptr;
				void* shaderProperty = nullptr;
				void* geometryVtable = nullptr;
				void* propertyVtable = nullptr;
				if (!SafeLoad(entry + 0x18, entryFlags) ||
					!SafeLoad(entry + 0x10, geometry) || !geometry ||
					!SafeLoad(reinterpret_cast<std::uintptr_t>(geometry), geometryVtable) ||
					!SafeLoad(reinterpret_cast<std::uintptr_t>(geometry) + 0x108, geometryFlags) ||
					!SafeLoad(reinterpret_cast<std::uintptr_t>(geometry) + 0x138, shaderProperty) ||
					!shaderProperty || !SafeLoad(reinterpret_cast<std::uintptr_t>(shaderProperty), propertyVtable) ||
					reinterpret_cast<std::uintptr_t>(geometryVtable) < rdata.address() ||
					reinterpret_cast<std::uintptr_t>(geometryVtable) >= rdata.address() + rdata.size() ||
					reinterpret_cast<std::uintptr_t>(propertyVtable) < rdata.address() ||
					reinterpret_cast<std::uintptr_t>(propertyVtable) >= rdata.address() + rdata.size())
					return false;
				for (std::uint32_t prior = 0; prior < index; ++prior) {
					if (census.geometries[prior] == geometry)
						return false;
				}
				census.geometries[index] = geometry;
				census.shaderProperties[index] = shaderProperty;
				const bool entryAppCulled =
					(entryFlags & kAppCulledFlag) != 0;
				const bool geometryAppCulled =
					(geometryFlags & kAppCulledFlag) != 0;

				if (entryAppCulled != geometryAppCulled)
					return false;
				const auto selectorEligible = static_cast<std::uint8_t>(
					geometryAppCulled ? 0u : 1u);
				census.selectorEligible[index] = selectorEligible;
				if (selectorEligible != 0)
					++census.drawableCount;
				addHash(std::addressof(geometry), sizeof(geometry));
				addHash(std::addressof(shaderProperty), sizeof(shaderProperty));
				addHash(std::addressof(selectorEligible), sizeof(selectorEligible));
			}
			census.identity = hash ? hash : 1u;
			return census.drawableCount != 0 && census.drawableCount <= census.count;
		}

		[[nodiscard]] bool SameFlattenedModelCensus(
			const FlattenedModelCensus& expected, const FlattenedModelCensus& observed) noexcept
		{
			return expected.owner == observed.owner && expected.entries == observed.entries &&
			       expected.count == observed.count && expected.drawableCount == observed.drawableCount &&
			       expected.identity == observed.identity &&
			       std::equal(expected.geometries.begin(), expected.geometries.begin() + expected.count,
				   observed.geometries.begin()) &&
			       std::equal(expected.shaderProperties.begin(),
				   expected.shaderProperties.begin() + expected.count,
			       observed.shaderProperties.begin()) &&
			       std::equal(expected.selectorEligible.begin(),
				   expected.selectorEligible.begin() + expected.count,
			       observed.selectorEligible.begin());
		}

		[[nodiscard]] bool AttestRequiredEyeGeometryCensus(
			const Request& request, const FlattenedModelCensus& census,
			std::uint32_t& authoritativeCount) noexcept
		{
			authoritativeCount = 0;
			if (request.requiredEyeGeometryCount == 0 ||
				request.requiredEyeGeometryCount > request.requiredEyeGeometries.size() ||
				request.requiredEyeGeometryCount > census.count)
				return false;
			for (std::uint32_t eyeIndex = 0;
				eyeIndex < request.requiredEyeGeometryCount; ++eyeIndex) {
				auto* requiredEye = request.requiredEyeGeometries[eyeIndex];
				if (!requiredEye)
					return false;
				for (std::uint32_t prior = 0; prior < eyeIndex; ++prior) {
					if (request.requiredEyeGeometries[prior] == requiredEye)
						return false;
				}
				bool present = false;
				for (std::uint32_t geometryIndex = 0; geometryIndex < census.count;
					++geometryIndex) {
					if (census.geometries[geometryIndex] == requiredEye) {
						present = true;
						break;
					}
				}
				if (!present)
					return false;
				++authoritativeCount;
			}
			return authoritativeCount == request.requiredEyeGeometryCount;
		}

		struct Depth3ReservationSnapshot
		{
			struct Handle
			{
				std::int32_t physical{ -1 };
				std::int32_t refCount{ -1 };
				std::int32_t logicalOwner{ -1 };
			};
			static_assert(sizeof(Handle) == 12);

			void* pointer{};
			Handle handle{};
			std::uint32_t mapping{};
			std::uint16_t persistency{};
			void* acquiredPointer{};
			Handle acquiredHandle{};
			void* observedPointer{};
			Handle observedHandle{};
			std::uint32_t observedMapping{ kLogicalTargetNone };
			std::uint16_t observedPersistency{ 0xFFFFu };
			std::uint32_t failureCode{};
			bool persistent{};
			bool cold{};
			bool captured{};

			[[nodiscard]] bool Capture(
				RE::BSGraphics::RenderTargetManager* manager) noexcept
			{
				captured = false;
				persistent = false;
				cold = false;
				acquiredPointer = nullptr;
				acquiredHandle = {};
				observedPointer = nullptr;
				observedHandle = {};
				observedMapping = kLogicalTargetNone;
				observedPersistency = 0xFFFFu;
				failureCode = 0;
				if (!manager) {
					failureCode = 1u;
					return false;
				}
				if (!SafeLoad(Address(kRVA_DepthStencilReservationPointers) + 3u * sizeof(void*), pointer)) {
					failureCode = 2u;
					return false;
				}

				if (!SafeLoad(Address(kRVA_DepthStencilPersistency) +
						3u * sizeof(std::uint16_t), persistency)) {
					failureCode = 3u;
					return false;
				}
				if (!SafeLoad(reinterpret_cast<std::uintptr_t>(ReflectionRuntime::DepthStencilTargetIds(manager)) +
						3u * sizeof(std::uint32_t), mapping)) {
					failureCode = 5u;
					return false;
				}

				if (pointer == reinterpret_cast<void*>(~std::uintptr_t{ 0 })) {
					persistent = true;
					captured = persistency == 1u && mapping < kPhysicalDepthCount;
					if (!captured)
						failureCode = 10u;
					return captured;
				}

				if (persistency != 0u) {
					failureCode = 4u;
					return false;
				}

				if (!pointer) {
					cold = true;
					captured = mapping == kLogicalTargetNone;
					if (!captured)
						failureCode = 6u;
					return captured;
				}
				if (!SafeCopy(std::addressof(handle), pointer, sizeof(handle))) {
					failureCode = 7u;
					return false;
				}
				if (handle.physical < 0 ||
					handle.physical >= static_cast<std::int32_t>(kPhysicalDepthCount) ||
					handle.refCount < 0 ||
					handle.refCount == (std::numeric_limits<std::int32_t>::max)() ||
					(handle.logicalOwner != -1 && handle.logicalOwner != 3)) {
					failureCode = 8u;
					return false;
				}
				const bool unowned = handle.logicalOwner == -1 && handle.refCount == 0 &&
				                     mapping == kLogicalTargetNone;
				const bool ownedByThree = handle.logicalOwner == 3 && handle.refCount > 0 &&
				                          mapping == static_cast<std::uint32_t>(handle.physical);
				captured = unowned || ownedByThree;
				if (!captured)
					failureCode = 9u;
				return captured;
			}

			[[nodiscard]] bool Verify(
				RE::BSGraphics::RenderTargetManager* manager) const noexcept
			{
				if (!captured || !manager)
					return false;
				void* currentPointer = nullptr;
				std::uint32_t currentMapping = kLogicalTargetNone;
				std::uint16_t currentPersistency = 0xFFFF;
				const bool headerReadable = SafeLoad(Address(kRVA_DepthStencilReservationPointers) +
							   3u * sizeof(void*), currentPointer) &&
				       SafeLoad(Address(kRVA_DepthStencilPersistency) +
							   3u * sizeof(std::uint16_t), currentPersistency) &&
				       SafeLoad(reinterpret_cast<std::uintptr_t>(ReflectionRuntime::DepthStencilTargetIds(manager)) +
							   3u * sizeof(std::uint32_t), currentMapping) &&
				       currentPersistency == persistency;
				if (!headerReadable)
					return false;
				if (persistent)
					return currentPointer == pointer && currentMapping == mapping;
				if (!currentPointer ||
					currentPointer == reinterpret_cast<void*>(~std::uintptr_t{ 0 }))
					return false;
				Handle currentHandle{};
				if (!SafeCopy(std::addressof(currentHandle), currentPointer, sizeof(currentHandle)))
					return false;
				if (cold) {
					return acquiredPointer && currentPointer == acquiredPointer &&
					       currentHandle.physical == acquiredHandle.physical &&
					       currentHandle.refCount == 0 && currentHandle.logicalOwner == -1 &&
					       currentMapping == kLogicalTargetNone;
				}
				return currentPointer == pointer &&
				       std::memcmp(std::addressof(currentHandle), std::addressof(handle),
					   sizeof(handle)) == 0 &&
				       currentMapping == mapping;
			}

			[[nodiscard]] bool VerifyAcquired(
				RE::BSGraphics::RenderTargetManager* manager) noexcept
			{
				if (!captured || !manager) {
					failureCode = 20u;
					return false;
				}
				observedPointer = nullptr;
				observedHandle = {};
				observedMapping = kLogicalTargetNone;
				observedPersistency = 0xFFFFu;
				if (!SafeLoad(Address(kRVA_DepthStencilReservationPointers) +
							3u * sizeof(void*), observedPointer)) {
					failureCode = 21u;
					return false;
				}
				if (!SafeLoad(Address(kRVA_DepthStencilPersistency) +
							3u * sizeof(std::uint16_t), observedPersistency)) {
					failureCode = 24u;
					return false;
				}
				if (!SafeLoad(reinterpret_cast<std::uintptr_t>(ReflectionRuntime::DepthStencilTargetIds(manager)) +
							3u * sizeof(std::uint32_t), observedMapping)) {
					failureCode = 25u;
					return false;
				}
				if (observedPersistency != persistency) {
					failureCode = 26u;
					return false;
				}
				if (persistent) {
					const bool exactNoOp = observedPointer == pointer &&
						observedPointer == reinterpret_cast<void*>(~std::uintptr_t{ 0 }) &&
						observedMapping == mapping && observedMapping < kPhysicalDepthCount;
					failureCode = exactNoOp ? 0u : 34u;
					return exactNoOp;
				}
				if (!observedPointer ||
					observedPointer == reinterpret_cast<void*>(~std::uintptr_t{ 0 })) {
					failureCode = 22u;
					return false;
				}
				if (!SafeCopy(std::addressof(observedHandle), observedPointer,
						sizeof(observedHandle))) {
					failureCode = 23u;
					return false;
				}
				if (cold) {
					const bool canonicalColdAcquire = observedHandle.physical >= 0 &&
						observedHandle.physical < static_cast<std::int32_t>(kPhysicalDepthCount) &&
						observedHandle.refCount == 1 && observedHandle.logicalOwner == 3 &&
						observedMapping == static_cast<std::uint32_t>(observedHandle.physical);
					if (canonicalColdAcquire) {
						acquiredPointer = observedPointer;
						acquiredHandle = observedHandle;
						failureCode = 0u;
					} else {
						failureCode = 27u;
					}
					return canonicalColdAcquire;
				}
				const auto expectedRefCount = handle.refCount + 1;
				if (observedPointer != pointer)
					failureCode = 28u;
				else if (expectedRefCount <= 0)
					failureCode = 29u;
				else if (observedHandle.physical != handle.physical)
					failureCode = 30u;
				else if (observedHandle.refCount != expectedRefCount)
					failureCode = 31u;
				else if (observedHandle.logicalOwner != 3)
					failureCode = 32u;
				else if (observedMapping != static_cast<std::uint32_t>(handle.physical))
					failureCode = 33u;
				else
					failureCode = 0u;
				return failureCode == 0u;
			}
		};

		[[nodiscard]] bool AttestPrivatePointNode(const Request& request, std::size_t index,
			void* node, void* expectedParent) noexcept
		{
			if (!node || index >= request.privatePointLightCount)
				return false;
			const auto& expected = request.privatePointLightValues[index];
			if (!std::all_of(expected.translation.begin(), expected.translation.end(),
					[](float value) { return std::isfinite(value); }) ||
				!std::all_of(expected.diffuse.begin(), expected.diffuse.end(),
					[](float value) { return std::isfinite(value); }) ||
				!std::isfinite(expected.radius) || !std::isfinite(expected.dimmer) ||
				expected.radius <= 0.0f || expected.dimmer < 0.0f)
				return false;

			std::array<float, 3> translation{};
			std::array<float, 3> diffuse{};
			std::array<float, 3> radius{};
			float dimmer = 0.0f;
			std::uint64_t flags = 0;
			std::array<float, 3> pointDefaults{};
			void* parent = nullptr;
			const auto address = reinterpret_cast<std::uintptr_t>(node);
			return SafeLoad(address + 0x28, parent) && parent == expectedParent &&
			       SafeCopy(translation.data(), reinterpret_cast<const void*>(address + 0x60),
					   sizeof(translation)) &&
			       SafeLoad(address + 0x108, flags) && (flags & 1ull) == 0 &&
			       SafeCopy(diffuse.data(), reinterpret_cast<const void*>(address + 0x12C),
					   sizeof(diffuse)) &&
			       SafeCopy(radius.data(), reinterpret_cast<const void*>(address + 0x138),
					   sizeof(radius)) &&
			       SafeLoad(address + 0x144, dimmer) &&
			       SafeCopy(pointDefaults.data(), reinterpret_cast<const void*>(address + 0x170),
					   sizeof(pointDefaults)) &&
			       translation == expected.translation && diffuse == expected.diffuse &&
			       radius[0] == expected.radius && radius[1] == 0.0f && radius[2] == 0.0f &&
			       dimmer == expected.dimmer && pointDefaults[0] == 0.0f &&
			       pointDefaults[1] == 1.0f && pointDefaults[2] == 2.0f;
		}

		struct RawPointerArray
		{
			void** data{};
			std::uint32_t count{};
			std::uint32_t capacity{};
		};

		[[nodiscard]] bool LoadPrivateSSNArray(std::uintptr_t ssn, std::size_t dataOffset,
			std::size_t countOffset, std::size_t capacityOffset, RawPointerArray& array) noexcept
		{
			return SafeLoad(ssn + dataOffset, array.data) && SafeLoad(ssn + countOffset, array.count) &&
			       SafeLoad(ssn + capacityOffset, array.capacity) && array.count <= array.capacity &&
			       array.capacity <= 64 && (array.count == 0 || array.data != nullptr);
		}

		[[nodiscard]] bool AttestPrivateSSNLightGraph(const Request& request,
			const EngineStateSnapshot& engine, bool needsLightSetup, void* requestedSSN) noexcept
		{
			auto graphFail = [](std::uint32_t code, std::uint64_t a = 0, std::uint64_t b = 0,
								 std::uint64_t c = 0, std::uint64_t d = 0) noexcept {
				static std::atomic<std::uint32_t> graphFailLogs{ 0 };
				if (graphFailLogs.fetch_add(1, std::memory_order_relaxed) < 16u)
					spdlog::warn(
						"[FlatDeferredPlayerCapture] SSN light-graph attestation failed: check={} a=0x{:X} b=0x{:X} c=0x{:X} d=0x{:X}",
						code, a, b, c, d);
				return false;
			};
			if (!requestedSSN)
				return graphFail(1);
			const auto ssn = reinterpret_cast<std::uintptr_t>(requestedSSN);

			void** childData = nullptr;
			std::uint16_t childCapacity = 0;
			std::uint16_t childCount = 0;
			if (!SafeLoad(ssn + 0x128, childData) || !SafeLoad(ssn + 0x130, childCapacity) ||
				!SafeLoad(ssn + 0x134, childCount) || childCount > childCapacity ||
				childCapacity > 64 || (childCount != 0 && !childData))
				return graphFail(2, childCapacity, childCount);

			std::array<bool, Request::kMaxPrivatePointLights> childMatched{};
			std::uint32_t nonNullChildren = 0;
			for (std::uint16_t slot = 0; slot < childCapacity; ++slot) {
				void* child = nullptr;
				if (!SafeLoad(reinterpret_cast<std::uintptr_t>(childData + slot), child))
					return graphFail(3, slot);
				if (!child)
					continue;
				++nonNullChildren;
				bool matched = false;
				for (std::size_t index = 0; index < request.privatePointLightCount; ++index) {
					if (!childMatched[index] && child == request.privatePointLights[index].get()) {
						childMatched[index] = true;
						matched = true;
						break;
					}
				}
				if (!matched)
					return graphFail(4, slot, reinterpret_cast<std::uintptr_t>(child));
			}
			if (nonNullChildren != childCount)
				return graphFail(5, nonNullChildren, childCount);

			RawPointerArray active{};
			RawPointerArray shadow{};
			RawPointerArray portal{};
			RawPointerArray queuedAdd{};
			RawPointerArray queuedRemove{};
			if (!LoadPrivateSSNArray(ssn, 0x158, 0x168, 0x160, active) ||
				!LoadPrivateSSNArray(ssn, 0x170, 0x180, 0x178, shadow) ||
				!LoadPrivateSSNArray(ssn, 0x188, 0x198, 0x190, portal) ||
				!LoadPrivateSSNArray(ssn, 0x1A0, 0x1B0, 0x1A8, queuedAdd) ||
				!LoadPrivateSSNArray(ssn, 0x1B8, 0x1C8, 0x1C0, queuedRemove) ||
				shadow.count != 0 || portal.count != 0 || queuedAdd.count != 0 ||
				queuedRemove.count != 0)
				return graphFail(6, active.count, shadow.count,
					(static_cast<std::uint64_t>(portal.count) << 32) | queuedAdd.count,
					queuedRemove.count);

			std::uint64_t queueLock = 0;
			void* primarySunWrapper = nullptr;
			void* secondarySunWrapper = nullptr;
			if (!SafeLoad(ssn + 0x1D0, queueLock) || queueLock != 0 ||
				!SafeLoad(ssn + 0x1F8, primarySunWrapper) ||
				!SafeLoad(ssn + 0x208, secondarySunWrapper) || !primarySunWrapper ||
				!secondarySunWrapper || primarySunWrapper == secondarySunWrapper)
				return graphFail(7, queueLock,
					reinterpret_cast<std::uintptr_t>(primarySunWrapper),
					reinterpret_cast<std::uintptr_t>(secondarySunWrapper));
			REL::Relocation<std::uintptr_t> baseLightVtable{ RE::VTABLE::BSLight[0] };
			REL::Relocation<std::uintptr_t> shadowDirectionalVtable{
				RE::VTABLE::BSShadowDirectionalLight[0]
			};
			REL::Relocation<std::uintptr_t> directionalVtable{ RE::VTABLE::NiDirectionalLight[0] };
			std::uintptr_t primaryVtable = 0;
			std::uintptr_t secondaryVtable = 0;
			std::uintptr_t sentinelVtable = 0;
			void* primarySun = nullptr;
			void* secondarySun = nullptr;
			{

				static std::atomic<std::uint32_t> sunVtableExpectationLogs{ 0 };
				std::uintptr_t observedPrimaryVtable = 0;
				std::uintptr_t observedSecondaryVtable = 0;
				if (SafeLoad(reinterpret_cast<std::uintptr_t>(primarySunWrapper),
						observedPrimaryVtable) &&
					SafeLoad(reinterpret_cast<std::uintptr_t>(secondarySunWrapper),
						observedSecondaryVtable) &&
					(observedPrimaryVtable != baseLightVtable.address() ||
						observedSecondaryVtable != shadowDirectionalVtable.address()) &&
					sunVtableExpectationLogs.fetch_add(1, std::memory_order_relaxed) < 4u)
					spdlog::warn(
						"[FlatDeferredPlayerCapture] sun wrapper vtable expectation: "
						"primary=0x{:X} expected=0x{:X} secondary=0x{:X} expected=0x{:X}",
						observedPrimaryVtable, baseLightVtable.address(),
						observedSecondaryVtable, shadowDirectionalVtable.address());
			}
			if (!SafeLoad(reinterpret_cast<std::uintptr_t>(primarySunWrapper), primaryVtable) ||
				!SafeLoad(reinterpret_cast<std::uintptr_t>(secondarySunWrapper), secondaryVtable) ||
				primaryVtable != baseLightVtable.address() ||
				secondaryVtable != shadowDirectionalVtable.address() ||
				!SafeLoad(reinterpret_cast<std::uintptr_t>(primarySunWrapper) + 0xB8, primarySun) ||
				!SafeLoad(reinterpret_cast<std::uintptr_t>(secondarySunWrapper) + 0xB8, secondarySun))
				return graphFail(8, primaryVtable, secondaryVtable,
					reinterpret_cast<std::uintptr_t>(primarySun),
					reinterpret_cast<std::uintptr_t>(secondarySun));

			for (void* sun : { primarySun, secondarySun }) {
				if (!sun ||
					(sun == request.privateDirectionalLight.get() && sun == primarySun) ||
					!SafeLoad(reinterpret_cast<std::uintptr_t>(sun), sentinelVtable) ||
					sentinelVtable != directionalVtable.address())
					return graphFail(8, primaryVtable, secondaryVtable,
						reinterpret_cast<std::uintptr_t>(sun), sentinelVtable);
				for (std::size_t index = 0; index < request.privatePointLightCount; ++index) {
					if (sun == request.privatePointLights[index].get())
						return graphFail(9, index, reinterpret_cast<std::uintptr_t>(sun));
				}
			}
			for (void* liveSSN : engine.activeSSNSlots) {
				if (!liveSSN)
					continue;
				for (const std::size_t wrapperOffset : { std::size_t{ 0x1F8 }, std::size_t{ 0x208 } }) {
					void* liveWrapper = nullptr;
					void* liveSun = nullptr;
					if (!SafeLoad(reinterpret_cast<std::uintptr_t>(liveSSN) + wrapperOffset, liveWrapper))
						return graphFail(10, reinterpret_cast<std::uintptr_t>(liveSSN), wrapperOffset);
					if (!liveWrapper)
						continue;
					if (liveWrapper == primarySunWrapper || liveWrapper == secondarySunWrapper ||
						!SafeLoad(reinterpret_cast<std::uintptr_t>(liveWrapper) + 0xB8, liveSun) ||
						(liveSun && (liveSun == primarySun || liveSun == secondarySun)))
						return graphFail(11, reinterpret_cast<std::uintptr_t>(liveSSN),
							wrapperOffset, reinterpret_cast<std::uintptr_t>(liveWrapper),
							reinterpret_cast<std::uintptr_t>(liveSun));
				}
			}

			if (needsLightSetup) {
				
				if (childCount != 0 || active.count != 0)
					return graphFail(12, childCount, active.count);
				return true;
			}
			if (childCount != request.privatePointLightCount ||
				active.count != request.privatePointLightCount)
				return graphFail(13, childCount, active.count, request.privatePointLightCount);

			std::array<bool, Request::kMaxPrivatePointLights> wrapperMatched{};
			for (std::uint32_t index = 0; index < active.count; ++index) {
				void* wrapper = nullptr;
				std::uintptr_t wrapperVtable = 0;
				void* niLight = nullptr;
				if (!SafeLoad(reinterpret_cast<std::uintptr_t>(active.data + index), wrapper) || !wrapper ||
					!SafeLoad(reinterpret_cast<std::uintptr_t>(wrapper), wrapperVtable) ||
					wrapperVtable != baseLightVtable.address() ||
					!SafeLoad(reinterpret_cast<std::uintptr_t>(wrapper) + 0xB8, niLight))
					return graphFail(14, index, reinterpret_cast<std::uintptr_t>(wrapper),
						wrapperVtable);
				bool matched = false;
				for (std::size_t lightIndex = 0; lightIndex < request.privatePointLightCount; ++lightIndex) {
					if (!wrapperMatched[lightIndex] &&
						niLight == request.privatePointLights[lightIndex].get()) {
						wrapperMatched[lightIndex] = true;
						matched = true;
						break;
					}
				}
				if (!matched)
					return graphFail(15, index, reinterpret_cast<std::uintptr_t>(niLight));
			}
			return true;
		}

		[[nodiscard]] bool AttestPrivateRenderer(const Request& request,
			const EngineStateSnapshot& engine, PrivateRendererFacts& facts) noexcept
		{
			auto* renderer = reinterpret_cast<std::byte*>(request.privateRenderer);
			auto* requestedSSN = request.privateShadowSceneNode.get();
			if (!renderer || !requestedSSN || !request.exclusiveAccumulator ||
				request.privateRendererIdentitySerial == 0) {
				facts.failureCode = 1u;
				return false;
			}

			void* worldRoot = nullptr;
			void* screenRoot = nullptr;
			void* offscreenRoot = nullptr;
			std::uintptr_t objectVtable = 0;
			void* attachChildTarget = nullptr;
			void* detachChildTarget = nullptr;
			void* mainLightData = nullptr;
			std::uint32_t mainLightCapacity = 0;
			bool needsLightSetup = false;
			void* accumulatorCamera = nullptr;
			if (!SafeLoad(reinterpret_cast<std::uintptr_t>(renderer) + kInterfaceRendererEnabledOffset,
					facts.enabled) ||
				!SafeLoad(reinterpret_cast<std::uintptr_t>(renderer) +
							  kInterfaceRendererOffscreenEnabledOffset,
					facts.offscreenEnabled) ||
				!SafeLoad(reinterpret_cast<std::uintptr_t>(renderer) + kInterfaceRendererEnableAOOffset,
					facts.enableAO) ||
				!SafeLoad(reinterpret_cast<std::uintptr_t>(renderer) + kInterfaceRendererPostAAOffset,
					facts.postAA) ||
				!SafeLoad(reinterpret_cast<std::uintptr_t>(renderer) + kInterfaceRendererNeedsLightSetupOffset,
					needsLightSetup) ||
				!SafeLoad(reinterpret_cast<std::uintptr_t>(renderer) + kInterfaceRendererPostEffectOffset,
					facts.postEffect) ||
				!SafeLoad(Address(kRVA_InterfaceGlobal1D34Source),
					facts.interfaceGlobal1D34Source) ||
				!SafeLoad(reinterpret_cast<std::uintptr_t>(renderer) + 0x48,
					facts.opacityAlpha) ||
				!SafeLoad(reinterpret_cast<std::uintptr_t>(renderer) + 0x1F8,
					facts.menuDiffuseIntensity) ||
				!SafeLoad(reinterpret_cast<std::uintptr_t>(renderer) + 0x1FC,
					facts.menuEmitIntensity) ||
				!SafeLoad(reinterpret_cast<std::uintptr_t>(renderer) + kInterfaceRendererWorldRootOffset,
					worldRoot) ||
				!SafeLoad(reinterpret_cast<std::uintptr_t>(renderer) + kInterfaceRendererScreenRootOffset,
					screenRoot) ||
				!SafeLoad(reinterpret_cast<std::uintptr_t>(renderer) + kInterfaceRendererOffscreenRootOffset,
					offscreenRoot) ||
				!SafeLoad(reinterpret_cast<std::uintptr_t>(renderer) + kInterfaceRendererAccumulatorOffset,
					facts.accumulator) ||
				!SafeLoad(reinterpret_cast<std::uintptr_t>(renderer) + kInterfaceRendererMainLightsSizeOffset,
					facts.mainLightCount) ||
				!SafeLoad(reinterpret_cast<std::uintptr_t>(renderer) + 0x1C8, mainLightData) ||
				!SafeLoad(reinterpret_cast<std::uintptr_t>(renderer) + 0x1D0, mainLightCapacity) ||
				!SafeLoad(reinterpret_cast<std::uintptr_t>(renderer) +
							  kInterfaceRendererDirectionalLightOffset,
					facts.directionalLight) ||
				!SafeLoad(reinterpret_cast<std::uintptr_t>(renderer) + kInterfaceRendererScreenSSNOffset,
					facts.screenSSN) ||
				!SafeLoad(reinterpret_cast<std::uintptr_t>(renderer) +
							  kInterfaceRendererOffscreenSSNOffset,
					facts.offscreenSSN) ||
				!SafeLoad(reinterpret_cast<std::uintptr_t>(requestedSSN), objectVtable) ||
				!SafeLoad(objectVtable + 0x1D0, attachChildTarget) ||
				!SafeLoad(objectVtable + 0x1E8, detachChildTarget) ||
				!SafeLoad(reinterpret_cast<std::uintptr_t>(request.exclusiveAccumulator) +
							  kAccumulatorCameraOffset,
					accumulatorCamera)) {
				facts.failureCode = 2u;
				return false;
			}
			facts.maskedGeometryNameLengthReadable = SafeBSFixedStringLength(
				reinterpret_cast<std::uintptr_t>(renderer) + 0x220,
				facts.maskedGeometryNameLength);
			facts.maskedMaterialNameLengthReadable = SafeBSFixedStringLength(
				reinterpret_cast<std::uintptr_t>(renderer) + 0x228,
				facts.maskedMaterialNameLength);

			facts.exactSSNVtable = objectVtable == Address(kRVA_ShadowSceneNodeVtable) &&
				attachChildTarget == reinterpret_cast<void*>(Address(kRVA_NiNodeAttachChild)) &&
				detachChildTarget == reinterpret_cast<void*>(Address(kRVA_NiNodeDetachChild));
			facts.rootsIdle = worldRoot == nullptr && screenRoot == nullptr && offscreenRoot == nullptr;
			facts.privateSceneGraphAttested =
				AttestPrivateSSNLightGraph(request, engine, needsLightSetup, requestedSSN);
			facts.accumulatorIdle = accumulatorCamera == nullptr;
			const bool pointCountValid = request.privatePointLightCount <= Request::kMaxPrivatePointLights &&
			                             facts.mainLightCount == request.privatePointLightCount &&
			                             facts.mainLightCount <= mainLightCapacity &&
			                             (facts.mainLightCount == 0 || mainLightData != nullptr);
			facts.lightPinsHeld = pointCountValid && request.privateDirectionalLight &&
			                      request.privateDirectionalLight.get() == facts.directionalLight;
			REL::Relocation<std::uintptr_t> directionalVtable{ RE::VTABLE::NiDirectionalLight[0] };
			REL::Relocation<std::uintptr_t> pointVtable{ RE::VTABLE::NiPointLight[0] };
			std::uintptr_t actualDirectionalVtable = 0;
			facts.lightPinsHeld = facts.lightPinsHeld &&
			                      SafeLoad(reinterpret_cast<std::uintptr_t>(facts.directionalLight), actualDirectionalVtable) &&
			                      actualDirectionalVtable == directionalVtable.address() &&
			                      AttestPrivateDirectionalNode(request, facts.directionalLight);
			for (std::size_t index = 0; facts.lightPinsHeld && index < request.privatePointLights.size(); ++index) {
				if (index >= request.privatePointLightCount) {
					facts.lightPinsHeld = !request.privatePointLights[index];
					continue;
				}
				RawLightParams light{};
				std::uintptr_t lightVtable = 0;
				facts.lightPinsHeld = request.privatePointLights[index] &&
				                      SafeCopy(std::addressof(light), reinterpret_cast<const std::byte*>(mainLightData) + index * sizeof(RawLightParams), sizeof(light)) &&
				                      light.type == static_cast<std::int32_t>(RE::Interface3D::LightType::kPoint) &&
				                      light.lookAtObject == nullptr && light.light == request.privatePointLights[index].get() &&
				                      light.light != facts.directionalLight && std::isfinite(light.spotFOV) &&
				                      std::all_of(light.lookAtPosition.begin(), light.lookAtPosition.end(),
										  [](float value) { return std::isfinite(value); }) &&
				                      SafeLoad(reinterpret_cast<std::uintptr_t>(light.light), lightVtable) &&
				                      lightVtable == pointVtable.address() &&
				                      AttestPrivatePointNode(request, index, light.light,
										  needsLightSetup ? nullptr : requestedSSN) &&
				                      light.light != request.playerRoot.get() &&
				                      light.light != request.captureCamera.get() &&
				                      light.light != request.restoreCamera.get() &&
				                      light.light != request.privateShadowSceneNode.get();
				for (std::size_t prior = 0; facts.lightPinsHeld && prior < index; ++prior)
					facts.lightPinsHeld = request.privatePointLights[prior].get() != light.light;
			}

			const bool hasPrivateLightingAuthority = facts.lightPinsHeld && facts.directionalLight != nullptr;
			const bool privateSSNNotActive = std::none_of(engine.activeSSNSlots.begin(),
				engine.activeSSNSlots.end(), [&](void* active) { return active == requestedSSN; });
			const bool compositeMatchesRenderer = request.compositeFlag == (facts.postEffect == 3);
			const bool prefixScalarsValid = std::isfinite(facts.opacityAlpha) &&
				std::isfinite(facts.menuDiffuseIntensity) && std::isfinite(facts.menuEmitIntensity) &&
				(std::abs(facts.menuDiffuseIntensity) + std::abs(facts.menuEmitIntensity)) > 1.0e-6f;

			const bool backgroundFXPrefixIsNoOp = facts.maskedGeometryNameLengthReadable &&
				facts.maskedMaterialNameLengthReadable && facts.maskedGeometryNameLength == 0 &&
				facts.maskedMaterialNameLength == 0;

			if (facts.enabled)
				facts.failureCode = 3u;
			else if (facts.offscreenEnabled)
				facts.failureCode = 4u;
			else if (facts.enableAO)
				facts.failureCode = 5u;
			else if (facts.postAA)
				facts.failureCode = 6u;
			else if (!facts.rootsIdle)
				facts.failureCode = 7u;
			else if (facts.accumulator != request.exclusiveAccumulator)
				facts.failureCode = 8u;
			else if (facts.accumulator == engine.currentAccumulator)
				facts.failureCode = 9u;
			else if (facts.screenSSN != requestedSSN)
				facts.failureCode = 10u;
			else if (facts.offscreenSSN == nullptr || facts.offscreenSSN == requestedSSN)
				facts.failureCode = 11u;
			else if (!hasPrivateLightingAuthority)
				facts.failureCode = 12u;
			else if (!privateSSNNotActive)
				facts.failureCode = 13u;
			else if (!facts.privateSceneGraphAttested)
				facts.failureCode = 14u;
			else if (!facts.accumulatorIdle)
				facts.failureCode = 15u;
			else if (!facts.exactSSNVtable)
				facts.failureCode = 16u;
			else if (!compositeMatchesRenderer)
				facts.failureCode = 17u;
			else if (!prefixScalarsValid)
				facts.failureCode = 18u;
			else if (!backgroundFXPrefixIsNoOp)
				facts.failureCode = 19u;
			else
				facts.failureCode = 0u;
			return facts.failureCode == 0u;
		}

		struct NativeRunPacket
		{
			alignas(16) std::array<std::byte, kCullerSize> cullerStorage{};
			void* privateRenderer{};
			void* targetManager{};
			void* exclusiveAccumulator{};
			ResourceTransaction* resourceTransaction{};
			RE::NiCamera* camera{};
			RE::NiAVObject* playerRoot{};
			void* privateShadowSceneNode{};
			RE::NiAVObject* privateDirectionalLight{};
			const FlattenedModelCensus* entryCensus{};
			std::array<RE::NiAVObject*, Request::kMaxRequiredEyeGeometries> requiredEyeGeometries{};
			FlattenedModelCensus selectorCensus{};
			std::uint32_t requiredEyeGeometryCount{};
			std::uint32_t interfaceGlobal1D34Source{};
			float opacityAlpha{};
			float menuDiffuseIntensity{};
			float menuEmitIntensity{};
			std::uint32_t exceptionCode{};
			std::uintptr_t exceptionInstruction{};
			std::uintptr_t exceptionReturnAddress{};
			std::uintptr_t exceptionObject{};
			std::uintptr_t exceptionParent{};
			std::uintptr_t exceptionVtable{};
			bool shadowSceneNodePublished{};
			bool cameraPublished{};
			bool shaderCameraPublished{};
			bool privateNativeMutationBegan{};
			bool cullerConstructed{};
			bool cullerAccumulatorSet{};
			bool cullerCameraSet{};
			bool accumulatorForwardStateSet{};
			bool cameraDetachArmed{};
			bool rootDetachArmed{};
			bool cameraAttached{};
			bool rootAttached{};
			bool lightsUpdated{};
			bool queuedLightsProcessed{};
			bool shadowSceneNodeUpdated{};
			bool sunDetachArmed{};
			bool sunAttached{};
			bool selectorCensusCaptured{};
			bool selectorCensusMatchesEntry{};
			bool forwardGeometryProofArmed{};
			bool forwardSelectorObservationArmed{};
			bool forwardSelectorObservationCompleted{};
			bool depth3Acquired{};
			bool nativeStackHeadroomAttested{};
			bool forwardSelectorReturned{};
			bool cameraDetached{};
			bool rootDetached{};
			bool sunDetached{};
			bool cullerDestroyed{};
			bool nativeCompleted{};
		};

		struct NativeWorldRunPacket
		{
			static constexpr std::size_t kSubmittedRootHashCapacity =
				NativeWorldRequest::kMaxSubmittedRoots * 2;
			static_assert(std::has_single_bit(kSubmittedRootHashCapacity));
			void* targetManager{};
			void* renderContext{};
			void* distantRenderer{};
			void* effect45{};
			void* effect46{};
			void* effect47{};
			RE::Interface3D::Renderer* privateRenderer{};
			void* exclusiveAccumulator{};
			RE::NiCamera* camera{};
			void* privateShadowSceneNode{};
			RE::NiAVObject* modelRoot{};
			ResourceTransaction* resourceTransaction{};
			std::array<RE::NiAVObject*, NativeWorldRequest::kMaxSubmittedRoots> submittedRoots{};
			
			std::array<std::uintptr_t, kSubmittedRootHashCapacity> submittedRootIdentitySet{};
			std::uint32_t submittedRootCount{};
			RE::NiAVObject* privateMutableRoot{};
			NativeWorldLightingPreparer lightingPreparer{};
			void* lightingPreparerContext{};
			NativeWorldPassAccumulator passAccumulator{};
			void* passAccumulatorContext{};
			std::uint64_t ownerIdentitySerial{};
			std::uint64_t preparedAccumulatorSerial{};
			std::uint64_t frameSerial{};
			bool mrt4Enabled{};
			std::uint32_t exceptionCode{};
			std::uintptr_t exceptionInstruction{};
			std::uintptr_t exceptionReturnAddress{};
			bool mutationBegan{};
			bool drawModelAccumulateHookArmed{};
			bool drawModelAccumulateHookTLSRestored{};
			bool drawModelAccumulateHookEntered{};
			bool drawModelAccumulateHookArgumentsAttested{};
			bool directionalAmbientBlackWriteSuppressed{};
			std::uint32_t drawModelExpandedRootCount{};
			bool drawModelRootExpansionAttested{};
			void* drawModelCullingProcess{};
			bool cullerConstructed{};
			bool cullerAccumulatorSet{};
			bool cullerCameraSet{};
			bool drawModelCullerAuthorityRevoked{};
			std::uint32_t lightingPreparationFailureCode{};
			bool lightingPreparerInvoked{};
			bool lightingPreparerReturned{};
			bool lightingPreparationAttested{};
			std::uint32_t passAccumulationFailureCode{};
			bool passAccumulatorInvoked{};
			bool passAccumulatorReturned{};
			bool passAccumulationAttested{};
			bool prepassDepthCaptureArmed{};
			bool prepassDepthCaptureTLSRestored{};
			bool nativeWorldRasterizerTLSArmed{};
			bool nativeWorldRasterizerTLSRestored{};
			bool depthTargetBound{};
			std::array<bool, 6> gbufferTargetsBound{};
			bool viewportSelected{};
			bool rendererFlushed{};
			bool nativeCallReturned{};
			bool captureCameraDetached{};
			bool modelRootDetached{};
			bool privateSunDetached{};
			bool accumulatorGroup5Cleared{};
			std::uint32_t submittedRootsCleared{};
			bool accumulatorActivePassesCleared{};
			bool sortedMode18CurrentRecordValidated{};
			bool sortedMode18CurrentRecordRepaired{};
			bool nativeCompleted{};
		};

		[[nodiscard]] bool InsertNativeWorldRootIdentity(
			NativeWorldRunPacket& packet, RE::NiAVObject* root) noexcept
		{
			const auto identity = reinterpret_cast<std::uintptr_t>(root);
			if (identity <= 0x10000 || (identity & (alignof(void*) - 1u)) != 0u)
				return false;
			std::size_t slot = (identity >> 4u) &
				(NativeWorldRunPacket::kSubmittedRootHashCapacity - 1u);
			for (std::size_t probe = 0;
				probe < NativeWorldRunPacket::kSubmittedRootHashCapacity; ++probe) {
				auto& entry = packet.submittedRootIdentitySet[slot];
				if (entry == identity)
					return false;
				if (entry == 0u) {
					entry = identity;
					return true;
				}
				slot = (slot + 1u) &
					(NativeWorldRunPacket::kSubmittedRootHashCapacity - 1u);
			}
			return false;
		}

		[[nodiscard]] bool NativeWorldRootIdentityRecorded(
			const NativeWorldRunPacket& packet, const RE::NiAVObject* root) noexcept
		{
			const auto identity = reinterpret_cast<std::uintptr_t>(root);
			if (identity == 0u)
				return false;
			std::size_t slot = (identity >> 4u) &
				(NativeWorldRunPacket::kSubmittedRootHashCapacity - 1u);
			for (std::size_t probe = 0;
				probe < NativeWorldRunPacket::kSubmittedRootHashCapacity; ++probe) {
				const auto entry = packet.submittedRootIdentitySet[slot];
				if (entry == identity)
					return true;
				if (entry == 0u)
					return false;
				slot = (slot + 1u) &
					(NativeWorldRunPacket::kSubmittedRootHashCapacity - 1u);
			}
			return false;
		}

		[[nodiscard]] bool NativeWorldRootsPairwiseSubtreeDisjoint(
			const NativeWorldRunPacket& packet) noexcept
		{
			bool disjoint = true;
			__try {
				for (std::uint32_t index = 0; index < packet.submittedRootCount; ++index) {
					auto* root = packet.submittedRoots[index];
					auto* parent = root ? root->parent : nullptr;
					std::uint32_t depth = 0u;
					for (; parent && depth < 128u; ++depth) {
						if (parent == root || NativeWorldRootIdentityRecorded(packet, parent)) {
							disjoint = false;
							__leave;
						}
						parent = parent->parent;
					}
					if (parent != nullptr) {
						disjoint = false;
						__leave;
					}
				}
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				disjoint = false;
			}
			return disjoint;
		}

		thread_local NativeWorldRunPacket* g_activeNativeWorldDrawModelPacket = nullptr;
		thread_local RE::BSShader* g_nativeWorldCurrentTechniqueShader = nullptr;
		thread_local bool g_nativeWorldCurrentTechniqueIsGeometry = false;

		void CallNativeWorldPassAccumulator(
			NativeWorldRunPacket& packet, void* cullingProcess) noexcept;

		struct DrawModelDirectionalAmbientBlackHook
		{
			static void thunk(void* colors, void* mainColor, float scale) noexcept
			{
				if (auto* packet = g_activeNativeWorldDrawModelPacket) {
					packet->directionalAmbientBlackWriteSuppressed = true;
					return;
				}
				func(colors, mainColor, scale);
			}

			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct DrawModelAccumulateSceneHook
		{
			static void thunk(RE::NiCamera* camera, RE::NiAVObject* incomingRoot,
				void* cullingProcess, bool updateLODs) noexcept
			{
				auto* packet = g_activeNativeWorldDrawModelPacket;
				if (!packet) {
					func(camera, incomingRoot, cullingProcess, updateLODs);
					return;
				}

				packet->drawModelAccumulateHookEntered = true;
				packet->drawModelCullingProcess = cullingProcess;
				RE::NiCamera* cullerCamera = nullptr;
				void* cullerAccumulator = nullptr;
				std::uint32_t cullerMode = ~0u;
				const auto cullerAddress = reinterpret_cast<std::uintptr_t>(cullingProcess);
				const bool exactArguments = camera == packet->camera &&
					incomingRoot == packet->privateShadowSceneNode && updateLODs &&
					cullingProcess &&
					SafeLoad(cullerAddress + kCullerCameraOffset, cullerCamera) &&
					cullerCamera == packet->camera &&
					SafeLoad(cullerAddress + kCullerModeOffset, cullerMode) && cullerMode == 0u &&
					SafeLoad(cullerAddress + 0x190, cullerAccumulator) &&
					cullerAccumulator == packet->exclusiveAccumulator;
				packet->drawModelAccumulateHookArgumentsAttested = exactArguments;

				if (!exactArguments)
					return;

				packet->cullerConstructed = true;
				packet->cullerAccumulatorSet = cullingProcess != nullptr;
				packet->cullerCameraSet = camera != nullptr;
				for (std::uint32_t index = 0; index < packet->submittedRootCount; ++index) {
					auto* root = packet->submittedRoots[index];
					if (!root)
						break;
					func(camera, root, cullingProcess, root == packet->privateMutableRoot);
					++packet->drawModelExpandedRootCount;
				}
				packet->drawModelRootExpansionAttested =
					packet->drawModelExpandedRootCount == packet->submittedRootCount;
				CallNativeWorldPassAccumulator(*packet, cullingProcess);
			}

			static inline REL::Relocation<decltype(thunk)> func;
		};

		template <class T>
		struct ProcessLifetimeScratchSlot
		{
			alignas(T) std::array<std::byte, sizeof(T)> storage{};
			bool initialized{};

			[[nodiscard]] T* Get() noexcept
			{
				return std::launder(reinterpret_cast<T*>(storage.data()));
			}

			void Destroy() noexcept
			{
				static_assert(std::is_nothrow_destructible_v<T>);
				if (!initialized)
					return;
				Get()->~T();
				initialized = false;
			}

			T& Emplace()
			{
				Destroy();
				auto* value = ::new (static_cast<void*>(storage.data())) T{};
				initialized = true;
				return *value;
			}
		};

		struct CaptureScratchStorage
		{
			ProcessLifetimeScratchSlot<PipelineState> entryPipeline;
			ProcessLifetimeScratchSlot<PipelineState> restoredPipeline;
			ProcessLifetimeScratchSlot<EngineStateSnapshot> engine;
			ProcessLifetimeScratchSlot<FlattenedModelCensus> entryFlattenedCensus;
			ProcessLifetimeScratchSlot<FlattenedModelCensus> returnedFlattenedCensus;
			ProcessLifetimeScratchSlot<ResourceTransaction> resources;
			ProcessLifetimeScratchSlot<NativeRunPacket> packet;
			bool inUse{};

			void DestroyAll() noexcept
			{
				packet.Destroy();
				returnedFlattenedCensus.Destroy();
				resources.Destroy();
				entryFlattenedCensus.Destroy();
				engine.Destroy();
				restoredPipeline.Destroy();
				entryPipeline.Destroy();
			}

			void EmplaceAll()
			{
				entryPipeline.Emplace();
				restoredPipeline.Emplace();
				engine.Emplace();
				entryFlattenedCensus.Emplace();
				returnedFlattenedCensus.Emplace();
				resources.Emplace();
				packet.Emplace();
			}
		};

		CaptureScratchStorage g_captureScratchStorage{};

		struct NativeWorldScratchStorage
		{
			ProcessLifetimeScratchSlot<PipelineState> entryPipeline;
			ProcessLifetimeScratchSlot<PipelineState> restoredPipeline;
			ProcessLifetimeScratchSlot<EngineStateSnapshot> engine;
			ProcessLifetimeScratchSlot<ResourceTransaction> resources;
			ProcessLifetimeScratchSlot<NativeWorldRunPacket> packet;
			bool inUse{};

			void DestroyAll() noexcept
			{
				packet.Destroy();
				resources.Destroy();
				engine.Destroy();
				restoredPipeline.Destroy();
				entryPipeline.Destroy();
			}

			void EmplaceAll()
			{
				entryPipeline.Emplace();
				restoredPipeline.Emplace();
				engine.Emplace();
				resources.Emplace();
				packet.Emplace();
			}
		};

		NativeWorldScratchStorage g_nativeWorldScratchStorage{};

		struct NativeWorldScratchClaim
		{
			NativeWorldScratchStorage* storage{};
			bool armed{};

			explicit NativeWorldScratchClaim(NativeWorldScratchStorage& requested) :
				storage(std::addressof(requested))
			{
				if (requested.inUse)
					return;
				requested.inUse = true;
				try {
					requested.DestroyAll();
					requested.EmplaceAll();
					armed = true;
				} catch (...) {
					requested.DestroyAll();
					requested.inUse = false;
					throw;
				}
			}

			~NativeWorldScratchClaim() noexcept
			{
				if (!armed || !storage)
					return;
				storage->DestroyAll();
				storage->inUse = false;
			}

			NativeWorldScratchClaim(const NativeWorldScratchClaim&) = delete;
			NativeWorldScratchClaim& operator=(const NativeWorldScratchClaim&) = delete;
		};

		struct CaptureScratchClaim
		{
			CaptureScratchStorage* storage{};
			bool armed{};

			explicit CaptureScratchClaim(CaptureScratchStorage& requested) :
				storage(std::addressof(requested))
			{
				if (requested.inUse)
					return;
				requested.inUse = true;
				try {
					requested.DestroyAll();
					requested.EmplaceAll();
					armed = true;
				} catch (...) {
					requested.DestroyAll();
					requested.inUse = false;
					throw;
				}
			}

			~CaptureScratchClaim() noexcept
			{
				if (!armed || !storage)
					return;
				storage->DestroyAll();
				storage->inUse = false;
			}

			CaptureScratchClaim(const CaptureScratchClaim&) = delete;
			CaptureScratchClaim& operator=(const CaptureScratchClaim&) = delete;
		};

		struct NativeStackHeadroom
		{
			std::uintptr_t low{};
			std::uintptr_t high{};
			std::uintptr_t marker{};
			std::size_t remaining{};
			bool valid{};
		};

		constexpr std::size_t kMinimumNativeStackHeadroom = 384u * 1024u;

		[[nodiscard]] NativeStackHeadroom MeasureNativeStackHeadroom() noexcept
		{

			constexpr unsigned long kTEBStackBaseOffset = 0x08;
			constexpr unsigned long kTEBDeallocationStackOffset = 0x1478;
			const auto low = static_cast<std::uintptr_t>(__readgsqword(kTEBDeallocationStackOffset));
			const auto high = static_cast<std::uintptr_t>(__readgsqword(kTEBStackBaseOffset));
			std::byte marker{};
			NativeStackHeadroom result{};
			result.low = low;
			result.high = high;
			result.marker = reinterpret_cast<std::uintptr_t>(std::addressof(marker));
			result.valid = result.low != 0 && result.low < result.marker &&
				result.marker <= result.high;
			result.remaining = result.valid ? result.marker - result.low : 0;
			return result;
		}

		void LogNativeStackHeadroomUnsafe(
			const char* phase, const NativeStackHeadroom& headroom) noexcept
		{
			static std::atomic_uint32_t logs{};
			const auto ordinal = logs.fetch_add(1, std::memory_order_relaxed) + 1u;
			if (ordinal > 8u)
				return;
			try {
				spdlog::info(
					"[FlatDeferredPlayerCapture] native stack headroom: phase={} valid={} remaining={} "
					"low=0x{:X} marker=0x{:X} high=0x{:X} scratch={} pipeline={} resources={} engine={} packet={}",
					phase ? phase : "unknown", headroom.valid, headroom.remaining,
					headroom.low, headroom.marker, headroom.high, sizeof(CaptureScratchStorage),
					sizeof(PipelineState), sizeof(ResourceTransaction), sizeof(EngineStateSnapshot),
					sizeof(NativeRunPacket));
			} catch (...) {
			}
		}

		void LogNativeStackHeadroom(const char* phase, const NativeStackHeadroom& headroom) noexcept
		{
			__try {
				LogNativeStackHeadroomUnsafe(phase, headroom);
			} __except (EXCEPTION_EXECUTE_HANDLER) {
			}
		}

		void LogNativeForwardCheckpointUnsafe(
			const char* stage, const NativeRunPacket& packet) noexcept
		{
			static std::atomic_uint32_t logs{};
			const auto ordinal = logs.fetch_add(1, std::memory_order_relaxed) + 1u;
			if (ordinal > 96u)
				return;
			try {
				const auto* transaction = packet.resourceTransaction;
				spdlog::info(
					"[FlatDeferredPlayerCapture] NATIVE FORWARD CHECKPOINT: stage={} ordinal={} "
					"exception=0x{:08X} rip=0x{:X} selectorReturned={} nativeCompleted={} "
					"setup={} restore={} activePass={:p}",
					stage ? stage : "unknown", ordinal, packet.exceptionCode,
					packet.exceptionInstruction, packet.forwardSelectorReturned,
					packet.nativeCompleted,
					transaction ? transaction->forwardGeometrySetupCalls : 0u,
					transaction ? transaction->forwardGeometryRestoreCalls : 0u,
					transaction ? static_cast<void*>(transaction->forwardActivePass) : nullptr);
			} catch (...) {
			}
		}

		void LogNativeForwardCheckpoint(const char* stage, const NativeRunPacket& packet) noexcept
		{
			__try {
				LogNativeForwardCheckpointUnsafe(stage, packet);
			} __except (EXCEPTION_EXECUTE_HANDLER) {
			}
		}

		void LogForwardGetRenderPassesStageUnsafe(const char* stage, const void* shaderProperty,
			const void* geometry, std::uint32_t renderMode, const void* accumulator,
			const void* result) noexcept
		{
			static std::atomic_uint32_t logs{};
			const auto ordinal = logs.fetch_add(1, std::memory_order_relaxed) + 1u;
			if (ordinal > 128u)
				return;
			try {
				spdlog::info(
					"[FlatDeferredPlayerCapture] FORWARD GETRENDERPASSES CHECKPOINT: "
					"stage={} ordinal={} thread={} property={:p} geometry={:p} mode={} "
					"accumulator={:p} result={:p}",
					stage ? stage : "unknown", ordinal, GetCurrentThreadId(), shaderProperty,
					geometry, renderMode, accumulator, result);
			} catch (...) {
			}
		}

		void LogForwardGetRenderPassesStage(const char* stage, const void* shaderProperty,
			const void* geometry, std::uint32_t renderMode, const void* accumulator,
			const void* result) noexcept
		{
			__try {
				LogForwardGetRenderPassesStageUnsafe(
					stage, shaderProperty, geometry, renderMode, accumulator, result);
			} __except (EXCEPTION_EXECUTE_HANDLER) {
			}
		}

		struct Depth3ReservationLease
		{
			RE::BSGraphics::RenderTargetManager* manager{};
			Depth3ReservationSnapshot* snapshot{};
			std::uint32_t exceptionCode{};
			bool acquired{};
			bool releaseArmed{};
			bool releaseReturned{};
			bool restored{};

			Depth3ReservationLease(RE::BSGraphics::RenderTargetManager* targetManager,
				Depth3ReservationSnapshot& reservation) noexcept :
				manager(targetManager), snapshot(std::addressof(reservation))
			{}

			~Depth3ReservationLease() noexcept
			{

				if (releaseArmed && !Release())
					g_captureEscapePhase = CaptureEscapePhase::kReservationAmbiguous;
			}

			[[nodiscard]] bool Acquire() noexcept
			{
				if (!manager || !snapshot || !snapshot->captured || releaseArmed)
					return false;
				__try {
					reinterpret_cast<void (*)(void*, std::int32_t)>(Address(kRVA_AcquireDepthStencil))(
						manager, 3);

					releaseArmed = true;
					acquired = snapshot->VerifyAcquired(manager);
				} __except (exceptionCode = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER) {

					acquired = false;
					releaseArmed = false;
					g_captureEscapePhase = CaptureEscapePhase::kReservationAmbiguous;
				}
				return exceptionCode == 0 && acquired;
			}

			[[nodiscard]] bool IsAcquired() const noexcept
			{
				return acquired && releaseArmed && snapshot && snapshot->VerifyAcquired(manager);
			}

			[[nodiscard]] bool Release() noexcept
			{
				if (!releaseArmed)
					return releaseReturned && restored;

				releaseArmed = false;
				acquired = false;
				__try {
					reinterpret_cast<void (*)(void*, std::uint32_t)>(Address(kRVA_ReleaseDepthStencil))(
						manager, 3u);
					releaseReturned = true;
				} __except (exceptionCode = exceptionCode ? exceptionCode : GetExceptionCode(),
					EXCEPTION_EXECUTE_HANDLER) {
				}
				restored = releaseReturned && snapshot && snapshot->Verify(manager);
				return restored;
			}

			Depth3ReservationLease(const Depth3ReservationLease&) = delete;
			Depth3ReservationLease& operator=(const Depth3ReservationLease&) = delete;
		};

		int NativeExceptionFilter(EXCEPTION_POINTERS* exception, NativeRunPacket& packet) noexcept
		{

			if (packet.exceptionCode != 0)
				return EXCEPTION_EXECUTE_HANDLER;
			packet.exceptionCode = exception && exception->ExceptionRecord ?
				exception->ExceptionRecord->ExceptionCode : static_cast<std::uint32_t>(EXCEPTION_ACCESS_VIOLATION);
			if (exception && exception->ContextRecord) {
				packet.exceptionInstruction = static_cast<std::uintptr_t>(exception->ContextRecord->Rip);
				packet.exceptionObject = static_cast<std::uintptr_t>(exception->ContextRecord->Rdi);
				packet.exceptionParent = static_cast<std::uintptr_t>(exception->ContextRecord->Rbx);
				packet.exceptionVtable = static_cast<std::uintptr_t>(exception->ContextRecord->Rax);
				__try {
					packet.exceptionReturnAddress =
						*reinterpret_cast<const std::uintptr_t*>(exception->ContextRecord->Rsp);
				} __except (EXCEPTION_EXECUTE_HANDLER) {
					packet.exceptionReturnAddress = 0;
				}
			}
			return EXCEPTION_EXECUTE_HANDLER;
		}

		int NativeWorldExceptionFilter(
			EXCEPTION_POINTERS* exception, NativeWorldRunPacket& packet) noexcept
		{
			if (packet.exceptionCode != 0)
				return EXCEPTION_EXECUTE_HANDLER;
			packet.exceptionCode = exception && exception->ExceptionRecord ?
				exception->ExceptionRecord->ExceptionCode :
				static_cast<std::uint32_t>(EXCEPTION_ACCESS_VIOLATION);
			if (exception && exception->ContextRecord) {
				packet.exceptionInstruction =
					static_cast<std::uintptr_t>(exception->ContextRecord->Rip);
				__try {
					packet.exceptionReturnAddress =
						*reinterpret_cast<const std::uintptr_t*>(exception->ContextRecord->Rsp);
				} __except (EXCEPTION_EXECUTE_HANDLER) {
					packet.exceptionReturnAddress = 0;
				}
			}
			return EXCEPTION_EXECUTE_HANDLER;
		}

		struct OwnerAttestationCall
		{
			std::uint32_t exceptionCode{};
			std::uint32_t failureCode{};
			bool invoked{};
			bool returned{};
			bool attested{};
		};

		void CallNativeWorldOwnerAttestor(const NativeWorldRequest& request,
			const NativeWorldOwnerAttestationView& view, OwnerAttestationCall& call) noexcept
		{
			call = {};
			call.invoked = request.ownerAttestor != nullptr;
			if (!call.invoked)
				return;
			__try {
				call.attested = request.ownerAttestor(
					request.ownerAttestorContext, view, std::addressof(call.failureCode));
				call.returned = true;
			} __except (call.exceptionCode = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER) {
			}
		}

		void CallNativeWorldLightingPreparer(
			NativeWorldRunPacket& packet, void* cullingProcess) noexcept
		{
			packet.lightingPreparerInvoked = packet.lightingPreparer != nullptr;
			if (!packet.lightingPreparerInvoked)
				return;
			NativeWorldLightingPreparationView view{};
			view.exclusiveAccumulator = packet.exclusiveAccumulator;
			view.captureCamera = packet.camera;
			view.privateShadowSceneNode =
				reinterpret_cast<RE::NiAVObject*>(packet.privateShadowSceneNode);
			view.cullingProcess = cullingProcess;
			view.ownerIdentitySerial = packet.ownerIdentitySerial;
			view.preparedAccumulatorSerial = packet.preparedAccumulatorSerial;
			view.frameSerial = packet.frameSerial;
			__try {
				packet.lightingPreparationAttested = packet.lightingPreparer(
					packet.lightingPreparerContext, view,
					std::addressof(packet.lightingPreparationFailureCode));
				packet.lightingPreparerReturned = true;
			} __except (packet.exceptionCode = packet.exceptionCode ? packet.exceptionCode :
				GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER) {
			}
		}

		void CallNativeWorldPassAccumulator(
			NativeWorldRunPacket& packet, void* cullingProcess) noexcept
		{
			packet.passAccumulatorInvoked = packet.passAccumulator != nullptr;
			if (!packet.passAccumulatorInvoked)
				return;
			NativeWorldPassAccumulationView view{};
			view.exclusiveAccumulator = packet.exclusiveAccumulator;
			view.captureCamera = packet.camera;
			view.privateShadowSceneNode =
				reinterpret_cast<RE::NiAVObject*>(packet.privateShadowSceneNode);
			view.cullingProcess = cullingProcess;
			view.ownerIdentitySerial = packet.ownerIdentitySerial;
			view.preparedAccumulatorSerial = packet.preparedAccumulatorSerial;
			view.frameSerial = packet.frameSerial;
			__try {
				packet.passAccumulationAttested = packet.passAccumulator(
					packet.passAccumulatorContext, view,
					std::addressof(packet.passAccumulationFailureCode));
				packet.passAccumulatorReturned = true;
			} __except (NativeWorldExceptionFilter(GetExceptionInformation(), packet)) {
			}
		}

		void NullNativeWorldSunLeaf(NativeWorldRunPacket& packet) noexcept
		{
			if (!packet.privateShadowSceneNode)
				return;
			__try {
				reinterpret_cast<void (*)(void*, void*)>(Address(kRVA_SetSunLight))(
					packet.privateShadowSceneNode, nullptr);
				packet.privateSunDetached = true;
			} __except (packet.exceptionCode = packet.exceptionCode ? packet.exceptionCode :
				GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER) {
			}
		}

		void RepairNativeWorldSortedCurrentRecordLeaf(NativeWorldRunPacket& packet) noexcept
		{

			constexpr std::size_t kAccumulatorBatchOffset = 0xC8;
			constexpr std::size_t kCurrentRecordPointerOffset = 0x410;
			constexpr std::size_t kRecordHeadOffset = 0x08;
			constexpr std::size_t kRecordTailOffset = 0x10;
			constexpr std::size_t kRecordCountOffset = 0x1C;
			constexpr std::size_t kRecordFlagsOffset = 0x20;
			constexpr std::uintptr_t kMaximumUserAddress = 0x00007FFFFFFFFFFFULL;
			auto plausiblePointer = [](const void* pointer) noexcept {
				const auto address = reinterpret_cast<std::uintptr_t>(pointer);
				return pointer == nullptr ||
					(address > 0x10000u && address <= kMaximumUserAddress &&
						(address & (alignof(void*) - 1u)) == 0u);
			};

			__try {
				auto* accumulator = reinterpret_cast<std::byte*>(packet.exclusiveAccumulator);
				auto* batch = accumulator + kAccumulatorBatchOffset;
				auto* record = *reinterpret_cast<std::byte**>(
					batch + kCurrentRecordPointerOffset);
				void* head = nullptr;
				void* tail = nullptr;
				std::uint32_t count = 0;
				std::uint8_t flags = 0;
				if (!record) {
					packet.sortedMode18CurrentRecordValidated = true;
					packet.sortedMode18CurrentRecordRepaired = true;
					__leave;
				}
				if (!plausiblePointer(record))
					__leave;
				head = *reinterpret_cast<void**>(record + kRecordHeadOffset);
				tail = *reinterpret_cast<void**>(record + kRecordTailOffset);
				count = *reinterpret_cast<std::uint32_t*>(record + kRecordCountOffset);
				flags = *reinterpret_cast<std::uint8_t*>(record + kRecordFlagsOffset);
				if ((flags & 1u) == 0u || !plausiblePointer(head) || !plausiblePointer(tail))
					__leave;
				packet.sortedMode18CurrentRecordValidated = true;
				*reinterpret_cast<void**>(record + kRecordHeadOffset) = nullptr;
				*reinterpret_cast<void**>(record + kRecordTailOffset) = nullptr;
				*reinterpret_cast<std::uint32_t*>(record + kRecordCountOffset) = 0u;
				packet.sortedMode18CurrentRecordRepaired =
					*reinterpret_cast<std::byte**>(batch + kCurrentRecordPointerOffset) == record &&
					*reinterpret_cast<void**>(record + kRecordHeadOffset) == nullptr &&
					*reinterpret_cast<void**>(record + kRecordTailOffset) == nullptr &&
					*reinterpret_cast<std::uint32_t*>(record + kRecordCountOffset) == 0u &&
					(*reinterpret_cast<std::uint8_t*>(record + kRecordFlagsOffset) & 1u) != 0u;
				(void)count;
			} __except (packet.exceptionCode = packet.exceptionCode ? packet.exceptionCode :
				GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER) {
			}
		}

		void ObserveNativeWorldAccumulatorIdleLeaf(NativeWorldRunPacket& packet) noexcept
		{
			packet.accumulatorGroup5Cleared = false;
			packet.accumulatorActivePassesCleared = false;
			if (!packet.exclusiveAccumulator)
				return;
			constexpr std::size_t kAccumulatorBatchOffset = 0xC8;
			constexpr std::size_t kBatchGroupArrayOffset = 0x08;
			constexpr std::size_t kBatchGroupArrayStride = 0x18;
			constexpr std::uint32_t kAccumulatorGroupArrayCount = 13;
			constexpr std::size_t kBatchSortedCurrentRecordOffset = 0x410;
			constexpr std::size_t kBatchSortedCountOffset = 0x418;
			constexpr std::size_t kPassRecordHeadOffset = 0x08;
			constexpr std::size_t kPassRecordTailOffset = 0x10;
			constexpr std::size_t kPassRecordCountOffset = 0x1C;
			constexpr std::uintptr_t kMaximumUserAddress = 0x00007FFFFFFFFFFFULL;
			const auto plausiblePointer = [](const void* pointer) noexcept {
				const auto address = reinterpret_cast<std::uintptr_t>(pointer);
				return pointer == nullptr ||
					(address > 0x10000u && address <= kMaximumUserAddress &&
						(address & (alignof(void*) - 1u)) == 0u);
			};
			const auto batch = reinterpret_cast<std::uintptr_t>(
				packet.exclusiveAccumulator) + kAccumulatorBatchOffset;
			for (std::uint32_t index = 0; index < kAccumulatorGroupArrayCount; ++index) {
				std::uint32_t count = ~0u;
				if (!SafeLoad(batch + kBatchGroupArrayOffset +
						static_cast<std::size_t>(index) * kBatchGroupArrayStride + 0x10,
						count) || count != 0)
					return;
				if (index == 5u)
					packet.accumulatorGroup5Cleared = true;
			}
			std::uint32_t sortedCount = ~0u;
			void* currentRecord = nullptr;
			if (!SafeLoad(batch + kBatchSortedCountOffset, sortedCount) || sortedCount != 0 ||
				!SafeLoad(batch + kBatchSortedCurrentRecordOffset, currentRecord))
				return;
			if (currentRecord) {
				void* head = nullptr;
				void* tail = nullptr;
				std::uint32_t count = ~0u;
				const auto record = reinterpret_cast<std::uintptr_t>(currentRecord);
				if (!plausiblePointer(currentRecord) ||
					!SafeLoad(record + kPassRecordHeadOffset, head) ||
					!SafeLoad(record + kPassRecordTailOffset, tail) ||
					!SafeLoad(record + kPassRecordCountOffset, count) || head || tail ||
					count != 0)
					return;
			}
			packet.accumulatorActivePassesCleared = true;
		}

		void CleanupNativeWorldPassesLeaf(NativeWorldRunPacket& packet) noexcept
		{

			if (!packet.nativeCallReturned)
				return;
			for (std::uint32_t index = 0; index < packet.submittedRootCount; ++index) {
				__try {
					reinterpret_cast<void (*)(void*)>(Address(kRVA_ClearRenderPasses))(
						packet.submittedRoots[index]);
					++packet.submittedRootsCleared;
				} __except (packet.exceptionCode = packet.exceptionCode ? packet.exceptionCode :
					GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER) {
				}
			}

			if (packet.exceptionCode == 0 &&
				packet.submittedRootsCleared == packet.submittedRootCount) {
				__try {
					reinterpret_cast<void (*)(void*, bool)>(Address(kRVA_ClearActivePasses))(
						packet.exclusiveAccumulator, true);
				} __except (packet.exceptionCode = packet.exceptionCode ? packet.exceptionCode :
					GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER) {
				}
			}
			RepairNativeWorldSortedCurrentRecordLeaf(packet);
			ObserveNativeWorldAccumulatorIdleLeaf(packet);
		}

		void CloseNativeWorldDepthCaptureLeaf(NativeWorldRunPacket& packet) noexcept
		{
			if (!packet.prepassDepthCaptureArmed ||
				packet.prepassDepthCaptureTLSRestored)
				return;
			__try {
				if (g_activeNativeWorldDepthTransaction == packet.resourceTransaction) {
					g_activeNativeWorldDepthTransaction = nullptr;
					packet.prepassDepthCaptureTLSRestored = true;
				}
			} __except (packet.exceptionCode = packet.exceptionCode ? packet.exceptionCode :
				GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER) {
			}
		}

		void CloseNativeWorldRasterizerTransactionLeaf(NativeWorldRunPacket& packet) noexcept
		{
			if (!packet.nativeWorldRasterizerTLSArmed ||
				packet.nativeWorldRasterizerTLSRestored)
				return;
			__try {
				if (g_activeNativeWorldRasterizerTransaction == packet.resourceTransaction) {

					auto* transaction = packet.resourceTransaction;
					g_activeNativeWorldRasterizerTransaction = nullptr;
					g_nativeWorldGeometryPassDepth = 0;
					g_nativeWorldOrdinaryPassDepth = 0;
					g_nativeWorldCurrentTechniqueShader = nullptr;
					g_nativeWorldCurrentTechniqueIsGeometry = false;
					packet.nativeWorldRasterizerTLSRestored = true;
					if (transaction) {
						(void)PlanarMirrorLookup::RestoreOrdinaryRasterizer(
							transaction->device, transaction->context);
					}
				}
			} __except (packet.exceptionCode = packet.exceptionCode ? packet.exceptionCode :
				GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER) {
			}
		}

		void RunNativeWorldLeaf(NativeWorldRunPacket& packet) noexcept
		{
			ID3D11RenderTargetView* prefixRTV = nullptr;
			ID3D11DepthStencilView* prefixDSV = nullptr;
			__try {
				__try {
					using SetRenderTarget = void (*)(void*, std::int32_t, std::int32_t,
						std::int32_t);
					using SetDepthStencil = void (*)(void*, std::int32_t, std::int32_t,
						std::int32_t, std::uint64_t);
					using DrawModel = void (*)(RE::Interface3D::Renderer*, RE::NiAVObject*,
						RE::ShadowSceneNode*, RE::NiCamera*, std::uint32_t, std::uint8_t,
						std::uint8_t, std::uint8_t);

					if (!packet.privateRenderer || !packet.modelRoot ||
						!packet.privateShadowSceneNode || !packet.camera ||
						!packet.exclusiveAccumulator || !packet.resourceTransaction)
						__leave;
					packet.mutationBegan = true;

					reinterpret_cast<void (*)(std::uint32_t, void*)>(
						Address(kRVA_SetShadowSceneNode))(4u, packet.privateShadowSceneNode);

					CallNativeWorldLightingPreparer(packet, nullptr);

					if (!packet.lightingPreparerReturned || !packet.lightingPreparationAttested ||
						packet.exceptionCode != 0)
						__leave;

					if (!packet.resourceTransaction || g_activeNativeWorldDepthTransaction ||
						g_activeNativeWorldRasterizerTransaction ||
						g_activeNativeWorldDrawModelPacket)
						__leave;
					g_activeNativeWorldDepthTransaction = packet.resourceTransaction;
					packet.prepassDepthCaptureArmed = true;
					g_activeNativeWorldRasterizerTransaction = packet.resourceTransaction;
					g_nativeWorldGeometryPassDepth = 0;
					g_nativeWorldOrdinaryPassDepth = 0;
					g_nativeWorldCurrentTechniqueShader = nullptr;
					g_nativeWorldCurrentTechniqueIsGeometry = false;
					packet.resourceTransaction->nativeWorldRasterizerPolicyArmed = true;
					packet.nativeWorldRasterizerTLSArmed = true;

					reinterpret_cast<SetRenderTarget>(Address(kRVA_SetCurrentRenderTarget))(
						packet.targetManager, 0, kOutputLogicalColor, 3);
					reinterpret_cast<void (*)(void*)>(Address(kRVA_SetViewportDefault))(
						packet.targetManager);
					reinterpret_cast<SetDepthStencil>(Address(kRVA_SetCurrentDepthStencil))(
						packet.targetManager, kNativeWorldOutputLogicalDepth, 3, 0, 0);
					reinterpret_cast<void (*)(void*)>(Address(kRVA_RendererFlush))(
						reinterpret_cast<void*>(Address(kRVA_Renderer)));
					packet.rendererFlushed = true;

					auto* context = packet.resourceTransaction->context;
					if (!context || !packet.resourceTransaction->sourceColorRTV ||
						!packet.resourceTransaction->writableDepthView)
						__leave;
					context->OMGetRenderTargets(1u, std::addressof(prefixRTV),
						std::addressof(prefixDSV));
					UINT viewportCount = 1u;
					D3D11_VIEWPORT viewport{};
					context->RSGetViewports(std::addressof(viewportCount),
						std::addressof(viewport));
					const auto& colorDesc = packet.resourceTransaction->sourceColorDesc;
					packet.viewportSelected = viewportCount == 1u && viewport.TopLeftX == 0.0f &&
						viewport.TopLeftY == 0.0f &&
						viewport.Width == static_cast<float>(colorDesc.Width) &&
						viewport.Height == static_cast<float>(colorDesc.Height);
					packet.depthTargetBound =
						prefixRTV == packet.resourceTransaction->sourceColorRTV.Get() &&
						prefixDSV == packet.resourceTransaction->writableDepthView.Get();
					if (!packet.viewportSelected || !packet.depthTargetBound)
						__leave;

					g_activeNativeWorldDrawModelPacket = std::addressof(packet);
					packet.drawModelAccumulateHookArmed = true;

					reinterpret_cast<DrawModel>(Address(kRVA_Interface3DDrawModel))(
						packet.privateRenderer, packet.modelRoot,
						reinterpret_cast<RE::ShadowSceneNode*>(packet.privateShadowSceneNode),
						packet.camera, kOutputLogicalColor, 0u, 1u, 1u);
					packet.nativeCallReturned = true;

					void* cameraParent = reinterpret_cast<void*>(~std::uintptr_t{ 0 });
					void* modelParent = reinterpret_cast<void*>(~std::uintptr_t{ 0 });
					packet.captureCameraDetached =
						SafeLoad(reinterpret_cast<std::uintptr_t>(packet.camera) + 0x28, cameraParent) &&
						cameraParent == nullptr;
					packet.modelRootDetached =
						SafeLoad(reinterpret_cast<std::uintptr_t>(packet.modelRoot) + 0x28, modelParent) &&
						modelParent == nullptr;
					CleanupNativeWorldPassesLeaf(packet);
				} __finally {

					if (packet.drawModelAccumulateHookArmed &&
						g_activeNativeWorldDrawModelPacket == std::addressof(packet)) {
						const bool exactCullerObserved =
							packet.drawModelAccumulateHookArgumentsAttested &&
							packet.drawModelCullingProcess != nullptr;
						g_activeNativeWorldDrawModelPacket = nullptr;
						packet.drawModelAccumulateHookTLSRestored = true;
						packet.drawModelCullerAuthorityRevoked = exactCullerObserved &&
							g_activeNativeWorldDrawModelPacket == nullptr;
					}
					packet.drawModelCullingProcess = nullptr;
					CloseNativeWorldDepthCaptureLeaf(packet);
					CloseNativeWorldRasterizerTransactionLeaf(packet);
					NullNativeWorldSunLeaf(packet);
					if (prefixRTV) {
						prefixRTV->Release();
						prefixRTV = nullptr;
					}
					if (prefixDSV) {
						prefixDSV->Release();
						prefixDSV = nullptr;
					}
				}

				packet.nativeCompleted = packet.exceptionCode == 0 &&
					packet.nativeCallReturned && packet.resourceTransaction &&
					packet.resourceTransaction->prepassDepthCaptured &&
					packet.directionalAmbientBlackWriteSuppressed &&
					packet.lightingPreparationAttested &&
					packet.drawModelAccumulateHookArgumentsAttested &&
					packet.drawModelRootExpansionAttested &&
					packet.passAccumulatorInvoked && packet.passAccumulatorReturned &&
					packet.passAccumulationAttested;
			} __except (NativeWorldExceptionFilter(GetExceptionInformation(), packet)) {
			}
		}

		[[nodiscard]] bool BeginForwardOcclusionQueryLeaf(
			ResourceTransaction& transaction, NativeRunPacket& packet) noexcept
		{
			bool completed = false;
			LogNativeForwardCheckpoint("begin-occlusion-query-enter", packet);
			__try {
				if (transaction.prepared && transaction.context && transaction.pool &&
					transaction.pool->coverage.forwardOcclusion) {
					transaction.context->Begin(transaction.pool->coverage.forwardOcclusion.Get());
					transaction.forwardOcclusionQueryBegan = true;
					completed = true;
				}
			} __except (NativeExceptionFilter(GetExceptionInformation(), packet)) {
			}
			LogNativeForwardCheckpoint("begin-occlusion-query-return", packet);
			return completed;
		}

		[[nodiscard]] bool BeginForwardStatisticsQueryLeaf(
			ResourceTransaction& transaction, NativeRunPacket& packet) noexcept
		{
			bool completed = false;
			LogNativeForwardCheckpoint("begin-statistics-query-enter", packet);
			__try {
				if (transaction.prepared && transaction.context && transaction.pool &&
					transaction.pool->coverage.forwardStatistics) {
					transaction.context->Begin(transaction.pool->coverage.forwardStatistics.Get());
					transaction.forwardStatisticsQueryBegan = true;
					completed = true;
				}
			} __except (NativeExceptionFilter(GetExceptionInformation(), packet)) {
			}
			LogNativeForwardCheckpoint("begin-statistics-query-return", packet);
			return completed;
		}

		[[nodiscard]] bool BeginForwardQueriesLeaf(
			ResourceTransaction& transaction, NativeRunPacket& packet) noexcept
		{
			LogNativeForwardCheckpoint("begin-queries-enter", packet);
			const bool occlusionBegan = BeginForwardOcclusionQueryLeaf(transaction, packet);
			const bool statisticsBegan = occlusionBegan &&
				BeginForwardStatisticsQueryLeaf(transaction, packet);
			transaction.forwardQueriesBegan = occlusionBegan && statisticsBegan;
			LogNativeForwardCheckpoint("begin-queries-return", packet);
			return transaction.forwardQueriesBegan;
		}

		[[nodiscard]] bool EndForwardStatisticsQueryLeaf(
			ResourceTransaction& transaction, NativeRunPacket& packet) noexcept
		{
			if (!transaction.forwardStatisticsQueryBegan)
				return true;
			if (transaction.forwardStatisticsQueryEnded)
				return true;
			bool completed = false;
			LogNativeForwardCheckpoint("end-statistics-query-enter", packet);
			__try {
				if (transaction.context && transaction.pool &&
					transaction.pool->coverage.forwardStatistics) {
					transaction.context->End(transaction.pool->coverage.forwardStatistics.Get());
					transaction.forwardStatisticsQueryEnded = true;
					completed = true;
				}
			} __except (NativeExceptionFilter(GetExceptionInformation(), packet)) {
			}
			LogNativeForwardCheckpoint("end-statistics-query-return", packet);
			return completed;
		}

		[[nodiscard]] bool EndForwardOcclusionQueryLeaf(
			ResourceTransaction& transaction, NativeRunPacket& packet) noexcept
		{
			if (!transaction.forwardOcclusionQueryBegan)
				return true;
			if (transaction.forwardOcclusionQueryEnded)
				return true;
			bool completed = false;
			LogNativeForwardCheckpoint("end-occlusion-query-enter", packet);
			__try {
				if (transaction.context && transaction.pool &&
					transaction.pool->coverage.forwardOcclusion) {
					transaction.context->End(transaction.pool->coverage.forwardOcclusion.Get());
					transaction.forwardOcclusionQueryEnded = true;
					completed = true;
				}
			} __except (NativeExceptionFilter(GetExceptionInformation(), packet)) {
			}
			LogNativeForwardCheckpoint("end-occlusion-query-return", packet);
			return completed;
		}

		[[nodiscard]] bool EndForwardQueriesLeaf(
			ResourceTransaction& transaction, NativeRunPacket& packet) noexcept
		{
			LogNativeForwardCheckpoint("end-queries-enter", packet);

			const bool statisticsEnded = EndForwardStatisticsQueryLeaf(transaction, packet);
			const bool occlusionEnded = EndForwardOcclusionQueryLeaf(transaction, packet);
			transaction.forwardQueriesEnded = transaction.forwardQueriesBegan &&
				statisticsEnded && occlusionEnded && transaction.forwardStatisticsQueryEnded &&
				transaction.forwardOcclusionQueryEnded;
			LogNativeForwardCheckpoint("end-queries-return", packet);
			return transaction.forwardQueriesEnded;
		}

		void DetachNativeChildLeaf(NativeRunPacket& packet, void* child, bool armed,
			bool& completed) noexcept
		{
			if (!armed || !packet.privateShadowSceneNode || !child)
				return;
			__try {
				auto** vtable = *reinterpret_cast<void***>(packet.privateShadowSceneNode);
				using DetachChild = void (*)(void*, void*);
				reinterpret_cast<DetachChild>(vtable[0x1E8 / sizeof(void*)])(
					packet.privateShadowSceneNode, child);
				void* parent = reinterpret_cast<void*>(~std::uintptr_t{ 0 });
				completed = SafeLoad(reinterpret_cast<std::uintptr_t>(child) + 0x28, parent) &&
				            parent == nullptr;
			} __except (packet.exceptionCode = packet.exceptionCode ? packet.exceptionCode :
			                                                   GetExceptionCode(),
				EXCEPTION_EXECUTE_HANDLER) {
			}
		}

		void NullNativeSunLeaf(NativeRunPacket& packet) noexcept
		{
			if (!packet.sunDetachArmed || !packet.privateShadowSceneNode)
				return;
			__try {
				reinterpret_cast<void (*)(void*, void*)>(Address(kRVA_SetSunLight))(
					packet.privateShadowSceneNode, nullptr);
				packet.sunDetached = true;
			} __except (packet.exceptionCode = packet.exceptionCode ? packet.exceptionCode :
			                                                   GetExceptionCode(),
				EXCEPTION_EXECUTE_HANDLER) {
			}
		}

		void DestroyNativeCullerLeaf(NativeRunPacket& packet) noexcept
		{
			if (!packet.cullerConstructed)
				return;
			__try {
				reinterpret_cast<void (*)(void*)>(Address(kRVA_CullerDtor))(
					packet.cullerStorage.data());
				packet.cullerDestroyed = true;
			} __except (packet.exceptionCode = packet.exceptionCode ? packet.exceptionCode :
			                                                   GetExceptionCode(),
				EXCEPTION_EXECUTE_HANDLER) {
			}
		}

		void CleanupNativeForwardLeaf(NativeRunPacket& packet) noexcept
		{
			LogNativeForwardCheckpoint("cleanup-enter", packet);
			if (packet.resourceTransaction && packet.forwardSelectorObservationArmed &&
				!packet.forwardSelectorObservationCompleted) {
				(void)packet.resourceTransaction->EndForwardSelectorObservation(false);
			}

			DetachNativeChildLeaf(packet, packet.camera, packet.cameraDetachArmed,
				packet.cameraDetached);
			DetachNativeChildLeaf(packet, packet.playerRoot, packet.rootDetachArmed,
				packet.rootDetached);
			NullNativeSunLeaf(packet);
			DestroyNativeCullerLeaf(packet);
			LogNativeForwardCheckpoint("cleanup-exit", packet);
		}

		void RunNativeLeaf(NativeRunPacket& packet) noexcept
		{
			LogNativeForwardCheckpoint("leaf-enter", packet);
			__try {
				__try {
					using SetCameraData = void (*)(void*, RE::NiCamera*, bool, float, float);
					using CullerConstructor = void* (*)(void*, void*);
					using UpdateLights = void (*)(void*, void*, void*, bool*);
					using SetRenderTarget = void (*)(void*, std::int32_t, std::int32_t, std::int32_t);
					using SetDepthStencil = void (*)(void*, std::int32_t, std::int32_t,
						std::int32_t, std::uint64_t);
					using DrawModelForward = void (*)(void*, RE::NiAVObject*, void*, RE::NiCamera*,
						std::uint32_t);

					if (!packet.depth3Acquired)
						__leave;
					packet.privateNativeMutationBegan = true;

					reinterpret_cast<void (*)(std::uint32_t, void*)>(Address(kRVA_SetShadowSceneNode))(
						4u, packet.privateShadowSceneNode);
					packet.shadowSceneNodePublished = true;
					reinterpret_cast<SetCameraData>(Address(kRVA_SetCameraData))(
						reinterpret_cast<void*>(Address(kRVA_GraphicsState)), packet.camera,
						false, 0.0f, 1.0f);
					packet.cameraPublished = true;
					LogNativeForwardCheckpoint("wrapper-camera-published", packet);

					void* culler = reinterpret_cast<CullerConstructor>(Address(kRVA_CullerCtor))(
						packet.cullerStorage.data(), nullptr);
					if (culler != packet.cullerStorage.data())
						__leave;
					packet.cullerConstructed = true;
					*reinterpret_cast<std::uint32_t*>(packet.cullerStorage.data() + kCullerModeOffset) = 0u;
					reinterpret_cast<void (*)(void*, void*)>(Address(kRVA_CullerSetAccumulator))(
						culler, packet.exclusiveAccumulator);
					packet.cullerAccumulatorSet = true;
					*reinterpret_cast<RE::NiCamera**>(packet.cullerStorage.data() + kCullerCameraOffset) =
						packet.camera;
					packet.cullerCameraSet = true;

					auto* accumulator = reinterpret_cast<std::byte*>(packet.exclusiveAccumulator);
					*reinterpret_cast<void**>(accumulator + kAccumulatorShadowSceneNodeOffset) =
						packet.privateShadowSceneNode;
					*reinterpret_cast<std::uint32_t*>(accumulator + kAccumulatorRenderModeOffset) = 0u;
					std::memcpy(accumulator + kAccumulatorCameraEyeOffset,
						reinterpret_cast<const std::byte*>(packet.camera) + 0xA0, 3u * sizeof(float));
					*reinterpret_cast<std::uint8_t*>(accumulator + kAccumulatorForwardFlagOffset) = 0u;
					packet.accumulatorForwardStateSet = true;
					*reinterpret_cast<void**>(Address(kRVA_ShaderCamera)) = packet.camera;
					packet.shaderCameraPublished = true;

					auto** ssnVtable = *reinterpret_cast<void***>(packet.privateShadowSceneNode);
					using AttachChild = void (*)(void*, void*, bool);
					packet.cameraDetachArmed = true;
					reinterpret_cast<AttachChild>(ssnVtable[0x1D0 / sizeof(void*)])(
						packet.privateShadowSceneNode, packet.camera, true);
					void* cameraParent = nullptr;
					packet.cameraAttached =
						SafeLoad(reinterpret_cast<std::uintptr_t>(packet.camera) + 0x28, cameraParent) &&
						cameraParent == packet.privateShadowSceneNode;
					if (!packet.cameraAttached)
						__leave;

					packet.rootDetachArmed = true;
					reinterpret_cast<AttachChild>(ssnVtable[0x1D0 / sizeof(void*)])(
						packet.privateShadowSceneNode, packet.playerRoot, true);
					void* rootParent = nullptr;
					packet.rootAttached =
						SafeLoad(reinterpret_cast<std::uintptr_t>(packet.playerRoot) + 0x28, rootParent) &&
						rootParent == packet.privateShadowSceneNode;
					if (!packet.rootAttached)
						__leave;

					auto* renderer = reinterpret_cast<std::byte*>(packet.privateRenderer);
					reinterpret_cast<UpdateLights>(Address(kRVA_Interface3DUpdateLights))(
						renderer, renderer + 0x1C8, packet.privateShadowSceneNode,
						reinterpret_cast<bool*>(renderer + kInterfaceRendererNeedsLightSetupOffset));
					packet.lightsUpdated = true;
					reinterpret_cast<std::int32_t (*)(void*, void*)>(Address(kRVA_ProcessQueuedLights))(
						packet.privateShadowSceneNode, culler);
					packet.queuedLightsProcessed = true;
					RE::NiUpdateData update{};
					reinterpret_cast<void (*)(void*, RE::NiUpdateData*)>(Address(kRVA_NiAVObjectUpdate))(
						packet.privateShadowSceneNode, std::addressof(update));
					packet.shadowSceneNodeUpdated = true;
					packet.sunDetachArmed = true;
					reinterpret_cast<void (*)(void*, void*)>(Address(kRVA_SetSunLight))(
						packet.privateShadowSceneNode, packet.privateDirectionalLight);
					packet.sunAttached = true;
					packet.selectorCensusCaptured =
						CaptureFlattenedModelCensus(packet.playerRoot, packet.selectorCensus);
					packet.selectorCensusMatchesEntry = packet.selectorCensusCaptured &&
						packet.entryCensus &&
						SameFlattenedModelCensus(*packet.entryCensus, packet.selectorCensus);
					if (!packet.selectorCensusMatchesEntry || !packet.resourceTransaction)
						__leave;
					packet.forwardGeometryProofArmed =
						packet.resourceTransaction->ArmForwardGeometryProof(
							packet.selectorCensus.geometries.data(),
							packet.selectorCensus.shaderProperties.data(),
							packet.selectorCensus.selectorEligible.data(),
							packet.selectorCensus.count,
							packet.requiredEyeGeometries.data(),
							packet.requiredEyeGeometryCount);
					if (!packet.forwardGeometryProofArmed)
						__leave;

					*reinterpret_cast<float*>(Address(kRVA_InterfaceDisplayGeometry)) = 0.0f;
					*reinterpret_cast<std::uint32_t*>(Address(kRVA_InterfaceGlobal1D34)) =
						packet.interfaceGlobal1D34Source;
					*reinterpret_cast<float*>(Address(kRVA_InterfacePostAA)) = 0.0f;
					*reinterpret_cast<float*>(Address(kRVA_InterfaceOpacityAlpha)) =
						packet.opacityAlpha;
					reinterpret_cast<SetRenderTarget>(Address(kRVA_SetCurrentRenderTarget))(
						packet.targetManager, 0, kOutputLogicalColor, 3);
					reinterpret_cast<void (*)(void*)>(Address(kRVA_SetViewportDefault))(
						packet.targetManager);
					reinterpret_cast<SetDepthStencil>(Address(kRVA_SetCurrentDepthStencil))(
						packet.targetManager, kOutputLogicalDepth, 3, 0, 0);
					reinterpret_cast<void (*)(void*)>(Address(kRVA_RendererFlush))(
						reinterpret_cast<void*>(Address(kRVA_Renderer)));
					LogNativeForwardCheckpoint("prefix-target-depth-flushed", packet);
					*reinterpret_cast<float*>(Address(kRVA_InterfaceMenuDiffuseIntensity)) =
						packet.menuDiffuseIntensity;
					*reinterpret_cast<float*>(Address(kRVA_InterfaceMenuEmitIntensity)) =
						packet.menuEmitIntensity;
					const auto selectorStackHeadroom = MeasureNativeStackHeadroom();
					LogNativeStackHeadroom("before-draw-model-forward", selectorStackHeadroom);
					packet.forwardSelectorObservationArmed =
						packet.resourceTransaction->BeginForwardSelectorObservation(
							packet.exclusiveAccumulator);
					if (!packet.forwardSelectorObservationArmed)
						__leave;
					LogNativeForwardCheckpoint("draw-model-forward-enter", packet);
					reinterpret_cast<DrawModelForward>(Address(kRVA_Interface3DDrawModelForward))(
						packet.privateRenderer, packet.playerRoot, packet.privateShadowSceneNode,
						packet.camera, kOutputLogicalColor);
					packet.forwardSelectorReturned = true;
					packet.forwardSelectorObservationCompleted =
						packet.resourceTransaction->EndForwardSelectorObservation(true);
					LogNativeForwardCheckpoint("draw-model-forward-return", packet);
				} __finally {
					CleanupNativeForwardLeaf(packet);
				}
				packet.nativeCompleted = packet.shadowSceneNodePublished && packet.cameraPublished &&
					packet.shaderCameraPublished &&
					packet.cullerConstructed && packet.cullerAccumulatorSet && packet.cullerCameraSet &&
					packet.accumulatorForwardStateSet && packet.cameraAttached && packet.rootAttached &&
					packet.lightsUpdated && packet.queuedLightsProcessed &&
					packet.shadowSceneNodeUpdated && packet.sunAttached &&
					packet.selectorCensusCaptured && packet.selectorCensusMatchesEntry &&
					packet.forwardGeometryProofArmed && packet.forwardSelectorObservationArmed &&
					packet.forwardSelectorObservationCompleted &&
					packet.depth3Acquired && packet.nativeStackHeadroomAttested &&
					packet.forwardSelectorReturned && packet.cameraDetached && packet.rootDetached &&
					packet.sunDetached && packet.cullerDestroyed;
			} __except (NativeExceptionFilter(GetExceptionInformation(), packet)) {
			}
			LogNativeForwardCheckpoint("leaf-return", packet);
		}

		struct NativeCleanupProof
		{
			std::uint32_t exceptionCode{};
			bool activePassesCleared{};
			bool accumulatingFinished{};
			bool rootPassesCleared{};
			bool engineCleanupReturned{};
			bool sortedRecordRepaired{};
		};

		void AttestNativeReturnCleanup(const NativeRunPacket& packet, NativeCleanupProof& proof) noexcept
		{

			proof.exceptionCode = packet.exceptionCode;
			proof.activePassesCleared = packet.forwardSelectorReturned;
			proof.accumulatingFinished = packet.forwardSelectorReturned;
			proof.rootPassesCleared = packet.forwardSelectorReturned;
			proof.engineCleanupReturned = packet.cameraDetached && packet.rootDetached &&
			                              packet.sunDetached && packet.cullerDestroyed;
			proof.sortedRecordRepaired = proof.engineCleanupReturned;
		}

		struct CameraRestoreProof
		{
			std::uint32_t exceptionCode{};
			bool cameraCallCompleted{};
			bool accumulatorGlobalRestored{};
			bool shadowSceneNodeRestored{};
		};

		void RestoreCameraLeaf(RE::NiCamera* camera, void* accumulator,
			void* shadowSceneNode, CameraRestoreProof& proof) noexcept
		{
			__try {
				using CacheCameraData = void (*)(void*, RE::NiCamera*, bool);
				using SetCameraData = void (*)(void*, RE::NiCamera*, bool, float, float);
				const float cameraFlush = *reinterpret_cast<const float*>(Address(kRVA_CameraFlushScalar));
				auto* graphicsState = reinterpret_cast<void*>(Address(kRVA_GraphicsState));
				reinterpret_cast<CacheCameraData>(Address(kRVA_CacheCameraData))(
					graphicsState, camera, true);
				reinterpret_cast<SetCameraData>(Address(kRVA_SetCameraData))(
					graphicsState, camera, true, 0.0f, cameraFlush);
				proof.cameraCallCompleted = true;
			} __except (proof.exceptionCode = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER) {
			}
			__try {
				reinterpret_cast<void (*)(void*)>(Address(kRVA_SetCurrentAccumulator))(accumulator);
				proof.accumulatorGlobalRestored = true;
			} __except (proof.exceptionCode = proof.exceptionCode ? proof.exceptionCode :
			                                                        GetExceptionCode(),
				EXCEPTION_EXECUTE_HANDLER) {
			}
			__try {
				reinterpret_cast<void (*)(std::uint32_t, void*)>(Address(kRVA_SetShadowSceneNode))(
					4, shadowSceneNode);
				proof.shadowSceneNodeRestored = true;
			} __except (proof.exceptionCode = proof.exceptionCode ? proof.exceptionCode :
			                                                        GetExceptionCode(),
				EXCEPTION_EXECUTE_HANDLER) {
			}
		}

		[[nodiscard]] bool AllFinite(const std::array<float, 16>& values) noexcept
		{
			return std::all_of(values.begin(), values.end(), [](float value) { return std::isfinite(value); });
		}

		[[nodiscard]] std::uint64_t HashProjectionIdentity(const CompletionProof& proof) noexcept
		{
			constexpr std::uint64_t offset = 1469598103934665603ull;
			constexpr std::uint64_t prime = 1099511628211ull;
			std::uint64_t hash = offset;
			auto add = [&](const void* data, std::size_t size) noexcept {
				const auto* bytes = static_cast<const std::uint8_t*>(data);
				for (std::size_t index = 0; index < size; ++index) {
					hash ^= bytes[index];
					hash *= prime;
				}
			};
			add(proof.captureViewProjection.data(), sizeof(proof.captureViewProjection));
			add(proof.captureViewProjectionUnjittered.data(),
				sizeof(proof.captureViewProjectionUnjittered));
			add(std::addressof(proof.viewportTopLeftX), sizeof(float) * 6);
			add(std::addressof(proof.captureCamera), sizeof(proof.captureCamera));
			return hash ? hash : 1;
		}

		[[nodiscard]] bool CaptureProjectionProof(const Request& request,
			const ResourceTransaction& resources, CompletionProof& proof) noexcept
		{
			constexpr std::size_t stateViewProjectionOffset = 0x160 + 0xD0;
			constexpr std::size_t stateViewProjectionUnjitteredOffset = 0x160 + 0x110;
			constexpr std::size_t stateReferenceCameraOffset = 0x160 + 0x238;
			if (!SafeCopy(proof.captureViewProjection.data(),
					reinterpret_cast<void*>(Address(kRVA_GraphicsState) + stateViewProjectionOffset),
					sizeof(proof.captureViewProjection)) ||
				!SafeCopy(proof.captureViewProjectionUnjittered.data(),
					reinterpret_cast<void*>(Address(kRVA_GraphicsState) +
											stateViewProjectionUnjitteredOffset),
					sizeof(proof.captureViewProjectionUnjittered)) ||
				!SafeLoad(Address(kRVA_GraphicsState) + stateReferenceCameraOffset,
					proof.cameraStateReference))
				return false;

			std::array<D3D11_VIEWPORT, D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> viewports{};
			UINT viewportCount = static_cast<UINT>(viewports.size());
			request.immediateContext->RSGetViewports(std::addressof(viewportCount), viewports.data());
			proof.viewportCount = viewportCount;
			if (viewportCount == 0)
				return false;
			const auto& viewport = viewports[0];
			proof.viewportTopLeftX = viewport.TopLeftX;
			proof.viewportTopLeftY = viewport.TopLeftY;
			proof.viewportWidth = viewport.Width;
			proof.viewportHeight = viewport.Height;
			proof.viewportMinDepth = viewport.MinDepth;
			proof.viewportMaxDepth = viewport.MaxDepth;

			std::array<D3D11_RECT, D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> scissors{};
			UINT scissorCount = static_cast<UINT>(scissors.size());
			request.immediateContext->RSGetScissorRects(std::addressof(scissorCount), scissors.data());
			proof.scissorCount = scissorCount;
			ComPtr<ID3D11RasterizerState> rasterizer;
			request.immediateContext->RSGetState(rasterizer.GetAddressOf());
			D3D11_RASTERIZER_DESC rasterizerDescription{};
			if (rasterizer)
				rasterizer->GetDesc(std::addressof(rasterizerDescription));

			void* shaderCamera = nullptr;
			const bool shaderCameraRead = SafeLoad(Address(kRVA_ShaderCamera), shaderCamera);
			proof.captureProjectionAttested = shaderCameraRead &&
				shaderCamera == request.captureCamera.get() && request.expectedCameraStateReference != 0 &&
				proof.cameraStateReference == request.expectedCameraStateReference &&
				AllFinite(proof.captureViewProjection) && AllFinite(proof.captureViewProjectionUnjittered);
			proof.fullOriginViewportAttested = viewportCount == 1 && viewport.TopLeftX == 0.0f &&
			                                   viewport.TopLeftY == 0.0f && viewport.Width == static_cast<float>(resources.sourceColorDesc.Width) &&
			                                   viewport.Height == static_cast<float>(resources.sourceColorDesc.Height) &&
			                                   viewport.MinDepth == 0.0f && viewport.MaxDepth == 1.0f;
			proof.noRestrictiveScissorAttested = !rasterizer || !rasterizerDescription.ScissorEnable ||
			                                     (scissorCount == 1 && scissors[0].left <= 0 && scissors[0].top <= 0 &&
													 scissors[0].right >= static_cast<LONG>(resources.sourceColorDesc.Width) &&
													 scissors[0].bottom >= static_cast<LONG>(resources.sourceColorDesc.Height));
			proof.projectionIdentity = HashProjectionIdentity(proof);
			return proof.captureProjectionAttested && proof.fullOriginViewportAttested &&
			       proof.noRestrictiveScissorAttested;
		}

		struct CaptureLockGuard
		{
			~CaptureLockGuard() noexcept { g_captureLock.clear(std::memory_order_release); }
		};

		[[nodiscard]] Result CaptureImpl(const Request& input)
		{

			const Request request = input;
			Result result{};
			auto& proof = result.proof;
			proof.frameSerial = request.frameSerial;
			proof.externalPlayerStateGeneration = request.externalPlayerStateGeneration;
			proof.privateRendererIdentitySerial = request.privateRendererIdentitySerial;
			proof.playerRoot = reinterpret_cast<std::uintptr_t>(request.playerRoot.get());
			proof.captureCamera = reinterpret_cast<std::uintptr_t>(request.captureCamera.get());
			proof.restoreCamera = reinterpret_cast<std::uintptr_t>(request.restoreCamera.get());
			proof.privateRenderer = reinterpret_cast<std::uintptr_t>(request.privateRenderer);
			proof.accumulator = reinterpret_cast<std::uintptr_t>(request.exclusiveAccumulator);
			proof.shadowSceneNode =
				reinterpret_cast<std::uintptr_t>(request.privateShadowSceneNode.get());
			proof.cameraConstantBuffer = reinterpret_cast<std::uintptr_t>(request.cameraConstantBuffer);
			proof.coverageRule = CoverageRule::kForwardZDepthLessThanOne;
			proof.depthEncoding = DepthEncoding::kFlatForwardDeviceDepth;
			proof.cameraHandedness = request.cameraHandedness;
			proof.clearDepth = 1.0f;
			proof.nativeOpaqueDepthComparison = D3D11_COMPARISON_LESS_EQUAL;
			proof.visibilityStatus = VisibilityStatus::kNotReadBack;
			proof.directPlanarDepthComparable = false;
			proof.colorAlphaIsCoverage = false;
			bool privateRendererAttestedOnEntry = false;
			bool privateRendererRejectedOnEntry = false;
			bool nativeMutationBegan = false;
			bool nativeReservationStateAmbiguous = false;
			bool privateRendererDispositionClassified = false;

			auto fail = [&](Status status) -> Result {
				result.status = status;
				proof.status = status;

				if (nativeReservationStateAmbiguous) {

					proof.privateRendererReusable = false;
					proof.privateRendererQuarantineRequired = true;
					privateRendererDispositionClassified = true;
				} else if (!nativeMutationBegan) {
					proof.privateRendererReusable = !privateRendererRejectedOnEntry && request.device &&
						request.device->GetDeviceRemovedReason() == S_OK;
					proof.privateRendererQuarantineRequired = false;
					if (privateRendererAttestedOnEntry)
						proof.privateRendererPostconditionAttested = true;
					privateRendererDispositionClassified = true;
				} else if (!privateRendererDispositionClassified) {

					proof.privateRendererReusable = false;
					proof.privateRendererQuarantineRequired = true;
				}
				result.color.Reset();
				result.colorSRV.Reset();
				result.depth.Reset();
				result.depthSRV.Reset();
				return result;
			};

			if (!request.playerRoot || !request.captureCamera || !request.restoreCamera ||
				!request.privateRenderer || !request.exclusiveAccumulator ||
				!request.privateShadowSceneNode || !request.privateDirectionalLight ||
				request.privatePointLightCount > Request::kMaxPrivatePointLights ||
				!request.device || !request.immediateContext ||
				!request.cameraConstantBuffer || request.expectedCameraStateReference == 0 ||
				request.renderThreadId == 0 ||
				request.externalPlayerStateGeneration == 0 || request.privateRendererIdentitySerial == 0 ||
				request.frameSerial == 0 ||
				request.requiredEyeGeometryCount == 0 ||
				request.requiredEyeGeometryCount > request.requiredEyeGeometries.size() ||
				(request.cameraHandedness != CameraHandedness::kOrdinary &&
					request.cameraHandedness != CameraHandedness::kReflected) ||
				request.playerRoot.get() == request.privateShadowSceneNode.get() ||
				request.captureCamera.get() == request.restoreCamera.get() ||
				reinterpret_cast<void*>(request.playerRoot.get()) == request.privateRenderer)
				return fail(Status::kInvalidRequest);
			if (GetCurrentThreadId() != request.renderThreadId)
				return fail(Status::kWrongThread);
			if (g_captureLock.test_and_set(std::memory_order_acquire))
				return fail(Status::kBusy);
			CaptureLockGuard captureLock{};
			if (g_activeForwardTransaction)
				return fail(Status::kBusy);
			CaptureScratchClaim scratchClaim{ g_captureScratchStorage };
			if (!scratchClaim.armed)
				return fail(Status::kBusy);
			auto& entryPipeline = *g_captureScratchStorage.entryPipeline.Get();
			auto& restoredPipeline = *g_captureScratchStorage.restoredPipeline.Get();
			auto& engine = *g_captureScratchStorage.engine.Get();
			auto& entryFlattenedCensus = *g_captureScratchStorage.entryFlattenedCensus.Get();
			auto& returnedFlattenedCensus =
				*g_captureScratchStorage.returnedFlattenedCensus.Get();
			auto& resources = *g_captureScratchStorage.resources.Get();
			auto& packet = *g_captureScratchStorage.packet.Get();

			proof.runtimeAttested =
				(REL::Module::get().version() == REL::Version{ 1, 10, 163, 0 } || ReflectionRuntime::IsPort240()) &&
				!REL::Module::IsVR();
			if (!proof.runtimeAttested)
				return fail(Status::kUnsupportedRuntime);
			proof.executableHashAttested = MatchExecutableFileHash();
			if (!proof.executableHashAttested)
				return fail(Status::kContractMismatch);

			const bool frozenCodeAttested = AttestFrozenCodeContract();
			proof.bodyHashesAttested = frozenCodeAttested;
			proof.dfLightMappingAttested = frozenCodeAttested;
			proof.abiCallsiteAttested = frozenCodeAttested;
			proof.forwardSelectorAttested = frozenCodeAttested;
			if (!proof.bodyHashesAttested || !proof.dfLightMappingAttested ||
				!proof.abiCallsiteAttested || !proof.forwardSelectorAttested)
				return fail(Status::kContractMismatch);

			if (request.immediateContext->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE)
				return fail(Status::kInvalidRequest);
			ComPtr<ID3D11Device> contextDevice;
			request.immediateContext->GetDevice(contextDevice.GetAddressOf());
			if (contextDevice.Get() != request.device ||
				!ResourceUsesDevice(request.cameraConstantBuffer, request.device))
				return fail(Status::kInvalidRequest);
			D3D11_BUFFER_DESC cameraBufferDescription{};
			request.cameraConstantBuffer->GetDesc(std::addressof(cameraBufferDescription));
			if ((cameraBufferDescription.BindFlags & D3D11_BIND_CONSTANT_BUFFER) == 0 ||
				cameraBufferDescription.ByteWidth == 0 || (cameraBufferDescription.ByteWidth & 15u) != 0 ||
				(cameraBufferDescription.Usage != D3D11_USAGE_DEFAULT &&
					cameraBufferDescription.Usage != D3D11_USAGE_DYNAMIC))
				return fail(Status::kInvalidRequest);
			if (request.device->GetDeviceRemovedReason() != S_OK)
				return fail(Status::kDeviceRemoved);

			if (!entryPipeline.Capture(request.immediateContext))
				return fail(Status::kUnsupportedPipelineState);
			constexpr std::size_t kNativeCameraBufferSlot = 12;
			if (entryPipeline.vs.constantBuffers[kNativeCameraBufferSlot].Get() !=
				request.cameraConstantBuffer)
				return fail(Status::kInvalidRequest);

			if (!engine.Capture(request.exclusiveAccumulator))
				return fail(Status::kMissingRenderContext);
			if (engine.currentAccumulator == request.exclusiveAccumulator)
				return fail(Status::kAliasedMainAccumulator);
			if (engine.shaderCamera != request.restoreCamera.get() ||
				engine.cameraStateReference == nullptr ||
				engine.accumulatorCamera != nullptr)
				return fail(Status::kInvalidRequest);
			proof.captureCameraCachePreexisting =
				engine.ContainsCachedCamera(request.captureCamera.get(), true) &&
				engine.ContainsCachedCamera(request.captureCamera.get(), false);
			proof.restoreCameraCachePreexisting =
				engine.ContainsCachedCamera(request.restoreCamera.get(), true);
			if (!proof.captureCameraCachePreexisting || !proof.restoreCameraCachePreexisting)
				return fail(Status::kInvalidRequest);

			PrivateRendererFacts privateRenderer{};
			if (!AttestPrivateRenderer(request, engine, privateRenderer)) {
				proof.privateRendererEntryFailureCode = privateRenderer.failureCode;
				privateRendererRejectedOnEntry = true;
				const bool aliasesMain = std::any_of(engine.activeSSNSlots.begin(),
					engine.activeSSNSlots.end(), [&](void* active) {
						return active == request.privateShadowSceneNode.get();
					});
				return fail(aliasesMain ? Status::kAliasedMainLightingContext :
										  Status::kInvalidPrivateRenderer);
			}
			proof.accumulatorWasExclusive = true;
			proof.privateLightingContextAttested = true;
			proof.privateRendererDisabledOnEntry = !privateRenderer.enabled &&
			                                       !privateRenderer.offscreenEnabled;
			proof.privateRendererIdleOnEntry = privateRenderer.rootsIdle;
			proof.privateLightingAuthorityAttested = privateRenderer.lightPinsHeld &&
			                                         privateRenderer.directionalLight != nullptr;
			proof.privateSceneGraphAttested = privateRenderer.privateSceneGraphAttested;
			proof.privateLightPinsHeld = privateRenderer.lightPinsHeld;
			proof.privateLightCount = 1u + privateRenderer.mainLightCount;
			privateRendererAttestedOnEntry = true;
			proof.strongRootPinHeld = true;
			proof.captureCameraDetachedOnEntry =
				AttestDetachedObject(reinterpret_cast<RE::NiAVObject*>(request.captureCamera.get()));
			proof.playerRootDetachedOnEntry = AttestDetachedPlayerRoot(request);
			if (!proof.captureCameraDetachedOnEntry || !proof.playerRootDetachedOnEntry)
				return fail(Status::kInvalidRequest);
			proof.flattenedModelCensusAttested =
				CaptureFlattenedModelCensus(request.playerRoot.get(), entryFlattenedCensus);
			if (!proof.flattenedModelCensusAttested)
				return fail(Status::kInvalidRequest);
			proof.flattenedGeometryIdentity = entryFlattenedCensus.identity;
			proof.flattenedGeometryCount = entryFlattenedCensus.count;
			proof.flattenedDrawableCount = entryFlattenedCensus.drawableCount;
			proof.requiredEyeGeometryCount = request.requiredEyeGeometryCount;
			const bool entryEyeGeometryAuthorityAttested = AttestRequiredEyeGeometryCensus(
				request, entryFlattenedCensus, proof.authoritativeEyeGeometryCount);
			if (!entryEyeGeometryAuthorityAttested)
				return fail(Status::kInvalidRequest);

			Depth3ReservationSnapshot depth3Reservation{};
			auto* depth3TargetManager = reinterpret_cast<RE::BSGraphics::RenderTargetManager*>(
				Address(kRVA_RenderTargetManager));
			proof.depth3ReservationAttested =
				depth3Reservation.Capture(depth3TargetManager);
			auto logDepth3ReservationReject = [&](const char* phase,
				std::uint32_t exceptionCode) noexcept {
				static std::atomic_uint32_t rejectLogs{};
				if (rejectLogs.fetch_add(1, std::memory_order_relaxed) >= 8u)
					return;
				try {
					spdlog::warn(
						"[FlatDeferredPlayerCapture] depth3 reservation {} rejected: detail={} cold={} exception=0x{:08X} entry=0x{:X}/phys={}/ref={}/owner={}/map={} word=0x{:04X} observed=0x{:X}/phys={}/ref={}/owner={}/map={} word=0x{:04X}",
						phase, depth3Reservation.failureCode, depth3Reservation.cold,
						exceptionCode,
						reinterpret_cast<std::uintptr_t>(depth3Reservation.pointer),
						depth3Reservation.handle.physical, depth3Reservation.handle.refCount,
						depth3Reservation.handle.logicalOwner, depth3Reservation.mapping,
						depth3Reservation.persistency,
						reinterpret_cast<std::uintptr_t>(depth3Reservation.observedPointer),
						depth3Reservation.observedHandle.physical,
						depth3Reservation.observedHandle.refCount,
						depth3Reservation.observedHandle.logicalOwner,
						depth3Reservation.observedMapping,
						depth3Reservation.observedPersistency);
				} catch (...) {
				}
			};
			if (!proof.depth3ReservationAttested) {
				logDepth3ReservationReject("snapshot", 0u);
				return fail(Status::kInvalidLogicalTarget);
			}
			proof.depth3ReservationHandle =
				reinterpret_cast<std::uintptr_t>(depth3Reservation.pointer);
			proof.depth3ReservationPhysical = depth3Reservation.handle.physical;
			proof.depth3ReservationRefCount = depth3Reservation.handle.refCount;
			proof.depth3ReservationLogicalOwner = depth3Reservation.handle.logicalOwner;

			Depth3ReservationLease depth3Lease{ depth3TargetManager, depth3Reservation };
			auto failWithDepth3Release = [&](Status status) -> Result {
				proof.depth3ReservationRestored = depth3Lease.Release();
				if (depth3Lease.exceptionCode != 0) {
					proof.sehExceptionCode = depth3Lease.exceptionCode;
					status = Status::kNativeException;
				} else if (!proof.depth3ReservationRestored) {
					status = Status::kRestoreFailed;
				}
				if (!proof.depth3ReservationRestored) {

					nativeReservationStateAmbiguous = true;
				}
				return fail(status);
			};
			if (!depth3Lease.Acquire()) {
				logDepth3ReservationReject("acquire", depth3Lease.exceptionCode);
				return failWithDepth3Release(depth3Lease.exceptionCode != 0 ?
					Status::kNativeException : Status::kInvalidLogicalTarget);
			}
			proof.depth3ReservationPhysical = depth3Reservation.observedMapping < kPhysicalDepthCount ?
				static_cast<std::int32_t>(depth3Reservation.observedMapping) : -1;
			if (!depth3Reservation.persistent) {
				proof.depth3ReservationRefCount = depth3Reservation.observedHandle.refCount;
				proof.depth3ReservationLogicalOwner = depth3Reservation.observedHandle.logicalOwner;
			}

			if (depth3Reservation.cold)
				return failWithDepth3Release(Status::kResourcePoolWarming);

			if (!resources.Prepare(request, entryPipeline, engine.renderContext,
					engine.global1BC6 != 0,
					engine.global1BC7 != 0, proof))
				return failWithDepth3Release(Status::kResourceBackupFailed);
			proof.hotPathResourceCreations = resources.poolResourcesCreated;
			proof.resourcePoolWarmOnEntry = resources.poolResourcesCreated == 0;
			if (!proof.resourcePoolWarmOnEntry)
				return failWithDepth3Release(Status::kResourcePoolWarming);
			if (resources.targetManager != depth3TargetManager ||
				resources.depthPhysical != depth3Reservation.observedMapping ||
				!depth3Lease.IsAcquired()) {
				logDepth3ReservationReject("held", depth3Lease.exceptionCode);
				return failWithDepth3Release(Status::kInvalidLogicalTarget);
			}
			const auto preMutationStackHeadroom = MeasureNativeStackHeadroom();
			if (!preMutationStackHeadroom.valid ||
				preMutationStackHeadroom.remaining < kMinimumNativeStackHeadroom)
				return failWithDepth3Release(Status::kInvalidRequest);

			LogNativeStackHeadroom("before-clear", preMutationStackHeadroom);
			proof.logicalWriteSetAttested = proof.bodyHashesAttested &&
			                                proof.privateLightingContextAttested;

			nativeMutationBegan = true;
			g_captureEscapePhase = CaptureEscapePhase::kPostMutation;
			resources.ClearForCapture();

			packet.privateRenderer = request.privateRenderer;
			packet.targetManager = resources.targetManager;
			packet.exclusiveAccumulator = request.exclusiveAccumulator;
			packet.resourceTransaction = std::addressof(resources);
			packet.camera = request.captureCamera.get();
			packet.playerRoot = request.playerRoot.get();
			packet.entryCensus = std::addressof(entryFlattenedCensus);
			packet.requiredEyeGeometries = request.requiredEyeGeometries;
			packet.requiredEyeGeometryCount = request.requiredEyeGeometryCount;
			packet.privateShadowSceneNode = request.privateShadowSceneNode.get();
			packet.privateDirectionalLight = request.privateDirectionalLight.get();
			packet.depth3Acquired = true;

			packet.nativeStackHeadroomAttested = true;
			packet.interfaceGlobal1D34Source = privateRenderer.interfaceGlobal1D34Source;
			packet.opacityAlpha = privateRenderer.opacityAlpha;
			packet.menuDiffuseIntensity = privateRenderer.menuDiffuseIntensity;
			packet.menuEmitIntensity = privateRenderer.menuEmitIntensity;

			bool reflectedRasterizerTransactionRestored = false;
			{
				ActiveForwardTransactionGuard forwardTransaction{
					resources, reflectedRasterizerTransactionRestored
				};
				proof.reflectedRasterizerTransactionArmed = forwardTransaction.armed;
				if (forwardTransaction.armed) {

					packet.privateNativeMutationBegan = true;
					const bool forwardQueriesReady = BeginForwardQueriesLeaf(resources, packet);
					if (forwardQueriesReady)
						RunNativeLeaf(packet);
					LogNativeForwardCheckpoint("capture-after-native-leaf", packet);
					if (resources.forwardOcclusionQueryBegan || resources.forwardStatisticsQueryBegan)
						static_cast<void>(EndForwardQueriesLeaf(resources, packet));
				}
			}
			proof.reflectedRasterizerTransactionRestored =
				reflectedRasterizerTransactionRestored && !g_activeForwardTransaction;

			const bool depth3ReservationHeld = depth3Lease.IsAcquired();
			proof.flattenedModelCensusAttested = proof.flattenedModelCensusAttested &&
				packet.selectorCensusCaptured && packet.selectorCensusMatchesEntry;
			proof.forwardExpectedGeometryCount = resources.forwardExpectedGeometryCount;
			proof.forwardGeometrySetupCalls = resources.forwardGeometrySetupCalls;
			proof.forwardGeometryRestoreCalls = resources.forwardGeometryRestoreCalls;
			proof.forwardForeignGeometryCallbacks = resources.forwardForeignGeometryCallbacks;
			proof.forwardNullGeometryCallbacks = resources.forwardNullGeometryCallbacks;
			proof.forwardUnbalancedGeometryCallbacks =
				resources.forwardUnbalancedGeometryCallbacks;
			proof.forwardForeignOutputCallbacks = resources.forwardForeignOutputCallbacks;
			proof.forwardGeometryPassesAttested = packet.forwardSelectorReturned &&
				resources.AttestForwardGeometryProof(
					proof.forwardObservedGeometryCount,
					proof.forwardOrdinaryGeometryCount,
					proof.forwardSpecialAlphaGeometryCount,
					proof.forwardNoPassGeometryCount,
					proof.forwardRoutedEyeGeometryCount);
			proof.forwardSelectorResultCalls = resources.forwardSelectorResultCalls;
			static std::atomic<std::uint32_t> forwardProofLogs{ 0 };
			if (forwardProofLogs.fetch_add(1, std::memory_order_relaxed) < 12u) {
				try {
					spdlog::info(
						"[FlatDeferredPlayerCapture] selector proof: raw={} eligible={} expected={} calls={} observed={} routes ordinary/special/no-pass={}/{}/{} routedEyes={} setup/restore={}/{} foreign/null/unbalanced/output={}/{}/{}/{} vsRepair attempts/accepted/rejected/mismatch={}/{}/{}/{} attested={}",
						proof.flattenedGeometryCount, proof.flattenedDrawableCount,
						proof.forwardExpectedGeometryCount, proof.forwardSelectorResultCalls,
						proof.forwardObservedGeometryCount, proof.forwardOrdinaryGeometryCount,
						proof.forwardSpecialAlphaGeometryCount, proof.forwardNoPassGeometryCount,
						proof.forwardRoutedEyeGeometryCount, proof.forwardGeometrySetupCalls,
						proof.forwardGeometryRestoreCalls, proof.forwardForeignGeometryCallbacks,
						proof.forwardNullGeometryCallbacks, proof.forwardUnbalancedGeometryCallbacks,
						proof.forwardForeignOutputCallbacks,
						resources.essentialVertexShaderRepairAttempts,
						resources.essentialVertexShaderRepairAcceptances,
						resources.essentialVertexShaderRepairRejections,
						resources.essentialVertexShaderRepairOutcomeMismatches,
						proof.forwardGeometryPassesAttested);
				} catch (...) {
				}
			}
			proof.reflectedRasterizerApplyAttempts = resources.reflectedRasterizerApplyAttempts;
			proof.reflectedRasterizerApplySuccesses = resources.reflectedRasterizerApplySuccesses;
			proof.reflectedRasterizerApplyFailures = resources.reflectedRasterizerApplyFailures;
			proof.ordinaryRasterizerPolicyCommits = resources.ordinaryRasterizerPolicyCommits;
			proof.unattributedRasterizerPolicyCommits =
				resources.unattributedRasterizerPolicyCommits;
			const bool rasterizerPolicyTotalsAttested = resources.reflectedHandednessRequired ?
				(resources.reflectedRasterizerApplyAttempts >= resources.forwardGeometrySetupCalls &&
					(resources.forwardGeometrySetupCalls == 0 ||
						resources.reflectedRasterizerApplyAttempts != 0) &&
					resources.reflectedRasterizerApplySuccesses ==
						resources.reflectedRasterizerApplyAttempts &&
					resources.reflectedRasterizerApplyFailures == 0 &&
					resources.ordinaryRasterizerPolicyCommits == 0) :
				(resources.reflectedRasterizerApplyAttempts == 0 &&
					resources.reflectedRasterizerApplySuccesses == 0 &&
					resources.reflectedRasterizerApplyFailures == 0 &&
					resources.ordinaryRasterizerPolicyCommits >= resources.forwardGeometrySetupCalls);
			proof.reflectedRasterizerCommitAttested =
				proof.reflectedRasterizerTransactionArmed &&
				proof.reflectedRasterizerTransactionRestored &&
				proof.forwardGeometryPassesAttested &&
				resources.unattributedRasterizerPolicyCommits == 0 &&
				rasterizerPolicyTotalsAttested;
			LogNativeForwardCheckpoint("final-depth-enter", packet);
			if (packet.nativeCompleted && depth3ReservationHeld)
				resources.CaptureFinalForwardDepth();
			LogNativeForwardCheckpoint("final-depth-return", packet);

			const bool returnedCensusCaptured =
				CaptureFlattenedModelCensus(request.playerRoot.get(), returnedFlattenedCensus);
			proof.flattenedModelCensusStable = packet.nativeCompleted &&
				packet.selectorCensusMatchesEntry && returnedCensusCaptured &&
				SameFlattenedModelCensus(entryFlattenedCensus, packet.selectorCensus) &&
				SameFlattenedModelCensus(entryFlattenedCensus, returnedFlattenedCensus) &&
				SameFlattenedModelCensus(packet.selectorCensus, returnedFlattenedCensus);
			proof.eyeGeometryAuthorityAttested = entryEyeGeometryAuthorityAttested &&
				proof.flattenedModelCensusStable;
			proof.forwardSpecializedBranchAttested = proof.flattenedModelCensusStable &&
				proof.eyeGeometryAuthorityAttested &&
				packet.forwardSelectorReturned && proof.forwardGeometryPassesAttested &&
				proof.forwardExpectedGeometryCount == proof.flattenedDrawableCount &&
				proof.forwardObservedGeometryCount == proof.forwardExpectedGeometryCount &&
				proof.forwardRoutedEyeGeometryCount != 0;

			bool projectionCaptured = false;
			LogNativeForwardCheckpoint("projection-proof-enter", packet);
			if (packet.nativeCompleted)
				projectionCaptured = CaptureProjectionProof(request, resources, proof);
			LogNativeForwardCheckpoint("projection-proof-return", packet);

			NativeCleanupProof cleanup{};
			AttestNativeReturnCleanup(packet, cleanup);

			LogNativeForwardCheckpoint("clear-state-enter", packet);
			request.immediateContext->ClearState();
			LogNativeForwardCheckpoint("clear-state-return", packet);
			const bool nativeCleanupSucceeded = cleanup.exceptionCode == 0 &&
			                                    cleanup.activePassesCleared && cleanup.accumulatingFinished &&
			                                    cleanup.rootPassesCleared && cleanup.engineCleanupReturned &&
			                                    cleanup.sortedRecordRepaired &&
			                                    proof.reflectedRasterizerTransactionArmed &&
			                                    proof.reflectedRasterizerTransactionRestored;
			std::uint64_t coveredPixels = 0;
			std::uint64_t mappedDepthPixels = 0;
			std::uint64_t informativeColorPixels = 0;
			std::uint64_t spatiallyVariantColorPixels = 0;
			bool depthReadbackCompleted = false;
			bool colorReadbackCompleted = false;
			LogNativeForwardCheckpoint("copy-result-enter", packet);
			const bool resultCopied = packet.nativeCompleted && depth3ReservationHeld &&
			                          projectionCaptured &&
			                          proof.flattenedModelCensusStable &&
			                          proof.forwardSpecializedBranchAttested &&
			                          proof.forwardGeometryPassesAttested &&
			                          proof.reflectedRasterizerCommitAttested &&
			                          nativeCleanupSucceeded &&
			                          resources.CopyResult(result, coveredPixels, mappedDepthPixels,
						  informativeColorPixels, spatiallyVariantColorPixels,
						  depthReadbackCompleted, colorReadbackCompleted, proof.resultCopyStage);
			LogNativeForwardCheckpoint("copy-result-return", packet);

			try {
				if (projectionCaptured) {
					static std::atomic<std::uint32_t> clipLogs{ 0 };
					const auto drawCount = std::min(resources.prepassDrawDiag.drawsSeen,
						PrepassDrawDiag::kMaxDraws);
					if (drawCount != 0 && clipLogs.fetch_add(1, std::memory_order_relaxed) < 8u) {
						const auto& viewProjection = proof.captureViewProjection;
						static std::atomic<std::uint32_t> matrixLogs{ 0 };
						if (matrixLogs.fetch_add(1, std::memory_order_relaxed) < 2u) {
							const auto& cameraEye = request.captureCamera->world.translate;
							spdlog::info(
								"[FlatDeferredPlayerCapture] capture camera eye=({:.2f},{:.2f},{:.2f}) "
								"vp=[{:.5f},{:.5f},{:.5f},{:.5f} | {:.5f},{:.5f},{:.5f},{:.5f} | "
								"{:.5f},{:.5f},{:.5f},{:.5f} | {:.5f},{:.5f},{:.5f},{:.5f}]",
								cameraEye.x, cameraEye.y, cameraEye.z, viewProjection[0],
								viewProjection[1], viewProjection[2], viewProjection[3],
								viewProjection[4], viewProjection[5], viewProjection[6],
								viewProjection[7], viewProjection[8], viewProjection[9],
								viewProjection[10], viewProjection[11], viewProjection[12],
								viewProjection[13], viewProjection[14], viewProjection[15]);
							const float* worldToCamera =
								std::addressof(request.captureCamera->worldToCam[0][0]);
							spdlog::info(
								"[FlatDeferredPlayerCapture] capture camera worldToCam="
								"[{:.5f},{:.5f},{:.5f},{:.5f} | {:.5f},{:.5f},{:.5f},{:.5f} | "
								"{:.5f},{:.5f},{:.5f},{:.5f} | {:.5f},{:.5f},{:.5f},{:.5f}]",
								worldToCamera[0], worldToCamera[1], worldToCamera[2],
								worldToCamera[3], worldToCamera[4], worldToCamera[5],
								worldToCamera[6], worldToCamera[7], worldToCamera[8],
								worldToCamera[9], worldToCamera[10], worldToCamera[11],
								worldToCamera[12], worldToCamera[13], worldToCamera[14],
								worldToCamera[15]);
						}

						const auto& eye = request.captureCamera->world.translate;
						for (std::uint32_t index = 0; index < drawCount; ++index) {
							const auto& draw = resources.prepassDrawDiag.draws[index];
							const float position[4]{ draw.worldPosition[0] - eye.x,
								draw.worldPosition[1] - eye.y, draw.worldPosition[2] - eye.z, 1.0f };
							float row[4]{};
							float column[4]{};
							for (std::uint32_t component = 0; component < 4; ++component) {
								for (std::uint32_t term = 0; term < 4; ++term) {
									row[component] += position[term] * viewProjection[term * 4 + component];
									column[component] += viewProjection[component * 4 + term] * position[term];
								}
							}
							const float rowW = row[3] != 0.0f ? row[3] : 1.0f;
							const float columnW = column[3] != 0.0f ? column[3] : 1.0f;
							spdlog::info(
								"[FlatDeferredPlayerCapture] prepass draw[{}] clip: "
								"row=({:.3f},{:.3f},{:.3f},w={:.3f}) col=({:.3f},{:.3f},{:.3f},w={:.3f})",
								index, row[0] / rowW, row[1] / rowW, row[2] / rowW, row[3],
								column[0] / columnW, column[1] / columnW, column[2] / columnW,
								column[3]);
						}
					}
				}
			} catch (...) {

			}

			CameraRestoreProof cameraRestore{};
			LogNativeForwardCheckpoint("restore-camera-enter", packet);
			RestoreCameraLeaf(request.restoreCamera.get(), engine.currentAccumulator,
				engine.activeSSNSlots[4], cameraRestore);
			LogNativeForwardCheckpoint("restore-camera-return", packet);
			LogNativeForwardCheckpoint("restore-engine-raw-enter", packet);
			const bool engineRawRestoreIssued = engine.RestoreRaw(request.exclusiveAccumulator);
			LogNativeForwardCheckpoint("restore-engine-raw-return", packet);
			LogNativeForwardCheckpoint("restore-resources-enter", packet);
			const bool resourceRestoreSucceeded = resources.Restore();
			LogNativeForwardCheckpoint("restore-resources-return", packet);

			LogNativeForwardCheckpoint("release-depth3-enter", packet);
			proof.depth3ReservationRestored = depth3Lease.Release();
			LogNativeForwardCheckpoint("release-depth3-return", packet);
			LogNativeForwardCheckpoint("restore-entry-pipeline-enter", packet);
			entryPipeline.Restore(request.immediateContext);
			LogNativeForwardCheckpoint("restore-entry-pipeline-return", packet);

			LogNativeForwardCheckpoint("verify-restored-state-enter", packet);
			const bool engineRawVerified = engineRawRestoreIssued &&
			                               engine.VerifyRaw(request.exclusiveAccumulator);
			const bool pipelineVerified = restoredPipeline.Capture(request.immediateContext) &&
			                              entryPipeline.Equivalent(restoredPipeline);
			const bool deviceHealthy = request.device->GetDeviceRemovedReason() == S_OK;
			LogNativeForwardCheckpoint("verify-restored-state-return", packet);
			PrivateRendererFacts postPrivateRenderer{};
			const bool postRendererAttested = packet.nativeCompleted &&
			                                  AttestPrivateRenderer(request, engine, postPrivateRenderer);

			const bool backgroundFXPrefixNoOpPostcondition = postRendererAttested &&
				postPrivateRenderer.maskedGeometryNameLengthReadable &&
				postPrivateRenderer.maskedMaterialNameLengthReadable &&
				postPrivateRenderer.maskedGeometryNameLength == 0 &&
				postPrivateRenderer.maskedMaterialNameLength == 0;
			const bool privateRendererPostcondition =
				postRendererAttested && backgroundFXPrefixNoOpPostcondition;
			proof.captureCameraDetachedAfterReturn = packet.cameraDetached &&
				AttestDetachedObject(reinterpret_cast<RE::NiAVObject*>(request.captureCamera.get()));
			proof.playerRootDetachedAfterReturn =
				packet.rootDetached && AttestDetachedPlayerRoot(request);
			proof.captureCameraBoundForLighting = packet.shadowSceneNodePublished &&
				packet.cameraPublished && packet.shaderCameraPublished && packet.cullerCameraSet;
			proof.captureCameraAttachedForLighting = packet.cameraAttached && packet.cameraDetached;
			proof.playerRootAttachedForLighting = packet.rootAttached && packet.rootDetached;
			proof.privateLightsUpdated = packet.lightsUpdated;
			proof.queuedLightsProcessed = packet.queuedLightsProcessed;
			proof.privateShadowSceneNodeUpdated = packet.shadowSceneNodeUpdated;
			proof.privateSunAttached = packet.sunAttached;
			proof.privateSunDetached = packet.sunDetached;
			proof.lightingPreparationAttested = proof.captureCameraBoundForLighting &&
				proof.captureCameraAttachedForLighting && proof.playerRootAttachedForLighting &&
				proof.privateLightsUpdated && proof.queuedLightsProcessed &&
				proof.privateShadowSceneNodeUpdated && proof.privateSunAttached &&
				proof.privateSunDetached && packet.cullerConstructed &&
				packet.cullerAccumulatorSet && packet.accumulatorForwardStateSet &&
				packet.cullerDestroyed;

			proof.sehExceptionCode = packet.exceptionCode ? packet.exceptionCode :
				(cleanup.exceptionCode ? cleanup.exceptionCode :
					(cameraRestore.exceptionCode ? cameraRestore.exceptionCode :
						depth3Lease.exceptionCode));
			proof.nativeExceptionInstruction = packet.exceptionInstruction;
			proof.nativeExceptionReturnAddress = packet.exceptionReturnAddress;
			proof.nativeExceptionObject = packet.exceptionObject;
			proof.nativeExceptionParent = packet.exceptionParent;
			proof.nativeExceptionVtable = packet.exceptionVtable;
			proof.soleModelArgumentAttested = packet.nativeCompleted && proof.strongRootPinHeld &&
			                                  proof.captureCameraDetachedOnEntry &&
			                                  proof.captureCameraDetachedAfterReturn &&
			                                  proof.playerRootDetachedOnEntry &&
			                                  proof.playerRootDetachedAfterReturn &&
			                                  proof.lightingPreparationAttested &&
			                                  proof.privateRendererIdleOnEntry &&
			                                  proof.privateSceneGraphAttested;
			proof.detachedSoleModelAccumulated = proof.soleModelArgumentAttested &&
			                                    proof.privateLightingContextAttested;
			proof.nativeCompleted = packet.nativeCompleted;
			proof.drawModelCompleted = packet.nativeCompleted;
			proof.forwardSuffixCompleted = packet.nativeCompleted;
			proof.privateRendererPostconditionAttested = privateRendererPostcondition;
			proof.privateRendererPostconditionFailureCode = packet.nativeCompleted ?
			                                                    postPrivateRenderer.failureCode :
			                                                    0xFFu;

			const bool completedGraphReusable = packet.nativeCompleted && nativeCleanupSucceeded &&
			                                    proof.depth3ReservationRestored &&
			                                    privateRendererPostcondition &&
			                                    proof.captureCameraDetachedAfterReturn &&
			                                    proof.playerRootDetachedAfterReturn;
			proof.privateRendererReusable = completedGraphReusable && deviceHealthy;
			proof.privateRendererQuarantineRequired =
				packet.privateNativeMutationBegan && !completedGraphReusable;
			privateRendererDispositionClassified = true;
			proof.colorCopied = resultCopied && result.color && result.colorSRV;
			proof.depthCopied = resultCopied && result.depth && result.depthSRV;
			proof.coveredPixelCount = coveredPixels;
			proof.mappedDepthPixelCount = mappedDepthPixels;
			proof.informativeColorPixelCount = informativeColorPixels;
			proof.spatiallyVariantColorPixelCount = spatiallyVariantColorPixels;
			proof.requiredInformativeColorPixelCount = (std::max)(
				static_cast<std::uint64_t>(proof.flattenedDrawableCount),
				(std::max)(1ull, mappedDepthPixels / 256ull));
			proof.requiredSpatiallyVariantColorPixelCount = (std::max)(
				(static_cast<std::uint64_t>(proof.flattenedDrawableCount) + 1ull) / 2ull,
				(std::max)(1ull, mappedDepthPixels / 1024ull));
			proof.forwardOcclusionSamples = resources.forwardOcclusionSamples;
			proof.forwardVSInvocations = resources.forwardStatistics.VSInvocations;
			proof.forwardPSInvocations = resources.forwardStatistics.PSInvocations;
			proof.coverageReducedOnGPU = depthReadbackCompleted;
			proof.coverageReadbackBytes = depthReadbackCompleted ? sizeof(std::uint32_t) : 0;
			proof.colorAuthorityReducedOnGPU = colorReadbackCompleted;
			proof.colorAuthorityReadbackBytes =
				colorReadbackCompleted ? 3u * sizeof(std::uint32_t) : 0u;
			proof.nondegenerateColorAuthority = colorReadbackCompleted && mappedDepthPixels != 0 &&
				informativeColorPixels >= proof.requiredInformativeColorPixelCount &&
				spatiallyVariantColorPixels >= proof.requiredSpatiallyVariantColorPixelCount;
			proof.forwardDrawQueryReadBack = resources.forwardQueriesReadBack;
			proof.finalForwardDepthCaptured = resources.finalForwardDepthCaptured;
			proof.fullResolutionDepthMapped = false;
			proof.visibilityStatus = depthReadbackCompleted && coveredPixels != 0 ?
			                             VisibilityStatus::kDetachedModelDepthObserved :
			                             VisibilityStatus::kNotReadBack;
			proof.cullerDestroyed = cleanup.engineCleanupReturned && privateRendererPostcondition;
			proof.renderPassesCleared = nativeCleanupSucceeded && privateRendererPostcondition;
			proof.sortedMode18RecordRepaired = cleanup.sortedRecordRepaired && privateRendererPostcondition;
			proof.logicalResourcesStable = resources.restoreMappingsStable;
			proof.mappingMismatchKind = resources.mappingMismatchKind;
			proof.mappingMismatchLogical = resources.mappingMismatchLogical;
			proof.mappingMismatchExpected = resources.mappingMismatchExpected;
			proof.mappingMismatchObserved = resources.mappingMismatchObserved;
			proof.engineTargetsRestored = resources.restoreCopiesIssued;
			proof.entryBoundConstantBufferContentsRestored = resources.restoreCopiesIssued;
			proof.dynamicBufferRoundTripsRestored = resources.dynamicBufferRoundTripsRestored;
			proof.contextConstantBuffersRestored = resources.restoreCopiesIssued &&
			                                       resources.restoreMappingsStable;
			proof.graphicsCameraCacheStable = engineRawVerified;
			proof.hbaoDerivedStateRestored = engine.hbaoCallbackCompleted &&
			                                 engine.directionalAmbientCallbackCompleted;
			proof.cameraConstantsRestored = cameraRestore.cameraCallCompleted &&
			                                resources.restoreCopiesIssued;
			proof.engineStateRestored = engineRawVerified && cameraRestore.cameraCallCompleted &&
			                            cameraRestore.accumulatorGlobalRestored && cameraRestore.shadowSceneNodeRestored;
			proof.engineRawStateVerified = engineRawVerified;
			proof.accumulatorGlobalRestored = cameraRestore.accumulatorGlobalRestored;
			proof.shadowSceneNodeGlobalRestored = cameraRestore.shadowSceneNodeRestored;
			proof.pipelineStateRestored = pipelineVerified;

			Status status = Status::kSuccess;
			if (packet.exceptionCode != 0 || depth3Lease.exceptionCode != 0)
				status = Status::kNativeException;
			else if (packet.selectorCensusCaptured && !packet.selectorCensusMatchesEntry)
				status = Status::kIncompleteForwardGeometryAuthority;
			else if (!packet.nativeCompleted)
				status = Status::kNativeDidNotComplete;
			else if (!proof.flattenedModelCensusStable ||
				!proof.forwardSpecializedBranchAttested ||
				!proof.forwardGeometryPassesAttested ||
				!proof.reflectedRasterizerCommitAttested)
				status = Status::kIncompleteForwardGeometryAuthority;
			else if (!projectionCaptured)
				status = Status::kProjectionAttestationFailed;
			else if (!nativeCleanupSucceeded)
				status = Status::kCleanupFailed;
			else if (!resourceRestoreSucceeded || !proof.depth3ReservationRestored ||
				     !engineRawVerified || !privateRendererPostcondition ||
				     !proof.playerRootDetachedAfterReturn ||
				     !cameraRestore.cameraCallCompleted || !cameraRestore.accumulatorGlobalRestored ||
				     !cameraRestore.shadowSceneNodeRestored || !pipelineVerified)
				status = Status::kRestoreFailed;
			else if (resultCopied && depthReadbackCompleted && coveredPixels == 0)
				status = Status::kNoDetachedModelCoverage;
			else if (depthReadbackCompleted && coveredPixels != 0 && colorReadbackCompleted &&
				!proof.nondegenerateColorAuthority)
				status = Status::kDegenerateNativeColor;
			else if (!resultCopied)
				status = Status::kResultCopyFailed;
			else if (!deviceHealthy)
				status = Status::kDeviceRemoved;

			if (status != Status::kSuccess)
				return fail(status);
			resources.CommitResult();
			proof.completionIdentity = g_completionSerial.fetch_add(1, std::memory_order_relaxed);
			if (proof.completionIdentity == 0)
				proof.completionIdentity = g_completionSerial.fetch_add(1, std::memory_order_relaxed);
			result.status = proof.status = Status::kSuccess;
			return result;
		}

		[[nodiscard]] bool ClearNativeWorldPipelineLeaf(
			ID3D11DeviceContext* context, std::uint32_t& exceptionCode) noexcept
		{
			if (!context)
				return false;
			__try {
				context->ClearState();
				return true;
			} __except (exceptionCode = exceptionCode ? exceptionCode : GetExceptionCode(),
				EXCEPTION_EXECUTE_HANDLER) {
				return false;
			}
		}

		[[nodiscard]] bool RestoreNativeWorldEngineLeaf(
			EngineStateSnapshot& engine, void* accumulator,
			std::uint32_t& exceptionCode) noexcept
		{
			__try {
				return engine.RestoreRaw(accumulator);
			} __except (exceptionCode = exceptionCode ? exceptionCode : GetExceptionCode(),
				EXCEPTION_EXECUTE_HANDLER) {
				return false;
			}
		}

		[[nodiscard]] bool RestoreNativeWorldResourcesLeaf(
			ResourceTransaction& resources, std::uint32_t& exceptionCode) noexcept
		{
			__try {
				return resources.Restore();
			} __except (exceptionCode = exceptionCode ? exceptionCode : GetExceptionCode(),
				EXCEPTION_EXECUTE_HANDLER) {
				return false;
			}
		}

		[[nodiscard]] bool RestoreNativeWorldPipelineLeaf(
			const PipelineState& pipeline, ID3D11DeviceContext* context,
			std::uint32_t& exceptionCode) noexcept
		{
			if (!context)
				return false;
			__try {
				pipeline.Restore(context);
				return true;
			} __except (exceptionCode = exceptionCode ? exceptionCode : GetExceptionCode(),
				EXCEPTION_EXECUTE_HANDLER) {
				return false;
			}
		}

		struct NativeWorldRollbackGuard
		{
			NativeWorldRollbackGuard(const NativeWorldRequest& aRequest,
				EngineStateSnapshot& aEngine, ResourceTransaction& aResources,
				const PipelineState& aEntryPipeline, Depth3ReservationLease& aDepth3Lease) noexcept :
				request(std::addressof(aRequest)),
				engine(std::addressof(aEngine)),
				resources(std::addressof(aResources)),
				entryPipeline(std::addressof(aEntryPipeline)),
				depth3Lease(std::addressof(aDepth3Lease))
			{}

			NativeWorldRollbackGuard(const NativeWorldRollbackGuard&) = delete;
			NativeWorldRollbackGuard& operator=(const NativeWorldRollbackGuard&) = delete;

			const NativeWorldRequest* request{};
			EngineStateSnapshot* engine{};
			ResourceTransaction* resources{};
			const PipelineState* entryPipeline{};
			Depth3ReservationLease* depth3Lease{};
			CameraRestoreProof cameraRestore{};
			std::uint32_t exceptionCode{};
			bool pipelineCleared{};
			bool engineRawRestoreIssued{};
			bool resourceRestoreSucceeded{};
			bool depthReservationRestored{};
			bool pipelineRestoreIssued{};
			bool armed{};

			void ClearPipeline() noexcept
			{
				if (!armed || pipelineCleared || !request)
					return;
				pipelineCleared = ClearNativeWorldPipelineLeaf(
					request->immediateContext, exceptionCode);
			}

			void Rollback() noexcept
			{
				if (!armed)
					return;

				armed = false;
				if (!request || !engine || !resources || !entryPipeline || !depth3Lease)
					return;
				if (!pipelineCleared)
					pipelineCleared = ClearNativeWorldPipelineLeaf(
						request->immediateContext, exceptionCode);
				RestoreCameraLeaf(request->restoreCamera.get(), engine->currentAccumulator,
					engine->activeSSNSlots[4], cameraRestore);
				if (cameraRestore.exceptionCode != 0 && exceptionCode == 0)
					exceptionCode = cameraRestore.exceptionCode;
				engineRawRestoreIssued = RestoreNativeWorldEngineLeaf(
					*engine, request->exclusiveAccumulator, exceptionCode);
				resourceRestoreSucceeded = RestoreNativeWorldResourcesLeaf(
					*resources, exceptionCode);
				depthReservationRestored = depth3Lease->Release();
				if (depth3Lease->exceptionCode != 0 && exceptionCode == 0)
					exceptionCode = depth3Lease->exceptionCode;
				pipelineRestoreIssued = RestoreNativeWorldPipelineLeaf(
					*entryPipeline, request->immediateContext, exceptionCode);
			}

			~NativeWorldRollbackGuard() noexcept
			{
				Rollback();
			}
		};

		[[nodiscard]] NativeWorldResourcePrewarmResult PrewarmNativeWorldResourcesImpl(
			const NativeWorldResourcePrewarmRequest& request)
		{
			NativeWorldResourcePrewarmResult result{};
			if (!request.device || !request.immediateContext ||
				!request.cameraConstantBuffer || request.renderThreadId == 0 ||
				request.frameSerial == 0 ||
				((request.requiredWidth == 0) != (request.requiredHeight == 0)))
				return result;
			if (GetCurrentThreadId() != request.renderThreadId) {
				result.status = Status::kWrongThread;
				return result;
			}
			if (g_captureLock.test_and_set(std::memory_order_acquire)) {
				result.status = Status::kBusy;
				return result;
			}
			CaptureLockGuard captureLock{};
			if (g_activeForwardTransaction || g_activeNativeWorldDepthTransaction ||
				g_activeNativeWorldRasterizerTransaction ||
				g_activeNativeWorldDrawModelPacket ||
				g_pendingNativeWorldAuthority.valid) {
				result.status = Status::kBusy;
				return result;
			}
			g_nativeWorldResourceGeneration.valid = false;
			NativeWorldScratchClaim scratchClaim{ g_nativeWorldScratchStorage };
			if (!scratchClaim.armed) {
				result.status = Status::kBusy;
				return result;
			}
			auto& entryPipeline = *g_nativeWorldScratchStorage.entryPipeline.Get();
			auto& resources = *g_nativeWorldScratchStorage.resources.Get();

			result.runtimeAttested =
				(REL::Module::get().version() == REL::Version{ 1, 10, 163, 0 } || ReflectionRuntime::IsPort240()) &&
				!REL::Module::IsVR();
			if (!result.runtimeAttested) {
				result.status = Status::kUnsupportedRuntime;
				return result;
			}

			result.installedHookIdentityAttested =
				NativeWorldInstalledHookIdentityMatches();
			result.executableHashAttested = result.runtimeAttested;
			result.bodyHashesAttested = result.installedHookIdentityAttested;
			if (!result.installedHookIdentityAttested) {
				result.status = Status::kContractMismatch;
				return result;
			}
			if (request.immediateContext->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE) {
				result.status = Status::kInvalidRequest;
				return result;
			}
			ComPtr<ID3D11Device> contextDevice;
			request.immediateContext->GetDevice(contextDevice.GetAddressOf());
			if (contextDevice.Get() != request.device ||
				!ResourceUsesDevice(request.cameraConstantBuffer, request.device) ||
				request.device->GetDeviceRemovedReason() != S_OK ||
				!entryPipeline.Capture(request.immediateContext)) {
				result.status = Status::kInvalidRequest;
				return result;
			}
			constexpr std::size_t kNativeCameraBufferSlot = 12;
			if (entryPipeline.vs.constantBuffers[kNativeCameraBufferSlot].Get() !=
				request.cameraConstantBuffer) {
				result.status = Status::kInvalidRequest;
				return result;
			}
			void* renderContext = ResolveRenderContextLeaf();
			std::uint8_t mrt4Enabled = 0;
			std::uint8_t secondLightTargetEnabled = 0;
			if (!renderContext || !SafeLoad(Address(kRVA_Global1BC6), mrt4Enabled) ||
				!SafeLoad(Address(kRVA_Global1BC7), secondLightTargetEnabled)) {
				result.status = Status::kMissingRenderContext;
				return result;
			}

			NativeWorldRequest internal{};
			internal.device = request.device;
			internal.immediateContext = request.immediateContext;
			internal.cameraConstantBuffer = request.cameraConstantBuffer;
			internal.renderThreadId = request.renderThreadId;
			internal.frameSerial = request.frameSerial;
			internal.requiredWidth = request.requiredWidth;
			internal.requiredHeight = request.requiredHeight;
			NativeWorldCompletionProof proof{};
			const bool prepared = resources.PrepareNativeWorld(internal, entryPipeline,
				renderContext, mrt4Enabled != 0, secondLightTargetEnabled != 0,
				false, true, proof);
			result.colorWidth = proof.colorWidth;
			result.colorHeight = proof.colorHeight;
			result.depthWidth = proof.depthWidth;
			result.depthHeight = proof.depthHeight;
			result.resourcesCreated = resources.poolResourcesCreated;
			result.dynamicBufferBackupCount = resources.dynamicBufferBackupCount;
			result.contextConstantGroupCount =
				static_cast<std::uint32_t>(resources.contextConstantGroups.size());
			result.nonBlockingRollbackPrepared =
				prepared && resources.dynamicBufferBackupCount != 0;
			result.contextConstantGroupRollbackPrepared = prepared &&
				result.contextConstantGroupCount == kContextConstantGroupCount;
			result.exactRequiredExtentSatisfied = proof.exactRequiredExtentSatisfied;
			result.declaredLogicalWriteSetPrepared =
				prepared && proof.declaredLogicalWriteSetBacked;
			result.rollbackContentsStaged = false;
			result.engineOrPipelineMutationPerformed = false;
			if (!prepared) {
				result.status = proof.sourceExtentReported &&
					!proof.exactRequiredExtentSatisfied ?
					Status::kIncompatibleTargetExtent : Status::kResourceBackupFailed;
				return result;
			}
			if (!SealNativeWorldResourceGeneration(request, entryPipeline, renderContext,
					mrt4Enabled != 0, secondLightTargetEnabled != 0, resources,
					result.resourceGenerationIdentity)) {
				result.status = Status::kResourceBackupFailed;
				return result;
			}
			result.status = Status::kSuccess;
			return result;
		}

		[[nodiscard]] NativeWorldResult CaptureNativeWorldImpl(
			const NativeWorldRequest& input)
		{

			const NativeWorldRequest request = input;
			NativeWorldResult result{};
			auto& proof = result.proof;
			proof.frameSerial = request.frameSerial;
			proof.ownerIdentitySerial = request.ownerIdentitySerial;
			proof.preparedAccumulatorSerial = request.preparedAccumulatorSerial;
			proof.resourceGenerationIdentity = request.resourceGenerationIdentity;
			proof.accumulator = reinterpret_cast<std::uintptr_t>(request.exclusiveAccumulator);
			proof.privateRenderer = reinterpret_cast<std::uintptr_t>(request.privateRenderer);
			proof.captureCamera = reinterpret_cast<std::uintptr_t>(request.captureCamera.get());
			proof.restoreCamera = reinterpret_cast<std::uintptr_t>(request.restoreCamera.get());
			proof.shadowSceneNode =
				reinterpret_cast<std::uintptr_t>(request.privateShadowSceneNode.get());
			proof.modelRoot = reinterpret_cast<std::uintptr_t>(request.modelRoot.get());
			proof.cameraConstantBuffer =
				reinterpret_cast<std::uintptr_t>(request.cameraConstantBuffer);
			proof.cameraHandedness = request.cameraHandedness;
			proof.publicationPerformed = false;
			bool mutationBegan = false;
			bool ownerDispositionClassified = false;

			auto fail = [&](Status status) -> NativeWorldResult {
				result.status = proof.status = status;
				if (g_captureEscapePhase == CaptureEscapePhase::kReservationAmbiguous) {
					proof.ownerDisposition = NativeWorldOwnerDisposition::kQuarantine;
				} else if (!mutationBegan) {
					proof.ownerDisposition = NativeWorldOwnerDisposition::kEntryUnchanged;
				} else if (!ownerDispositionClassified) {
					proof.ownerDisposition = NativeWorldOwnerDisposition::kQuarantine;
				}
				result.color.Reset();
				result.colorSRV.Reset();
				result.depth.Reset();
				result.depthSRV.Reset();
				return result;
			};

			if (!request.privateRenderer || !request.exclusiveAccumulator || !request.captureCamera ||
				!request.restoreCamera || !request.privateShadowSceneNode || !request.device ||
				!request.modelRoot || !request.immediateContext || !request.cameraConstantBuffer ||
				request.renderThreadId == 0 || !request.ownerAttestor || !request.lightingPreparer ||
				!request.passAccumulator || request.ownerIdentitySerial == 0 ||
				request.preparedAccumulatorSerial == 0 || request.frameSerial == 0 ||
				request.resourceGenerationIdentity == 0 ||
				!request.submittedRoots || request.submittedRootCount == 0 ||
				request.submittedRootCount > NativeWorldRequest::kMaxSubmittedRoots)
				return fail(Status::kInvalidRequest);
			if (GetCurrentThreadId() != request.renderThreadId)
				return fail(Status::kWrongThread);
			if (g_captureLock.test_and_set(std::memory_order_acquire))
				return fail(Status::kBusy);
			CaptureLockGuard captureLock{};
			if (g_activeForwardTransaction || g_activeNativeWorldDepthTransaction ||
				g_activeNativeWorldRasterizerTransaction ||
				g_activeNativeWorldDrawModelPacket ||
				g_pendingNativeWorldAuthority.valid)
				return fail(Status::kBusy);

			if (!NativeWorldResourceGenerationEntryMatches(request))
				return fail(Status::kResourceBackupFailed);
			NativeWorldScratchClaim scratchClaim{ g_nativeWorldScratchStorage };
			if (!scratchClaim.armed)
				return fail(Status::kBusy);
			auto& entryPipeline = *g_nativeWorldScratchStorage.entryPipeline.Get();
			auto& restoredPipeline = *g_nativeWorldScratchStorage.restoredPipeline.Get();
			auto& engine = *g_nativeWorldScratchStorage.engine.Get();
			auto& resources = *g_nativeWorldScratchStorage.resources.Get();
			auto& packet = *g_nativeWorldScratchStorage.packet.Get();
			packet.submittedRootCount = request.submittedRootCount;
			proof.submittedRootCount = request.submittedRootCount;
			packet.submittedRootIdentitySet.fill(0u);
			for (std::uint32_t index = 0; index < request.submittedRootCount; ++index) {
				RE::NiAVObject* root = nullptr;
				const auto rootSlot = reinterpret_cast<std::uintptr_t>(request.submittedRoots) +
					static_cast<std::uintptr_t>(index) * sizeof(RE::NiAVObject*);
				if (!SafeLoad(rootSlot, root) || !root ||
					root == request.privateShadowSceneNode.get() || root == request.modelRoot.get() ||
					!InsertNativeWorldRootIdentity(packet, root))
					return fail(Status::kInvalidRequest);
				packet.submittedRoots[index] = root;
			}
			if (request.privateMutableRoot &&
				!NativeWorldRootIdentityRecorded(packet, request.privateMutableRoot))
				return fail(Status::kInvalidRequest);
			packet.privateMutableRoot = request.privateMutableRoot;
			if (!NativeWorldRootsPairwiseSubtreeDisjoint(packet))
				return fail(Status::kInvalidRequest);

			proof.runtimeAttested =
				(REL::Module::get().version() == REL::Version{ 1, 10, 163, 0 } || ReflectionRuntime::IsPort240()) &&
				!REL::Module::IsVR();
			if (!proof.runtimeAttested)
				return fail(Status::kUnsupportedRuntime);

			proof.installedHookIdentityAttested =
				NativeWorldInstalledHookIdentityMatches();
			proof.executableHashAttested = proof.runtimeAttested;
			proof.bodyHashesAttested = proof.installedHookIdentityAttested;
			proof.abiCallsiteAttested = proof.installedHookIdentityAttested;
			proof.prepassDepthHookInstalled =
				g_prepassDepthHookInstalled.load(std::memory_order_acquire);
			proof.drawModelAccumulateHookInstalled =
				g_drawModelAccumulateHookInstalled.load(std::memory_order_acquire) &&
				g_drawModelAmbientPreservationHookInstalled.load(std::memory_order_acquire);
			if (!proof.installedHookIdentityAttested)
				return fail(Status::kContractMismatch);

			if (request.immediateContext->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE)
				return fail(Status::kInvalidRequest);
			ComPtr<ID3D11Device> contextDevice;
			request.immediateContext->GetDevice(contextDevice.GetAddressOf());
			if (contextDevice.Get() != request.device ||
				!ResourceUsesDevice(request.cameraConstantBuffer, request.device))
				return fail(Status::kInvalidRequest);
			D3D11_BUFFER_DESC cameraBufferDescription{};
			request.cameraConstantBuffer->GetDesc(std::addressof(cameraBufferDescription));
			if ((cameraBufferDescription.BindFlags & D3D11_BIND_CONSTANT_BUFFER) == 0 ||
				cameraBufferDescription.ByteWidth == 0 ||
				(cameraBufferDescription.ByteWidth & 15u) != 0 ||
				(cameraBufferDescription.Usage != D3D11_USAGE_DEFAULT &&
					cameraBufferDescription.Usage != D3D11_USAGE_DYNAMIC))
				return fail(Status::kInvalidRequest);
			if (request.device->GetDeviceRemovedReason() != S_OK)
				return fail(Status::kDeviceRemoved);

			if (!entryPipeline.Capture(request.immediateContext))
				return fail(Status::kUnsupportedPipelineState);
			if (!engine.Capture(request.exclusiveAccumulator))
				return fail(Status::kMissingRenderContext);
			proof.accumulatorWasExclusive =
				engine.currentAccumulator != request.exclusiveAccumulator;
			proof.activeShadowSceneNodeAliasesRejected =
				std::none_of(engine.activeSSNSlots.begin(), engine.activeSSNSlots.end(),
					[&](void* active) {
						return active == request.privateShadowSceneNode.get();
					});
			if (!proof.accumulatorWasExclusive)
				return fail(Status::kAliasedMainAccumulator);
			if (!proof.activeShadowSceneNodeAliasesRejected)
				return fail(Status::kAliasedMainLightingContext);

			proof.preparedAccumulatorTupleAttested = true;
			proof.captureCameraCachePreexisting = true;
			proof.restoreCameraCachePreexisting = true;

			if (!resources.PrepareNativeWorld(request, entryPipeline, engine.renderContext,
					engine.global1BC6 != 0, engine.global1BC7 != 0, true, false, proof)) {
				if (proof.sourceExtentReported && !proof.exactRequiredExtentSatisfied)
					return fail(Status::kIncompatibleTargetExtent);
				return fail(Status::kResourceBackupFailed);
			}
			proof.hotPathResourceCreations = resources.poolResourcesCreated;
			proof.resourcePoolWarmOnEntry = resources.poolResourcesCreated == 0;
			proof.resourceGenerationMatched = proof.resourcePoolWarmOnEntry &&
				NativeWorldResourceGenerationMatches(request, entryPipeline,
					engine.renderContext, engine.global1BC6 != 0,
					engine.global1BC7 != 0, resources);
			if (!proof.resourcePoolWarmOnEntry)
				return fail(Status::kResourcePoolWarming);
			if (!proof.resourceGenerationMatched)
				return fail(Status::kResourceBackupFailed);
			proof.doubleBufferedResultLifetime = resources.pool &&
				resources.pool->resultSlots[0].configured &&
				resources.pool->resultSlots[1].configured &&
				resources.pool->resultSlots[0].color.Get() !=
					resources.pool->resultSlots[1].color.Get() &&
				resources.pool->resultSlots[0].depth.Get() !=
					resources.pool->resultSlots[1].depth.Get();

			NativeWorldOwnerAttestationView ownerEntryView{};
			ownerEntryView.phase = NativeWorldOwnerAttestationPhase::kSealedPreparedEntry;
			ownerEntryView.exclusiveAccumulator = request.exclusiveAccumulator;
			ownerEntryView.captureCamera = request.captureCamera.get();
			ownerEntryView.privateShadowSceneNode = request.privateShadowSceneNode.get();
			ownerEntryView.submittedRoots = request.submittedRoots;
			ownerEntryView.submittedRootCount = request.submittedRootCount;
			ownerEntryView.ownerIdentitySerial = request.ownerIdentitySerial;
			ownerEntryView.preparedAccumulatorSerial = request.preparedAccumulatorSerial;
			ownerEntryView.frameSerial = request.frameSerial;
			OwnerAttestationCall ownerEntry{};
			CallNativeWorldOwnerAttestor(request, ownerEntryView, ownerEntry);
			proof.ownerEntryAttestorInvoked = ownerEntry.invoked;
			proof.ownerEntryAttestorReturned = ownerEntry.returned;
			proof.ownerEntryAttested = ownerEntry.returned && ownerEntry.attested;
			proof.ownerEntryFailureCode = ownerEntry.failureCode;
			if (ownerEntry.exceptionCode != 0)
				proof.sehExceptionCode = ownerEntry.exceptionCode;
			if (!proof.ownerEntryAttested)
				return fail(ownerEntry.exceptionCode != 0 ?
					Status::kNativeException : Status::kOwnerEntryAttestationFailed);

			auto* depth3TargetManager =
				reinterpret_cast<RE::BSGraphics::RenderTargetManager*>(resources.targetManager);
			Depth3ReservationSnapshot depth3Reservation{};
			proof.depth3ReservationAttested =
				depth3Reservation.Capture(depth3TargetManager);
			if (!proof.depth3ReservationAttested)
				return fail(Status::kInvalidLogicalTarget);
			proof.depth3ReservationHandle =
				reinterpret_cast<std::uintptr_t>(depth3Reservation.pointer);
			proof.depth3ReservationPhysical = depth3Reservation.handle.physical;
			proof.depth3ReservationRefCount = depth3Reservation.handle.refCount;
			proof.depth3ReservationLogicalOwner = depth3Reservation.handle.logicalOwner;
			Depth3ReservationLease depth3Lease{ depth3TargetManager, depth3Reservation };
			if (!depth3Lease.Acquire()) {
				proof.depth3ReservationRestored = depth3Lease.Release();
				if (depth3Lease.exceptionCode != 0)
					proof.sehExceptionCode = depth3Lease.exceptionCode;
				return fail(depth3Lease.exceptionCode != 0 ?
					Status::kNativeException : Status::kInvalidLogicalTarget);
			}
			proof.depth3ReservationPhysical =
				depth3Reservation.observedMapping < kPhysicalDepthCount ?
					static_cast<std::int32_t>(depth3Reservation.observedMapping) : -1;
			if (!depth3Reservation.persistent) {
				proof.depth3ReservationRefCount = depth3Reservation.observedHandle.refCount;
				proof.depth3ReservationLogicalOwner =
					depth3Reservation.observedHandle.logicalOwner;
			}
			if (depth3Reservation.cold) {
				proof.depth3ReservationRestored = depth3Lease.Release();
				return fail(proof.depth3ReservationRestored ?
					Status::kResourcePoolWarming : Status::kRestoreFailed);
			}

			mutationBegan = true;
			proof.nativeMutationBegan = true;
			proof.ownerDisposition = NativeWorldOwnerDisposition::kQuarantine;
			g_captureEscapePhase = CaptureEscapePhase::kPostMutation;
			NativeWorldRollbackGuard rollback{
				request, engine, resources, entryPipeline, depth3Lease
			};
			rollback.armed = true;
			resources.ClearForCapture();
			packet.targetManager = resources.targetManager;
			packet.renderContext = engine.renderContext;
			packet.distantRenderer = engine.distantRenderer;
			packet.effect45 = engine.effect45;
			packet.effect46 = engine.effect46;
			packet.effect47 = engine.effect47;
			packet.privateRenderer = request.privateRenderer;
			packet.exclusiveAccumulator = request.exclusiveAccumulator;
			packet.camera = request.captureCamera.get();
			packet.privateShadowSceneNode = request.privateShadowSceneNode.get();
			packet.modelRoot = request.modelRoot.get();
			packet.resourceTransaction = std::addressof(resources);
			packet.lightingPreparer = request.lightingPreparer;
			packet.lightingPreparerContext = request.lightingPreparerContext;
			packet.passAccumulator = request.passAccumulator;
			packet.passAccumulatorContext = request.passAccumulatorContext;
			packet.ownerIdentitySerial = request.ownerIdentitySerial;
			packet.preparedAccumulatorSerial = request.preparedAccumulatorSerial;
			packet.frameSerial = request.frameSerial;
			packet.mrt4Enabled = engine.global1BC6 != 0;
			RunNativeWorldLeaf(packet);

			proof.sehExceptionCode = packet.exceptionCode;
			proof.nativeExceptionInstruction = packet.exceptionInstruction;
			proof.nativeExceptionReturnAddress = packet.exceptionReturnAddress;
			proof.cullerConstructed = packet.cullerConstructed;
			proof.cullerAccumulatorSet = packet.cullerAccumulatorSet;
			proof.cullerCameraSet = packet.cullerCameraSet;
			proof.drawModelAccumulateHookArmed = packet.drawModelAccumulateHookArmed;
			proof.drawModelAccumulateHookEntered = packet.drawModelAccumulateHookEntered;
			proof.drawModelExpandedRootCount = packet.drawModelExpandedRootCount;
			proof.drawModelRootExpansionAttested =
				packet.drawModelRootExpansionAttested &&
				packet.drawModelAccumulateHookArgumentsAttested &&
				packet.drawModelAccumulateHookTLSRestored &&
				g_activeNativeWorldDrawModelPacket == nullptr;
			proof.lightingPreparerInvoked = packet.lightingPreparerInvoked;
			proof.lightingPreparerReturned = packet.lightingPreparerReturned;
			proof.callerPreparedLightingAttested =
				packet.lightingPreparerReturned && packet.lightingPreparationAttested;
			proof.lightingPreparationFailureCode =
				packet.lightingPreparationFailureCode;
			proof.passAccumulatorInvoked = packet.passAccumulatorInvoked;
			proof.passAccumulatorReturned = packet.passAccumulatorReturned;
			proof.passAccumulatorAttested =
				packet.passAccumulatorReturned && packet.passAccumulationAttested;
			proof.passAccumulationFailureCode =
				packet.passAccumulationFailureCode;
			proof.privateSunDetached = packet.privateSunDetached;
			proof.prepassDepthCaptureArmed = packet.prepassDepthCaptureArmed;
			proof.prepassDepthCaptured = resources.prepassDepthCaptured;
			proof.prepassDepthCaptureTLSRestored =
				packet.prepassDepthCaptureTLSRestored &&
				g_activeNativeWorldDepthTransaction == nullptr;
			proof.nativeWorldRasterizerTLSArmed =
				packet.nativeWorldRasterizerTLSArmed;
			proof.nativeWorldRasterizerTLSRestored =
				packet.nativeWorldRasterizerTLSRestored &&
				g_activeNativeWorldRasterizerTransaction == nullptr;
			proof.nativeWorldSetDirtyIssuedCount =
				resources.nativeWorldSetDirtyIssuedCount;
			proof.nativeWorldSetDirtyCommitCount =
				resources.nativeWorldSetDirtyCommitCount;
			proof.nativeWorldGeometryRequiredCommitCount =
				resources.nativeWorldGeometryRequiredCommitCount;
			proof.nativeWorldOrdinaryRequiredCommitCount =
				resources.nativeWorldOrdinaryRequiredCommitCount;
			proof.nativeWorldForeignCommitCount =
				resources.nativeWorldForeignCommitCount;
			proof.nativeWorldPlanarTargetIsolationCount =
				resources.nativeWorldPlanarTargetIsolationCount;
			proof.nativeWorldPlanarTargetIsolationFailures =
				resources.nativeWorldPlanarTargetIsolationFailures;
			proof.nativeWorldRasterizerApplyAttempts =
				resources.nativeWorldRasterizerApplyAttempts;
			proof.nativeWorldRasterizerApplySuccesses =
				resources.nativeWorldRasterizerApplySuccesses;
			proof.nativeWorldRasterizerApplyFailures =
				resources.nativeWorldRasterizerApplyFailures;
			proof.nativeWorldOrdinaryRasterizerApplyAttempts =
				resources.nativeWorldOrdinaryRasterizerApplyAttempts;
			proof.nativeWorldOrdinaryRasterizerApplySuccesses =
				resources.nativeWorldOrdinaryRasterizerApplySuccesses;
			proof.nativeWorldOrdinaryRasterizerApplyFailures =
				resources.nativeWorldOrdinaryRasterizerApplyFailures;
			proof.nativeWorldCommitIdentitiesAttested =
				proof.nativeWorldSetDirtyIssuedCount != 0 &&
				proof.nativeWorldSetDirtyIssuedCount ==
					proof.nativeWorldSetDirtyCommitCount &&
				proof.nativeWorldForeignCommitCount == 0 &&
				proof.nativeWorldGeometryRequiredCommitCount <=
					proof.nativeWorldSetDirtyCommitCount &&
				proof.nativeWorldOrdinaryRequiredCommitCount ==
					proof.nativeWorldSetDirtyCommitCount -
					proof.nativeWorldGeometryRequiredCommitCount;
			proof.nativeWorldPlanarTargetsIsolated =
				proof.nativeWorldCommitIdentitiesAttested &&
				proof.nativeWorldPlanarTargetIsolationCount ==
					proof.nativeWorldSetDirtyCommitCount &&
				proof.nativeWorldPlanarTargetIsolationFailures == 0;
			proof.nativeWorldReflectedRasterizerAttested =
				proof.nativeWorldGeometryRequiredCommitCount != 0 &&
				proof.nativeWorldRasterizerApplyAttempts ==
					proof.nativeWorldGeometryRequiredCommitCount &&
				proof.nativeWorldRasterizerApplySuccesses ==
					proof.nativeWorldRasterizerApplyAttempts &&
				proof.nativeWorldRasterizerApplyFailures == 0;
			proof.nativeWorldOrdinaryRasterizerAttested =
				proof.nativeWorldOrdinaryRasterizerApplyAttempts ==
					proof.nativeWorldOrdinaryRequiredCommitCount &&
				proof.nativeWorldOrdinaryRasterizerApplySuccesses ==
					proof.nativeWorldOrdinaryRasterizerApplyAttempts &&
				proof.nativeWorldOrdinaryRasterizerApplyFailures == 0;
			proof.drawModelCullerAuthorityRevoked =
				packet.drawModelCullerAuthorityRevoked &&
				packet.drawModelCullingProcess == nullptr &&
				g_activeNativeWorldDrawModelPacket == nullptr;
			proof.accumulatorGroup5Cleared = packet.accumulatorGroup5Cleared;
			proof.submittedRootsCleared = packet.submittedRootsCleared;
			proof.submittedRootPassesCleared = packet.submittedRootCount != 0 &&
				packet.submittedRootsCleared == packet.submittedRootCount;
			proof.accumulatorActivePassesCleared =
				packet.accumulatorActivePassesCleared;
			proof.sortedMode18CurrentRecordValidated =
				packet.sortedMode18CurrentRecordValidated;
			proof.sortedMode18CurrentRecordRepaired =
				packet.sortedMode18CurrentRecordRepaired;
			proof.defaultViewportSelected = packet.viewportSelected;
			proof.rendererFlushed = packet.rendererFlushed;
			proof.nativeCallReturned = packet.nativeCallReturned;
			proof.nativeCompleted = packet.nativeCompleted;
			proof.captureCameraDetached = packet.captureCameraDetached;
			proof.modelRootDetached = packet.modelRootDetached;
			proof.canonicalPrefixBound = packet.depthTargetBound &&
				packet.viewportSelected && packet.rendererFlushed;

			rollback.ClearPipeline();
			std::uint64_t coveredPixels = 0;
			std::uint64_t mappedDepthPixels = 0;
			std::uint64_t informativeColorPixels = 0;
			std::uint64_t spatiallyVariantColorPixels = 0;
			bool depthReadbackCompleted = false;
			bool colorReadbackCompleted = false;
			const bool nativePublicationCandidate = packet.nativeCompleted &&
				proof.installedHookIdentityAttested &&
				proof.nativeWorldRasterizerTLSRestored &&
				proof.nativeWorldCommitIdentitiesAttested &&
				proof.nativeWorldPlanarTargetsIsolated &&
				proof.nativeWorldReflectedRasterizerAttested &&
				proof.nativeWorldOrdinaryRasterizerAttested;
			const bool resultCopyIssued = nativePublicationCandidate &&
				resources.CopyNativeWorldResult(result, coveredPixels, mappedDepthPixels,
					informativeColorPixels, spatiallyVariantColorPixels,
					depthReadbackCompleted, colorReadbackCompleted, proof.resultCopyStage);

			rollback.Rollback();
			const auto& cameraRestore = rollback.cameraRestore;
			const bool engineRawRestoreIssued = rollback.engineRawRestoreIssued;
			const bool resourceRestoreSucceeded = rollback.resourceRestoreSucceeded;
			proof.depth3ReservationRestored = rollback.depthReservationRestored;
			if (rollback.exceptionCode != 0 && proof.sehExceptionCode == 0)
				proof.sehExceptionCode = rollback.exceptionCode;
			const bool engineRawVerified = engineRawRestoreIssued &&
				engine.VerifyRaw(request.exclusiveAccumulator);
			const bool pipelineVerified = rollback.pipelineRestoreIssued &&
				restoredPipeline.Capture(request.immediateContext) &&
				entryPipeline.Equivalent(restoredPipeline);
			const bool deviceHealthy = request.device->GetDeviceRemovedReason() == S_OK;

			proof.installedHookIdentityAttested =
				proof.installedHookIdentityAttested &&
				NativeWorldInstalledHookIdentityMatches();
			proof.bodyHashesAttested = proof.installedHookIdentityAttested;
			proof.abiCallsiteAttested = proof.installedHookIdentityAttested;

			proof.colorCopied = resultCopyIssued && result.color && result.colorSRV;
			proof.depthCopied = resultCopyIssued && result.depth && result.depthSRV;
			proof.resultResourcesDisjoint = proof.colorCopied && proof.depthCopied &&
				result.color.Get() != resources.sourceColor.Get() &&
				result.depth.Get() != resources.sourceDepth.Get() &&
				result.color.Get() != result.depth.Get();
			proof.resultColorResource =
				reinterpret_cast<std::uintptr_t>(result.color.Get());
			proof.resultDepthResource =
				reinterpret_cast<std::uintptr_t>(result.depth.Get());
			proof.resultSlot = resources.candidateResultSlot;
			proof.gpuAuthorityDispatchCount = resultCopyIssued ? 1u : 0u;
			proof.blockingReadbackCount = 0u;
			proof.logicalMappingsStable = resources.restoreMappingsStable;
			proof.mappingMismatchKind = resources.mappingMismatchKind;
			proof.mappingMismatchLogical = resources.mappingMismatchLogical;
			proof.mappingMismatchExpected = resources.mappingMismatchExpected;
			proof.mappingMismatchObserved = resources.mappingMismatchObserved;
			proof.dynamicBufferBackupCount = resources.dynamicBufferBackupCount;
			proof.dynamicAsyncRestoreCopyCount = resources.dynamicAsyncRestoreCopyCount;
			proof.nonBlockingRollbackAttested =
				resources.dynamicBufferBackupCount != 0 &&
				resources.dynamicAsyncRestoreCopyCount ==
					resources.dynamicBufferBackupCount &&
				resources.dynamicBufferRoundTripsRestored;
			proof.contextConstantGroupsRestored =
				resources.contextConstantGroupsRestored &&
				proof.contextConstantGroupCount == kContextConstantGroupCount;
			proof.declaredResourcesRestored = resources.restoreCopiesIssued &&
				resources.restoreMappingsStable && proof.nonBlockingRollbackAttested &&
				proof.contextConstantGroupsRestored;
			proof.engineRawStateVerified = engineRawVerified;
			proof.accumulatorGlobalRestored = cameraRestore.accumulatorGlobalRestored;
			proof.shadowSceneNodeGlobalRestored = cameraRestore.shadowSceneNodeRestored;
			proof.graphicsCameraCacheStable = engineRawVerified;
			proof.hbaoDerivedStateRestored = engine.hbaoCallbackCompleted &&
				engine.directionalAmbientCallbackCompleted;
			proof.cameraConstantsRestored = cameraRestore.cameraCallCompleted &&
				resources.restoreCopiesIssued;
			proof.pipelineStateRestored = pipelineVerified;

			const bool completeGenericCleanup = packet.exceptionCode == 0 &&
				proof.installedHookIdentityAttested &&
				packet.nativeCallReturned && packet.captureCameraDetached &&
				packet.modelRootDetached && packet.privateSunDetached &&
				proof.drawModelCullerAuthorityRevoked &&
				packet.accumulatorActivePassesCleared &&
				proof.submittedRootPassesCleared &&
				proof.sortedMode18CurrentRecordRepaired &&
				proof.prepassDepthCaptureTLSRestored &&
				proof.nativeWorldRasterizerTLSRestored &&
				proof.nativeWorldCommitIdentitiesAttested &&
				proof.nativeWorldPlanarTargetsIsolated &&
				proof.nativeWorldReflectedRasterizerAttested &&
				proof.nativeWorldOrdinaryRasterizerAttested &&
				proof.depth3ReservationRestored &&
				resourceRestoreSucceeded && engineRawVerified &&
				cameraRestore.cameraCallCompleted &&
				cameraRestore.accumulatorGlobalRestored &&
				cameraRestore.shadowSceneNodeRestored &&
				proof.hbaoDerivedStateRestored && proof.declaredResourcesRestored &&
				proof.contextConstantGroupsRestored && pipelineVerified;

			NativeWorldOwnerAttestationView postOwnerView{};
			postOwnerView.phase = NativeWorldOwnerAttestationPhase::kRestoredPostCleanup;
			postOwnerView.exclusiveAccumulator = request.exclusiveAccumulator;
			postOwnerView.captureCamera = request.captureCamera.get();
			postOwnerView.privateShadowSceneNode = request.privateShadowSceneNode.get();
			postOwnerView.submittedRoots = request.submittedRoots;
			postOwnerView.submittedRootCount = request.submittedRootCount;
			postOwnerView.ownerIdentitySerial = request.ownerIdentitySerial;
			postOwnerView.preparedAccumulatorSerial = request.preparedAccumulatorSerial;
			postOwnerView.frameSerial = request.frameSerial;
			postOwnerView.nativeMutationBegan = proof.nativeMutationBegan;
			postOwnerView.nativeCallReturned = proof.nativeCallReturned;
			postOwnerView.lightingPreparationReturned =
				proof.lightingPreparerReturned && proof.callerPreparedLightingAttested;
			postOwnerView.privateSunDetached = proof.privateSunDetached;
			postOwnerView.accumulatorActivePassesCleared =
				proof.accumulatorActivePassesCleared;
			postOwnerView.sortedMode18CurrentRecordRepaired =
				proof.sortedMode18CurrentRecordRepaired;
			postOwnerView.nativeWorldRasterizerPolicyAttested =
				proof.nativeWorldRasterizerTLSRestored &&
				proof.nativeWorldCommitIdentitiesAttested &&
				proof.nativeWorldPlanarTargetsIsolated &&
				proof.nativeWorldReflectedRasterizerAttested &&
				proof.nativeWorldOrdinaryRasterizerAttested;
			postOwnerView.contextConstantGroupsRestored =
				proof.contextConstantGroupsRestored;
			postOwnerView.declaredResourcesRestored = proof.declaredResourcesRestored;
			postOwnerView.engineStateRestored = engineRawVerified &&
				cameraRestore.cameraCallCompleted &&
				cameraRestore.accumulatorGlobalRestored &&
				cameraRestore.shadowSceneNodeRestored && proof.hbaoDerivedStateRestored;
			postOwnerView.pipelineStateRestored = pipelineVerified;
			OwnerAttestationCall postOwner{};
			CallNativeWorldOwnerAttestor(request, postOwnerView, postOwner);
			proof.ownerPostconditionAttestorInvoked = postOwner.invoked;
			proof.ownerPostconditionAttestorReturned = postOwner.returned;
			proof.ownerPostconditionAttested = completeGenericCleanup &&
				postOwner.returned && postOwner.attested;
			proof.ownerPostconditionFailureCode = postOwner.failureCode;
			if (postOwner.exceptionCode != 0 && proof.sehExceptionCode == 0)
				proof.sehExceptionCode = postOwner.exceptionCode;
			proof.ownerDisposition = proof.ownerPostconditionAttested ?
				NativeWorldOwnerDisposition::kReusable :
				NativeWorldOwnerDisposition::kQuarantine;
			ownerDispositionClassified = true;
			g_captureEscapePhase = CaptureEscapePhase::kPreMutation;

			Status terminalStatus = Status::kAuthorityPending;
			if (packet.exceptionCode != 0 || depth3Lease.exceptionCode != 0 ||
				postOwner.exceptionCode != 0)
				terminalStatus = Status::kNativeException;
			else if (!nativePublicationCandidate)
				terminalStatus = Status::kNativeDidNotComplete;
			else if (!resultCopyIssued)
				terminalStatus = Status::kResultCopyFailed;
			else if (!resourceRestoreSucceeded || !proof.depth3ReservationRestored ||
				!engineRawVerified || !cameraRestore.cameraCallCompleted ||
				!cameraRestore.accumulatorGlobalRestored ||
				!cameraRestore.shadowSceneNodeRestored || !pipelineVerified)
				terminalStatus = Status::kRestoreFailed;
			else if (!proof.ownerPostconditionAttested ||
				proof.ownerDisposition != NativeWorldOwnerDisposition::kReusable)
				terminalStatus = Status::kOwnerPostconditionFailed;
			else if (!deviceHealthy)
				terminalStatus = Status::kDeviceRemoved;
			if (terminalStatus != Status::kAuthorityPending)
				return fail(terminalStatus);

			const auto pendingIdentity =
				g_pendingNativeWorldSerial.fetch_add(1, std::memory_order_relaxed);
			proof.pendingIdentity = pendingIdentity ? pendingIdentity :
				g_pendingNativeWorldSerial.fetch_add(1, std::memory_order_relaxed);
			proof.status = result.status = Status::kAuthorityPending;
			proof.pendingRetained = true;
			proof.pendingTerminal = false;
			g_pendingNativeWorldAuthority = {};
			g_pendingNativeWorldAuthority.result = result;
			g_pendingNativeWorldAuthority.device = request.device;
			g_pendingNativeWorldAuthority.context = request.immediateContext;
			g_pendingNativeWorldAuthority.pool = resources.pool;
			g_pendingNativeWorldAuthority.identity = proof.pendingIdentity;
			g_pendingNativeWorldAuthority.colorPixelCount =
				static_cast<std::uint64_t>(resources.sourceColorDesc.Width) *
				static_cast<std::uint64_t>(resources.sourceColorDesc.Height);
			g_pendingNativeWorldAuthority.depthPixelCount =
				static_cast<std::uint64_t>(resources.sourceDepthDesc.Width) *
				static_cast<std::uint64_t>(resources.sourceDepthDesc.Height);
			g_pendingNativeWorldAuthority.resultSlot = resources.candidateResultSlot;
			g_pendingNativeWorldAuthority.renderThreadId = request.renderThreadId;
			g_pendingNativeWorldAuthority.valid = true;

			result.color.Reset();
			result.colorSRV.Reset();
			result.depth.Reset();
			result.depthSRV.Reset();
			return result;
		}

		[[nodiscard]] NativeWorldResult PollNativeWorldCaptureImpl(
			std::uint64_t pendingIdentity, std::uint32_t renderThreadId)
		{
			NativeWorldResult rejected{};
			if (pendingIdentity == 0 || renderThreadId == 0)
				return rejected;
			if (GetCurrentThreadId() != renderThreadId) {
				rejected.status = rejected.proof.status = Status::kWrongThread;
				return rejected;
			}
			if (g_captureLock.test_and_set(std::memory_order_acquire)) {
				rejected.status = rejected.proof.status = Status::kBusy;
				return rejected;
			}
			CaptureLockGuard captureLock{};
			auto& pending = g_pendingNativeWorldAuthority;
			if (!pending.valid || pending.identity != pendingIdentity ||
				pending.renderThreadId != renderThreadId || !pending.device || !pending.context ||
				!pending.pool || pending.resultSlot >= pending.pool->resultSlots.size())
				return rejected;

			if (pending.result.proof.authorityPollCount !=
				std::numeric_limits<std::uint32_t>::max())
				++pending.result.proof.authorityPollCount;
			std::array<std::uint32_t, 4> counters{};
			const auto pollState = TryPollNativeWorldAuthority(pending, counters);
			if (pollState == NativeWorldAuthorityPollState::kPending) {
				auto result = pending.result;
				result.status = result.proof.status = Status::kAuthorityPending;
				result.proof.pendingRetained = true;
				result.proof.pendingTerminal = false;
				result.color.Reset();
				result.colorSRV.Reset();
				result.depth.Reset();
				result.depthSRV.Reset();
				return result;
			}

			auto result = pending.result;
			auto& proof = result.proof;
			Status terminalStatus = Status::kResultCopyFailed;
			const bool deviceHealthy = pending.device->GetDeviceRemovedReason() == S_OK;
			if (pollState == NativeWorldAuthorityPollState::kReady) {
				proof.authorityReadbackBytes = 4u * sizeof(std::uint32_t);
				proof.coveredPixelCount = counters[0];
				proof.mappedDepthPixelCount = counters[1];
				proof.informativeColorPixelCount = counters[2];
				proof.spatiallyVariantColorPixelCount = counters[3];
				proof.requiredInformativeColorPixelCount =
					(std::max)(1ull, proof.mappedDepthPixelCount / 256ull);
				proof.requiredSpatiallyVariantColorPixelCount =
					(std::max)(1ull, proof.mappedDepthPixelCount / 1024ull);
				proof.depthCoverageReducedOnGPU = true;
				proof.colorAuthorityReducedOnGPU = true;
				proof.nonClearColorWriteObserved =
					proof.informativeColorPixelCount >=
						proof.requiredInformativeColorPixelCount;
				proof.nondegenerateDepthQualifiedColor =
					proof.coveredPixelCount != 0 && proof.mappedDepthPixelCount != 0 &&
					proof.nonClearColorWriteObserved &&
					proof.spatiallyVariantColorPixelCount >=
						proof.requiredSpatiallyVariantColorPixelCount;

				const auto& slot = pending.pool->resultSlots[pending.resultSlot];
				const bool retainedCandidateStable = slot.configured && result.color &&
					result.colorSRV && result.depth && result.depthSRV &&
					slot.color.Get() == result.color.Get() &&
					slot.colorView.Get() == result.colorSRV.Get() &&
					slot.depth.Get() == result.depth.Get() &&
					slot.depthView.Get() == result.depthSRV.Get();
				const bool retainedNativeAuthority =
					proof.installedHookIdentityAttested &&
					proof.nativeWorldCommitIdentitiesAttested &&
					proof.nativeWorldPlanarTargetsIsolated &&
					proof.nativeWorldReflectedRasterizerAttested &&
					proof.nativeWorldOrdinaryRasterizerAttested &&
					proof.ownerPostconditionAttested &&
					proof.ownerDisposition == NativeWorldOwnerDisposition::kReusable;
				if (!deviceHealthy)
					terminalStatus = Status::kDeviceRemoved;
				else if (!proof.ownerPostconditionAttested ||
					proof.ownerDisposition != NativeWorldOwnerDisposition::kReusable)
					terminalStatus = Status::kOwnerPostconditionFailed;
				else if (!retainedNativeAuthority)
					terminalStatus = Status::kNativeDidNotComplete;
				else if (!retainedCandidateStable)
					terminalStatus = Status::kResultCopyFailed;
				else if (!proof.nondegenerateDepthQualifiedColor)

					terminalStatus = Status::kResultCopyFailed;
				else
					terminalStatus = Status::kSuccess;
			} else if (!deviceHealthy) {
				terminalStatus = Status::kDeviceRemoved;
			}

			proof.pendingRetained = false;
			proof.pendingTerminal = true;
			if (terminalStatus == Status::kSuccess) {
				pending.pool->committedResultSlot = pending.resultSlot;
				proof.completionIdentity =
					g_completionSerial.fetch_add(1, std::memory_order_relaxed);
				if (proof.completionIdentity == 0)
					proof.completionIdentity =
						g_completionSerial.fetch_add(1, std::memory_order_relaxed);
				result.status = proof.status = Status::kSuccess;
			} else {
				result.status = proof.status = terminalStatus;
				result.color.Reset();
				result.colorSRV.Reset();
				result.depth.Reset();
				result.depthSRV.Reset();
			}
			pending.pool->avoidNextResultSlot =
				std::numeric_limits<std::uint32_t>::max();

			g_pendingNativeWorldAuthority = {};
			return result;
		}

		[[nodiscard]] NativeWorldResult RecoverRetainedPendingResult(
			std::uint64_t pendingIdentity, std::uint32_t renderThreadId,
			const NativeWorldRequest* originatingRequest = nullptr) noexcept
		{
			NativeWorldResult unknown{};
			if (renderThreadId == 0 ||
				g_captureLock.test_and_set(std::memory_order_acquire))
				return unknown;
			CaptureLockGuard captureLock{};
			const auto& pending = g_pendingNativeWorldAuthority;
			const bool identityMatches = pending.valid && pending.renderThreadId == renderThreadId &&
				(pendingIdentity == 0 || pending.identity == pendingIdentity);
			const bool originMatches = !originatingRequest ||
				(pending.result.proof.frameSerial == originatingRequest->frameSerial &&
					pending.result.proof.ownerIdentitySerial ==
						originatingRequest->ownerIdentitySerial &&
					pending.result.proof.preparedAccumulatorSerial ==
						originatingRequest->preparedAccumulatorSerial &&
					pending.result.proof.accumulator == reinterpret_cast<std::uintptr_t>(
						originatingRequest->exclusiveAccumulator));
			if (!identityMatches || !originMatches)
				return unknown;
			auto result = pending.result;
			result.status = result.proof.status = Status::kAuthorityPending;
			result.proof.pendingRetained = true;
			result.proof.pendingTerminal = false;
			result.color.Reset();
			result.colorSRV.Reset();
			result.depth.Reset();
			result.depthSRV.Reset();
			return result;
		}

		[[nodiscard]] bool DiscardPendingNativeWorldCaptureImpl(
			std::uint64_t pendingIdentity, ID3D11Device* expectedDevice,
			ID3D11DeviceContext* expectedContext, std::uint32_t retiredRenderThreadId,
			NativeWorldPendingDiscardReason reason) noexcept
		{
			if (pendingIdentity == 0 || !expectedDevice || !expectedContext ||
				retiredRenderThreadId == 0 ||
				g_captureLock.test_and_set(std::memory_order_acquire))
				return false;
			CaptureLockGuard captureLock{};
			auto& pending = g_pendingNativeWorldAuthority;
			if (!pending.valid || pending.identity != pendingIdentity ||
				pending.renderThreadId != retiredRenderThreadId ||
				pending.device.Get() != expectedDevice || pending.context.Get() != expectedContext ||
				!pending.pool || pending.resultSlot >= pending.pool->resultSlots.size())
				return false;

			const bool authorized = reason == NativeWorldPendingDiscardReason::kDeviceRemoved ?
				expectedDevice->GetDeviceRemovedReason() != S_OK :
				((reason == NativeWorldPendingDiscardReason::kRenderThreadGenerationRetired &&
					GetCurrentThreadId() != retiredRenderThreadId) ||
					(reason == NativeWorldPendingDiscardReason::kLoadGenerationRetired &&
						GetCurrentThreadId() == retiredRenderThreadId));
			if (!authorized)
				return false;

			pending.pool->avoidNextResultSlot = pending.resultSlot;
			g_nativeWorldResourceGeneration.valid = false;
			pending = {};
			return true;
		}

		struct CameraCacheFacts
		{
			void* data{};
			std::uint32_t capacity{};
			std::uint32_t size{};
			void* shaderCamera{};
			void* stateReferenceCamera{};
			std::array<std::byte, kMaxCameraCacheEntries * kCameraCacheEntrySize> entries{};

			[[nodiscard]] bool Read() noexcept
			{
				if (!SafeLoad(Address(kRVA_GraphicsState) + kCameraCacheOffset, data) ||
					!SafeLoad(Address(kRVA_GraphicsState) + kCameraCacheOffset + 0x08, capacity) ||
					!SafeLoad(Address(kRVA_GraphicsState) + kCameraCacheOffset + 0x10, size) ||
					!SafeLoad(Address(kRVA_ShaderCamera), shaderCamera) ||
					!SafeLoad(Address(kRVA_GraphicsState) + kCameraStateOffset + 0x238,
						stateReferenceCamera) || size > capacity || size > kMaxCameraCacheEntries ||
					(size != 0 && (!data || !SafeCopy(entries.data(), data,
						static_cast<std::size_t>(size) * kCameraCacheEntrySize))))
					return false;
				return true;
			}

			[[nodiscard]] bool Contains(const void* camera, bool selector) const noexcept
			{
				if (!camera)
					return false;
				for (std::uint32_t index = 0; index < size; ++index) {
					void* cachedCamera = nullptr;
					const auto offset = static_cast<std::size_t>(index) * kCameraCacheEntrySize;
					std::memcpy(std::addressof(cachedCamera), entries.data() + offset + 0x238,
						sizeof(cachedCamera));
					const auto cachedSelector = static_cast<std::uint8_t>(entries[offset + 0x240]);
					if (cachedCamera == camera &&
						cachedSelector == static_cast<std::uint8_t>(selector))
						return true;
				}
				return false;
			}
		};

		[[nodiscard]] std::uint64_t CameraCacheWarmupIdentity(
			const CameraCacheWarmupResult& result) noexcept
		{
			std::uint64_t hash = 1469598103934665603ull;
			const auto add = [&](const void* data, std::size_t size) {
				const auto* bytes = static_cast<const std::uint8_t*>(data);
				for (std::size_t index = 0; index < size; ++index) {
					hash ^= bytes[index];
					hash *= 1099511628211ull;
				}
			};
			add(std::addressof(result.captureCamera), sizeof(result.captureCamera));
			add(std::addressof(result.restoreCamera), sizeof(result.restoreCamera));
			add(std::addressof(result.cacheData), sizeof(result.cacheData));
			add(std::addressof(result.cacheCapacity), sizeof(result.cacheCapacity));
			add(std::addressof(result.cacheSize), sizeof(result.cacheSize));
			add(std::addressof(result.renderThreadId), sizeof(result.renderThreadId));
			return hash ? hash : 1;
		}

		void PrewarmCameraCacheLeaf(RE::NiCamera* captureCamera, RE::NiCamera* restoreCamera,
			bool& completed, std::uint32_t& exceptionCode) noexcept
		{
			__try {
				using CacheCameraData = void (*)(void*, RE::NiCamera*, bool);
				using SetCameraData = void (*)(void*, RE::NiCamera*, bool, float, float);
				auto* graphicsState = reinterpret_cast<void*>(Address(kRVA_GraphicsState));
				const float cameraFlush = *reinterpret_cast<const float*>(Address(kRVA_CameraFlushScalar));
				if (!std::isfinite(cameraFlush) || cameraFlush <= 0.0f)
					__leave;
				reinterpret_cast<CacheCameraData>(Address(kRVA_CacheCameraData))(
					graphicsState, captureCamera, true);

				reinterpret_cast<CacheCameraData>(Address(kRVA_CacheCameraData))(
					graphicsState, captureCamera, false);
				reinterpret_cast<CacheCameraData>(Address(kRVA_CacheCameraData))(
					graphicsState, restoreCamera, true);
				reinterpret_cast<SetCameraData>(Address(kRVA_SetCameraData))(
					graphicsState, restoreCamera, true, 0.0f, cameraFlush);
				completed = true;
			} __except (exceptionCode = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER) {
			}
		}

		[[nodiscard]] CameraCacheWarmupResult PrewarmCameraCacheImpl(
			RE::NiCamera* captureCamera, RE::NiCamera* restoreCamera,
			std::uint32_t renderThreadId)
		{
			CameraCacheWarmupResult result{};
			result.captureCamera = reinterpret_cast<std::uintptr_t>(captureCamera);
			result.restoreCamera = reinterpret_cast<std::uintptr_t>(restoreCamera);
			result.renderThreadId = renderThreadId;
			if (!captureCamera || !restoreCamera || captureCamera == restoreCamera ||
				renderThreadId == 0)
				return result;
			if (GetCurrentThreadId() != renderThreadId) {
				result.status = Status::kWrongThread;
				return result;
			}
			if (g_captureLock.test_and_set(std::memory_order_acquire)) {
				result.status = Status::kBusy;
				return result;
			}
			CaptureLockGuard captureLock{};

			result.runtimeAttested =
				(REL::Module::get().version() == REL::Version{ 1, 10, 163, 0 } || ReflectionRuntime::IsPort240()) &&
				!REL::Module::IsVR();
			if (!result.runtimeAttested) {
				result.status = Status::kUnsupportedRuntime;
				return result;
			}

			result.installedHookIdentityAttested =
				NativeWorldInstalledHookIdentityMatches();
			result.executableHashAttested = result.runtimeAttested;
			result.bodyHashesAttested = result.installedHookIdentityAttested;
			if (!result.installedHookIdentityAttested) {
				result.status = Status::kContractMismatch;
				return result;
			}

			CameraCacheFacts before{};
			if (!before.Read())
				return result;
			result.entryShaderCamera = reinterpret_cast<std::uintptr_t>(before.shaderCamera);
			result.entryStateReferenceCamera =
				reinterpret_cast<std::uintptr_t>(before.stateReferenceCamera);

			result.restoreWasCurrentOnEntry = before.shaderCamera == restoreCamera;
			if (!result.restoreWasCurrentOnEntry)
				return result;

			std::uint32_t exceptionCode = 0;
			PrewarmCameraCacheLeaf(
				captureCamera, restoreCamera, result.restoreRepublished, exceptionCode);
			if (exceptionCode != 0 || !result.restoreRepublished) {
				result.status = Status::kNativeException;
				return result;
			}

			CameraCacheFacts after{};
			CameraCacheFacts verified{};
			if (!after.Read() || !verified.Read())
				return result;
			result.afterShaderCamera = reinterpret_cast<std::uintptr_t>(after.shaderCamera);
			result.afterStateReferenceCamera =
				reinterpret_cast<std::uintptr_t>(after.stateReferenceCamera);
			result.verifiedShaderCamera = reinterpret_cast<std::uintptr_t>(verified.shaderCamera);
			result.verifiedStateReferenceCamera =
				reinterpret_cast<std::uintptr_t>(verified.stateReferenceCamera);
			result.cacheData = reinterpret_cast<std::uintptr_t>(after.data);
			result.cacheCapacity = after.capacity;
			result.cacheSize = after.size;
			result.verifiedCacheCapacity = verified.capacity;
			result.verifiedCacheSize = verified.size;
			result.captureEntryReady = after.Contains(captureCamera, true) &&
			                           after.Contains(captureCamera, false);
			result.restoreEntryReady = after.Contains(restoreCamera, true);
			result.metadataStableAfterRepublish = after.data == verified.data &&
				after.capacity == verified.capacity && after.size == verified.size &&
				after.shaderCamera == restoreCamera && after.stateReferenceCamera != nullptr &&
				verified.shaderCamera == restoreCamera &&
				verified.stateReferenceCamera == after.stateReferenceCamera &&
				verified.Contains(captureCamera, true) && verified.Contains(captureCamera, false) &&
				verified.Contains(restoreCamera, true);
			if (!result.captureEntryReady || !result.restoreEntryReady ||
				!result.metadataStableAfterRepublish)
				return result;
			result.identity = CameraCacheWarmupIdentity(result);
			result.status = Status::kSuccess;
			return result;
		}

#undef FD_CAPTURE_STAGE
#undef FD_CAPTURE_CB1
#undef FD_RESTORE_STAGE
#undef FD_RESTORE_CB
	}

	const FrozenContract& Contract() noexcept
	{
		return kContract;
	}

	RE::NiCamera* CurrentShaderCamera(std::uint32_t renderThreadId) noexcept
	{
		if (renderThreadId == 0u || renderThreadId != GetCurrentThreadId() ||
			REL::Module::IsVR() ||
			(REL::Module::get().version() != REL::Version{ 1, 10, 163, 0 } && !ReflectionRuntime::IsPort240()) ||
			!NativeWorldInstalledHookIdentityMatches())
			return nullptr;
		RE::NiCamera* camera = nullptr;
		return SafeLoad(Address(kRVA_ShaderCamera), camera) ? camera : nullptr;
	}

	CameraCacheWarmupResult PrewarmCameraCache(
		RE::NiCamera* captureCamera, RE::NiCamera* restoreCamera,
		std::uint32_t renderThreadId) noexcept
	{
		try {
			return PrewarmCameraCacheImpl(captureCamera, restoreCamera, renderThreadId);
		} catch (...) {
			CameraCacheWarmupResult result{};
			result.status = Status::kResourceBackupFailed;
			return result;
		}
	}

	NativeWorldResourcePrewarmResult PrewarmNativeWorldResources(
		const NativeWorldResourcePrewarmRequest& request) noexcept
	{
		try {
			return PrewarmNativeWorldResourcesImpl(request);
		} catch (...) {
			NativeWorldResourcePrewarmResult result{};
			result.status = Status::kResourceBackupFailed;
			return result;
		}
	}

	NativeWorldResult CaptureNativeWorld(const NativeWorldRequest& request) noexcept
	{
		const auto previousEscapePhase = g_captureEscapePhase;
		g_captureEscapePhase = CaptureEscapePhase::kPreMutation;
		try {
			auto result = CaptureNativeWorldImpl(request);
			g_captureEscapePhase = previousEscapePhase;
			return result;
		} catch (...) {
			const bool postMutationEscape =
				g_captureEscapePhase == CaptureEscapePhase::kPostMutation ||
				g_captureEscapePhase == CaptureEscapePhase::kReservationAmbiguous;
			g_captureEscapePhase = previousEscapePhase;

			auto retained = RecoverRetainedPendingResult(
				0, request.renderThreadId, std::addressof(request));
			if (retained.IsPending())
				return retained;
			NativeWorldResult result{};
			result.status = result.proof.status = Status::kResourceBackupFailed;
			result.proof.ownerDisposition = postMutationEscape ?
				NativeWorldOwnerDisposition::kQuarantine :
				NativeWorldOwnerDisposition::kEntryUnchanged;
			return result;
		}
	}

	NativeWorldResult PollNativeWorldCapture(
		std::uint64_t pendingIdentity, std::uint32_t renderThreadId) noexcept
	{
		try {
			return PollNativeWorldCaptureImpl(pendingIdentity, renderThreadId);
		} catch (...) {
			auto retained = RecoverRetainedPendingResult(
				pendingIdentity, renderThreadId);
			if (retained.IsPending())
				return retained;
			NativeWorldResult result{};
			result.status = result.proof.status = Status::kResultCopyFailed;
			return result;
		}
	}

	bool DiscardPendingNativeWorldCapture(
		std::uint64_t pendingIdentity, ID3D11Device* expectedDevice,
		ID3D11DeviceContext* expectedContext, std::uint32_t retiredRenderThreadId,
		NativeWorldPendingDiscardReason reason) noexcept
	{
		return DiscardPendingNativeWorldCaptureImpl(
			pendingIdentity, expectedDevice, expectedContext, retiredRenderThreadId, reason);
	}

	Result Capture(const Request& request) noexcept
	{
		const auto previousEscapePhase = g_captureEscapePhase;
		g_captureEscapePhase = CaptureEscapePhase::kPreMutation;
		try {
			auto result = CaptureImpl(request);
			g_captureEscapePhase = previousEscapePhase;
			return result;
		} catch (...) {
			const bool postMutationEscape =
				g_captureEscapePhase == CaptureEscapePhase::kPostMutation ||
				g_captureEscapePhase == CaptureEscapePhase::kReservationAmbiguous;
			g_captureEscapePhase = previousEscapePhase;
			Result result{};
			result.status = Status::kResourceBackupFailed;
			result.proof.status = result.status;
			result.proof.privateRendererReusable = false;
			result.proof.privateRendererQuarantineRequired = postMutationEscape;
			return result;
		}
	}

	void InstallPrepassDepthCaptureHook() noexcept
	{
		if (g_prepassDepthHookInstalled.load(std::memory_order_acquire) &&
			g_drawModelAccumulateHookInstalled.load(std::memory_order_acquire) &&
			g_drawModelAmbientPreservationHookInstalled.load(std::memory_order_acquire))
			return;
		try {
			if ((REL::Module::get().version() != REL::Version{ 1, 10, 163, 0 } && !ReflectionRuntime::IsPort240()) ||
				REL::Module::IsVR())
				return;

			if (!g_prepassDepthHookInstalled.load(std::memory_order_acquire)) {
				if (!MatchBytes(kRVA_DeferredPrepassDepthClearCall,
						kDeferredPrepassDepthClearCallBytes))
					return;
				stl::write_thunk_call<DeferredPrepassDepthClearHook>(
					Address(kRVA_DeferredPrepassDepthClearCall));
				std::array<std::uint8_t, 5> patched{};
				if (!SafeCopy(patched.data(),
						reinterpret_cast<const void*>(Address(kRVA_DeferredPrepassDepthClearCall)),
						patched.size()) || patched[0] != 0xE8 ||
						patched == kDeferredPrepassDepthClearCallBytes ||
						DeferredPrepassDepthClearHook::func.address() == 0)
					return;
				g_prepassDepthHookPatchedBytes = patched;
				g_prepassDepthHookInstalled.store(true, std::memory_order_release);
			}

			if (!g_drawModelAccumulateHookInstalled.load(std::memory_order_acquire)) {
				if (!MatchBytes(kRVA_Interface3DDrawModelAccumulateSceneCall,
						kDrawModelAccumulateSceneCallBytes))
					return;
				stl::write_thunk_call<DrawModelAccumulateSceneHook>(
					Address(kRVA_Interface3DDrawModelAccumulateSceneCall));
				std::array<std::uint8_t, 5> patched{};
				if (!SafeCopy(patched.data(), reinterpret_cast<const void*>(
						Address(kRVA_Interface3DDrawModelAccumulateSceneCall)), patched.size()) ||
						patched[0] != 0xE8 || patched == kDrawModelAccumulateSceneCallBytes ||
						DrawModelAccumulateSceneHook::func.address() == 0)
					return;
				g_drawModelAccumulateHookPatchedBytes = patched;
				g_drawModelAccumulateHookInstalled.store(true, std::memory_order_release);
			}

			if (!g_drawModelAmbientPreservationHookInstalled.load(std::memory_order_acquire)) {
				if (!MatchBytes(kRVA_Interface3DDrawModelAmbientBlackCall,
						kDrawModelAmbientBlackCallBytes))
					return;
				stl::write_thunk_call<DrawModelDirectionalAmbientBlackHook>(
					Address(kRVA_Interface3DDrawModelAmbientBlackCall));
				std::array<std::uint8_t, 5> patched{};
				if (!SafeCopy(patched.data(), reinterpret_cast<const void*>(
						Address(kRVA_Interface3DDrawModelAmbientBlackCall)), patched.size()) ||
						patched[0] != 0xE8 || patched == kDrawModelAmbientBlackCallBytes ||
						DrawModelDirectionalAmbientBlackHook::func.address() == 0)
					return;
				g_drawModelAmbientPreservationHookPatchedBytes = patched;
				g_drawModelAmbientPreservationHookInstalled.store(
					true, std::memory_order_release);
			}
		} catch (...) {
		}
	}

	void NotePrepassGeometrySetup(RE::BSRenderPass* pass) noexcept
	{
		if (auto* transaction = g_activeForwardTransaction) {
			LogForwardGetRenderPassesStage("lighting-setup-enter",
				pass ? pass->shaderProperty : nullptr, pass ? pass->geometry : nullptr,
				pass ? pass->technique : 0u, nullptr, pass);
			transaction->NoteForwardGeometrySetup(pass);

			LogForwardGetRenderPassesStage("lighting-setup-observed",
				pass ? pass->shaderProperty : nullptr, pass ? pass->geometry : nullptr,
				pass ? pass->technique : 0u, nullptr, pass);
		}
	}

	void NotePrepassGeometryRestore(RE::BSRenderPass* pass) noexcept
	{
		if (auto* transaction = g_activeForwardTransaction) {
			LogForwardGetRenderPassesStage("lighting-restore-enter",
				pass ? pass->shaderProperty : nullptr, pass ? pass->geometry : nullptr,
				pass ? pass->technique : 0u, nullptr, pass);
			transaction->NoteForwardGeometryRestore(pass);
			LogForwardGetRenderPassesStage("lighting-restore-observed",
				pass ? pass->shaderProperty : nullptr, pass ? pass->geometry : nullptr,
				pass ? pass->technique : 0u, nullptr, pass);
		}
	}

	bool TryBeginOrdinaryForwardPass(RE::BSRenderPass* pass) noexcept
	{
		if (auto* transaction = g_activeForwardTransaction) {
			__try {
				return transaction->TryBeginOrdinaryForwardPass(pass);
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				transaction->forwardGeometryCounterOverflow = true;
			}
		}
		return false;
	}

	bool PrimeOrdinaryForwardPassCommit(RE::BSRenderPass* pass) noexcept
	{
		if (auto* transaction = g_activeForwardTransaction) {
			__try {
				return transaction->PrimeOrdinaryForwardPassCommit(pass);
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				transaction->forwardGeometryCounterOverflow = true;
			}
		}
		return false;
	}

	bool TrySubstituteEssentialForwardVertexShader(
		RE::BSShader* shader, std::uint32_t requestedVertexDescriptor,
		std::uint32_t hullDescriptor, std::uint32_t domainDescriptor,
		std::uint32_t pixelDescriptor, const void* outputStruct,
		std::uint32_t& nativeVertexDescriptor) noexcept
	{
		if (auto* transaction = g_activeForwardTransaction) {
			__try {
				return transaction->TrySubstituteEssentialForwardVertexShader(
					shader, requestedVertexDescriptor, hullDescriptor, domainDescriptor,
					pixelDescriptor, outputStruct, nativeVertexDescriptor);
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				transaction->forwardGeometryCounterOverflow = true;
			}
		}
		return false;
	}

	void NoteEssentialForwardVertexShaderResult(
		std::uint32_t requestedVertexDescriptor,
		std::uint32_t nativeVertexDescriptor,
		std::uint32_t pixelDescriptor, bool accepted) noexcept
	{
		if (auto* transaction = g_activeForwardTransaction) {
			__try {
				transaction->NoteEssentialForwardVertexShaderResult(
					requestedVertexDescriptor, nativeVertexDescriptor,
					pixelDescriptor, accepted);
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				transaction->forwardGeometryCounterOverflow = true;
			}
		}
	}

	void EndOrdinaryForwardPass(
		RE::BSRenderPass* pass, bool nativeCompleted, bool accepted) noexcept
	{
		if (auto* transaction = g_activeForwardTransaction)
			transaction->NoteForwardGeometryRestore(pass, nativeCompleted, accepted);
	}

	void NoteForwardGetRenderPassesStage(const char* stage, const void* shaderProperty,
		const void* geometry, std::uint32_t renderMode, const void* accumulator,
		const void* result) noexcept
	{
		if (!g_activeForwardTransaction)
			return;
		LogForwardGetRenderPassesStage(
			stage, shaderProperty, geometry, renderMode, accumulator, result);
	}

	void NoteForwardGetRenderPassesResult(const void* shaderProperty, const void* geometry,
		std::uint32_t renderMode, const void* accumulator, const void* result) noexcept
	{
		if (auto* transaction = g_activeForwardTransaction) {
			__try {
				transaction->ObserveForwardSelectorResult(
					shaderProperty, geometry, renderMode, accumulator, result);
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				transaction->forwardGeometryCounterOverflow = true;
			}
		}
	}

	bool PrepassTransactionActive() noexcept
	{

		auto* transaction = g_activeForwardTransaction;
		if (!transaction || !transaction->forwardSelectorObservationActive)
			return false;
		const bool specialRouteActive = transaction->forwardActiveSpecialAlphaArmed &&
			transaction->forwardActiveSpecialAlphaIndex < transaction->forwardExpectedGeometryCount;
		const bool ordinaryRouteActive = transaction->forwardActivePassArmed &&
			transaction->forwardActiveExpectedIndex < transaction->forwardExpectedGeometryCount;
		return specialRouteActive || ordinaryRouteActive;
	}

	bool ReflectedRasterizerRequired() noexcept
	{
		return g_activeForwardTransaction &&
		       g_activeForwardTransaction->reflectedHandednessRequired;
	}

	bool RebindForwardOutputsAfterSetDirty() noexcept
	{
		if (auto* transaction = g_activeForwardTransaction)
			return transaction->RebindForwardOutputs();
		return false;
	}

	void NoteTransactionCommit() noexcept
	{
		if (auto* transaction = g_activeForwardTransaction)
			++transaction->prepassDrawDiag.commitsSeen;
	}

	void NoteReflectedRasterizerCommit(bool applied) noexcept
	{
		if (auto* transaction = g_activeForwardTransaction)
			transaction->NoteReflectedRasterizerCommit(applied);
	}

	void NoteOrdinaryRasterizerCommit() noexcept
	{
		if (auto* transaction = g_activeForwardTransaction)
			transaction->NoteOrdinaryRasterizerCommit();
	}

	bool NativeWorldTransactionActive() noexcept
	{
		return g_activeNativeWorldRasterizerTransaction &&
			g_activeNativeWorldRasterizerTransaction->nativeWorldRasterizerPolicyArmed;
	}

	NativeWorldPassWindingScope BeginNativeWorldGeometryPass(RE::BSRenderPass* pass) noexcept
	{
		if (!NativeWorldTransactionActive() || !pass)
			return NativeWorldPassWindingScope::kInactive;

		const bool hasGeometry = pass->geometry != nullptr;
		bool geometry = false;
		__try {
			auto* shader = pass->shader;
			if (shader) {
				const char* fxp = shader->fxpFilename;
				const auto type = static_cast<RE::BSShader::Type>(shader->shaderType);
				const bool screenSpace = (fxp &&
					(std::strcmp(fxp, "DFLight") == 0 ||
					 std::strcmp(fxp, "DFComposite") == 0)) ||
					type == RE::BSShader::Type::ImageSpace ||
					type == RE::BSShader::Type::Utility ||
					type == RE::BSShader::Type::BloodSplatter;
				if (hasGeometry && !screenSpace) {
					switch (type) {
					case RE::BSShader::Type::Lighting:
					case RE::BSShader::Type::DistantTree:
					case RE::BSShader::Type::Sky:
					case RE::BSShader::Type::Grass:
					case RE::BSShader::Type::Particle:
					case RE::BSShader::Type::Water:
						geometry = true;
						break;
					default:
						break;
					}
				}
			}
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			geometry = false;
		}
		if (geometry) {
			if (g_nativeWorldGeometryPassDepth ==
				(std::numeric_limits<std::uint32_t>::max)())
				return NativeWorldPassWindingScope::kInactive;
			++g_nativeWorldGeometryPassDepth;
			return NativeWorldPassWindingScope::kGeometry;
		}
		if (g_nativeWorldOrdinaryPassDepth ==
			(std::numeric_limits<std::uint32_t>::max)())
			return NativeWorldPassWindingScope::kInactive;
		++g_nativeWorldOrdinaryPassDepth;
		return NativeWorldPassWindingScope::kOrdinary;
	}

	void EndNativeWorldGeometryPass(NativeWorldPassWindingScope scope) noexcept
	{
		if (scope == NativeWorldPassWindingScope::kGeometry &&
			g_nativeWorldGeometryPassDepth != 0)
			--g_nativeWorldGeometryPassDepth;
		else if (scope == NativeWorldPassWindingScope::kOrdinary &&
			g_nativeWorldOrdinaryPassDepth != 0)
			--g_nativeWorldOrdinaryPassDepth;
	}

	bool NativeWorldGeometryPassActive() noexcept
	{
		return NativeWorldTransactionActive() && g_nativeWorldOrdinaryPassDepth == 0 &&
			g_nativeWorldGeometryPassDepth != 0;
	}

	void NoteNativeWorldTechnique(RE::BSShader* shader) noexcept
	{
		if (!NativeWorldTransactionActive())
			return;

		g_nativeWorldCurrentTechniqueShader = shader;
		g_nativeWorldCurrentTechniqueIsGeometry = false;
		if (!shader)
			return;

		__try {
			const char* fxp = shader->fxpFilename;
			if (!fxp)
				return;

			const auto type = static_cast<RE::BSShader::Type>(shader->shaderType);
			if (std::strcmp(fxp, "DFLight") == 0 ||
				std::strcmp(fxp, "DFComposite") == 0 ||
				type == RE::BSShader::Type::ImageSpace ||
				type == RE::BSShader::Type::Utility ||
				type == RE::BSShader::Type::BloodSplatter)
				return;

			switch (type) {
			case RE::BSShader::Type::Lighting:
			case RE::BSShader::Type::DistantTree:
			case RE::BSShader::Type::Sky:
			case RE::BSShader::Type::Grass:
			case RE::BSShader::Type::Particle:
			case RE::BSShader::Type::Water:
				g_nativeWorldCurrentTechniqueIsGeometry = true;
				break;
			default:
				break;
			}
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			g_nativeWorldCurrentTechniqueShader = nullptr;
			g_nativeWorldCurrentTechniqueIsGeometry = false;
		}
	}

	bool NativeWorldCurrentTechniqueNeedsReflectedWinding() noexcept
	{
		return NativeWorldTransactionActive() && g_nativeWorldOrdinaryPassDepth == 0 &&
			g_nativeWorldCurrentTechniqueShader != nullptr &&
			g_nativeWorldCurrentTechniqueIsGeometry;
	}

	NativeWorldSetDirtyCommitToken BeginNativeWorldSetDirtyCommit() noexcept
	{
		NativeWorldSetDirtyCommitToken token{};
		auto* transaction = g_activeNativeWorldRasterizerTransaction;
		if (!transaction)
			return token;
		if (transaction->nativeWorldSetDirtyIssuedCount ==
			(std::numeric_limits<std::uint32_t>::max)()) {
			if (transaction->nativeWorldForeignCommitCount !=
				(std::numeric_limits<std::uint32_t>::max)())
				++transaction->nativeWorldForeignCommitCount;
			return token;
		}
		token.transactionIdentity = reinterpret_cast<std::uintptr_t>(transaction);
		token.commitIdentity = ++transaction->nativeWorldSetDirtyIssuedCount;
		return token;
	}

	bool AttestNativeWorldTargetsAfterSetDirty(
		const NativeWorldSetDirtyCommitToken& token) noexcept
	{
		auto* transaction = g_activeNativeWorldRasterizerTransaction;
		if (!transaction || !token || token.transactionIdentity !=
				reinterpret_cast<std::uintptr_t>(transaction) ||
			transaction->nativeWorldSetDirtyCommitCount ==
				(std::numeric_limits<std::uint32_t>::max)() ||
			token.commitIdentity != transaction->nativeWorldSetDirtyCommitCount + 1u ||
			token.commitIdentity > transaction->nativeWorldSetDirtyIssuedCount)
			return false;
		return transaction->AttestNativeWorldTargetsAfterSetDirty();
	}

	void CompleteNativeWorldSetDirtyCommit(const NativeWorldSetDirtyCommitToken& token,
		bool exactNativeTargetsObserved, bool reflectedWindingRequired,
		bool rasterizerPolicyApplied) noexcept
	{
		auto* transaction = g_activeNativeWorldRasterizerTransaction;
		if (!transaction)
			return;
		const auto noteForeign = [&]() noexcept {
			if (transaction->nativeWorldForeignCommitCount !=
				(std::numeric_limits<std::uint32_t>::max)())
				++transaction->nativeWorldForeignCommitCount;
		};
		if (!token || token.transactionIdentity !=
				reinterpret_cast<std::uintptr_t>(transaction) ||
			transaction->nativeWorldSetDirtyCommitCount ==
				(std::numeric_limits<std::uint32_t>::max)() ||
			token.commitIdentity != transaction->nativeWorldSetDirtyCommitCount + 1u ||
			token.commitIdentity > transaction->nativeWorldSetDirtyIssuedCount) {
			noteForeign();
			return;
		}

		++transaction->nativeWorldSetDirtyCommitCount;
		if (exactNativeTargetsObserved)
			++transaction->nativeWorldPlanarTargetIsolationCount;
		else
			++transaction->nativeWorldPlanarTargetIsolationFailures;

		if (reflectedWindingRequired) {
			++transaction->nativeWorldGeometryRequiredCommitCount;
			++transaction->nativeWorldRasterizerApplyAttempts;
			if (rasterizerPolicyApplied)
				++transaction->nativeWorldRasterizerApplySuccesses;
			else
				++transaction->nativeWorldRasterizerApplyFailures;
		} else {
			++transaction->nativeWorldOrdinaryRequiredCommitCount;
			++transaction->nativeWorldOrdinaryRasterizerApplyAttempts;
			if (rasterizerPolicyApplied)
				++transaction->nativeWorldOrdinaryRasterizerApplySuccesses;
			else
				++transaction->nativeWorldOrdinaryRasterizerApplyFailures;
		}
	}
}

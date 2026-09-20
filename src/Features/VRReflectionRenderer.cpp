#include "MirrorPerformance.h"
#include "VRReflectionRenderer.h"
#include "MirrorAuthoring.h"
#include "MirrorSettings.h"
#include "VRMirrorScene.h"
#include "VRMirrorPolicy.h"
#include "VRMirrorRenderState.h"
#include "VRMirrorDeferred.h"
#include "VRMirrorLighting.h"
#include "VRMirrorPose.h"
#include "VRMirrorSkin.h"
#include "MirrorFaceGenSnapshot.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include <d3d11.h>
#include <filesystem>
#include <windows.h>
#define MIRROR_VR_PRODUCTION_TU 1
#include <wrl/client.h>

#include "RE/Bethesda/BSGraphics.h"
#include "RE/Bethesda/BSLock.h"
#include "RE/Bethesda/Main.h"
#include "RE/Bethesda/MemoryManager.h"
#include "RE/Bethesda/PlayerCharacter.h"
#include "RE/Bethesda/TESCamera.h"
#include "RE/NetImmerse/NiCamera.h"
#include "RE/NetImmerse/NiCloningProcess.h"
#include "RE/NetImmerse/NiNode.h"
#include "RE/NetImmerse/NiUpdateData.h"

#include "Features/PlanarMirrorLookup.h"
#include "Features/PlanarMirrorMath.h"
#include "Features/PlanarMirrors.h"
#include "Features/PlanarMirrorSilhouetteData.h"
#include "Features/MirrorSceneRenderer.h"
#include "Features/ReflectionRuntime.h"
#include "Features/NativePassPoolRecovery.h"
#include "Features/NiVirtualDispatch.h"
#include "Globals.h"
#include "Hooks.h"
#include "RenderHooks.h"
#include "Utils/D3D.h"

namespace
{
	using Microsoft::WRL::ComPtr;

	constexpr std::uint32_t kEyeCount = 2;
	constexpr std::size_t kVRNiCameraSize = 0x230;
	constexpr std::size_t kVRCubeCameraSize = 0x270;
	constexpr std::size_t kVRCullingGroupSize = 0x180;
	constexpr std::size_t kVRRenderContextSize = 0x2D8;
	constexpr std::size_t kMaximumSubmittedRoots = VRMirrorScene::kMaximumRoots;
	constexpr std::size_t kMaximumVisibilityObjects = 2048;

	struct VRCameraStateData
	{
		RE::BSGraphics::ViewData camViewData[kEyeCount];  
		RE::NiPoint3 posAdjust[kEyeCount];                
		RE::NiPoint3 currentPosAdjust[kEyeCount];         
		RE::NiPoint3 previousPosAdjust[kEyeCount];        
		const RE::NiCamera* referenceCamera;               
		bool useJitter;                                    
		std::byte pad[0x0F]{};                             
	};
	static_assert(offsetof(VRCameraStateData, posAdjust) == 0x420);
	static_assert(offsetof(VRCameraStateData, referenceCamera) == 0x468);
	static_assert(sizeof(VRCameraStateData) == 0x480);

	struct alignas(16) StereoMirrorData
	{
		DirectX::XMFLOAT4 screen{};
		DirectX::XMFLOAT4 mirrorCenter{};
		DirectX::XMFLOAT4 mirrorNormal{};
		DirectX::XMFLOAT4 mirrorTangent{};
		DirectX::XMFLOAT4 mirrorBitangent{};
		DirectX::XMFLOAT4 outputEncoding{};
		DirectX::XMFLOAT4 mainEye[kEyeCount]{};
		DirectX::XMFLOAT4X4 mainViewProjection[kEyeCount]{};
		DirectX::XMFLOAT4 reflectedEye[kEyeCount]{};
		DirectX::XMFLOAT4X4 reflectedViewProjection[kEyeCount]{};
		DirectX::XMFLOAT4 mainDepthRange{ 0, 1, 0, 0 };
	};
	static_assert(sizeof(StereoMirrorData) == 432);

	using PresentationVertex = PlanarMirrorSilhouetteData::Float2;
	constexpr std::array<PresentationVertex, 6> kRectangleVertices{ {
		{ -1.0f, -1.0f },
		{ 1.0f, -1.0f },
		{ -1.0f, 1.0f },
		{ -1.0f, 1.0f },
		{ 1.0f, -1.0f },
		{ 1.0f, 1.0f }
	} };
	constexpr std::size_t kAuthoredVertexCount = PlanarMirrorSilhouetteData::kTriangleCount * 3u;
	constexpr std::size_t kPresentationVertexCount = kRectangleVertices.size() + kAuthoredVertexCount;

	constexpr auto BuildPresentationVertices() noexcept
	{
		std::array<PresentationVertex, kPresentationVertexCount> vertices{};
		for (std::size_t index = 0; index < kRectangleVertices.size(); ++index)
			vertices[index] = kRectangleVertices[index];
		std::size_t destination = kRectangleVertices.size();
		for (const auto& triangle : PlanarMirrorSilhouetteData::kTriangles) {
			vertices[destination++] = triangle.a;
			vertices[destination++] = triangle.b;
			vertices[destination++] = triangle.c;
		}
		return vertices;
	}

	inline constexpr auto kPresentationVertices = BuildPresentationVertices();
	static_assert(kPresentationVertices.size() == 17811u);

	struct AccumulatorBackup
	{
		void* camera{ nullptr };
		void* shadowSceneNode{ nullptr };
		std::uint32_t renderMode{ 0 };
		std::uint8_t alphaEnabled{ 0 };
		std::uint8_t depthPrepassEnabled{ 0 };
		std::array<std::byte, 16> eye0{};
		std::array<std::byte, 16> eye1{};
		bool armed{ false };
	};

	struct RendererBackup
	{
		void* renderer{ nullptr };
		std::uint32_t dirtyFlags{ 0 };
		
		std::array<std::byte, VRMirrorRenderState::kSize> modes{};
		std::uint32_t materialDepthMode{};
		std::uint8_t materialDepthOverride{};
		bool armed{ false };
	};

	struct VisibilityMutation
	{
		RE::NiAVObject* object{ nullptr };
		bool appCulled{ false };
	};

	std::atomic<bool> g_installed{ false };
	std::atomic<bool> g_faulted{ false };
	thread_local VRReflectionRenderer::CaptureKind g_captureKind =
		VRReflectionRenderer::CaptureKind::kNone;

	RE::NiCamera* g_cubeOwner = nullptr;    
	RE::NiCamera* g_stereoCamera = nullptr; 
	alignas(16) std::array<std::byte, kVRCullingGroupSize> g_cullingGroup{};
	alignas(16) std::array<std::byte, kVRRenderContextSize> g_mirrorRenderContext{};
	bool g_cullingGroupConstructed = false;

	PlanarMirrors::RenderTarget g_mirrorTarget;
	VRMirrorDeferred::Resources g_materialResources;
	VRMirrorDeferred::Constants g_materialConstants{};
	VRMirrorScene::Frustum g_captureFrusta[2]{};
	ComPtr<ID3D11ComputeShader> g_materialResolve;
	bool g_materialCompileAttempted = false;
	bool g_materialPhase = false;
	std::array<DirectX::XMFLOAT4X4, kEyeCount> g_mirrorViewProjection{};
	std::array<DirectX::XMFLOAT4, kEyeCount> g_reflectedEye{};
	std::array<DirectX::XMFLOAT4X4, kEyeCount> g_mainViewProjection{};
	std::array<DirectX::XMFLOAT4, kEyeCount> g_mainEye{};
	std::array<RE::NiMatrix3, kEyeCount> g_mainEyeBasis{};
	DirectX::XMFLOAT3 g_mainOriginOffset{};
	DirectX::XMFLOAT4 g_mainDepthRange{ 0, 1, 0, 0 };
	bool g_mirrorReady = false;

	struct StereoMirrorImage
	{
		ComPtr<ID3D11Texture2D> texture;
		ComPtr<ID3D11ShaderResourceView> srv;
		std::array<DirectX::XMFLOAT4X4, kEyeCount> viewProjection{};
		std::array<DirectX::XMFLOAT4, kEyeCount> eye{};
		MirrorSceneRenderer::VRMirrorCaptureRequest request{};
	};

	struct PendingMirrorImage
	{
		StereoMirrorImage image;
		ComPtr<ID3D11Query> coverage;
		std::uint64_t serial{}, capturedAt{};
		bool pending{ false };
	};
	struct MirrorSlot
	{
		StereoMirrorImage front;
		std::array<PendingMirrorImage, VRMirrorPolicy::kPendingFrameCount> frames{};
		MirrorSceneRenderer::VRMirrorCaptureRequest owner{};
		std::uint64_t nextSerial{}, publishedSerial{}, capturedAt{};
		bool ready{ false };
	};

	static_assert(MirrorSceneRenderer::kVRMirrorSlotCount == 1u);
	auto& g_mirrorSlot = *new MirrorSlot();
	ID3D11Query* g_activeCoverageQuery = nullptr;
	bool g_coverageQueryBegan = false;
	const char* g_captureStage = "idle";
	VRMirrorPolicy::CaptureClock g_captureClock;

	ComPtr<ID3D11RenderTargetView> g_mainColorRTV;
	ComPtr<ID3D11DepthStencilView> g_mainDepthDSV;
	ComPtr<ID3D11Texture2D> g_mainDepthSnapshot;
	ComPtr<ID3D11ShaderResourceView> g_mainDepthSnapshotSRV;
	D3D11_TEXTURE2D_DESC g_mainColorDesc{};

	ComPtr<ID3D11VertexShader> g_overlayVS;
	ComPtr<ID3D11PixelShader> g_overlayPS;
	ComPtr<ID3D11Buffer> g_overlayVertices;
	ComPtr<ID3D11ShaderResourceView> g_overlayVertexSRV;
	ComPtr<ID3D11Buffer> g_overlayCB;
	ComPtr<ID3D11SamplerState> g_overlaySampler;
	ComPtr<ID3D11RasterizerState> g_overlayRasterizer;
	ComPtr<ID3D11BlendState> g_overlayBlend;
	ComPtr<ID3D11DepthStencilState> g_overlayDepthStencil;
	ID3D11Device* g_overlayDevice = nullptr;
	ID3D11Device* g_deviceIdentity = nullptr;
	bool g_overlayCompileAttempted = false;

	AccumulatorBackup g_accumulatorBackup{};
	RendererBackup g_rendererBackup{};
	std::array<VisibilityMutation, kMaximumVisibilityObjects> g_visibilityMutations{};
	std::size_t g_visibilityMutationCount = 0;
	std::array<RE::NiAVObject*, kMaximumSubmittedRoots> g_submittedRoots{};
	std::size_t g_submittedRootCount = 0;
	bool g_cameraOverrideArmed = false;
	bool g_targetBegan = false;
	bool g_privatePassesPending = false;
	void* g_privateRenderContext = nullptr;
	ComPtr<ID3D11RasterizerState> g_privateEntryRasterizer;
	bool g_privateEntryRasterizerArmed = false;
	RE::NiCamera* g_mainCameraForRestore = nullptr;

	[[nodiscard]] DirectX::XMFLOAT4X4 CopyRowsToMatrix(const __m128 rows[4]) noexcept
	{
		DirectX::XMMATRIX matrix{};
		matrix.r[0] = rows[0];
		matrix.r[1] = rows[1];
		matrix.r[2] = rows[2];
		matrix.r[3] = rows[3];
		DirectX::XMFLOAT4X4 output{};
		DirectX::XMStoreFloat4x4(std::addressof(output), matrix);
		return output;
	}

	template <std::size_t Size, class... Arguments>
	void AppendRasterDiagnostic(
		char (&destination)[Size],
		std::size_t& used,
		const char* format,
		Arguments... arguments) noexcept
	{
		if (used >= Size - 1)
			return;
		const int written = std::snprintf(
			destination + used, Size - used, format, arguments...);
		if (written > 0) {
			used += std::min<std::size_t>(
				static_cast<std::size_t>(written), Size - used - 1);
		}
	}

	void LogPrivateTargetRasterState(
		ID3D11DeviceContext* context,
		const PlanarMirrors::RenderTarget& target,
		bool entry) noexcept
	{
		if (!context)
			return;
		static std::array<std::uint64_t, 3> entryObservations{};
		static std::array<std::uint64_t, 3> rebindObservations{};
		const auto kindIndex = std::min<std::size_t>(
			static_cast<std::size_t>(g_captureKind), entryObservations.size() - 1);
		auto& observationCount = entry ? entryObservations[kindIndex] : rebindObservations[kindIndex];
		const auto observation = ++observationCount;
		const std::uint64_t interval = entry ? 240u : 262144u;
		if (observation != 1u && (observation % interval) != 0u)
			return;

		std::array<D3D11_VIEWPORT, D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE>
			viewports{};
		UINT viewportCount = static_cast<UINT>(viewports.size());
		context->RSGetViewports(&viewportCount, viewports.data());
		std::array<D3D11_RECT, D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE>
			scissorRects{};
		UINT scissorRectCount = static_cast<UINT>(scissorRects.size());
		context->RSGetScissorRects(&scissorRectCount, scissorRects.data());

		char viewportText[2048]{};
		std::size_t viewportTextUsed = 0;
		if (viewportCount == 0) {
			AppendRasterDiagnostic(viewportText, viewportTextUsed, "none");
		} else {
			for (UINT index = 0; index < viewportCount; ++index) {
				const auto& viewport = viewports[index];
				AppendRasterDiagnostic(
					viewportText, viewportTextUsed,
					"%sv%u=(%.1f,%.1f %.1fx%.1f z=%.3f:%.3f)",
					index == 0 ? "" : ",", index,
					viewport.TopLeftX, viewport.TopLeftY, viewport.Width, viewport.Height,
					viewport.MinDepth, viewport.MaxDepth);
			}
		}
		char scissorText[1024]{};
		std::size_t scissorTextUsed = 0;
		if (scissorRectCount == 0) {
			AppendRasterDiagnostic(scissorText, scissorTextUsed, "none");
		} else {
			for (UINT index = 0; index < scissorRectCount; ++index) {
				const auto& rect = scissorRects[index];
				AppendRasterDiagnostic(
					scissorText, scissorTextUsed, "%ss%u=(%ld,%ld)-(%ld,%ld)",
					index == 0 ? "" : ",", index,
					rect.left, rect.top, rect.right, rect.bottom);
			}
		}

		try {
			logger::info(
				"[VRReflections] private target raster phase={} sample={} kind={} target={}x{} eyes={} "
				"viewportCount={} viewports=[{}] scissorCount={} scissors=[{}]",
				entry ? "entry" : "pre-rebind", observation,
				static_cast<std::uint32_t>(g_captureKind), target.Width(), target.Height(),
				target.EyeCount(), viewportCount, viewportText, scissorRectCount, scissorText);
		} catch (...) {
			
		}
	}

	[[nodiscard]] bool IsSRGBFormat(DXGI_FORMAT format) noexcept
	{
		return format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB ||
			format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB ||
			format == DXGI_FORMAT_B8G8R8X8_UNORM_SRGB;
	}

	class ScopedPresentationState
	{
	public:
		explicit ScopedPresentationState(ID3D11DeviceContext* a_context) noexcept : context(a_context)
		{
			if (!context)
				return;
			context->OMGetRenderTargets(
				static_cast<UINT>(renderTargets.size()), renderTargets.data(), std::addressof(depthStencil));
			context->OMGetBlendState(std::addressof(blendState), blendFactor.data(), std::addressof(sampleMask));
			context->OMGetDepthStencilState(std::addressof(depthStencilState), std::addressof(stencilReference));
			context->RSGetState(std::addressof(rasterizerState));
			viewportCount = static_cast<UINT>(viewports.size());
			context->RSGetViewports(std::addressof(viewportCount), viewports.data());
			context->IAGetInputLayout(std::addressof(inputLayout));
			context->IAGetPrimitiveTopology(std::addressof(topology));
			context->VSGetShader(std::addressof(vertexShader), nullptr, nullptr);
			context->PSGetShader(std::addressof(pixelShader), nullptr, nullptr);
			context->GSGetShader(std::addressof(geometryShader), nullptr, nullptr);
			context->HSGetShader(std::addressof(hullShader), nullptr, nullptr);
			context->DSGetShader(std::addressof(domainShader), nullptr, nullptr);
			context->VSGetConstantBuffers(
				0, static_cast<UINT>(vertexConstantBuffers.size()), vertexConstantBuffers.data());
			context->VSGetShaderResources(
				0, static_cast<UINT>(vertexResources.size()), vertexResources.data());
			context->PSGetConstantBuffers(
				0, static_cast<UINT>(pixelConstantBuffers.size()), pixelConstantBuffers.data());
			context->PSGetShaderResources(
				0, static_cast<UINT>(pixelResources.size()), pixelResources.data());
			context->PSGetSamplers(0, static_cast<UINT>(pixelSamplers.size()), pixelSamplers.data());
			captured = true;
		}

		~ScopedPresentationState()
		{
			if (!captured || !context)
				return;
			std::array<ID3D11ShaderResourceView*, 2> nullPixelResources{};
			std::array<ID3D11ShaderResourceView*, 2> nullVertexResources{};
			context->PSSetShaderResources(0, static_cast<UINT>(nullPixelResources.size()), nullPixelResources.data());
			context->VSSetShaderResources(0, static_cast<UINT>(nullVertexResources.size()), nullVertexResources.data());
			context->OMSetRenderTargets(
				static_cast<UINT>(renderTargets.size()), renderTargets.data(), depthStencil);
			context->OMSetBlendState(blendState, blendFactor.data(), sampleMask);
			context->OMSetDepthStencilState(depthStencilState, stencilReference);
			context->RSSetState(rasterizerState);
			if (viewportCount != 0)
				context->RSSetViewports(viewportCount, viewports.data());
			context->IASetInputLayout(inputLayout);
			context->IASetPrimitiveTopology(topology);
			context->VSSetShader(vertexShader, nullptr, 0);
			context->PSSetShader(pixelShader, nullptr, 0);
			context->GSSetShader(geometryShader, nullptr, 0);
			context->HSSetShader(hullShader, nullptr, 0);
			context->DSSetShader(domainShader, nullptr, 0);
			context->VSSetConstantBuffers(
				0, static_cast<UINT>(vertexConstantBuffers.size()), vertexConstantBuffers.data());
			context->VSSetShaderResources(
				0, static_cast<UINT>(vertexResources.size()), vertexResources.data());
			context->PSSetConstantBuffers(
				0, static_cast<UINT>(pixelConstantBuffers.size()), pixelConstantBuffers.data());
			context->PSSetShaderResources(
				0, static_cast<UINT>(pixelResources.size()), pixelResources.data());
			context->PSSetSamplers(0, static_cast<UINT>(pixelSamplers.size()), pixelSamplers.data());
			ReleaseAll();
		}

		ScopedPresentationState(const ScopedPresentationState&) = delete;
		ScopedPresentationState& operator=(const ScopedPresentationState&) = delete;

	private:
		template <class T, std::size_t N>
		static void ReleaseArray(std::array<T*, N>& values) noexcept
		{
			for (auto*& value : values) {
				if (value)
					value->Release();
				value = nullptr;
			}
		}

		void ReleaseAll() noexcept
		{
			ReleaseArray(renderTargets);
			ReleaseArray(vertexConstantBuffers);
			ReleaseArray(vertexResources);
			ReleaseArray(pixelConstantBuffers);
			ReleaseArray(pixelResources);
			ReleaseArray(pixelSamplers);
			auto release = [](auto*& value) noexcept {
				if (value)
					value->Release();
				value = nullptr;
			};
			release(depthStencil);
			release(blendState);
			release(depthStencilState);
			release(rasterizerState);
			release(inputLayout);
			release(vertexShader);
			release(pixelShader);
			release(geometryShader);
			release(hullShader);
			release(domainShader);
		}

		ID3D11DeviceContext* context{ nullptr };
		std::array<ID3D11RenderTargetView*, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> renderTargets{};
		ID3D11DepthStencilView* depthStencil{ nullptr };
		ID3D11BlendState* blendState{ nullptr };
		std::array<float, 4> blendFactor{};
		UINT sampleMask{ 0xFFFFFFFFu };
		ID3D11DepthStencilState* depthStencilState{ nullptr };
		UINT stencilReference{ 0 };
		ID3D11RasterizerState* rasterizerState{ nullptr };
		std::array<D3D11_VIEWPORT, D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> viewports{};
		UINT viewportCount{ 0 };
		ID3D11InputLayout* inputLayout{ nullptr };
		D3D11_PRIMITIVE_TOPOLOGY topology{ D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED };
		ID3D11VertexShader* vertexShader{ nullptr };
		ID3D11PixelShader* pixelShader{ nullptr };
		ID3D11GeometryShader* geometryShader{ nullptr };
		ID3D11HullShader* hullShader{ nullptr };
		ID3D11DomainShader* domainShader{ nullptr };
		std::array<ID3D11Buffer*, 1> vertexConstantBuffers{};
		std::array<ID3D11ShaderResourceView*, 2> vertexResources{};
		std::array<ID3D11Buffer*, 1> pixelConstantBuffers{};
		std::array<ID3D11ShaderResourceView*, 2> pixelResources{};
		std::array<ID3D11SamplerState*, 1> pixelSamplers{};
		bool captured{ false };
	};

	void ReleaseOverlayResources() noexcept
	{
		g_overlayDepthStencil.Reset();
		g_overlayBlend.Reset();
		g_overlayRasterizer.Reset();
		g_overlaySampler.Reset();
		g_overlayCB.Reset();
		g_overlayVertexSRV.Reset();
		g_overlayVertices.Reset();
		g_overlayPS.Reset();
		g_overlayVS.Reset();
		g_overlayDevice = nullptr;
		g_overlayCompileAttempted = false;
	}

	void ReleaseMirrorImages() noexcept
	{
		g_mirrorReady = false;
		g_mirrorSlot = {};
		g_captureClock.Reset();
		g_activeCoverageQuery = nullptr;
		g_coverageQueryBegan = false;
	}

	void ReleaseDeviceResources(ID3D11DeviceContext* context) noexcept
	{
		g_mirrorTarget.Release(context);
		g_materialResources.Reset();
		g_materialResolve.Reset();
		g_materialCompileAttempted = false;
		g_materialPhase = false;
		ReleaseMirrorImages();
		g_mainDepthSnapshotSRV.Reset();
		g_mainDepthSnapshot.Reset();
		g_mainDepthDSV.Reset();
		g_mainColorRTV.Reset();
		g_mainColorDesc = {};
		ReleaseOverlayResources();
	}

	[[nodiscard]] DXGI_FORMAT DepthResourceFormat(DXGI_FORMAT source) noexcept
	{
		switch (source) {
		case DXGI_FORMAT_D16_UNORM:
		case DXGI_FORMAT_R16_TYPELESS:
			return DXGI_FORMAT_R16_TYPELESS;
		case DXGI_FORMAT_D24_UNORM_S8_UINT:
		case DXGI_FORMAT_R24G8_TYPELESS:
			return DXGI_FORMAT_R24G8_TYPELESS;
		case DXGI_FORMAT_D32_FLOAT:
		case DXGI_FORMAT_R32_TYPELESS:
			return DXGI_FORMAT_R32_TYPELESS;
		case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
		case DXGI_FORMAT_R32G8X24_TYPELESS:
			return DXGI_FORMAT_R32G8X24_TYPELESS;
		default:
			return DXGI_FORMAT_UNKNOWN;
		}
	}

	[[nodiscard]] DXGI_FORMAT DepthSRVFormat(DXGI_FORMAT resource) noexcept
	{
		switch (resource) {
		case DXGI_FORMAT_R16_TYPELESS:
			return DXGI_FORMAT_R16_UNORM;
		case DXGI_FORMAT_R24G8_TYPELESS:
			return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
		case DXGI_FORMAT_R32_TYPELESS:
			return DXGI_FORMAT_R32_FLOAT;
		case DXGI_FORMAT_R32G8X24_TYPELESS:
			return DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
		default:
			return DXGI_FORMAT_UNKNOWN;
		}
	}

	[[nodiscard]] bool CaptureMainTargets(ID3D11Device* device, ID3D11DeviceContext* context) noexcept
	{
		if (!device || !context)
			return false;
		g_mainColorRTV.Reset();
		g_mainDepthDSV.Reset();
		ID3D11RenderTargetView* color = nullptr;
		ID3D11DepthStencilView* depth = nullptr;
		context->OMGetRenderTargets(1, std::addressof(color), std::addressof(depth));
		g_mainColorRTV.Attach(color);
		g_mainDepthDSV.Attach(depth);
		if (!g_mainColorRTV || !g_mainDepthDSV)
			return false;
		D3D11_VIEWPORT mainViewport{};
		UINT viewportCount = 1;
		context->RSGetViewports(&viewportCount, &mainViewport);
		if (viewportCount != 1 || !std::isfinite(mainViewport.MinDepth) ||
			!std::isfinite(mainViewport.MaxDepth) || mainViewport.MinDepth < 0 ||
			mainViewport.MaxDepth > 1 || mainViewport.MaxDepth <= mainViewport.MinDepth) return false;
		g_mainDepthRange = { mainViewport.MinDepth, mainViewport.MaxDepth, 0, 0 };

		ComPtr<ID3D11Resource> colorResource;
		g_mainColorRTV->GetResource(colorResource.GetAddressOf());
		ComPtr<ID3D11Texture2D> colorTexture;
		if (!colorResource || FAILED(colorResource.As(std::addressof(colorTexture))) || !colorTexture)
			return false;
		colorTexture->GetDesc(std::addressof(g_mainColorDesc));
		if (g_mainColorDesc.Width < 2 || (g_mainColorDesc.Width & 1u) != 0 ||
			g_mainColorDesc.Height == 0 || g_mainColorDesc.ArraySize != 1 ||
			g_mainColorDesc.SampleDesc.Count != 1)
			return false;

		const auto extent = VRMirrorPolicy::CaptureExtent(g_mainColorDesc.Width / 2u, g_mainColorDesc.Height, MirrorSettings::ResolutionOverride());
		if (!g_mirrorTarget.Ready() || !g_mirrorTarget.UsesDevice(device) ||
			!g_mirrorTarget.StereoSideBySide() || g_mirrorTarget.PerEyeWidth() != extent.width ||
			g_mirrorTarget.Height() != extent.height) {
			const bool deviceChanged = g_mirrorTarget.Ready() && !g_mirrorTarget.UsesDevice(device);
			if (deviceChanged) ReleaseMirrorImages();
			static std::uint64_t nextAttempt = 0;
			static VRMirrorPolicy::Extent requested{};
			const auto tick = GetTickCount64();
			if (deviceChanged || requested.width != extent.width || requested.height != extent.height) nextAttempt = 0;
			requested = extent;

			if (tick >= nextAttempt) {
				if (g_mirrorTarget.Create(
					device, extent.width, extent.height,
					DXGI_FORMAT_R16G16B16A16_FLOAT, true, true, true, false, false, false)) {
					for (auto& frame : g_mirrorSlot.frames) frame = {};
					g_captureClock.Reset();
					MirrorSettings::CaptureResourcesChanged();
					nextAttempt = 0;
					logger::info("[MirrorSettings] VR capture resized to {}x{} per eye; prior front retained={}", extent.width, extent.height, g_mirrorSlot.ready);
				} else {
					nextAttempt = tick + 1000u;
					logger::warn("[MirrorSettings] VR allocation failed for {}x{} per eye; retry bounded, prior size retained", extent.width, extent.height);
				}
			}
			if (!g_mirrorTarget.Ready() || !g_mirrorTarget.UsesDevice(device)) return false;
		}
		static std::uint64_t materialRetry = 0;
		static std::uint32_t materialWidth = 0, materialHeight = 0;
		static ID3D11Device* materialDevice = nullptr;
		if (materialWidth != g_mirrorTarget.Width() || materialHeight != g_mirrorTarget.Height() || materialDevice != device) {
			materialRetry = 0;
			materialWidth = g_mirrorTarget.Width(); materialHeight = g_mirrorTarget.Height(); materialDevice = device;
		}
		const auto materialTick = GetTickCount64();
		if (materialTick < materialRetry) return false;
		if (!g_materialResources.Ensure(device, materialWidth, materialHeight)) {
			materialRetry = materialTick + 1000u;
			logger::warn("[MirrorSettings] VR material allocation failed at {}x{}; retry in one second", materialWidth, materialHeight);
			return false;
		}
		if (!g_materialResolve && !g_materialCompileAttempted) {
			g_materialCompileAttempted = true;
			g_materialResolve.Attach(static_cast<ID3D11ComputeShader*>(Util::CompileShader(
				L"Data\\Shaders\\MirrorsOfFallout\\PlanarMirrorResolveVR.hlsl", {}, "cs_5_0", "main")));
			if (!g_materialResolve)
				logger::error("[VRReflections] private material resolve compilation failed");
		}
		if (!g_materialResolve)
			return false;

		ComPtr<ID3D11Resource> depthResource;
		g_mainDepthDSV->GetResource(depthResource.GetAddressOf());
		ComPtr<ID3D11Texture2D> depthTexture;
		if (!depthResource || FAILED(depthResource.As(std::addressof(depthTexture))) || !depthTexture)
			return false;
		D3D11_TEXTURE2D_DESC sourceDepthDesc{};
		depthTexture->GetDesc(std::addressof(sourceDepthDesc));
		const DXGI_FORMAT snapshotResourceFormat = DepthResourceFormat(sourceDepthDesc.Format);
		const DXGI_FORMAT snapshotSRVFormat = DepthSRVFormat(snapshotResourceFormat);
		if (sourceDepthDesc.Width != g_mainColorDesc.Width ||
			sourceDepthDesc.Height != g_mainColorDesc.Height || sourceDepthDesc.ArraySize != 1 ||
			sourceDepthDesc.MipLevels != 1 || sourceDepthDesc.SampleDesc.Count != 1 ||
			snapshotResourceFormat == DXGI_FORMAT_UNKNOWN || snapshotSRVFormat == DXGI_FORMAT_UNKNOWN)
			return false;

		bool recreateDepth = !g_mainDepthSnapshot || !g_mainDepthSnapshotSRV;
		if (!recreateDepth) {
			D3D11_TEXTURE2D_DESC existing{};
			g_mainDepthSnapshot->GetDesc(std::addressof(existing));
			recreateDepth = existing.Width != sourceDepthDesc.Width || existing.Height != sourceDepthDesc.Height ||
				existing.Format != snapshotResourceFormat;
		}
		if (recreateDepth) {
			g_mainDepthSnapshotSRV.Reset();
			g_mainDepthSnapshot.Reset();
			D3D11_TEXTURE2D_DESC snapshotDesc = sourceDepthDesc;
			snapshotDesc.Format = snapshotResourceFormat;
			snapshotDesc.Usage = D3D11_USAGE_DEFAULT;
			snapshotDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			snapshotDesc.CPUAccessFlags = 0;
			snapshotDesc.MiscFlags = 0;
			if (FAILED(device->CreateTexture2D(
					std::addressof(snapshotDesc), nullptr, g_mainDepthSnapshot.GetAddressOf())))
				return false;
			D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
			srvDesc.Format = snapshotSRVFormat;
			srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
			srvDesc.Texture2D.MostDetailedMip = 0;
			srvDesc.Texture2D.MipLevels = 1;
			if (FAILED(device->CreateShaderResourceView(
					g_mainDepthSnapshot.Get(), std::addressof(srvDesc),
					g_mainDepthSnapshotSRV.GetAddressOf())))
				return false;
		}
		context->CopyResource(g_mainDepthSnapshot.Get(), depthTexture.Get());
		return true;
	}

	[[nodiscard]] bool EnsureOverlayResources(ID3D11Device* device) noexcept
	{
		if (!device)
			return false;
		if (g_overlayDevice != device) {
			ReleaseOverlayResources();
			g_overlayDevice = device;
		}
		if (g_overlayVS && g_overlayPS && g_overlayVertices && g_overlayVertexSRV && g_overlayCB &&
			g_overlaySampler && g_overlayRasterizer && g_overlayBlend && g_overlayDepthStencil)
			return true;
		if (g_overlayCompileAttempted)
			return false;
		g_overlayCompileAttempted = true;

		g_overlayVS.Attach(static_cast<ID3D11VertexShader*>(Util::CompileShader(
			L"Data\\Shaders\\MirrorsOfFallout\\PlanarMirrorOverlayVR.hlsl", {}, "vs_5_0", "VSMain")));
		g_overlayPS.Attach(static_cast<ID3D11PixelShader*>(Util::CompileShader(
			L"Data\\Shaders\\MirrorsOfFallout\\PlanarMirrorOverlayVR.hlsl", {}, "ps_5_0", "PSMain")));
		if (!g_overlayVS || !g_overlayPS)
			return false;

		D3D11_BUFFER_DESC vertexDesc{};
		vertexDesc.ByteWidth = static_cast<UINT>(sizeof(kPresentationVertices));
		vertexDesc.Usage = D3D11_USAGE_IMMUTABLE;
		vertexDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		vertexDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		vertexDesc.StructureByteStride = sizeof(PresentationVertex);
		D3D11_SUBRESOURCE_DATA vertexData{};
		vertexData.pSysMem = kPresentationVertices.data();
		if (FAILED(device->CreateBuffer(
				std::addressof(vertexDesc), std::addressof(vertexData), g_overlayVertices.GetAddressOf())))
			return false;
		D3D11_SHADER_RESOURCE_VIEW_DESC vertexViewDesc{};
		vertexViewDesc.Format = DXGI_FORMAT_UNKNOWN;
		vertexViewDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		vertexViewDesc.Buffer.NumElements = static_cast<UINT>(kPresentationVertices.size());
		if (FAILED(device->CreateShaderResourceView(
				g_overlayVertices.Get(), std::addressof(vertexViewDesc), g_overlayVertexSRV.GetAddressOf())))
			return false;

		D3D11_BUFFER_DESC constantDesc{};
		constantDesc.ByteWidth = sizeof(StereoMirrorData);
		constantDesc.Usage = D3D11_USAGE_DEFAULT;
		constantDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		if (FAILED(device->CreateBuffer(std::addressof(constantDesc), nullptr, g_overlayCB.GetAddressOf())))
			return false;

		D3D11_SAMPLER_DESC samplerDesc{};
		samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
		samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
		if (FAILED(device->CreateSamplerState(std::addressof(samplerDesc), g_overlaySampler.GetAddressOf())))
			return false;

		D3D11_RASTERIZER_DESC rasterizerDesc{};
		rasterizerDesc.FillMode = D3D11_FILL_SOLID;
		rasterizerDesc.CullMode = D3D11_CULL_NONE;
		rasterizerDesc.DepthClipEnable = FALSE;
		if (FAILED(device->CreateRasterizerState(
				std::addressof(rasterizerDesc), g_overlayRasterizer.GetAddressOf())))
			return false;

		D3D11_BLEND_DESC blendDesc{};
		blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
		if (FAILED(device->CreateBlendState(std::addressof(blendDesc), g_overlayBlend.GetAddressOf())))
			return false;

		D3D11_DEPTH_STENCIL_DESC depthDesc{};
		depthDesc.DepthEnable = FALSE;
		depthDesc.StencilEnable = FALSE;
		if (FAILED(device->CreateDepthStencilState(
				std::addressof(depthDesc), g_overlayDepthStencil.GetAddressOf())))
			return false;
		logger::info("[VRReflections] compiled stereo PlanarMirrorOverlayVR presentation pipeline");
		return true;
	}

	using CubeCameraCtor_t = void* (*)(void*, void*, void*);
	using NiCameraCtor_t = RE::NiCamera* (*)(void*, std::uint32_t);
	using ConstructCullingGroup_t = void (*)(void*, std::uint32_t, std::uint32_t);
	using InitializeRenderContext_t = void* (*)(void*, RE::NiCamera*, void*);
	using GroupReset_t = void (*)(void*);
	using GroupStartAdding_t = void (*)(void*, RE::NiCamera*);
	using GroupAdd_t = void (*)(void*, RE::NiAVObject*, const RE::NiBound*, std::uint64_t);
	using GroupProcess_t = void (*)(void*, bool);
	using GroupAccumulate_t = void (*)(void*, void*);
	using CameraEyePosition_t = const RE::NiPoint3* (*)(RE::NiCamera*, std::uint32_t);
	using RendererFlush_t = void (*)();
	using SetCameraData_t = void (*)(void*, RE::NiCamera*, bool, float, float);
	using BuildCameraStateData_t = void (*)(void*, void*, RE::NiCamera*, bool);
	using FindCameraStateData_t = const VRCameraStateData* (*)(void*, RE::NiCamera*, bool);
	using ApplyCameraStateData_t = void (*)(void*, const VRCameraStateData*, const void*, float, float);
	using RenderGeometryGroup_t = void (*)(void*, std::uint8_t, void*, bool);
	using RenderSortedGeometry_t = void (*)(void*, void*);
	using ClearAccumulator_t = void (*)(void*);
	using SetMaterialDepthMode_t = void (*)(std::uint32_t);

	constexpr std::uint32_t kMaterialMode = VRMirrorDeferred::kMaterialMode;
	constexpr std::uintptr_t kFindCameraStateData = 0x1DAAF30;
	constexpr std::uintptr_t kApplyCameraStateData = 0x1DAA860;
	constexpr std::uintptr_t kRenderGeometryGroup = 0x281E400;
	constexpr std::uintptr_t kRenderSortedGeometry = 0x281E640;
	constexpr std::uintptr_t kRenderAlphaTestedGeometry = 0x281D220;
	constexpr std::uintptr_t kRenderAlphaBlendedGeometry = 0x281D380;
	constexpr std::uintptr_t kSetMaterialDepthMode = 0x2880190;
	constexpr std::uintptr_t kMaterialDepthMode = 0x3924204;

	constexpr std::uintptr_t kMaterialDepthOverride = 0x6886DA8;
	constexpr std::uintptr_t kFlushRenderContext = 0x2891300;
	constexpr std::uintptr_t kFinishAccumulatorCamera = 0x299C000;
	constexpr std::uintptr_t kClearAccumulatorGroups = 0x281EC00;

	[[nodiscard]] bool VerifyPrivateEntries() noexcept
	{
		
		struct Entry { std::uintptr_t rva; std::array<std::uint8_t, 16> bytes; };
		constexpr Entry entries[]{
			{ kFindCameraStateData, {0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57,0x48,0x83,0xEC,0x20,0x48} },
			{ kApplyCameraStateData, {0x48,0x89,0x5C,0x24,0x10,0x56,0x48,0x83,0xEC,0x20,0x48,0x8B,0x82,0x68,0x04,0x00} },
			{ kRenderGeometryGroup, {0x48,0x89,0x6C,0x24,0x18,0x48,0x89,0x7C,0x24,0x20,0x41,0x56,0x48,0x83,0xEC,0x20} },
			{ kRenderSortedGeometry, {0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x30,0x48,0x8B,0xF9,0x48,0x8B,0xCA} },
			{ kRenderAlphaTestedGeometry, {0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x20,0x80,0xB9,0x7C,0xF6,0x00,0x00} },
			{ kRenderAlphaBlendedGeometry, {0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x20,0x80,0xB9,0x7C,0xF6,0x00,0x00} },
			{ kSetMaterialDepthMode, {0x89,0x0D,0x6E,0x40,0x0A,0x01,0xC3,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC} },
			{ 0x287B8BE, {0x80,0x3D,0xE3,0xB4,0x00,0x04,0x00,0x75,0x38,0x48,0x85,0xC9,0x74,0x09,0x48,0x8D} },
			{ kFlushRenderContext, {0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,0xD9,0x48,0x8B,0x49,0x38,0x48,0x85,0xC9} },
			{ kFinishAccumulatorCamera, {0x48,0xC7,0x41,0x10,0x00,0x00,0x00,0x00,0xC3,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC} },
			{ kClearAccumulatorGroups, {0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57,0x48,0x83,0xEC,0x20,0x48} }
		};
		for (const auto& entry : entries) {
			const auto address = ReflectionRuntime::Address(entry.rva);
			if (!address || std::memcmp(reinterpret_cast<const void*>(address), entry.bytes.data(), entry.bytes.size()) != 0) {
				logger::error("[VRReflections] private material contract mismatch at VR+{:X}", entry.rva);
				return false;
			}
		}
		return true;
	}

	[[nodiscard]] void* GetAccumulator(const ReflectionRuntime::Contract& runtime) noexcept;

	void ClearPrivatePassesUnsafe() noexcept
	{
		if (!g_privatePassesPending)
			return;
		const auto* runtime = ReflectionRuntime::Get();
		void* accumulator = runtime ? GetAccumulator(*runtime) : nullptr;
		if (!accumulator)
			return;
		if (g_privateRenderContext)
			reinterpret_cast<ClearAccumulator_t>(ReflectionRuntime::Address(kFlushRenderContext))(g_privateRenderContext);
		reinterpret_cast<ClearAccumulator_t>(ReflectionRuntime::Address(kFinishAccumulatorCamera))(accumulator);
		reinterpret_cast<ClearAccumulator_t>(ReflectionRuntime::Address(kClearAccumulatorGroups))(accumulator);
		g_privatePassesPending = false;
		g_privateRenderContext = nullptr;
	}

	[[nodiscard]] void* GetAccumulator(const ReflectionRuntime::Contract& runtime) noexcept
	{
		if (!g_cubeOwner)
			return nullptr;
		auto* bytes = reinterpret_cast<std::byte*>(g_cubeOwner);
		return *reinterpret_cast<void**>(bytes + runtime.cubeCameraAccumulatorOffset);
	}

	void SetAccumulatorCamera(
		const ReflectionRuntime::Contract& runtime,
		void* accumulator,
		RE::NiCamera* camera) noexcept
	{
		if (!accumulator)
			return;
		auto* vtable = *reinterpret_cast<std::uintptr_t**>(accumulator);
		if (!vtable)
			return;
		using SetCamera_t = void (*)(void*, RE::NiCamera*);
		
		auto setCamera = *reinterpret_cast<SetCamera_t*>(
			reinterpret_cast<std::byte*>(vtable) + runtime.accumulatorSetCameraVtableOffset);
		setCamera(accumulator, camera);
	}

	[[nodiscard]] bool EnsureNativeObjectsUnsafe(const ReflectionRuntime::Contract& runtime) noexcept
	{
		if (!runtime.vr || runtime.niCameraSize != kVRNiCameraSize ||
			runtime.cubeCameraSize != kVRCubeCameraSize ||
			runtime.cullingGroupSize != kVRCullingGroupSize ||
			runtime.renderContextSize != kVRRenderContextSize)
			return false;

		auto& memory = RE::MemoryManager::GetSingleton();
		if (!g_cubeOwner) {
			void* storage = memory.Allocate(runtime.cubeCameraSize, 16, true);
			if (!storage)
				return false;
			auto ctor = reinterpret_cast<CubeCameraCtor_t>(
				ReflectionRuntime::Address(runtime.cubeCameraCtor));
			auto* camera = static_cast<RE::NiCamera*>(ctor(storage, nullptr, nullptr));
			if (!camera)
				return false;
			camera->IncRefCount();
			g_cubeOwner = camera;
		}
		if (!g_stereoCamera) {
			void* storage = memory.Allocate(runtime.niCameraSize, 16, true);
			if (!storage)
				return false;
			auto ctor = reinterpret_cast<NiCameraCtor_t>(
				ReflectionRuntime::Address(runtime.niCameraCtor));
			auto* camera = ctor(storage, kEyeCount);
			if (!camera)
				return false;
			camera->IncRefCount();
			g_stereoCamera = camera;
		}
		const auto stereoViewCount = *reinterpret_cast<const std::uint32_t*>(
			reinterpret_cast<const std::byte*>(g_stereoCamera) + 0x208);
		if (stereoViewCount != kEyeCount || !GetAccumulator(runtime))
			return false;

		if (!g_cullingGroupConstructed) {
			std::memset(g_cullingGroup.data(), 0, g_cullingGroup.size());
			struct FakeArray
			{
				void* pad;
				void* data;
			} fake{ nullptr, g_cullingGroup.data() };
			auto construct = reinterpret_cast<ConstructCullingGroup_t>(
				ReflectionRuntime::Address(runtime.cullingGroupConstruct));
			construct(std::addressof(fake), 0, 1);
			g_cullingGroupConstructed = true;
		}

		return true;
	}

	void ReleaseSubmittedRootsUnsafe() noexcept
	{
		while (g_submittedRootCount != 0) {
			auto*& root = g_submittedRoots[--g_submittedRootCount];
			if (root)
				root->DecRefCount();
			root = nullptr;
		}
	}

	struct FrikApiAbi
	{
		std::uint32_t(__cdecl* getVersion)();
		const char*(__cdecl* getModVersion)();
		bool(__cdecl* isSkeletonReady)();
		bool(__cdecl* isConfigOpen)();
		bool(__cdecl* isSelfieModeOn)();  
		void* setSelfieModeOn;
		void* isOffHandGrippingWeapon;
		void* isWristPipboyOpen;
		void* getIndexFingerTipPosition;
		void* getHandPoseSetTagState;
		void* getCurrentHandPose;
		void* setHandPose;
		void* setHandPoseCustomFingerPositions;
		void* clearHandPose;
		void* setHandPoseFingerPositions;
		void* clearHandPoseFingerPositions;
		void* registerOpenModSettingButtonToMainConfig;
		void* blockOffHandWeaponGripping;
		void* setHandPoseCustom;
		void* blockFeature;
		void* isFeatureBlocked;
		int(__cdecl* getConfigValue)(
			const char* caller, const char* section, const char* key, char* outBuf, int bufLen,
			const char* defaultValue);  
	};
	constexpr std::uint32_t kFrikApiConfigVersion = 4;
	constexpr std::uint32_t kFrikApiMaximumPlausibleVersion = 64;
	constexpr std::uint32_t kFrikConfigRefreshFrames = 300;
	constexpr const char* kFrikIniSection = "Fallout4VRBody";
	constexpr const char* kFrikApiCaller = "MirrorsOfFallout";
	
	constexpr std::size_t kVRPlayerWorldNodeOffset = 0x6E0;

	struct FrikState
	{
		bool probed{ false };
		bool present{ false };
		const FrikApiAbi* api{ nullptr };
		std::uint32_t apiVersion{ 0 };
		char modVersion[24]{};
		bool configValid{ false };
		std::uint32_t configFrame{ 0 };
		const char* configSource{ "defaults" };
		bool hideHead{ false };
		bool hideHeadEquipment{ true };
		bool hideSkin{ false };
		bool selfieIgnoreHideFlags{ true };
		float headBackPositionOffset{ 4.0f };
	};
	FrikState g_frik{};

	RE::NiNode* g_frikRigRoot = nullptr;
	RE::NiAVObject* g_frikPlayerRoot = nullptr;  
	constexpr float kFrikPlayerBoundRadius = 160.0f;

	[[nodiscard]] const char* FrikTrimLeft(const char* text) noexcept
	{
		while (text && (*text == ' ' || *text == '\t'))
			++text;
		return text;
	}

	[[nodiscard]] bool FrikParseBool(const char* text, bool fallback) noexcept
	{
		text = FrikTrimLeft(text);
		if (!text || !*text)
			return fallback;
		if (_strnicmp(text, "true", 4) == 0 || *text == '1')
			return true;
		if (_strnicmp(text, "false", 5) == 0 || *text == '0')
			return false;
		return fallback;
	}

	[[nodiscard]] float FrikParseFloat(const char* text, float fallback) noexcept
	{
		text = FrikTrimLeft(text);
		if (!text || !*text)
			return fallback;
		char* end = nullptr;
		const float value = std::strtof(text, std::addressof(end));
		return end != text && std::isfinite(value) ? value : fallback;
	}

	[[nodiscard]] const FrikApiAbi* FrikResolveApiUnsafe(const FrikApiAbi* (__cdecl* getApi)()) noexcept
	{
		__try {
			return getApi ? getApi() : nullptr;
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			return nullptr;
		}
	}

	[[nodiscard]] bool FrikApiVersionUnsafe(
		const FrikApiAbi* api, std::uint32_t& version, char* modVersion, std::size_t capacity) noexcept
	{
		__try {
			version = api->getVersion ? api->getVersion() : 0u;
			const char* mod = api->getModVersion ? api->getModVersion() : nullptr;
			std::size_t used = 0;
			for (; mod && mod[used] && used + 1 < capacity; ++used)
				modVersion[used] = mod[used];
			modVersion[used] = '\0';
			return true;
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			return false;
		}
	}

	[[nodiscard]] bool FrikApiConfigUnsafe(
		const FrikApiAbi* api, const char* key, char* out, int capacity, const char* fallback) noexcept
	{
		__try {
			if (!api->getConfigValue)
				return false;
			out[0] = '\0';
			return api->getConfigValue(kFrikApiCaller, kFrikIniSection, key, out, capacity, fallback) >= 0 && out[0] != '\0';
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			return false;
		}
	}

	void FrikProbe() noexcept
	{
		if (g_frik.probed)
			return;
		g_frik.probed = true;
		const HMODULE module = GetModuleHandleA("FRIK.dll");
		if (!module) {
			logger::info("[VRReflections] FRIK: FRIK.dll not loaded; body compatibility layer idle");
			return;
		}
		g_frik.present = true;
		auto getApi = reinterpret_cast<const FrikApiAbi* (__cdecl*)()>(GetProcAddress(module, "FRIKAPI_GetApi"));
		const FrikApiAbi* api = FrikResolveApiUnsafe(getApi);
		std::uint32_t version = 0;
		if (api && FrikApiVersionUnsafe(api, version, g_frik.modVersion, sizeof(g_frik.modVersion)) &&
			version >= 1u && version <= kFrikApiMaximumPlausibleVersion) {
			g_frik.api = api;
			g_frik.apiVersion = version;
		}
		logger::info(
			"[VRReflections] FRIK: loaded, API {} (v{}, mod '{}'); config via {}",
			g_frik.api ? "available" : "unavailable", g_frik.apiVersion, g_frik.modVersion,
			g_frik.api && g_frik.apiVersion >= kFrikApiConfigVersion ? "FRIK API" : "FRIK.ini");
	}

	[[nodiscard]] bool FrikReadIniFile() noexcept
	{
		try {
			const auto logDirectory = logger::log_directory();
			if (!logDirectory)
				return false;
			
			const auto path = logDirectory->parent_path() / "FRIK_Config" / "FRIK.ini";
			std::ifstream file(path);
			if (!file)
				return false;
			std::string line;
			bool inSection = false;
			bool sawKey = false;
			while (std::getline(file, line)) {
				const auto first = line.find_first_not_of(" \t\r");
				if (first == std::string::npos)
					continue;
				line.erase(0, first);
				if (line[0] == '#' || line[0] == ';')
					continue;
				if (line[0] == '[') {
					const auto close = line.find(']');
					const std::string section = close == std::string::npos ? line.substr(1) : line.substr(1, close - 1);
					inSection = _stricmp(section.c_str(), kFrikIniSection) == 0;
					continue;
				}
				if (!inSection)
					continue;
				const auto equals = line.find('=');
				if (equals == std::string::npos)
					continue;
				std::string key = line.substr(0, equals);
				const auto keyEnd = key.find_last_not_of(" \t");
				key.erase(keyEnd == std::string::npos ? 0 : keyEnd + 1);
				const char* value = line.c_str() + equals + 1;
				if (_stricmp(key.c_str(), "bHidePlayerHead") == 0)
					g_frik.hideHead = FrikParseBool(value, g_frik.hideHead);
				else if (_stricmp(key.c_str(), "bHidePlayerHeadEquipment") == 0)
					g_frik.hideHeadEquipment = FrikParseBool(value, g_frik.hideHeadEquipment);
				else if (_stricmp(key.c_str(), "bHidePlayerSkin") == 0)
					g_frik.hideSkin = FrikParseBool(value, g_frik.hideSkin);
				else if (_stricmp(key.c_str(), "bSelfieIgnoreHideFlags") == 0)
					g_frik.selfieIgnoreHideFlags = FrikParseBool(value, g_frik.selfieIgnoreHideFlags);
				else if (_stricmp(key.c_str(), "fHeadBackPositionOffset") == 0)
					g_frik.headBackPositionOffset = FrikParseFloat(value, g_frik.headBackPositionOffset);
				else
					continue;
				sawKey = true;
			}
			return sawKey;
		} catch (...) {
			return false;
		}
	}

	void FrikRefreshConfig(std::uint32_t frame) noexcept
	{
		if (!g_frik.present)
			return;
		if (g_frik.configValid && frame - g_frik.configFrame < kFrikConfigRefreshFrames)
			return;
		g_frik.configFrame = frame;
		bool read = false;
		if (g_frik.api && g_frik.apiVersion >= kFrikApiConfigVersion) {
			char buffer[64]{};
			const auto readBool = [&](const char* key, bool& target) noexcept {
				if (FrikApiConfigUnsafe(g_frik.api, key, buffer, sizeof(buffer), target ? "true" : "false")) {
					target = FrikParseBool(buffer, target);
					read = true;
				}
			};
			readBool("bHidePlayerHead", g_frik.hideHead);
			readBool("bHidePlayerHeadEquipment", g_frik.hideHeadEquipment);
			readBool("bHidePlayerSkin", g_frik.hideSkin);
			readBool("bSelfieIgnoreHideFlags", g_frik.selfieIgnoreHideFlags);
			if (FrikApiConfigUnsafe(g_frik.api, "fHeadBackPositionOffset", buffer, sizeof(buffer), "4")) {
				g_frik.headBackPositionOffset = FrikParseFloat(buffer, g_frik.headBackPositionOffset);
				read = true;
			}
			if (read)
				g_frik.configSource = "FRIK API";
		}
		if (!read && FrikReadIniFile()) {
			read = true;
			g_frik.configSource = "FRIK.ini";
		}
		const bool firstRead = !g_frik.configValid;
		g_frik.configValid = true;
		if (firstRead) {
			logger::info(
				"[VRReflections] FRIK config ({}): hideHead={} hideHeadEquipment={} hideSkin={} selfieIgnoreHideFlags={} headBackPositionOffset={:.2f}",
				g_frik.configSource, g_frik.hideHead, g_frik.hideHeadEquipment, g_frik.hideSkin,
				g_frik.selfieIgnoreHideFlags, g_frik.headBackPositionOffset);
		}
	}

	struct VRPlayerRig
	{
		RE::NiNode* playerWorldNode{ nullptr };
	};

	[[nodiscard]] VRPlayerRig ReadVRPlayerRig(const RE::PlayerCharacter* player) noexcept
	{
		VRPlayerRig rig{};
		if (!player || !REL::Module::IsVR())
			return rig;
		const auto base = reinterpret_cast<std::uintptr_t>(player);
		rig.playerWorldNode = *reinterpret_cast<RE::NiNode* const*>(base + kVRPlayerWorldNodeOffset);
		return rig;
	}

	[[nodiscard]] bool NodeNameIs(const RE::NiAVObject* object, const char* expected) noexcept
	{
		const char* name = object ? object->name.c_str() : nullptr;
		return name && _stricmp(name, expected) == 0;
	}

	[[nodiscard]] bool IsFrikHiddenHandWeapon(const RE::NiAVObject* object) noexcept
	{
		if (!object || !object->GetAppCulled() || !NodeNameIs(object, "Weapon"))
			return false;
		const auto* parent = object->parent;
		return parent && (NodeNameIs(parent, "RArm_Hand") || NodeNameIs(parent, "LArm_Hand"));
	}

	[[nodiscard]] bool RememberVisibilityMutation(RE::NiAVObject* object, bool desired) noexcept
	{
		if (!object)
			return true;
		for (std::size_t index = 0; index < g_visibilityMutationCount; ++index) {
			if (g_visibilityMutations[index].object == object) {
				if (object->GetAppCulled() != desired)
					NiVirtualDispatch::SetAppCulled(object, desired);
				return true;
			}
		}
		const bool current = object->GetAppCulled();
		if (current == desired)
			return true;
		if (g_visibilityMutationCount >= g_visibilityMutations.size())
			return false;
		g_visibilityMutations[g_visibilityMutationCount++] = { object, current };
		NiVirtualDispatch::SetAppCulled(object, desired);
		return true;
	}

	[[nodiscard]] bool WasOriginallyAppCulled(const RE::NiAVObject* object) noexcept
	{
		if (!object)
			return false;
		for (std::size_t index = 0; index < g_visibilityMutationCount; ++index) {
			const auto& mutation = g_visibilityMutations[index];
			if (mutation.object == object)
				return mutation.appCulled;
		}
		return false;
	}

	void ResetFrikCaptureRootsUnsafe() noexcept
	{
		g_frikRigRoot = nullptr;
		g_frikPlayerRoot = nullptr;
	}

#include "VRMirrorAvatar.inl"

	[[nodiscard]] bool PrepareVisibilityUnsafe(
		RE::NiAVObject* exclusionRoot) noexcept
	{
		g_visibilityMutationCount = 0;
		g_frikRigRoot = nullptr;
		static std::uint32_t s_prepareFrame = 0;
		++s_prepareFrame;
		FrikProbe();
		FrikRefreshConfig(s_prepareFrame);
		auto* player = RE::PlayerCharacter::GetSingleton();
		const VRPlayerRig rig = ReadVRPlayerRig(player);
		g_frikRigRoot = rig.playerWorldNode;
		RE::NiAVObject* thirdPerson = nullptr;
		RE::NiAVObject* firstPerson = nullptr;
		if (player) {
			if (player->loadedData && player->loadedData->data3D)
				thirdPerson = player->loadedData->data3D.get();

			firstPerson = thirdPerson ? nullptr : player->firstPerson3D.get();
		}
		auto* authoritativePlayerRoot = thirdPerson ? thirdPerson : firstPerson;
		g_frikPlayerRoot = VRMirrorAvatar::Prepare(authoritativePlayerRoot);

		if (authoritativePlayerRoot && !RememberVisibilityMutation(authoritativePlayerRoot, true)) return false;
		if (exclusionRoot && !RememberVisibilityMutation(exclusionRoot, true))
			return false;
		return true;
	}

	VRMirrorScene::RootSet g_submittedRootSet;

	[[nodiscard]] bool AddSubmittedRootUnsafe(
		GroupAdd_t add,
		RE::NiAVObject* root) noexcept
	{

		const auto membership = g_submittedRootSet.Insert(root);
		if (membership == VRMirrorScene::RootSet::Result::Duplicate)
			return true;
		if (membership == VRMirrorScene::RootSet::Result::Full || g_submittedRootCount >= g_submittedRoots.size())
			return false;
		MirrorSceneRenderer::SuppressPrivateGoreCapsForSubmission(root);
		root->IncRefCount();
		g_submittedRoots[g_submittedRootCount++] = root;
		if (root == g_frikPlayerRoot && g_frik.present) {

			RE::NiBound bound = root->worldBound;
			bound.center = root->world.translate;
			bound.fRadius = (std::max)(bound.fRadius, kFrikPlayerBoundRadius);
			add(g_cullingGroup.data(), root, std::addressof(bound), 0);
			return true;
		}
		add(g_cullingGroup.data(), root, std::addressof(root->worldBound), 0);
		return true;
	}

	[[nodiscard]] RE::NiAVObject* ActiveShadowSceneNodeUnsafe(
		const ReflectionRuntime::Contract& runtime) noexcept
	{
		const auto slot = ReflectionRuntime::Address(runtime.activeShadowSceneNode);
		return slot ? *reinterpret_cast<RE::NiAVObject**>(slot) : nullptr;
	}

	[[nodiscard]] bool PartitionSceneContainer(RE::NiAVObject* object) noexcept
	{

		const auto* rtti = object->GetRTTI();
		return object->IsNode() && rtti && VRMirrorScene::PartitionContainer(rtti->GetName());
	}

	[[nodiscard]] bool SnapshotMirrorLightingUnsafe(const ReflectionRuntime::Contract& runtime,
		const void* renderContext, const RE::NiPoint3& paneCenter) noexcept
	{
		auto* scene = ActiveShadowSceneNodeUnsafe(runtime);
		if (!scene) return false;
		if (!VRMirrorLighting::ContextAmbient(renderContext, g_materialConstants)) return false;
		auto* lock = reinterpret_cast<RE::BSSpinLock*>(reinterpret_cast<std::byte*>(scene) + VRMirrorLighting::kLightLock);
		bool result = false;
		lock->lock("MirrorsOfFalloutVRLightSnapshot");
		__try {

			result = VRMirrorLighting::Snapshot(scene, g_captureFrusta,
				{ paneCenter.x, paneCenter.y, paneCenter.z, 1.0f }, g_materialConstants);
		} __finally {
			lock->unlock();
		}
		return result;
	}

	[[nodiscard]] bool SubmitSceneRootsUnsafe(const ReflectionRuntime::Contract& runtime) noexcept
	{
		g_submittedRootCount = 0;
		MirrorSceneRenderer::RestorePrivateGoreCaps();  
		g_submittedRootSet.Clear();
		auto add = reinterpret_cast<GroupAdd_t>(ReflectionRuntime::Address(runtime.cullingGroupAdd));
		auto* shadowScene = ActiveShadowSceneNodeUnsafe(runtime);
		auto* shadowNode = shadowScene ? shadowScene->IsNode() : nullptr;
		if (!add || !shadowNode)
			return false;
		return VRMirrorScene::SubmitWorld<RE::NiAVObject>(shadowScene, g_frikPlayerRoot,
			[](RE::NiAVObject* object) noexcept -> RE::NiAVObject* { return object->parent; },
			[](RE::NiAVObject* object, auto visit) noexcept {
				auto* node = object->IsNode();
				if (!node)
					return false;
				auto& children = node->GetRuntimeData().children;
				for (std::uint16_t index = 0; index < children.capacity(); ++index) {
					if (!visit(children[index].get()))
						return false;
				}
				return true;
			},
			[](RE::NiAVObject* object) noexcept {
				if (object->GetAppCulled() || WasOriginallyAppCulled(object)) return false;
				const auto& bound = object->worldBound;
				return g_captureFrusta[0].Intersects(bound.center.x, bound.center.y, bound.center.z, bound.fRadius) ||
					g_captureFrusta[1].Intersects(bound.center.x, bound.center.y, bound.center.z, bound.fRadius);
			},
			PartitionSceneContainer,
			[add](RE::NiAVObject* object) noexcept { return AddSubmittedRootUnsafe(add, object); }) &&
			g_submittedRootCount != 0;
	}

	[[nodiscard]] void* ResolveRendererUnsafe(const ReflectionRuntime::Contract& runtime) noexcept
	{
		void* renderer = nullptr;
		if (runtime.vrRendererPointer) {
			const auto slot = ReflectionRuntime::Address(runtime.vrRendererPointer);
			if (slot)
				renderer = *reinterpret_cast<void**>(slot);
		}
		if (!renderer && runtime.mainRendererPointer) {
			const auto slot = ReflectionRuntime::Address(runtime.mainRendererPointer);
			if (slot)
				renderer = *reinterpret_cast<void**>(slot);
		}
		return renderer;
	}

	[[nodiscard]] bool ArmRendererUnsafe(const ReflectionRuntime::Contract& runtime) noexcept
	{
		if (!runtime.vr || runtime.rendererDepthStencilModeOffset != VRMirrorRenderState::kStart ||
			runtime.rendererCullModeOffset != VRMirrorRenderState::kCull ||
			runtime.rendererDirtyFlagsOffset != VRMirrorRenderState::kDirty)
			return false;
		void* renderer = ResolveRendererUnsafe(runtime);
		if (!renderer)
			return false;
		auto* bytes = static_cast<std::byte*>(renderer);
		g_rendererBackup.renderer = renderer;
		g_rendererBackup.dirtyFlags =
			*reinterpret_cast<std::uint32_t*>(bytes + runtime.rendererDirtyFlagsOffset);
		std::memcpy(g_rendererBackup.modes.data(), bytes + runtime.rendererDepthStencilModeOffset,
			g_rendererBackup.modes.size());
		g_rendererBackup.materialDepthMode = *reinterpret_cast<const std::uint32_t*>(
			ReflectionRuntime::Address(kMaterialDepthMode));
		g_rendererBackup.materialDepthOverride = *reinterpret_cast<const std::uint8_t*>(
			ReflectionRuntime::Address(kMaterialDepthOverride));
		g_rendererBackup.armed = true;

		*reinterpret_cast<std::uint8_t*>(ReflectionRuntime::Address(kMaterialDepthOverride)) = 1;
		VRMirrorRenderState::Begin(renderer);
		reinterpret_cast<SetMaterialDepthMode_t>(ReflectionRuntime::Address(kSetMaterialDepthMode))(3);
		return true;
	}

	void RestoreRendererUnsafe(const ReflectionRuntime::Contract& runtime) noexcept
	{
		if (!g_rendererBackup.armed || !g_rendererBackup.renderer)
			return;
		auto* bytes = static_cast<std::byte*>(g_rendererBackup.renderer);
		std::memcpy(bytes + runtime.rendererDepthStencilModeOffset, g_rendererBackup.modes.data(),
			g_rendererBackup.modes.size());
		*reinterpret_cast<std::uint32_t*>(bytes + runtime.rendererDirtyFlagsOffset) |=
			g_rendererBackup.dirtyFlags | 0x1Cu;
		auto flush = reinterpret_cast<RendererFlush_t>(
			ReflectionRuntime::Address(runtime.rendererFlush));
		reinterpret_cast<SetMaterialDepthMode_t>(ReflectionRuntime::Address(kSetMaterialDepthMode))(
			g_rendererBackup.materialDepthMode);
		*reinterpret_cast<std::uint8_t*>(ReflectionRuntime::Address(kMaterialDepthOverride)) =
			g_rendererBackup.materialDepthOverride;
		flush();
		g_rendererBackup = {};
	}

	[[nodiscard]] bool ArmAccumulatorUnsafe(
		const ReflectionRuntime::Contract& runtime,
		RE::NiCamera* camera) noexcept
	{
		void* accumulator = GetAccumulator(runtime);
		auto* shadowScene = ActiveShadowSceneNodeUnsafe(runtime);
		if (!accumulator || !shadowScene || !camera)
			return false;
		auto* bytes = static_cast<std::byte*>(accumulator);
		g_accumulatorBackup.camera = *reinterpret_cast<void**>(bytes + 0x10);
		g_accumulatorBackup.shadowSceneNode =
			*reinterpret_cast<void**>(bytes + runtime.accumulatorActiveShadowSceneNodeOffset);
		g_accumulatorBackup.renderMode =
			*reinterpret_cast<std::uint32_t*>(bytes + runtime.accumulatorRenderModeOffset);
		g_accumulatorBackup.alphaEnabled = *reinterpret_cast<std::uint8_t*>(bytes + 0xF67C);
		g_accumulatorBackup.depthPrepassEnabled = *reinterpret_cast<std::uint8_t*>(bytes + 0xF669);
		std::memcpy(g_accumulatorBackup.eye0.data(), bytes + runtime.accumulatorEye0Offset, 16);
		std::memcpy(g_accumulatorBackup.eye1.data(), bytes + runtime.accumulatorEye1Offset, 16);
		g_accumulatorBackup.armed = true;

		SetAccumulatorCamera(runtime, accumulator, camera);
		*reinterpret_cast<void**>(bytes + runtime.accumulatorActiveShadowSceneNodeOffset) = shadowScene;

		*reinterpret_cast<std::uint32_t*>(bytes + runtime.accumulatorRenderModeOffset) = kMaterialMode;
		
		*reinterpret_cast<std::uint8_t*>(bytes + 0xF67C) = 1;

		*reinterpret_cast<std::uint8_t*>(bytes + 0xF669) = 0;
		auto eyePosition = reinterpret_cast<CameraEyePosition_t>(
			ReflectionRuntime::Address(runtime.cameraEyePosition));
		for (std::uint32_t eye = 0; eye < kEyeCount; ++eye) {
			const RE::NiPoint3* position = eyePosition(camera, eye);
			if (!position)
				return false;
			auto* destination = bytes + (eye == 0 ? runtime.accumulatorEye0Offset : runtime.accumulatorEye1Offset);
			std::memset(destination, 0, 16);
			std::memcpy(destination, position, sizeof(RE::NiPoint3));
		}
		return true;
	}

	void RestoreAccumulatorUnsafe(const ReflectionRuntime::Contract& runtime) noexcept
	{
		if (!g_accumulatorBackup.armed)
			return;
		void* accumulator = GetAccumulator(runtime);
		if (accumulator) {
			auto* bytes = static_cast<std::byte*>(accumulator);
			SetAccumulatorCamera(
				runtime, accumulator, static_cast<RE::NiCamera*>(g_accumulatorBackup.camera));
			*reinterpret_cast<void**>(bytes + runtime.accumulatorActiveShadowSceneNodeOffset) =
				g_accumulatorBackup.shadowSceneNode;
			*reinterpret_cast<std::uint32_t*>(bytes + runtime.accumulatorRenderModeOffset) =
				g_accumulatorBackup.renderMode;
			*reinterpret_cast<std::uint8_t*>(bytes + 0xF67C) = g_accumulatorBackup.alphaEnabled;
			*reinterpret_cast<std::uint8_t*>(bytes + 0xF669) = g_accumulatorBackup.depthPrepassEnabled;
			std::memcpy(bytes + runtime.accumulatorEye0Offset, g_accumulatorBackup.eye0.data(), 16);
			std::memcpy(bytes + runtime.accumulatorEye1Offset, g_accumulatorBackup.eye1.data(), 16);
		}
		g_accumulatorBackup = {};
	}

	void RestoreMainCameraUnsafe(const ReflectionRuntime::Contract& runtime) noexcept
	{
		if (!g_mainCameraForRestore)
			return;
		auto setCameraData = reinterpret_cast<SetCameraData_t>(
			ReflectionRuntime::Address(runtime.setCameraData));
		void* state = reinterpret_cast<void*>(ReflectionRuntime::Address(runtime.graphicsState));

		if (state && setCameraData)
			setCameraData(state, g_mainCameraForRestore, false, 0.0f, 1.0f);
		g_mainCameraForRestore = nullptr;
	}

	[[nodiscard]] bool ArmPrivateRasterizerStateUnsafe(ID3D11DeviceContext* context) noexcept
	{
		if (!context || g_privateEntryRasterizerArmed)
			return false;
		g_privateEntryRasterizer.Reset();
		context->RSGetState(g_privateEntryRasterizer.GetAddressOf());

		g_privateEntryRasterizerArmed = true;
		return true;
	}

	bool RestorePrivateStateGuarded() noexcept
	{

		bool restored = NativePassPoolRecovery::Restore();
		const auto* runtime = ReflectionRuntime::Get();
		__try {
			ClearPrivatePassesUnsafe();
			if (g_privatePassesPending)
				restored = false;
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			restored = false;
		}

		if (g_coverageQueryBegan && g_activeCoverageQuery && globals::d3d::context) {
			globals::d3d::context->End(g_activeCoverageQuery);
			g_coverageQueryBegan = false;
		}
		__try {
			ResetFrikCaptureRootsUnsafe();
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			restored = false;
		}
		__try {
			MirrorSceneRenderer::RestorePrivateGoreCaps();
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			restored = false;
		}
		if (g_cameraOverrideArmed) {
			bool componentRestored = false;
			__try {
				PlanarMirrors::EndCameraOverride(g_stereoCamera);
				componentRestored = true;
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				componentRestored = false;
			}
			if (componentRestored)
				g_cameraOverrideArmed = false;
			else
				restored = false;
		}

		for (std::size_t index = 0; index < g_visibilityMutationCount; ++index) {
			auto& mutation = g_visibilityMutations[index];
			if (!mutation.object)
				continue;
			bool componentRestored = false;
			__try {
				NiVirtualDispatch::SetAppCulled(mutation.object, mutation.appCulled);
				componentRestored = true;
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				componentRestored = false;
			}
			if (componentRestored)
				mutation = {};
			else
				restored = false;
		}
		std::size_t retainedMutations = 0;
		for (std::size_t index = 0; index < g_visibilityMutationCount; ++index) {
			if (g_visibilityMutations[index].object)
				g_visibilityMutations[retainedMutations++] = g_visibilityMutations[index];
		}
		for (std::size_t index = retainedMutations; index < g_visibilityMutationCount; ++index)
			g_visibilityMutations[index] = {};
		g_visibilityMutationCount = retainedMutations;

		if (g_accumulatorBackup.armed) {
			bool componentRestored = false;
			if (runtime) {
				__try {
					RestoreAccumulatorUnsafe(*runtime);
					componentRestored = true;
				} __except (EXCEPTION_EXECUTE_HANDLER) {
					componentRestored = false;
				}
			}
			if (!componentRestored)
				restored = false;
		}

		for (std::size_t index = 0; index < g_submittedRootCount; ++index) {
			auto*& root = g_submittedRoots[index];
			if (!root)
				continue;
			bool componentRestored = false;
			__try {
				root->DecRefCount();
				componentRestored = true;
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				componentRestored = false;
			}
			if (componentRestored)
				root = nullptr;
			else
				restored = false;
		}
		std::size_t retainedRoots = 0;
		for (std::size_t index = 0; index < g_submittedRootCount; ++index) {
			if (g_submittedRoots[index])
				g_submittedRoots[retainedRoots++] = g_submittedRoots[index];
		}
		for (std::size_t index = retainedRoots; index < g_submittedRootCount; ++index)
			g_submittedRoots[index] = nullptr;
		g_submittedRootCount = retainedRoots;

		if (g_rendererBackup.armed) {
			bool componentRestored = false;
			if (runtime) {
				__try {
					RestoreRendererUnsafe(*runtime);
					componentRestored = true;
				} __except (EXCEPTION_EXECUTE_HANDLER) {
					componentRestored = false;
				}
			}
			if (!componentRestored)
				restored = false;
		}

		if (g_mainCameraForRestore) {
			bool componentRestored = false;
			if (runtime) {
				__try {
					RestoreMainCameraUnsafe(*runtime);
					componentRestored = true;
				} __except (EXCEPTION_EXECUTE_HANDLER) {
					componentRestored = false;
				}
			}
			if (!componentRestored)
				restored = false;
		}

		if (g_targetBegan) {
			bool componentRestored = false;
			__try {
				if (g_captureKind == VRReflectionRenderer::CaptureKind::kMirror)
					g_mirrorTarget.End(globals::d3d::context);
				componentRestored = true;
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				componentRestored = false;
			}
			if (componentRestored)
				g_targetBegan = false;
			else
				restored = false;
		}

		if (g_privateEntryRasterizerArmed) {
			bool componentRestored = false;
			__try {
				auto* context = globals::d3d::context;
				if (context) {
					context->RSSetState(g_privateEntryRasterizer.Get());
					g_privateEntryRasterizer.Reset();
					componentRestored = true;
				}
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				componentRestored = false;
			}
			if (componentRestored)
				g_privateEntryRasterizerArmed = false;
			else
				restored = false;
		}
		if (!g_targetBegan && !g_privateEntryRasterizerArmed)
			g_captureKind = VRReflectionRenderer::CaptureKind::kNone;
		return restored;
	}

	[[nodiscard]] bool BuildMainStereoSnapshotUnsafe(
		const ReflectionRuntime::Contract& runtime,
		RE::NiCamera* camera) noexcept
	{
		if (!camera)
			return false;
		const auto viewCount = *reinterpret_cast<const std::uint32_t*>(
			reinterpret_cast<const std::byte*>(camera) + 0x208);
		if (viewCount != kEyeCount)
			return false;
		const auto* bases = *reinterpret_cast<const RE::NiMatrix3* const*>(
			reinterpret_cast<const std::byte*>(camera) + 0x1E8);
		if (!bases)
			return false;
		g_mainOriginOffset = {};
		if (auto* playerCamera = RE::PlayerCamera::GetSingleton()) {
			if (auto* root = playerCamera->cameraRoot.get()) {
				g_mainOriginOffset = { root->world.translate.x - camera->world.translate.x,
					root->world.translate.y - camera->world.translate.y,
					root->world.translate.z - camera->world.translate.z };
			}
		}
		alignas(16) VRCameraStateData cameraState{};
		auto build = reinterpret_cast<BuildCameraStateData_t>(
			ReflectionRuntime::Address(runtime.buildCameraStateData));
		void* state = reinterpret_cast<void*>(ReflectionRuntime::Address(runtime.graphicsState));
		build(state, std::addressof(cameraState), camera, false);
		for (std::uint32_t eye = 0; eye < kEyeCount; ++eye) {
			g_mainEye[eye] = {
				cameraState.posAdjust[eye].x + g_mainOriginOffset.x,
				cameraState.posAdjust[eye].y + g_mainOriginOffset.y,
				cameraState.posAdjust[eye].z + g_mainOriginOffset.z,
				1.0f
			};
			g_mainViewProjection[eye] = CopyRowsToMatrix(cameraState.camViewData[eye].viewProjMat);
			g_mainEyeBasis[eye] = bases[eye];
		}
		return true;
	}

	void LogMirrorCameraTransforms(
		const PlanarMirrorMath::Plane& absolutePlane,
		const RE::NiPoint3& absoluteCamera,
		const DirectX::XMFLOAT3& originOffset) noexcept
	{
		static std::uint32_t s_dumps = 0;
		if (s_dumps >= 3u || !g_mainCameraForRestore || !g_stereoCamera)
			return;
		++s_dumps;
		const auto& m = g_mainCameraForRestore->world;
		const auto& r = g_stereoCamera->world;
		const auto viewCount = *reinterpret_cast<const std::uint32_t*>(
			reinterpret_cast<const std::byte*>(g_stereoCamera) + 0x208);
		logger::info(
			"[VRReflections] CAMERA-XFORM plane=({:.5f},{:.5f},{:.5f} d={:.3f}) cameraRoot=({:.3f},{:.3f},{:.3f}) originOffset=({:.3f},{:.3f},{:.3f}) "
			"main.t=({:.3f},{:.3f},{:.3f}) main.rot=[{:.4f},{:.4f},{:.4f} | {:.4f},{:.4f},{:.4f} | {:.4f},{:.4f},{:.4f}] "
			"reflected.t=({:.3f},{:.3f},{:.3f}) reflected.rot=[{:.4f},{:.4f},{:.4f} | {:.4f},{:.4f},{:.4f} | {:.4f},{:.4f},{:.4f}] viewCount={}",
			absolutePlane.normal.x, absolutePlane.normal.y, absolutePlane.normal.z, absolutePlane.distance,
			absoluteCamera.x, absoluteCamera.y, absoluteCamera.z, originOffset.x, originOffset.y, originOffset.z,
			m.translate.x, m.translate.y, m.translate.z,
			m.rotate.entry[0][0], m.rotate.entry[0][1], m.rotate.entry[0][2],
			m.rotate.entry[1][0], m.rotate.entry[1][1], m.rotate.entry[1][2],
			m.rotate.entry[2][0], m.rotate.entry[2][1], m.rotate.entry[2][2],
			r.translate.x, r.translate.y, r.translate.z,
			r.rotate.entry[0][0], r.rotate.entry[0][1], r.rotate.entry[0][2],
			r.rotate.entry[1][0], r.rotate.entry[1][1], r.rotate.entry[1][2],
			r.rotate.entry[2][0], r.rotate.entry[2][1], r.rotate.entry[2][2],
			viewCount);
	}

	[[nodiscard]] bool ReflectStereoEyesUnsafe(RE::NiCamera* camera,
		const PlanarMirrorMath::Plane& absolutePlane, const DirectX::XMFLOAT3& paneUp,
		RE::NiMatrix3& paneBasis) noexcept
	{
		auto* cameraBytes = reinterpret_cast<std::byte*>(camera);
		auto* positions = *reinterpret_cast<RE::NiPoint3**>(cameraBytes + 0x1B8);
		auto* previousPositions = *reinterpret_cast<RE::NiPoint3**>(cameraBytes + 0x1D0);
		auto* bases = *reinterpret_cast<RE::NiMatrix3**>(cameraBytes + 0x1E8);
		if (!positions || !previousPositions || !bases) return false;
		using namespace DirectX;
		const auto normal = XMLoadFloat3(&absolutePlane.normal);
		const auto suppliedUp = XMLoadFloat3(&paneUp);
		const auto orthogonalUp = suppliedUp - normal * XMVector3Dot(suppliedUp, normal);
		const float upLength = XMVectorGetX(XMVector3LengthSq(orthogonalUp));
		if (!std::isfinite(upLength) || upLength < 1.e-6f) return false;
		const auto up = XMVector3Normalize(orthogonalUp);
		const XMFLOAT3 midpoint{ (g_mainEye[0].x + g_mainEye[1].x) * 0.5f,
			(g_mainEye[0].y + g_mainEye[1].y) * 0.5f, (g_mainEye[0].z + g_mainEye[1].z) * 0.5f };
		const float side = PlanarMirrorMath::SignedDistance(absolutePlane, midpoint);
		if (!std::isfinite(side) || std::fabs(side) < 0.01f) return false;
		const auto forward = normal * (side >= 0.0f ? 1.0f : -1.0f);
		const auto right = XMVector3Cross(up, forward);

		XMFLOAT3 directions[3]{};
		XMStoreFloat3(&directions[0], forward);
		XMStoreFloat3(&directions[1], up);
		XMStoreFloat3(&directions[2], right);
		for (std::uint32_t axis = 0; axis < 3u; ++axis)
			paneBasis.entry[axis] = { directions[axis].x, directions[axis].y, directions[axis].z, 0.0f };
		for (std::uint32_t eye = 0; eye < kEyeCount; ++eye) {
			const XMFLOAT3 source{ g_mainEye[eye].x, g_mainEye[eye].y, g_mainEye[eye].z };
			
			if (PlanarMirrorMath::SignedDistance(absolutePlane, source) * (side >= 0.0f ? 1.0f : -1.0f) <= 0.01f)
				return false;
			const auto reflected = PlanarMirrorMath::ReflectPoint(absolutePlane, source);
			positions[eye] = previousPositions[eye] = { reflected.x, reflected.y, reflected.z };
			bases[eye] = paneBasis;
		}
		return true;
	}

	[[nodiscard]] bool PrepareMirrorCameraUnsafe(
		const MirrorSceneRenderer::VRMirrorCaptureRequest& request) noexcept
	{
		g_mainCameraForRestore = RE::Main::WorldRootCamera();
		g_captureStage = "camera";
		if (!g_mainCameraForRestore || !g_stereoCamera)
			return false;
		PlanarMirrorMath::Plane absolutePlane{
			{ request.plane[0], request.plane[1], request.plane[2] }, request.plane[3] };
		if (!PlanarMirrorMath::NormalizePlane(absolutePlane))
			return false;

		const auto originOffset = g_mainOriginOffset;
		const RE::NiPoint3 absoluteCamera{
			g_mainCameraForRestore->world.translate.x + originOffset.x,
			g_mainCameraForRestore->world.translate.y + originOffset.y,
			g_mainCameraForRestore->world.translate.z + originOffset.z };
		PlanarMirrorMath::Plane sourcePlane = absolutePlane;
		sourcePlane.distance -= sourcePlane.normal.x * originOffset.x +
			sourcePlane.normal.y * originOffset.y + sourcePlane.normal.z * originOffset.z;
		if (!PlanarMirrors::ReflectCamera(g_mainCameraForRestore, sourcePlane, g_stereoCamera))
			return false;

		g_stereoCamera->local.rotate.entry[1] *= -1.0f;
		g_stereoCamera->local.translate.x += originOffset.x;
		g_stereoCamera->local.translate.y += originOffset.y;
		g_stereoCamera->local.translate.z += originOffset.z;
		RE::NiUpdateData update{};
		NiVirtualDispatch::UpdateWorldData(g_stereoCamera, std::addressof(update));

		RE::NiMatrix3 paneBasis{};
		if (!ReflectStereoEyesUnsafe(g_stereoCamera, absolutePlane,
			{ request.route.bitangent[0], request.route.bitangent[1], request.route.bitangent[2] }, paneBasis))
			return false;
		g_stereoCamera->local.rotate = g_stereoCamera->world.rotate = paneBasis;
		LogMirrorCameraTransforms(absolutePlane, absoluteCamera, originOffset);
		const auto& pane = request.route;
		g_captureStage = "pane-frustum";
		if (!PlanarMirrors::FitCaptureToPane(g_stereoCamera,
				{ pane.center[0], pane.center[1], pane.center[2] },
				{ pane.tangent[0], pane.tangent[1], pane.tangent[2] },
				{ pane.bitangent[0], pane.bitangent[1], pane.bitangent[2] },
				pane.halfWidth, pane.halfHeight))
			return false;
		if (!PlanarMirrors::BeginCameraOverride(g_stereoCamera, absolutePlane, 1.5f))
			return false;
		g_cameraOverrideArmed = true;
		return true;
	}

	[[nodiscard]] bool ApplyMirrorCameraStateUnsafe(
		const ReflectionRuntime::Contract& runtime, RE::NiCamera* camera) noexcept
	{
		if (!runtime.vr || !camera || !g_cameraOverrideArmed)
			return false;
		auto* state = reinterpret_cast<std::byte*>(ReflectionRuntime::Address(runtime.graphicsState));
		auto find = reinterpret_cast<FindCameraStateData_t>(ReflectionRuntime::Address(kFindCameraStateData));
		auto build = reinterpret_cast<BuildCameraStateData_t>(ReflectionRuntime::Address(runtime.buildCameraStateData));
		auto apply = reinterpret_cast<ApplyCameraStateData_t>(ReflectionRuntime::Address(kApplyCameraStateData));
		if (!state || !find || !build || !apply)
			return false;

		const auto* cached = find(state, camera, false);
		const void* seed = cached ? static_cast<const void*>(cached) : state + 0x160;
		alignas(16) VRCameraStateData reflected{};
		std::memcpy(&reflected, seed, sizeof(reflected));
		build(state, &reflected, camera, false);  
		if (reflected.referenceCamera != camera || reflected.useJitter)
			return false;
		for (std::uint32_t eye = 0; eye < kEyeCount; ++eye) {
			PlanarMirrors::PatchedCameraState patched{};
			if (!PlanarMirrors::GetPatchedCameraStateForEye(camera, eye, patched) || !patched.obliqueApplied)
				return false;
		}
		
		apply(state, &reflected, reinterpret_cast<const std::byte*>(camera) + 0x214, 0.0f, 1.0f);
		return true;
	}

	void RenderMaterialGeometryUnsafe(void* accumulator, void* renderContext) noexcept
	{
		const auto group = reinterpret_cast<RenderGeometryGroup_t>(ReflectionRuntime::Address(kRenderGeometryGroup));

		reinterpret_cast<RenderSortedGeometry_t>(ReflectionRuntime::Address(kRenderAlphaTestedGeometry))(
			accumulator, renderContext);
		group(accumulator, 2, renderContext, false);
		group(accumulator, 1, renderContext, false);
	}

	void RenderForwardGeometryUnsafe(void* accumulator, void* renderContext) noexcept
	{
		const auto group = reinterpret_cast<RenderGeometryGroup_t>(ReflectionRuntime::Address(kRenderGeometryGroup));
		const auto materialDepth = reinterpret_cast<SetMaterialDepthMode_t>(ReflectionRuntime::Address(kSetMaterialDepthMode));
		auto* renderer = g_rendererBackup.renderer;

		constexpr std::uint8_t geometryGroups[]{ 34, 36, 11, 10, 3, 4, 17, 18 };
		for (const auto index : geometryGroups)
			group(accumulator, index, renderContext, false);
		VRMirrorRenderState::Set(renderer, VRMirrorRenderState::kWriteMode, 0, 0x10);
		group(accumulator, 15, renderContext, false);
		VRMirrorRenderState::Set(renderer, VRMirrorRenderState::kWriteMode, 1, 0x10);
		group(accumulator, 16, renderContext, false);
		VRMirrorRenderState::Set(renderer, VRMirrorRenderState::kBlend, 1, 0x10);
		VRMirrorRenderState::Set(renderer, VRMirrorRenderState::kDepth, 1, 4);
		materialDepth(1);
		group(accumulator, 9, renderContext, false);
		materialDepth(3);
		VRMirrorRenderState::Set(renderer, VRMirrorRenderState::kDepth, 3, 4);
		VRMirrorRenderState::Set(renderer, VRMirrorRenderState::kBlend, 0, 0x10);
		reinterpret_cast<RenderSortedGeometry_t>(ReflectionRuntime::Address(kRenderSortedGeometry))(
			accumulator, renderContext);
		VRMirrorRenderState::Set(renderer, VRMirrorRenderState::kBlend, 0, 0x10);
		group(accumulator, 23, renderContext, true);

	}

	[[nodiscard]] bool ExecutePrivateRenderUnsafe(
		const ReflectionRuntime::Contract& runtime,
		RE::NiCamera* camera,
		void* renderContext,
		RE::NiAVObject* exclusionRoot, const RE::NiPoint3& paneCenter) noexcept
	{
		if (!camera || !renderContext)
			return false;
		auto* context = globals::d3d::context;
		g_captureStage = "player-visibility";
		if (!ArmPrivateRasterizerStateUnsafe(context) || !PrepareVisibilityUnsafe(exclusionRoot))
			return false;

		std::memset(renderContext, 0, runtime.renderContextSize);
		auto initializeContext = reinterpret_cast<InitializeRenderContext_t>(
			ReflectionRuntime::Address(runtime.initializeRenderContext));
		initializeContext(renderContext, camera, GetAccumulator(runtime));
		const float clear[4]{ 0.0f, 0.0f, 0.0f, 1.0f };
		if (g_captureKind == VRReflectionRenderer::CaptureKind::kMirror) {
			LogPrivateTargetRasterState(context, g_mirrorTarget, true);
			g_targetBegan = g_mirrorTarget.BeginMaterialCapture(context, clear);
			if (g_targetBegan) {
				g_materialResources.Clear(context);
				g_mirrorTarget.BindMaterialGBuffer(context);
				g_materialPhase = true;
			}
		}
		g_captureStage = "material-target";
		if (!g_targetBegan || !ArmAccumulatorUnsafe(runtime, camera) || !ArmRendererUnsafe(runtime))
			return false;

		auto flush = reinterpret_cast<RendererFlush_t>(
			ReflectionRuntime::Address(runtime.rendererFlush));
		flush();
		VRReflectionRenderer::BindTargetAfterSetDirty();
		
		if (!g_activeCoverageQuery || !ApplyMirrorCameraStateUnsafe(runtime, camera))
			return false;
		g_captureStage = "stereo-matrices";
		g_materialConstants = {};
		g_materialConstants.extentAndLights = { g_mirrorTarget.PerEyeWidth(), g_mirrorTarget.Height(), 0, 0 };
		for (std::uint32_t eye = 0; eye < kEyeCount; ++eye) {
			PlanarMirrors::PatchedCameraState patched{};
			if (!PlanarMirrors::GetPatchedCameraStateForEye(camera, eye, patched)) return false;
			DirectX::XMVECTOR determinant{};
			const auto inverse = DirectX::XMMatrixInverse(&determinant, DirectX::XMLoadFloat4x4(&patched.viewProjection));
			if (!std::isfinite(DirectX::XMVectorGetX(determinant)) ||
				std::fabs(DirectX::XMVectorGetX(determinant)) < 1.e-12f) return false;
			DirectX::XMStoreFloat4x4(&g_materialConstants.inverseViewProjection[eye], inverse);
			g_materialConstants.eyeOrigin[eye] = { patched.cameraOrigin.x, patched.cameraOrigin.y, patched.cameraOrigin.z, 1 };
			g_captureFrusta[eye].Set(patched.viewProjection.m, patched.cameraOrigin.x, patched.cameraOrigin.y, patched.cameraOrigin.z);
			if (eye == 0) {
				const auto normalToWorld = DirectX::XMMatrixInverse(&determinant, DirectX::XMLoadFloat4x4(&patched.view));
				if (!std::isfinite(DirectX::XMVectorGetX(determinant)) ||
					std::fabs(DirectX::XMVectorGetX(determinant)) < 1.e-12f) return false;
				DirectX::XMStoreFloat4x4(&g_materialConstants.normalToWorld, normalToWorld);
			}
		}
		g_captureStage = "lights";
		if (!SnapshotMirrorLightingUnsafe(runtime, renderContext, paneCenter)) return false;

		auto reset = reinterpret_cast<GroupReset_t>(
			ReflectionRuntime::Address(runtime.cullingGroupReset));
		auto startAdding = reinterpret_cast<GroupStartAdding_t>(
			ReflectionRuntime::Address(runtime.cullingGroupStartAdding));
		auto process = reinterpret_cast<GroupProcess_t>(
			ReflectionRuntime::Address(runtime.cullingGroupProcess));
		auto accumulate = reinterpret_cast<GroupAccumulate_t>(
			ReflectionRuntime::Address(runtime.cullingGroupAccumulate));
		reset(g_cullingGroup.data());

		g_cullingGroup[0x17A] = std::byte{ 1 };
		startAdding(g_cullingGroup.data(), camera);
		g_captureStage = "scene-submission";
		if (!SubmitSceneRootsUnsafe(runtime))
			return false;
		process(g_cullingGroup.data(), false);

		g_privatePassesPending = true;
		g_privateRenderContext = renderContext;
		accumulate(g_cullingGroup.data(), GetAccumulator(runtime));

		if (!ApplyMirrorCameraStateUnsafe(runtime, camera)) return false;
		context->Begin(g_activeCoverageQuery);
		g_coverageQueryBegan = true;
		RenderMaterialGeometryUnsafe(GetAccumulator(runtime), renderContext);
		context->End(g_activeCoverageQuery);
		g_coverageQueryBegan = false;

		reinterpret_cast<RenderSortedGeometry_t>(ReflectionRuntime::Address(kRenderAlphaBlendedGeometry))(
			GetAccumulator(runtime), renderContext);
		reinterpret_cast<ClearAccumulator_t>(ReflectionRuntime::Address(kFlushRenderContext))(renderContext);
		g_captureStage = "lighting-resolve";
		if (!g_materialResources.Resolve(context, g_materialResolve.Get(), g_materialConstants,
			g_mirrorTarget.MaterialDiffuseSRV(), g_mirrorTarget.MaterialNormalSRV(), g_mirrorTarget.MaterialPropertiesSRV(),
			g_mirrorTarget.DepthSRV(), g_mirrorTarget.ColorUAV())) return false;
		g_materialPhase = false;
		VRMirrorRenderState::Begin(g_rendererBackup.renderer);
		reinterpret_cast<SetMaterialDepthMode_t>(ReflectionRuntime::Address(kSetMaterialDepthMode))(3);
		VRReflectionRenderer::BindTargetAfterSetDirty();
		RenderForwardGeometryUnsafe(GetAccumulator(runtime), renderContext);
		ClearPrivatePassesUnsafe();
		static std::uint32_t proofLogs = 0;
		if (proofLogs++ < 12u) {
			logger::info(
				"[VRReflections] native material capture resolved: kind={} camera={} accumulator={} context={} roots={} target={}x{} mode=25 freshStereoCamera=true activeSSN={}",
				static_cast<std::uint32_t>(g_captureKind), static_cast<void*>(camera),
				GetAccumulator(runtime), renderContext, g_submittedRootCount,
				g_mirrorTarget.Width(), g_mirrorTarget.Height(),
				static_cast<void*>(ActiveShadowSceneNodeUnsafe(runtime)));
		}
		return true;
	}

	[[nodiscard]] bool CaptureMirrorNativeGuarded(
		const MirrorSceneRenderer::VRMirrorCaptureRequest& request) noexcept
	{
		g_captureStage = "native-pools";
		if (!NativePassPoolRecovery::PoolsIdle())
			return false;
		bool rendered = false;
		bool nativeFault = false;
		__try {
			const auto* runtime = ReflectionRuntime::Get();
			g_captureKind = VRReflectionRenderer::CaptureKind::kMirror;
			if (runtime && EnsureNativeObjectsUnsafe(*runtime) && PrepareMirrorCameraUnsafe(request)) {
				rendered = ExecutePrivateRenderUnsafe(
					*runtime, g_stereoCamera, g_mirrorRenderContext.data(), request.exclusionRoot,
					{ request.route.center[0], request.route.center[1], request.route.center[2] });
				if (rendered) {
					auto eyePosition = reinterpret_cast<CameraEyePosition_t>(
						ReflectionRuntime::Address(runtime->cameraEyePosition));
					for (std::uint32_t eye = 0; eye < kEyeCount && rendered; ++eye) {
						PlanarMirrors::PatchedCameraState patched{};
						rendered = PlanarMirrors::GetPatchedCameraStateForEye(
							g_stereoCamera, eye, patched) && patched.obliqueApplied;
						g_mirrorViewProjection[eye] = patched.viewProjection;
						const auto* position = eyePosition(g_stereoCamera, eye);
						if (!position) {
							rendered = false;
						} else {
							g_reflectedEye[eye] = { position->x, position->y, position->z, 1.0f };
						}
					}
				}
			}
			if (!RestorePrivateStateGuarded()) {
				g_captureStage = "restore";
				rendered = false;
				nativeFault = true;
			}
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			rendered = false;
			nativeFault = true;
			(void)RestorePrivateStateGuarded();
		}
		if (nativeFault)
			g_faulted.store(true, std::memory_order_release);
		if (!rendered) {
			static auto lastLog = std::chrono::steady_clock::time_point{};
			static std::uint32_t rejected = 0;
			++rejected;
			const auto now = std::chrono::steady_clock::now();
			if (now - lastLog >= std::chrono::seconds(5)) {
				logger::warn("[VRReflections] capture rejected: receiver={:08X} stage={} count={} fault={}",
					request.receiverFormID, g_captureStage, rejected, nativeFault);
				lastLog = now;
				rejected = 0;
			}
		}
		return rendered;
	}

	struct CullingBatchQEnabledHook
	{
		static bool thunk()
		{
			if (g_captureKind != VRReflectionRenderer::CaptureKind::kNone)
				return false;
			return func();
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct AccumulationMTAQEnabledHook
	{
		static bool thunk()
		{
			if (g_captureKind != VRReflectionRenderer::CaptureKind::kNone)
				return false;
			return func();
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	[[nodiscard]] bool RefreshMainStereoSnapshotGuarded() noexcept
	{
		bool valid = false;
		__try {
			const auto* runtime = ReflectionRuntime::Get();
			auto* camera = RE::Main::WorldRootCamera();
			valid = runtime && camera && BuildMainStereoSnapshotUnsafe(*runtime, camera);
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			valid = false;
		}
		return valid;
	}

	void ServiceMirrorDumpTrigger(ID3D11Device* device, ID3D11DeviceContext* context,
		const StereoMirrorImage& publication) noexcept
	{
		static std::uint32_t s_frame = 0;
		if ((++s_frame % 90u) != 0u)
			return;
		try {
			namespace fs = std::filesystem;
			const fs::path trigger{ L"Data\\dynref_vrdump" };
			std::error_code ec;
			if (!fs::exists(trigger, ec))
				return;
			const fs::path directory{ L"Data\\DynRefDump" };
			fs::create_directories(directory, ec);
			HRESULT mirrorResult = E_FAIL;
			HRESULT mainResult = E_FAIL;
			if (auto* mirrorTexture = publication.texture.Get())
				mirrorResult = Util::SaveTextureToFile(device, context, directory / L"vr_mirror_color.dds", mirrorTexture);
			if (g_mainColorRTV) {
				ComPtr<ID3D11Resource> resource;
				g_mainColorRTV->GetResource(resource.GetAddressOf());
				ComPtr<ID3D11Texture2D> mainTexture;
				if (resource && SUCCEEDED(resource.As(&mainTexture)) && mainTexture)
					mainResult = Util::SaveTextureToFile(device, context, directory / L"vr_main_color.dds", mainTexture.Get());
			}
			fs::rename(trigger, fs::path{ L"Data\\dynref_vrdump.done" }, ec);
			logger::info(
				"[VRReflections] dynref_vrdump: mirror={:#x} ({}x{} eyes={}) main={:#x} ({}x{} fmt={}) mirrorExposure={:.3f}",
				static_cast<std::uint32_t>(mirrorResult), g_mirrorTarget.Width(), g_mirrorTarget.Height(), g_mirrorTarget.EyeCount(),
				static_cast<std::uint32_t>(mainResult), g_mainColorDesc.Width, g_mainColorDesc.Height,
				static_cast<std::uint32_t>(g_mainColorDesc.Format), 1.0f);
		} catch (...) {
			
		}
	}

	bool SameMirrorReceiver(
		const MirrorSceneRenderer::VRMirrorCaptureRequest& a,
		const MirrorSceneRenderer::VRMirrorCaptureRequest& b) noexcept
	{
		if (a.receiverFormID == 0u || a.receiverFormID != b.receiverFormID || a.cellFormID != b.cellFormID ||
			a.loadGeneration != b.loadGeneration)
			return false;
		for (std::size_t i = 0; i < 3u; ++i) {
			if (std::fabs(a.route.center[i] - b.route.center[i]) > 0.01f ||
				std::fabs(a.route.normal[i] - b.route.normal[i]) > 0.001f ||
				std::fabs(a.route.tangent[i] - b.route.tangent[i]) > 0.001f ||
				std::fabs(a.route.bitangent[i] - b.route.bitangent[i]) > 0.001f)
				return false;
		}
		return a.route.halfWidth == b.route.halfWidth && a.route.halfHeight == b.route.halfHeight &&
			a.route.ellipseMask == b.route.ellipseMask &&
			a.route.firstVertex == b.route.firstVertex && a.route.vertexCount == b.route.vertexCount &&
			a.route.authoredContour == b.route.authoredContour;
	}

	std::uint64_t g_lastServiceMicros = 0;
	double g_serviceFrameMicros = 16667.0;
	constexpr double kSlowFrameMicros = 60000.0;
	constexpr std::uint64_t kPauseMicros = 200000u;

	void ObserveServiceFrame(std::uint64_t now) noexcept
	{
		if (g_lastServiceMicros && now > g_lastServiceMicros) {
			const auto delta = now - g_lastServiceMicros;
			if (delta < kPauseMicros)
				g_serviceFrameMicros = g_serviceFrameMicros * 0.9 + static_cast<double>(delta) * 0.1;
		}
		g_lastServiceMicros = now;
	}

	[[nodiscard]] std::uint64_t EffectivePublicationLifetime() noexcept
	{
		const auto configured = MirrorSettings::PublicationLifetime();
		if (g_serviceFrameMicros < kSlowFrameMicros) return configured;
		const auto frameBased = static_cast<std::uint64_t>(g_serviceFrameMicros * 6.0);
		return (std::min)((std::max)(configured, frameBased), std::uint64_t{ 1500000u });
	}

	PendingMirrorImage* PrepareMirrorSlot(MirrorSlot& slot, ID3D11Device* device) noexcept
	{
		if (!g_mirrorTarget.ColorTexture()) return nullptr;
		PendingMirrorImage* available = nullptr;
		for (auto& frame : slot.frames) {
			if (!frame.pending) { available = &frame; break; }
		}
		if (!available) return nullptr;
		auto& back = available->image;
		D3D11_TEXTURE2D_DESC expected{}, actual{};
		g_mirrorTarget.ColorTexture()->GetDesc(&expected);
		if (back.texture) back.texture->GetDesc(&actual);
		if (!back.srv || actual.Width != expected.Width || actual.Height != expected.Height ||
			actual.Format != expected.Format) {
			StereoMirrorImage replacement{};
			expected.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			expected.Usage = D3D11_USAGE_DEFAULT;
			expected.CPUAccessFlags = expected.MiscFlags = 0u;
			if (FAILED(device->CreateTexture2D(&expected, nullptr, replacement.texture.GetAddressOf())) ||
				FAILED(device->CreateShaderResourceView(replacement.texture.Get(), nullptr, replacement.srv.GetAddressOf())))
				return nullptr;
			back = std::move(replacement);
		}
		if (!available->coverage) {
			const D3D11_QUERY_DESC description{ D3D11_QUERY_OCCLUSION, 0u };
			if (FAILED(device->CreateQuery(&description, available->coverage.GetAddressOf()))) return nullptr;
		}
		return back.texture.Get() != slot.front.texture.Get() &&
			back.texture.Get() != g_mirrorTarget.ColorTexture() ? available : nullptr;
	}

	void PollMirrorSlot(MirrorSlot& slot, const MirrorSceneRenderer::VRMirrorCaptureRequest& current,
		std::uint64_t now) noexcept
	{
		const auto lifetime = EffectivePublicationLifetime();
		PendingMirrorImage* newest = nullptr;
		for (auto& frame : slot.frames) {
			if (!frame.pending) continue;
			std::uint64_t samples = 0;
			const auto result = globals::d3d::context->GetData(
				frame.coverage.Get(), &samples, sizeof(samples), D3D11_ASYNC_GETDATA_DONOTFLUSH);
			if (result == S_FALSE) continue;
			frame.pending = false;
			if (result == S_OK && samples != 0u && frame.serial > slot.publishedSerial &&
				SameMirrorReceiver(frame.image.request, current) &&
				now >= frame.capturedAt && now - frame.capturedAt <= lifetime &&
				(!newest || frame.serial > newest->serial)) newest = &frame;
		}
		if (newest) {
			std::swap(slot.front, newest->image);
			slot.publishedSerial = newest->serial;
			slot.capturedAt = newest->capturedAt;
			slot.ready = true;
			MirrorPerformance::Published(0u);
		}
		
		if (slot.ready && (!SameMirrorReceiver(slot.front.request, current) || now < slot.capturedAt ||
			now - slot.capturedAt > lifetime)) slot.ready = false;
	}

	void CompositeMirror(const StereoMirrorImage& publication) noexcept
	{
		auto* device = globals::d3d::device;
		auto* context = globals::d3d::context;
		if (!publication.srv || !device || !context || !g_mainColorRTV || !g_mainDepthSnapshotSRV ||
			g_mirrorTarget.Bound() ||
			!EnsureOverlayResources(device))
			return;
		ServiceMirrorDumpTrigger(device, context, publication);
		const auto& route = publication.request.route;
		if (!route.classifierValid || route.vertexCount < 3u) return;
		ID3D11ShaderResourceView* vertexResource = g_overlayVertexSRV.Get();
		if (route.authoredContour) {
			vertexResource = MirrorAuthoring::Vertices(route.authoredContour, route.vertexCount, device);
			if (!vertexResource || route.firstVertex != 0) return;
		} else if (route.firstVertex >= kPresentationVertexCount ||
			route.vertexCount > kPresentationVertexCount - route.firstVertex) return;

		D3D11_RENDER_TARGET_VIEW_DESC outputViewDesc{};
		g_mainColorRTV->GetDesc(std::addressof(outputViewDesc));
		const bool linearHDR = outputViewDesc.Format == DXGI_FORMAT_R11G11B10_FLOAT ||
			outputViewDesc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT ||
			outputViewDesc.Format == DXGI_FORMAT_R32G32B32A32_FLOAT;
		StereoMirrorData constants{};
		constants.mainDepthRange = g_mainDepthRange;
		constants.mainDepthRange.z = static_cast<float>(route.firstVertex);  
		constants.screen = {
			1.0f / static_cast<float>(g_mainColorDesc.Width),
			1.0f / static_cast<float>(g_mainColorDesc.Height),
			static_cast<float>(g_mainColorDesc.Width),
			static_cast<float>(g_mainColorDesc.Height)
		};
		constants.mirrorCenter = { route.center[0], route.center[1], route.center[2], 4.0f };
		constants.mirrorNormal = {
			route.normal[0], route.normal[1], route.normal[2], route.halfThickness };
		constants.mirrorTangent = {
			route.tangent[0], route.tangent[1], route.tangent[2], route.halfWidth };
		constants.mirrorBitangent = {
			route.bitangent[0], route.bitangent[1], route.bitangent[2], route.halfHeight };
		constants.outputEncoding = {
			linearHDR ? -1.0f : (IsSRGBFormat(outputViewDesc.Format) ? 0.0f : 1.0f),
			1.0f,  
			route.ellipseMask,
			route.selfDepthMargin
		};
		for (std::uint32_t eye = 0; eye < kEyeCount; ++eye) {
			constants.mainEye[eye] = g_mainEye[eye];
			constants.mainViewProjection[eye] = g_mainViewProjection[eye];
			constants.reflectedEye[eye] = publication.eye[eye];
			constants.reflectedViewProjection[eye] = publication.viewProjection[eye];
		}
		context->UpdateSubresource(g_overlayCB.Get(), 0, nullptr, std::addressof(constants), 0, 0);

		ScopedPresentationState savedState(context);
		ID3D11Buffer* constantBuffer = g_overlayCB.Get();
		ID3D11ShaderResourceView* pixelResources[2]{
			g_mainDepthSnapshotSRV.Get(), publication.srv.Get() };
		ID3D11SamplerState* sampler = g_overlaySampler.Get();
		context->VSSetShader(g_overlayVS.Get(), nullptr, 0);
		context->PSSetShader(g_overlayPS.Get(), nullptr, 0);
		context->GSSetShader(nullptr, nullptr, 0);
		context->HSSetShader(nullptr, nullptr, 0);
		context->DSSetShader(nullptr, nullptr, 0);
		context->VSSetConstantBuffers(0, 1, std::addressof(constantBuffer));
		context->PSSetConstantBuffers(0, 1, std::addressof(constantBuffer));
		context->VSSetShaderResources(0, 1, std::addressof(vertexResource));
		context->PSSetShaderResources(0, 2, pixelResources);
		context->PSSetSamplers(0, 1, std::addressof(sampler));
		context->RSSetState(g_overlayRasterizer.Get());
		D3D11_VIEWPORT viewport{};
		viewport.Width = static_cast<float>(g_mainColorDesc.Width);
		viewport.Height = static_cast<float>(g_mainColorDesc.Height);
		viewport.MinDepth = 0.0f;
		viewport.MaxDepth = 1.0f;
		context->RSSetViewports(1, std::addressof(viewport));
		context->IASetInputLayout(nullptr);
		context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		context->OMSetBlendState(g_overlayBlend.Get(), nullptr, 0xFFFFFFFFu);
		context->OMSetDepthStencilState(g_overlayDepthStencil.Get(), 0);
		ID3D11RenderTargetView* output = g_mainColorRTV.Get();
		context->OMSetRenderTargets(1, std::addressof(output), nullptr);

		context->DrawInstanced(route.vertexCount, kEyeCount, 0, 0);
	}

	using GroupCleanup_t = void (*)(void*, bool, bool);
	using GroupArenaDtor_t = void (*)(void*);

	void ReleaseNativeObjectsUnsafe() noexcept
	{
		VRMirrorAvatar::Reset();
		const auto* runtime = ReflectionRuntime::Get();
		if (runtime && g_cullingGroupConstructed) {
			auto cleanup = reinterpret_cast<GroupCleanup_t>(
				ReflectionRuntime::Address(runtime->cullingGroupCleanup));
			auto arenaDtor = reinterpret_cast<GroupArenaDtor_t>(
				ReflectionRuntime::Address(runtime->cullingGroupArenaDtor));
			auto subgroupDtor = reinterpret_cast<GroupArenaDtor_t>(
				ReflectionRuntime::Address(runtime->cullingGroupSubgroupArenaDtor));
			cleanup(g_cullingGroup.data(), true, true);
			auto* group = g_cullingGroup.data();
			arenaDtor(group + 0x128);
			arenaDtor(group + 0x0E8);
			arenaDtor(group + 0x0A8);
			subgroupDtor(group + 0x070);
		}
		std::memset(g_cullingGroup.data(), 0, g_cullingGroup.size());
		std::memset(g_mirrorRenderContext.data(), 0, g_mirrorRenderContext.size());
		g_cullingGroupConstructed = false;
		if (g_stereoCamera) {
			g_stereoCamera->DecRefCount();
			g_stereoCamera = nullptr;
		}
		if (g_cubeOwner) {
			g_cubeOwner->DecRefCount();
			g_cubeOwner = nullptr;
		}
	}

	bool ReleaseNativeObjectsGuarded() noexcept
	{
		__try {
			ReleaseNativeObjectsUnsafe();
			return true;
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			g_cullingGroupConstructed = false;
			g_stereoCamera = nullptr;
			g_cubeOwner = nullptr;
			std::memset(g_cullingGroup.data(), 0, g_cullingGroup.size());
			return false;
		}
	}

	void ResetForWorldChange() noexcept
	{
		g_mirrorReady = false;
		if (RestorePrivateStateGuarded())
			ReleaseMirrorImages();
		(void)ReleaseNativeObjectsGuarded();
	}
}

namespace VRReflectionRenderer
{
	void Install() noexcept
	{
		if (!REL::Module::IsVR() || g_installed.load(std::memory_order_acquire) ||
			stl::hookInstallationFailed.load(std::memory_order_acquire))
			return;
		const auto* runtime = ReflectionRuntime::Get();
		if (!runtime || !runtime->vr) {
			g_installed.store(false, std::memory_order_release);
			return;
		}
		try {
			if (!VerifyPrivateEntries()) {
				g_faulted.store(true, std::memory_order_release);
				return;
			}
			if (!NativePassPoolRecovery::Install(PrivateCaptureActive)) {
				g_faulted.store(true, std::memory_order_release);
				return;
			}
			if (!stl::detour_thunk<CullingBatchQEnabledHook>(
				ReflectionRuntime::Address(runtime->cullingBatchQEnabled)))
				return;
			if (!stl::detour_thunk<AccumulationMTAQEnabledHook>(
				ReflectionRuntime::Address(runtime->accumulationMTAQEnabled)))
				return;
			cs::engine::RegisterPostRenderPreUI(CapturePostRenderPreUI);
			if (stl::hookInstallationFailed.load(std::memory_order_acquire))
				return;
			g_installed.store(true, std::memory_order_release);
			logger::info(
				"[VRReflections] installed native stereo producer: NiCamera(2), fresh reflected cameras, DFPrepass mode 25 with independent depth writes, private avatar/complete pose, five-MRT lighting resolve, linear HDR; biggest mirror only, MCM quality and fixed/every-frame refresh");
		} catch (...) {
			g_faulted.store(true, std::memory_order_release);
			logger::error("[VRReflections] install failed; native VR producer disabled");
		}
	}

	void CapturePostRenderPreUI() noexcept
	{
		if (!g_installed.load(std::memory_order_acquire) ||
			stl::hookInstallationFailed.load(std::memory_order_acquire))
			return;
		if (!REL::Module::IsVR() || MirrorSceneRenderer::ShutdownRequested())
			return;
		Hooks::ServiceMirrorToggle();
		MirrorPerformance::FrameRendered(!MirrorSceneRenderer::LoadBlocked() && !MirrorSceneRenderer::InteractiveMenuOpen());
		MirrorPerformance::Sample serviceTiming(MirrorPerformance::Stage::VRService);
		static std::uint32_t seenLoadGeneration = (std::numeric_limits<std::uint32_t>::max)();
		const std::uint32_t loadGeneration = MirrorSceneRenderer::LoadGeneration();
		if (loadGeneration != seenLoadGeneration) {
			seenLoadGeneration = loadGeneration;
			ResetForWorldChange();
		}

		if (!MirrorSceneRenderer::ServiceVRReflectionLoadGate())
			return;
		auto* device = globals::d3d::device;
		auto* context = globals::d3d::context;
		if (!device || !context)
			return;
		MirrorPerformance::Tick();
		if (g_deviceIdentity != device) {
			ReleaseDeviceResources(context);
			(void)ReleaseNativeObjectsGuarded();
			g_deviceIdentity = device;
		}
		static bool enabledStateInitialized = false;
		static bool seenEnabled = false;
		const bool enabled = MirrorSceneRenderer::Enabled();
		if (!enabledStateInitialized || enabled != seenEnabled) {
			enabledStateInitialized = true;
			seenEnabled = enabled;
			const bool restored = RestorePrivateStateGuarded();
			MirrorSceneRenderer::ResetVRReflectionState();
			if (!restored) {
				logger::error("[VRReflections] {} edge cleanup incomplete; producer remains faulted",
					enabled ? "ON" : "OFF");
				return;
			}
			ReleaseDeviceResources(context);
			(void)ReleaseNativeObjectsGuarded();
			g_deviceIdentity = device;
			if (enabled)
				g_faulted.store(false, std::memory_order_release);
			logger::info("[VRReflections] {} edge: private mirror state reset", enabled ? "ON" : "OFF");
		}
		if (!enabled)
			return;
		if (g_faulted.load(std::memory_order_acquire)) {

			(void)RestorePrivateStateGuarded();
			return;
		}

		g_mirrorReady = false;
		if (!RefreshMainStereoSnapshotGuarded()) {
			return;
		}
		MirrorSceneRenderer::VRMirrorCaptureRequest request{};
		if (!MirrorSceneRenderer::PrepareVRMirrorCapture(request)) {

			return;
		}
		auto& slot = g_mirrorSlot;
		if (!SameMirrorReceiver(slot.owner, request)) {
			slot = {};
			slot.owner = request;
			slot.owner.exclusionRoot = nullptr;
		}
		if (!CaptureMainTargets(device, context)) return;

		if (!SameMirrorReceiver(slot.owner, request)) {
			slot.owner = request;
			slot.owner.exclusionRoot = nullptr;
		}
		const auto now = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
			std::chrono::steady_clock::now().time_since_epoch()).count());
		ObserveServiceFrame(now);
		PollMirrorSlot(slot, request, now);

		if (auto* frame = PrepareMirrorSlot(slot, device); frame && g_captureClock.Claim(now, MirrorSettings::CaptureInterval(33333u))) {
			g_activeCoverageQuery = frame->coverage.Get();
			RE::NiPointer<RE::NiAVObject> receiverPin{ request.exclusionRoot };
			MirrorPerformance::Sample captureTiming(MirrorPerformance::Stage::VRStereo, 0u);
			const bool captured = CaptureMirrorNativeGuarded(request);
			if (captured) {
				auto& back = frame->image;
				context->CopyResource(back.texture.Get(), g_mirrorTarget.ColorTexture());
				back.request = request;
				back.request.exclusionRoot = nullptr;
				back.viewProjection = g_mirrorViewProjection;
				back.eye = g_reflectedEye;
				frame->serial = ++slot.nextSerial;
				frame->capturedAt = now;
				frame->pending = true;
			}
			captureTiming.Finish(captured);
			g_activeCoverageQuery = nullptr;
		}
		g_mirrorReady = slot.ready && !g_faulted.load(std::memory_order_acquire);
		if (g_mirrorReady) {
			MirrorPerformance::Sample compositeTiming(MirrorPerformance::Stage::VRComposite);
			CompositeMirror(slot.front);
		}
	}

	bool ProjectPaneForSelection(
		const DirectX::XMFLOAT3& center, const DirectX::XMFLOAT3& tangent,
		const DirectX::XMFLOAT3& bitangent, float halfWidth, float halfHeight,
		float& projectedArea) noexcept
	{
		projectedArea = 0.0f;
		bool visible = false;

		constexpr float kSelectionClipMargin = 1.5f;
		for (std::uint32_t eye = 0; eye < kEyeCount; ++eye) {
			float area = 0.0f;
			if (PlanarMirrorMath::PaneIntersectsView(center, tangent, bitangent, halfWidth, halfHeight,
					{ g_mainEye[eye].x, g_mainEye[eye].y, g_mainEye[eye].z }, g_mainViewProjection[eye], area,
					kSelectionClipMargin)) {
				visible = true;
				projectedArea = (std::max)(projectedArea, area);
			}
		}
#ifdef MIRROR_VR_PRODUCTION_TU  
		if (!visible) {
			
			static std::uint32_t s_outsideLogs = 0;
			static std::uint64_t s_lastOutsideTick = 0;
			const auto tick = GetTickCount64();
			if (s_outsideLogs < 24u && tick - s_lastOutsideTick >= 500u) {
				++s_outsideLogs; s_lastOutsideTick = tick;
				for (std::uint32_t eye = 0; eye < kEyeCount; ++eye) {
					const auto viewProjection = DirectX::XMLoadFloat4x4(&g_mainViewProjection[eye]);
					float ndc[4][3]{};
					std::size_t cornerIndex = 0;
					for (const float ts : { -1.0f, 1.0f }) {
						for (const float bs : { -1.0f, 1.0f }) {
							const DirectX::XMFLOAT3 corner{
								center.x - g_mainEye[eye].x + ts * halfWidth * tangent.x + bs * halfHeight * bitangent.x,
								center.y - g_mainEye[eye].y + ts * halfWidth * tangent.y + bs * halfHeight * bitangent.y,
								center.z - g_mainEye[eye].z + ts * halfWidth * tangent.z + bs * halfHeight * bitangent.z };
							DirectX::XMFLOAT4 clip{};
							DirectX::XMStoreFloat4(&clip, DirectX::XMVector4Transform(
								DirectX::XMVectorSet(corner.x, corner.y, corner.z, 1.0f), viewProjection));
							const float w = std::fabs(clip.w) > 1.0e-6f ? clip.w : 1.0e-6f;
							ndc[cornerIndex][0] = clip.x / w; ndc[cornerIndex][1] = clip.y / w; ndc[cornerIndex][2] = clip.w;
							++cornerIndex;
						}
					}
					logger::info("[VRReflections] PANE-OUTSIDE eye={} eyePos=({:.0f},{:.0f},{:.0f}) ndc=[({:.2f},{:.2f} w={:.0f}) ({:.2f},{:.2f} w={:.0f}) ({:.2f},{:.2f} w={:.0f}) ({:.2f},{:.2f} w={:.0f})] basisForward=({:.2f},{:.2f},{:.2f})",
						eye, g_mainEye[eye].x, g_mainEye[eye].y, g_mainEye[eye].z,
						ndc[0][0], ndc[0][1], ndc[0][2], ndc[1][0], ndc[1][1], ndc[1][2],
						ndc[2][0], ndc[2][1], ndc[2][2], ndc[3][0], ndc[3][1], ndc[3][2],
						g_mainEyeBasis[eye].entry[0][0], g_mainEyeBasis[eye].entry[0][1], g_mainEyeBasis[eye].entry[0][2]);
				}
			}
		}
#endif
		return visible;
	}

	bool PrivateCaptureActive() noexcept
	{
		return g_captureKind != CaptureKind::kNone;
	}

	CaptureKind ActiveCaptureKind() noexcept
	{
		return g_captureKind;
	}

	bool MirrorCaptureActive() noexcept
	{
		return g_captureKind == CaptureKind::kMirror;
	}

	void BindTargetAfterSetDirty() noexcept
	{
		auto* device = globals::d3d::device;
		auto* context = globals::d3d::context;
		if (!device || !context)
			return;
		if (g_captureKind == CaptureKind::kMirror && g_mirrorTarget.Bound()) {

			LogPrivateTargetRasterState(context, g_mirrorTarget, false);
			g_mirrorTarget.Rebind(context);
			if (g_materialPhase) {
				g_materialResources.Bind(context, g_mirrorTarget.MaterialDiffuseRTV(), g_mirrorTarget.MaterialNormalRTV(),
					g_mirrorTarget.MaterialPropertiesRTV(), g_mirrorTarget.DepthDSV());
			} else {
				auto* color = g_mirrorTarget.ColorRTV();
				context->OMSetRenderTargets(1, &color, g_mirrorTarget.DepthDSV());
			}
			PlanarMirrorLookup::ApplyReflectedRasterizer(device, context);
		}
	}

	ID3D11ShaderResourceView* GetMirrorColorSRV() noexcept
	{
		return g_mirrorReady ? g_mirrorSlot.front.srv.Get() : nullptr;
	}

	bool GetMirrorViewProjection(std::uint32_t eye, DirectX::XMFLOAT4X4& output) noexcept
	{
		if (!g_mirrorReady || eye >= g_mirrorViewProjection.size())
			return false;
		output = g_mirrorSlot.front.viewProjection[eye];
		return true;
	}

	std::uint32_t MirrorPerEyeWidth() noexcept
	{
		D3D11_TEXTURE2D_DESC description{};
		if (g_mirrorReady && g_mirrorSlot.front.texture)
			g_mirrorSlot.front.texture->GetDesc(&description);
		return description.Width / kEyeCount;
	}

	std::uint32_t MirrorHeight() noexcept
	{
		D3D11_TEXTURE2D_DESC description{};
		if (g_mirrorReady && g_mirrorSlot.front.texture)
			g_mirrorSlot.front.texture->GetDesc(&description);
		return description.Height;
	}

	void Release() noexcept
	{
		if (PrivateCaptureActive())
			return;
		ResetForWorldChange();
		ReleaseDeviceResources(globals::d3d::context);
		g_deviceIdentity = nullptr;
	}
}

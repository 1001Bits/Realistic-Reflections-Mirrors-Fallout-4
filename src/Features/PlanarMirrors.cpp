#include "PlanarMirrors.h"
#include "MirrorCaptureOptimizations.h"
#include "Utils/D3DDeviceIdentity.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <memory>
#include <utility>

#include <d3d11_1.h>

#include "RE/Bethesda/BSGraphics.h"
#include "RE/NetImmerse/NiCamera.h"

#include "ReflectionRuntime.h"
#include "NiVirtualDispatch.h"

namespace
{
	constexpr std::uint32_t kMaximumTargetDimension = 16384;
	constexpr std::uint32_t kMaximumVREyes = 2;

	auto& g_materialEnvironmentScratchTexture =
		*new Microsoft::WRL::ComPtr<ID3D11Texture2D>();
	auto& g_materialEnvironmentScratchSRV =
		*new Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>();
	ID3D11Device* g_materialEnvironmentScratchDevice = nullptr;  
	std::uint32_t g_materialEnvironmentScratchWidth = 0;
	std::uint32_t g_materialEnvironmentScratchHeight = 0;
	DXGI_FORMAT g_materialEnvironmentScratchFormat = DXGI_FORMAT_UNKNOWN;

	struct VRCameraStateData
	{
		RE::BSGraphics::ViewData camViewData[kMaximumVREyes];  
		RE::NiPoint3 posAdjust[kMaximumVREyes];                
		RE::NiPoint3 currentPosAdjust[kMaximumVREyes];         
		RE::NiPoint3 previousPosAdjust[kMaximumVREyes];        
		const RE::NiCamera* referenceCamera;                    
		bool useJitter;                                         
		std::byte pad[0x0F]{};                                  
	};
	static_assert(offsetof(VRCameraStateData, posAdjust) == 0x420);
	static_assert(offsetof(VRCameraStateData, currentPosAdjust) == 0x438);
	static_assert(offsetof(VRCameraStateData, previousPosAdjust) == 0x450);
	static_assert(offsetof(VRCameraStateData, referenceCamera) == 0x468);
	static_assert(offsetof(VRCameraStateData, useJitter) == 0x470);
	static_assert(sizeof(VRCameraStateData) == 0x480);

	struct CameraOverrideState
	{
		const RE::NiCamera* camera{ nullptr };
		PlanarMirrorMath::Plane plane{};
		std::array<DirectX::XMFLOAT4X4, kMaximumVREyes> finalView{};
		std::array<DirectX::XMFLOAT4X4, kMaximumVREyes> finalViewProjection{};
		std::array<DirectX::XMFLOAT4X4, kMaximumVREyes> unclippedViewProjection{};
		std::array<DirectX::XMFLOAT3, kMaximumVREyes> cameraOrigin{};
		std::uint32_t availableEyeMask{ 0 };
		std::uint32_t obliqueEyeMask{ 0 };
		float clipBias{ 1.5f };
		bool applyObliqueClip{ true };
		bool projectionAvailable{ false };
		bool active{ false };
	};

	thread_local CameraOverrideState g_cameraOverride{};

	void CopyMatrixToRows(const DirectX::XMFLOAT4X4& source, __m128 destination[4]) noexcept
	{
		const DirectX::XMMATRIX matrix = DirectX::XMLoadFloat4x4(&source);
		destination[0] = matrix.r[0];
		destination[1] = matrix.r[1];
		destination[2] = matrix.r[2];
		destination[3] = matrix.r[3];
	}

	DirectX::XMFLOAT4X4 CopyRowsToMatrix(const __m128 source[4]) noexcept
	{
		DirectX::XMMATRIX matrix{};
		matrix.r[0] = source[0];
		matrix.r[1] = source[1];
		matrix.r[2] = source[2];
		matrix.r[3] = source[3];
		DirectX::XMFLOAT4X4 output{};
		DirectX::XMStoreFloat4x4(&output, matrix);
		return output;
	}

	bool FiniteMatrix(const DirectX::XMFLOAT4X4& matrix) noexcept
	{
		const float* values = &matrix._11;
		for (std::size_t index = 0; index < 16; ++index) {
			if (!std::isfinite(values[index]))
				return false;
		}
		return true;
	}

	bool MatricesApproximatelyEqual(
		const DirectX::XMFLOAT4X4& actual,
		const DirectX::XMFLOAT4X4& expected,
		float relativeTolerance) noexcept
	{
		if (!FiniteMatrix(actual) || !FiniteMatrix(expected) ||
			!std::isfinite(relativeTolerance) || relativeTolerance < 0.0f)
			return false;
		const float* actualValues = &actual._11;
		const float* expectedValues = &expected._11;
		for (std::size_t index = 0; index < 16; ++index) {
			const float tolerance = relativeTolerance * (1.0f + std::abs(expectedValues[index]));
			if (std::abs(actualValues[index] - expectedValues[index]) > tolerance)
				return false;
		}
		return true;
	}

	bool ProjectionForViewProjection(
		const DirectX::XMFLOAT4X4& view,
		const DirectX::XMFLOAT4X4& viewProjection,
		DirectX::XMFLOAT4X4& projection) noexcept
	{
		const auto viewMatrix = DirectX::XMLoadFloat4x4(&view);
		DirectX::XMVECTOR viewDeterminant{};
		const auto inverseView = DirectX::XMMatrixInverse(&viewDeterminant, viewMatrix);
		const float determinant = DirectX::XMVectorGetX(viewDeterminant);
		if (!std::isfinite(determinant) || std::abs(determinant) <= 1.0e-8f)
			return false;
		const auto stableProjection = DirectX::XMMatrixMultiply(
			inverseView, DirectX::XMLoadFloat4x4(&viewProjection));
		DirectX::XMStoreFloat4x4(&projection, stableProjection);
		if (!FiniteMatrix(projection))
			return false;
		DirectX::XMFLOAT4X4 reconstructedViewProjection{};
		DirectX::XMStoreFloat4x4(
			&reconstructedViewProjection,
			DirectX::XMMatrixMultiply(viewMatrix, stableProjection));
		return MatricesApproximatelyEqual(
			reconstructedViewProjection, viewProjection, 1.0e-4f);
	}

	void LogObliqueFallback(
		std::uint32_t eye,
		const PlanarMirrorMath::ObliqueProjectionDiagnostics& diagnostics,
		bool nativeViewProjectionAvailable) noexcept
	{
		using Clock = std::chrono::steady_clock;
		static thread_local std::uint64_t fallbackCount = 0;
		static thread_local Clock::time_point previousFallback{};
		const auto now = Clock::now();
		const double intervalMs = fallbackCount == 0 ? 0.0 :
			std::chrono::duration<double, std::milli>(now - previousFallback).count();
		previousFallback = now;
		++fallbackCount;
		if (fallbackCount <= 4u || (fallbackCount % 120u) == 0u) {
			try {
				logger::warn(
					"[PlanarMirrors] oblique fallback reason={} eye={} nativeVPAvailable={} obliqueApplied=false "
					"viewDet={:.6g} projectionDet={:.6g} denominator={:.6g} count={} intervalMs={:.2f}",
					PlanarMirrorMath::ObliqueProjectionFailureName(diagnostics.failure),
					eye,
					nativeViewProjectionAvailable,
					diagnostics.viewDeterminant,
					diagnostics.projectionDeterminant,
					diagnostics.denominator,
					fallbackCount,
					intervalMs);
			} catch (...) {
				
			}
		}
	}

	void LogObliqueInputs(
		const char* outcome,
		std::uint32_t eye,
		const DirectX::XMFLOAT3& cameraOrigin,
		const DirectX::XMFLOAT4X4& view,
		const DirectX::XMFLOAT4X4& projection,
		const PlanarMirrorMath::ObliqueProjectionDiagnostics& diagnostics) noexcept
	{
		static thread_local std::uint32_t s_failureDumps[kMaximumVREyes]{};
		static thread_local std::uint32_t s_successDumps[kMaximumVREyes]{};
		if (eye >= kMaximumVREyes)
			return;
		const bool failed = diagnostics.failure != PlanarMirrorMath::ObliqueProjectionFailure::kNone;
		auto& count = failed ? s_failureDumps[eye] : s_successDumps[eye];
		if (count >= (failed ? 3u : 1u))
			return;
		++count;
		try {
			const auto& plane = g_cameraOverride.plane;
			const float* v = &view._11;
			const float* p = &projection._11;
			logger::info(
				"[PlanarMirrors] OBLIQUE-INPUTS outcome={} eye={} vr={} worldPlane=({:.5f},{:.5f},{:.5f} d={:.3f}) clipBias={:.3f} "
				"origin=({:.3f},{:.3f},{:.3f}) cameraPlane=({:.5f},{:.5f},{:.5f},{:.3f}) q=({:.5f},{:.5f},{:.5f},{:.7f}) "
				"denominator={:.6g} viewDet={:.4g} projDet={:.4g}",
				outcome, eye, ReflectionRuntime::IsVR(), plane.normal.x, plane.normal.y, plane.normal.z, plane.distance,
				g_cameraOverride.clipBias, cameraOrigin.x, cameraOrigin.y, cameraOrigin.z,
				diagnostics.cameraPlane.x, diagnostics.cameraPlane.y, diagnostics.cameraPlane.z, diagnostics.cameraPlane.w,
				diagnostics.clipCorner.x, diagnostics.clipCorner.y, diagnostics.clipCorner.z, diagnostics.clipCorner.w,
				diagnostics.denominator, diagnostics.viewDeterminant, diagnostics.projectionDeterminant);
			logger::info(
				"[PlanarMirrors] OBLIQUE-INPUTS eye={} view=[{:.5f},{:.5f},{:.5f},{:.5f} | {:.5f},{:.5f},{:.5f},{:.5f} | "
				"{:.5f},{:.5f},{:.5f},{:.5f} | {:.3f},{:.3f},{:.3f},{:.5f}]",
				eye, v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7], v[8], v[9], v[10], v[11], v[12], v[13], v[14], v[15]);
			logger::info(
				"[PlanarMirrors] OBLIQUE-INPUTS eye={} proj=[{:.5f},{:.5f},{:.5f},{:.5f} | {:.5f},{:.5f},{:.5f},{:.5f} | "
				"{:.5f},{:.5f},{:.5f},{:.5f} | {:.5f},{:.5f},{:.5f},{:.5f}]",
				eye, p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7], p[8], p[9], p[10], p[11], p[12], p[13], p[14], p[15]);
		} catch (...) {
			
		}
	}

	bool PatchViewData(
		RE::BSGraphics::ViewData& viewData,
		const RE::NiPoint3& position,
		std::uint32_t eye) noexcept
	{
		if (eye >= g_cameraOverride.finalViewProjection.size())
			return false;
		const std::uint32_t eyeBit = 1u << eye;
		g_cameraOverride.availableEyeMask &= ~eyeBit;
		g_cameraOverride.obliqueEyeMask &= ~eyeBit;

		const DirectX::XMFLOAT4X4 view = CopyRowsToMatrix(viewData.viewMat);
		const DirectX::XMFLOAT4X4 nativeUnjitteredViewProjection =
			CopyRowsToMatrix(viewData.viewProjUnjittered);
		const DirectX::XMFLOAT3 cameraOrigin{ position.x, position.y, position.z };
		const auto& stableViewProjection = nativeUnjitteredViewProjection;
		DirectX::XMFLOAT4X4 stableProjection{};
		if (!FiniteMatrix(view) || !FiniteMatrix(stableViewProjection) ||
			!std::isfinite(cameraOrigin.x) || !std::isfinite(cameraOrigin.y) ||
			!std::isfinite(cameraOrigin.z) ||
			!ProjectionForViewProjection(view, stableViewProjection, stableProjection)) {

			static std::uint32_t s_cameraStateRejects = 0;
			const auto rejects = ++s_cameraStateRejects;
			if (rejects <= 4u || (rejects % 300u) == 0u)
				logger::warn(
					"[PlanarMirrors] reflected camera state rejected before the oblique clip: viewFinite={} vpFinite={} originFinite={} (count={})",
					FiniteMatrix(view), FiniteMatrix(stableViewProjection),
					std::isfinite(cameraOrigin.x) && std::isfinite(cameraOrigin.y) && std::isfinite(cameraOrigin.z),
					rejects);
			return false;
		}

		g_cameraOverride.unclippedViewProjection[eye] = stableViewProjection;
		if (!g_cameraOverride.applyObliqueClip) {

			CopyMatrixToRows(stableProjection, viewData.projMat);
			CopyMatrixToRows(stableViewProjection, viewData.viewProjMat);
			CopyMatrixToRows(stableViewProjection, viewData.viewProjUnjittered);
			g_cameraOverride.finalView[eye] = view;
			g_cameraOverride.finalViewProjection[eye] = stableViewProjection;
			g_cameraOverride.cameraOrigin[eye] = cameraOrigin;
			g_cameraOverride.availableEyeMask |= eyeBit;
			return true;
		}

		DirectX::XMFLOAT4X4 obliqueProjection{};
		DirectX::XMFLOAT4X4 obliqueViewProjection{};
		PlanarMirrorMath::ObliqueProjectionDiagnostics diagnostics{};
		if (!PlanarMirrorMath::BuildObliqueViewProjection(
				g_cameraOverride.plane,
				g_cameraOverride.clipBias,
				cameraOrigin,
				view,
				stableProjection,
				obliqueProjection,
				obliqueViewProjection,
				nullptr,
				&diagnostics)) {
			LogObliqueFallback(eye, diagnostics, true);
			LogObliqueInputs("fallback", eye, cameraOrigin, view, stableProjection, diagnostics);

			CopyMatrixToRows(stableProjection, viewData.projMat);
			CopyMatrixToRows(stableViewProjection, viewData.viewProjMat);
			CopyMatrixToRows(stableViewProjection, viewData.viewProjUnjittered);
			g_cameraOverride.finalView[eye] = view;
			g_cameraOverride.finalViewProjection[eye] = stableViewProjection;
			g_cameraOverride.cameraOrigin[eye] = cameraOrigin;
			g_cameraOverride.availableEyeMask |= eyeBit;
			return true;
		}

		LogObliqueInputs("applied", eye, cameraOrigin, view, stableProjection, diagnostics);
		CopyMatrixToRows(obliqueProjection, viewData.projMat);
		CopyMatrixToRows(obliqueViewProjection, viewData.viewProjMat);
		CopyMatrixToRows(obliqueViewProjection, viewData.viewProjUnjittered);
		g_cameraOverride.finalView[eye] = view;
		g_cameraOverride.finalViewProjection[eye] = obliqueViewProjection;
		g_cameraOverride.cameraOrigin[eye] = cameraOrigin;
		g_cameraOverride.availableEyeMask |= eyeBit;
		g_cameraOverride.obliqueEyeMask |= eyeBit;
		return true;
	}

	void SetDebugName(ID3D11DeviceChild* object, const char* name) noexcept
	{
		if (object && name)
			object->SetPrivateData(WKPDID_D3DDebugObjectName, static_cast<UINT>(std::strlen(name)), name);
	}

	bool DeviceChildUsesDevice(ID3D11DeviceChild* child, ID3D11Device* device) noexcept
	{
		return Util::D3DChildUsesDevice(child, device);
	}

	bool ViewUsesResource(ID3D11View* view, ID3D11Resource* resource) noexcept
	{
		if (!view || !resource)
			return false;
		Microsoft::WRL::ComPtr<ID3D11Resource> owner;
		view->GetResource(owner.GetAddressOf());
		return owner.Get() == resource;
	}

	bool TextureDescriptionsMatch(
		const D3D11_TEXTURE2D_DESC& left,
		const D3D11_TEXTURE2D_DESC& right) noexcept
	{
		return left.Width == right.Width && left.Height == right.Height &&
			left.MipLevels == right.MipLevels && left.ArraySize == right.ArraySize &&
			left.Format == right.Format && left.SampleDesc.Count == right.SampleDesc.Count &&
			left.SampleDesc.Quality == right.SampleDesc.Quality && left.Usage == right.Usage &&
			left.BindFlags == right.BindFlags && left.CPUAccessFlags == right.CPUAccessFlags &&
			left.MiscFlags == right.MiscFlags;
	}

	bool ViewDescriptionsMatch(
		const D3D11_RENDER_TARGET_VIEW_DESC& left,
		const D3D11_RENDER_TARGET_VIEW_DESC& right) noexcept
	{
		return left.Format == right.Format &&
			left.ViewDimension == D3D11_RTV_DIMENSION_TEXTURE2D &&
			right.ViewDimension == D3D11_RTV_DIMENSION_TEXTURE2D &&
			left.Texture2D.MipSlice == right.Texture2D.MipSlice;
	}

	bool ViewDescriptionsMatch(
		const D3D11_SHADER_RESOURCE_VIEW_DESC& left,
		const D3D11_SHADER_RESOURCE_VIEW_DESC& right) noexcept
	{
		return left.Format == right.Format &&
			left.ViewDimension == D3D11_SRV_DIMENSION_TEXTURE2D &&
			right.ViewDimension == D3D11_SRV_DIMENSION_TEXTURE2D &&
			left.Texture2D.MostDetailedMip == right.Texture2D.MostDetailedMip &&
			left.Texture2D.MipLevels == right.Texture2D.MipLevels;
	}

	bool ViewDescriptionsMatch(
		const D3D11_UNORDERED_ACCESS_VIEW_DESC& left,
		const D3D11_UNORDERED_ACCESS_VIEW_DESC& right) noexcept
	{
		return left.Format == right.Format &&
			left.ViewDimension == D3D11_UAV_DIMENSION_TEXTURE2D &&
			right.ViewDimension == D3D11_UAV_DIMENSION_TEXTURE2D &&
			left.Texture2D.MipSlice == right.Texture2D.MipSlice;
	}

	bool EnsureMaterialEnvironmentScratch(
		ID3D11Device* device,
		std::uint32_t width,
		std::uint32_t height,
		DXGI_FORMAT format) noexcept
	{
		if (!device || width == 0 || height == 0 || format == DXGI_FORMAT_UNKNOWN)
			return false;
		const bool identityMatches = g_materialEnvironmentScratchDevice == device &&
			g_materialEnvironmentScratchFormat == format;
		if (identityMatches && g_materialEnvironmentScratchTexture && g_materialEnvironmentScratchSRV &&
			g_materialEnvironmentScratchWidth >= width && g_materialEnvironmentScratchHeight >= height)
			return true;

		const std::uint32_t requestedWidth = identityMatches ?
			std::max(width, g_materialEnvironmentScratchWidth) : width;
		const std::uint32_t requestedHeight = identityMatches ?
			std::max(height, g_materialEnvironmentScratchHeight) : height;
		D3D11_TEXTURE2D_DESC description{};
		description.Width = requestedWidth;
		description.Height = requestedHeight;
		description.MipLevels = 1;
		description.ArraySize = 1;
		description.Format = format;
		description.SampleDesc.Count = 1;
		description.Usage = D3D11_USAGE_DEFAULT;
		description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		Microsoft::WRL::ComPtr<ID3D11Texture2D> replacementTexture;
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> replacementSRV;
		if (FAILED(device->CreateTexture2D(&description, nullptr, &replacementTexture)) ||
			FAILED(device->CreateShaderResourceView(replacementTexture.Get(), nullptr, &replacementSRV)))
			return false;

		SetDebugName(replacementTexture.Get(), "RealisticReflectionsMirrors.PlanarMirror.EnvironmentScratch");
		SetDebugName(replacementSRV.Get(), "RealisticReflectionsMirrors.PlanarMirror.EnvironmentScratchSRV");
		g_materialEnvironmentScratchTexture = std::move(replacementTexture);
		g_materialEnvironmentScratchSRV = std::move(replacementSRV);
		g_materialEnvironmentScratchDevice = device;
		g_materialEnvironmentScratchWidth = requestedWidth;
		g_materialEnvironmentScratchHeight = requestedHeight;
		g_materialEnvironmentScratchFormat = format;
		return true;
	}

	class ScopedComputeState
	{
	public:
		explicit ScopedComputeState(ID3D11DeviceContext* context) noexcept : context(context)
		{
			if (!context)
				return;
			computeClassInstanceCount = static_cast<UINT>(computeClassInstances.size());
			context->CSGetShader(
				&computeShader, computeClassInstances.data(), &computeClassInstanceCount);
			context->CSGetShaderResources(0, static_cast<UINT>(computeResources.size()), computeResources.data());
			context->CSGetUnorderedAccessViews(0, static_cast<UINT>(computeUAVs.size()), computeUAVs.data());
			context->CSGetSamplers(
				0, static_cast<UINT>(computeSamplers.size()), computeSamplers.data());

			context->CSGetConstantBuffers(
				0, static_cast<UINT>(computeConstantBuffers.size()), computeConstantBuffers.data());
			if (SUCCEEDED(context->QueryInterface(IID_PPV_ARGS(computeContext1.GetAddressOf()))) &&
				computeContext1) {
				std::array<ID3D11Buffer*, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT>
					rangedBuffers{};
				computeContext1->CSGetConstantBuffers1(
					0, static_cast<UINT>(rangedBuffers.size()), rangedBuffers.data(),
					computeFirstConstants.data(), computeNumConstants.data());
				bool rangesMatchCapturedBuffers = true;
				for (std::size_t index = 0; index < rangedBuffers.size(); ++index) {
					rangesMatchCapturedBuffers &= rangedBuffers[index] == computeConstantBuffers[index];
					if (rangedBuffers[index])
						rangedBuffers[index]->Release();
				}
				if (!rangesMatchCapturedBuffers)
					computeContext1.Reset();
			}
			context->PSGetShaderResources(0, static_cast<UINT>(pixelResources.size()), pixelResources.data());

			std::array<ID3D11ShaderResourceView*, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT> nullResources{};
			std::array<ID3D11UnorderedAccessView*, D3D11_PS_CS_UAV_REGISTER_COUNT> nullUAVs{};
			context->CSSetShaderResources(0, static_cast<UINT>(nullResources.size()), nullResources.data());
			context->PSSetShaderResources(0, static_cast<UINT>(nullResources.size()), nullResources.data());
			context->CSSetUnorderedAccessViews(0, static_cast<UINT>(nullUAVs.size()), nullUAVs.data(), nullptr);
			captured = true;
		}

		~ScopedComputeState()
		{
			if (!captured || !context)
				return;
			context->CSSetShaderResources(
				0, static_cast<UINT>(computeResources.size()), computeResources.data());
			context->CSSetShader(
				computeShader, computeClassInstances.data(), computeClassInstanceCount);
			context->CSSetUnorderedAccessViews(
				0, static_cast<UINT>(computeUAVs.size()), computeUAVs.data(), nullptr);
			context->CSSetSamplers(
				0, static_cast<UINT>(computeSamplers.size()), computeSamplers.data());
			if (computeContext1) {
				computeContext1->CSSetConstantBuffers1(
					0, static_cast<UINT>(computeConstantBuffers.size()), computeConstantBuffers.data(),
					computeFirstConstants.data(), computeNumConstants.data());
			} else {
				context->CSSetConstantBuffers(
					0, static_cast<UINT>(computeConstantBuffers.size()), computeConstantBuffers.data());
			}
			context->PSSetShaderResources(
				0, static_cast<UINT>(pixelResources.size()), pixelResources.data());

			if (computeShader)
				computeShader->Release();
			for (UINT index = 0; index < computeClassInstanceCount; ++index) {
				if (computeClassInstances[index])
					computeClassInstances[index]->Release();
			}
			for (auto* resource : computeResources) {
				if (resource)
					resource->Release();
			}
			for (auto* view : computeUAVs) {
				if (view)
					view->Release();
			}
			for (auto* buffer : computeConstantBuffers) {
				if (buffer)
					buffer->Release();
			}
			for (auto* sampler : computeSamplers) {
				if (sampler)
					sampler->Release();
			}
			for (auto* resource : pixelResources) {
				if (resource)
					resource->Release();
			}
		}

		ScopedComputeState(const ScopedComputeState&) = delete;
		ScopedComputeState& operator=(const ScopedComputeState&) = delete;

	private:
		ID3D11DeviceContext* context{ nullptr };
		ID3D11ComputeShader* computeShader{ nullptr };
		std::array<ID3D11ClassInstance*, D3D11_SHADER_MAX_INTERFACES> computeClassInstances{};
		UINT computeClassInstanceCount{ 0 };
		std::array<ID3D11ShaderResourceView*, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT> computeResources{};
		std::array<ID3D11UnorderedAccessView*, D3D11_PS_CS_UAV_REGISTER_COUNT> computeUAVs{};
		std::array<ID3D11Buffer*, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT>
			computeConstantBuffers{};
		std::array<UINT, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT> computeFirstConstants{};
		std::array<UINT, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT> computeNumConstants{};
		Microsoft::WRL::ComPtr<ID3D11DeviceContext1> computeContext1;
		std::array<ID3D11SamplerState*, D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT> computeSamplers{};
		std::array<ID3D11ShaderResourceView*, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT> pixelResources{};
		bool captured{ false };
	};
}

namespace PlanarMirrors
{
	bool SupportsNativeCameraReflection() noexcept
	{
		const auto* runtime = ReflectionRuntime::Get();
		return runtime && runtime->reflectCameraAboutPlane && runtime->buildCameraStateData;
	}

	bool ReflectCamera(
		const RE::NiCamera* source,
		const PlanarMirrorMath::Plane& plane,
		RE::NiCamera* destination) noexcept
	{
		if (!SupportsNativeCameraReflection() || !source || !destination || source == destination)
			return false;
		PlanarMirrorMath::Plane normalizedPlane = plane;
		if (!PlanarMirrorMath::NormalizePlane(normalizedPlane))
			return false;

		const std::size_t limitsOffset = ReflectionRuntime::IsVR() ? 0x20Cu : 0x17Cu;
		std::memcpy(reinterpret_cast<std::byte*>(destination) + limitsOffset,
			reinterpret_cast<const std::byte*>(source) + limitsOffset, 0x1Cu);
		const float nativePlane[4] = {
			normalizedPlane.normal.x,
			normalizedPlane.normal.y,
			normalizedPlane.normal.z,
			normalizedPlane.distance
		};
		using ReflectCamera_t = void (*)(const RE::NiCamera*, const float*, RE::NiCamera*);
		const auto* runtime = ReflectionRuntime::Get();
		if (!runtime)
			return false;
		auto reflect = reinterpret_cast<ReflectCamera_t>(
			ReflectionRuntime::Address(runtime->reflectCameraAboutPlane));
		reflect(source, nativePlane, destination);
		return true;
	}

	bool GetUnclippedCameraView(const RE::NiCamera* camera,
		DirectX::XMFLOAT4X4& viewProjection, DirectX::XMFLOAT3& origin) noexcept
	{
		const auto* runtime = ReflectionRuntime::Get();
		if (!camera || !runtime || runtime->vr) return false;
		RE::BSGraphics::CameraStateData data{};
		using Build_t = void (*)(void*, void*, const RE::NiCamera*, bool);
		reinterpret_cast<Build_t>(ReflectionRuntime::Address(runtime->buildCameraStateData))(
			reinterpret_cast<void*>(ReflectionRuntime::Address(runtime->graphicsState)), &data, camera, false);

		viewProjection = CameraOverrideActive(camera) && g_cameraOverride.projectionAvailable ?
			g_cameraOverride.unclippedViewProjection[0] : CopyRowsToMatrix(data.camViewData.viewProjUnjittered);
		origin = {data.posAdjust.x, data.posAdjust.y, data.posAdjust.z};
		return FiniteMatrix(viewProjection) && std::isfinite(origin.x) && std::isfinite(origin.y) && std::isfinite(origin.z);
	}

	[[nodiscard]] bool PaneCaptureCropEnabled() noexcept
	{
		return MirrorCaptureOptimizations::On(MirrorCaptureOptimizations::paneCrop);
	}

	bool FitCaptureToPane(
		RE::NiCamera* camera,
		const DirectX::XMFLOAT3& center,
		const DirectX::XMFLOAT3& tangent,
		const DirectX::XMFLOAT3& bitangent,
		float halfWidth, float halfHeight,
		const CaptureCropView* cropView) noexcept
	{
		const auto* runtime = ReflectionRuntime::Get();
		if (!camera || !runtime || CameraOverrideActive() || !runtime->cameraSetViewFrustum)
			return false;
		if (!runtime->vr) {

			DirectX::XMFLOAT3 forward{}, up{}, right{};
			const auto& position = camera->world.translate;
			if (!PlanarMirrorMath::FlatPaneCaptureBasis(center, tangent, bitangent,
					{position.x, position.y, position.z}, forward, up, right)) return false;
			camera->local.rotate.entry[0] = {forward.x, forward.y, forward.z, 0};
			camera->local.rotate.entry[1] = {up.x, up.y, up.z, 0};
			camera->local.rotate.entry[2] = {right.x, right.y, right.z, 0};
			RE::NiUpdateData update{};
			NiVirtualDispatch::UpdateWorldData(camera, &update);
		}

		alignas(16) VRCameraStateData nativeState{};
		using Build_t = void (*)(void*, void*, const RE::NiCamera*, bool);
		reinterpret_cast<Build_t>(ReflectionRuntime::Address(runtime->buildCameraStateData))(
			reinterpret_cast<void*>(ReflectionRuntime::Address(runtime->graphicsState)), &nativeState, camera, false);
		const auto* bytes = reinterpret_cast<const std::byte*>(camera);
		const auto eyeCount = runtime->vr ? *reinterpret_cast<const std::uint32_t*>(bytes + 0x208) : 1u;
		const auto* frusta = runtime->vr ? *reinterpret_cast<const RE::NiFrustum* const*>(bytes + 0x1A0) :
			std::addressof(camera->viewFrustum);
		if (!frusta || eyeCount == 0u || eyeCount > 2u)
			return false;
		RE::NiFrustum fitted = frusta[0];
		
		DirectX::XMFLOAT4X4 visibleViewProjection{};
		DirectX::XMFLOAT3 visibleEye{};
		bool cropToView = false;
		if (!runtime->vr && cropView && cropView->camera && PaneCaptureCropEnabled() &&
			GetUnclippedCameraView(cropView->camera, visibleViewProjection, visibleEye)) {
			visibleEye = { visibleEye.x + cropView->originOffset.x, visibleEye.y + cropView->originOffset.y,
				visibleEye.z + cropView->originOffset.z };
			cropToView = std::isfinite(visibleEye.x) && std::isfinite(visibleEye.y) && std::isfinite(visibleEye.z);
		}
		std::uint32_t visibleEyes = 0;
		for (std::uint32_t eye = 0; eye < eyeCount; ++eye) {
			const auto& frustum = frusta[eye];
			if (frustum.ortho)
				return false;
			const auto& flat = *reinterpret_cast<const RE::BSGraphics::CameraStateData*>(&nativeState);
			const auto& view = runtime->vr ? nativeState.camViewData[eye] : flat.camViewData;
			const auto& position = runtime->vr ? nativeState.posAdjust[eye] : flat.posAdjust;
			DirectX::XMFLOAT4 slopes{};
			if (!PlanarMirrorMath::FitPaneFrustum(center, tangent, bitangent, halfWidth, halfHeight,
					{ position.x, position.y, position.z }, CopyRowsToMatrix(view.viewProjUnjittered),
					{ frustum.left, frustum.right, frustum.bottom, frustum.top }, slopes,
					cropToView ? &visibleViewProjection : nullptr, cropToView ? &visibleEye : nullptr,
					MirrorCaptureOptimizations::kPaneCropMargin))
				continue; 
			fitted.left = visibleEyes == 0u ? slopes.x : (std::min)(fitted.left, slopes.x);
			fitted.right = visibleEyes == 0u ? slopes.y : (std::max)(fitted.right, slopes.y);
			fitted.bottom = visibleEyes == 0u ? slopes.z : (std::min)(fitted.bottom, slopes.z);
			fitted.top = visibleEyes == 0u ? slopes.w : (std::max)(fitted.top, slopes.w);
			++visibleEyes;
		}
		if (visibleEyes == 0u) return false;

		using Set_t = void (*)(RE::NiCamera*, const RE::NiFrustum*);
		reinterpret_cast<Set_t>(ReflectionRuntime::Address(runtime->cameraSetViewFrustum))(camera, &fitted);
		return true;
	}

	bool BeginCameraOverride(
		const RE::NiCamera* reflectedCamera,
		const PlanarMirrorMath::Plane& plane,
		float clipBias,
		bool applyObliqueClip) noexcept
	{
		if (g_cameraOverride.active || !reflectedCamera || !std::isfinite(clipBias) || clipBias < 0.0f)
			return false;
		PlanarMirrorMath::Plane normalizedPlane = plane;
		if (!PlanarMirrorMath::NormalizePlane(normalizedPlane))
			return false;
		g_cameraOverride.camera = reflectedCamera;
		g_cameraOverride.plane = normalizedPlane;
		g_cameraOverride.clipBias = clipBias;
		g_cameraOverride.applyObliqueClip = applyObliqueClip;
		g_cameraOverride.availableEyeMask = 0;
		g_cameraOverride.obliqueEyeMask = 0;
		g_cameraOverride.projectionAvailable = false;
		g_cameraOverride.active = true;
		return true;
	}

	void EndCameraOverride(const RE::NiCamera* reflectedCamera) noexcept
	{
		if (!g_cameraOverride.active || (reflectedCamera && reflectedCamera != g_cameraOverride.camera))
			return;
		g_cameraOverride = {};
	}

	bool CameraOverrideActive(const RE::NiCamera* camera) noexcept
	{
		return g_cameraOverride.active && (!camera || camera == g_cameraOverride.camera);
	}

	bool PatchCameraStateData(
		const RE::NiCamera* camera,
		RE::BSGraphics::CameraStateData& stateData) noexcept
	{
		if (!CameraOverrideActive(camera))
			return false;

		g_cameraOverride.availableEyeMask = 0;
		g_cameraOverride.obliqueEyeMask = 0;
		g_cameraOverride.projectionAvailable = PatchViewData(
			stateData.camViewData, stateData.posAdjust, 0);

		return g_cameraOverride.projectionAvailable;
	}

	bool PatchVRCameraStateData(const RE::NiCamera* camera, void* stateData) noexcept
	{
		if (!ReflectionRuntime::IsVR() || !CameraOverrideActive(camera) || !stateData)
			return false;
		auto& stereo = *static_cast<VRCameraStateData*>(stateData);
		std::uint32_t viewCount = kMaximumVREyes;
		const auto* cameraBytes = reinterpret_cast<const std::byte*>(camera);
		const auto nativeCount = *reinterpret_cast<const std::uint32_t*>(cameraBytes + 0x208);
		if (nativeCount > 0 && nativeCount < viewCount)
			viewCount = nativeCount;

		g_cameraOverride.availableEyeMask = 0;
		g_cameraOverride.obliqueEyeMask = 0;
		bool anyAvailable = false;
		for (std::uint32_t eye = 0; eye < viewCount; ++eye)
			anyAvailable = PatchViewData(stereo.camViewData[eye], stereo.posAdjust[eye], eye) || anyAvailable;
		g_cameraOverride.projectionAvailable = anyAvailable;
		return anyAvailable;
	}

	bool GetPatchedViewProjection(
		const RE::NiCamera* reflectedCamera,
		DirectX::XMFLOAT4X4& output,
		bool* obliqueApplied) noexcept
	{
		return GetPatchedViewProjectionForEye(reflectedCamera, 0, output, obliqueApplied);
	}

	bool GetPatchedViewProjectionForEye(
		const RE::NiCamera* reflectedCamera,
		std::uint32_t eye,
		DirectX::XMFLOAT4X4& output,
		bool* obliqueApplied) noexcept
	{
		if (obliqueApplied)
			*obliqueApplied = false;
		if (!CameraOverrideActive(reflectedCamera) || !g_cameraOverride.projectionAvailable ||
			eye >= g_cameraOverride.finalViewProjection.size() ||
			(g_cameraOverride.availableEyeMask & (1u << eye)) == 0)
			return false;
		output = g_cameraOverride.finalViewProjection[eye];
		if (obliqueApplied)
			*obliqueApplied = (g_cameraOverride.obliqueEyeMask & (1u << eye)) != 0;
		return true;
	}

	bool GetPatchedCameraStateForEye(
		const RE::NiCamera* reflectedCamera,
		std::uint32_t eye,
		PatchedCameraState& output) noexcept
	{
		output = {};
		if (!CameraOverrideActive(reflectedCamera) || !g_cameraOverride.projectionAvailable ||
			eye >= g_cameraOverride.finalViewProjection.size() ||
			(g_cameraOverride.availableEyeMask & (1u << eye)) == 0)
			return false;
		output.view = g_cameraOverride.finalView[eye];
		output.viewProjection = g_cameraOverride.finalViewProjection[eye];
		output.unclippedViewProjection = g_cameraOverride.unclippedViewProjection[eye];
		output.cameraOrigin = g_cameraOverride.cameraOrigin[eye];
		output.obliqueApplied = (g_cameraOverride.obliqueEyeMask & (1u << eye)) != 0;
		return FiniteMatrix(output.view) && FiniteMatrix(output.viewProjection) &&
			FiniteMatrix(output.unclippedViewProjection) &&
			std::isfinite(output.cameraOrigin.x) && std::isfinite(output.cameraOrigin.y) &&
			std::isfinite(output.cameraOrigin.z);
	}

	CameraOverrideScope::CameraOverrideScope(
		const RE::NiCamera* reflectedCamera,
		const PlanarMirrorMath::Plane& plane,
		float clipBias) noexcept :
		camera(reflectedCamera),
		active(BeginCameraOverride(reflectedCamera, plane, clipBias))
	{}

	CameraOverrideScope::~CameraOverrideScope()
	{
		if (active)
			EndCameraOverride(camera);
	}

	RenderTarget::~RenderTarget()
	{
		Release();
	}

	bool RenderTarget::Create(
		ID3D11Device* device,
		std::uint32_t requestedPerEyeWidth,
		std::uint32_t requestedHeight,
		DXGI_FORMAT colorFormat,
		bool createMaterialCaptureResources,
		bool stereoSideBySide,
		bool createMaterialProperties,
		bool createNativeLitOverlayResources,
		bool createDurableMaterialEnvironment,
		bool createSplitOverlayDepth,
		bool createMaterialHistory,
		bool captureNativeMaterialProperties) noexcept
	{
		const std::uint32_t requestedEyeCount = stereoSideBySide ? 2u : 1u;
		if (!device || bound || (createMaterialProperties && !createMaterialCaptureResources) ||
			(createMaterialHistory && !createMaterialCaptureResources) ||
			(createDurableMaterialEnvironment &&
				(!createMaterialCaptureResources || !createMaterialProperties || !createSplitOverlayDepth)) ||
			requestedPerEyeWidth == 0 || requestedHeight == 0 ||
			requestedPerEyeWidth > kMaximumTargetDimension / requestedEyeCount ||
			requestedHeight > kMaximumTargetDimension)
			return false;
		const std::uint32_t requestedWidth = requestedPerEyeWidth * requestedEyeCount;

		D3D11_TEXTURE2D_DESC colorDescription{};
		colorDescription.Width = requestedWidth;
		colorDescription.Height = requestedHeight;
		colorDescription.MipLevels = 1;
		colorDescription.ArraySize = 1;
		colorDescription.Format = colorFormat;
		colorDescription.SampleDesc.Count = 1;
		colorDescription.Usage = D3D11_USAGE_DEFAULT;
		const bool createColorUAV = createMaterialCaptureResources || createNativeLitOverlayResources;
		colorDescription.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE |
			(createColorUAV ? D3D11_BIND_UNORDERED_ACCESS : 0u);

		Microsoft::WRL::ComPtr<ID3D11Texture2D> newColorTexture;
		Microsoft::WRL::ComPtr<ID3D11RenderTargetView> newColorRTV;
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> newColorSRV;
		Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> newColorUAV;
		if (FAILED(device->CreateTexture2D(&colorDescription, nullptr, &newColorTexture)) ||
			FAILED(device->CreateRenderTargetView(newColorTexture.Get(), nullptr, &newColorRTV)) ||
			FAILED(device->CreateShaderResourceView(newColorTexture.Get(), nullptr, &newColorSRV)))
			return false;
		if (createColorUAV &&
			FAILED(device->CreateUnorderedAccessView(newColorTexture.Get(), nullptr, &newColorUAV)))
			return false;
		D3D11_FEATURE_DATA_FORMAT_SUPPORT2 colorFormatSupport2{};
		colorFormatSupport2.InFormat = colorFormat;
		const bool newColorTypedUAVLoadSupported = createColorUAV &&
			SUCCEEDED(device->CheckFeatureSupport(
				D3D11_FEATURE_FORMAT_SUPPORT2,
				std::addressof(colorFormatSupport2),
				sizeof(colorFormatSupport2))) &&
			(colorFormatSupport2.OutFormatSupport2 &
				D3D11_FORMAT_SUPPORT2_UAV_TYPED_LOAD) != 0;

		Microsoft::WRL::ComPtr<ID3D11Texture2D> newMaterialDiffuseTexture;
		Microsoft::WRL::ComPtr<ID3D11RenderTargetView> newMaterialDiffuseRTV;
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> newMaterialDiffuseSRV;
		Microsoft::WRL::ComPtr<ID3D11Texture2D> newMaterialNormalTexture;
		Microsoft::WRL::ComPtr<ID3D11RenderTargetView> newMaterialNormalRTV;
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> newMaterialNormalSRV;
		Microsoft::WRL::ComPtr<ID3D11Texture2D> newMaterialPropertiesTexture;
		Microsoft::WRL::ComPtr<ID3D11RenderTargetView> newMaterialPropertiesRTV;
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> newMaterialPropertiesSRV;
		if (createMaterialCaptureResources) {
			D3D11_TEXTURE2D_DESC materialDescription = colorDescription;
			materialDescription.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
			if (FAILED(device->CreateTexture2D(&materialDescription, nullptr, &newMaterialDiffuseTexture)) ||
				FAILED(device->CreateRenderTargetView(
					newMaterialDiffuseTexture.Get(), nullptr, &newMaterialDiffuseRTV)) ||
				FAILED(device->CreateShaderResourceView(
					newMaterialDiffuseTexture.Get(), nullptr, &newMaterialDiffuseSRV)) ||
				FAILED(device->CreateTexture2D(&materialDescription, nullptr, &newMaterialNormalTexture)) ||
				FAILED(device->CreateRenderTargetView(
					newMaterialNormalTexture.Get(), nullptr, &newMaterialNormalRTV)) ||
				FAILED(device->CreateShaderResourceView(
					newMaterialNormalTexture.Get(), nullptr, &newMaterialNormalSRV)) ||
				(createMaterialProperties &&
					(FAILED(device->CreateTexture2D(
						&materialDescription, nullptr, &newMaterialPropertiesTexture)) ||
					FAILED(device->CreateRenderTargetView(
						newMaterialPropertiesTexture.Get(), nullptr, &newMaterialPropertiesRTV)) ||
					FAILED(device->CreateShaderResourceView(
						newMaterialPropertiesTexture.Get(), nullptr, &newMaterialPropertiesSRV)))))
				return false;
		}

		Microsoft::WRL::ComPtr<ID3D11Texture2D> newMaterialEmissiveTexture;
		Microsoft::WRL::ComPtr<ID3D11RenderTargetView> newMaterialEmissiveRTV;
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> newMaterialEmissiveSRV;
		if (captureNativeMaterialProperties && createMaterialCaptureResources && createMaterialProperties) {
			D3D11_TEXTURE2D_DESC emissiveDescription = colorDescription;
			
			emissiveDescription.Format = DXGI_FORMAT_R11G11B10_FLOAT;
			emissiveDescription.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
			if (FAILED(device->CreateTexture2D(&emissiveDescription, nullptr, &newMaterialEmissiveTexture)) ||
				FAILED(device->CreateRenderTargetView(
					newMaterialEmissiveTexture.Get(), nullptr, &newMaterialEmissiveRTV)) ||
				FAILED(device->CreateShaderResourceView(
					newMaterialEmissiveTexture.Get(), nullptr, &newMaterialEmissiveSRV))) {
				newMaterialEmissiveSRV.Reset();
				newMaterialEmissiveRTV.Reset();
				newMaterialEmissiveTexture.Reset();
			}
		}

		D3D11_TEXTURE2D_DESC depthDescription = colorDescription;
		depthDescription.Format = DXGI_FORMAT_R24G8_TYPELESS;
		depthDescription.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
		Microsoft::WRL::ComPtr<ID3D11Texture2D> newDepthTexture;
		Microsoft::WRL::ComPtr<ID3D11DepthStencilView> newDepthDSV;
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> newDepthSRV;
		if (FAILED(device->CreateTexture2D(&depthDescription, nullptr, &newDepthTexture)))
			return false;

		D3D11_DEPTH_STENCIL_VIEW_DESC depthViewDescription{};
		depthViewDescription.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
		depthViewDescription.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
		if (FAILED(device->CreateDepthStencilView(newDepthTexture.Get(), &depthViewDescription, &newDepthDSV)))
			return false;

		D3D11_SHADER_RESOURCE_VIEW_DESC depthResourceDescription{};
		depthResourceDescription.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
		depthResourceDescription.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		depthResourceDescription.Texture2D.MipLevels = 1;
		if (FAILED(device->CreateShaderResourceView(newDepthTexture.Get(), &depthResourceDescription, &newDepthSRV)))
			return false;

		Microsoft::WRL::ComPtr<ID3D11Texture2D> newOverlayDepthTexture;
		Microsoft::WRL::ComPtr<ID3D11DepthStencilView> newOverlayDepthDSV;
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> newOverlayDepthSRV;
		Microsoft::WRL::ComPtr<ID3D11Texture2D> newEnvironmentColorTexture;
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> newEnvironmentColorSRV;
		Microsoft::WRL::ComPtr<ID3D11Texture2D> newEnvironmentDepthTexture;
		Microsoft::WRL::ComPtr<ID3D11Query> newMaterialOverlayCoverageQuery;
		if (createMaterialCaptureResources && createSplitOverlayDepth &&
			(FAILED(device->CreateTexture2D(
				&depthDescription, nullptr, &newOverlayDepthTexture)) ||
			FAILED(device->CreateDepthStencilView(
				newOverlayDepthTexture.Get(), &depthViewDescription, &newOverlayDepthDSV)) ||
			FAILED(device->CreateShaderResourceView(
				newOverlayDepthTexture.Get(), &depthResourceDescription, &newOverlayDepthSRV)))) {
			return false;
		}
		if (createMaterialCaptureResources) {
			D3D11_QUERY_DESC coverageQueryDescription{};
			coverageQueryDescription.Query = D3D11_QUERY_OCCLUSION;
			if (FAILED(device->CreateQuery(
					&coverageQueryDescription, &newMaterialOverlayCoverageQuery)))
				return false;
		}
		if (createDurableMaterialEnvironment) {
			D3D11_TEXTURE2D_DESC environmentColorDescription = colorDescription;
			environmentColorDescription.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			D3D11_TEXTURE2D_DESC environmentDepthDescription = depthDescription;
			environmentDepthDescription.BindFlags = 0;
			if (FAILED(device->CreateTexture2D(
					&environmentColorDescription, nullptr, &newEnvironmentColorTexture)) ||
				FAILED(device->CreateShaderResourceView(
					newEnvironmentColorTexture.Get(), nullptr, &newEnvironmentColorSRV)) ||
				FAILED(device->CreateTexture2D(
					&environmentDepthDescription, nullptr, &newEnvironmentDepthTexture)))
				return false;
		}

		Microsoft::WRL::ComPtr<ID3D11Texture2D> newHistoryCandidate, newHistoryCommitted;
		Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> newHistoryUAV, newHistoryCommittedUAV;
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> newHistorySRV, newHistoryCandidateSRV;
		if (createMaterialHistory) {
			auto description = colorDescription;
			description.Format = DXGI_FORMAT_R32_UINT;
			description.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
			if (FAILED(device->CreateTexture2D(&description, nullptr, &newHistoryCandidate)) ||
				FAILED(device->CreateUnorderedAccessView(newHistoryCandidate.Get(), nullptr, &newHistoryUAV)) ||
				FAILED(device->CreateShaderResourceView(newHistoryCandidate.Get(), nullptr, &newHistoryCandidateSRV)))
				return false;
			if (FAILED(device->CreateTexture2D(&description, nullptr, &newHistoryCommitted)) ||
				FAILED(device->CreateShaderResourceView(newHistoryCommitted.Get(), nullptr, &newHistorySRV)) ||
				FAILED(device->CreateUnorderedAccessView(newHistoryCommitted.Get(), nullptr, &newHistoryCommittedUAV)))
				return false;
		}

		Release();
		historyCandidate = std::move(newHistoryCandidate);
		historyCommitted = std::move(newHistoryCommitted);
		historyCandidateUAV = std::move(newHistoryUAV);
		historyCommittedUAV = std::move(newHistoryCommittedUAV);
		historyCandidateSRV = std::move(newHistoryCandidateSRV);
		historyCommittedSRV = std::move(newHistorySRV);
		colorTexture = std::move(newColorTexture);
		colorRTV = std::move(newColorRTV);
		colorSRV = std::move(newColorSRV);
		colorUAV = std::move(newColorUAV);
		materialDiffuseTexture = std::move(newMaterialDiffuseTexture);
		materialDiffuseRTV = std::move(newMaterialDiffuseRTV);
		materialDiffuseSRV = std::move(newMaterialDiffuseSRV);
		materialNormalTexture = std::move(newMaterialNormalTexture);
		materialNormalRTV = std::move(newMaterialNormalRTV);
		materialNormalSRV = std::move(newMaterialNormalSRV);
		materialPropertiesTexture = std::move(newMaterialPropertiesTexture);
		materialPropertiesRTV = std::move(newMaterialPropertiesRTV);
		materialPropertiesSRV = std::move(newMaterialPropertiesSRV);
		materialEmissiveTexture = std::move(newMaterialEmissiveTexture);
		materialEmissiveRTV = std::move(newMaterialEmissiveRTV);
		materialEmissiveSRV = std::move(newMaterialEmissiveSRV);
		depthTexture = std::move(newDepthTexture);
		depthDSV = std::move(newDepthDSV);
		depthSRV = std::move(newDepthSRV);
		overlayDepthTexture = std::move(newOverlayDepthTexture);
		overlayDepthDSV = std::move(newOverlayDepthDSV);
		overlayDepthSRV = std::move(newOverlayDepthSRV);
		environmentColorTexture = std::move(newEnvironmentColorTexture);
		environmentColorSRV = std::move(newEnvironmentColorSRV);
		environmentDepthTexture = std::move(newEnvironmentDepthTexture);
		materialOverlayCoverageQuery = std::move(newMaterialOverlayCoverageQuery);
		materialOverlayCoverageQueryActive = false;
		materialOverlayCoverageQueryPending = false;
		width = requestedWidth;
		height = requestedHeight;
		eyeCount = requestedEyeCount;
		perEyeWidth = requestedPerEyeWidth;
		colorTypedUAVLoadSupported = newColorTypedUAVLoadSupported;
		materialPropertiesEnabled = createMaterialProperties;
		nativeMaterialProperties = captureNativeMaterialProperties;
		materialEmissiveEnabled = createMaterialProperties && materialEmissiveRTV && materialEmissiveSRV;
		durableMaterialEnvironmentEnabled = createDurableMaterialEnvironment;
		environmentSnapshotValid = false;
		SetDebugName(colorTexture.Get(), "RealisticReflectionsMirrors.PlanarMirror.Color");
		SetDebugName(colorRTV.Get(), "RealisticReflectionsMirrors.PlanarMirror.ColorRTV");
		SetDebugName(colorSRV.Get(), "RealisticReflectionsMirrors.PlanarMirror.ColorSRV");
		SetDebugName(colorUAV.Get(), "RealisticReflectionsMirrors.PlanarMirror.ColorUAV");
		SetDebugName(materialDiffuseTexture.Get(), "RealisticReflectionsMirrors.PlanarMirror.MaterialDiffuse");
		SetDebugName(materialDiffuseRTV.Get(), "RealisticReflectionsMirrors.PlanarMirror.MaterialDiffuseRTV");
		SetDebugName(materialDiffuseSRV.Get(), "RealisticReflectionsMirrors.PlanarMirror.MaterialDiffuseSRV");
		SetDebugName(materialNormalTexture.Get(), "RealisticReflectionsMirrors.PlanarMirror.MaterialNormal");
		SetDebugName(materialNormalRTV.Get(), "RealisticReflectionsMirrors.PlanarMirror.MaterialNormalRTV");
		SetDebugName(materialNormalSRV.Get(), "RealisticReflectionsMirrors.PlanarMirror.MaterialNormalSRV");
		SetDebugName(materialPropertiesTexture.Get(), "RealisticReflectionsMirrors.PlanarMirror.MaterialProperties");
		SetDebugName(materialPropertiesRTV.Get(), "RealisticReflectionsMirrors.PlanarMirror.MaterialPropertiesRTV");
		SetDebugName(materialPropertiesSRV.Get(), "RealisticReflectionsMirrors.PlanarMirror.MaterialPropertiesSRV");
		SetDebugName(materialEmissiveTexture.Get(), "RealisticReflectionsMirrors.PlanarMirror.MaterialEmissive");
		SetDebugName(materialEmissiveRTV.Get(), "RealisticReflectionsMirrors.PlanarMirror.MaterialEmissiveRTV");
		SetDebugName(materialEmissiveSRV.Get(), "RealisticReflectionsMirrors.PlanarMirror.MaterialEmissiveSRV");
		SetDebugName(depthTexture.Get(), "RealisticReflectionsMirrors.PlanarMirror.Depth");
		SetDebugName(depthDSV.Get(), "RealisticReflectionsMirrors.PlanarMirror.DepthDSV");
		SetDebugName(depthSRV.Get(), "RealisticReflectionsMirrors.PlanarMirror.DepthSRV");
		SetDebugName(overlayDepthTexture.Get(), "RealisticReflectionsMirrors.PlanarMirror.OverlayDepth");
		SetDebugName(overlayDepthDSV.Get(), "RealisticReflectionsMirrors.PlanarMirror.OverlayDepthDSV");
		SetDebugName(overlayDepthSRV.Get(), "RealisticReflectionsMirrors.PlanarMirror.OverlayDepthSRV");
		SetDebugName(
			environmentColorTexture.Get(), "RealisticReflectionsMirrors.PlanarMirror.ImmutableEnvironmentColor");
		SetDebugName(
			environmentColorSRV.Get(), "RealisticReflectionsMirrors.PlanarMirror.ImmutableEnvironmentColorSRV");
		SetDebugName(
			environmentDepthTexture.Get(), "RealisticReflectionsMirrors.PlanarMirror.ImmutableEnvironmentDepth");
		SetDebugName(
			materialOverlayCoverageQuery.Get(),
			"RealisticReflectionsMirrors.PlanarMirror.MaterialOverlayCoverageQuery");
		return true;
	}

	void RenderTarget::Release(ID3D11DeviceContext* context) noexcept
	{
		tileLightSRV.Reset(); tileLightUAV.Reset(); tileLightBuffer.Reset();
		tileLightWidth = tileLightHeight = 0;
		InvalidateMaterialHistory();
		historyCandidateTick = 0;
		historyCandidateResolved = false;
		historyCandidateUAV.Reset();
		historyCommittedUAV.Reset();
		historyCandidateSRV.Reset();
		historyCommittedSRV.Reset();
		historyCandidate.Reset();
		historyCommitted.Reset();
		if (materialOverlayCoverageQueryActive && context && materialOverlayCoverageQuery)
			context->End(materialOverlayCoverageQuery.Get());
		materialOverlayCoverageQueryActive = false;
		materialOverlayCoverageQueryPending = false;
		materialCoverageProofRetainsTarget = false;
		materialCoverageProofResultReady = false;
		materialCaptureCandidateResolved = false;
		materialCoverageProofSerial = 0;
		materialCoverageProofSamples = 0;
		materialCoverageProofResult = S_FALSE;
		if (bound && context)
			End(context);
		else if (bound) {
			bound = false;
			ResetSavedState();
		}
		materialOverlayCoverageQuery.Reset();
		environmentDepthTexture.Reset();
		environmentColorSRV.Reset();
		environmentColorTexture.Reset();
		overlayDepthSRV.Reset();
		overlayDepthDSV.Reset();
		overlayDepthTexture.Reset();
		depthSRV.Reset();
		depthDSV.Reset();
		depthTexture.Reset();
		materialEmissiveSRV.Reset();
		materialEmissiveRTV.Reset();
		materialEmissiveTexture.Reset();
		materialPropertiesSRV.Reset();
		materialPropertiesRTV.Reset();
		materialPropertiesTexture.Reset();
		materialNormalSRV.Reset();
		materialNormalRTV.Reset();
		materialNormalTexture.Reset();
		materialDiffuseSRV.Reset();
		materialDiffuseRTV.Reset();
		materialDiffuseTexture.Reset();
		colorUAV.Reset();
		colorSRV.Reset();
		colorRTV.Reset();
		colorTexture.Reset();
		width = 0;
		height = 0;
		eyeCount = 0;
		perEyeWidth = 0;
		colorTypedUAVLoadSupported = false;
		materialPropertiesEnabled = false;
		nativeMaterialProperties = false;
		materialEmissiveEnabled = false;
		durableMaterialEnvironmentEnabled = false;
		environmentSnapshotValid = false;
	}

	bool RenderTarget::SwapUnbound(RenderTarget& other) noexcept
	{
		if (bound || other.bound || materialOverlayCoverageQueryActive || other.materialOverlayCoverageQueryActive ||
			materialOverlayCoverageQueryPending || other.materialOverlayCoverageQueryPending) return false;
		RenderTarget temporary(std::move(*this));
		*this = std::move(other);
		other = std::move(temporary);
		InvalidateMaterialHistory(); other.InvalidateMaterialHistory();
		ClearMaterialCoverageProofResult(); other.ClearMaterialCoverageProofResult();
		materialCaptureCandidateResolved = other.materialCaptureCandidateResolved = false;
		environmentSnapshotValid = other.environmentSnapshotValid = false;
		return true;
	}

	bool RenderTarget::UsesDevice(ID3D11Device* device) const noexcept
	{
		if (!device || !colorTexture)
			return false;
		Microsoft::WRL::ComPtr<ID3D11Device> owner;
		colorTexture->GetDevice(&owner);
		return Util::D3DDevicesMatch(owner.Get(), device);
	}

	bool RenderTarget::WritableColorBundleCompatible(
		const WritableColorBundle& bundle) const noexcept
	{
		if (bound || !colorTexture || !colorRTV || !colorSRV || !colorUAV ||
			!bundle.Complete() || bundle.texture.Get() == colorTexture.Get())
			return false;

		Microsoft::WRL::ComPtr<ID3D11Device> device;
		colorTexture->GetDevice(device.GetAddressOf());
		if (!device || !DeviceChildUsesDevice(colorRTV.Get(), device.Get()) ||
			!DeviceChildUsesDevice(colorSRV.Get(), device.Get()) ||
			!DeviceChildUsesDevice(colorUAV.Get(), device.Get()) ||
			!DeviceChildUsesDevice(bundle.texture.Get(), device.Get()) ||
			!DeviceChildUsesDevice(bundle.rtv.Get(), device.Get()) ||
			!DeviceChildUsesDevice(bundle.srv.Get(), device.Get()) ||
			!DeviceChildUsesDevice(bundle.uav.Get(), device.Get()) ||
			!ViewUsesResource(colorRTV.Get(), colorTexture.Get()) ||
			!ViewUsesResource(colorSRV.Get(), colorTexture.Get()) ||
			!ViewUsesResource(colorUAV.Get(), colorTexture.Get()) ||
			!ViewUsesResource(bundle.rtv.Get(), bundle.texture.Get()) ||
			!ViewUsesResource(bundle.srv.Get(), bundle.texture.Get()) ||
			!ViewUsesResource(bundle.uav.Get(), bundle.texture.Get()))
			return false;

		D3D11_TEXTURE2D_DESC currentTextureDescription{};
		D3D11_TEXTURE2D_DESC replacementTextureDescription{};
		colorTexture->GetDesc(std::addressof(currentTextureDescription));
		bundle.texture->GetDesc(std::addressof(replacementTextureDescription));
		if (!TextureDescriptionsMatch(
				currentTextureDescription, replacementTextureDescription))
			return false;

		D3D11_RENDER_TARGET_VIEW_DESC currentRTVDescription{};
		D3D11_RENDER_TARGET_VIEW_DESC replacementRTVDescription{};
		D3D11_SHADER_RESOURCE_VIEW_DESC currentSRVDescription{};
		D3D11_SHADER_RESOURCE_VIEW_DESC replacementSRVDescription{};
		D3D11_UNORDERED_ACCESS_VIEW_DESC currentUAVDescription{};
		D3D11_UNORDERED_ACCESS_VIEW_DESC replacementUAVDescription{};
		colorRTV->GetDesc(std::addressof(currentRTVDescription));
		bundle.rtv->GetDesc(std::addressof(replacementRTVDescription));
		colorSRV->GetDesc(std::addressof(currentSRVDescription));
		bundle.srv->GetDesc(std::addressof(replacementSRVDescription));
		colorUAV->GetDesc(std::addressof(currentUAVDescription));
		bundle.uav->GetDesc(std::addressof(replacementUAVDescription));
		return ViewDescriptionsMatch(currentRTVDescription, replacementRTVDescription) &&
			ViewDescriptionsMatch(currentSRVDescription, replacementSRVDescription) &&
			ViewDescriptionsMatch(currentUAVDescription, replacementUAVDescription);
	}

	bool RenderTarget::CreateCompatibleWritableColorBundle(
		ID3D11Device* device,
		WritableColorBundle& bundle) const noexcept
	{
		bundle = {};
		if (!device || bound || !colorTexture || !colorRTV || !colorSRV || !colorUAV ||
			!UsesDevice(device))
			return false;

		D3D11_TEXTURE2D_DESC textureDescription{};
		D3D11_RENDER_TARGET_VIEW_DESC rtvDescription{};
		D3D11_SHADER_RESOURCE_VIEW_DESC srvDescription{};
		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDescription{};
		colorTexture->GetDesc(std::addressof(textureDescription));
		colorRTV->GetDesc(std::addressof(rtvDescription));
		colorSRV->GetDesc(std::addressof(srvDescription));
		colorUAV->GetDesc(std::addressof(uavDescription));

		WritableColorBundle replacement{};
		if (FAILED(device->CreateTexture2D(
				std::addressof(textureDescription), nullptr,
				replacement.texture.GetAddressOf())) ||
			FAILED(device->CreateRenderTargetView(
				replacement.texture.Get(), std::addressof(rtvDescription),
				replacement.rtv.GetAddressOf())) ||
			FAILED(device->CreateShaderResourceView(
				replacement.texture.Get(), std::addressof(srvDescription),
				replacement.srv.GetAddressOf())) ||
			FAILED(device->CreateUnorderedAccessView(
				replacement.texture.Get(), std::addressof(uavDescription),
				replacement.uav.GetAddressOf())) ||
			!WritableColorBundleCompatible(replacement))
			return false;

		SetDebugName(
			replacement.texture.Get(), "RealisticReflectionsMirrors.PlanarMirror.RotatingColor");
		SetDebugName(
			replacement.rtv.Get(), "RealisticReflectionsMirrors.PlanarMirror.RotatingColorRTV");
		SetDebugName(
			replacement.srv.Get(), "RealisticReflectionsMirrors.PlanarMirror.RotatingColorSRV");
		SetDebugName(
			replacement.uav.Get(), "RealisticReflectionsMirrors.PlanarMirror.RotatingColorUAV");
		bundle = std::move(replacement);
		return true;
	}

	void RenderTarget::ExchangeWritableColorBundle(WritableColorBundle& bundle) noexcept
	{

		std::swap(colorTexture, bundle.texture);
		std::swap(colorRTV, bundle.rtv);
		std::swap(colorSRV, bundle.srv);
		std::swap(colorUAV, bundle.uav);

		materialCaptureCandidateResolved = false;
	}

	bool RenderTarget::Begin(ID3D11DeviceContext* context, const float clearColor[4]) noexcept
	{
		return BeginInternal(context, clearColor, false, false);
	}

	bool RenderTarget::BeginMaterialCapture(
		ID3D11DeviceContext* context,
		const float clearColor[4]) noexcept
	{
		return BeginInternal(context, clearColor, true, false);
	}

	bool RenderTarget::BeginMaterialOverlayCapture(
		ID3D11DeviceContext* context,
		const float clearColor[4]) noexcept
	{
		return BeginInternal(context, clearColor, true, true);
	}

	bool RenderTarget::BeginInternal(
		ID3D11DeviceContext* context,
		const float clearColor[4],
		bool materialCapture,
		bool preserveForwardColor) noexcept
	{
		const bool overlayReady = MaterialOverlayCaptureReady() ||
			MaterialPropertiesOverlayCaptureReady();
		if (!context || !clearColor || bound || !Ready() ||
			(materialCapture && !MaterialCaptureReady()) ||
			(preserveForwardColor && (!materialCapture || !overlayReady ||
				(durableMaterialEnvironmentEnabled && !DurableMaterialEnvironmentReady()))))
			return false;

		if (materialCoverageProofRetainsTarget || materialOverlayCoverageQueryActive)
			return false;
		if (materialOverlayCoverageQueryPending) {
			std::uint64_t abandonedSamples = 0;
			const HRESULT abandonedResult = context->GetData(
				materialOverlayCoverageQuery.Get(), std::addressof(abandonedSamples),
				sizeof(abandonedSamples), D3D11_ASYNC_GETDATA_DONOTFLUSH);
			if (abandonedResult == S_FALSE)
				return false;
			materialOverlayCoverageQueryPending = false;
			materialCoverageProofSerial = 0;
			ClearMaterialCoverageProofResult();
			if (FAILED(abandonedResult))
				return false;
		} else if (materialCoverageProofResultReady) {
			materialCoverageProofSerial = 0;
			ClearMaterialCoverageProofResult();
		}

		if (preserveForwardColor && durableMaterialEnvironmentEnabled) {
			context->CopyResource(colorTexture.Get(), environmentColorTexture.Get());
			context->CopyResource(depthTexture.Get(), environmentDepthTexture.Get());
		}
		if (!preserveForwardColor)
			materialCaptureCandidateResolved = false;
		if (materialCapture && !preserveForwardColor && durableMaterialEnvironmentEnabled)
			environmentSnapshotValid = false;

		ID3D11RenderTargetView* rawRTVs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]{};
		ID3D11DepthStencilView* rawDSV = nullptr;
		context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, rawRTVs, &rawDSV);
		for (std::size_t index = 0; index < savedRTVs.size(); ++index)
			savedRTVs[index].Attach(rawRTVs[index]);
		savedDSV.Attach(rawDSV);

		savedViewportCount = static_cast<std::uint32_t>(savedViewports.size());
		context->RSGetViewports(&savedViewportCount, savedViewports.data());
		savedScissorRectCount = static_cast<std::uint32_t>(savedScissorRects.size());
		context->RSGetScissorRects(&savedScissorRectCount, savedScissorRects.data());
		bindPhase = preserveForwardColor ? BindPhase::kMaterialOverlayDepth :
			(materialCapture ? BindPhase::kMaterialDepth : BindPhase::kFinalColor);
		bound = true;

		if (preserveForwardColor)
			context->CopyResource(overlayDepthTexture.Get(), depthTexture.Get());
		Rebind(context);
		if (!preserveForwardColor)
			context->ClearRenderTargetView(colorRTV.Get(), clearColor);
		if (materialCapture) {

			const float diffuseNoWriterClear[4] = { -1.0f, -1.0f, -1.0f, -1.0f };
			const float normalClear[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
			const float propertiesClear[4] = { 0.0f, 0.0f, 0.0f, -1.0f };
			context->ClearRenderTargetView(materialDiffuseRTV.Get(), diffuseNoWriterClear);
			context->ClearRenderTargetView(materialNormalRTV.Get(), normalClear);
			if (materialPropertiesRTV)
				context->ClearRenderTargetView(materialPropertiesRTV.Get(), propertiesClear);
			if (materialEmissiveEnabled && materialEmissiveRTV) {
				const float emissiveClear[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
				context->ClearRenderTargetView(materialEmissiveRTV.Get(), emissiveClear);
			}
		}
		if (!preserveForwardColor) {
			context->ClearDepthStencilView(
				depthDSV.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
		}
		return true;
	}

	void RenderTarget::BindMaterialGBuffer(ID3D11DeviceContext* context) noexcept
	{
		if (!context || !bound || !MaterialCaptureReady() ||
			(bindPhase != BindPhase::kMaterialDepth &&
				bindPhase != BindPhase::kMaterialOverlayDepth))
			return;
		bindPhase = bindPhase == BindPhase::kMaterialOverlayDepth ?
			BindPhase::kMaterialOverlayGBuffer : BindPhase::kMaterialGBuffer;
		Rebind(context);
	}

	bool RenderTarget::BeginMaterialOverlayCoverageProof(
		ID3D11DeviceContext* context) noexcept
	{
		return BeginMaterialCoverageProofInternal(context, false, nullptr);
	}

	bool RenderTarget::BeginRetainedMaterialCoverageProof(
		ID3D11DeviceContext* context,
		std::uint64_t& serial) noexcept
	{
		serial = 0;
		return BeginMaterialCoverageProofInternal(context, true, std::addressof(serial));
	}

	bool RenderTarget::BeginMaterialCoverageProofInternal(
		ID3D11DeviceContext* context,
		bool retainTarget,
		std::uint64_t* serial) noexcept
	{
		if (serial)
			*serial = 0;
		if (!context || !materialOverlayCoverageQuery || materialOverlayCoverageQueryActive ||
			materialCoverageProofRetainsTarget || materialCoverageProofResultReady ||
			!bound || (bindPhase != BindPhase::kMaterialOverlayDepth &&
				bindPhase != BindPhase::kMaterialGBuffer))
			return false;
		if (materialOverlayCoverageQueryPending) {
			std::uint64_t staleSamples = 0;
			const HRESULT staleResult = context->GetData(
					materialOverlayCoverageQuery.Get(), &staleSamples, sizeof(staleSamples),
					D3D11_ASYNC_GETDATA_DONOTFLUSH);
			if (staleResult != S_OK)
				return false;
			materialOverlayCoverageQueryPending = false;
		}
		if (++materialCoverageProofNextSerial == 0)
			++materialCoverageProofNextSerial;
		materialCoverageProofSerial = materialCoverageProofNextSerial;
		materialCoverageProofRetainsTarget = retainTarget;
		ClearMaterialCoverageProofResult();
		context->Begin(materialOverlayCoverageQuery.Get());
		materialOverlayCoverageQueryActive = true;
		if (serial)
			*serial = materialCoverageProofSerial;
		return true;
	}

	bool RenderTarget::EndMaterialOverlayCoverageProof(
		ID3D11DeviceContext* context) noexcept
	{
		if (!context || !materialOverlayCoverageQuery ||
			!materialOverlayCoverageQueryActive)
			return false;
		context->End(materialOverlayCoverageQuery.Get());
		materialOverlayCoverageQueryActive = false;
		materialOverlayCoverageQueryPending = true;
		return true;
	}

	MaterialCoverageProofResult RenderTarget::PollMaterialCoverageProof(
		ID3D11DeviceContext* context) noexcept
	{
		MaterialCoverageProofResult proof{};
		proof.serial = materialCoverageProofSerial;
		if (!context || !materialOverlayCoverageQuery || materialOverlayCoverageQueryActive)
			return proof;
		if (materialCoverageProofResultReady) {
			proof.samples = materialCoverageProofSamples;
			proof.result = materialCoverageProofResult;
			proof.status = materialCoverageProofResult == S_OK ?
				(materialCoverageProofSamples != 0 ? MaterialCoverageProofStatus::kReadyNonZero :
					MaterialCoverageProofStatus::kReadyZero) : MaterialCoverageProofStatus::kFault;
			return proof;
		}
		if (!materialOverlayCoverageQueryPending)
			return proof;

		std::uint64_t samples = 0;
		const HRESULT queryResult = context->GetData(
			materialOverlayCoverageQuery.Get(), std::addressof(samples), sizeof(samples),
			D3D11_ASYNC_GETDATA_DONOTFLUSH);
		proof.result = queryResult;
		if (queryResult == S_FALSE) {
			proof.status = MaterialCoverageProofStatus::kPending;
			return proof;
		}
		materialOverlayCoverageQueryPending = false;
		materialCoverageProofResultReady = true;
		materialCoverageProofResult = queryResult;
		materialCoverageProofSamples = queryResult == S_OK ? samples : 0;
		proof.samples = materialCoverageProofSamples;
		proof.status = queryResult == S_OK ?
			(samples != 0 ? MaterialCoverageProofStatus::kReadyNonZero :
				MaterialCoverageProofStatus::kReadyZero) : MaterialCoverageProofStatus::kFault;
		return proof;
	}

	bool RenderTarget::ReadMaterialCoverageProof(
		ID3D11DeviceContext* context,
		std::uint64_t& samples) noexcept
	{
		samples = 0;
		if (!context || !materialOverlayCoverageQuery || materialOverlayCoverageQueryActive ||
			materialCoverageProofRetainsTarget)
			return false;
		if (materialCoverageProofResultReady) {
			const bool ready = materialCoverageProofResult == S_OK;
			if (ready)
				samples = materialCoverageProofSamples;
			materialCoverageProofSerial = 0;
			ClearMaterialCoverageProofResult();
			return ready;
		}
		if (!materialOverlayCoverageQueryPending)
			return false;

		constexpr auto kCoverageWait = std::chrono::milliseconds(4);
		const auto deadline = std::chrono::steady_clock::now() + kCoverageWait;
		HRESULT queryResult = S_FALSE;
		do {
			queryResult = context->GetData(
				materialOverlayCoverageQuery.Get(), &samples, sizeof(samples), 0);
			if (queryResult != S_FALSE)
				break;
		} while (std::chrono::steady_clock::now() < deadline);
		if (queryResult != S_OK) {
			samples = 0;
			return false;
		}
		materialOverlayCoverageQueryPending = false;
		materialCoverageProofSerial = 0;
		return true;
	}

	bool RenderTarget::ResolveMaterialCaptureCandidate(
		ID3D11DeviceContext* context,
		ID3D11ComputeShader* resolveShader,
		ID3D11Buffer* lightingConstants,
		ID3D11ShaderResourceView* backgroundCube,
		ID3D11SamplerState* backgroundSampler,
		ID3D11Buffer* backgroundConstants,
		ID3D11ShaderResourceView* previousMirrorColor,
		const MirrorShadowMaps::Bindings* shadows,
		ID3D11ComputeShader* tileCullShader, MirrorNativeLighting::Bindings nativeLighting) noexcept
	{
		materialCaptureCandidateResolved = false;
		historyCandidateResolved = false;
		if (!context || !resolveShader || !bound || !MaterialCaptureReady() ||
			bindPhase != BindPhase::kMaterialGBuffer)
			return false;
		const bool anyBackgroundBinding = backgroundCube || backgroundSampler || backgroundConstants;
		if (anyBackgroundBinding && (!backgroundCube || !backgroundSampler || !backgroundConstants))
			return false;

		if (previousMirrorColor && ViewUsesResource(previousMirrorColor, colorTexture.Get()))
			return false;

		context->OMSetRenderTargets(0, nullptr, nullptr);
		ScopedComputeState savedState(context);
		ID3D11ShaderResourceView* resources[7] = {
			materialDiffuseSRV.Get(), materialNormalSRV.Get(), materialPropertiesSRV.Get(), depthSRV.Get(),
			backgroundCube, previousMirrorColor, historyCommittedTick ? historyCommittedSRV.Get() : nullptr
		};
		ID3D11UnorderedAccessView* outputs[2] = {colorUAV.Get(), historyCandidateUAV.Get()};

		ID3D11ShaderResourceView* tileLights = nullptr;
		if (tileCullShader && lightingConstants && EnsureTileLightBuffer()) {
			const std::uint32_t tilesX = (width + kTileLightSize - 1u) / kTileLightSize;
			const std::uint32_t tilesY = (height + kTileLightSize - 1u) / kTileLightSize;
			ID3D11ShaderResourceView* cullDepth[4]{ nullptr, nullptr, nullptr, depthSRV.Get() };
			ID3D11UnorderedAccessView* cullOutput[1]{ tileLightUAV.Get() };
			context->CSSetShader(tileCullShader, nullptr, 0);
			context->CSSetConstantBuffers(0, 1, &lightingConstants);
			context->CSSetShaderResources(0, 4, cullDepth);
			context->CSSetUnorderedAccessViews(0, 1, cullOutput, nullptr);
			context->Dispatch(tilesX, tilesY, 1);
			ID3D11UnorderedAccessView* noOutput[1]{};
			ID3D11ShaderResourceView* noDepth[4]{};
			context->CSSetUnorderedAccessViews(0, 1, noOutput, nullptr);
			context->CSSetShaderResources(0, 4, noDepth);
			tileLights = tileLightSRV.Get();
		}

		context->CSSetShader(resolveShader, nullptr, 0);
		nativeLighting.Bind(context,false);
		context->CSSetShaderResources(0, 7, resources);
		ID3D11ShaderResourceView* emissiveResource = materialEmissiveEnabled ? materialEmissiveSRV.Get() : nullptr;
		context->CSSetShaderResources(30, 1, &emissiveResource);  
		context->CSSetShaderResources(31, 1, &tileLights);  
		const MirrorPrivateSun::Bindings privateSun = shadows ? shadows->privateSun : MirrorPrivateSun::Bindings{};
		privateSun.Compute(context);
		ID3D11Buffer* shadowConstants=shadows && shadows->sampler?shadows->constants:nullptr;
		context->CSSetConstantBuffers(2,1,&shadowConstants);
		if (shadows && shadowConstants && shadows->sampler) {
			context->CSSetShaderResources(7,MirrorShadowMaps::kResources,shadows->resources.data());
			context->CSSetSamplers(1,1,&shadows->sampler);
		}

		ID3D11Buffer* constants[2] = { lightingConstants, backgroundConstants };
		context->CSSetConstantBuffers(0, 2, constants);
		if (anyBackgroundBinding)
			context->CSSetSamplers(0, 1, &backgroundSampler);
		context->CSSetUnorderedAccessViews(0, 2, outputs, nullptr);
		context->Dispatch((width + 7u) / 8u, (height + 7u) / 8u, 1);

		ID3D11ShaderResourceView* nullResources[7]{};
		ID3D11UnorderedAccessView* nullOutputs[2]{};
		ID3D11Buffer* nullConstants[2]{};
		ID3D11SamplerState* nullSampler = nullptr;
		context->CSSetShaderResources(0, 7, nullResources);
		context->CSSetShaderResources(30, 1, nullResources);
		context->CSSetShaderResources(31, 1, nullResources);
		context->CSSetConstantBuffers(0, 2, nullConstants);
		if (anyBackgroundBinding)
			context->CSSetSamplers(0, 1, &nullSampler);
		context->CSSetUnorderedAccessViews(0, 2, nullOutputs, nullptr);
		bindPhase = BindPhase::kMaterialResolved;
		materialCaptureCandidateResolved = true;
		historyCandidateResolved = historyCandidateUAV && historyCandidateTick != 0;
		return true;
	}

	void RenderTarget::BeginMaterialHistory(std::uint64_t tick) noexcept
	{
		historyCandidateTick = tick;
		historyCandidateResolved = false;
	}

	void RenderTarget::InvalidateMaterialHistory() noexcept
	{
		historyCommittedTick = 0;
	}

	void RenderTarget::CommitMaterialHistory(ID3D11DeviceContext* context) noexcept
	{
		if (context && historyCandidateResolved && historyCandidate && historyCommitted) {

			historyCommitted.Swap(historyCandidate);
			historyCommittedSRV.Swap(historyCandidateSRV);
			historyCommittedUAV.Swap(historyCandidateUAV);
			historyCommittedTick = historyCandidateTick;
		} else {
			InvalidateMaterialHistory();
		}
		historyCandidateResolved = false;
	}

	bool RenderTarget::EnsureTileLightBuffer() noexcept
	{
		if (!colorTexture || !width || !height)
			return false;
		
		Microsoft::WRL::ComPtr<ID3D11Device> device;
		colorTexture->GetDevice(device.GetAddressOf());
		if (!device)
			return false;
		if (tileLightBuffer && tileLightWidth == width && tileLightHeight == height)
			return true;
		tileLightBuffer.Reset(); tileLightUAV.Reset(); tileLightSRV.Reset();
		tileLightWidth = tileLightHeight = 0;
		const std::uint32_t tilesX = (width + kTileLightSize - 1u) / kTileLightSize;
		const std::uint32_t tilesY = (height + kTileLightSize - 1u) / kTileLightSize;
		const std::uint64_t tiles = std::uint64_t{ tilesX } * tilesY;
		const std::uint64_t elements = tiles * kTileLightCapacity;

		if (tiles == 0 || elements > (1u << 24))
			return false;
		D3D11_BUFFER_DESC desc{};
		desc.ByteWidth = static_cast<UINT>(elements * sizeof(std::uint32_t));
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
		desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		desc.StructureByteStride = sizeof(std::uint32_t);
		if (FAILED(device->CreateBuffer(&desc, nullptr, tileLightBuffer.ReleaseAndGetAddressOf())))
			return false;
		D3D11_UNORDERED_ACCESS_VIEW_DESC uav{};
		uav.Format = DXGI_FORMAT_UNKNOWN;
		uav.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
		uav.Buffer.NumElements = static_cast<UINT>(elements);
		if (FAILED(device->CreateUnorderedAccessView(tileLightBuffer.Get(), &uav, tileLightUAV.ReleaseAndGetAddressOf())))
			return false;
		D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
		srv.Format = DXGI_FORMAT_UNKNOWN;
		srv.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		srv.Buffer.NumElements = static_cast<UINT>(elements);
		if (FAILED(device->CreateShaderResourceView(tileLightBuffer.Get(), &srv, tileLightSRV.ReleaseAndGetAddressOf())))
			return false;
		tileLightWidth = width; tileLightHeight = height;
		return true;
	}

	bool RenderTarget::ResolveMaterialCapture(
		ID3D11DeviceContext* context,
		ID3D11ComputeShader* resolveShader,
		ID3D11Buffer* lightingConstants,
		ID3D11ShaderResourceView* backgroundCube,
		ID3D11SamplerState* backgroundSampler,
		ID3D11Buffer* backgroundConstants,
		ID3D11ShaderResourceView* previousMirrorColor,
		const MirrorShadowMaps::Bindings* shadows,
		ID3D11ComputeShader* tileCullShader, MirrorNativeLighting::Bindings nativeLighting) noexcept
	{
		if (durableMaterialEnvironmentEnabled && materialCoverageProofRetainsTarget)
			return false;
		if (!ResolveMaterialCaptureCandidate(
				context, resolveShader, lightingConstants, backgroundCube,
				backgroundSampler, backgroundConstants, previousMirrorColor, shadows, tileCullShader, nativeLighting))
			return false;
		if (durableMaterialEnvironmentEnabled) {
			if (!environmentColorTexture || !environmentColorSRV || !environmentDepthTexture)
				return false;
			context->CopyResource(environmentColorTexture.Get(), colorTexture.Get());
			context->CopyResource(environmentDepthTexture.Get(), depthTexture.Get());
			environmentSnapshotValid = true;
		}
		return true;
	}

	bool RenderTarget::CommitRetainedMaterialEnvironment(
		ID3D11DeviceContext* context,
		std::uint64_t serial) noexcept
	{
		if (!context || bound || serial == 0 || serial != materialCoverageProofSerial ||
			!materialCoverageProofRetainsTarget || materialOverlayCoverageQueryActive ||
			materialOverlayCoverageQueryPending || !materialCoverageProofResultReady ||
			materialCoverageProofResult != S_OK || materialCoverageProofSamples == 0 ||
			!materialCaptureCandidateResolved || !DurableMaterialEnvironmentAllocated())
			return false;
		context->CopyResource(environmentColorTexture.Get(), colorTexture.Get());
		context->CopyResource(environmentDepthTexture.Get(), depthTexture.Get());
		environmentSnapshotValid = true;
		return ConsumeRetainedMaterialCoverageProof(serial);
	}

	bool RenderTarget::ConsumeRetainedMaterialCoverageProof(std::uint64_t serial) noexcept
	{
		if (serial == 0 || serial != materialCoverageProofSerial ||
			!materialCoverageProofRetainsTarget || materialOverlayCoverageQueryActive ||
			materialOverlayCoverageQueryPending || !materialCoverageProofResultReady)
			return false;
		materialCoverageProofRetainsTarget = false;
		materialCoverageProofSerial = 0;
		materialCaptureCandidateResolved = false;
		ClearMaterialCoverageProofResult();
		return true;
	}

	bool RenderTarget::DiscardRetainedMaterialCoverageProof(std::uint64_t serial) noexcept
	{
		if (serial == 0 || serial != materialCoverageProofSerial ||
			!materialCoverageProofRetainsTarget)
			return false;
		materialCoverageProofRetainsTarget = false;
		materialCaptureCandidateResolved = false;
		ClearMaterialCoverageProofResult();
		if (!materialOverlayCoverageQueryActive && !materialOverlayCoverageQueryPending)
			materialCoverageProofSerial = 0;
		return true;
	}

	void RenderTarget::ClearMaterialCoverageProofResult() noexcept
	{
		materialCoverageProofResultReady = false;
		materialCoverageProofSamples = 0;
		materialCoverageProofResult = S_FALSE;
	}

	bool RenderTarget::ResolveMaterialOverlayCapture(
		ID3D11DeviceContext* context,
		ID3D11ComputeShader* resolveShader,
		std::uint64_t* depthQualifiedSamples) noexcept
	{
		if (depthQualifiedSamples)
			*depthQualifiedSamples = 0;
		if (!context || !resolveShader || !bound || !MaterialOverlayCaptureReady() ||
			bindPhase != BindPhase::kMaterialOverlayGBuffer ||
			(depthQualifiedSamples && (!materialOverlayCoverageQueryPending ||
				materialOverlayCoverageQueryActive)))
			return false;

		context->OMSetRenderTargets(0, nullptr, nullptr);
		ScopedComputeState savedState(context);
		ID3D11ShaderResourceView* resources[4] = {
			materialDiffuseSRV.Get(), materialNormalSRV.Get(), overlayDepthSRV.Get(), depthSRV.Get()
		};
		ID3D11UnorderedAccessView* output = colorUAV.Get();
		context->CSSetShader(resolveShader, nullptr, 0);
		context->CSSetShaderResources(0, 4, resources);
		context->CSSetUnorderedAccessViews(0, 1, &output, nullptr);
		context->Dispatch((width + 7u) / 8u, (height + 7u) / 8u, 1);

		ID3D11ShaderResourceView* nullResources[4]{};
		ID3D11UnorderedAccessView* nullOutput = nullptr;
		context->CSSetShaderResources(0, 4, nullResources);
		context->CSSetUnorderedAccessViews(0, 1, &nullOutput, nullptr);
		bindPhase = BindPhase::kMaterialOverlayResolved;
		if (depthQualifiedSamples) {
			std::uint64_t samples = 0;
			if (!ReadMaterialCoverageProof(context, samples))
				return false;
			*depthQualifiedSamples = samples;
		}
		return true;
	}

	bool RenderTarget::ResolveMaterialOverlayCaptureWithProperties(
		ID3D11DeviceContext* context,
		ID3D11ComputeShader* resolveShader,
		ID3D11Buffer* lightingConstants) noexcept
	{
		if (!context || !resolveShader || !bound || !MaterialPropertiesOverlayCaptureReady() ||
			bindPhase != BindPhase::kMaterialOverlayGBuffer || !colorTexture)
			return false;

		ID3D11ShaderResourceView* environmentColor = nullptr;
		if (durableMaterialEnvironmentEnabled) {
			if (!DurableMaterialEnvironmentReady())
				return false;
			environmentColor = environmentColorSRV.Get();
		} else {
			D3D11_TEXTURE2D_DESC colorDescription{};
			colorTexture->GetDesc(&colorDescription);
			Microsoft::WRL::ComPtr<ID3D11Device> device;
			colorTexture->GetDevice(&device);
			if (!EnsureMaterialEnvironmentScratch(
					device.Get(), width, height, colorDescription.Format))
				return false;
			environmentColor = g_materialEnvironmentScratchSRV.Get();
		}

		context->OMSetRenderTargets(0, nullptr, nullptr);
		ScopedComputeState savedState(context);
		if (!durableMaterialEnvironmentEnabled) {
			D3D11_BOX sourceBox{};
			sourceBox.right = width;
			sourceBox.bottom = height;
			sourceBox.back = 1;
			context->CopySubresourceRegion(
				g_materialEnvironmentScratchTexture.Get(), 0, 0, 0, 0,
				colorTexture.Get(), 0, std::addressof(sourceBox));
		}
		ID3D11ShaderResourceView* resources[6] = {
			materialDiffuseSRV.Get(), materialNormalSRV.Get(), materialPropertiesSRV.Get(),
			overlayDepthSRV.Get(), depthSRV.Get(), environmentColor
		};
		ID3D11UnorderedAccessView* output = colorUAV.Get();
		context->CSSetShader(resolveShader, nullptr, 0);
		context->CSSetShaderResources(0, 6, resources);
		context->CSSetConstantBuffers(0, 1, &lightingConstants);
		context->CSSetUnorderedAccessViews(0, 1, &output, nullptr);
		context->Dispatch((width + 7u) / 8u, (height + 7u) / 8u, 1);

		ID3D11ShaderResourceView* nullResources[6]{};
		ID3D11UnorderedAccessView* nullOutput = nullptr;
		ID3D11Buffer* nullConstants = nullptr;
		context->CSSetShaderResources(0, 6, nullResources);
		context->CSSetConstantBuffers(0, 1, &nullConstants);
		context->CSSetUnorderedAccessViews(0, 1, &nullOutput, nullptr);
		bindPhase = BindPhase::kMaterialOverlayResolved;
		return true;
	}

	bool RenderTarget::ResolveNativeLitPlayerOverlay(
		ID3D11DeviceContext* context,
		ID3D11ComputeShader* resolveShader,
		ID3D11ShaderResourceView* playerColor,
		ID3D11ShaderResourceView* playerDepth) noexcept
	{
		if (!context || !resolveShader || !playerColor || !playerDepth || bound || !Ready() ||
			!colorUAV || !depthSRV || !colorTypedUAVLoadSupported || width == 0u || height == 0u)
			return false;

		struct Texture2DEvidence
		{
			D3D11_SHADER_RESOURCE_VIEW_DESC view{};
			D3D11_TEXTURE2D_DESC texture{};
			Microsoft::WRL::ComPtr<ID3D11Device> device;
			Microsoft::WRL::ComPtr<ID3D11Texture2D> resource;
		};
		auto exactTexture2D = [](ID3D11ShaderResourceView* view, bool requireDepth,
			Texture2DEvidence* evidence) noexcept {
			if (!view || !evidence)
				return false;
			*evidence = {};
			view->GetDesc(std::addressof(evidence->view));
			if (evidence->view.ViewDimension != D3D11_SRV_DIMENSION_TEXTURE2D ||
				evidence->view.Texture2D.MostDetailedMip != 0u || evidence->view.Texture2D.MipLevels != 1u)
				return false;
			if (requireDepth && evidence->view.Format != DXGI_FORMAT_R24_UNORM_X8_TYPELESS)
				return false;
			Microsoft::WRL::ComPtr<ID3D11Resource> resource;
			view->GetResource(resource.GetAddressOf());
			if (!resource || FAILED(resource.As(&evidence->resource)) || !evidence->resource)
				return false;
			evidence->resource->GetDesc(std::addressof(evidence->texture));
			evidence->resource->GetDevice(evidence->device.GetAddressOf());
			const bool exactFormat = requireDepth ?
				(evidence->texture.Format == DXGI_FORMAT_R24G8_TYPELESS &&
					evidence->view.Format == DXGI_FORMAT_R24_UNORM_X8_TYPELESS) :
				(evidence->view.Format == evidence->texture.Format);
			return evidence->device && evidence->texture.Width != 0u && evidence->texture.Height != 0u &&
				evidence->texture.MipLevels == 1u && evidence->texture.ArraySize == 1u &&
				evidence->texture.SampleDesc.Count == 1u && exactFormat;
		};
		Texture2DEvidence playerColorEvidence{};
		Texture2DEvidence playerDepthEvidence{};
		if (!exactTexture2D(playerColor, false, std::addressof(playerColorEvidence)) ||
			!exactTexture2D(playerDepth, true, std::addressof(playerDepthEvidence)) ||
			(playerColorEvidence.texture.Format != DXGI_FORMAT_R8G8B8A8_UNORM &&
			 playerColorEvidence.texture.Format != DXGI_FORMAT_R16G16B16A16_FLOAT) ||
			!Util::D3DDevicesMatch(playerColorEvidence.device.Get(), playerDepthEvidence.device.Get()) ||
			playerDepthEvidence.texture.Width < playerColorEvidence.texture.Width ||
			playerDepthEvidence.texture.Height < playerColorEvidence.texture.Height ||
			playerColorEvidence.resource.Get() == playerDepthEvidence.resource.Get() ||
			playerColorEvidence.resource.Get() == colorTexture.Get() ||
			playerColorEvidence.resource.Get() == depthTexture.Get() ||
			playerDepthEvidence.resource.Get() == colorTexture.Get() ||
			playerDepthEvidence.resource.Get() == depthTexture.Get())
			return false;
		Microsoft::WRL::ComPtr<ID3D11Device> targetDevice;
		colorTexture->GetDevice(targetDevice.GetAddressOf());
		Microsoft::WRL::ComPtr<ID3D11Device> contextDevice;
		context->GetDevice(contextDevice.GetAddressOf());
		Microsoft::WRL::ComPtr<ID3D11Device> shaderDevice;
		resolveShader->GetDevice(shaderDevice.GetAddressOf());
		if (!targetDevice || !contextDevice || !shaderDevice ||
			!Util::D3DDevicesMatch(targetDevice.Get(), contextDevice.Get()) ||
			!Util::D3DDevicesMatch(targetDevice.Get(), shaderDevice.Get()) ||
			!Util::D3DDevicesMatch(targetDevice.Get(), playerColorEvidence.device.Get()))
			return false;

		auto stageResourcesAreNull = [context](auto getter) noexcept {
			std::array<ID3D11ShaderResourceView*, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT> views{};
			(context->*getter)(0, static_cast<UINT>(views.size()), views.data());
			bool allNull = true;
			for (auto* view : views) {
				if (view) {
					allNull = false;
					view->Release();
				}
			}
			return allNull;
		};
		if (!stageResourcesAreNull(&ID3D11DeviceContext::VSGetShaderResources) ||
			!stageResourcesAreNull(&ID3D11DeviceContext::HSGetShaderResources) ||
			!stageResourcesAreNull(&ID3D11DeviceContext::DSGetShaderResources) ||
			!stageResourcesAreNull(&ID3D11DeviceContext::GSGetShaderResources))
			return false;
		std::array<ID3D11UnorderedAccessView*, D3D11_PS_CS_UAV_REGISTER_COUNT> outputMergerUAVs{};
		context->OMGetRenderTargetsAndUnorderedAccessViews(
			0, nullptr, nullptr, 0, static_cast<UINT>(outputMergerUAVs.size()), outputMergerUAVs.data());
		bool outputMergerUAVsNull = true;
		for (auto* view : outputMergerUAVs) {
			if (view) {
				outputMergerUAVsNull = false;
				view->Release();
			}
		}
		if (!outputMergerUAVsNull)
			return false;

		std::array<Microsoft::WRL::ComPtr<ID3D11RenderTargetView>,
			D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> priorTargets{};
		std::array<ID3D11RenderTargetView*, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> rawTargets{};
		ID3D11DepthStencilView* rawDepth = nullptr;
		context->OMGetRenderTargets(
			static_cast<UINT>(rawTargets.size()), rawTargets.data(), std::addressof(rawDepth));
		for (std::size_t index = 0; index < rawTargets.size(); ++index)
			priorTargets[index].Attach(rawTargets[index]);
		Microsoft::WRL::ComPtr<ID3D11DepthStencilView> priorDepth;
		priorDepth.Attach(rawDepth);

		context->OMSetRenderTargets(0, nullptr, nullptr);
		{

			ScopedComputeState savedState(context);
			ID3D11ShaderResourceView* resources[3] = { playerColor, playerDepth, depthSRV.Get() };
			ID3D11UnorderedAccessView* output = colorUAV.Get();
			context->CSSetShader(resolveShader, nullptr, 0);
			context->CSSetShaderResources(0, 3, resources);
			context->CSSetUnorderedAccessViews(0, 1, &output, nullptr);
			context->Dispatch((width + 7u) / 8u, (height + 7u) / 8u, 1);

			ID3D11ShaderResourceView* nullResources[3]{};
			ID3D11UnorderedAccessView* nullOutput = nullptr;
			context->CSSetShaderResources(0, 3, nullResources);
			context->CSSetUnorderedAccessViews(0, 1, &nullOutput, nullptr);
		}
		bindPhase = BindPhase::kMaterialOverlayResolved;
		std::array<ID3D11RenderTargetView*, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> restoreTargets{};
		for (std::size_t index = 0; index < priorTargets.size(); ++index)
			restoreTargets[index] = priorTargets[index].Get();
		context->OMSetRenderTargets(
			static_cast<UINT>(restoreTargets.size()), restoreTargets.data(), priorDepth.Get());
		return true;
	}

	bool RenderTarget::BindResolvedColor(ID3D11DeviceContext* context) noexcept
	{
		if (!context || !bound || !Ready() ||
			(bindPhase != BindPhase::kMaterialResolved &&
				bindPhase != BindPhase::kMaterialOverlayResolved))
			return false;

		bindPhase = bindPhase == BindPhase::kMaterialOverlayResolved ?
			BindPhase::kMaterialOverlayForward : BindPhase::kFinalColor;
		Rebind(context);
		return true;
	}

	bool RenderTarget::RestoreDurableMaterialEnvironment(ID3D11DeviceContext* context) noexcept
	{
		if (!context || !bound || bindPhase != BindPhase::kFinalColor ||
			!DurableMaterialEnvironmentReady() || !colorTexture || !depthTexture)
			return false;

		context->OMSetRenderTargets(0, nullptr, nullptr);
		context->CopyResource(colorTexture.Get(), environmentColorTexture.Get());
		context->CopyResource(depthTexture.Get(), environmentDepthTexture.Get());
		Rebind(context);
		return true;
	}

	void RenderTarget::Rebind(ID3D11DeviceContext* context) noexcept
	{
		if (!context || !bound || !Ready())
			return;
		switch (bindPhase) {
		case BindPhase::kMaterialDepth:
			context->OMSetRenderTargets(0, nullptr, depthDSV.Get());
			break;
		case BindPhase::kMaterialOverlayDepth:
			context->OMSetRenderTargets(0, nullptr, overlayDepthDSV.Get());
			break;
		case BindPhase::kMaterialGBuffer:
		case BindPhase::kMaterialOverlayGBuffer: {
			MirrorMaterialLayout::Bind(context, MaterialTargets(),
				bindPhase == BindPhase::kMaterialGBuffer ? depthDSV.Get() : overlayDepthDSV.Get());
			break;
		}
		case BindPhase::kMaterialResolved:
		case BindPhase::kMaterialOverlayResolved:
			context->OMSetRenderTargets(0, nullptr, nullptr);
			break;
		case BindPhase::kMaterialOverlayForward: {
			ID3D11RenderTargetView* target = colorRTV.Get();
			context->OMSetRenderTargets(1, &target, overlayDepthDSV.Get());
			break;
		}
		default: {
			ID3D11RenderTargetView* target = colorRTV.Get();
			context->OMSetRenderTargets(1, &target, depthDSV.Get());
			break;
		}
		}
		D3D11_VIEWPORT viewport{};
		viewport.Width = static_cast<float>(width);
		viewport.Height = static_cast<float>(height);
		viewport.MinDepth = 0.0f;
		viewport.MaxDepth = 1.0f;

		context->RSSetViewports(1, &viewport);

		D3D11_RECT fullTargetScissor{};
		fullTargetScissor.right = static_cast<LONG>(width);
		fullTargetScissor.bottom = static_cast<LONG>(height);
		context->RSSetScissorRects(1, &fullTargetScissor);
	}

	void RenderTarget::End(ID3D11DeviceContext* context) noexcept
	{
		if (!bound)
			return;
		if (context) {
			if (materialOverlayCoverageQueryActive && materialOverlayCoverageQuery) {
				context->End(materialOverlayCoverageQuery.Get());
				materialOverlayCoverageQueryActive = false;
				materialOverlayCoverageQueryPending = true;
			}
			ID3D11RenderTargetView* rawRTVs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]{};
			for (std::size_t index = 0; index < savedRTVs.size(); ++index)
				rawRTVs[index] = savedRTVs[index].Get();
			context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, rawRTVs, savedDSV.Get());
			context->RSSetViewports(
				savedViewportCount, savedViewportCount > 0 ? savedViewports.data() : nullptr);
			context->RSSetScissorRects(
				savedScissorRectCount,
				savedScissorRectCount > 0 ? savedScissorRects.data() : nullptr);
		}
		bound = false;
		bindPhase = BindPhase::kFinalColor;
		ResetSavedState();
	}

	void RenderTarget::ResetSavedState() noexcept
	{
		for (auto& target : savedRTVs)
			target.Reset();
		savedDSV.Reset();
		savedViewportCount = 0;
		savedViewports = {};
		savedScissorRectCount = 0;
		savedScissorRects = {};
	}
}

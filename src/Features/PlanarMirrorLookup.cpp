#include "PlanarMirrorLookup.h"

#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <format>
#include <limits>
#include <string_view>
#include <utility>

#include <d3d11.h>
#include <wincodec.h>
#include <DirectXTex.h>
#include <wrl/client.h>

#include "Utils/D3D.h"

namespace
{
	using Microsoft::WRL::ComPtr;

	constexpr std::size_t kRasterizerCacheSize = 64;
	constexpr wchar_t kMirrorDumpTriggerPath[] = L"Data\\dynref_mirrordump";
	constexpr wchar_t kMirrorDumpDirectory[] = L"Data\\DynRefDump";
	constexpr wchar_t kEyePixelDumpTriggerPath[] = L"Data\\dynref_eyepixeldump";
	constexpr wchar_t kPresentedPaneDumpTriggerPath[] = L"Data\\dynref_panedump";
	constexpr wchar_t kEyePixelDumpDirectory[] = L"Data\\DynRefEyePixelDump";
	constexpr std::uint32_t kEyePixelDumpCompleteMask = 0xFu;
	constexpr std::uint32_t kEyePixelDumpOcclusionQueryBudget = 4u;
	constexpr std::uint64_t kEyePixelDumpQueryBudgetRefillMs = 1000u;

	struct EyePixelDumpState
	{
		std::filesystem::file_time_type activeTriggerTime{};
		std::filesystem::file_time_type lastCompletedTriggerTime{};
		std::uint32_t capturedStageMask{ 0 };
		std::uint32_t remainingOcclusionQueries{ 0 };
		std::uint64_t minimumExactProofSerial{ 0 };
		std::uint64_t qualifiedExactProofSerial{ 0 };
		std::uint64_t queryBudgetWindowStartMs{ 0 };
		bool transactionQualified{ false };
		bool active{ false };
	};

	EyePixelDumpState& GetEyePixelDumpState() noexcept
	{
		static EyePixelDumpState state;
		return state;
	}

	struct RasterizerCacheEntry
	{
		ComPtr<ID3D11RasterizerState> source;
		ComPtr<ID3D11RasterizerState> reflected;
		bool sourceIsDefault{ false };
		bool used{ false };
	};

	struct Resources
	{
		ComPtr<ID3D11Device> device;

		std::array<RasterizerCacheEntry, kRasterizerCacheSize> rasterizerCache{};

		void Reset() noexcept
		{
			for (auto& entry : rasterizerCache)
				entry = {};
			device.Reset();
		}
	};

	Resources& GetResources() noexcept
	{

		static Resources* resources = new Resources();
		return *resources;
	}

	void SetDebugName(ID3D11DeviceChild* object, const char* name) noexcept
	{
		if (object && name)
			object->SetPrivateData(WKPDID_D3DDebugObjectName, static_cast<UINT>(std::strlen(name)), name);
	}

	bool SelectDevice(Resources& resources, ID3D11Device* device) noexcept
	{
		if (!device)
			return false;
		if (resources.device && resources.device.Get() != device)
			resources.Reset();
		if (!resources.device)
			resources.device = device;
		return true;
	}

	bool LogAndSaveImageDiagnostic(
		const DirectX::Image& source,
		std::string_view label,
		const std::filesystem::path& outputPath) noexcept
	{
		DirectX::ScratchImage floatImage;
		HRESULT floatResult = S_OK;
		const DirectX::Image* image = &source;
		if (source.format != DXGI_FORMAT_R32G32B32A32_FLOAT) {
			floatResult = DirectX::Convert(
				source,
				DXGI_FORMAT_R32G32B32A32_FLOAT,
				DirectX::TEX_FILTER_DEFAULT,
				DirectX::TEX_THRESHOLD_DEFAULT,
				floatImage);
			image = SUCCEEDED(floatResult) ? floatImage.GetImage(0, 0, 0) : nullptr;
		}
		if (!image || !image->pixels || image->width == 0 || image->height == 0) {
			logger::warn(
				"[PlanarMirrors] diagnostic '{}' float conversion failed "
				"(hr=0x{:08X}, sourceFormat={}, {}x{}, rowPitch={}, slicePitch={})",
				label, static_cast<std::uint32_t>(floatResult),
				static_cast<std::uint32_t>(source.format), source.width, source.height,
				source.rowPitch, source.slicePitch);
			return false;
		}

		double sum = 0.0;
		double sumSquares = 0.0;
		float maximum = 0.0f;
		std::uint64_t finitePixels = 0;
		std::uint64_t nonBlackPixels = 0;
		for (std::size_t y = 0; y < image->height; ++y) {
			const auto* row = reinterpret_cast<const float*>(image->pixels + y * image->rowPitch);
			for (std::size_t x = 0; x < image->width; ++x) {
				const float r = row[x * 4 + 0];
				const float g = row[x * 4 + 1];
				const float b = row[x * 4 + 2];
				if (!std::isfinite(r) || !std::isfinite(g) || !std::isfinite(b))
					continue;
				const float luminance = std::max(0.0f, 0.2126f * r + 0.7152f * g + 0.0722f * b);
				sum += luminance;
				sumSquares += static_cast<double>(luminance) * luminance;
				maximum = std::max(maximum, luminance);
				++finitePixels;
				if (luminance > 1.0e-3f)
					++nonBlackPixels;
			}
		}
		const double mean = finitePixels ? sum / static_cast<double>(finitePixels) : 0.0;
		const double variance = finitePixels ?
			std::max(0.0, sumSquares / static_cast<double>(finitePixels) - mean * mean) : 0.0;
		const double nonBlackPercent = finitePixels ?
			100.0 * static_cast<double>(nonBlackPixels) / static_cast<double>(finitePixels) : 0.0;
		logger::info("[PlanarMirrors] diagnostic '{}': {}x{} meanLuma={:.5f} stddev={:.5f} max={:.5f} nonBlack={:.2f}%",
			label, image->width, image->height, mean, std::sqrt(variance), maximum, nonBlackPercent);

		DirectX::ScratchImage preview;

		const HRESULT previewResult = source.format == DXGI_FORMAT_R8G8B8A8_UNORM ?
			preview.InitializeFromImage(source) :
			DirectX::Convert(
				source,
				DXGI_FORMAT_R8G8B8A8_UNORM,
				DirectX::TEX_FILTER_DEFAULT,
				DirectX::TEX_THRESHOLD_DEFAULT,
				preview);
		const auto* previewImage = SUCCEEDED(previewResult) ? preview.GetImage(0, 0, 0) : nullptr;
		const bool invalidPreview = !previewImage || !previewImage->pixels ||
			previewImage->width == 0 || previewImage->height == 0 ||
			previewImage->width > std::numeric_limits<std::size_t>::max() / 4u ||
			previewImage->rowPitch < previewImage->width * 4u;
		if (invalidPreview) {
			logger::warn(
				"[PlanarMirrors] diagnostic '{}' PNG preview conversion failed "
				"(hr=0x{:08X}, sourceFormat={}, {}x{}, rowPitch={}, slicePitch={}): {}",
				label, static_cast<std::uint32_t>(previewResult),
				static_cast<std::uint32_t>(source.format), source.width, source.height,
				source.rowPitch, source.slicePitch, outputPath.string());
			return false;
		}

		for (std::size_t y = 0; y < previewImage->height; ++y) {
			auto* row = preview.GetPixels() + y * previewImage->rowPitch;
			for (std::size_t x = 0; x < previewImage->width; ++x)
				row[x * 4u + 3u] = 0xFFu;
		}
		const HRESULT saveResult = DirectX::SaveToWICFile(
			*previewImage,
			DirectX::WIC_FLAGS_NONE,
			GUID_ContainerFormatPng,
			outputPath.c_str());
		if (FAILED(saveResult)) {
			logger::warn(
				"[PlanarMirrors] diagnostic '{}' PNG encode failed "
				"(hr=0x{:08X}, sourceFormat={}, previewFormat={}, {}x{}, rowPitch={}, slicePitch={}): {}",
				label, static_cast<std::uint32_t>(saveResult),
				static_cast<std::uint32_t>(source.format),
				static_cast<std::uint32_t>(previewImage->format), previewImage->width,
				previewImage->height, previewImage->rowPitch, previewImage->slicePitch,
				outputPath.string());
			return false;
		}
		return true;
	}

	constexpr bool IsPackedD24DiagnosticFormat(DXGI_FORMAT format) noexcept
	{
		return format == DXGI_FORMAT_R24G8_TYPELESS ||
			format == DXGI_FORMAT_D24_UNORM_S8_UINT ||
			format == DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
	}
	static_assert(IsPackedD24DiagnosticFormat(DXGI_FORMAT_R24G8_TYPELESS));
	static_assert(IsPackedD24DiagnosticFormat(DXGI_FORMAT_D24_UNORM_S8_UINT));
	static_assert(IsPackedD24DiagnosticFormat(DXGI_FORMAT_R24_UNORM_X8_TYPELESS));
	static_assert(!IsPackedD24DiagnosticFormat(DXGI_FORMAT_X24_TYPELESS_G8_UINT));

	bool LogAndSavePackedDepthDiagnostic(
		const DirectX::Image& source,
		std::string_view label,
		const std::filesystem::path& outputPath) noexcept
	{
		if (!IsPackedD24DiagnosticFormat(source.format)) {
			logger::warn(
				"[PlanarMirrors] diagnostic '{}' packed-depth decode rejected unsupported format {}",
				label, static_cast<std::uint32_t>(source.format));
			return false;
		}
		const bool invalidSource = !source.pixels || source.width == 0 || source.height == 0 ||
			source.width > std::numeric_limits<std::size_t>::max() / sizeof(std::uint32_t) ||
			source.rowPitch < source.width * sizeof(std::uint32_t) || source.rowPitch == 0 ||
			source.height > std::numeric_limits<std::size_t>::max() / source.rowPitch ||
			source.slicePitch < source.rowPitch * source.height;
		if (invalidSource) {
			logger::warn(
				"[PlanarMirrors] diagnostic '{}' packed-depth layout invalid "
				"(sourceFormat={}, {}x{}, rowPitch={}, slicePitch={})",
				label, static_cast<std::uint32_t>(source.format), source.width, source.height,
				source.rowPitch, source.slicePitch);
			return false;
		}

		DirectX::ScratchImage decoded;
		const HRESULT initializeResult = decoded.Initialize2D(
			DXGI_FORMAT_R32G32B32A32_FLOAT, source.width, source.height, 1u, 1u);
		auto* decodedImage = SUCCEEDED(initializeResult) ? decoded.GetImage(0, 0, 0) : nullptr;
		if (!decodedImage || !decodedImage->pixels) {
			logger::warn(
				"[PlanarMirrors] diagnostic '{}' packed-depth allocation failed (hr=0x{:08X})",
				label, static_cast<std::uint32_t>(initializeResult));
			return false;
		}
		constexpr float inverseD24Maximum = 1.0f / 16777215.0f;
		for (std::size_t y = 0; y < source.height; ++y) {
			const auto* sourceRow = reinterpret_cast<const std::uint32_t*>(
				source.pixels + y * source.rowPitch);
			auto* destinationRow = reinterpret_cast<float*>(
				decodedImage->pixels + y * decodedImage->rowPitch);
			for (std::size_t x = 0; x < source.width; ++x) {
				const float depth = static_cast<float>(sourceRow[x] & 0x00FFFFFFu) *
					inverseD24Maximum;
				destinationRow[x * 4u + 0u] = depth;
				destinationRow[x * 4u + 1u] = depth;
				destinationRow[x * 4u + 2u] = depth;
				destinationRow[x * 4u + 3u] = 1.0f;
			}
		}
		logger::info(
			"[PlanarMirrors] diagnostic '{}' decoded packed D24 sourceFormat={} to float grayscale",
			label, static_cast<std::uint32_t>(source.format));
		return LogAndSaveImageDiagnostic(*decodedImage, label, outputPath);
	}

	struct MirrorLayerDumpDescription
	{
		std::string_view label;
		const wchar_t* filename;
	};

	constexpr std::array<MirrorLayerDumpDescription, 4> kMirrorLayerDumpDescriptions{
		MirrorLayerDumpDescription{ "environment before native player", L"mirror_environment_before_player.png" },
		MirrorLayerDumpDescription{ "native player color", L"mirror_native_player_color.png" },
		MirrorLayerDumpDescription{ "native player depth", L"mirror_native_player_depth.png" },
		MirrorLayerDumpDescription{ "combined after native player", L"mirror_combined_after_player.png" }
	};

	bool SameDevice(ID3D11DeviceChild* object, ID3D11Device* expectedDevice) noexcept
	{
		if (!object || !expectedDevice)
			return false;
		ComPtr<ID3D11Device> objectDevice;
		object->GetDevice(&objectDevice);
		return objectDevice.Get() == expectedDevice;
	}

	D3D11_RASTERIZER_DESC DefaultRasterizerDescription() noexcept
	{
		D3D11_RASTERIZER_DESC description{};
		description.FillMode = D3D11_FILL_SOLID;
		description.CullMode = D3D11_CULL_BACK;
		description.FrontCounterClockwise = FALSE;
		description.DepthClipEnable = TRUE;
		return description;
	}

}

namespace PlanarMirrorLookup
{

	bool CapturePresentedPaneDump(
		ID3D11Device* device,
		ID3D11DeviceContext* context,
		ID3D11Texture2D* texture) noexcept
	{
		if (!device || !context || !texture || context->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE)
			return false;
		static std::filesystem::file_time_type lastCaptured{};
		std::error_code triggerError;
		const auto triggerTime =
			std::filesystem::last_write_time(kPresentedPaneDumpTriggerPath, triggerError);
		if (triggerError || triggerTime <= lastCaptured)
			return false;
		std::error_code directoryError;
		std::filesystem::create_directories(kMirrorDumpDirectory, directoryError);
		if (directoryError)
			return false;
		DirectX::ScratchImage capture;
		if (FAILED(DirectX::CaptureTexture(device, context, texture, capture)))
			return false;
		const auto* image = capture.GetImage(0, 0, 0);
		if (!image)
			return false;
		const auto outputPath =
			std::filesystem::path(kMirrorDumpDirectory) / L"mirror_presented_pane.png";
		if (!LogAndSaveImageDiagnostic(*image, "presented pane", outputPath))
			return false;
		lastCaptured = triggerTime;
		return true;
	}

	bool CaptureMirrorLayerDump(
		ID3D11Device* device,
		ID3D11DeviceContext* context,
		ID3D11Texture2D* texture,
		MirrorLayerDumpStage stage) noexcept
	{
		const auto stageIndex = static_cast<std::size_t>(stage);
		if (!device || !context || !texture || stageIndex >= kMirrorLayerDumpDescriptions.size())
			return false;

		static std::array<std::filesystem::file_time_type, kMirrorLayerDumpDescriptions.size()>
			lastCapturedTriggerTimes{};
		std::error_code triggerError;
		const auto triggerTime = std::filesystem::last_write_time(kMirrorDumpTriggerPath, triggerError);
		if (triggerError || triggerTime <= lastCapturedTriggerTimes[stageIndex])
			return false;

		std::error_code directoryError;
		std::filesystem::create_directories(kMirrorDumpDirectory, directoryError);
		if (directoryError) {
			logger::warn(
				"[PlanarMirrors] mirror-layer diagnostic directory creation failed: {}",
				directoryError.message());
			return false;
		}

		DirectX::ScratchImage capture;
		const HRESULT captureResult = DirectX::CaptureTexture(device, context, texture, capture);
		const auto* image = SUCCEEDED(captureResult) ? capture.GetImage(0, 0, 0) : nullptr;
		if (!image) {
			logger::warn(
				"[PlanarMirrors] mirror-layer diagnostic '{}' capture failed (hr=0x{:X})",
				kMirrorLayerDumpDescriptions[stageIndex].label,
				static_cast<std::uint32_t>(captureResult));
			return false;
		}
		const auto outputPath = std::filesystem::path(kMirrorDumpDirectory) /
			kMirrorLayerDumpDescriptions[stageIndex].filename;
		const bool saved = stage == MirrorLayerDumpStage::kNativePlayerDepth ?
			LogAndSavePackedDepthDiagnostic(
				*image, kMirrorLayerDumpDescriptions[stageIndex].label, outputPath) :
			LogAndSaveImageDiagnostic(
				*image, kMirrorLayerDumpDescriptions[stageIndex].label, outputPath);

		if (saved)
			lastCapturedTriggerTimes[stageIndex] = triggerTime;
		return saved;
	}

	bool BeginEyePixelDumpCapture(std::uint64_t currentExactProofSerial) noexcept
	{
		auto& state = GetEyePixelDumpState();
		if (state.active)
			return true;

		std::error_code triggerError;
		const auto triggerTime = std::filesystem::last_write_time(kEyePixelDumpTriggerPath, triggerError);
		if (triggerError || triggerTime <= state.lastCompletedTriggerTime)
			return false;

		std::error_code directoryError;
		std::filesystem::create_directories(kEyePixelDumpDirectory, directoryError);
		if (directoryError) {

			state.lastCompletedTriggerTime = triggerTime;
			logger::warn(
				"[MirrorSceneRenderer] EYEPIXELDUMP directory creation failed: {}",
				directoryError.message());
			return false;
		}

		state.activeTriggerTime = triggerTime;
		state.capturedStageMask = 0;
		state.remainingOcclusionQueries = kEyePixelDumpOcclusionQueryBudget;
		state.minimumExactProofSerial = currentExactProofSerial;
		state.qualifiedExactProofSerial = 0;
		state.queryBudgetWindowStartMs = 0;
		state.transactionQualified = false;
		state.active = true;
		logger::info(
			"[MirrorSceneRenderer] EYEPIXELDUMP armed: proofFloorSerial={} awaiting player-owned exact EyeHazel query",
			currentExactProofSerial);
		return true;
	}

	bool EyePixelDumpCaptureActive() noexcept
	{
		return GetEyePixelDumpState().active;
	}

	bool EyePixelDumpNeedsFreshExactProof(
		std::uint64_t currentExactProofSerial, bool currentProofMatchesCandidate) noexcept
	{
		const auto& state = GetEyePixelDumpState();
		return state.active &&
			(!currentProofMatchesCandidate || currentExactProofSerial <= state.minimumExactProofSerial);
	}

	bool ConsumeEyePixelDumpOcclusionQueryBudget(std::uint64_t nowMs) noexcept
	{
		auto& state = GetEyePixelDumpState();
		if (!state.active)
			return false;
		if (state.queryBudgetWindowStartMs == 0u)
			state.queryBudgetWindowStartMs = nowMs;
		if (state.remainingOcclusionQueries == 0u) {
			if (nowMs - state.queryBudgetWindowStartMs < kEyePixelDumpQueryBudgetRefillMs)
				return false;
			state.remainingOcclusionQueries = kEyePixelDumpOcclusionQueryBudget;
			state.queryBudgetWindowStartMs = nowMs;
		}
		--state.remainingOcclusionQueries;
		return true;
	}

	bool BeginQualifiedEyePixelDumpTransaction(std::uint64_t exactProofSerial) noexcept
	{
		auto& state = GetEyePixelDumpState();
		if (!state.active || exactProofSerial == 0u ||
			exactProofSerial <= state.minimumExactProofSerial)
			return false;
		state.capturedStageMask = 0;
		state.qualifiedExactProofSerial = exactProofSerial;
		state.transactionQualified = true;
		logger::info(
			"[MirrorSceneRenderer] EYEPIXELDUMP qualified: exactProofSerial={} playerOwned=true exactEyeHazel=true",
			exactProofSerial);
		return true;
	}

	void CancelQualifiedEyePixelDumpTransaction() noexcept
	{
		auto& state = GetEyePixelDumpState();
		state.capturedStageMask = 0;
		state.qualifiedExactProofSerial = 0;
		state.transactionQualified = false;
	}

	bool CaptureEyePixelDumpStage(
		ID3D11Device* device,
		ID3D11DeviceContext* context,
		ID3D11Texture2D* texture,
		EyePixelDumpStage stage) noexcept
	{
		auto& state = GetEyePixelDumpState();
		if (!state.active || !state.transactionQualified ||
			!device || !context || !texture)
			return false;

		const auto stageIndex = static_cast<std::uint32_t>(stage);
		if (stageIndex > static_cast<std::uint32_t>(EyePixelDumpStage::kFinalAfterGroup8))
			return false;
		const std::uint32_t stageBit = 1u << stageIndex;
		if (stage == EyePixelDumpStage::kMaterialAfterGroup4) {

			state.capturedStageMask = 0;
		} else {
			const std::uint32_t prerequisiteMask = stageBit - 1u;
			if ((state.capturedStageMask & prerequisiteMask) != prerequisiteMask)
				return false;
		}

		const wchar_t* filename = nullptr;
		std::string_view label;
		switch (stage) {
		case EyePixelDumpStage::kMaterialAfterGroup4:
			filename = L"eye_01_material_after_group4.png";
			label = "eye material after group 4";
			break;
		case EyePixelDumpStage::kResolvedBeforeGroup8:
			filename = L"eye_02_resolved_before_group8.png";
			label = "eye resolved before group 8";
			break;
		case EyePixelDumpStage::kAfterGroup8BeforeBatch8:
			filename = L"eye_03_after_group8_before_batch8.png";
			label = "eye after group 8 before batch 8";
			break;
		case EyePixelDumpStage::kFinalAfterGroup8:
			filename = L"eye_04_final_after_group8.png";
			label = "eye final after complete forward batch 8";
			break;
		default:
			return false;
		}

		DirectX::ScratchImage capture;
		const HRESULT captureResult = DirectX::CaptureTexture(device, context, texture, capture);
		const auto* image = SUCCEEDED(captureResult) ? capture.GetImage(0, 0, 0) : nullptr;
		const bool captured = image != nullptr;
		const bool saved = captured && LogAndSaveImageDiagnostic(
			*image,
			label,
			std::filesystem::path(kEyePixelDumpDirectory) / filename);
		if (!captured) {
			logger::warn(
				"[MirrorSceneRenderer] EYEPIXELDUMP '{}' capture failed (hr=0x{:X})",
				label, static_cast<std::uint32_t>(captureResult));
		}

		if (saved)
			state.capturedStageMask |= stageBit;
		if (stage == EyePixelDumpStage::kFinalAfterGroup8 && saved &&
			state.capturedStageMask == kEyePixelDumpCompleteMask) {
			state.lastCompletedTriggerTime = state.activeTriggerTime;
			const auto completedMask = state.capturedStageMask;
			const auto completedProofSerial = state.qualifiedExactProofSerial;
			state.active = false;
			state.capturedStageMask = 0;
			state.remainingOcclusionQueries = 0;
			state.minimumExactProofSerial = 0;
			state.qualifiedExactProofSerial = 0;
			state.queryBudgetWindowStartMs = 0;
			state.transactionQualified = false;
			logger::info(
				"[MirrorSceneRenderer] EYEPIXELDUMP complete: mask=0x{:X} expected=0x{:X} exactProofSerial={} "
				"playerOwned=true exactEyeHazel=true directory='Data\\DynRefEyePixelDump'",
				completedMask, kEyePixelDumpCompleteMask, completedProofSerial);
		} else if (stage == EyePixelDumpStage::kFinalAfterGroup8) {
			logger::warn(
				"[MirrorSceneRenderer] EYEPIXELDUMP incomplete: mask=0x{:X} expected=0x{:X}; trigger remains armed",
				state.capturedStageMask, kEyePixelDumpCompleteMask);
			state.capturedStageMask = 0;
			state.qualifiedExactProofSerial = 0;
			state.transactionQualified = false;
		}
		return saved;
	}

	void ReleaseResources() noexcept
	{
		GetResources().Reset();
	}

	bool ApplyReflectedRasterizer(
		ID3D11Device* device,
		ID3D11DeviceContext* context) noexcept
	{
		if (!device || !context || !SameDevice(context, device))
			return false;
		auto& resources = GetResources();
		if (!SelectDevice(resources, device))
			return false;

		ComPtr<ID3D11RasterizerState> currentState;
		context->RSGetState(&currentState);

		for (auto& entry : resources.rasterizerCache) {
			if (!entry.used)
				continue;
			if (currentState.Get() == entry.reflected.Get())
				return true;
			if ((currentState && currentState.Get() == entry.source.Get()) ||
				(!currentState && entry.sourceIsDefault)) {
				context->RSSetState(entry.reflected.Get());
				return true;
			}
		}

		D3D11_RASTERIZER_DESC description = DefaultRasterizerDescription();
		if (currentState)
			currentState->GetDesc(&description);
		if (description.CullMode == D3D11_CULL_NONE)
			return true;
		description.FrontCounterClockwise = !description.FrontCounterClockwise;

		std::size_t slot = resources.rasterizerCache.size();
		for (std::size_t index = 0; index < resources.rasterizerCache.size(); ++index) {
			if (!resources.rasterizerCache[index].used) {
				slot = index;
				break;
			}
		}
		if (slot == resources.rasterizerCache.size()) {
			static bool loggedFull = false;
			if (!loggedFull) {
				loggedFull = true;
				logger::warn("[PlanarMirrors] reflected rasterizer cache exhausted; preserving engine state");
			}
			return false;
		}

		ComPtr<ID3D11RasterizerState> reflected;
		if (FAILED(device->CreateRasterizerState(&description, &reflected)) || !reflected)
			return false;
		auto& entry = resources.rasterizerCache[slot];
		entry.source = currentState;
		entry.reflected = std::move(reflected);
		entry.sourceIsDefault = !currentState;
		entry.used = true;
		SetDebugName(entry.reflected.Get(), "RealisticReflectionsMirrors.PlanarMirror.ReflectedWindingRS");

		context->RSSetState(entry.reflected.Get());
		return true;
	}

	bool RestoreOrdinaryRasterizer(
		ID3D11Device* device,
		ID3D11DeviceContext* context) noexcept
	{
		if (!device || !context || !SameDevice(context, device))
			return false;
		auto& resources = GetResources();
		if (!SelectDevice(resources, device))
			return false;

		ComPtr<ID3D11RasterizerState> currentState;
		context->RSGetState(&currentState);
		for (auto& entry : resources.rasterizerCache) {
			if (!entry.used || currentState.Get() != entry.reflected.Get())
				continue;
			context->RSSetState(entry.sourceIsDefault ? nullptr : entry.source.Get());
			return true;
		}

		return true;
	}

	bool SaveTextureDiagnosticPNG(
		ID3D11Device* device,
		ID3D11DeviceContext* context,
		ID3D11Texture2D* texture,
		const char* label,
		const wchar_t* outputPath) noexcept
	{
		if (!device || !context || !texture || !label || !outputPath)
			return false;
		DirectX::ScratchImage capture;
		const HRESULT captureResult = DirectX::CaptureTexture(device, context, texture, capture);
		const auto* image = SUCCEEDED(captureResult) ? capture.GetImage(0, 0, 0) : nullptr;
		if (!image) {
			logger::warn(
				"[PlanarMirrors] diagnostic '{}' texture readback failed (hr=0x{:08X})",
				label, static_cast<std::uint32_t>(captureResult));
			return false;
		}
		
		DirectX::ScratchImage decompressed;
		if (DirectX::IsCompressed(image->format)) {
			const HRESULT decompressResult = DirectX::Decompress(
				*image, DXGI_FORMAT_R8G8B8A8_UNORM, decompressed);
			if (FAILED(decompressResult) || !decompressed.GetImage(0, 0, 0)) {
				logger::warn(
					"[PlanarMirrors] diagnostic '{}' decompress failed (hr=0x{:08X}, format={})",
					label, static_cast<std::uint32_t>(decompressResult),
					static_cast<std::uint32_t>(image->format));
				return false;
			}
			image = decompressed.GetImage(0, 0, 0);
		}
		return LogAndSaveImageDiagnostic(*image, label, std::filesystem::path(outputPath));
	}
}

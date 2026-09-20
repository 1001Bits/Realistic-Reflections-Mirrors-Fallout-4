#include "MirrorNativeWorldReentry.h"
#include "MirrorNativeWorldPhase.h"
#include "MirrorSceneRenderer.h"

#include "Globals.h"
#include "Utils/D3D.h"

#include <d3d11.h>

namespace MirrorNativeWorldReentry
{
	namespace
	{
		ID3D11Texture2D* g_scratch = nullptr;
		ID3D11ComputeShader* g_blit = nullptr;
		ID3D11Buffer* g_blitConstants = nullptr;
		bool g_blitCompileAttempted = false;
		ID3D11Device* g_blitDevice = nullptr;
		struct BlitConstants { std::uint32_t sourceRect[4]; std::uint32_t destinationExtent[4]; };
		static_assert(sizeof(BlitConstants) == 32);

		[[nodiscard]] ID3D11ShaderResourceView* MainColorSRV() noexcept
		{
			auto* renderer = globals::game::renderer;
			if (!renderer) return nullptr;
			constexpr std::size_t kMainTarget = 3;
			return reinterpret_cast<ID3D11ShaderResourceView*>(
				renderer->data.renderTargets[kMainTarget].srView);
		}

		[[nodiscard]] bool EnsureBlit() noexcept
		{
			auto* device = globals::d3d::device;
			if (!device) return false;
			if (device != g_blitDevice) {
				if (g_blit) { g_blit->Release(); g_blit = nullptr; }
				if (g_blitConstants) { g_blitConstants->Release(); g_blitConstants = nullptr; }
				g_blitCompileAttempted = false;
				g_blitDevice = device;
			}
			if (!g_blit && !g_blitCompileAttempted) {
				g_blitCompileAttempted = true;
				g_blit = static_cast<ID3D11ComputeShader*>(Util::CompileShader(
					L"Data\\\\Shaders\\\\MirrorsOfFallout\\\\MirrorNativeWorldBlitCS.hlsl", {}, "cs_5_0"));
				logger::info("[PlanarMirrors] native world re-entry blit {}",
					g_blit ? "compiled" : "FAILED to compile; re-entry cannot publish");
			}
			if (!g_blit) return false;
			if (!g_blitConstants) {
				D3D11_BUFFER_DESC desc{};
				desc.ByteWidth = sizeof(BlitConstants);
				desc.Usage = D3D11_USAGE_DYNAMIC;
				desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
				desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
				if (FAILED(device->CreateBuffer(&desc, nullptr, &g_blitConstants))) {
					g_blitConstants = nullptr; return false;
				}
			}
			return true;
		}
		std::uint32_t g_scratchWidth = 0, g_scratchHeight = 0;
		std::uint32_t g_scratchFormat = 0;

		[[nodiscard]] ID3D11Texture2D* MainColorTexture() noexcept
		{
			auto* renderer = globals::game::renderer;
			if (!renderer)
				return nullptr;

			constexpr std::size_t kMainRenderTarget = 3;
			auto& main = renderer->data.renderTargets[kMainRenderTarget];
			return reinterpret_cast<ID3D11Texture2D*>(main.texture);
		}

		[[nodiscard]] bool EnsureScratch(ID3D11Texture2D* a_main) noexcept
		{
			if (!a_main || !globals::d3d::device)
				return false;
			D3D11_TEXTURE2D_DESC desc{};
			a_main->GetDesc(&desc);
			if (g_scratch && g_scratchWidth == desc.Width && g_scratchHeight == desc.Height &&
				g_scratchFormat == static_cast<std::uint32_t>(desc.Format))
				return true;
			if (g_scratch) {
				g_scratch->Release();
				g_scratch = nullptr;
			}

			D3D11_TEXTURE2D_DESC scratch = desc;
			scratch.BindFlags = 0;
			scratch.CPUAccessFlags = 0;
			scratch.MiscFlags = 0;
			scratch.Usage = D3D11_USAGE_DEFAULT;
			if (FAILED(globals::d3d::device->CreateTexture2D(&scratch, nullptr, &g_scratch))) {
				g_scratch = nullptr;
				return false;
			}
			g_scratchWidth = desc.Width;
			g_scratchHeight = desc.Height;
			g_scratchFormat = static_cast<std::uint32_t>(desc.Format);
			return true;
		}

		bool RunStages(void* a_reflectedCamera, void** a_cameraSlot, void** a_visCameraSlot) noexcept
		{
			using Stage = void (*)();
			const auto stage = [](std::uintptr_t rva) noexcept {
				return reinterpret_cast<Stage>(REL::Offset(ReflectionRuntime::Rva(rva)).address());
			};
			void* savedCamera = nullptr;
			void* savedVisCamera = nullptr;
			bool installed = false;
			bool completed = false;
			__try {
				savedCamera = *a_cameraSlot;
				savedVisCamera = *a_visCameraSlot;

				if (savedCamera && savedVisCamera) {
					*a_cameraSlot = a_reflectedCamera;
					*a_visCameraSlot = a_reflectedCamera;
					installed = true;

					stage(kRVA_DoUmbraQuery)();
					stage(kRVA_LightUpdate)();
					stage(kRVA_MainAccum)();
					stage(kRVA_MainRenderSetup)();
					stage(kRVA_DeferredPrePass)();
					stage(kRVA_DeferredDecals)();
					stage(kRVA_ImagespaceSAO)();
					stage(kRVA_DeferredLightsImpl)();
					stage(kRVA_DeferredComposite)();
					stage(kRVA_Forward)();
					stage(kRVA_Refraction)();
					completed = true;
				}
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				g_faults.fetch_add(1, std::memory_order_relaxed);
			}

			__try {
				if (installed) {
					*a_cameraSlot = savedCamera;
					*a_visCameraSlot = savedVisCamera;
				}
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				g_faults.fetch_add(1, std::memory_order_relaxed);
			}
			return completed;
		}
	}

	void ReleaseResources() noexcept
	{
		if (g_scratch) {
			g_scratch->Release();
			g_scratch = nullptr;
		}
		if (g_blit) { g_blit->Release(); g_blit = nullptr; }
		if (g_blitConstants) { g_blitConstants->Release(); g_blitConstants = nullptr; }
		g_blitCompileAttempted = false;
		g_scratchWidth = g_scratchHeight = g_scratchFormat = 0;
	}

	bool RenderReflectedInto(void* a_reflectedCamera, ID3D11UnorderedAccessView* a_destination,
		std::uint32_t a_width, std::uint32_t a_height) noexcept
	{
		if (!a_reflectedCamera || !a_destination || !a_width || !a_height || !Enabled() || !Available())
			return false;

		if (MirrorSceneRenderer::PrivateRenderActive() || MirrorSceneRenderer::LoadBlocked() ||
			!MirrorNativeWorldPhase::TryEnter()) return false;
		struct PhaseExit { ~PhaseExit() { MirrorNativeWorldPhase::Leave(); } } phaseExit;
		auto* context = globals::d3d::context;
		auto* main = MainColorTexture();
		auto* mainSRV = MainColorSRV();
		if (!context || !main || !mainSRV || !EnsureScratch(main) || !EnsureBlit())
			return false;

		auto* const cameraSlot = reinterpret_cast<void**>(
			REL::Offset(ReflectionRuntime::Rva(kRVA_pCamera)).address());
		auto* const visCameraSlot = reinterpret_cast<void**>(
			REL::Offset(ReflectionRuntime::Rva(kRVA_pVisCamera)).address());
		if (!cameraSlot || !visCameraSlot)
			return false;

		g_attempts.fetch_add(1, std::memory_order_relaxed);

		context->CopyResource(g_scratch, main);

		const bool completed = RunStages(a_reflectedCamera, cameraSlot, visCameraSlot);

		bool published = false;
		if (completed) {

			D3D11_VIEWPORT viewport{};
			UINT viewportCount = 1;
			context->RSGetViewports(&viewportCount, &viewport);
			D3D11_TEXTURE2D_DESC sourceDesc{};
			main->GetDesc(&sourceDesc);
			const bool usable = viewportCount == 1 && viewport.Width >= 1.0f && viewport.Height >= 1.0f;
			BlitConstants constants{};
			constants.sourceRect[0] = usable ? static_cast<std::uint32_t>(viewport.TopLeftX) : 0u;
			constants.sourceRect[1] = usable ? static_cast<std::uint32_t>(viewport.TopLeftY) : 0u;
			constants.sourceRect[2] = usable ? static_cast<std::uint32_t>(viewport.Width) : sourceDesc.Width;
			constants.sourceRect[3] = usable ? static_cast<std::uint32_t>(viewport.Height) : sourceDesc.Height;
			constants.destinationExtent[0] = a_width;
			constants.destinationExtent[1] = a_height;
			D3D11_MAPPED_SUBRESOURCE mapped{};
			if (SUCCEEDED(context->Map(g_blitConstants, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
				std::memcpy(mapped.pData, &constants, sizeof(constants));
				context->Unmap(g_blitConstants, 0);

				ID3D11ComputeShader* previousShader = nullptr;
				ID3D11ShaderResourceView* previousSRV = nullptr;
				ID3D11UnorderedAccessView* previousUAV = nullptr;
				ID3D11Buffer* previousBuffer = nullptr;
				context->CSGetShader(&previousShader, nullptr, nullptr);
				context->CSGetShaderResources(0, 1, &previousSRV);
				context->CSGetUnorderedAccessViews(0, 1, &previousUAV);
				context->CSGetConstantBuffers(0, 1, &previousBuffer);
				const UINT initial = 0;
				context->CSSetShader(g_blit, nullptr, 0);
				context->CSSetShaderResources(0, 1, &mainSRV);
				context->CSSetUnorderedAccessViews(0, 1, &a_destination, &initial);
				context->CSSetConstantBuffers(0, 1, &g_blitConstants);
				context->Dispatch((a_width + 7u) / 8u, (a_height + 7u) / 8u, 1u);
				ID3D11ShaderResourceView* const noSRV = nullptr;
				ID3D11UnorderedAccessView* const noUAV = nullptr;
				context->CSSetShaderResources(0, 1, &noSRV);
				context->CSSetUnorderedAccessViews(0, 1, &noUAV, &initial);
				context->CSSetShader(previousShader, nullptr, 0);
				context->CSSetShaderResources(0, 1, &previousSRV);
				context->CSSetUnorderedAccessViews(0, 1, &previousUAV, &initial);
				context->CSSetConstantBuffers(0, 1, &previousBuffer);
				if (previousShader) previousShader->Release();
				if (previousSRV) previousSRV->Release();
				if (previousUAV) previousUAV->Release();
				if (previousBuffer) previousBuffer->Release();
				published = true;
			}
		}
		
		context->CopyResource(main, g_scratch);

		if (published)
			g_completed.fetch_add(1, std::memory_order_relaxed);
		return published;
	}
}

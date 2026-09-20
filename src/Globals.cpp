#include "Features/ReflectionRuntime.h"
#include "Globals.h"

#include <array>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <d3dcompiler.h>
#include <fstream>
#include <windows.h>

#include <filesystem>
#include "Features/FlatDeferredPlayerCapture.h"
#include "Features/MirrorSceneRenderer.h"
#include "Features/MirrorDrawDispatch.h"
#include "Features/MirrorDrawHookRegistry.h"
#include "Utils/D3D.h"
#include "Utils/D3DDeviceIdentity.h"

namespace globals
{

	extern bool g_albedoLoudDiagnostic;

	namespace d3d
	{
		ID3D11Device* device = nullptr;
		ID3D11DeviceContext* context = nullptr;
		IDXGISwapChain* swapChain = nullptr;
	}

	namespace features
	{

		void InitFeatures() {}

		namespace llf
		{
		}
	}

	namespace game
	{
		RE::BSGraphics::RendererShadowState* shadowState = nullptr;
		RE::BSGraphics::State* graphicsState = nullptr;
		RE::BSGraphics::Renderer* renderer = nullptr;
		RE::BSShaderManager::State* smState = nullptr;
		RE::TES* tes = nullptr;
		bool isVR = false;
		RE::MemoryManager* memoryManager = nullptr;
		RE::INISettingCollection* iniSettingCollection = nullptr;
		RE::INIPrefSettingCollection* iniPrefSettingCollection = nullptr;
		RE::GameSettingCollection* gameSettingCollection = nullptr;
		float* cameraNear = nullptr;
		float* cameraFar = nullptr;
		float* deltaTime = nullptr;
		RE::BSUtilityShader* utilityShader = nullptr;
		RE::Sky* sky = nullptr;
		RE::UI* ui = nullptr;

		RE::BSGraphics::PixelShader** currentPixelShader = nullptr;
		RE::BSGraphics::VertexShader** currentVertexShader = nullptr;
		REX::EnumSet<RE::BSGraphics::ShaderFlags, uint32_t>* stateUpdateFlags = nullptr;

		RE::Setting* bEnableLandFade = nullptr;
		RE::Setting* bShadowsOnGrass = nullptr;
		RE::Setting* shadowMaskQuarter = nullptr;

		REL::Relocation<ID3D11Buffer**> perFrame;
		REL::Relocation<RE::BSGraphics::BSShaderAccumulator**> currentAccumulator;

		ID3D11Buffer* identifiedPerFrameBuffer = nullptr;

		D3D11_MAPPED_SUBRESOURCE* mappedFrameBuffer = nullptr;
		FrameBufferCache frameBufferCached{};
	}

	namespace rtti
	{
		REL::Relocation<const RE::NiRTTI*> BSLightingShaderPropertyRTTI;
	}

	void OnInit()
	{

	}

	static void DllTrace([[maybe_unused]] const char* msg)
	{
	#ifdef MIRRORS_DLL_TRACE
		char path[MAX_PATH];
		if (GetEnvironmentVariableA("USERPROFILE", path, MAX_PATH))
			strcat_s(path, "\\Documents\\RealisticReflectionsMirrors_trace.log");
		else
			strcpy_s(path, "C:\\RealisticReflectionsMirrors_trace.log");
		FILE* f = nullptr;
		fopen_s(&f, path, "a");
		if (f) { fprintf(f, "%s\n", msg); fclose(f); }
	#endif
	}

	void ReInit()
	{
		DllTrace("ReInit: entered");
		{
			using namespace game;

			DllTrace("ReInit: getting RendererData");

			const bool rendererIdResolvable = !REL::Module::IsNG() || (ReflectionRuntime::IsPort240() && ReflectionRuntime::PortId(1235449) != 0);
			auto* rendererData = rendererIdResolvable ? RE::BSGraphics::RendererData::GetSingleton() : nullptr;
			if (!rendererData) {
				DllTrace("ReInit: RendererData is NULL (renderer not initialized yet) — deferring D3D init");
				
			} else {
				DllTrace("ReInit: RendererData OK, reading shadowState");

				if (auto* liveShadowState = rendererData->shadowState)
					shadowState = liveShadowState;

				{
					auto stateAddr = REL::RelocationID(600795, 0).address();
					if (stateAddr) {
						graphicsState = reinterpret_cast<RE::BSGraphics::State*>(stateAddr);
					} else {

						graphicsState = nullptr;
						logger::warn("[FO4] BSGraphics::State* address not resolved (NG) — graphicsState will be null");
					}
				}

				renderer = reinterpret_cast<RE::BSGraphics::Renderer*>(
					reinterpret_cast<char*>(rendererData) - offsetof(RE::BSGraphics::Renderer, data));

				if (shadowState) {
					auto& runtimeData = shadowState->GetRuntimeData();
					currentPixelShader = &runtimeData.currentPixelShader;
					currentVertexShader = &runtimeData.currentVertexShader;
					stateUpdateFlags = reinterpret_cast<REX::EnumSet<RE::BSGraphics::ShaderFlags, uint32_t>*>(&runtimeData.stateUpdateFlags);
				}

				if (auto* liveDevice = reinterpret_cast<ID3D11Device*>(renderer->data.device)) {
					Util::RegisterD3DDeviceIdentity(liveDevice);
					if (auto canonical = Util::CanonicalD3DDevice(liveDevice)) {
						if (liveDevice != canonical.Get() && d3d::device != canonical.Get())
							logger::info("[MirrorsOfFallout] normalized D3D device identity: engine={} resource-owner={}",
								static_cast<void*>(liveDevice), static_cast<void*>(canonical.Get()));
						
						d3d::device = canonical.Get();
					} else {
						logger::error("[MirrorsOfFallout] live renderer device has no canonical ID3D11Device interface");
					}
				}
				if (auto* liveContext = reinterpret_cast<ID3D11DeviceContext*>(renderer->data.context))
					d3d::context = liveContext;
				if (renderer->data.renderWindow) {
					if (auto* liveSwapChain = reinterpret_cast<IDXGISwapChain*>(renderer->data.renderWindow->swapChain))
						d3d::swapChain = liveSwapChain;
				}
				DllTrace("ReInit: D3D pointers set");
			}

			static RE::BSShaderManager::State smStateStub{};
			smState = &smStateStub;

			isVR = REL::Module::IsVR();
			DllTrace("ReInit: getting singletons");
			iniSettingCollection = RE::INISettingCollection::GetSingleton();
			iniPrefSettingCollection = RE::INIPrefSettingCollection::GetSingleton();
			gameSettingCollection = RE::GameSettingCollection::GetSingleton();
			tes = RE::TES::GetSingleton();

			static float cameraNearStub = 10.0f;
			static float cameraFarStub = 100000.0f;
			static float deltaTimeStub = 0.016f;
			cameraNear = &cameraNearStub;
			cameraFar = &cameraFarStub;
			deltaTime = &deltaTimeStub;

			ui = RE::UI::GetSingleton();

		}

		DllTrace("ReInit: setting RTTI");
		{
			using namespace rtti;
			
			BSLightingShaderPropertyRTTI = RE::BSLightingShaderProperty::Ni_RTTI.address();
		}
		DllTrace("ReInit: complete");
	}

	void OnDataLoaded()
	{
		DllTrace("OnDataLoaded: entered");
		using namespace game;

		ReInit();

		sky = RE::Sky::GetSingleton();

		if (reinterpret_cast<uintptr_t>(sky) < 0x10000) {
			logger::warn("[FO4] Sky::GetSingleton() returned invalid pointer {}, treating as null", (void*)sky);
			sky = nullptr;
		}
		
		utilityShader = nullptr;

		iniSettingCollection = RE::INISettingCollection::GetSingleton();
		iniPrefSettingCollection = RE::INIPrefSettingCollection::GetSingleton();
		gameSettingCollection = RE::GameSettingCollection::GetSingleton();
		tes = RE::TES::GetSingleton();

		if (iniSettingCollection) {
			bEnableLandFade = iniSettingCollection->GetSetting("bEnableLandFade:Display");
		} else {
			logger::warn("[FO4 PORT] INISettingCollection singleton is null at DataLoaded");
		}

		bShadowsOnGrass = RE::GetINISetting("bShadowsOnGrass:Display");
		shadowMaskQuarter = RE::GetINISetting("iShadowMaskQuarter:Display");
	}

	void UpdatePerFrame()
	{
		using namespace game;

		{
			static LARGE_INTEGER qpcFreq = {};
			static LARGE_INTEGER qpcLast = {};
			static float deltaTimeLive = 0.016f;

			if (qpcFreq.QuadPart == 0) {
				QueryPerformanceFrequency(&qpcFreq);
				QueryPerformanceCounter(&qpcLast);
			}

			LARGE_INTEGER qpcNow;
			QueryPerformanceCounter(&qpcNow);
			float dt = static_cast<float>(qpcNow.QuadPart - qpcLast.QuadPart) / static_cast<float>(qpcFreq.QuadPart);
			qpcLast = qpcNow;

			if (dt > 0.0f && dt < 1.0f)
				deltaTimeLive = dt;
			else
				deltaTimeLive = 0.016f;

			deltaTime = &deltaTimeLive;
		}

		if (auto* cam = RE::Main::WorldRootCamera()) {
			static float cameraNearLive = 10.0f;
			static float cameraFarLive = 100000.0f;
			
			auto* frustumFloats = reinterpret_cast<const float*>(&cam->viewFrustum);
			cameraNearLive = frustumFloats[4];  
			cameraFarLive = frustumFloats[5];   
			cameraNear = &cameraNearLive;
			cameraFar = &cameraFarLive;
		}

		static std::uint32_t mirrorSwitchPoll = 0;
		if (++mirrorSwitchPoll % 60 == 0) {
			std::error_code ec;

			const bool unsafePerFace =
				std::filesystem::exists("Data/dynref_perfaceunsafe", ec) &&
				!std::filesystem::exists("Data/dynref_noocclude", ec);
			static bool lastUnsafePerFace = false;
			if (unsafePerFace != lastUnsafePerFace) {
				lastUnsafePerFace = unsafePerFace;
				MirrorSceneRenderer::OcclusionCull() = unsafePerFace;
				logger::warn("[MirrorsOfFallout] unsafe per-face visibility {}",
					unsafePerFace ? "ENABLED" : "disabled");
			}

			const bool albedoLoud =
				std::filesystem::exists("Data/dynref_albedoloud", ec);
			static bool lastAlbedoLoud = false;
			if (albedoLoud != lastAlbedoLoud) {
				lastAlbedoLoud = albedoLoud;
				g_albedoLoudDiagnostic = albedoLoud;
				logger::info("[MirrorsOfFallout] albedo diagnostic {}",
					albedoLoud ? "enabled" : "disabled");
			}

			const bool eyeGroup4NoPixel =
				std::filesystem::exists("Data/dynref_eyegroup4nopixel", ec);
			static bool lastEyeGroup4NoPixel = false;
			if (eyeGroup4NoPixel != lastEyeGroup4NoPixel) {
				lastEyeGroup4NoPixel = eyeGroup4NoPixel;
				MirrorSceneRenderer::EyeGroup4NoPixelRef() = eyeGroup4NoPixel;
			}

			const bool facePasses =
				std::filesystem::exists("Data/dynref_facepasses", ec);
			static bool lastFacePasses = false;
			if (facePasses != lastFacePasses) {
				lastFacePasses = facePasses;
				MirrorSceneRenderer::DrawStrandedFacePassesRef() = facePasses;
			}

			const bool skinRewrite =
				std::filesystem::exists("Data/dynref_skinrewrite", ec);
			static bool lastSkinRewrite = false;
			if (skinRewrite != lastSkinRewrite) {
				lastSkinRewrite = skinRewrite;
				MirrorSceneRenderer::SkinTintRewriteEnabledRef() = skinRewrite;
			}

			const bool allGroups =
				std::filesystem::exists("Data/dynref_allgroups", ec);
			static bool lastAllGroups = false;
			if (allGroups != lastAllGroups) {
				lastAllGroups = allGroups;
				MirrorSceneRenderer::CaptureRemainingGeometryGroupsRef() = allGroups;
			}

			const bool livePose =
				std::filesystem::exists("Data/dynref_livepose", ec);
			static bool lastLivePose = false;
			if (livePose != lastLivePose) {
				lastLivePose = livePose;
				MirrorSceneRenderer::LivePlayerPoseFallback() = livePose;
			}

			const bool lateDecals =
				std::filesystem::exists("Data/dynref_eyedecalslate", ec);
			static bool lastLateDecals = false;
			if (lateDecals != lastLateDecals) {
				lastLateDecals = lateDecals;
				MirrorSceneRenderer::LateBlendedDecals() = lateDecals;
			}

			const bool poseBridge =
				std::filesystem::exists("Data/dynref_posebridge", ec);
			static bool lastPoseBridge = false;
			if (poseBridge != lastPoseBridge) {
				lastPoseBridge = poseBridge;
				MirrorSceneRenderer::PoseBridgeDisabled() = !poseBridge;
			}

			const bool bridgeHead =
				std::filesystem::exists("Data/dynref_bridgehead", ec);
			static bool lastBridgeHead = false;
			if (bridgeHead != lastBridgeHead) {
				lastBridgeHead = bridgeHead;
				MirrorSceneRenderer::PoseBridgeIncludeHead() = bridgeHead;
			}
		}

	}

	std::atomic<bool> gameDataReadyComplete{false};

constexpr const char* kEyeAlbedoPSSource = R"(
Texture2D<float4> EyeDiffuse : register(t0);
Texture2D<float4> EyeNormal : register(t1);
SamplerState EyeDiffuseSampler : register(s0);
SamplerState EyeNormalSampler : register(s1);
cbuffer EyePerGeometry : register(b2)
{
	float4 eyePerGeometry[7];
};
struct PSInput
{
	float4 position : SV_POSITION;
	// EyeHazel's exact BSDFPrePass permutation (PS descriptor 0x00018806) does not carry UV in
	// TEXCOORD0.xy. TEXCOORD0..2 are its tangent basis; the authored UV is packed into the W lanes of
	// TEXCOORD3 and TEXCOORD4. D3D10/11 stage linkage is physical-register ordered rather than a runtime
	// semantic remap, so these unused prefix fields are mandatory: without them the standalone replacement
	// PS packs TEXCOORD3/4 into v1/v2 and reads undefined tangent-basis W lanes instead of the native v4.w/v5.w.
	float3 tangentBasis0 : TEXCOORD0;
	float3 tangentBasis1 : TEXCOORD1;
	float3 tangentBasis2 : TEXCOORD2;
	float4 packedTexCoord3 : TEXCOORD3;
	float4 packedTexCoord4 : TEXCOORD4;
};
struct PSOutput
{
	float4 diffuse : SV_Target0;
	float4 normal : SV_Target1;
};
// The coherent bathroom material G-buffer has two targets. Target 0 carries
// sampled authored colour; Target 1 carries the native view-space normal.
PSOutput main(PSInput input)
{
	const float2 eyeUV = float2(input.packedTexCoord3.w, input.packedTexCoord4.w);
	const float4 albedo = EyeDiffuse.Sample(EyeDiffuseSampler, eyeUV);
	// Alpha testing is disabled on EyeLashesLeft, but texture alpha still drives its
	// authored SRC_ALPHA blend. Never gate atlas coverage on the alpha-test enable.
	const float nativeAlpha = saturate(eyePerGeometry[2].x * albedo.a);
	const float3 tangentNormal = EyeNormal.Sample(EyeNormalSampler, eyeUV).xyz * 2.0 - 1.0;
	const float3 viewNormal = normalize(float3(
		dot(input.tangentBasis0, tangentNormal),
		dot(input.tangentBasis1, tangentNormal),
		dot(input.tangentBasis2, tangentNormal)));
	const float denominator = max(0.001, sqrt(max(0.0, 8.0 - 8.0 * viewNormal.z)));
	PSOutput output;
	// EyeHazel is also the eyelash coverage atlas. Its transparent texels carry
	// black RGB, so preserve the authored alpha for the exact scoped straight-alpha
	// blend. The vanilla NiAlphaProperty has alpha testing disabled; do not clip.
	output.diffuse = float4(albedo.rgb, nativeAlpha);
	output.normal = float4(viewNormal.xy / denominator + 0.5, 0.0, nativeAlpha);
	return output;
}
)";

	constexpr const char* kFaceSkinTintPSSource = R"(
Texture2D<float4> SkinDiffuse : register(t0);
Texture2D<float4> SkinNormal : register(t1);
SamplerState SkinDiffuseSampler : register(s0);
SamplerState SkinNormalSampler : register(s1);
cbuffer SkinPerMaterial : register(b1)
{
	float4 LODTexParams : packoffset(c0);
	float4 TintColor : packoffset(c1);
};
struct PSInput
{
	float4 position : SV_POSITION;
	float3 tbn0 : TEXCOORD0;
	float3 tbn1 : TEXCOORD1;
	float3 tbn2 : TEXCOORD2;
	float4 currentPositionAndU : TEXCOORD3;
	float4 previousPositionAndV : TEXCOORD4;
};
struct PSOutput
{
	float4 diffuse : SV_Target0;
	float4 normal : SV_Target1;
};
PSOutput main(PSInput input)
{
	const float2 uv = float2(input.currentPositionAndU.w, input.previousPositionAndV.w);
	const float4 rawBase = SkinDiffuse.Sample(SkinDiffuseSampler, uv);
	const float4 rawNormal = SkinNormal.Sample(SkinNormalSampler, uv);

	// Bethesda FACEGEN_RGB_TINT, using the character's live per-material TintColor at b1/c1.
	float3 tint = TintColor.xyz * rawBase.rgb * 2.0.xxx;
	tint -= tint * rawBase.rgb;
	const float3 albedo = float3(1.01171875, 0.99609375, 1.01171875) *
		(rawBase.rgb * rawBase.rgb + tint);

	const float3 tangentNormal = rawNormal.xyz * 2.0.xxx - 1.0.xxx;
	const float3 screenNormal = normalize(float3(
		dot(input.tbn0, tangentNormal),
		dot(input.tbn1, tangentNormal),
		dot(input.tbn2, tangentNormal)));
	const float encodeDenominator = max(0.001, sqrt(max(0.0, 8.0 - 8.0 * screenNormal.z)));
	const float2 encodedNormal = screenNormal.xy / encodeDenominator + 0.5.xx;

	PSOutput output;
	output.diffuse = float4(albedo, 1.0);
	output.normal = float4(encodedNormal, 0.0, saturate(rawNormal.a));
	return output;
}
)";

	constexpr const char* kSkinAlbedoPSSource = R"(
Texture2D<float4> SkinDiffuse : register(t0);
SamplerState SkinDiffuseSampler : register(s0);
struct PSInput
{
	float4 position : SV_POSITION;
	float4 texCoord : TEXCOORD0;
};
struct PSOutput
{
	float4 diffuse : SV_Target0;
	float4 normal : SV_Target1;
};
PSOutput main(PSInput input)
{
	float3 albedo = SkinDiffuse.Sample(SkinDiffuseSampler, input.texCoord.xy).rgb;
	if (dot(albedo, float3(1.0, 1.0, 1.0)) <= 0.0)
		albedo = float3(0.62, 0.44, 0.32);
	PSOutput output;
	output.diffuse = float4(albedo, 1.0);
	output.normal = float4(0.5, 0.5, 0.0, 0.0);
	return output;
}
)";

	constexpr const char* kEyeAlbedoLoudPSSource = R"(
struct LoudOutput
{
	float4 diffuse : SV_Target0;
	float4 normal : SV_Target1;
};
LoudOutput main(float4 position : SV_POSITION)
{
	LoudOutput output;
	output.diffuse = float4(0.0, 1.0, 0.0, 1.0);
	output.normal = float4(0.5, 0.5, 0.0, 0.0);
	return output;
}
)";

	constexpr const char* kSkinAlbedoLoudPSSource = R"(
struct LoudOutput
{
	float4 diffuse : SV_Target0;
	float4 normal : SV_Target1;
};
LoudOutput main(float4 position : SV_POSITION)
{
	LoudOutput output;
	output.diffuse = float4(1.0, 0.0, 1.0, 1.0);
	output.normal = float4(0.5, 0.5, 0.0, 0.0);
	return output;
}
)";

	ID3D11PixelShader* g_eyeAlbedoPS = nullptr;
	ID3D11PixelShader* g_skinAlbedoPS = nullptr;
	ID3D11PixelShader* g_faceSkinTintPS = nullptr;
	ID3D11PixelShader* g_eyeAlbedoLoudPS = nullptr;
	ID3D11PixelShader* g_skinAlbedoLoudPS = nullptr;
	ID3D11PixelShader* g_mirrorEyeAuthoredColorPS = nullptr;
	ID3D11BlendState* g_mirrorEyeAuthoredAlphaBlend = nullptr;
	bool g_albedoLoudDiagnostic = false;
	ID3D11Device* g_eyeAlbedoPSDevice = nullptr;
	bool g_eyeAlbedoPSCompileAttempted = false;
	std::atomic<std::uint32_t> g_eyeOvCalls{ 0 };
	std::atomic<std::uint32_t> g_eyeOvMatches{ 0 };

	static ID3D11PixelShader* CompileAlbedoOverridePS(const char* source, const char* name)
	{
		auto* device = globals::d3d::device;
		if (!device)
			return nullptr;
		ID3DBlob* blob = nullptr;
		ID3DBlob* errors = nullptr;
		const HRESULT hr = D3DCompile(
			source, std::strlen(source), name, nullptr, nullptr, "main", "ps_5_0", 0, 0, &blob, &errors);
		if (FAILED(hr) || !blob) {
			logger::error("[MirrorSceneRenderer] {} compile FAILED (hr=0x{:X}): {}",
				name, static_cast<std::uint32_t>(hr),
				errors ? static_cast<const char*>(errors->GetBufferPointer()) : "<no output>");
			if (errors)
				errors->Release();
			if (blob)
				blob->Release();
			return nullptr;
		}
		if (errors)
			errors->Release();
		ID3D11PixelShader* shader = nullptr;
		const HRESULT createHr = device->CreatePixelShader(
			blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &shader);
		blob->Release();
		if (FAILED(createHr)) {
			logger::error("[MirrorSceneRenderer] {} CreatePixelShader FAILED (hr=0x{:X})",
				name, static_cast<std::uint32_t>(createHr));
			return nullptr;
		}
		logger::info("[MirrorSceneRenderer] {} compiled and created", name);
		return shader;
	}

	static ID3D11BlendState* CreateMirrorEyeAuthoredAlphaBlend()
	{
		auto* device = globals::d3d::device;
		if (!device)
			return nullptr;
		D3D11_BLEND_DESC description{};
		auto& target = description.RenderTarget[0];
		target.BlendEnable = TRUE;
		target.SrcBlend = D3D11_BLEND_SRC_ALPHA;
		target.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
		target.BlendOp = D3D11_BLEND_OP_ADD;
		target.SrcBlendAlpha = D3D11_BLEND_ONE;
		target.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
		target.BlendOpAlpha = D3D11_BLEND_OP_ADD;
		target.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
		ID3D11BlendState* blend = nullptr;
		const HRESULT hr = device->CreateBlendState(std::addressof(description), std::addressof(blend));
		if (FAILED(hr) || !blend) {
			logger::error(
				"[MirrorSceneRenderer] MirrorEyeAuthoredAlphaBlend CreateBlendState FAILED (hr=0x{:X})",
				static_cast<std::uint32_t>(hr));
			if (blend)
				blend->Release();
			return nullptr;
		}
		logger::info("[MirrorSceneRenderer] MirrorEyeAuthoredAlphaBlend created");
		return blend;
	}

	ID3D11PixelShader* GetAlbedoOverridePS(int kind);

	static void EnsureAlbedoOverrideShaders()
	{
		auto* device = globals::d3d::device;
		if (!device)
			return;
		if (device != g_eyeAlbedoPSDevice) {
			if (g_eyeAlbedoPS)
				g_eyeAlbedoPS->Release();
			if (g_skinAlbedoPS)
				g_skinAlbedoPS->Release();
			if (g_faceSkinTintPS)
				g_faceSkinTintPS->Release();
			if (g_eyeAlbedoLoudPS)
				g_eyeAlbedoLoudPS->Release();
			if (g_skinAlbedoLoudPS)
				g_skinAlbedoLoudPS->Release();
			if (g_mirrorEyeAuthoredColorPS)
				g_mirrorEyeAuthoredColorPS->Release();
			if (g_mirrorEyeAuthoredAlphaBlend)
				g_mirrorEyeAuthoredAlphaBlend->Release();
			g_eyeAlbedoPS = nullptr;
			g_skinAlbedoPS = nullptr;
			g_faceSkinTintPS = nullptr;
			g_eyeAlbedoLoudPS = nullptr;
			g_skinAlbedoLoudPS = nullptr;
			g_mirrorEyeAuthoredColorPS = nullptr;
			g_mirrorEyeAuthoredAlphaBlend = nullptr;
			g_eyeAlbedoPSDevice = device;
			g_eyeAlbedoPSCompileAttempted = false;
		}
		if (g_eyeAlbedoPSCompileAttempted)
			return;
		g_eyeAlbedoPSCompileAttempted = true;
		g_eyeAlbedoPS = CompileAlbedoOverridePS(kEyeAlbedoPSSource, "EyeAlbedoPS");
		g_skinAlbedoPS = CompileAlbedoOverridePS(kSkinAlbedoPSSource, "SkinAlbedoPS");
		g_faceSkinTintPS = CompileAlbedoOverridePS(
			kFaceSkinTintPSSource, "FaceSkinTintPS");
		g_eyeAlbedoLoudPS = CompileAlbedoOverridePS(kEyeAlbedoLoudPSSource, "EyeAlbedoLoudPS");
		g_skinAlbedoLoudPS = CompileAlbedoOverridePS(kSkinAlbedoLoudPSSource, "SkinAlbedoLoudPS");
		g_mirrorEyeAuthoredColorPS = static_cast<ID3D11PixelShader*>(Util::CompileShader(
			L"Data\\Shaders\\MirrorsOfFallout\\FlatMirrorEyePS.hlsl", {}, "ps_5_0"));
		g_mirrorEyeAuthoredAlphaBlend = CreateMirrorEyeAuthoredAlphaBlend();
	}

	enum class AlbedoOverrideKind
	{
		kNone,
		kEye,
		kSkin,
		kFaceSkinTint
	};

	ID3D11PixelShader* GetAlbedoOverridePS(int kind)
	{
		if (kind != 1 && kind != 2)
			return nullptr;
		EnsureAlbedoOverrideShaders();
		if (g_albedoLoudDiagnostic)
			return kind == 1 ? g_eyeAlbedoLoudPS : g_skinAlbedoLoudPS;
		return kind == 1 ? g_eyeAlbedoPS : g_skinAlbedoPS;
	}

	ID3D11PixelShader* GetProductionEyeAlbedoPS()
	{
		EnsureAlbedoOverrideShaders();
		return g_eyeAlbedoPS;
	}

	ID3D11PixelShader* GetProductionFaceSkinTintPS()
	{
		EnsureAlbedoOverrideShaders();
		return g_faceSkinTintPS;
	}

	ID3D11PixelShader* GetProductionMirrorEyeAuthoredColorPS()
	{
		EnsureAlbedoOverrideShaders();
		return g_mirrorEyeAuthoredColorPS;
	}

	ID3D11BlendState* GetProductionMirrorEyeAuthoredAlphaBlend()
	{
		EnsureAlbedoOverrideShaders();
		return g_mirrorEyeAuthoredAlphaBlend;
	}

	static void TryPrepareEyeAlbedoControl(ID3D11DeviceContext* ctx)
	{
		g_eyeOvCalls.fetch_add(1, std::memory_order_relaxed);

		const int kindValue = MirrorSceneRenderer::ClassifyCurrentArmedDrawMaterial();
		if (kindValue == 0)
			return;
		const AlbedoOverrideKind kind = kindValue == 1 ? AlbedoOverrideKind::kEye :
			(kindValue == 3 ? AlbedoOverrideKind::kFaceSkinTint : AlbedoOverrideKind::kSkin);
		g_eyeOvMatches.fetch_add(1, std::memory_order_relaxed);
		if (kind == AlbedoOverrideKind::kEye) {

			if (MirrorSceneRenderer::PrepareEyeGroup4AlbedoPSOverride(ctx)) {
				static std::uint32_t s_eyeNativeControlLogs = 0;
				if (s_eyeNativeControlLogs++ < 8u) {
					logger::info(
						"[MirrorSceneRenderer] EYEALBEDOCONTROL native inputs captured; production EyeHazel PS and private blend ready for final draw");
				}
			}
			return;
		}
		if (kind == AlbedoOverrideKind::kFaceSkinTint) {

			return;
		}

		EnsureAlbedoOverrideShaders();
		auto* overridePS = g_skinAlbedoPS;
		if (g_albedoLoudDiagnostic)
			overridePS = g_skinAlbedoLoudPS;
		if (overridePS) {
			ctx->PSSetShader(overridePS, nullptr, 0);
			static std::uint32_t s_eyeOverrideLogs = 0;
			if (s_eyeOverrideLogs < 8u) {
				++s_eyeOverrideLogs;
				logger::info(
					"[MirrorSceneRenderer] legacy skin albedo diagnostic override bound for a private capture draw");
				spdlog::default_logger_raw()->flush();
			}
		}
	}

	struct ID3D11DeviceContext_DrawIndexed
	{
		static void thunk(ID3D11DeviceContext* This, UINT IndexCount, UINT StartIndexLocation, INT BaseVertexLocation)
		{
			MirrorDrawHooks::Forward(func, This, IndexCount, StartIndexLocation, BaseVertexLocation);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct ID3D11DeviceContext_Draw
	{
		static void thunk(ID3D11DeviceContext* This, UINT VertexCount, UINT StartVertexLocation)
		{
			MirrorDrawHooks::Forward(func, This, VertexCount, StartVertexLocation);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct ID3D11DeviceContext_DrawIndexedInstanced
	{
		static void thunk(ID3D11DeviceContext* This, UINT IndexCountPerInstance, UINT InstanceCount,
			UINT StartIndexLocation, INT BaseVertexLocation, UINT StartInstanceLocation)
		{
			MirrorDrawHooks::Forward(func, This, IndexCountPerInstance, InstanceCount,
				StartIndexLocation, BaseVertexLocation, StartInstanceLocation);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	[[nodiscard]] bool DrawSeamOwnedBySystemRuntime(ID3D11DeviceContext* a_context) noexcept
	{
		if (!a_context)
			return false;
		auto** vtable = *reinterpret_cast<void***>(a_context);
		if (!vtable)
			return false;
		std::array<wchar_t, MAX_PATH> systemPath{};
		const auto systemLength = GetSystemDirectoryW(systemPath.data(), static_cast<UINT>(systemPath.size()));
		if (systemLength == 0 || systemLength >= systemPath.size())
			return false;
		for (const std::size_t slot : { std::size_t{ 12 }, std::size_t{ 13 }, std::size_t{ 20 } }) {
			auto* entry = vtable[slot];
			HMODULE owner = nullptr;
			if (!entry ||
				!GetModuleHandleExW(
					GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
					reinterpret_cast<LPCWSTR>(entry), std::addressof(owner)) ||
				!owner)
				return false;
			std::array<wchar_t, MAX_PATH> modulePath{};
			const auto moduleLength =
				GetModuleFileNameW(owner, modulePath.data(), static_cast<DWORD>(modulePath.size()));
			if (moduleLength == 0 || moduleLength >= modulePath.size() ||
				_wcsnicmp(modulePath.data(), systemPath.data(), systemLength) != 0)
				return false;
		}
		return true;
	}

	void InstallD3DHooks(ID3D11DeviceContext* a_context)
	{
		const bool systemOwned = DrawSeamOwnedBySystemRuntime(a_context);
		MirrorSceneRenderer::NoteD3DDrawSeamWrapper(!systemOwned);
		if (!systemOwned) {
			logger::warn(
				"[MirrorsOfFallout] device-context draw entries are not the system d3d11 runtime (a wrapper such "
				"as ENB owns them); the draw seam is not installed. Mirrors still capture and present.");
			return;
		}
		if (!stl::detour_vfunc<12, ID3D11DeviceContext_DrawIndexed>(a_context))
			return;
		(void)MirrorDrawHooks::registry.Register(12, reinterpret_cast<const void*>(&ID3D11DeviceContext_DrawIndexed::thunk));
		if (!stl::detour_vfunc<13, ID3D11DeviceContext_Draw>(a_context))
			return;
		(void)MirrorDrawHooks::registry.Register(13, reinterpret_cast<const void*>(&ID3D11DeviceContext_Draw::thunk));
		if (!stl::detour_vfunc<20, ID3D11DeviceContext_DrawIndexedInstanced>(a_context))
			return;
		(void)MirrorDrawHooks::registry.Register(20, reinterpret_cast<const void*>(&ID3D11DeviceContext_DrawIndexedInstanced::thunk));
		logger::info("[MirrorsOfFallout] mirror-only D3D draw seams installed");
	}
}

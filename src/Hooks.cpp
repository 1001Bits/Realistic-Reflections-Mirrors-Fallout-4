

#include "Hooks.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <memory>
#include <mutex>
#include <set>
#include <type_traits>
#include <utility>

#include "Globals.h"

#include <DbgHelp.h>
#include "Utils/Format.h"

#include "Features/FlatDeferredPlayerCapture.h"
#include "Features/CellMusicFallbackCode.h"
#include "Features/MirrorAnimationBinding.h"
#include "Features/MirrorPerformance.h"
#include "Features/MirrorPerformanceOverlay.h"
#include "Features/MirrorToggle.h"
#include "Features/MirrorCaptureOptimizations.h"
#include "Features/MirrorSettings.h"
#include "Features/MirrorNativeAmbientProbe.h"
#include "Features/MirrorTextureRouting.h"
#include "Features/PlanarMirrorLookup.h"
#include "Features/PlanarMirrors.h"
#include "Features/ReflectionRuntime.h"

namespace
{

	[[nodiscard]] inline const char* ShaderFxpFilename(const RE::BSShader* shader) noexcept
	{
		if (!shader)
			return nullptr;
		const auto offset = ReflectionRuntime::IsPort240() ? 0x188u : 0x110u;
		return *reinterpret_cast<const char* const*>(reinterpret_cast<std::uintptr_t>(shader) + offset);
	}

}
#include "Features/MirrorSceneRenderer.h"
#include "Features/VRReflectionRenderer.h"

namespace
{
	std::atomic<std::uintptr_t> g_overlayForwardWndProc{ 0 };
	std::atomic<HWND> g_overlayOutputWindow{ nullptr };
	std::atomic_bool g_overlayOutputWindowHookReady{ false };
	
	MirrorToggle::Requests g_mirrorToggleRequests{VK_F12};
	MirrorToggle::Requests g_mirrorOverlayRequests{VK_F11};
	
	MirrorToggle::Requests g_optimizationRequests{VK_F7};
	MirrorToggle::Requests g_benchmarkRequests{VK_F8};
	MirrorToggle::Requests& DebugKeyRequests(WPARAM key) noexcept
	{
		return key == VK_F11 ? g_mirrorOverlayRequests : key == VK_F12 ? g_mirrorToggleRequests :
			key == VK_F7 ? g_optimizationRequests : g_benchmarkRequests;
	}

	LRESULT CALLBACK MirrorsOfFalloutWndProc(
		HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
	{
		const bool closeRequested =
			(message == WM_SYSCOMMAND && (wParam & 0xFFF0u) == SC_CLOSE) ||
			message == WM_CLOSE || (message == WM_ENDSESSION && wParam != FALSE);
		if (closeRequested)
			MirrorSceneRenderer::NotifyShutdown();

		if (message == WM_KILLFOCUS) {
			(void)g_mirrorToggleRequests.OnKeyMessage(message, wParam, static_cast<std::uintptr_t>(lParam));
			(void)g_mirrorOverlayRequests.OnKeyMessage(message, wParam, static_cast<std::uintptr_t>(lParam));
			(void)g_optimizationRequests.OnKeyMessage(message, wParam, static_cast<std::uintptr_t>(lParam));
			(void)g_benchmarkRequests.OnKeyMessage(message, wParam, static_cast<std::uintptr_t>(lParam));
		} else if ((wParam == VK_F11 || wParam == VK_F12 || wParam == VK_F7 || wParam == VK_F8) &&
			MirrorSettings::DebugKeysEnabled() &&
			(message == WM_KEYDOWN || message == WM_KEYUP ||
			message == WM_SYSKEYDOWN || message == WM_SYSKEYUP)) {
			const bool modified = (GetKeyState(VK_CONTROL) & 0x8000) != 0 ||
				(GetKeyState(VK_SHIFT) & 0x8000) != 0;
			auto& requests = DebugKeyRequests(wParam);
			if (!modified && requests.OnKeyMessage(message, wParam, static_cast<std::uintptr_t>(lParam)))
				return 0;
		}

		const auto forwardAddress = g_overlayForwardWndProc.load(std::memory_order_acquire);
		auto* forward = reinterpret_cast<WNDPROC>(forwardAddress);
		return forward && forward != &MirrorsOfFalloutWndProc ?
			CallWindowProcW(forward, hwnd, message, wParam, lParam) :
			DefWindowProcW(hwnd, message, wParam, lParam);
	}

	bool EnsureOverlayOutputWindowHook(HWND outputWindow) noexcept
	{
		if (!outputWindow) return false;
		
		if (GetWindowThreadProcessId(outputWindow, nullptr) != GetCurrentThreadId())
			return false;
		const HWND installedWindow = g_overlayOutputWindow.load(std::memory_order_acquire);
		if (installedWindow == outputWindow &&
			g_overlayOutputWindowHookReady.load(std::memory_order_acquire)) {

			return true;
		}
		if (installedWindow && installedWindow != outputWindow) {
			static bool loggedWindowChange = false;
			if (!loggedWindowChange) {
				loggedWindowChange = true;
				logger::error(
					"[FO4] OutputWindow changed after WndProc subclass (old={} new={}); leaving new window unmodified to preserve the old subclass chain",
					reinterpret_cast<void*>(installedWindow), reinterpret_cast<void*>(outputWindow));
			}
			return false;
		}
		const auto currentAddress = static_cast<std::uintptr_t>(
			GetWindowLongPtrW(outputWindow, GWLP_WNDPROC));
		const auto ours = reinterpret_cast<std::uintptr_t>(&MirrorsOfFalloutWndProc);
		if (currentAddress != ours) {
			g_overlayForwardWndProc.store(currentAddress, std::memory_order_release);
			SetLastError(ERROR_SUCCESS);
			const auto previousAddress = static_cast<std::uintptr_t>(SetWindowLongPtrW(
				outputWindow, GWLP_WNDPROC, static_cast<LONG_PTR>(ours)));
			if (previousAddress == 0 && GetLastError() != ERROR_SUCCESS) {
				static bool loggedFailure = false;
				if (!loggedFailure) {
					loggedFailure = true;
					logger::error(
						"[FO4] OutputWindow WndProc subclass FAILED: hwnd={} error={}",
						reinterpret_cast<void*>(outputWindow), GetLastError());
				}
				return false;
			}
			if (previousAddress != 0 && previousAddress != ours)
				g_overlayForwardWndProc.store(previousAddress, std::memory_order_release);
		}

		const bool verified = static_cast<std::uintptr_t>(
			GetWindowLongPtrW(outputWindow, GWLP_WNDPROC)) == ours;
		g_overlayOutputWindow.store(outputWindow, std::memory_order_release);
		g_overlayOutputWindowHookReady.store(verified, std::memory_order_release);
		static bool logged = false;
		if (!logged) {
			logged = true;
			logger::info(
				"[FO4] OutputWindow WndProc subclass: hwnd={} previous={} installed={} verified={}",
				reinterpret_cast<void*>(outputWindow), reinterpret_cast<void*>(currentAddress),
				reinterpret_cast<void*>(ours), verified);
		}
		return verified;
	}
}

void Hooks::ServiceMirrorToggle() noexcept
{
	if (MirrorSceneRenderer::ShutdownRequested() || MirrorSceneRenderer::PrivateRenderActive() ||
		VRReflectionRenderer::PrivateCaptureActive())
		return;
	
	static bool s_benchmarkOwned = false;
	if (MirrorPerformance::BenchmarkActive()) {
		g_mirrorToggleRequests.Clear();
		const bool wanted = MirrorPerformance::BenchmarkWantsMirrorsOn();
		if (MirrorSceneRenderer::Enabled() != wanted) {
			MirrorSceneRenderer::Enabled() = wanted;
			MirrorToggle::active.store(wanted, std::memory_order_release);
			logger::info("[Mirrors BENCH] mirrors {}", wanted ? "ON" : "OFF");
		}
		MirrorPerformance::SetEnabled(wanted);
		s_benchmarkOwned = true;
		return;
	}
	if (std::exchange(s_benchmarkOwned, false) && !MirrorSceneRenderer::Enabled()) {
		MirrorSceneRenderer::Enabled() = true;
		MirrorToggle::active.store(true, std::memory_order_release);
		MirrorPerformance::SetEnabled(true);
		logger::info("[Mirrors BENCH] benchmark ended: mirrors ON");
	}
	
	if (!MirrorSettings::DebugKeysEnabled()) {
		g_mirrorToggleRequests.Clear();
		if (!MirrorSceneRenderer::Enabled()) {
			MirrorSceneRenderer::Enabled() = true;
			MirrorToggle::active.store(true, std::memory_order_release);
			logger::info("[Mirrors AB] debug keys off: mirrors ON");
		}
		MirrorPerformance::SetEnabled(true);
		return;
	}
	const bool changed = g_mirrorToggleRequests.Consume(MirrorSceneRenderer::Enabled());
	MirrorPerformance::SetEnabled(MirrorSceneRenderer::Enabled());
	if (changed) {
		logger::info("[Mirrors AB] F12: mirrors {}; captures, presentation and private animation follow the master switch",
			MirrorSceneRenderer::Enabled() ? "ON" : "OFF");
		spdlog::default_logger()->flush();
	}
}

__declspec(thread) const char* g_crashContext = "unknown";

static HMODULE GetOwnModule()
{
	static HMODULE s_self = nullptr;
	if (!s_self)
		GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			reinterpret_cast<LPCSTR>(&GetOwnModule), &s_self);
	return s_self;
}

static LONG WINAPI CrashHandler(EXCEPTION_POINTERS* ExceptionInfo)
{
	auto code = ExceptionInfo->ExceptionRecord->ExceptionCode;
	auto addr = ExceptionInfo->ExceptionRecord->ExceptionAddress;
	auto rip = ExceptionInfo->ContextRecord->Rip;

	if (code == EXCEPTION_ACCESS_VIOLATION ||
		code == EXCEPTION_STACK_OVERFLOW ||
		code == EXCEPTION_ILLEGAL_INSTRUCTION ||
		code == EXCEPTION_INT_DIVIDE_BY_ZERO) {

		{
			HMODULE ripMod = nullptr;
			GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
				reinterpret_cast<LPCSTR>(rip), &ripMod);
			if (ripMod && ripMod != GetOwnModule() && ripMod != GetModuleHandleA(nullptr))
				return EXCEPTION_CONTINUE_SEARCH;
		}

		static constexpr int kMaxTrackedRips = 32;
		static constexpr int kMaxPerRip = 2;
		static constexpr int kMaxTotalLogs = 50;
		struct RipEntry { std::atomic<uintptr_t> rip{ 0 }; std::atomic<int> count{ 0 }; };
		static RipEntry s_rips[kMaxTrackedRips];
		static std::atomic<int> s_totalLogs{ 0 };
		static std::atomic<int> s_nextSlot{ 0 };

		if (s_totalLogs.load() >= kMaxTotalLogs)
			return EXCEPTION_CONTINUE_SEARCH;

		int slot = -1;
		for (int i = 0; i < kMaxTrackedRips; i++) {
			if (s_rips[i].rip.load() == rip) { slot = i; break; }
			uintptr_t zero = 0;
			if (s_rips[i].rip.compare_exchange_strong(zero, rip)) { slot = i; break; }
		}
		if (slot < 0)
			return EXCEPTION_CONTINUE_SEARCH;  

		int count = s_rips[slot].count.fetch_add(1);
		if (count >= kMaxPerRip)
			return EXCEPTION_CONTINUE_SEARCH;  

		s_totalLogs.fetch_add(1);

		static std::atomic<bool> s_firstWrite{ true };
		char path[MAX_PATH];
		if (GetEnvironmentVariableA("USERPROFILE", path, MAX_PATH))
			strcat_s(path, "\\Documents\\RealisticReflectionsMirrors_crash.log");
		else
			strcpy_s(path, "C:\\RealisticReflectionsMirrors_crash.log");
		FILE* f = nullptr;
		fopen_s(&f, path, s_firstWrite.exchange(false) ? "w" : "a");
		if (f) {
			fprintf(f, "EXCEPTION (first chance; recovery not yet known): code=0x%08lX addr=%p RIP=0x%llX\n",
				code, addr, (unsigned long long)rip);
			if (code == EXCEPTION_ACCESS_VIOLATION && ExceptionInfo->ExceptionRecord->NumberParameters >= 2) {
				fprintf(f, "  Access violation: %s address 0x%llX\n",
					ExceptionInfo->ExceptionRecord->ExceptionInformation[0] == 0 ? "reading" :
						(ExceptionInfo->ExceptionRecord->ExceptionInformation[0] == 8 ? "executing" : "writing"),
					(unsigned long long)ExceptionInfo->ExceptionRecord->ExceptionInformation[1]);
			}
			
			HMODULE hMod = nullptr;
			GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, (LPCSTR)rip, &hMod);
			if (hMod) {
				char modName[MAX_PATH];
				GetModuleFileNameA(hMod, modName, MAX_PATH);
				fprintf(f, "  Module: %s base=0x%llX RVA=0x%llX\n",
					modName, (unsigned long long)hMod, (unsigned long long)(rip - (uintptr_t)hMod));
			}
			
			fprintf(f, "  Context: %s\n", g_crashContext);
			
			auto rsp = ExceptionInfo->ContextRecord->Rsp;
			fprintf(f, "  Stack (RSP=0x%llX):", (unsigned long long)rsp);
			for (int i = 0; i < 16; i++) {
				__try {
					auto val = *reinterpret_cast<uintptr_t*>(rsp + i * 8);
					HMODULE stackMod = nullptr;
					if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)val, &stackMod)) {
						fprintf(f, " [%d]=0x%llX", i, (unsigned long long)val);
					}
				} __except (EXCEPTION_EXECUTE_HANDLER) {
					break;
				}
			}
			fprintf(f, "\n");
			
			auto dev = globals::d3d::device;
			if (dev) {
				HRESULT hr = dev->GetDeviceRemovedReason();
				if (hr != S_OK)
					fprintf(f, "  D3D Device removed! Reason=0x%08lX\n", (unsigned long)hr);
			}
			fclose(f);
		}

		logger::warn("[EXCEPTION] code=0x{:08X} addr={} RIP=0x{:X}; first chance, recovery not yet known; first two events per address logged", code, addr, rip);
	}
	return EXCEPTION_CONTINUE_SEARCH;
}

namespace
{

	struct BSGraphics_BuildCameraStateData
	{
		static void thunk(
			RE::BSGraphics::State* state,
			void* output,
			RE::NiCamera* camera,
			bool useJitter)
		{
			func(state, output, camera, useJitter);
			if (!output || !camera)
				return;
			if (ReflectionRuntime::IsVR())
				PlanarMirrors::PatchVRCameraStateData(camera, output);
			else
				PlanarMirrors::PatchCameraStateData(
					camera, *static_cast<RE::BSGraphics::CameraStateData*>(output));
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	constexpr std::uintptr_t kRVA_GameTLSIndexHooks = 0x67347B4;           
	constexpr std::uintptr_t kRVA_DefaultContextHooks = 0x61DDC68;         
	constexpr std::uintptr_t kRVA_State_SetCameraViewPort = 0x1D21AB0;     
	constexpr std::uintptr_t kRVA_Renderer_SetPerFrameConstants = 0x1D11160;  
	constexpr std::uintptr_t kRVA_RendererInstanceHooks = 0x61E0900;       

	[[nodiscard]] std::uintptr_t CurrentRenderContext240() noexcept
	{
		const auto base = REL::Module::get().base();
		const auto tlsIndex = *reinterpret_cast<const std::uint32_t*>(base + ReflectionRuntime::Rva(kRVA_GameTLSIndexHooks));
		const auto tlsArray = __readgsqword(0x58);
		std::uintptr_t context = 0;
		if (tlsArray) {
			const auto tlsBlock = *reinterpret_cast<const std::uintptr_t*>(tlsArray + static_cast<std::uintptr_t>(tlsIndex) * 8u);
			if (tlsBlock)
				context = *reinterpret_cast<const std::uintptr_t*>(tlsBlock + 0xB20u);
		}
		if (!context)
			context = *reinterpret_cast<const std::uintptr_t*>(base + ReflectionRuntime::Rva(kRVA_DefaultContextHooks));
		return context;
	}

	[[nodiscard]] RE::BSGraphics::CameraStateData* FindCameraStateData240(
		RE::BSGraphics::State* state, const RE::NiCamera* camera, bool useJitter) noexcept
	{
		const auto stateBytes = reinterpret_cast<std::uintptr_t>(state);
		auto* entries = *reinterpret_cast<RE::BSGraphics::CameraStateData**>(stateBytes + 0x140u);
		const auto count = *reinterpret_cast<const std::uint32_t*>(stateBytes + 0x150u);
		if (!entries || count == 0u || count > 64u)
			return nullptr;
		for (std::uint32_t i = 0; i < count; ++i) {
			auto& entry = entries[i];
			if (entry.referenceCamera == camera && entry.useJitter == useJitter)
				return std::addressof(entry);
		}
		return nullptr;
	}

	void ApplyCameraStateData240(
		RE::BSGraphics::State* state,
		const RE::BSGraphics::CameraStateData* data,
		const RE::NiRect<float>* port,
		float nearPlane,
		float farPlane) noexcept
	{
		const auto context = CurrentRenderContext240();
		if (!context)
			return;
		const auto* src = reinterpret_cast<const std::uint8_t*>(data);
		auto* dst = reinterpret_cast<std::uint8_t*>(context);
		std::memcpy(dst + 0x2260u, src + 0x020u, 0x30u);
		std::memcpy(dst + 0x2290u, src + 0x050u, 0xC0u);
		std::memcpy(dst + 0x2350u, src + 0x110u, 0x20u);
		std::memcpy(dst + 0x2370u, src + 0x130u, 0x20u);
		std::memcpy(dst + 0x23D0u, src + 0x190u, 0x40u);
		std::memcpy(dst + 0x2230u, src + 0x228u, 0x0Cu);
		std::memcpy(dst + 0x2220u, src + 0x210u, 0x0Cu);
		using SetCameraViewPort_t = void (*)(RE::BSGraphics::State*, const RE::NiRect<float>*, float, float);
		using SetPerFrameConstants_t = void (*)(void*);
		const auto base = REL::Module::get().base();
		reinterpret_cast<SetCameraViewPort_t>(base + ReflectionRuntime::Rva(kRVA_State_SetCameraViewPort))(
			state, port, nearPlane, farPlane);
		reinterpret_cast<SetPerFrameConstants_t>(base + ReflectionRuntime::Rva(kRVA_Renderer_SetPerFrameConstants))(
			reinterpret_cast<void*>(base + ReflectionRuntime::Rva(kRVA_RendererInstanceHooks)));
	}

	struct BSGraphics_SetCameraData
	{
		static constexpr std::uintptr_t kRVA_FindCameraStateData = 0x1D231F0;
		static constexpr std::uintptr_t kRVA_ApplyCameraStateData = 0x1D22F20;

		using FindCameraStateData_t = RE::BSGraphics::CameraStateData* (*)(
			RE::BSGraphics::State*, const RE::NiCamera*, bool);
		using ApplyCameraStateData_t = void (*)(
			RE::BSGraphics::State*,
			const RE::BSGraphics::CameraStateData*,
			const RE::NiRect<float>*,
			float,
			float);

		static void thunk(
			RE::BSGraphics::State* state,
			RE::NiCamera* camera,
			bool useJitter,
			float nearPlane,
			float farPlane)
		{
			if (!state || !camera || ReflectionRuntime::IsVR() ||
				!PlanarMirrors::CameraOverrideActive(camera)) {
				func(state, camera, useJitter, nearPlane, farPlane);
				return;
			}

			static_assert(sizeof(RE::BSGraphics::CameraStateData) == 0x250);
			static_assert(alignof(RE::BSGraphics::CameraStateData) >= 16);
			static_assert(std::is_trivially_copyable_v<RE::BSGraphics::CameraStateData>);
			static_assert(offsetof(RE::BSGraphics::CameraStateData, posAdjust) == 0x210);
			static_assert(offsetof(RE::BSGraphics::CameraStateData, currentPosAdjust) == 0x21C);
			static_assert(offsetof(RE::BSGraphics::CameraStateData, previousPosAdjust) == 0x228);
			static_assert(offsetof(RE::BSGraphics::CameraStateData, referenceCamera) == 0x238);
			static_assert(offsetof(RE::BSGraphics::CameraStateData, useJitter) == 0x240);
			static_assert(offsetof(RE::BSGraphics::State, cameraDataCache) == 0x140);
			static_assert(offsetof(RE::BSGraphics::State, cameraState) == 0x160);
			static_assert(offsetof(RE::NiCamera, port) == 0x184);

			const bool port240 = ReflectionRuntime::IsPort240();
			RE::BSGraphics::CameraStateData* cachedState = nullptr;
			if (port240) {
				cachedState = FindCameraStateData240(state, camera, useJitter);
			} else {
				auto findCameraStateData = reinterpret_cast<FindCameraStateData_t>(
					REL::Offset(ReflectionRuntime::Rva(kRVA_FindCameraStateData)).address());
				cachedState = findCameraStateData(state, camera, useJitter);
			}
			const auto* seedState = cachedState ? cachedState : std::addressof(state->cameraState);

			alignas(16) RE::BSGraphics::CameraStateData localState;
			std::memcpy(std::addressof(localState), seedState, sizeof(localState));
			BSGraphics_BuildCameraStateData::func(state, std::addressof(localState), camera, useJitter);
			const bool projectionAvailable = PlanarMirrors::PatchCameraStateData(camera, localState);
			if (port240) {
				ApplyCameraStateData240(
					state, std::addressof(localState), std::addressof(camera->port), nearPlane, farPlane);
			} else {
				auto applyCameraStateData = reinterpret_cast<ApplyCameraStateData_t>(
					REL::Offset(ReflectionRuntime::Rva(kRVA_ApplyCameraStateData)).address());
				applyCameraStateData(
					state, std::addressof(localState), std::addressof(camera->port), nearPlane, farPlane);
			}

			using Clock = std::chrono::steady_clock;
			static thread_local std::uint64_t rebuildCount = 0;
			static thread_local Clock::time_point previousRebuild{};
			const auto now = Clock::now();
			const double intervalMs = rebuildCount == 0 ? 0.0 :
				std::chrono::duration<double, std::milli>(now - previousRebuild).count();
			previousRebuild = now;
			++rebuildCount;
			if (rebuildCount <= 4u || (rebuildCount % 120u) == 0u) {
				DirectX::XMFLOAT4X4 appliedViewProjection{};
				bool obliqueApplied = false;
				const bool storedProjectionAvailable = PlanarMirrors::GetPatchedViewProjection(
					camera, appliedViewProjection, std::addressof(obliqueApplied));
				try {
					logger::info(
						"[PlanarMirrors] fresh reflected camera state applied seed={} useJitter={} "
						"projectionAvailable={} storedProjectionAvailable={} obliqueApplied={} count={} intervalMs={:.2f}",
						cachedState ? "cache" : "state",
						useJitter,
						projectionAvailable,
						storedProjectionAvailable,
						obliqueApplied,
						rebuildCount,
						intervalMs);
				} catch (...) {
					
				}
			}
		}

		static inline REL::Relocation<decltype(thunk)> func;
	};

	constexpr std::array<std::uint8_t, 20> kFlatSetCameraDataPrologue{
		0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10,
		0x48, 0x89, 0x74, 0x24, 0x18, 0x57, 0x48, 0x83, 0xEC, 0x40
	};
	constexpr std::array<std::uint8_t, 33> kFlatBuildCameraStateDataPrologue{
		0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10,
		0x48, 0x89, 0x74, 0x24, 0x18, 0x48, 0x89, 0x7C, 0x24, 0x20,
		0x41, 0x54, 0x41, 0x56, 0x41, 0x57, 0x48, 0x81, 0xEC, 0xF0,
		0x00, 0x00, 0x00
	};
	constexpr std::array<std::uint8_t, 15> kFlatFindCameraStateDataPrologue{
		0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18,
		0x57, 0x48, 0x83, 0xEC, 0x20
	};
	constexpr std::array<std::uint8_t, 15> kFlatApplyCameraStateDataPrologue{
		0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x65, 0x48, 0x8B, 0x04,
		0x25, 0x58, 0x00, 0x00, 0x00
	};

	bool InstallFlatReflectedCameraStateHooks(
		std::uintptr_t buildCameraStateAddress,
		std::uintptr_t setCameraDataAddress) noexcept
	{
		const auto findCameraStateDataAddress =
			REL::Offset(ReflectionRuntime::Rva(BSGraphics_SetCameraData::kRVA_FindCameraStateData)).address();
		const auto applyCameraStateDataAddress =
			REL::Offset(ReflectionRuntime::Rva(BSGraphics_SetCameraData::kRVA_ApplyCameraStateData)).address();
		const auto entryMatches = [](
			std::uintptr_t address, const std::uint8_t* expected, std::size_t size) noexcept {
			bool matches = false;
			__try {
				matches = address > 0x10000 && expected && size != 0 &&
					std::memcmp(reinterpret_cast<const void*>(address), expected, size) == 0;
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				matches = false;
			}
			return matches;
		};
		const bool setEntryMatches = entryMatches(
			setCameraDataAddress,
			ReflectionRuntime::Prologue(ReflectionRuntime::Get()->setCameraData, kFlatSetCameraDataPrologue).data(),
			kFlatSetCameraDataPrologue.size());
		const bool buildEntryMatches = entryMatches(
			buildCameraStateAddress,
			ReflectionRuntime::Prologue(ReflectionRuntime::Get()->buildCameraStateData, kFlatBuildCameraStateDataPrologue).data(),
			kFlatBuildCameraStateDataPrologue.size());

		const bool findEntryMatches = ReflectionRuntime::IsPort240() || entryMatches(
			findCameraStateDataAddress,
			ReflectionRuntime::Prologue(BSGraphics_SetCameraData::kRVA_FindCameraStateData, kFlatFindCameraStateDataPrologue).data(),
			kFlatFindCameraStateDataPrologue.size());
		const bool applyEntryMatches = ReflectionRuntime::IsPort240() || entryMatches(
			applyCameraStateDataAddress,
			ReflectionRuntime::Prologue(BSGraphics_SetCameraData::kRVA_ApplyCameraStateData, kFlatApplyCameraStateDataPrologue).data(),
			kFlatApplyCameraStateDataPrologue.size());
		if (!setEntryMatches || !buildEntryMatches || !findEntryMatches || !applyEntryMatches) {
			logger::error(
				"[PlanarMirrors] flat reflected camera hooks skipped: entry attestation failed "
				"(Set={} Build={} Find={} Apply={})",
				setEntryMatches,
				buildEntryMatches,
				findEntryMatches,
				applyEntryMatches);
			return false;
		}

		BSGraphics_BuildCameraStateData::func = buildCameraStateAddress;
		BSGraphics_SetCameraData::func = setCameraDataAddress;
		stl::DetourTransactionLock detourTransactionLock{ stl::DetourTransactionMutex() };
		LONG hookStatus = DetourTransactionBegin();
		const bool hookTransactionStarted = hookStatus == NO_ERROR;
		if (hookStatus == NO_ERROR)
			hookStatus = DetourUpdateThread(GetCurrentThread());
		if (hookStatus == NO_ERROR) {
			hookStatus = DetourAttach(
				reinterpret_cast<PVOID*>(&BSGraphics_BuildCameraStateData::func),
				reinterpret_cast<PVOID>(BSGraphics_BuildCameraStateData::thunk));
		}
		if (hookStatus == NO_ERROR) {
			hookStatus = DetourAttach(
				reinterpret_cast<PVOID*>(&BSGraphics_SetCameraData::func),
				reinterpret_cast<PVOID>(BSGraphics_SetCameraData::thunk));
		}
		if (hookStatus == NO_ERROR) {
			hookStatus = DetourTransactionCommit();
		} else if (hookTransactionStarted) {
			(void)DetourTransactionAbort();
		}
		if (hookStatus != NO_ERROR) {
			logger::error(
				"[PlanarMirrors] flat reflected camera hook transaction failed ({})",
				hookStatus);
			return false;
		}

		logger::info(
			"[PlanarMirrors] hooked flat BuildCameraStateData @ 0x{:X} and SetCameraData @ 0x{:X} "
			"(Set/Build/Find/Apply entries attested; fresh reflected-state path)",
			buildCameraStateAddress,
			setCameraDataAddress);
		return true;
	}
}

static void InstallCrashHandler()
{
	AddVectoredExceptionHandler(0, CrashHandler);  
}

static bool s_worldRenderedThisFrame = false;

bool Hooks::WorldRenderFrameActive() noexcept
{
	return s_worldRenderedThisFrame;
}

static void InstallLazyVtableHooks(RE::BSShader* shader);

bool Hooks::BSShader_BeginTechnique::thunk(RE::BSShader* shader, uint32_t vertexDescriptor, uint32_t hullDescriptor, uint32_t domainDescriptor, uint32_t pixelDescriptor, void* outputStruct)
{
	MirrorNativeAmbientProbe::techniqueActive = false;
	extern __declspec(thread) const char* g_crashContext;
	g_crashContext = "BeginTechnique::mirror";
	MirrorPerformance::OwnWorkScope ownWork(MirrorPerformance::DetailedTimingEnabled() &&
		(FlatDeferredPlayerCapture::NativeWorldTransactionActive() || MirrorSceneRenderer::PrivateRenderActive()));
	InstallLazyVtableHooks(shader);
	if (!FlatDeferredPlayerCapture::NativeWorldTransactionActive() &&
		shader && ShaderFxpFilename(shader) &&
		std::strcmp(ShaderFxpFilename(shader), "DFPrepass") == 0) {
		s_worldRenderedThisFrame = true;

		if (!globals::game::identifiedPerFrameBuffer && globals::d3d::context) {
			ID3D11Buffer* perFrameBuffer = nullptr;
			globals::d3d::context->VSGetConstantBuffers(12, 1, &perFrameBuffer);
			if (perFrameBuffer) {
				globals::game::identifiedPerFrameBuffer = perFrameBuffer;
				perFrameBuffer->Release();  
				logger::info("[FO4] Identified perFrame cb12 buffer: {}",
					static_cast<void*>(globals::game::identifiedPerFrameBuffer));
			}
		}
	}

	if (FlatDeferredPlayerCapture::NativeWorldTransactionActive()) {
		FlatDeferredPlayerCapture::NoteNativeWorldTechnique(shader);
		ownWork.BeginNative();
		const bool result = func(
			shader, vertexDescriptor, hullDescriptor, domainDescriptor,
			pixelDescriptor, outputStruct);
		ownWork.EndNative();
		g_crashContext = "BeginTechnique::DONE";
		return result;
	}

	std::uint32_t nativeVertexDescriptor = vertexDescriptor;
	const bool repaired =
		FlatDeferredPlayerCapture::TrySubstituteEssentialForwardVertexShader(
			shader, vertexDescriptor, hullDescriptor, domainDescriptor,
			pixelDescriptor, outputStruct, nativeVertexDescriptor);
	ownWork.BeginNative();
	const bool result = func(
		shader, nativeVertexDescriptor, hullDescriptor, domainDescriptor,
		pixelDescriptor, outputStruct);
	ownWork.EndNative();
	if (!REL::Module::IsVR() && !MirrorSceneRenderer::PrivateRenderActive())
		MirrorNativeAmbientProbe::Technique(ShaderFxpFilename(shader), pixelDescriptor, result);
	if (repaired) {
		FlatDeferredPlayerCapture::NoteEssentialForwardVertexShaderResult(
			vertexDescriptor, nativeVertexDescriptor, pixelDescriptor, result);
	}
	g_crashContext = "BeginTechnique::DONE";
	return result;
}

void Hooks::BSGraphics_SetDirtyStates::thunk(bool isCompute, bool preserveInputLayout)
{
	extern __declspec(thread) const char* g_crashContext;
	const bool nativeWorldTransaction =
		!isCompute && FlatDeferredPlayerCapture::NativeWorldTransactionActive();
	MirrorPerformance::OwnWorkScope ownWork(MirrorPerformance::DetailedTimingEnabled() &&
		(nativeWorldTransaction || MirrorSceneRenderer::PrivateRenderActive()));
	if (nativeWorldTransaction) {

		g_crashContext = "SetDirtyStates::native-world";
		const auto nativeCommit =
			FlatDeferredPlayerCapture::BeginNativeWorldSetDirtyCommit();
		if (!isCompute)
			MirrorSceneRenderer::ReconcileEngineShadowBeforeCommit();
		ownWork.BeginNative();
		func(isCompute, preserveInputLayout);
		ownWork.EndNative();

		const bool nativeTargetsObserved =
			FlatDeferredPlayerCapture::AttestNativeWorldTargetsAfterSetDirty(nativeCommit);

		const bool reflectedGeometryCommit =
			FlatDeferredPlayerCapture::NativeWorldGeometryPassActive() ||
			FlatDeferredPlayerCapture::NativeWorldCurrentTechniqueNeedsReflectedWinding();
		bool rasterizerPolicyApplied = false;
		if (reflectedGeometryCommit) {
			rasterizerPolicyApplied = PlanarMirrorLookup::ApplyReflectedRasterizer(
				globals::d3d::device, globals::d3d::context);
		} else {

			rasterizerPolicyApplied = PlanarMirrorLookup::RestoreOrdinaryRasterizer(
				globals::d3d::device, globals::d3d::context);
		}

		FlatDeferredPlayerCapture::CompleteNativeWorldSetDirtyCommit(
			nativeCommit, nativeTargetsObserved, reflectedGeometryCommit,
			rasterizerPolicyApplied);

		MirrorSceneRenderer::RecordTripwireDrawState(globals::d3d::context);
		g_crashContext = "SetDirtyStates::DONE";
		return;
	}
	if (!isCompute) {
		MirrorSceneRenderer::RestoreFaceSkinTintBeforeSetDirty(globals::d3d::context);
		MirrorSceneRenderer::RestoreEyeGroup4AlbedoPSBeforeSetDirty(globals::d3d::context);
		MirrorSceneRenderer::RestoreEyeGroup4NoPixelPSBeforeSetDirty(globals::d3d::context);

		MirrorSceneRenderer::ReconcileEngineShadowBeforeCommit();
	}

	ownWork.BeginNative();
	func(isCompute, preserveInputLayout);
	ownWork.EndNative();

	if (!isCompute) {
		if (MirrorNativeAmbientProbe::techniqueActive)
			MirrorSceneRenderer::ObserveNativeAmbientCommit();
		MirrorSceneRenderer::BindPlanarTarget();
		VRReflectionRenderer::BindTargetAfterSetDirty();
		MirrorSceneRenderer::ApplyEyeGroup4NoPixelPS(globals::d3d::context);
		MirrorSceneRenderer::BindPlanarTarget();
		MirrorSceneRenderer::RecordEyeGroup4AlbedoPSOverrideApplied();

		if (FlatDeferredPlayerCapture::PrepassTransactionActive()) {
			FlatDeferredPlayerCapture::NoteTransactionCommit();
			(void)FlatDeferredPlayerCapture::RebindForwardOutputsAfterSetDirty();
			if (FlatDeferredPlayerCapture::ReflectedRasterizerRequired()) {
				const bool reflectedRasterizerApplied =
					PlanarMirrorLookup::ApplyReflectedRasterizer(
						globals::d3d::device, globals::d3d::context);
				FlatDeferredPlayerCapture::NoteReflectedRasterizerCommit(
					reflectedRasterizerApplied);
			} else {
				FlatDeferredPlayerCapture::NoteOrdinaryRasterizerCommit();
			}
		}
		MirrorSceneRenderer::RecordTripwireDrawState(globals::d3d::context);
	}
	g_crashContext = "SetDirtyStates::DONE";
	return;
}

void Hooks::BSBatchRenderer_RenderPassImmediately1::thunk(RE::BSRenderPass*, uint32_t, bool, uint32_t) {}

struct IDXGISwapChain_Present
{
	static HRESULT WINAPI thunk(IDXGISwapChain* This, UINT SyncInterval, UINT Flags)
	{
		if (!globals::gameDataReadyComplete.load(std::memory_order_acquire))
			return func(This, SyncInterval, Flags);

		static bool d3dAcquired = false;
		if (!d3dAcquired && This) {
			ID3D11Device* device = nullptr;
			if (SUCCEEDED(This->GetDevice(
					__uuidof(ID3D11Device), reinterpret_cast<void**>(&device))) && device) {
				ID3D11DeviceContext* context = nullptr;
				device->GetImmediateContext(&context);
				if (!globals::d3d::device)
					globals::d3d::device = device;
				else
					device->Release();
				if (context) {
					if (!globals::d3d::context)
						globals::d3d::context = context;
					else
						context->Release();
				}
				globals::d3d::swapChain = This;
				d3dAcquired = globals::d3d::device && globals::d3d::context;
			}
		}

		if ((Flags & DXGI_PRESENT_TEST) != 0u)
			return func(This, SyncInterval, Flags);
		DXGI_SWAP_CHAIN_DESC description{};
		const HWND window = SUCCEEDED(This->GetDesc(&description)) ? description.OutputWindow : nullptr;
		const bool windowHook = EnsureOverlayOutputWindowHook(window);
		const bool focused = window && GetForegroundWindow() == window;
		const bool debugKeys = MirrorSettings::DebugKeysEnabled();
		if (!windowHook && debugKeys) {
			const bool modified = ((GetAsyncKeyState(VK_MENU) | GetAsyncKeyState(VK_CONTROL) |
				GetAsyncKeyState(VK_SHIFT)) & 0x8000) != 0;
			g_mirrorToggleRequests.ObserveKey((GetAsyncKeyState(VK_F12) & 0x8000) != 0, focused, modified);
			g_mirrorOverlayRequests.ObserveKey((GetAsyncKeyState(VK_F11) & 0x8000) != 0, focused, modified);
			g_optimizationRequests.ObserveKey((GetAsyncKeyState(VK_F7) & 0x8000) != 0, focused, modified);
			g_benchmarkRequests.ObserveKey((GetAsyncKeyState(VK_F8) & 0x8000) != 0, focused, modified);
		}
		const bool rendered = std::exchange(s_worldRenderedThisFrame, false);
		static bool overlayVisible=false;
		if (g_mirrorOverlayRequests.ConsumeToggle(overlayVisible) && !overlayVisible)
			MirrorPerformance::DismissBenchmark();
		{
			bool optimizations = MirrorCaptureOptimizations::master.load(std::memory_order_relaxed);
			if (g_optimizationRequests.ConsumeToggle(optimizations)) {
				MirrorCaptureOptimizations::master.store(optimizations, std::memory_order_relaxed);
				logger::info("[Mirrors AB] F7: capture optimisations {}", optimizations ? "ON" : "OFF");
			}
			bool benchmark = false;
			if (g_benchmarkRequests.ConsumeToggle(benchmark)) {
				MirrorPerformance::RequestBenchmark();
				overlayVisible = true;  
			}
		}
		if (!debugKeys) {
			g_mirrorOverlayRequests.Clear();
			g_optimizationRequests.Clear();
			g_benchmarkRequests.Clear();
			overlayVisible = false;
			
			if (!MirrorCaptureOptimizations::master.exchange(true, std::memory_order_relaxed))
				logger::info("[Mirrors AB] debug keys off: capture optimisations ON");
		}
		const bool overlayShown = overlayVisible && focused && rendered &&
			!MirrorSceneRenderer::LoadBlocked() && !MirrorSceneRenderer::ShutdownRequested() && !REL::Module::IsVR();
		
		MirrorPerformance::SetDetailedTiming(overlayShown);
		MirrorPerformanceOverlay::Draw(This, overlayShown);
		const HRESULT result = func(This, SyncInterval, Flags);
		MirrorPerformance::FramePresented(rendered && result == S_OK && focused &&
			!MirrorSceneRenderer::LoadBlocked() && !MirrorSceneRenderer::InteractiveMenuOpen());
		return result;
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

decltype(&CreateDXGIFactory) ptrCreateDXGIFactory = nullptr;

HRESULT WINAPI hk_CreateDXGIFactory(REFIID, void** ppFactory)
{
	return ptrCreateDXGIFactory(__uuidof(IDXGIFactory4), ppFactory);
}

decltype(&D3D11CreateDeviceAndSwapChain) ptrD3D11CreateDeviceAndSwapChain = nullptr;

HRESULT WINAPI hk_D3D11CreateDeviceAndSwapChain(
	IDXGIAdapter* pAdapter,
	D3D_DRIVER_TYPE DriverType,
	HMODULE Software,
	UINT Flags,
	[[maybe_unused]] const D3D_FEATURE_LEVEL* pFeatureLevels,
	[[maybe_unused]] UINT FeatureLevels,
	UINT SDKVersion,
	DXGI_SWAP_CHAIN_DESC* pSwapChainDesc,
	IDXGISwapChain** ppSwapChain,
	ID3D11Device** ppDevice,
	D3D_FEATURE_LEVEL* pFeatureLevel,
	ID3D11DeviceContext** ppImmediateContext)
{

	auto ret = ptrD3D11CreateDeviceAndSwapChain(pAdapter,
		DriverType,
		Software,
		Flags,
		pFeatureLevels,
		FeatureLevels,
		SDKVersion,
		pSwapChainDesc,
		ppSwapChain,
		ppDevice,
		pFeatureLevel,
		ppImmediateContext);

	if (SUCCEEDED(ret) && ppDevice && *ppDevice) {
		if (!globals::d3d::device)
			globals::d3d::device = *ppDevice;
	}

	return ret;
}

namespace Hooks
{
	struct WndProcHandler_Hook
	{
		static LRESULT thunk(HWND a_hwnd, UINT a_msg, WPARAM a_wParam, LPARAM a_lParam)
		{
			return MirrorsOfFalloutWndProc(a_hwnd, a_msg, a_wParam, a_lParam);
		}
	};

	struct RegisterClassA_Hook
	{
		static ATOM thunk(WNDCLASSA* a_wndClass)
		{
			const auto original = reinterpret_cast<std::uintptr_t>(a_wndClass->lpfnWndProc);
			if (original != 0 && original != reinterpret_cast<std::uintptr_t>(&MirrorsOfFalloutWndProc))
				g_overlayForwardWndProc.store(original, std::memory_order_release);
			a_wndClass->lpfnWndProc = &MirrorsOfFalloutWndProc;
			return func(a_wndClass);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};
}

namespace LightingExtensions
{
	struct BSLightingShader_SetupGeometry
	{

		static void thunk(RE::BSShader* shader, RE::BSRenderPass* pass)
		{
			MirrorSceneRenderer::NoteDFPrepassSetupGeometry();
			const bool reflectionDraw = MirrorSceneRenderer::MirrorRenderActive();
			if (!reflectionDraw && pass) {
				MirrorSceneRenderer::ObserveRegisteredMirrorReference(pass);
				if (MirrorTextureRouting::IsLikelyMirrorSurface(pass))
					MirrorSceneRenderer::ObserveMirrorGeometry(pass);
			}
			MirrorSceneRenderer::BeginEyeGroup4AlbedoLightingDraw(pass);
			MirrorSceneRenderer::BeginEyeGroup4NoPixelEffectDraw(pass);
			func(shader, pass);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	struct BSLightingShader_RestoreGeometry
	{
		static void thunk(RE::BSShader* shader, RE::BSRenderPass* pass)
		{

			MirrorSceneRenderer::EndEyeGroup4NoPixelEffectDraw(pass);
			MirrorSceneRenderer::EndEyeGroup4AlbedoLightingDraw(pass);
			func(shader, pass);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

}

static void InstallLazyVtableHooks(RE::BSShader* shader)
{

	if (REL::Module::IsVR())
		return;
	if (!shader || !ShaderFxpFilename(shader))
		return;

	constexpr int SETUP_GEOMETRY_SLOT = 7;    
	constexpr int RESTORE_GEOMETRY_SLOT = 8;  

	static bool installedDFPrepassHook = false;
	if (!installedDFPrepassHook && strcmp(ShaderFxpFilename(shader), "DFPrepass") == 0) {
		installedDFPrepassHook = true;
		auto actualVtable = *reinterpret_cast<uintptr_t**>(shader);
		auto origSetupFunc = actualVtable[SETUP_GEOMETRY_SLOT];
		auto origRestoreFunc = actualVtable[RESTORE_GEOMETRY_SLOT];
		
		LightingExtensions::BSLightingShader_SetupGeometry::func = origSetupFunc;
		LightingExtensions::BSLightingShader_RestoreGeometry::func = origRestoreFunc;
		auto setupThunkAddr = reinterpret_cast<uintptr_t>(
			&LightingExtensions::BSLightingShader_SetupGeometry::thunk);
		auto restoreThunkAddr = reinterpret_cast<uintptr_t>(
			&LightingExtensions::BSLightingShader_RestoreGeometry::thunk);
		REL::safe_write(
			reinterpret_cast<uintptr_t>(&actualVtable[SETUP_GEOMETRY_SLOT]),
			setupThunkAddr);
		REL::safe_write(
			reinterpret_cast<uintptr_t>(&actualVtable[RESTORE_GEOMETRY_SLOT]),
			restoreThunkAddr);
		
		auto setupReadBack = actualVtable[SETUP_GEOMETRY_SLOT];
		auto restoreReadBack = actualVtable[RESTORE_GEOMETRY_SLOT];
		logger::info("[FO4] Installed Setup/RestoreGeometry hooks on DFPrepass vtable at {:X} slots {}/{} (setupOrig={:X}, setupThunk={:X}, setupReadback={:X}, setupMatch={}; restoreOrig={:X}, restoreThunk={:X}, restoreReadback={:X}, restoreMatch={})",
			reinterpret_cast<uintptr_t>(actualVtable), SETUP_GEOMETRY_SLOT,
			RESTORE_GEOMETRY_SLOT, origSetupFunc, setupThunkAddr, setupReadBack,
			setupReadBack == setupThunkAddr, origRestoreFunc, restoreThunkAddr, restoreReadBack,
			restoreReadBack == restoreThunkAddr);

		logger::info("[FO4] DFPrepass shader instance={:p}, vtable_ptr={:X}",
			(void*)shader, reinterpret_cast<uintptr_t>(actualVtable));
	}

}

bool InstallCellMusicRegionNullGuard()
{
	const auto* recovery = ReflectionRuntime::Recovery();
	if (!recovery)
		return false;
	const auto address = REL::Offset(recovery->cellMusicRead).address();
	const auto& expected = recovery->cellMusicEntry;
	if (address <= 0x10000u ||
		std::memcmp(reinterpret_cast<const void*>(address), expected.data(), expected.size()) != 0) {
		logger::error("[FO4] cell music shutdown guard skipped: runtime fallback bytes do not match");
		return false;
	}
	CellMusicFallbackCode code(REL::Offset(recovery->playerRegionSlot).address(), address + 11u);
	F4SE::AllocTrampoline(code.getSize() + 14u);
	auto& trampoline = F4SE::GetTrampoline();
	const auto replacement = reinterpret_cast<std::uintptr_t>(trampoline.allocate(code));
	FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<const void*>(replacement), code.getSize());
	trampoline.write_branch<5>(address, replacement);
	constexpr std::array<std::uint8_t, 6> padding{ 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 };
	REL::safe_write(address + 5u, padding.data(), padding.size());
	FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<const void*>(address), 11u);
	logger::info("[FO4] cell music shutdown guard installed at {}; native worldspace fallback and cell unlock preserved",
		reinterpret_cast<void*>(address));
	return true;
}

bool NativeSafetyEntryMatches(std::uintptr_t address, const void* expected, std::size_t size) noexcept
{
	__try {
		return address > 0x10000u && std::memcmp(reinterpret_cast<const void*>(address), expected, size) == 0;
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

struct SubtitleManager_HideSubtitle_NullGuard
{
	static void thunk(void* manager, void* speaker) noexcept
	{

		if (!manager) {
			static std::atomic_bool logged{ false };
			if (!logged.exchange(true, std::memory_order_relaxed))
				logger::info("[FO4] SubtitleManager::HideSubtitle ignored late cleanup after manager destruction");
			return;
		}
		func(manager, speaker);
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

struct AnimationCopyEnableBindingGuard
{
	static void thunk(const MirrorAnimationBindingSetView* set, void* sync, void* bindable, void* behavior) noexcept
	{

		if (!MirrorHasAnimationEnableBinding(set)) {
			if (set && set->enableIndex != -1) {
				static std::atomic_bool logged{ false };
				if (!logged.exchange(true, std::memory_order_relaxed))
					logger::warn("[FO4] animation enable binding unavailable: set={} data={} count={} index={}; keeping modifier enable state",
						static_cast<const void*>(set), set->bindings, set->count, set->enableIndex);
			}
			return;
		}
		func(set, sync, bindable, behavior);
	}
	static inline REL::Relocation<decltype(thunk)> func;
};

bool InstallSubtitleManagerHideSubtitleNullGuard() noexcept
{
	const auto* contract = ReflectionRuntime::Recovery();
	if (!contract)
		return false;
	const auto address = REL::Offset(contract->subtitleHide).address();
	if (!NativeSafetyEntryMatches(address, contract->subtitleHideEntry.data(), contract->subtitleHideEntry.size())) {
		stl::hookInstallationFailed.store(true, std::memory_order_release);
		logger::error("[FO4] SubtitleManager::HideSubtitle guard entry attestation failed at {}", reinterpret_cast<void*>(address));
		return false;
	}
	SubtitleManager_HideSubtitle_NullGuard::func = address;
	const auto status = stl::InstallDetours({ {
		reinterpret_cast<PVOID*>(&SubtitleManager_HideSubtitle_NullGuard::func),
		reinterpret_cast<PVOID>(&SubtitleManager_HideSubtitle_NullGuard::thunk) } });
	if (status != NO_ERROR) {
		logger::error("[FO4] SubtitleManager::HideSubtitle guard installation failed ({})", status);
		return false;
	}
	logger::info("[FO4] SubtitleManager::HideSubtitle null-manager guard installed at {} (runtime entry verified)", reinterpret_cast<void*>(address));
	return true;
}

bool InstallAnimationCopyEnableBindingGuard() noexcept
{
	const auto* contract = ReflectionRuntime::Recovery();
	if (!contract)
		return false;
	const auto address = REL::Offset(contract->animationCopyEnable).address();
	if (!NativeSafetyEntryMatches(address, contract->animationCopyEnableEntry.data(), contract->animationCopyEnableEntry.size())) {
		stl::hookInstallationFailed.store(true, std::memory_order_release);
		logger::error("[FO4] animation enable-binding guard entry attestation failed at {}", reinterpret_cast<void*>(address));
		return false;
	}
	AnimationCopyEnableBindingGuard::func = address;
	const auto status = stl::InstallDetours({ {
		reinterpret_cast<PVOID*>(&AnimationCopyEnableBindingGuard::func),
		reinterpret_cast<PVOID>(&AnimationCopyEnableBindingGuard::thunk) } });
	if (status != NO_ERROR) {
		logger::error("[FO4] animation enable-binding guard installation failed ({})", status);
		return false;
	}
	logger::info("[FO4] animation enable-binding bounds guard installed at {} (runtime entry verified)", reinterpret_cast<void*>(address));
	return true;
}

namespace
{
	using PureCallHandler = void(__cdecl*)();
	PureCallHandler g_previousPureCallHandler = nullptr;
	std::atomic_bool g_pureCallReported{ false };

	std::string Narrow(const wchar_t* text) noexcept
	{
		if (!text)
			return {};
		const auto needed = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
		if (needed <= 1)
			return {};
		std::string result(static_cast<std::size_t>(needed) - 1u, char{});
		WideCharToMultiByte(CP_UTF8, 0, text, -1, result.data(), needed, nullptr, nullptr);
		return result;
	}

	void LogPureCallBacktrace() noexcept
	{
		std::array<void*, 40> frames{};
		const auto captured = RtlCaptureStackBackTrace(0, static_cast<DWORD>(frames.size()), frames.data(), nullptr);
		for (USHORT i = 0; i < captured; ++i) {
			HMODULE owner = nullptr;
			std::array<wchar_t, MAX_PATH> path{};
			const wchar_t* name = L"?";
			std::uintptr_t rva = reinterpret_cast<std::uintptr_t>(frames[i]);
			if (GetModuleHandleExW(
					GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
					reinterpret_cast<LPCWSTR>(frames[i]), std::addressof(owner)) &&
				owner && GetModuleFileNameW(owner, path.data(), static_cast<DWORD>(path.size()))) {
				const auto* slash = std::wcsrchr(path.data(), L'\\');
				name = slash ? slash + 1 : path.data();
				rva -= reinterpret_cast<std::uintptr_t>(owner);
			}
			logger::critical("[PURECALL] frame {:02} {} +0x{:X}", i, Narrow(name), rva);
		}
	}

	void WritePureCallMinidump() noexcept
	{
		auto* dbghelp = LoadLibraryW(L"dbghelp.dll");
		if (!dbghelp)
			return;
		using WriteDump = BOOL(WINAPI*)(HANDLE, DWORD, HANDLE, MINIDUMP_TYPE,
			PMINIDUMP_EXCEPTION_INFORMATION, PMINIDUMP_USER_STREAM_INFORMATION, PMINIDUMP_CALLBACK_INFORMATION);
		auto* write = reinterpret_cast<WriteDump>(GetProcAddress(dbghelp, "MiniDumpWriteDump"));
		if (!write)
			return;
		std::array<wchar_t, MAX_PATH> path{};
		if (!GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size())))
			return;
		if (auto* slash = std::wcsrchr(path.data(), L'\\'))
			slash[1] = L'\0';
		std::wstring file(path.data());
		file += L"RealisticReflectionsMirrors_purecall_" + std::to_wstring(GetCurrentProcessId()) + L".dmp";
		auto* handle = CreateFileW(file.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
			FILE_ATTRIBUTE_NORMAL, nullptr);
		if (handle == INVALID_HANDLE_VALUE)
			return;
		const auto type = static_cast<MINIDUMP_TYPE>(
			MiniDumpWithThreadInfo | MiniDumpWithIndirectlyReferencedMemory | MiniDumpWithUnloadedModules);
		const bool written = write(GetCurrentProcess(), GetCurrentProcessId(), handle, type,
			nullptr, nullptr, nullptr) != FALSE;
		CloseHandle(handle);
		logger::critical("[PURECALL] minidump {}: {}", written ? "written" : "FAILED", Narrow(file.c_str()));
	}

	void __cdecl MirrorsOfFalloutPureCallHandler()
	{
		
		if (!g_pureCallReported.exchange(true, std::memory_order_acq_rel)) {
			logger::critical("[PURECALL] pure virtual function call (R6025). Backtrace follows; frames are module+RVA.");
			LogPureCallBacktrace();
			WritePureCallMinidump();
			spdlog::default_logger()->flush();
		}
		if (g_previousPureCallHandler && g_previousPureCallHandler != &MirrorsOfFalloutPureCallHandler)
			g_previousPureCallHandler();
	}

	void InstallPureCallDiagnostics() noexcept
	{

		using SetPureCallHandler = PureCallHandler(__cdecl*)(PureCallHandler);
		for (const wchar_t* module : { L"msvcr110.dll", L"msvcr120.dll", L"vcruntime140.dll", L"ucrtbase.dll" }) {
			auto* crt = GetModuleHandleW(module);
			if (!crt)
				continue;
			auto* setter = reinterpret_cast<SetPureCallHandler>(GetProcAddress(crt, "_set_purecall_handler"));
			if (!setter)
				continue;
			g_previousPureCallHandler = setter(&MirrorsOfFalloutPureCallHandler);
			logger::info("[PURECALL] handler installed in {}; previous={}", Narrow(module),
				static_cast<const void*>(g_previousPureCallHandler));
			return;
		}
		logger::warn("[PURECALL] no loaded CRT exports _set_purecall_handler; an R6025 on exit stays unattributed");
	}
}

void Hooks::Install()
{
	InstallCrashHandler();
	InstallCellMusicRegionNullGuard();
	InstallSubtitleManagerHideSubtitleNullGuard();
	InstallAnimationCopyEnableBindingGuard();

	FlatDeferredPlayerCapture::InstallPrepassDepthCaptureHook();
	MirrorSettings::Initialize();
	InstallPureCallDiagnostics();
	logger::info("[FO4] Hooks::Install — Phase 1 (non-D3D hooks, crash handler installed) isNG={}", REL::Module::IsNG());

	if (auto addr = REL::VariantID(1041640, 72684, 0x2814BE0).address()) {
		BSShader_BeginTechnique::func = addr;
		stl::DetourTransactionLock detourTransactionLock{ stl::DetourTransactionMutex() };
		LONG error = DetourTransactionBegin();
		bool transactionOpen = error == NO_ERROR;
		if (error == NO_ERROR)
			error = DetourUpdateThread(GetCurrentThread());
		if (error == NO_ERROR) {
			error = DetourAttach(
				reinterpret_cast<PVOID*>(&BSShader_BeginTechnique::func),
				reinterpret_cast<PVOID>(BSShader_BeginTechnique::thunk));
		}
		if (error == NO_ERROR) {
			error = DetourTransactionCommit();
			transactionOpen = false;
		} else if (transactionOpen) {
			(void)DetourTransactionAbort();
			transactionOpen = false;
		}

		if (error == NO_ERROR) {
			logger::info(
				"[FO4] Hooked BSShader::BeginTechnique @ 0x{:X} (attested)",
				addr);
		} else {
			logger::error(
				"[FO4] FAILED: BSShader::BeginTechnique detour transaction ({})",
				error);
			stl::hookInstallationFailed.store(true, std::memory_order_release);
			return;
		}
	} else {
		logger::error("[FO4] FAILED: BSShader::BeginTechnique address not resolved (NG ID needed)");
	}

	std::uintptr_t setDirtyStatesAddress = 0;
	if (const auto* runtime = ReflectionRuntime::Get()) {
		setDirtyStatesAddress = ReflectionRuntime::Address(runtime->setDirtyStates);
	} else if (REL::Module::IsNG()) {
		setDirtyStatesAddress = REL::ID(4495320).address();
	}
	if (auto addr = setDirtyStatesAddress) {
		if (!stl::detour_thunk<BSGraphics_SetDirtyStates>(addr))
			return;
		logger::info("[FO4] Hooked BSGraphics::SetDirtyStates @ 0x{:X}", addr);
	} else {
		logger::error("[FO4] FAILED: BSGraphics::SetDirtyStates address not resolved for this runtime");
	}

	if (const auto* runtime = ReflectionRuntime::Get();
		runtime && PlanarMirrors::SupportsNativeCameraReflection()) {
		const auto cameraStateAddress = ReflectionRuntime::Address(runtime->buildCameraStateData);
		if (runtime->vr) {
			if (!stl::detour_thunk<BSGraphics_BuildCameraStateData>(cameraStateAddress))
				return;
			logger::info(
				"[PlanarMirrors] hooked VR stereo BSGraphics::State::BuildCameraStateData @ 0x{:X}",
				cameraStateAddress);
		} else {
			const auto setCameraDataAddress = ReflectionRuntime::Address(runtime->setCameraData);
			(void)InstallFlatReflectedCameraStateHooks(cameraStateAddress, setCameraDataAddress);
		}
	}

	logger::info("[FO4] Hooks::Install — Phase 1 + Phase 2 complete (D3D hooks deferred to InstallD3DHooks)");
}

void Hooks::InstallD3DHooks()
{
	static bool installed = false;
	if (installed) return;
	if (stl::hookInstallationFailed.load(std::memory_order_acquire))
		return;

	logger::info("[FO4] Hooks::InstallD3DHooks — D3D vtable detours");

	if (globals::d3d::swapChain) {
		logger::info("[FO4] Detouring IDXGISwapChain::Present (vtable 8)");
		if (!stl::detour_vfunc<8, IDXGISwapChain_Present>(globals::d3d::swapChain))
			return;
	} else {
		logger::error("[FO4] swapChain is STILL null at DataLoaded — IDXGISwapChain::Present hook FAILED");
	}

	if (globals::d3d::device) {
	} else {
		logger::error("[FO4] device is STILL null at DataLoaded — ID3D11Device vtable hooks FAILED");
	}

	if (globals::d3d::context) {
		logger::info("[FO4] Installing D3D context hooks (Map/Unmap)");
		globals::InstallD3DHooks(globals::d3d::context);
	} else {
		logger::error("[FO4] context is STILL null at DataLoaded — D3D context hooks FAILED");
	}

	installed = !stl::hookInstallationFailed.load(std::memory_order_acquire);

	logger::info("[FO4] Menu::Init deferred to Present render thread");

	logger::info("[FO4] non-Lighting SetupGeometry vtable hooks DEFERRED to BeginTechnique (lazy install on actual shader vtables)");

	logger::info("[FO4] Hooks::InstallD3DHooks — complete");
}

void Hooks::InstallEarlyHooks()
{
	logger::info("[FO4] InstallEarlyHooks — starting (isNG={})", REL::Module::IsNG());

	logger::info("[FO4] Hooking D3D11CreateDeviceAndSwapChain via IAT");
	*(uintptr_t*)&ptrD3D11CreateDeviceAndSwapChain = (uintptr_t)REL::PatchIAT(hk_D3D11CreateDeviceAndSwapChain, "d3d11.dll", "D3D11CreateDeviceAndSwapChain");

	logger::info("[FO4] Hooking CreateDXGIFactory via IAT");
	*(uintptr_t*)&ptrCreateDXGIFactory = (uintptr_t)REL::PatchIAT(hk_CreateDXGIFactory, "dxgi.dll", !REL::Module::IsVR() ? "CreateDXGIFactory" : "CreateDXGIFactory1");

	logger::info("[FO4] OutputWindow WndProc subclass deferred to first Present");

	logger::info("[FO4] InstallEarlyHooks — complete");
}


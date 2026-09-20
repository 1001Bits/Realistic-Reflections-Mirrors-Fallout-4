#include "Features/MirrorSceneRenderer.h"
#include "Globals.h"
#include "Features/MirrorAuthoring.h"
#include "Hooks.h"
#include "Features/ReflectionRuntime.h"
#include "Features/MirrorWorkshopMenu.h"
#include "Features/MirrorMenuCompatibility.h"
#include "Features/MirrorQualityMenu.h"

#include <psapi.h>

#include <cstdio>
#include <windows.h>
#include <cstring>

static void DllTrace([[maybe_unused]] const char* msg)
{
#ifdef MIRRORS_DLL_TRACE
	
	char path[MAX_PATH];
	if (GetEnvironmentVariableA("USERPROFILE", path, MAX_PATH)) {
		strcat_s(path, "\\Documents\\RealisticReflectionsMirrors_trace.log");
	} else {
		strcpy_s(path, "C:\\RealisticReflectionsMirrors_trace.log");
	}
	FILE* f = nullptr;
	fopen_s(&f, path, "a");
	if (f) {
		fprintf(f, "%s\n", msg);
		fclose(f);
	}
#endif
}

struct StaticInitTracer {
	StaticInitTracer() { DllTrace("StaticInitTracer: C++ static init reached user code"); }
} g_staticInitTracer;

extern "C" BOOL WINAPI DllMain(HINSTANCE, DWORD dwReason, LPVOID)
{
	if (dwReason == DLL_PROCESS_ATTACH) {
		DllTrace("DllMain: DLL_PROCESS_ATTACH entered");
	}
	return TRUE;
}

#define DLLEXPORT __declspec(dllexport)

static std::list<std::string>& GetErrors()
{
	static std::list<std::string> errors;
	return errors;
}

bool Load();

void InitializeLog([[maybe_unused]] spdlog::level::level_enum a_level = spdlog::level::info)
{
#ifndef NDEBUG
	auto sink = std::make_shared<spdlog::sinks::msvc_sink_mt>();
#else
	auto path = logger::log_directory();
	if (!path) {
		util::report_and_fail("Failed to find standard logging directory"sv);
	}

	*path /= std::format("{}.log"sv, Plugin::NAME);
	auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);
#endif

#ifndef NDEBUG
	const auto level = spdlog::level::trace;
#else
	const auto level = a_level;
#endif

	auto log = std::make_shared<spdlog::logger>("global log"s, std::move(sink));
	log->set_level(level);
	log->flush_on(spdlog::level::warn);  

	spdlog::set_default_logger(std::move(log));
	spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [%t] [%s:%#] %v");
}

extern "C" DLLEXPORT bool F4SEAPI F4SEPlugin_Load(const F4SE::LoadInterface* a_f4se)
{
#ifndef NDEBUG
	while (!REX::W32::IsDebuggerPresent()) {};
#endif
	DllTrace("F4SEPlugin_Load: entered");

	DllTrace("F4SEPlugin_Load: calling F4SE::Init");

	F4SE::Init(a_f4se, false);
	InitializeLog();
	spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%t] [%L] %v");
	logger::info("Loaded {} {}", Plugin::NAME, Plugin::VERSION.string());
	spdlog::default_logger()->flush();
	DllTrace("F4SEPlugin_Load: log initialized");
	MirrorQualityMenu::Register();
	DllTrace("F4SEPlugin_Load: F4SE::Init complete, calling Load()");
	auto result = Load();
	DllTrace("F4SEPlugin_Load: Load() complete");
	return result;
}

extern "C" DLLEXPORT auto F4SEPlugin_Version = []() noexcept {
	F4SE::PluginVersionData v{};
	v.PluginName(Plugin::NAME.data());
	v.PluginVersion(Plugin::VERSION);
	v.UsesAddressLibrary(true);
	v.IsLayoutDependent(true);

	v.CompatibleVersions({
#ifdef ENABLE_FALLOUT_F4
		F4SE::RUNTIME_1_10_163,
#endif
#ifdef ENABLE_FALLOUT_NG

		REL::Version{ 1, 11, 240, 0 },
#endif
#ifdef ENABLE_FALLOUT_VR
		F4SE::RUNTIME_VR_1_2_72,
#endif
	});
	return v;
}();

extern "C" DLLEXPORT bool F4SEAPI F4SEPlugin_Query(const F4SE::QueryInterface*, F4SE::PluginInfo* pluginInfo)
{
	pluginInfo->name = F4SEPlugin_Version.pluginName;
	pluginInfo->infoVersion = F4SE::PluginInfo::kVersion;
	pluginInfo->version = F4SEPlugin_Version.pluginVersion;
	return true;
}

static void RegisterMirrorWorkshopCategory()
{
	auto* handler = RE::TESDataHandler::GetSingleton();
	if (!handler)
		return;
	auto* category = handler->LookupForm<RE::BGSKeyword>(0xA00, "Realistic Reflections - Mirrors.esm");
	if (!category)
		return;  
	auto* decorations = RE::TESForm::GetFormByID<RE::BGSListForm>(0x00106DA4);
	if (!MirrorWorkshopMenu::AppendOnce(decorations, category))
		logger::warn("[MirrorWorkshop] Decorations menu unavailable; Mirrors category not registered");
	else
		logger::info("[MirrorWorkshop] Decorations > Mirrors registered; category={:08X}", category->GetFormID());
}

void F4SEAPI MessageHandler(F4SE::MessagingInterface::Message* message)
{
	switch (message->type) {
	case F4SE::MessagingInterface::kPreSaveGame:
		{
			const auto report = MirrorSceneRenderer::CleanupMirrorGalleryForLifecycle();
			logger::info(
				"[MirrorGallery] kPreSaveGame: delete-requested={} already-deleted={} retained={} "
				"unresolved={} identity-rejected={} delete-pending={}",
				report.deleteRequested, report.alreadyDeleted, report.Retained(), report.unresolved,
				report.rejectedIdentity, report.deletePending);
		}
		break;
	case F4SE::MessagingInterface::kPreLoadGame:
		{
			const auto report = MirrorSceneRenderer::CleanupMirrorGalleryForLifecycle();
			MirrorSceneRenderer::NotifyLoadSource(MirrorSceneRenderer::LoadSource::kSaveGame, true);
			logger::info(
				"[LoadGate] kPreLoadGame: gallery delete-requested={} already-deleted={} retained={} "
				"unresolved={} identity-rejected={} delete-pending={}; mirror rendering blocked",
				report.deleteRequested, report.alreadyDeleted, report.Retained(), report.unresolved,
				report.rejectedIdentity, report.deletePending);
		}
		break;
	case F4SE::MessagingInterface::kPostLoadGame:
		{
			const bool success = message->data != nullptr;
			if (success)
				RegisterMirrorWorkshopCategory();
			const std::uint32_t discarded = success ?
				MirrorSceneRenderer::ResetMirrorGalleryTrackingForWorldChange() : 0;
			if (success)
				MirrorSceneRenderer::InstallDeferredPlayerNaturalPoseSlot();
			MirrorSceneRenderer::NotifyLoadSource(MirrorSceneRenderer::LoadSource::kSaveGame, false);
			logger::info(
				"[LoadGate] kPostLoadGame: success={} discarded-stale-gallery-tracking={}",
				success, discarded);
		}
		break;
	case F4SE::MessagingInterface::kNewGame:
		{
			RegisterMirrorWorkshopCategory();
			MirrorAuthoring::Initialize();
			MirrorSceneRenderer::InstallDeferredPlayerNaturalPoseSlot();
			const std::uint32_t discarded =
				MirrorSceneRenderer::ResetMirrorGalleryTrackingForWorldChange();
			MirrorSceneRenderer::NotifyLoadSource(MirrorSceneRenderer::LoadSource::kSaveGame, true);
			MirrorSceneRenderer::NotifyLoadSource(MirrorSceneRenderer::LoadSource::kSaveGame, false);
			logger::info(
				"[LoadGate] kNewGame: discarded-stale-gallery-tracking={}", discarded);
		}
		break;
	case F4SE::MessagingInterface::kPostPostLoad:
		MirrorMenuCompatibility::Install();
		Hooks::Install();
		break;
	case F4SE::MessagingInterface::kGameDataReady:
		{
			RegisterMirrorWorkshopCategory();

			MirrorAuthoring::Initialize();
			globals::gameDataReadyComplete.store(false, std::memory_order_release);
			
			if (!REL::Module::IsNG() || ReflectionRuntime::IsPort240())
				globals::OnDataLoaded();
			else
				globals::ReInit();
			Hooks::InstallD3DHooks();
			MirrorSceneRenderer::Install();

			if (!RegisterMirrorMenuSink())
				logger::warn("[MirrorsOfFallout] menu open/close sink unavailable; the mirror composite cannot suspend for menus");
			if (stl::hookInstallationFailed.load(std::memory_order_acquire)) {
				logger::error("[MirrorsOfFallout] initialization incomplete; required hook failed, mirror producers disabled");
				return;
			}
			globals::gameDataReadyComplete.store(true, std::memory_order_release);
			logger::info("[MirrorsOfFallout] mirror-only runtime initialization complete");
		}
		break;
	default:
		break;
	}
}

namespace
{

	bool EnbPresent() noexcept
	{
		std::array<HMODULE, 1024> modules{};
		DWORD needed = 0;
		if (!EnumProcessModules(GetCurrentProcess(), modules.data(),
				static_cast<DWORD>(modules.size() * sizeof(HMODULE)), std::addressof(needed)))
			return false;
		const auto count = std::min<std::size_t>(needed / sizeof(HMODULE), modules.size());
		for (std::size_t i = 0; i < count; ++i)
			if (modules[i] && GetProcAddress(modules[i], "ENBGetSDKVersion"))
				return true;
		return false;
	}
}

bool Load()
{
	DllTrace("Load: entered (mirror-only product)");
	if (EnbPresent()) {

		logger::info("[MirrorsOfFallout] ENB detected; initializing mirror-only rendering through the existing D3D hook chain");
	}

	auto* messaging = F4SE::GetMessagingInterface();
	if (!messaging) {
		logger::critical("[MirrorsOfFallout] F4SE messaging interface unavailable");
		return false;
	}
	messaging->RegisterListener(MessageHandler);
	globals::OnInit();
	globals::ReInit();
	Hooks::InstallEarlyHooks();
	logger::info("[MirrorsOfFallout] mirror-only bootstrap complete");
	return true;
}

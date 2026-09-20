#include "PCH.h"
#include "MirrorSettings.h"
#include "MirrorSettingsFile.h"
#include "MirrorQualityPreset.h"

#include <array>
#include <atomic>
#include <filesystem>
#include <mutex>

namespace
{
	constexpr std::uint64_t kShadowsDisabled = std::uint64_t{1} << 63;
	constexpr std::uint64_t kSmallObjectShadows = std::uint64_t{1} << 62;
	constexpr std::uint64_t kSunlightShadowsOutdoorsOnly = std::uint64_t{1} << 61;
	constexpr std::uint64_t kLimitedOutdoorRange = std::uint64_t{1} << 60;
	constexpr std::uint64_t kPlayerSunlightShadows = std::uint64_t{1} << 59;
	constexpr std::uint64_t kPlayerShadowsDisabled = std::uint64_t{1} << 58;

	constexpr std::uint64_t kDistanceMask = std::uint64_t{0xFFFF} << 16;

	constexpr std::uint64_t kLightBudgetMask = std::uint64_t{0x3FF} << 48;
	static_assert((std::uint64_t{ MirrorSettingsPolicy::kLightBudgets.back() } << 48) <= kLightBudgetMask,
		"the largest light budget must fit its packed field or it corrupts the resolution above it");

	std::atomic_uint64_t g_values{kLimitedOutdoorRange |
		(std::uint64_t{MirrorQualityPreset::kMediumSettings.resolution} << 32) |
		std::uint64_t{MirrorQualityPreset::kMediumSettings.refreshRate} |
		(std::uint64_t{MirrorQualityPreset::kMediumSettings.objectDistance} << 16) |
		(std::uint64_t{MirrorSettingsPolicy::kDefaultLightBudget} << 48)};

	std::atomic_uint32_t g_shadowDistance{MirrorQualityPreset::kMediumSettings.shadowDistance};
	
	std::atomic_uint32_t g_shadowResolution{2048}, g_shadowDetailResolution{2048}, g_minimumSunLight{15};
	std::atomic_bool g_debugKeys{false};
	std::atomic_int g_qualityMode{-1};
	std::atomic_int g_menuPresetBaseline{-1};
	std::atomic_bool g_temporaryActive{false}, g_temporaryShadows{true};
	std::atomic_bool g_lowShadowQuality{false}, g_temporaryLowShadows{false};
	std::atomic_uint32_t g_temporaryResolution{}, g_temporaryRefresh{}, g_temporaryObjectDistance{}, g_temporaryShadowDistance{};
	std::mutex g_reloadMutex;
	MirrorSettingsFile::Paths g_paths;
	MirrorSettingsMenu::Synchronization g_menuChanges;
	std::atomic_uint64_t g_menuRevision{};

	std::atomic_uint32_t g_automaticConfig{MirrorAdaptiveQuality::kDefaultTargetFPS << 1};
	std::atomic_uint64_t g_requestedQuality{};
	std::atomic_uint32_t g_automaticLevel{MirrorAdaptiveQuality::kInitialLevel};
	std::atomic_uint64_t g_captureResourceGeneration{};
	std::atomic_bool g_canPrepareCaptureResources{false};
	MirrorAdaptiveQuality::Quality AutomaticValues() noexcept
	{
		return MirrorAdaptiveQuality::At(g_automaticLevel.load(std::memory_order_acquire));
	}

	MirrorAdaptiveQuality::SceneSettings EffectiveSceneValues() noexcept
	{
		const auto packed = g_values.load(std::memory_order_acquire);
		const MirrorAdaptiveQuality::SceneSettings configured{
			(packed & kLimitedOutdoorRange) ? static_cast<std::uint32_t>((packed & kDistanceMask) >> 16) : 0u,
			g_shadowDistance.load(std::memory_order_acquire), g_shadowResolution.load(std::memory_order_acquire),
			(packed & kShadowsDisabled) == 0, g_lowShadowQuality.load(std::memory_order_acquire) };
		return MirrorSettings::AutomaticQualityAdjustment() ?
			MirrorAdaptiveQuality::ConstrainScene(AutomaticValues(), configured) : configured;
	}

	void ReadSettings()
	{
		if (g_paths.defaults.empty()) return;
		const auto baseline = g_menuPresetBaseline.load(std::memory_order_acquire);
		if (baseline >= 0 && MirrorSettingsFile::ReadInt(g_paths, L"bAutomaticQuality", 1) != baseline) {
			std::error_code error;
			std::filesystem::create_directories(g_paths.user.parent_path(), error);
			if (error || !WritePrivateProfileStringW(L"Mirrors", L"bAutomaticQuality", baseline ? L"1" : L"0", g_paths.user.c_str()))
				logger::warn("[MirrorSettings] could not save Automatic's scene-settings baseline");
		}
		const auto values = MirrorSettingsFile::Read(g_paths);
		if (values.qualityMode != MirrorQualityMode::Mode::Automatic)
			g_menuPresetBaseline.store(values.qualityMode == MirrorQualityMode::Mode::Preset, std::memory_order_release);
		const bool modeChanged = g_qualityMode.exchange(static_cast<int>(values.qualityMode), std::memory_order_acq_rel) !=
			static_cast<int>(values.qualityMode);
		if (modeChanged)
			logger::info("[MirrorSettings] quality mode={}; scene settings={}", MirrorQualityMode::Name(values.qualityMode),
				values.automaticQuality ? MirrorQualityPreset::Name(values.qualityLevel) : "manual");
		const bool lowShadows = values.automaticQuality && values.qualityLevel == MirrorQualityPreset::Level::kLow;
		if (g_lowShadowQuality.exchange(lowShadows, std::memory_order_acq_rel) != lowShadows)
			logger::info("[MirrorSettings] Low shadow policy={}: sunlight map capped at 1024, no separate player-detail map; player receives enabled shadows", lowShadows);
		if (g_debugKeys.exchange(values.debugKeys, std::memory_order_acq_rel) != values.debugKeys)
			logger::info("[MirrorSettings] debug keys {} (F7 optimisations, F8 benchmark, F11 performance overlay, F12 reflections on/off)",
				values.debugKeys ? "ON" : "OFF");
		const auto automaticConfig = (values.automaticTargetFPS << 1) | unsigned(values.automaticQualityAdjustment);
		const bool automaticChanged = g_automaticConfig.exchange(automaticConfig, std::memory_order_acq_rel) != automaticConfig;
		if (automaticChanged) {
			g_requestedQuality.store(0, std::memory_order_release);
			g_automaticLevel.store(MirrorAdaptiveQuality::kInitialLevel, std::memory_order_release);
			logger::info("[MirrorSettings] automatic adjustment={} target={} FPS; 2048/60 -> 1024/60 -> 1024/30 -> 512/30; ineffective resolution reductions restored (2048/30 fallback)",
				values.automaticQualityAdjustment, values.automaticTargetFPS);
		}
		const auto packed = (std::uint64_t{ values.resolution } << 32) | values.refreshRate |
			(std::uint64_t{values.outdoorDistance} << 16) |
			(std::uint64_t{values.lightBudget} << 48) |
			(values.shadows ? 0 : kShadowsDisabled) | (values.smallObjectShadows ? kSmallObjectShadows : 0) |
			(values.sunlightShadowsOutdoorsOnly ? kSunlightShadowsOutdoorsOnly : 0) |
			(values.limitedOutdoorRange ? kLimitedOutdoorRange : 0) |
			(values.playerSunlightShadows ? kPlayerSunlightShadows : 0) |
			(values.playerShadows ? 0 : kPlayerShadowsDisabled);
		const bool distanceChanged =
			g_shadowDistance.exchange(values.shadowDistance, std::memory_order_acq_rel) != values.shadowDistance;

		const bool resolutionChanged =
			g_shadowResolution.exchange(values.shadowResolution, std::memory_order_acq_rel) != values.shadowResolution;
		const bool detailChanged =
			g_shadowDetailResolution.exchange(values.shadowDetailResolution, std::memory_order_acq_rel) != values.shadowDetailResolution;
		const bool minimumChanged =
			g_minimumSunLight.exchange(values.minimumSunLight, std::memory_order_acq_rel) != values.minimumSunLight;
		const bool sunChanged = resolutionChanged || detailChanged || minimumChanged;
		if (sunChanged)
			logger::info("[MirrorSettings] sunlight maps: wide={} detail={} minimumSunLight={}%; player shadows follow the master",
				values.shadowResolution, values.shadowDetailResolution, values.minimumSunLight);
		if (g_values.exchange(packed, std::memory_order_acq_rel) != packed || distanceChanged || sunChanged)
			logger::info("[MirrorSettings] MCM applied quality={} ({}) resolution={} refresh={} Hz shadows={} smallObjectShadows={} sunlightShadowsOutdoorsOnly={} limitedOutdoorRange={} lightBudget={} outdoorDistance={} playerSunlightShadows={} everyFrame={} shadowDistance={} (0=original resolution/refresh defaults unless everyFrame=true)",
				values.automaticQuality ? MirrorQualityPreset::Name(values.qualityLevel) : "manual",
				MirrorQualityMode::Name(values.qualityMode),
				values.resolution, values.refreshRate == MirrorSettingsPolicy::kEveryFrame ? 0u : values.refreshRate,
				values.shadows, values.smallObjectShadows, values.sunlightShadowsOutdoorsOnly,
				values.limitedOutdoorRange, values.lightBudget, values.outdoorDistance, values.playerSunlightShadows,
				values.refreshRate == MirrorSettingsPolicy::kEveryFrame, values.shadowDistance);
	}
}

	namespace MirrorSettings
{
	void Initialize()
	{
		std::lock_guard lock(g_reloadMutex);
		std::array<wchar_t, 32768> executable{};
		const auto length = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
		if (length == 0 || length >= executable.size()) {
			logger::warn("[MirrorSettings] game path unavailable; original mirror defaults retained");
			return;
		}
		const auto data = std::filesystem::path(executable.data()).parent_path() / L"Data";
		g_paths = { data / L"MCM/Config/MirrorsOfFallout/settings.ini", data / L"MCM/Settings/MirrorsOfFallout.ini" };
		ReadSettings();
		const auto refresh = RefreshRateOverride();
		logger::info("[MirrorSettings] MCM ready: resolution={} refresh={} Hz shadows={} everyFrame={}; settings apply on menu close",
			ResolutionOverride(), refresh == MirrorSettingsPolicy::kEveryFrame ? 0u : refresh, ShadowsEnabled(),
			refresh == MirrorSettingsPolicy::kEveryFrame);
	}

	void Reload()
	{

		std::lock_guard lock(g_reloadMutex);
		ReadSettings();
	}

	bool ReadMenuValues(MirrorSettingsMenu::Values& values)
	{
		std::lock_guard lock(g_reloadMutex);
		if (g_paths.defaults.empty()) return false;
		values = MirrorSettingsFile::ReadMenu(g_paths);
		return true;
	}
	bool SaveMenuValues(const MirrorSettingsMenu::Values& values, std::uint32_t changed)
	{
		if (!changed) return true;
		if (changed & (1u << MirrorSettingsMenu::Mode)) changed |= 1u << MirrorSettingsMenu::PresetBaseline;
		std::lock_guard lock(g_reloadMutex);
		if (g_paths.defaults.empty() || !MirrorSettingsFile::WriteMenu(g_paths, values, changed)) {
			logger::warn("[MirrorSettings] Menu Framework preferences could not be saved");
			return false;
		}
		if (changed & ((1u << MirrorSettingsMenu::Mode) | (1u << MirrorSettingsMenu::PresetBaseline)))
			g_menuPresetBaseline.store(values[MirrorSettingsMenu::PresetBaseline], std::memory_order_release);
		ReadSettings();
		g_menuChanges.values = MirrorSettingsFile::ReadMenu(g_paths);
		g_menuChanges.changed |= changed;
		g_menuChanges.revision = g_menuRevision.load(std::memory_order_relaxed) + 1;
		g_menuRevision.store(g_menuChanges.revision, std::memory_order_release);
		return true;
	}
	std::uint64_t MenuRevision() noexcept { return g_menuRevision.load(std::memory_order_acquire); }
	MirrorSettingsMenu::Synchronization MenuChanges()
	{
		std::lock_guard lock(g_reloadMutex);
		return g_menuChanges;
	}

	int QualityModeForMenu() noexcept { return g_qualityMode.load(std::memory_order_acquire); }
	void SelectQualityModeForMenu(int mode) noexcept
	{
		if (mode == 1 || mode == 2) g_menuPresetBaseline.store(mode == 1, std::memory_order_release);
	}
	bool AutomaticQualityAdjustment() noexcept
	{
		return !g_temporaryActive.load(std::memory_order_acquire) &&
			(g_automaticConfig.load(std::memory_order_acquire) & 1u) != 0;
	}
	bool SpatialCadenceEnabled() noexcept
	{
		const auto mode = g_qualityMode.load(std::memory_order_acquire);
		return !TemporaryValuesActive() && (mode == static_cast<int>(MirrorQualityMode::Mode::Automatic) ||
			mode == static_cast<int>(MirrorQualityMode::Mode::Preset));
	}
	void SetTemporaryValues(const TemporaryValues* values) noexcept
	{
		if (!values) {
			g_temporaryActive.store(false, std::memory_order_release);
			return;
		}
		g_temporaryActive.store(false, std::memory_order_release);
		g_temporaryResolution.store(values->resolution, std::memory_order_release);
		g_temporaryRefresh.store(values->refreshRate, std::memory_order_release);
		g_temporaryObjectDistance.store(values->objectDistance, std::memory_order_release);
		g_temporaryShadowDistance.store(values->shadowDistance, std::memory_order_release);
		g_temporaryShadows.store(values->shadows, std::memory_order_release);
		g_temporaryLowShadows.store(values->lowShadowQuality, std::memory_order_release);
		g_temporaryActive.store(true, std::memory_order_release);
	}
	bool TemporaryValuesActive() noexcept { return g_temporaryActive.load(std::memory_order_acquire); }
	unsigned AutomaticQualityLevel() noexcept { return g_automaticLevel.load(std::memory_order_acquire); }
	void CaptureResourcesChanged() noexcept { g_captureResourceGeneration.fetch_add(1u, std::memory_order_release); }
	bool CanPrepareCaptureResources() noexcept { return g_canPrepareCaptureResources.load(std::memory_order_acquire); }
	void ObserveQualityFrame(double nowMs, bool worldActive, std::uint64_t publications,
		std::uint64_t sceneCaptures, std::uint64_t sceneNodes, std::uint64_t bodyFaults) noexcept
	{
		static MirrorAdaptiveQuality::Controller controller;
		static unsigned previousConfig = 0;
		const auto config = g_automaticConfig.load(std::memory_order_acquire);

		static double priorMs = -1, prepareMs = 0;
		static unsigned prepareFrames = 0;
		const auto elapsed = nowMs - priorMs;
		priorMs = nowMs;
		if (!worldActive || elapsed <= 0 || elapsed > 100 || !std::isfinite(elapsed)) {
			prepareMs = 0; prepareFrames = 0;
			g_canPrepareCaptureResources.store(false, std::memory_order_release);
		} else {
			prepareMs += elapsed; ++prepareFrames;
			if (prepareMs >= 500) {
				g_canPrepareCaptureResources.store(prepareMs / prepareFrames <= 1.05 * 1000.0 / (config >> 1), std::memory_order_release);
				prepareMs = 0; prepareFrames = 0;
			}
		}
		if (config != previousConfig) {
			controller.Reset(config >> 1);
			previousConfig = config;
		}
		if (!(config & 1u)) return;
		const auto evidenceSerial = controller.EvidenceSerial();
		const auto level = controller.Observe(nowMs, worldActive && !TemporaryValuesActive(), publications,
			g_captureResourceGeneration.load(std::memory_order_acquire), sceneCaptures, sceneNodes, bodyFaults);
		if (controller.EvidenceSerial() != evidenceSerial) {
			const auto& e = controller.LastEvidence();
			logger::info("[MirrorSettings] automatic trial evidence: mean={:.2f}->{:.2f}ms p95={:.2f}->{:.2f}ms sceneNodesPerCapture={:.1f}->{:.1f} comparable={}",
				e.baselineMean, e.mean, e.baselineP95, e.p95, e.baselineWork, e.work, e.comparable);
		}
		g_requestedQuality.store((std::uint64_t{config} << 32) | level |
			(static_cast<std::uint64_t>(controller.LastDecision()) << 8), std::memory_order_release);
	}
	void ApplyAutomaticQuality() noexcept
	{
		const auto config = g_automaticConfig.load(std::memory_order_acquire);
		const auto request = g_requestedQuality.load(std::memory_order_acquire);
		if (!(config & 1u) || (request >> 32) != config || TemporaryValuesActive()) return;
		const auto level = static_cast<unsigned>(request & 0xFFu);
		const auto decision = static_cast<MirrorAdaptiveQuality::Decision>((request >> 8) & 0xFFu);
		static auto previousDecision = MirrorAdaptiveQuality::Decision::Initial;
		if (g_automaticLevel.exchange(level, std::memory_order_acq_rel) != level || previousDecision != decision) {
			const auto quality = MirrorAdaptiveQuality::At(level);
			logger::info("[MirrorSettings] automatic resolution={} refresh={} Hz target={} FPS; {}; lowSceneCost={} objectDistance={} shadowDistance={}; nearby moving mirror follows game frames",
				quality.resolution, quality.refreshRate, config >> 1, MirrorAdaptiveQuality::Name(decision),
				quality.lowSceneCost, OutdoorDistance(), ShadowDistance());
		}
		previousDecision = decision;
	}
	std::uint32_t ResolutionOverride() noexcept { if (g_temporaryActive.load(std::memory_order_acquire)) return g_temporaryResolution.load(std::memory_order_acquire); return AutomaticQualityAdjustment() ? AutomaticValues().resolution : static_cast<std::uint32_t>((g_values.load(std::memory_order_acquire) & ~(kShadowsDisabled | kSmallObjectShadows | kSunlightShadowsOutdoorsOnly | kLimitedOutdoorRange | kPlayerSunlightShadows | kPlayerShadowsDisabled | kLightBudgetMask)) >> 32); }
	std::uint32_t RefreshRateOverride() noexcept { if (g_temporaryActive.load(std::memory_order_acquire)) return g_temporaryRefresh.load(std::memory_order_acquire); return AutomaticQualityAdjustment() ? AutomaticValues().refreshRate : static_cast<std::uint32_t>(g_values.load(std::memory_order_acquire) & 0xFFFFu); }
	bool ShadowsEnabled() noexcept { if (g_temporaryActive.load(std::memory_order_acquire)) return g_temporaryShadows.load(std::memory_order_acquire); return EffectiveSceneValues().shadows; }
	bool ShadowRenderingEnabled() noexcept { return LowShadowQuality() || ShadowsEnabled(); }
	bool PlayerSunlightShadowsEnabled() noexcept { return ShadowRenderingEnabled(); }
	bool PlayerShadowsEnabled() noexcept { return ShadowRenderingEnabled(); }
	bool DebugKeysEnabled() noexcept { return g_debugKeys.load(std::memory_order_acquire); }
	bool SmallObjectShadowsEnabled() noexcept { return (g_values.load(std::memory_order_acquire) & kSmallObjectShadows) != 0; }
	bool SunlightShadowsOutdoorsOnly() noexcept { return (g_values.load(std::memory_order_acquire) & kSunlightShadowsOutdoorsOnly) != 0; }
	std::uint32_t OutdoorDistance() noexcept
	{
		if (g_temporaryActive.load(std::memory_order_acquire))
			return REL::Module::IsVR() ? 0u : g_temporaryObjectDistance.load(std::memory_order_acquire);
		
		return REL::Module::IsVR() ? 0u : EffectiveSceneValues().objectDistance;
	}
	std::uint32_t ShadowDistance() noexcept { if (g_temporaryActive.load(std::memory_order_acquire)) return g_temporaryShadowDistance.load(std::memory_order_acquire); return EffectiveSceneValues().shadowDistance; }
	bool LowShadowQuality() noexcept { return g_temporaryActive.load(std::memory_order_acquire) ? g_temporaryLowShadows.load(std::memory_order_acquire) : EffectiveSceneValues().lowShadows; }
	std::uint32_t ShadowResolution() noexcept { const auto size=EffectiveSceneValues().shadowResolution; return LowShadowQuality() ? (std::min)(size,1024u) : size; }
	std::uint32_t ShadowDetailResolution() noexcept { return g_shadowDetailResolution.load(std::memory_order_acquire); }
	std::uint32_t MinimumSunLight() noexcept { return g_minimumSunLight.load(std::memory_order_acquire); }
	std::uint32_t CaptureSize(std::uint32_t authored) noexcept { const auto size = ResolutionOverride(); return size ? size : authored; }
	std::uint32_t LightBudget() noexcept
	{
		return MirrorSettingsPolicy::LightBudget(
			static_cast<std::uint32_t>((g_values.load(std::memory_order_acquire) & kLightBudgetMask) >> 48));
	}
	std::uint64_t CaptureInterval(std::uint64_t fallback) noexcept { return MirrorSettingsPolicy::Interval(RefreshRateOverride(), fallback); }
	std::uint64_t PublicationLifetime() noexcept { return MirrorSettingsPolicy::PublicationLifetime(RefreshRateOverride()); }
}
